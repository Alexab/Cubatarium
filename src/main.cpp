#include <cstring>
#include <iostream>
#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "WindowManager.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "Object.h"
#include "World.h"
#include "Core.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "ObjectStorage.h"
#include "Prefab.h"
#include "TextRenderer.h"

namespace cutum {

static int RunValidateLoad()
{
    if (!glfwInit()) {
        std::cerr << "validate-load: glfwInit failed" << std::endl;
        return 1;
    }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* ctx = glfwCreateWindow(64, 64, "validate", nullptr, nullptr);
    if (!ctx) {
        std::cerr << "validate-load: failed to create GL context" << std::endl;
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(ctx);
    if (glewInit() != GLEW_OK) {
        std::cerr << "validate-load: glewInit failed" << std::endl;
        glfwDestroyWindow(ctx);
        glfwTerminate();
        return 1;
    }

    auto texture_base_instance = std::make_shared<TextureBaseStorage>();
    auto texture_cube_instance = std::make_shared<TextureCubeStorage>(texture_base_instance);
    auto object_storage = std::make_shared<ObjectStorage>(texture_cube_instance);
    auto prefab_library = std::make_shared<PrefabLibrary>();
    auto view_engine = std::make_shared<ViewEngine>();
    auto world = std::make_shared<World>(object_storage, view_engine);
    auto core = std::make_shared<Core>(texture_base_instance, texture_cube_instance,
                                         object_storage, prefab_library, world, nullptr, view_engine);

    core->LoadSystem("config.json");
    std::cout << "validate-load: blocks=" << world->GetCachedBlockCount()
              << " instances=" << world->GetRenderInstanceCount() << std::endl;

    glfwDestroyWindow(ctx);
    glfwTerminate();
    return 0;
}

} // namespace cutum

int main(int argc, char *argv[])
{
    using namespace cutum;

    try {
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--validate-load") == 0) {
                return RunValidateLoad();
            }
        }

        auto windowManager = std::make_unique<WindowManager>();
        
        if (!windowManager->Initialize(1280, 720, "Cubatarium")) {
            std::cerr << "Failed to initialize window manager" << std::endl;
            return -1;
        }

        auto texture_base_instance = std::make_shared<TextureBaseStorage>();
        auto texture_cube_instance = std::make_shared<TextureCubeStorage>(texture_base_instance);

        auto object_storage = std::make_shared<ObjectStorage>(texture_cube_instance);
        auto prefab_library = std::make_shared<PrefabLibrary>();
        auto view_engine = std::make_shared<ViewEngine>();

        auto world = std::make_shared<World>(object_storage, view_engine);

        auto text_renderer = std::make_shared<TextRenderer>();
        
        if (!text_renderer->Initialize(16)) {
            std::cerr << "Failed to initialize text renderer" << std::endl;
            return -1;
        }
        
        text_renderer->SetWindowSize(1280, 720);

        auto geometry_engine = std::make_shared<GeometryEngine>(object_storage, world, texture_base_instance, texture_cube_instance, text_renderer);
        
        if (!geometry_engine->InitEngine()) {
            std::cerr << "Failed to initialize geometry engine" << std::endl;
            return -1;
        }
        
        auto core = std::make_shared<Core>(texture_base_instance, texture_cube_instance,
                                          object_storage, prefab_library, world, geometry_engine, view_engine);

        windowManager->Init(core, world, geometry_engine, view_engine);
        
        windowManager->SetTextRenderer(text_renderer);

        core->LoadSystem("config.json");

        windowManager->Run();

        core->SaveSystem("config.json");

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "Unknown exception occurred" << std::endl;
        return -1;
    }
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    return main(__argc, __argv);
}
#endif
