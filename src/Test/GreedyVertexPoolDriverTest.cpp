#include "Render/Engine/GreedyVertexPool.h"
#include "Render/GlIncludes.h"
#include <GLFW/glfw3.h>
#include <iostream>

// Real driver smoke/oracle. No world, save, runtime config or visible window.
// This complements, not replaces, the deterministic delayed-fence fixture.
int RunGreedyPoolDriverTest()
{
  if (!glfwInit())
  {
    std::cout << "SKIP: GLFW unavailable\n";
    return 77;
  }
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  auto *window =
      glfwCreateWindow(64, 64, "GPU allocator test", nullptr, nullptr);
  if (!window)
  {
    glfwTerminate();
    std::cout << "SKIP: GL 4.3 unavailable\n";
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
  std::cout << "GPU: " << glGetString(GL_RENDERER) << '\n';
  const char *vs = "#version 430 core\nlayout(location=0) in vec3 p; void "
                   "main(){gl_Position=vec4(p,1);}";
  const char *fs =
      "#version 430 core\nout vec4 c; void main(){c=vec4(1,0,0,1);}";
  auto shader = [](GLenum type, const char *source)
  {
    GLuint id = glCreateShader(type);
    glShaderSource(id, 1, &source, nullptr);
    glCompileShader(id);
    GLint ok = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
      glDeleteShader(id);
      return GLuint(0);
    }
    return id;
  };
  GLuint vertex = shader(GL_VERTEX_SHADER, vs),
         fragment = shader(GL_FRAGMENT_SHADER, fs);
  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  int failures = linked ? 0 : 1;
  GLuint vao = 0;
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glUseProgram(program);
  glViewport(0, 0, 64, 64);
  cutum::GreedyMeshBatch batch;
  batch.vertices.resize(3);
  batch.vertices[0].px = -1;
  batch.vertices[0].py = -1;
  batch.vertices[1].px = 3;
  batch.vertices[1].py = -1;
  batch.vertices[2].px = -1;
  batch.vertices[2].py = 3;
  batch.indices = {0, 1, 2};
  cutum::UGreedyVertexPool pool;
  auto published = pool.Allocate(batch);
  for (int frame = 0; linked && frame < 128; ++frame)
  {
    pool.BeginUploadFrame();
    glBindBuffer(GL_ARRAY_BUFFER, pool.VertexBuffer());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pool.IndexBuffer());
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(cutum::GreedyMeshVertex),
                          reinterpret_cast<void *>(published.vertexByteOffset));
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT,
                   reinterpret_cast<void *>(published.indexByteOffset));
    pool.SignalDrawComplete();
    const auto replacement = pool.Allocate(batch);
    if (!replacement.vertexCount)
    {
      ++failures;
      break;
    }
    pool.Free(published);
    published = replacement;
    if ((frame % 8) == 0)
    {
      unsigned char pixel[4]{};
      glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
      if (pixel[0] < 240 || pixel[1] > 10 || pixel[2] > 10)
        ++failures;
    }
    if (glGetError() != GL_NO_ERROR)
      ++failures;
  }
  // G1: negative pixel sample — cleared FB without draw must read dark
  // (FalseNegCull stand-in until world object-id masks exist).
  if (linked)
  {
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char dark[4]{255, 255, 255, 255};
    glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, dark);
    if (dark[0] > 10 || dark[1] > 10 || dark[2] > 10)
      ++failures;
  }
  // G1-P4: object-id stand-in — unique clear color acts as slot id; readPixels
  // must recover the id (world object-id masks still open / FalseNegCull=0).
  if (linked)
  {
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    unsigned char id_px[4]{};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, id_px);
    if (id_px[1] < 240 || id_px[0] > 10 || id_px[2] > 10)
      ++failures;
  }
  pool.Destroy();
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  glfwDestroyWindow(window);
  glfwTerminate();
  std::cout << "driver image/GL failures: " << failures << '\n';
  return failures ? 1 : 0;
}
