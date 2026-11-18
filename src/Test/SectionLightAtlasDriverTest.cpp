#include "Render/Lighting/SectionLightAtlas.h"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
using Atlas = cutum::SectionLightAtlas;
using Volume = cutum::SectionLightVolume;

void Expect(bool condition, const char *message)
{
  if (!condition)
    throw std::runtime_error(message);
}

GLuint Compile(GLenum type, const char *source)
{
  const auto shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint compiled = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
  if (!compiled)
  {
    char log[2048]{};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    throw std::runtime_error(std::string("atlas shader: ") + log);
  }
  return shader;
}

std::shared_ptr<const Volume> Pattern(unsigned seed)
{
  auto result = std::make_shared<Volume>();
  for (int z = 0; z < Volume::Width; ++z)
    for (int y = 0; y < Volume::Width; ++y)
      for (int x = 0; x < Volume::Width; ++x)
      {
        const auto sky = (x + 3 * y + 5 * z + seed) & 15;
        const auto block = (7 * x + 11 * y + 13 * z + 3 * seed) & 15;
        result->cells[x + Volume::Width * (y + Volume::Width * z)] =
            static_cast<uint8_t>(sky | (block << 4));
      }
  return result;
}

void Run()
{
  const auto portable = Atlas::Layout::Choose(256, 32u * 1024u * 1024u, 4096);
  Expect(portable.texture_width == 240 && portable.capacity == 1728 &&
             portable.bytes == 1728u * Volume::CellCount,
         "device capability bounds padded3D page capacity");
  Expect(!Atlas::Layout::Choose(19, 100000, 8).capacity &&
             !Atlas::Layout::Choose(1024, Volume::CellCount - 1, 8).capacity &&
             !Atlas::Layout::Choose(1024, 100000, 0).capacity,
         "no page is promised below device, memory or page limits");
  const auto hostile = Atlas::Layout::Choose(
      std::numeric_limits<int>::max(), std::numeric_limits<size_t>::max(),
      std::numeric_limits<uint32_t>::max());
  Expect(hostile.capacity &&
             hostile.bytes / Volume::CellCount == hostile.capacity,
         "large capability inputs do not overflow page accounting");

  const auto vertex =
      Compile(GL_VERTEX_SHADER,
              "#version 330 core\n"
              "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
              "gl_Position=vec4(p*2.0-1.0,0,1);}");
  const auto fragment =
      Compile(GL_FRAGMENT_SHADER,
              "#version 330 core\n"
              "uniform usampler3D field; uniform ivec3 origin;"
              "layout(location=0) out uint packed;"
              "void main(){ivec2 p=ivec2(gl_FragCoord.xy);"
              "packed=texelFetch(field,origin+ivec3(p.x,p.y%20,p.y/20),0).r;}");
  const auto program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  Expect(linked != 0, "GL3.3 integer field sampler links");
  GLuint vao = 0, target = 0, framebuffer = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenTextures(1, &target);
  glBindTexture(GL_TEXTURE_2D, target);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 20, 400, 0, GL_RED_INTEGER,
               GL_UNSIGNED_INT, nullptr);
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         target, 0);
  Expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
         "independent integer readback target complete");

  GLuint sentinel = 0, pbo = 0;
  glActiveTexture(GL_TEXTURE3);
  glGenTextures(1, &sentinel);
  glBindTexture(GL_TEXTURE_3D, sentinel);
  glGenBuffers(1, &pbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, 4096, nullptr, GL_STATIC_DRAW);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 43);
  glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 41);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 3);
  glPixelStorei(GL_UNPACK_SKIP_IMAGES, 4);
  const auto check_state = [&]()
  {
    const std::array<std::pair<GLenum, GLint>, 9> state{
        {{GL_ACTIVE_TEXTURE, GL_TEXTURE3},
         {GL_TEXTURE_BINDING_3D, static_cast<GLint>(sentinel)},
         {GL_PIXEL_UNPACK_BUFFER_BINDING, static_cast<GLint>(pbo)},
         {GL_UNPACK_ALIGNMENT, 8},
         {GL_UNPACK_ROW_LENGTH, 43},
         {GL_UNPACK_IMAGE_HEIGHT, 41},
         {GL_UNPACK_SKIP_PIXELS, 2},
         {GL_UNPACK_SKIP_ROWS, 3},
         {GL_UNPACK_SKIP_IMAGES, 4}}};
    for (const auto [name, expected] : state)
    {
      GLint actual = -1;
      glGetIntegerv(name, &actual);
      Expect(actual == expected, "atlas preserves caller upload/binding state");
    }
  };

  Atlas atlas;
  Expect(!atlas.Acquire({}, Pattern(0)) && !atlas.Bind(7),
         "uninitialized atlas cannot supply pages or bindings");
  Expect(atlas.Initialize(8u * Volume::CellCount, 8),
         "bounded atlas allocated");
  Expect(atlas.GetLayout().capacity == 8, "eight-page memory ceiling honored");
  check_state();
  const auto probe = [&](Atlas::Page page, const Volume &expected)
  {
    const auto origin = atlas.Origin(page);
    Expect(origin.has_value() && atlas.Bind(7), "live page can be sampled");
    check_state();
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "field"), 7);
    glUniform3i(glGetUniformLocation(program, "origin"), origin->x, origin->y,
                origin->z);
    glViewport(0, 0, 20, 400);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<uint32_t, Volume::CellCount> result{};
    glReadPixels(0, 0, 20, 400, GL_RED_INTEGER, GL_UNSIGNED_INT, result.data());
    for (size_t n = 0; n < result.size(); ++n)
      if (result[n] != expected.cells[n])
        throw std::runtime_error("shader atlas sample mismatch at cell " +
                                 std::to_string(n));
    Expect(glGetError() == GL_NO_ERROR, "field sampling has no GL errors");
  };
  std::array<Atlas::Page, 8> pages;
  std::array<std::shared_ptr<const Volume>, 8> volumes;
  for (unsigned n = 0; n < pages.size(); ++n)
  {
    volumes[n] = Pattern(n);
    // Equal field IDs/revisions in different worlds are distinct owners.
    const Atlas::Owner owner{n == 0 ? 0u : 1u, 7, n < 2 ? 0u : n};
    const auto page = atlas.Acquire(owner, volumes[n]);
    Expect(page.has_value(), "free page acquired");
    pages[n] = *page;
    probe(*page, *volumes[n]);
  }
  Expect(
      pages[0].valid && pages[0].slot == 0 && pages[0].generation == 0,
      "page zero, generation zero and revision zero are valid explicit values");
  const auto shared = atlas.Acquire({0, 7, 0}, volumes[0]);
  Expect(shared == pages[0] && atlas.GetStatistics().uploads == 8,
         "same immutable owner shares page without another upload");
  auto contradictory = std::make_shared<Volume>(*volumes[0]);
  contradictory->cells[0] ^= 1;
  Expect(!atlas.Acquire({0, 7, 0}, contradictory),
         "same owner cannot overwrite its immutable GPU bytes");
  Expect(atlas.Release(*shared) && atlas.IsLive(pages[0]),
         "one consumer cannot retire another consumer's shared page");
  Expect(!atlas.Acquire({2, 7, 0}, Pattern(9)),
         "capacity pressure cannot evict live readers implicitly");
  Expect(atlas.Release(pages[0]) && !atlas.IsLive(pages[0]) &&
             !atlas.Origin(pages[0]) && !atlas.Release(pages[0]),
         "last release retires page and rejects duplicate/stale handles");
  Expect(!atlas.Acquire({2, 7, 0}, Pattern(9)),
         "retired page cannot be reused before explicit fence collection");
  glFinish();
  atlas.CollectRetired();
  const auto successor_volume = Pattern(9);
  const auto successor = atlas.Acquire({2, 7, 0}, successor_volume);
  Expect(successor && successor->slot == pages[0].slot &&
             successor->generation != pages[0].generation &&
             !atlas.IsLive(pages[0]),
         "fence completion permits generation-safe reuse");
  probe(*successor, *successor_volume);
  for (unsigned n = 1; n < pages.size(); ++n)
    probe(pages[n], *volumes[n]);
  Expect(atlas.GetStatistics().uploaded_bytes == 9u * Volume::CellCount,
         "only immutable new pages upload bytes");
  atlas.Reset();
  Expect(!atlas.IsLive(*successor) && atlas.Initialize(Volume::CellCount, 1),
         "reset invalidates previous atlas handles");
  const auto zero = std::make_shared<const Volume>();
  const auto reset_page = atlas.Acquire({2, 7, 0}, zero);
  Expect(reset_page &&
             reset_page->atlas_incarnation != successor->atlas_incarnation &&
             !atlas.IsLive(*successor),
         "atlas recreation cannot produce ABA handles");
  probe(*reset_page, *zero);
  atlas.Reset();
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glDeleteBuffers(1, &pbo);
  glDeleteTextures(1, &sentinel);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &target);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  Expect(glGetError() == GL_NO_ERROR, "atlas cleanup has no GL errors");
}
} // namespace

int main()
{
  if (!glfwInit())
    return 77;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  auto *window =
      glfwCreateWindow(32, 32, "Section light atlas test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    return 77;
  }
  glfwMakeContextCurrent(window);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK)
  {
    glfwDestroyWindow(window);
    glfwTerminate();
    return 77;
  }
  while (glGetError() != GL_NO_ERROR)
  {
  }
  std::cout << "GPU: " << glGetString(GL_RENDERER)
            << " GL: " << glGetString(GL_VERSION) << '\n';
  int exit_code = 0;
  try
  {
    Run();
  }
  catch (const std::exception &error)
  {
    std::cerr << "section_light_atlas_driver_test: " << error.what() << '\n';
    exit_code = 1;
  }
  glfwDestroyWindow(window);
  glfwTerminate();
  if (!exit_code)
    std::cout << "section_light_atlas_driver_test: PASS (8000 cells per page; "
                 "storage/sampler/lifetime only)\n";
  return exit_code;
}
