#include "Render/Lighting/BoundSectionLightProbe.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>

using namespace cutum;
static void Expect(bool value, const char *message)
{
  if (!value)
    throw std::runtime_error(message);
}
static GLuint Compile(GLenum kind, const std::string &source)
{
  const auto shader = glCreateShader(kind);
  const char *text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint okay = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
  Expect(okay, "source shader compile");
  return shader;
}
static std::shared_ptr<SectionLightVolume> Pattern(unsigned seed)
{
  auto result = std::make_shared<SectionLightVolume>();
  for (size_t i = 0; i < result->cells.size(); ++i)
    result->cells[i] = uint8_t((i * 37 + seed) & 255);
  return result;
}
static void Run()
{
  const auto vertex =
      Compile(GL_VERTEX_SHADER,
              "#version 330 core\nlayout(location=7) in uvec4 aHandle;"
              "flat out uvec4 vHandle;"
              "void main(){vHandle=aHandle;gl_Position=vec4(0,0,0,1);}");
  const auto fragment =
      Compile(GL_FRAGMENT_SHADER,
              std::string("#version 330 core\n") +
                  SectionLightBindingTable::ShaderInterface() +
                  "uniform bool "
                  "uUseSurfaceSectionLight,uUseSurfaceSectionLightDrawTable;"
                  "uniform uvec4 uSurfaceSectionLightHandle;flat in uvec4 "
                  "vHandle;out vec4 color;"
                  "void main(){uint "
                  "p=0u;if(uUseSurfaceSectionLight)"
                  "sectionLightFetch(uUseSurfaceSectionLightDrawTable?vHandle:"
                  "uSurfaceSectionLightHandle,ivec3(ivec2(gl_FragCoord.xy),0),"
                  "p);color=vec4(float(p)/255.0);}");
  const auto source = glCreateProgram();
  glAttachShader(source, vertex);
  glAttachShader(source, fragment);
  glLinkProgram(source);
  GLint okay = 0;
  glGetProgramiv(source, GL_LINK_STATUS, &okay);
  Expect(okay, "source program link");
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  SectionLightBindingTable table;
  Expect(table.Initialize(2, 8u * SectionLightVolume::CellCount, 8, true),
         "source table initialization");
  const glm::ivec3 origin(-64, -48, 16);
  const auto original = Pattern(7);
  const auto next = Pattern(19);
  const auto resident = table.Create({7, 41, 3}, original, origin);
  Expect(bool(resident), "resident source");
  BoundSectionLightProbe probe;
  const auto bind = [&](SectionLightBindingTable::Handle handle)
  {
    Expect(table.Bind(4, 5), "source binding");
    glUseProgram(source);
    glUniform1i(glGetUniformLocation(source, "uUseSurfaceSectionLight"), 1);
    glUniform1i(
        glGetUniformLocation(source, "uUseSurfaceSectionLightDrawTable"), 0);
    glUniform1i(glGetUniformLocation(source, "uSectionLightAtlas"), 4);
    glUniform1i(glGetUniformLocation(source, "uSectionLightBindings"), 5);
    const auto incarnation = table.ShaderIncarnation();
    glUniform2uiv(glGetUniformLocation(source, "uSectionLightTableIncarnation"),
                  1, incarnation.data());
    const auto words = handle.ShaderWords();
    glUniform4uiv(glGetUniformLocation(source, "uSurfaceSectionLightHandle"), 1,
                  words.data());
  };
  bind(*resident);
  GLuint draw_vao = 0;
  glGenVertexArrays(1, &draw_vao);
  glBindVertexArray(draw_vao);
  SectionLightDrawTable draw_rows;
  const std::vector<SectionLightDrawTable::Words> rows{{0, 0, 0, 0},
                                                       resident->ShaderWords()};
  Expect(draw_rows.Prepare(rows) && draw_rows.BindAttribute(),
         "actual instanced draw-table buffer bound");
  glUniform1i(glGetUniformLocation(source, "uUseSurfaceSectionLightDrawTable"),
              1);
  const auto table_capture =
      probe.Capture(source, resident->ShaderWords(), origin, *original, 1);
  Expect(table_capture.valid && table_capture.draw_table &&
             table_capture.draw_table_buffer == draw_rows.Buffer() &&
             table_capture.draw_table_row == 1 &&
             table_capture.data.cells == original->cells,
         "actual GPU instanced row selects the captured full field");
  Expect(!probe.Capture(source, resident->ShaderWords(), origin, *original, 0)
              .valid,
         "wrong GPU row cannot inherit expected uniform handle");
  Expect(!probe.Capture(source, resident->ShaderWords(), origin, *original, 2)
              .valid,
         "out-of-buffer GPU row rejected");
  glVertexAttribDivisor(7, 2);
  Expect(!probe.Capture(source, resident->ShaderWords(), origin, *original, 1)
              .valid,
         "wrong instanced row divisor rejected");
  glVertexAttribDivisor(7, 1);
  SectionLightDrawTable::DisableAttribute();
  Expect(!probe.Capture(source, resident->ShaderWords(), origin, *original, 1)
              .valid,
         "disabled draw-table attribute rejected");
  glBindVertexArray(0);
  glDeleteVertexArrays(1, &draw_vao);
  bind(*resident);
  glViewport(3, 4, 31, 29);
  glEnable(GL_SCISSOR_TEST);
  glScissor(2, 3, 1, 1);
  glEnable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glEnable(GL_RASTERIZER_DISCARD);
  glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  glPixelStorei(GL_PACK_ROW_LENGTH, 313);
  glPixelStorei(GL_PACK_SKIP_PIXELS, 11);
  glPixelStorei(GL_PACK_SKIP_ROWS, 7);
  GLuint pack = 0;
  glGenBuffers(1, &pack);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
  glBufferData(GL_PIXEL_PACK_BUFFER, 1, nullptr, GL_STREAM_READ);
  glActiveTexture(GL_TEXTURE3);
  const auto capture =
      probe.Capture(source, resident->ShaderWords(), origin, *original);
  Expect(capture.captured && capture.valid && capture.cpu_matches,
         "actual bound GPU cells match full independent pattern");
  Expect(capture.data.cells == original->cells,
         "all8000cells returned in x/y/z order");
  GLint program = 0, active = 0, buffer = 0, row = 0;
  std::array<GLint, 4> viewport{};
  std::array<GLboolean, 4> color{};
  glGetIntegerv(GL_CURRENT_PROGRAM, &program);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
  glGetIntegerv(GL_PACK_ROW_LENGTH, &row);
  glGetIntegerv(GL_VIEWPORT, viewport.data());
  glGetBooleanv(GL_COLOR_WRITEMASK, color.data());
  Expect(GLuint(program) == source && active == GL_TEXTURE3 &&
             GLuint(buffer) == pack && row == 313,
         "program, active texture, PBO and pack state restored");
  Expect(viewport == std::array<GLint, 4>{3, 4, 31, 29} &&
             color == std::array<GLboolean, 4>{0, 1, 0, 1} &&
             glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_RASTERIZER_DISCARD),
         "raster state restored");
  auto wrong = *original;
  wrong.cells[117] ^= 1;
  Expect(!probe.Capture(source, resident->ShaderWords(), origin, wrong).valid,
         "wrong expected CPU byte rejected");
  auto wrong_words = resident->ShaderWords();
  ++wrong_words[1];
  Expect(!probe.Capture(source, wrong_words, origin, *original).valid,
         "stale expected handle rejected");
  glUniform1i(glGetUniformLocation(source, "uUseSurfaceSectionLight"), 0);
  Expect(
      !probe.Capture(source, resident->ShaderWords(), origin, *original).valid,
      "actual shader physical mode disabled rejected");
  bind(*resident);
  const auto first = table.StageForOrderedDraw({7, 42, 1}, original, origin);
  Expect(bool(first), "first transient page");
  bind(*first);
  const auto held =
      probe.Capture(source, first->ShaderWords(), origin, *original);
  Expect(held.valid, "transient page captured before reuse");
  const auto second = table.StageForOrderedDraw({7, 43, 2}, next, origin);
  Expect(bool(second), "reuse transient page");
  bind(*second);
  const auto successor =
      probe.Capture(source, second->ShaderWords(), origin, *next);
  Expect(successor.valid && successor.data.cells == next->cells &&
             held.data.cells == original->cells,
         "captured transient data survives real ordered-page overwrite");
  Expect(!probe.Capture(source, second->ShaderWords(), origin, *original).valid,
         "overwritten page cannot pass against held predecessor");
  BoundSectionLightCapture surface = held;
  surface.origin = origin;
  const glm::vec3 top = glm::vec3(origin) + glm::vec3(3.2f, 4.5f, 6.2f);
  Expect(surface.SampleSurface(top, 4) ==
             original->cells[SectionLightVolume::Index(3, 5, 6)],
         "half-centered positive face samples outside cell");
  Expect(!surface.SampleSurface(glm::vec3(origin) + glm::vec3(100.0f), 4),
         "outside padded field rejected");
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  glDeleteBuffers(1, &pack);
  glUseProgram(0);
  glDeleteProgram(source);
  Expect(glGetError() == GL_NO_ERROR,
         "probe preserves an error-free GL context");
}
int main()
{
  if (!glfwInit())
    return 77;
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  auto *window =
      glfwCreateWindow(32, 32, "bound field probe", nullptr, nullptr);
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
  int result = 0;
  try
  {
    Run();
    std::cout << "bound section-light GPU probe PASS\n";
  }
  catch (const std::exception &error)
  {
    std::cerr << error.what() << '\n';
    result = 1;
  }
  glfwDestroyWindow(window);
  glfwTerminate();
  return result;
}
