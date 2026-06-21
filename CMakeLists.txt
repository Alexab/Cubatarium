cmake_minimum_required(VERSION 3.15)

# Static CRT (/MT) and vcpkg libs for MSVC Windows (Release family + Debug in bin/).
if(WIN32 AND (MSVC OR CMAKE_GENERATOR MATCHES "Visual Studio"))
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet" FORCE)
    set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Copy DLL deps next to exe" FORCE)
endif()

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
if(WIN32 AND (MSVC OR CMAKE_GENERATOR MATCHES "Visual Studio"))
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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

if(NOT ANDROID)
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
endif()

set(STB_IMAGE_HEADER "${CMAKE_SOURCE_DIR}/src/ThirdParty/stb_image.h")
if(NOT EXISTS ${STB_IMAGE_HEADER})
    file(DOWNLOAD 
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        ${STB_IMAGE_HEADER}
    )
endif()

set(SOURCES
    src/ThirdParty/stb_image.cpp
    src/main.cpp
    src/App/Utils.cpp
    src/Render/Camera/Camera.cpp
    src/Creatures/Core/CreatureBounds.cpp
    src/Creatures/Core/CreatureCatalogTypes.cpp
    src/Creatures/Locomotion/CreatureLocomotionController.cpp
    src/Creatures/Locomotion/CreatureLocomotionFacts.cpp
    src/Creatures/Core/Creature.cpp
    src/Creatures/Core/CreatureInventory.cpp
    src/Creatures/Player/Player.cpp
    src/Activity/CreatureActivityDirector.cpp
    src/Activity/WorldCreatureActivitySink.cpp
    src/Activity/CreatureActivityRegistry.cpp
    src/Activity/Agents/WanderActivityAgent.cpp
    src/Creatures/Core/WorldCreatures.cpp
    src/Creatures/Environment/CreatureEnvironment.cpp
    src/Creatures/Definition/CreatureDefinitionStorage.cpp
    src/Creatures/Visual/CreatureTextureStorage.cpp
    src/Creatures/Definition/SkinDefinitionStorage.cpp
    src/Creatures/Visual/CreatureAppearance.cpp
    src/Creatures/Visual/CreatureVisualRigid.cpp
    src/Creatures/Visual/CreatureVisualGltf.cpp
    src/Creatures/Visual/CreatureVisualFactory.cpp
    src/Creatures/Player/PlayerController.cpp
    src/Pose/CreaturePosePresenterRegistry.cpp
    src/Pose/TerrestrialBipedPosePresenter.cpp
    src/Pose/TerrestrialQuadrupedPosePresenter.cpp
    src/Pose/AerialPosePresenter.cpp
    src/Pose/AquaticPosePresenter.cpp
    src/Pose/SerpentinePosePresenter.cpp
    src/App/Core.cpp
    src/App/Settings/UiSettings.cpp
    src/Blocks/Input/BlockInputController.cpp
    src/Render/Primitives/Cube.cpp
    src/Render/Primitives/CubeGL.cpp
    src/Storage/Object.cpp
    src/Storage/ObjectImplementation.cpp
    src/Storage/ObjectStorage.cpp
    src/World/Prefabs/Prefab.cpp
    src/Render/Textures/TextureBase.cpp
    src/Render/Textures/TextureCube.cpp
    src/Creatures/Player/User.cpp
    src/Render/Engine/ViewEngine.cpp
    src/World/Core/World.cpp
    src/App/Platform/WindowManager.cpp
    src/Render/Engine/GeometryEngine.cpp
    src/Render/Pipeline/GlStateScope.cpp
    src/Render/Pipeline/GreedyShaderMode.cpp
    src/Render/Pipeline/GreedyTransparentSort.cpp
    src/Render/Pipeline/TransparentPass.cpp
    src/Render/Pipeline/GreedyTransparentPipeline.cpp
    src/World/Math/GridMath.cpp
    src/Blocks/BlockRegistry.cpp
    src/Blocks/BlockDefinition.cpp
    src/Blocks/BlockDefinitionStorage.cpp
    src/ResourcePacks/ResourcePack.cpp
    src/ResourcePacks/ResourcePackResolver.cpp
    src/ResourcePacks/BlockMergeRegistry.cpp
    src/ResourcePacks/BlockNameUtil.cpp
    src/ResourcePacks/TextureOverrides.cpp
    src/ResourcePacks/ResourcePackSmoke.cpp
    src/ResourcePacks/PlaceholderTextureCache.cpp
    src/ResourcePacks/CreaturePackMerge.cpp
    src/Render/Engine/AnimationClock.cpp
    src/World/Core/BlockWorld.cpp
    src/WorldGen/Core/Noise.cpp
    src/WorldGen/Core/ProceduralSettings.cpp
    src/WorldGen/Core/ProceduralConfigIO.cpp
    src/WorldGen/Core/WorldSeedParser.cpp
    src/WorldGen/Core/WorldGeneratorRegistry.cpp
    src/WorldGen/Core/WorldGenPack.cpp
    src/WorldGen/Core/WorldGenContentReload.cpp
    src/World/Prefabs/PrefabUtil.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/IWorldGenPipeline.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Sampling/OverworldHeightSampler.cpp
    src/WorldGen/Sampling/ClimateSampler.cpp
    src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
    src/WorldGen/Pipelines/ComposableWorldGenPipeline.cpp
    src/WorldGen/Sampling/BiomeSampler.cpp
    src/WorldGen/Features/CaveCarver.cpp
    src/WorldGen/Features/RavineCarver.cpp
    src/WorldGen/Features/OreVeinPlacer.cpp
    src/WorldGen/Features/PrefabFeatureConfig.cpp
    src/WorldGen/Features/PrefabFeaturePlacer.cpp
    src/WorldGen/Core/PipelineChunkPopulator.cpp
    src/WorldGen/Core/ProceduralWorldGenFactory.cpp
    src/World/Raycast/BlockRaycast.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/Render/Mesh/ChunkMeshCache.cpp
    src/World/Chunks/ChunkStreamer.cpp
    src/World/Chunks/TerrainColumnUtil.cpp
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Chunks/ChunkGenerationToken.cpp
    src/World/Chunks/ChunkLoadScheduler.cpp
    src/World/Core/IBlockWriter.cpp
    src/World/IO/AsyncChunkIO.cpp
    src/Core/Jobs/JobThreadPool.cpp
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/AsyncMeshBuilder.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/App/Platform/InputManager.cpp
    src/Creatures/Locomotion/LocomotionTypes.cpp
    src/Creatures/Locomotion/LocomotionStateDerive.cpp
    src/Render/Engine/ShaderManager.cpp
    src/Render/Engine/TextRenderer.cpp
    src/App/Application.cpp
    src/App/Platform/CursorCapture.cpp
    src/Game/GameSession.cpp
    src/Content/ContentTypeRegistry.cpp
    src/Commands/CommandRegistry.cpp
    src/Console/ConsoleCommandHistory.cpp
    src/Console/ConsoleInputSanitize.cpp
    src/Gui/Core/GuiTheme.cpp
    src/Gui/Core/GuiScale.cpp
    src/Gui/Core/GuiRenderer.cpp
    src/Gui/Batch/UiQuadBatch.cpp
    src/Gui/Batch/UiTexturedQuadBatch.cpp
    src/Gui/Core/GuiIconSource.cpp
    src/Gui/Cache/PrefabIconCache.cpp
    src/Gui/Cache/CreatureIconCache.cpp
    src/Gui/Core/GuiInputRouter.cpp
    src/Gui/Core/GuiFocus.cpp
    src/Gui/Core/GuiContext.cpp
    src/Gui/Core/GuiScreenBase.cpp
    src/Gui/Layout/GuiLayout.cpp
    src/Gui/Layout/GuiTooltipLayout.cpp
    src/Gui/Widgets/GuiWidget.cpp
    src/Gui/Widgets/GuiPanel.cpp
    src/Gui/Widgets/GuiButton.cpp
    src/Gui/Widgets/GuiLabel.cpp
    src/Gui/Widgets/GuiTextInput.cpp
    src/Gui/Widgets/GuiPopupMenu.cpp
    src/Gui/Widgets/GuiListView.cpp
    src/Gui/Widgets/GuiCheckList.cpp
    src/Gui/Widgets/GuiTabBar.cpp
    src/Gui/Widgets/GuiScrollView.cpp
    src/Gui/Widgets/GuiDialogFrame.cpp
    src/Gui/Widgets/GuiWindow.cpp
    src/Gui/Widgets/GuiSlot.cpp
    src/Gui/Widgets/GuiCheckbox.cpp
    src/Gui/Widgets/WorldGenSettingsForm.cpp
    src/Gui/Widgets/ResourcePackPickerForm.cpp
    src/Gui/Screens/MainMenuScreen.cpp
    src/Gui/Screens/SettingsScreen.cpp
    src/Gui/Screens/NewWorldScreen.cpp
    src/Gui/Screens/LoadWorldScreen.cpp
    src/Gui/Screens/WorldResourcePacksScreen.cpp
    src/Gui/Screens/InGameHudScreen.cpp
    src/Gui/Screens/ConsoleScreen.cpp
    src/Gui/Screens/CreativePaletteScreen.cpp
    src/App/Platform/AppRunner.cpp
    src/App/Platform/DesktopPlatformPaths.cpp
    src/App/Platform/DesktopPlatformWindow.cpp
    src/App/Platform/IPlatformPaths.cpp
    src/App/Platform/Log.cpp
)

set(HEADERS
    src/Render/Camera/Camera.h
    src/App/Settings/AppSettingsSnapshot.h
    src/App/Core.h
    src/Render/Primitives/Cube.h
    src/Render/Primitives/CubeGL.h
    src/Storage/Object.h
    src/Storage/ObjectImplementation.h
    src/Storage/ObjectStorage.h
    src/World/Prefabs/Prefab.h
    src/Render/Textures/TextureBase.h
    src/Render/Textures/TextureCube.h
    src/Creatures/Player/User.h
    src/Render/Engine/ViewEngine.h
    src/World/Core/World.h
    src/App/Platform/WindowManager.h
    src/Blocks/Input/BlockInputController.h
    src/Render/Engine/GeometryEngine.h
    src/World/Math/BlockTypes.h
    src/World/Math/GridMath.h
    src/Blocks/BlockRegistry.h
    src/Blocks/BlockDefinition.h
    src/Blocks/BlockDefinitionStorage.h
    src/ResourcePacks/ResourcePack.h
    src/ResourcePacks/ResourcePackResolver.h
    src/ResourcePacks/BlockMergeRegistry.h
    src/ResourcePacks/BlockNameUtil.h
    src/ResourcePacks/TextureOverrides.h
    src/ResourcePacks/PlaceholderTextureCache.h
    src/Render/Engine/AnimationClock.h
    src/World/Core/BlockWorld.h
    src/WorldGen/Core/WorldGeneratorDescriptor.h
    src/WorldGen/Core/WorldGenPack.h
    src/WorldGen/Pipelines/ComposableWorldGenerator.h
    src/WorldGen/Core/Noise.h
    src/WorldGen/Core/ProceduralSettings.h
    src/WorldGen/Core/ProceduralConfigIO.h
    src/World/Raycast/BlockRaycast.h
    src/World/Chunks/Chunk.h
    src/World/Chunks/ChunkManager.h
    src/Render/Mesh/ChunkMeshCache.h
    src/World/Chunks/ChunkStreamer.h
    src/Render/Mesh/GreedyMesher.h
    src/Render/Mesh/GreedyMeshBatch.h
    src/App/Platform/InputManager.h
    src/Render/Engine/ShaderManager.h
    src/Render/Engine/TextRenderer.h
    src/ThirdParty/stb_image.h
    src/App/Application.h
    src/App/Platform/CursorCapture.h
    src/Game/GameSession.h
    src/Content/ContentType.h
    src/Content/ContentTypeRegistry.h
    src/Commands/CommandRegistry.h
    src/Commands/CommandResult.h
    src/Console/ConsoleCommandHistory.h
    src/Console/ConsoleInputSanitize.h
    src/Gui/Core/GuiTypes.h
    src/Gui/Core/GuiTheme.h
    src/Gui/Core/GuiRenderer.h
    src/Gui/Batch/UiQuadBatch.h
    src/Gui/Batch/UiTexturedQuadBatch.h
    src/Gui/Core/GuiIconSource.h
    src/Gui/Cache/PrefabIconCache.h
    src/Gui/Interfaces/IGuiIconSource.h
    src/Gui/Core/GuiInputRouter.h
    src/Gui/Core/GuiFocus.h
    src/Gui/Core/GuiContext.h
    src/Gui/Core/GuiScreenBase.h
    src/Gui/Layout/GuiLayout.h
    src/Gui/Layout/GuiTooltipLayout.h
    src/Gui/Widgets/GuiWidget.h
    src/Gui/Widgets/GuiPanel.h
    src/Gui/Widgets/GuiButton.h
    src/Gui/Widgets/GuiLabel.h
    src/Gui/Widgets/GuiTextInput.h
    src/Gui/Widgets/GuiPopupMenu.h
    src/Gui/Widgets/GuiListView.h
    src/Gui/Widgets/GuiCheckList.h
    src/Gui/Widgets/GuiTabBar.h
    src/Gui/Widgets/GuiScrollView.h
    src/Gui/Widgets/GuiDialogFrame.h
    src/Gui/Widgets/GuiWindow.h
    src/Gui/Widgets/GuiSlot.h
    src/Gui/Widgets/GuiCheckbox.h
    src/Gui/Widgets/WorldGenSettingsForm.h
    src/Gui/Widgets/ResourcePackPickerForm.h
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

# --- Android native library ---
if(ANDROID)
    set(CUBATARIUM_DESKTOP_ONLY
        src/main.cpp
        src/App/Utils.cpp
        src/App/Platform/WindowManager.cpp
        src/App/Platform/InputManager.cpp
        src/App/Platform/CursorCapture.cpp
        src/App/Platform/DesktopPlatformPaths.cpp
        src/App/Platform/DesktopPlatformWindow.cpp
    )
    set(CUBATARIUM_ANDROID_SOURCES
        platforms/android/native/android_main.cpp
        platforms/android/native/egl_context.cpp
        platforms/android/native/android_jni.cpp
        platforms/android/native/android_soft_keyboard.cpp
        src/App/Platform/AppRunner.cpp
        src/App/Platform/AndroidPlatformPaths.cpp
        src/App/Platform/AndroidPlatformWindow.cpp
        src/App/Platform/TouchInputBridge.cpp
        src/Gui/Widgets/GuiTouchControls.cpp
        src/App/Platform/Log.cpp
        src/App/Platform/IPlatformPaths.cpp
    )
    set(CUBATARIUM_COMMON_SOURCES ${SOURCES})
    foreach(_desktop_only ${CUBATARIUM_DESKTOP_ONLY})
        list(REMOVE_ITEM CUBATARIUM_COMMON_SOURCES ${_desktop_only})
    endforeach()

    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 1.0.1
    )
    FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_Declare(
        freetype
        GIT_REPOSITORY https://github.com/freetype/freetype.git
        GIT_TAG VER-2-13-2
    )
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm json freetype)

    add_library(cubatarium SHARED
        ${CUBATARIUM_COMMON_SOURCES}
        ${CUBATARIUM_ANDROID_SOURCES}
    )

    target_include_directories(cubatarium PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/platforms/android/native
        ${CUBATARIUM_GENERATED_DIR}
    )

    target_compile_definitions(cubatarium PRIVATE
        CUBATARIUM_GLES=1
        $<$<CONFIG:Debug>:CUBATARIUM_DEBUG>
    )

    find_package(game-activity REQUIRED CONFIG)

    # Keep GameActivity JNI entry points when linking the static prefab.
    set(CMAKE_SHARED_LINKER_FLAGS
        "${CMAKE_SHARED_LINKER_FLAGS} -u Java_com_google_androidgamesdk_GameActivity_initializeNativeCode")

    find_library(android-lib android)
    find_library(log-lib log)
    find_library(egl-lib EGL)
    find_library(glesv3-lib GLESv3)

    target_link_libraries(cubatarium
        glm::glm
        nlohmann_json::nlohmann_json
        freetype
        game-activity::game-activity_static
        ${android-lib}
        ${log-lib}
        ${egl-lib}
        ${glesv3-lib}
    )

    return()
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

if(EXISTS "${CMAKE_SOURCE_DIR}/resource_packs")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resource_packs"
            "${CMAKE_BINARY_DIR}/resource_packs"
        COMMENT "Copying resource packs to output directory"
    )
endif()
