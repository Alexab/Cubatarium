#include "World/Lighting/GpuLightTransport.h"
#include "Render/GlIncludes.h"
#include <array>
#include <chrono>
#include <limits>

namespace cutum
{
bool TryGpuTransportLight(const LightTransportInput &input,
                           std::vector<uint8_t> &output,
                           GpuLightTransportWork &work, std::string &reason)
{
  work = {};
  reason.clear();
  const uint64_t count = uint64_t(input.width) * input.height;
  if (!input.width || !input.height || !input.depth || count > (1u << 20) ||
      count * input.depth > (1u << 20) ||
      input.seeds.size() != count * input.depth ||
      input.transmission.size() != input.seeds.size())
  {
    reason = "invalid_or_over_budget_domain";
    return false;
  }
  for (const auto material : input.transmission)
    if (material > 2) { reason = "invalid_transmission"; return false; }
#if defined(__ANDROID__) || defined(CUBATARIUM_GLES)
  reason = "desktop_compute_unavailable";
  return false;
#else
  if (!glGetString(GL_VERSION) || !GLEW_VERSION_4_3 || !glDispatchCompute)
  {
    reason = "no_current_gl43_context";
    return false;
  }
  const auto cells = static_cast<uint32_t>(input.seeds.size());
  const GLsizeiptr bytes = GLsizeiptr(cells) * sizeof(uint32_t);
  GLint64 max_block = 0;
  glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_block);
  if (bytes > max_block) { reason = "ssbo_capacity"; return false; }
  struct Resources
  {
    GLint old_program{0}, old_buffer{0};
    std::array<GLint, 4> bindings{};
    std::array<GLint64, 4> starts{}, sizes{};
    std::array<GLuint, 4> buffers{};
    GLuint program{0}, shader{0};
    GLsync fence{nullptr};
    Resources()
    {
      glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
      glGetIntegerv(GL_SHADER_STORAGE_BUFFER_BINDING, &old_buffer);
      for (GLuint i = 0; i < 4; ++i)
      {
        glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, i, &bindings[i]);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_START, i, &starts[i]);
        glGetInteger64i_v(GL_SHADER_STORAGE_BUFFER_SIZE, i, &sizes[i]);
      }
    }
    ~Resources()
    {
      glUseProgram(old_program);
      for (GLuint i = 0; i < 4; ++i)
        if (bindings[i] && sizes[i] > 0)
          glBindBufferRange(GL_SHADER_STORAGE_BUFFER, i, bindings[i], starts[i], sizes[i]);
        else glBindBufferBase(GL_SHADER_STORAGE_BUFFER, i, bindings[i]);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, old_buffer);
      if (fence) glDeleteSync(fence);
      glDeleteBuffers(4, buffers.data());
      if (program) glDeleteProgram(program);
      if (shader) glDeleteShader(shader);
    }
  } resources;
  constexpr const char *source = R"(#version 430 core
layout(local_size_x=128) in;
layout(std430,binding=0) readonly buffer Seed { uint seeds[]; };
layout(std430,binding=1) readonly buffer Material { uint material[]; };
layout(std430,binding=2) readonly buffer Previous { uint previous[]; };
layout(std430,binding=3) writeonly buffer Next { uint next_light[]; };
uniform uvec3 shape;
uint at(ivec3 p) { return uint(p.x)+shape.x*(uint(p.y)+shape.y*uint(p.z)); }
void main() {
 uint i=gl_GlobalInvocationID.x;
 if(i>=shape.x*shape.y*shape.z) return;
 uint sky=seeds[i]&15u, block=seeds[i]>>4u;
 if(material[i]!=0u) {
  ivec3 p=ivec3(i%shape.x,(i/shape.x)%shape.y,i/(shape.x*shape.y));
  const ivec3 delta[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));
  uint cost=material[i]==2u?2u:1u;
  for(int n=0;n<6;++n) {
   ivec3 q=p+delta[n];
   if(any(lessThan(q,ivec3(0)))||any(greaterThanEqual(q,ivec3(shape)))) continue;
   uint light=previous[at(q)], s=light&15u, b=light>>4u;
   sky=max(sky,s>cost?s-cost:0u);
   block=max(block,b>1u?b-1u:0u);
  }
 }
 next_light[i]=sky|(block<<4u);
}
)";
  resources.shader = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(resources.shader, 1, &source, nullptr);
  glCompileShader(resources.shader);
  GLint ok = 0;
  glGetShaderiv(resources.shader, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[2048]{}; glGetShaderInfoLog(resources.shader, sizeof(log), nullptr, log);
    reason = std::string("compute_compile: ") + log; return false;
  }
  resources.program = glCreateProgram();
  glAttachShader(resources.program, resources.shader);
  glLinkProgram(resources.program);
  glGetProgramiv(resources.program, GL_LINK_STATUS, &ok);
  if (!ok) { reason = "compute_link"; return false; }
  std::vector<uint32_t> seeds(input.seeds.begin(), input.seeds.end());
  std::vector<uint32_t> material(input.transmission.begin(), input.transmission.end());
  glGenBuffers(4, resources.buffers.data());
  for (GLuint i = 0; i < 4; ++i)
  {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.buffers[i]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes,
        i == 1 ? material.data() : seeds.data(), GL_STREAM_DRAW);
  }
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, resources.buffers[0]);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, resources.buffers[1]);
  glUseProgram(resources.program);
  glUniform3ui(glGetUniformLocation(resources.program, "shape"), input.width, input.height, input.depth);
  work.scratch_bytes = uint64_t(bytes) * 4;
  // Max light 15, each edge costs >=1: 14 hops suffice; pass 15 settles all
  // positive paths and starts from immutable seeds on every new calculation.
  for (unsigned pass = 0; pass < 15; ++pass)
  {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, resources.buffers[2 + (pass & 1)]);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, resources.buffers[3 - (pass & 1)]);
    glDispatchCompute((cells + 127) / 128, 1, 1);
    ++work.dispatches;
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
  }
  resources.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  if (!resources.fence) { reason = "compute_fence"; return false; }
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (;;)
  {
    const auto wait = glClientWaitSync(resources.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 10000000);
    if (wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED) break;
    if (wait == GL_WAIT_FAILED || std::chrono::steady_clock::now() >= deadline)
    { reason = "compute_wait_failed_or_timeout"; return false; }
  }
  std::vector<uint32_t> readback(cells);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, resources.buffers[3]);
  glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, bytes, readback.data());
  ++work.readbacks;
  if (glGetError() != GL_NO_ERROR) { reason = "compute_gl_error"; return false; }
  std::vector<uint8_t> completed; completed.reserve(cells);
  for (const auto packed : readback)
  {
    if (packed > 255) { reason = "compute_invalid_packed_light"; return false; }
    completed.push_back(static_cast<uint8_t>(packed));
  }
  output.swap(completed);
  return true;
#endif
}
}
