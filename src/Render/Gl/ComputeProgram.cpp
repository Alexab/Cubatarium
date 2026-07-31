#include "Render/Gl/ComputeProgram.h"
#include "Render/GlIncludes.h"
#include "glog/logging.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace cutum
{
namespace
{

std::string ReadFileText(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return {};
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool CompileAndLink(const char *source, GLuint &out_program)
{
  out_program = 0;
  const GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
  glShaderSource(sh, 1, &source, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[1024];
    glGetShaderInfoLog(sh, 1024, nullptr, log);
    LOG(WARNING) << "[ComputeProgram] compile failed: " << log;
    glDeleteShader(sh);
    return false;
  }
  const GLuint prog = glCreateProgram();
  glAttachShader(prog, sh);
  glLinkProgram(prog);
  glDeleteShader(sh);
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[1024];
    glGetProgramInfoLog(prog, 1024, nullptr, log);
    LOG(WARNING) << "[ComputeProgram] link failed: " << log;
    glDeleteProgram(prog);
    return false;
  }
  out_program = prog;
  return true;
}

} // namespace

UComputeProgram::~UComputeProgram() { Destroy(); }

void UComputeProgram::Destroy()
{
  if (ProgramId != 0)
  {
    glDeleteProgram(ProgramId);
    ProgramId = 0;
  }
}

bool UComputeProgram::CompileSource(const char *source)
{
  Destroy();
  if (!source || !*source)
  {
    return false;
  }
  GLuint prog = 0;
  if (!CompileAndLink(source, prog))
  {
    return false;
  }
  ProgramId = prog;
  return true;
}

bool UComputeProgram::CompileForCaps(const RenderBackendCaps &caps,
                                     const std::string &desktop_path,
                                     const std::string &gles_path)
{
  const bool use_gles = caps.Platform == RenderPlatformKind::Android;
  const std::string &path = use_gles ? gles_path : desktop_path;
  std::string src = ReadFileText(path);
  if (src.empty())
  {
    // Fallback: try alternate relative roots used by desktop vs APK assets.
    const std::string alt =
        use_gles ? ("assets/" + gles_path) : ("assets/" + desktop_path);
    src = ReadFileText(alt);
  }
  if (src.empty())
  {
    LOG(WARNING) << "[ComputeProgram] missing shader file: " << path;
    return false;
  }
  return CompileSource(src.c_str());
}

} // namespace cutum
