cmake_minimum_required(VERSION 3.10)

project(Cubatarium VERSION 0.0.1.1 LANGUAGES CXX)

include(${CMAKE_SOURCE_DIR}/cmake/GetVersionFromGit.cmake)
cubatarium_resolve_version(CUBATARIUM_VERSION_STRING)
set(CUBATARIUM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CUBATARIUM_GENERATED_DIR}")
configure_file(
    "${CMAKE_SOURCE_DIR}/src/Version.h.in"
    "${CUBATARIUM_GENERATED_DIR}/Version.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 17)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(MSVC)
        add_compile_options(/Zi /Od)
    else()
        add_compile_options(-g -O0)
    endif()
endif()

find_package(PkgConfig REQUIRED)
find_package(OpenGL REQUIRED)

if(WIN32)
    find_package(glfw3 REQUIRED)
else()
    find_package(glfw3 REQUIRED)
endif()

find_package(glm REQUIRED)
find_package(GLEW REQUIRED)
find_package(Freetype REQUIRED)
find_package(nlohmann_json REQUIRED)

set(STB_IMAGE_HEADER "${CMAKE_SOURCE_DIR}/src/stb_image.h")
if(NOT EXISTS ${STB_IMAGE_HEADER})
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        ${STB_IMAGE_HEADER}
    )
endif()

set(SOURCES
    src/stb_image.cpp
    src/main.cpp
    src/Camera.cpp
    src/CreatureBounds.cpp
    src/CreatureLocomotionController.cpp
    src/Creature.cpp
    src/CreatureInventory.cpp
    src/Player.cpp
    src/TestMob.cpp
    src/MobController.cpp
    src/WorldCreatures.cpp
    src/CreatureDefinitionStorage.cpp
    src/CreatureTextureStorage.cpp
    src/SkinDefinitionStorage.cpp
    src/CreatureAppearance.cpp
    src/CreatureVisualRigid.cpp
    src/CreatureVisualGltf.cpp
    src/CreatureVisualFactory.cpp
    src/PlayerController.cpp
    src/Core.cpp
    src/Cube.cpp
    src/CubeGL.cpp
    src/Object.cpp
    src/ObjectImplementation.cpp
    src/ObjectStorage.cpp
    src/Prefab.cpp
    src/TextureBase.cpp
    src/TextureCube.cpp
    src/User.cpp
    src/ViewEngine.cpp
    src/World.cpp
    src/WindowManager.cpp
    src/GeometryEngine.cpp
    src/render/GlStateScope.cpp
    src/render/GreedyShaderMode.cpp
    src/render/GreedyTransparentSort.cpp
    src/render/TransparentPass.cpp
    src/render/GreedyTransparentPipeline.cpp
    src/GridMath.cpp
    src/BlockRegistry.cpp
    src/BlockDefinition.cpp
    src/BlockDefinitionStorage.cpp
    src/AnimationClock.cpp
    src/BlockWorld.cpp
    src/WorldGenerator.cpp
    src/Noise.cpp
    src/ProceduralSettings.cpp
    src/ProceduralConfigIO.cpp
    src/PrefabUtil.cpp
    src/worldgen/WorldGenContext.cpp
    src/worldgen/IWorldGenPipeline.cpp
    src/worldgen/WorldGenStages.cpp
    src/worldgen/FlatPipeline.cpp
    src/worldgen/OverworldHeightSampler.cpp
    src/worldgen/OverworldPipeline.cpp
    src/worldgen/BiomeSampler.cpp
    src/worldgen/OverworldBiomesPipeline.cpp
    src/worldgen/CaveCarver.cpp
    src/worldgen/PrefabFeaturePlacer.cpp
    src/worldgen/OverworldFullPipeline.cpp
    src/worldgen/ProceduralWorldGenFactory.cpp
    src/BlockRaycast.cpp
    src/Chunk.cpp
    src/ChunkManager.cpp
    src/ChunkMeshCache.cpp
    src/ChunkStreamer.cpp
    src/GreedyMesher.cpp
    src/InputManager.cpp
    src/ShaderManager.cpp
    src/TextRenderer.cpp
    src/Application.cpp
    src/CursorCapture.cpp
    src/Game/GameSession.cpp
    src/Content/ContentTypeRegistry.cpp
    src/Commands/CommandRegistry.cpp
    src/ConsoleCommandHistory.cpp
    src/ConsoleInputSanitize.cpp
    src/Gui/GuiTheme.cpp
    src/Gui/GuiRenderer.cpp
    src/Gui/UiQuadBatch.cpp
    src/Gui/UiTexturedQuadBatch.cpp
    src/Gui/GuiIconSource.cpp
    src/Gui/PrefabIconCache.cpp
    src/Gui/GuiInputRouter.cpp
    src/Gui/GuiFocus.cpp
    src/Gui/GuiContext.cpp
    src/Gui/GuiScreenBase.cpp
    src/Gui/Layout/GuiLayout.cpp
    src/Gui/Widgets/GuiWidget.cpp
    src/Gui/Widgets/GuiPanel.cpp
    src/Gui/Widgets/GuiButton.cpp
    src/Gui/Widgets/GuiLabel.cpp
    src/Gui/Widgets/GuiTextInput.cpp
    src/Gui/Widgets/GuiPopupMenu.cpp
    src/Gui/Widgets/GuiListView.cpp
    src/Gui/Widgets/GuiTabBar.cpp
    src/Gui/Widgets/GuiScrollView.cpp
    src/Gui/Widgets/GuiDialogFrame.cpp
    src/Gui/Widgets/GuiWindow.cpp
    src/Gui/Widgets/GuiSlot.cpp
    src/Gui/Widgets/GuiCheckbox.cpp
    src/Gui/Widgets/WorldGenSettingsForm.cpp
    src/Gui/Screens/MainMenuScreen.cpp
    src/Gui/Screens/SettingsScreen.cpp
    src/Gui/Screens/NewWorldScreen.cpp
    src/Gui/Screens/LoadWorldScreen.cpp
    src/Gui/Screens/InGameHudScreen.cpp
    src/Gui/Screens/ConsoleScreen.cpp
    src/Gui/Screens/CreativePaletteScreen.cpp
)

set(HEADERS
    src/Camera.h
    src/AppSettingsSnapshot.h
    src/Core.h
    src/Cube.h
    src/CubeGL.h
    src/Object.h
    src/ObjectImplementation.h
    src/ObjectStorage.h
    src/Prefab.h
    src/TextureBase.h
    src/TextureCube.h
    src/User.h
    src/ViewEngine.h
    src/World.h
    src/WindowManager.h
    src/GeometryEngine.h
    src/BlockTypes.h
    src/GridMath.h
    src/BlockRegistry.h
    src/BlockDefinition.h
    src/BlockDefinitionStorage.h
    src/AnimationClock.h
    src/BlockWorld.h
    src/WorldGenerator.h
    src/Noise.h
    src/ProceduralSettings.h
    src/ProceduralConfigIO.h
    src/BlockRaycast.h
    src/Chunk.h
    src/ChunkManager.h
    src/ChunkMeshCache.h
    src/ChunkStreamer.h
    src/GreedyMesher.h
    src/InputManager.h
    src/ShaderManager.h
    src/TextRenderer.h
    src/stb_image.h
    src/Application.h
    src/CursorCapture.h
    src/Game/GameSession.h
    src/Content/ContentType.h
    src/Content/ContentTypeRegistry.h
    src/Commands/CommandRegistry.h
    src/Commands/CommandResult.h
    src/ConsoleCommandHistory.h
    src/ConsoleInputSanitize.h
    src/Gui/GuiTypes.h
    src/Gui/GuiTheme.h
    src/Gui/GuiRenderer.h
    src/Gui/UiQuadBatch.h
    src/Gui/UiTexturedQuadBatch.h
    src/Gui/GuiIconSource.h
    src/Gui/PrefabIconCache.h
    src/Gui/Interfaces/IGuiIconSource.h
    src/Gui/GuiInputRouter.h
    src/Gui/GuiFocus.h
    src/Gui/GuiContext.h
    src/Gui/GuiScreenBase.h
    src/Gui/Layout/GuiLayout.h
    src/Gui/Widgets/GuiWidget.h
    src/Gui/Widgets/GuiPanel.h
    src/Gui/Widgets/GuiButton.h
    src/Gui/Widgets/GuiLabel.h
    src/Gui/Widgets/GuiTextInput.h
    src/Gui/Widgets/GuiPopupMenu.h
    src/Gui/Widgets/GuiListView.h
    src/Gui/Widgets/GuiTabBar.h
    src/Gui/Widgets/GuiScrollView.h
    src/Gui/Widgets/GuiDialogFrame.h
    src/Gui/Widgets/GuiWindow.h
    src/Gui/Widgets/GuiSlot.h
    src/Gui/Widgets/GuiCheckbox.h
    src/Gui/Widgets/WorldGenSettingsForm.h
    src/Gui/Interfaces/IGuiMenuHost.h
    src/Gui/Interfaces/IGuiGameActions.h
    src/Gui/Interfaces/IHotbarViewModel.h
    src/Gui/Interfaces/IContentCatalog.h
    src/Gui/Interfaces/IGameCommandContext.h
    src/Gui/Interfaces/IGuiClipboard.h
    src/Gui/Screens/MainMenuScreen.h
    src/Gui/Screens/SettingsScreen.h
    src/Gui/Screens/NewWorldScreen.h
    src/Gui/Screens/LoadWorldScreen.h
    src/Gui/Screens/InGameHudScreen.h
    src/Gui/Screens/ConsoleScreen.h
    src/Gui/Screens/CreativePaletteScreen.h
)

if(WIN32)
    set(ICON_RESOURCE resources/Cubatarium.ico)
endif()

set(CMAKE_BINARY_DIR ${CMAKE_SOURCE_DIR}/bin)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${ICON_RESOURCE})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CUBATARIUM_GENERATED_DIR}
)

target_link_libraries(${PROJECT_NAME} 
    OpenGL::GL
    glfw
    glm::glm
    GLEW::GLEW
    Freetype::Freetype
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "WIN32")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:CUBATARIUM_DEBUG>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CMAKE_BINARY_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CMAKE_BINARY_DIR}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/shaders"
        "${CMAKE_BINARY_DIR}/shaders"
    COMMENT "Copying shaders to output directory"
)

# Seed bin/config.json from template only on first run (never overwrite runtime config).
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json.example")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_SRC="${CMAKE_SOURCE_DIR}/config.json.example"
            -DCONFIG_DST="${CMAKE_BINARY_DIR}/config.json"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_config_if_missing.cmake"
        COMMENT "Seeding config.json next to executable if missing"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/prefabs")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/prefabs"
            "${CMAKE_BINARY_DIR}/prefabs"
        COMMENT "Copying prefabs to output directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/models")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/models"
            "${CMAKE_BINARY_DIR}/models"
        COMMENT "Copying models to output directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/textures")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/textures"
            "${CMAKE_BINARY_DIR}/textures"
        COMMENT "Copying textures to output directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/content")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/content"
            "${CMAKE_BINARY_DIR}/content"
        COMMENT "Copying content data to output directory"
    )
endif()
