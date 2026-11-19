#pragma once

#include "Render/Lighting/SectionLightBindingTable.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace cutum
{
inline bool BoundSectionLightProbeRequested()
{
  const auto *value = std::getenv("CUBA_RGB_REFERENCE");
  return value && std::string(value) == "1";
}
// Diagnostic only: copied while the actual direct-draw handle and texture
// bindings are live, including the transient ordered-fallback page.
struct BoundSectionLightCapture
{
  bool captured{false}, valid{false}, cpu_matches{false};
  uint32_t program{0}, atlas_texture{0}, table_texture{0};
  std::array<uint32_t, 4> handle{};
  uint64_t frame_epoch{0}, world_epoch{0}, field_incarnation{0},
      field_revision{0};
  uint64_t geometry_incarnation{0}, geometry_content{0};
  glm::ivec3 origin{0};
  SectionLightVolume data;

  std::optional<uint8_t> SampleSurface(glm::vec3 point, int face) const
  {
    if (!valid || face < 0 || face > 5)
      return std::nullopt;
    glm::ivec3 cell;
    for (int a = 0; a < 3; ++a)
    {
      if (!std::isfinite(point[a]) || point[a] < -2147483000.0f ||
          point[a] > 2147483000.0f)
        return std::nullopt;
      cell[a] = int(std::floor(point[a] + .5f));
    }
    const int axis =
        (face == 0 || face == 2) ? 2 : ((face == 1 || face == 3) ? 0 : 1);
    cell[axis] = int(std::floor(point[axis] + 1.0f)) -
                 ((face == 2 || face == 3 || face == 5) ? 1 : 0);
    glm::ivec3 local;
    for (int a = 0; a < 3; ++a)
    {
      const int64_t delta = int64_t(cell[a]) - origin[a];
      if (delta < -SectionLightVolume::Radius ||
          delta >= CHUNK_SIZE + SectionLightVolume::Radius)
        return std::nullopt;
      local[a] = int(delta);
    }
    return data.cells[SectionLightVolume::Index(local.x, local.y, local.z)];
  }
};

class BoundSectionLightProbe
{
public:
  ~BoundSectionLightProbe() { Reset(); }
  void Reset()
  {
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    if (Program)
      glDeleteProgram(Program);
    if (Texture)
      glDeleteTextures(1, &Texture);
    if (Framebuffer)
      glDeleteFramebuffers(1, &Framebuffer);
    if (Vao)
      glDeleteVertexArrays(1, &Vao);
    Program = Texture = Framebuffer = Vao = 0;
#endif
  }
  BoundSectionLightCapture
  Capture(uint32_t expected_program,
          const std::array<uint32_t, 4> &expected_handle, glm::ivec3 origin,
          const SectionLightVolume &expected)
  {
    BoundSectionLightCapture result;
    result.origin = origin;
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
    State state;
    result.captured = true;
    result.program = state.program;
    const auto location = [&](const char *name)
    { return glGetUniformLocation(state.program, name); };
    const auto integer = [&](const char *name, GLint &value)
    {
      const auto at = location(name);
      if (at < 0)
        return false;
      glGetUniformiv(state.program, at, &value);
      return true;
    };
    GLint enabled = 0, table_mode = 0, atlas_unit = -1, table_unit = -1,
          max_units = 0;
    glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &max_units);
    if (uint32_t(state.program) != expected_program || !state.program ||
        !integer("uUseSurfaceSectionLight", enabled) || enabled != 1 ||
        !integer("uUseSurfaceSectionLightDrawTable", table_mode) ||
        table_mode != 0 || !integer("uSectionLightAtlas", atlas_unit) ||
        !integer("uSectionLightBindings", table_unit) || atlas_unit < 0 ||
        table_unit < 0 || atlas_unit >= max_units || table_unit >= max_units)
      return result;
    const auto handle_at = location("uSurfaceSectionLightHandle");
    const auto incarnation_at = location("uSectionLightTableIncarnation");
    if (handle_at < 0 || incarnation_at < 0)
      return result;
    std::array<GLuint, 2> incarnation{};
    glGetUniformuiv(state.program, handle_at, result.handle.data());
    glGetUniformuiv(state.program, incarnation_at, incarnation.data());
    if (result.handle != expected_handle ||
        !incarnation[0] && !incarnation[1] ||
        result.handle[2] != incarnation[0] ||
        result.handle[3] != incarnation[1])
      return result;
    GLint texture = 0;
    glActiveTexture(GL_TEXTURE0 + atlas_unit);
    glGetIntegerv(GL_TEXTURE_BINDING_3D, &texture);
    result.atlas_texture = texture;
    glActiveTexture(GL_TEXTURE0 + table_unit);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    result.table_texture = texture;
    if (!result.atlas_texture || !result.table_texture || !Initialize())
      return result;
    // Initialize temporarily binds a 2D output texture on unit0. Restore it
    // before querying the source: even a faulty source sampler on unit0 must
    // observe its real binding, never this probe's output attachment.
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state.texture0);
    glUseProgram(Program);
    glUniform1i(glGetUniformLocation(Program, "uSectionLightAtlas"),
                atlas_unit);
    glUniform1i(glGetUniformLocation(Program, "uSectionLightBindings"),
                table_unit);
    glUniform2uiv(
        glGetUniformLocation(Program, "uSectionLightTableIncarnation"), 1,
        incarnation.data());
    glUniform4uiv(glGetUniformLocation(Program, "probeHandle"), 1,
                  result.handle.data());
    glUniform3i(glGetUniformLocation(Program, "probeOrigin"), origin.x,
                origin.y, origin.z);
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
    glViewport(0, 0, 100, 80);
    for (const auto mode : State::Modes)
      glDisable(mode);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBindVertexArray(Vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<uint8_t, SectionLightVolume::CellCount * 4> pixels{};
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    for (size_t n = 0; n < State::PackNames.size(); ++n)
      glPixelStorei(State::PackNames[n], n == 0 ? 1 : 0);
    glReadPixels(0, 0, 100, 80, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE,
                 pixels.data());
    const auto error = glGetError();
    bool complete = error == GL_NO_ERROR;
    for (size_t n = 0; n < result.data.cells.size(); ++n)
    {
      result.data.cells[n] = pixels[4 * n];
      complete = complete && pixels[4 * n + 1] == 1 && pixels[4 * n + 3] == 255;
    }
    result.cpu_matches = complete && result.data.cells == expected.cells;
    result.valid = complete && result.cpu_matches;
#endif
    return result;
  }

private:
  GLuint Program{0}, Texture{0}, Framebuffer{0}, Vao{0};
#if !defined(__ANDROID__) && !defined(CUBATARIUM_GLES)
  struct State
  {
    static constexpr std::array<GLenum, 6> Modes = {
        GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND,
        GL_CULL_FACE,  GL_SCISSOR_TEST, GL_RASTERIZER_DISCARD};
    static constexpr std::array<GLenum, 6> PackNames = {
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,   GL_PACK_SKIP_PIXELS,
        GL_PACK_SKIP_ROWS, GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES};
    GLint program = 0, vao = 0, draw_fb = 0, read_fb = 0, active = 0,
          texture0 = 0, pack_buffer = 0, unpack_buffer = 0;
    std::array<GLint, 4> viewport{};
    std::array<GLint, 2> polygon{};
    std::array<GLboolean, 4> color{};
    std::array<GLboolean, Modes.size()> modes{};
    std::array<GLint, PackNames.size()> pack{};
    State()
    {
      glGetIntegerv(GL_CURRENT_PROGRAM, &program);
      glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
      glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fb);
      glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read_fb);
      glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
      glActiveTexture(GL_TEXTURE0);
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
      glGetIntegerv(GL_VIEWPORT, viewport.data());
      glGetIntegerv(GL_POLYGON_MODE, polygon.data());
      glGetBooleanv(GL_COLOR_WRITEMASK, color.data());
      glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack_buffer);
      glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpack_buffer);
      for (size_t n = 0; n < Modes.size(); ++n)
        modes[n] = glIsEnabled(Modes[n]);
      for (size_t n = 0; n < PackNames.size(); ++n)
        glGetIntegerv(PackNames[n], &pack[n]);
    }
    ~State()
    {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fb);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, read_fb);
      glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
      glUseProgram(program);
      glBindVertexArray(vao);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture0);
      glActiveTexture(active);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, pack_buffer);
      glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpack_buffer);
      glColorMask(color[0], color[1], color[2], color[3]);
      glPolygonMode(GL_FRONT_AND_BACK, polygon[0]);
      for (size_t n = 0; n < Modes.size(); ++n)
      {
        if (modes[n])
          glEnable(Modes[n]);
        else
          glDisable(Modes[n]);
      }
      for (size_t n = 0; n < PackNames.size(); ++n)
        glPixelStorei(PackNames[n], pack[n]);
    }
  };
  bool Initialize()
  {
    if (Program)
      return true;
    const auto compile = [](GLenum kind, const std::string &source)
    {
      const auto shader = glCreateShader(kind);
      const char *text = source.c_str();
      glShaderSource(shader, 1, &text, nullptr);
      glCompileShader(shader);
      GLint okay = 0;
      glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
      if (!okay)
      {
        glDeleteShader(shader);
        return GLuint(0);
      }
      return shader;
    };
    const auto vertex =
        compile(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){vec2 "
                                  "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_"
                                  "Position=vec4(p*2.0-1.0,0,1);}");
    const std::string fragment =
        std::string("#version 330 core\n") +
        SectionLightBindingTable::ShaderInterface() +
        "uniform uvec4 probeHandle;uniform ivec3 "
        "probeOrigin;layout(location=0)out uvec4 result;"
        "void main(){int i=int(gl_FragCoord.x)+100*int(gl_FragCoord.y);"
        "ivec3 p=ivec3(i%20,(i/20)%20,i/400)-ivec3(2);uint value;"
        "bool "
        "valid=sectionLightFetch(probeHandle,probeOrigin+p,value);result=uvec4("
        "value,valid?1u:0u,0u,255u);}";
    const auto pixel = compile(GL_FRAGMENT_SHADER, fragment);
    if (!vertex || !pixel)
    {
      if (vertex)
        glDeleteShader(vertex);
      if (pixel)
        glDeleteShader(pixel);
      return false;
    }
    Program = glCreateProgram();
    glAttachShader(Program, vertex);
    glAttachShader(Program, pixel);
    glLinkProgram(Program);
    glDeleteShader(vertex);
    glDeleteShader(pixel);
    GLint okay = 0;
    glGetProgramiv(Program, GL_LINK_STATUS, &okay);
    if (!okay)
    {
      Reset();
      return false;
    }
    glGenTextures(1, &Texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Texture);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, 100, 80, 0, GL_RGBA_INTEGER,
                 GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &Framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, Framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           Texture, 0);
    const GLenum draw = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &draw);
    glReadBuffer(draw);
    glGenVertexArrays(1, &Vao);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
      Reset();
      return false;
    }
    return true;
  }
#endif
};
} // namespace cutum
