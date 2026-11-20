#include "World/Lighting/GpuLightTransport.h"
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include <array>
#include <deque>
#include <iostream>
#include <stdexcept>

using cutum::LightTransportInput;
namespace
{
void Require(bool ok, const char *message)
{ if (!ok) throw std::runtime_error(message); }
size_t Index(const LightTransportInput &in, int x, int y, int z)
{ return size_t(z) * in.width * in.height + size_t(y) * in.width + x; }

// Independent work-list oracle: no production solver or GPU pass scheduling.
std::vector<uint8_t> Reference(const LightTransportInput &in)
{
  auto result = in.seeds;
  constexpr int directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
  for (int channel = 0; channel < 2; ++channel)
  {
    const int shift = channel * 4;
    std::deque<std::array<int,3>> queue;
    for (int z=0; z<int(in.depth); ++z)
      for (int y=0; y<int(in.height); ++y)
        for (int x=0; x<int(in.width); ++x)
          if (((result[Index(in,x,y,z)]>>shift)&15)>1) queue.push_back({x,y,z});
    while (!queue.empty())
    {
      const auto p=queue.front(); queue.pop_front();
      const int level=(result[Index(in,p[0],p[1],p[2])]>>shift)&15;
      for (const auto &d:directions)
      {
        const int x=p[0]+d[0], y=p[1]+d[1], z=p[2]+d[2];
        if (x<0||y<0||z<0||x>=int(in.width)||y>=int(in.height)||z>=int(in.depth)) continue;
        const size_t i=Index(in,x,y,z);
        if (!in.transmission[i]) continue;
        const int candidate=level-(channel==0 && in.transmission[i]==2?2:1);
        if (candidate>((result[i]>>shift)&15))
        {
          result[i]=uint8_t((result[i]&~(15<<shift))|(candidate<<shift));
          if (candidate>1) queue.push_back({x,y,z});
        }
      }
    }
  }
  return result;
}

LightTransportInput Scene(int phase)
{
  LightTransportInput in;
  in.width=35; in.height=39; in.depth=33;
  in.seeds.resize(size_t(in.width)*in.height*in.depth);
  in.transmission.assign(in.seeds.size(),1);
  for (int z=0;z<int(in.depth);++z)
    for (int x=0;x<int(in.width);++x)
    {
      in.transmission[Index(in,x,0,z)]=0;
      if (phase<3 && x>3 && x<31 && z>3 && z<29)
        in.transmission[Index(in,x,28,z)]=0;
      if (phase<3 && x>=18 && x<=20)
        for (int y=8;y<14;++y) in.transmission[Index(in,x,y,z)]=2;
    }
  if (phase==2)
    for (int y=1;y<28;++y)
      for (int z=0;z<int(in.depth);++z) in.transmission[Index(in,17,y,z)]=0;
  // Fixed full-height direct sky for this finite test domain, independent of
  // production seed preparation. GPU must perform all lateral transport.
  for (int z=0;z<int(in.depth);++z)
    for (int x=0;x<int(in.width);++x)
    {
      int sky=15;
      for (int y=int(in.height)-1;y>=0;--y)
      {
        const auto i=Index(in,x,y,z);
        if (!in.transmission[i]) sky=0;
        in.seeds[i]=uint8_t(sky);
        if (in.transmission[i]==2 && sky) --sky;
      }
    }
  if (phase==0 || phase==2)
  {
    const auto source=Index(in,15,12,16);
    in.transmission[source]=0; // Opaque emissive cell still emits to six faces.
    in.seeds[source]=uint8_t((phase==0?14:6)<<4);
  }
  if (phase<3) in.seeds[Index(in,31,20,2)]|=11<<4;
  return in;
}
}

int main()
{
  auto in=Scene(0);
  std::vector<uint8_t> result{0xab,0xcd};
  cutum::GpuLightTransportWork work;
  std::string reason;
  if (cutum::TryGpuTransportLight(in,result,work,reason) ||
      result!=std::vector<uint8_t>({0xab,0xcd}) || work.dispatches)
    return 1;
  if (!glfwInit()) return 77;
  glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
  auto *window=glfwCreateWindow(32,32,"GPU light transport",nullptr,nullptr);
  if (!window) { glfwTerminate(); return 77; }
  glfwMakeContextCurrent(window); glewExperimental=GL_TRUE;
  if (glewInit()!=GLEW_OK) { glfwDestroyWindow(window); glfwTerminate(); return 77; }
  while(glGetError()!=GL_NO_ERROR) {}
  std::cout<<"GPU: "<<glGetString(GL_RENDERER)<<" GL: "<<glGetString(GL_VERSION)<<'\n';
  int status=0;
  try
  {
    GLuint sentinels[4]{}; glGenBuffers(4,sentinels);
    GLint alignment=0; glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,&alignment);
    Require(alignment>0,"valid SSBO alignment");
    for (unsigned i=0;i<4;++i)
    {
      glBindBuffer(GL_SHADER_STORAGE_BUFFER,sentinels[i]);
      glBufferData(GL_SHADER_STORAGE_BUFFER,alignment*2,nullptr,GL_STATIC_DRAW);
      glBindBufferRange(GL_SHADER_STORAGE_BUFFER,i,sentinels[i],alignment,alignment);
    }
    for (int phase=0;phase<4;++phase)
    {
      in=Scene(phase); const auto expected=Reference(in);
      Require(cutum::TryGpuTransportLight(in,result,work,reason),reason.c_str());
      Require(work.dispatches==15 && work.readbacks==1 &&
              work.scratch_bytes==in.seeds.size()*16,"actual dispatch/readback/bounded bytes");
      Require(result==expected,"all GPU cells match independent work-list oracle");
      for (unsigned i=0;i<4;++i)
      {
        GLint buffer=0; GLint64 start=0,size=0;
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING,i,&buffer);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START,i,&start);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE,i,&size);
        Require(GLuint(buffer)==sentinels[i] && start==alignment && size==alignment,
                "GPU transport restores indexed SSBO ranges");
      }
      if (phase==0)
      {
        const int faces[6][3]={{16,12,16},{14,12,16},{15,13,16},{15,11,16},{15,12,17},{15,12,15}};
        for(const auto &p:faces) Require((result[Index(in,p[0],p[1],p[2])]>>4)==13,"opaque emitter crosses all six faces");
      }
      if (phase==3)
        for(const auto packed:result) Require((packed>>4)==0,"source removal leaves no stale blocklight");
      std::cout<<"phase="<<phase<<" cells="<<result.size()<<" dispatches="<<work.dispatches<<" readbacks="<<work.readbacks<<" PASS\n";
    }
    const auto before=result;
    in.transmission[0]=255;
    Require(!cutum::TryGpuTransportLight(in,result,work,reason) &&
                result==before && work.dispatches==0,"invalid inputs fail without dispatch/output mutation");
    for(unsigned i=0;i<4;++i) glBindBufferBase(GL_SHADER_STORAGE_BUFFER,i,0);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER,0); glDeleteBuffers(4,sentinels);
    Require(glGetError()==GL_NO_ERROR,"GPU transport leaves no GL errors");
  }
  catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; status=1; }
  glfwDestroyWindow(window); glfwTerminate();
  return status;
}
