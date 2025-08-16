#include <iostream>
#include <memory>
#include <GL/glew.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "WindowManager.h"
#include "TextureBase.h"
#include "TextureCube.h"
#include "Object.h"
#include "World.h"
#include "Core.h"
#include "GeometryEngine.h"
#include "ViewEngine.h"
#include "ObjectStorage.h"
#include "TextRenderer.h"

int main(int argc, char *argv[])
{
    using namespace cutum;

    try {
        // Create and initialize window
        auto windowManager = std::make_unique<WindowManager>();
        
        if (!windowManager->Initialize(1280, 720, "Cubatarium")) {
            std::cerr << "Failed to initialize window manager" << std::endl;
            return -1;
        }

        // Create system components
        auto texture_base_instance = std::make_shared<TextureBaseStorage>();
        auto texture_cube_instance = std::make_shared<TextureCubeStorage>(texture_base_instance);

        auto object_storage = std::make_shared<ObjectStorage>(texture_cube_instance);
        auto view_engine = std::make_shared<ViewEngine>();

        auto world = std::make_shared<World>(object_storage, view_engine);

        // Create TextRenderer for GeometryEngine
        auto text_renderer = std::make_shared<TextRenderer>();
        
        // Initialize TextRenderer with automatic font detection
        if (!text_renderer->Initialize(16)) {
            std::cerr << "Failed to initialize text renderer" << std::endl;
            return -1;
        }
        
        text_renderer->SetWindowSize(1280, 720);

        auto geometry_engine = std::make_shared<GeometryEngine>(object_storage, world, texture_base_instance, texture_cube_instance, text_renderer);
        
        // Initialize GeometryEngine
        if (!geometry_engine->InitEngine()) {
            std::cerr << "Failed to initialize geometry engine" << std::endl;
            return -1;
        }
        
        auto core = std::make_shared<Core>(texture_base_instance, texture_cube_instance,
                                          object_storage, world, geometry_engine, view_engine);

        // Initialize WindowManager with components
        windowManager->Init(core, world, geometry_engine, view_engine);
        
        // Pass TextRenderer to WindowManager
        windowManager->SetTextRenderer(text_renderer);

        // Load system
        core->LoadSystem("config.json");

        // Start main loop
        windowManager->Run();

        // Save system
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
