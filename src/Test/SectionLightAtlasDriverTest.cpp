#include "Render/Lighting/SectionLightAtlas.h"
#include "Render/Lighting/SectionLightBindingTable.h"
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

void RunBindings()
{
  using Table = cutum::SectionLightBindingTable;
  Table table;
  Expect(!table.Create({}, Pattern(1), {}) && !table.Bind(7, 8),
         "uninitialized binding table rejects allocation and binding");
  Expect(!table.Initialize(0) &&
             !table.Initialize(std::numeric_limits<uint32_t>::max()),
         "zero/hostile binding limits do not allocate unbounded storage");
  Expect(table.Initialize(3, 8u * Volume::CellCount, 8) &&
             table.Capacity() == 3 && !table.Initialize(),
         "bounded table initialized exactly once");

  const auto vertex =
      Compile(GL_VERTEX_SHADER,
              "#version 330 core\n"
              "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
              "gl_Position=vec4(p*2.0-1.0,0,1);}");
  const auto source = std::string("#version 330 core\n") +
                      Table::ShaderInterface() +
                      "\nuniform uvec4 handle; uniform ivec3 probeOrigin;"
                      "layout(location=0) out uint result;"
                      "void main(){ivec2 p=ivec2(gl_FragCoord.xy);uint value;"
                      "bool ok=sectionLightFetch(handle,probeOrigin+"
                      "ivec3(p.x-2,p.y%20-2,p.y/20-2),value);"
                      "result=ok?value:256u;}";
  const auto fragment = Compile(GL_FRAGMENT_SHADER, source.c_str());
  const auto program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  Expect(linked != 0, "production integer binding interface links on GL3.3");
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
         "binding probe readback target complete");

  GLuint sentinel = 0, pbo = 0;
  glActiveTexture(GL_TEXTURE3);
  glGenTextures(1, &sentinel);
  glBindTexture(GL_TEXTURE_2D, sentinel);
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
    const std::array<std::pair<GLenum, GLint>, 9> expected{
        {{GL_ACTIVE_TEXTURE, GL_TEXTURE3},
         {GL_TEXTURE_BINDING_2D, static_cast<GLint>(sentinel)},
         {GL_PIXEL_UNPACK_BUFFER_BINDING, static_cast<GLint>(pbo)},
         {GL_UNPACK_ALIGNMENT, 8},
         {GL_UNPACK_ROW_LENGTH, 43},
         {GL_UNPACK_IMAGE_HEIGHT, 41},
         {GL_UNPACK_SKIP_PIXELS, 2},
         {GL_UNPACK_SKIP_ROWS, 3},
         {GL_UNPACK_SKIP_IMAGES, 4}}};
    for (const auto [name, value] : expected)
    {
      GLint actual = -1;
      glGetIntegerv(name, &actual);
      Expect(actual == value, "binding upload preserves caller GL state");
    }
  };
  const auto probe = [&](Table &source_table, Table::Handle handle,
                         glm::ivec3 origin, const Volume *expected,
                         bool wrong_incarnation = false)
  {
    Expect(source_table.Bind(7, 8), "binding resources bind to distinct units");
    check_state();
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "uSectionLightAtlas"), 7);
    glUniform1i(glGetUniformLocation(program, "uSectionLightBindings"), 8);
    auto incarnation = source_table.ShaderIncarnation();
    if (wrong_incarnation)
      incarnation[1] ^= 1;
    glUniform2uiv(
        glGetUniformLocation(program, "uSectionLightTableIncarnation"), 1,
        incarnation.data());
    const auto words = handle.ShaderWords();
    glUniform4uiv(glGetUniformLocation(program, "handle"), 1, words.data());
    glUniform3i(glGetUniformLocation(program, "probeOrigin"), origin.x,
                origin.y, origin.z);
    glViewport(0, 0, 20, 400);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    std::array<uint32_t, Volume::CellCount> result{};
    glReadPixels(0, 0, 20, 400, GL_RED_INTEGER, GL_UNSIGNED_INT, result.data());
    for (size_t n = 0; n < result.size(); ++n)
      if (result[n] != (expected ? expected->cells[n] : 256u))
        throw std::runtime_error(
            "binding shader mismatch at cell " + std::to_string(n) +
            " origin=" + std::to_string(origin.x) + "," +
            std::to_string(origin.y) + "," + std::to_string(origin.z) +
            " actual=" + std::to_string(result[n]));
    Expect(glGetError() == GL_NO_ERROR, "binding lookup has no GL errors");
  };

  const glm::ivec3 origin(-320, -64, 48);
  const auto initial = Pattern(3);
  const auto first = table.Create({0, 7, 0}, initial, origin);
  const auto second = table.Create({0, 7, 0}, initial, origin);
  const auto third = table.Create({0, 7, 0}, initial, origin);
  Expect(first && second && third && first->index == 0 &&
             first->generation == 0 && first->valid &&
             table.GetAtlas().GetStatistics().uploads == 1,
         "binding zero/revision zero valid, independent readers share page");
  check_state();
  Expect(!table.Create({1, 1, 0}, Pattern(4), origin) && !table.Bind(7, 7) &&
             !table.Bind(7, 999999),
         "binding capacity and texture units are bounded");
  probe(table, *first, origin, initial.get());
  probe(table, *second, origin, initial.get());
  probe(table, *first, origin, nullptr, true);
  probe(table, {}, origin, nullptr);
  auto out_of_bounds = *first;
  out_of_bounds.index = std::numeric_limits<uint32_t>::max();
  probe(table, out_of_bounds, origin, nullptr);
  probe(table, *first, origin + glm::ivec3(40, 0, 0), nullptr);

  const auto successor = Pattern(9);
  Expect(table.Replace(*first, {0, 7, 1}, successor, origin) &&
             table.IsLive(*first),
         "field replacement keeps stable geometry binding handle");
  probe(table, *first, origin, successor.get());
  probe(table, *second, origin, initial.get());
  probe(table, *third, origin, initial.get());
  // Owner1 is already live, so Acquire shares it without a GL upload. An
  // unrelated prior error must not turn a successful table write into a
  // reported failure after it has already changed a published binding.
  glBindBuffer(GL_TEXTURE_2D,
               0); // Deliberate GL_INVALID_ENUM, no state change.
  Expect(!table.Replace(*second, {0, 7, 1}, successor, origin),
         "pending GL error rejects replacement without changing live entry");
  probe(table, *second, origin, initial.get());
  glBindBuffer(GL_TEXTURE_2D, 0);
  Expect(!table.Release(*second) && table.IsLive(*second),
         "failed release keeps a valid retryable binding");
  probe(table, *second, origin, initial.get());
  Expect(table.Replace(*first, {0, 7, 1}, successor, origin) &&
             table.GetAtlas().GetStatistics().uploads == 2,
         "same field does not upload or retire another reader");
  Expect(!table.Replace(*first, {0, 7, 1}, Pattern(2), origin) &&
             !table.Replace(*first, {0, 7, 2}, nullptr, origin),
         "contradictory owner and absent volume retain predecessor");
  probe(table, *first, origin, successor.get());
  Expect(table.Replace(*first, {0, 7, 1}, successor,
                       {std::numeric_limits<int>::max(), 0, 0}),
         "extreme origin can be represented exactly");
  probe(table, *first, {std::numeric_limits<int>::min() + 2, 0, 0}, nullptr);
  Expect(table.Replace(*first, {0, 7, 1}, successor, origin),
         "origin metadata restores without changing the field");
  Expect(table.Release(*second) && !table.Release(*second) &&
             !table.Replace(*second, {0, 7, 2}, successor, origin),
         "released binding rejects duplicate and stale operations");
  probe(table, *second, origin, nullptr);
  const glm::ivec3 extreme(-2147483600, 2147483600, -48);
  const auto reused = table.Create({1, 7, 0}, initial, extreme);
  Expect(reused && reused->index == second->index &&
             reused->generation != second->generation,
         "index reuse increments generation across different world owners");
  probe(table, *second, origin, nullptr);
  probe(table, *reused, extreme, initial.get());

  // One page leaves no room for an immutable successor. Its existing binding
  // must remain readable; this is not permission to overwrite a live page.
  Table tiny;
  Expect(tiny.Initialize(1, Volume::CellCount, 1), "one-page table allocated");
  const auto retained = tiny.Create({0, 0, 0}, initial, origin);
  Expect(retained && !tiny.Replace(*retained, {0, 0, 1}, successor, origin),
         "page pressure retains current binding");
  probe(tiny, *retained, origin, initial.get());
  Expect(tiny.Release(*retained) && !tiny.Create({0, 0, 1}, successor, origin),
         "free binding index cannot bypass atlas retirement fence");
  glFinish();
  tiny.CollectRetired();
  const auto recycled = tiny.Create({0, 0, 1}, successor, origin);
  Expect(recycled && recycled->generation != retained->generation,
         "completed atlas fence permits page and binding reuse");
  probe(tiny, *retained, origin, nullptr);
  probe(tiny, *recycled, origin, successor.get());
  tiny.Reset();

  Table rows;
  Expect(rows.Initialize(129, Volume::CellCount, 1),
         "multi-row table initializes with one shared physical page");
  std::vector<Table::Handle> row_handles;
  for (int n = 0; n < 129; ++n)
  {
    const auto handle = rows.Create({0, 0, 0}, initial, origin);
    Expect(handle.has_value(), "all declared binding rows are usable");
    row_handles.push_back(*handle);
  }
  probe(rows, row_handles[0], origin, initial.get());
  probe(rows, row_handles[127], origin, initial.get());
  probe(rows, row_handles[128], origin, initial.get());
  Expect(rows.GetAtlas().GetStatistics().uploads == 1,
         "binding metadata does not duplicate the shared physical field");
  rows.Reset();

  const auto old = *first;
  table.Reset();
  Expect(!table.IsLive(old) && table.Initialize(1, Volume::CellCount, 1),
         "reset removes all previous bindings");
  const auto reset = table.Create({0, 7, 0}, initial, origin);
  Expect(
      reset && reset->index == old.index &&
          reset->generation == old.generation &&
          reset->table_incarnation != old.table_incarnation,
      "table incarnation protects reset ABA even at identical slot/generation");
  probe(table, old, origin, nullptr);
  probe(table, *reset, origin, initial.get());
  Expect(table.Release(*reset), "last live binding released");
  probe(table, *reset, origin, nullptr);
  glFinish();
  table.CollectRetired();
  table.Reset();

  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_IMAGES, 0);
  glDeleteBuffers(1, &pbo);
  glDeleteTextures(1, &sentinel);
  glDeleteFramebuffers(1, &framebuffer);
  glDeleteTextures(1, &target);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  Expect(glGetError() == GL_NO_ERROR, "binding cleanup has no GL errors");
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
    RunBindings();
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
