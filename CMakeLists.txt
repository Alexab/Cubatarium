cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Static CRT (/MT) and vcpkg libs for MSVC Windows. Runtime output goes to bin/.
if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet" FORCE)
    set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Copy DLL deps next to exe" FORCE)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg manifest mode" FORCE)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library (/MT)" FORCE)
endif()

project(Cubatarium VERSION 0.0.1.1 LANGUAGES CXX)

option(CUBATARIUM_ENABLE_TSAN "Build with ThreadSanitizer (Linux Clang/GCC)" OFF)
if(CUBATARIUM_ENABLE_TSAN)
    if((CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU") AND NOT WIN32)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    else()
        message(WARNING "CUBATARIUM_ENABLE_TSAN is only supported on Linux with Clang/GCC")
    endif()
endif()

if(WIN32)
    include(${CMAKE_SOURCE_DIR}/cmake/AssertWindowsStatic.cmake)
endif()

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

set(STB_IMAGE_WRITE_HEADER "${CMAKE_SOURCE_DIR}/src/ThirdParty/stb_image_write.h")
if(NOT EXISTS ${STB_IMAGE_WRITE_HEADER})
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
        ${STB_IMAGE_WRITE_HEADER}
    )
endif()

set(SOURCES
    src/ThirdParty/stb_image.cpp
    src/ThirdParty/stb_image_write.cpp
    src/main.cpp
    src/App/Utils.cpp
    src/App/CreateWorldCli.cpp
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
    src/Activity/Helpers/CreatureActivitySteering.cpp
    src/Activity/Helpers/CreatureActivityNavigation.cpp
    src/Activity/Agents/FleeActivityAgent.cpp
    src/Activity/Agents/MeleeAttackActivityAgent.cpp
    src/Activity/Brain/SimpleFsmBrain.cpp
    src/Navigation/WorldNavigationQueries.cpp
    src/Navigation/UNavigationPathfinder.cpp
    src/Navigation/UWaypointFollower.cpp
    src/World/Environment/CreatureSpatialIndex.cpp
    src/Creatures/Core/WorldCreatures.cpp
    src/Creatures/Environment/CreatureEnvironment.cpp
    src/Creatures/Definition/CreatureDefinitionStorage.cpp
    src/Creatures/Visual/CreatureTextureStorage.cpp
    src/Creatures/Definition/SkinDefinitionStorage.cpp
    src/Creatures/Visual/CreatureAppearance.cpp
    src/Creatures/Visual/CreatureTextureResolver.cpp
    src/Creatures/Visual/CreatureRootTransform.cpp
    src/Creatures/Visual/CreatureDrawQueue.cpp
    src/Creatures/Visual/CreatureMeshGpuCache.cpp
    src/Creatures/Visual/CreatureBonePaletteGpu.cpp
    src/Creatures/Visual/CreatureRigidModelLoader.cpp
    src/Creatures/Visual/CreatureVisualRigid.cpp
    src/Creatures/Visual/CreatureVisualBoneSkeleton.cpp
    src/Creatures/Visual/CreatureVisualGltf.cpp
    src/Creatures/Visual/CreatureVisualSprite.cpp
    src/Creatures/Visual/CreatureVisualFactory.cpp
    src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp
    src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp
    src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
    src/Creatures/Visual/Gltf/CreatureGltfCache.cpp
    src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
    src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonCache.cpp
    src/Creatures/Player/PlayerController.cpp
    src/Pose/CreaturePosePresenterRegistry.cpp
    src/Pose/TerrestrialBipedPosePresenter.cpp
    src/Pose/TerrestrialQuadrupedPosePresenter.cpp
    src/Pose/AerialPosePresenter.cpp
    src/Pose/AquaticPosePresenter.cpp
    src/Pose/SerpentinePosePresenter.cpp
    src/Pose/BoneSkeleton/SkeletalAnimationProfiles.cpp
    src/Pose/BoneSkeleton/BoneSkeletonPoseEngine.cpp
    src/App/Core.cpp
    src/App/RuntimeTuningConfig.cpp
    src/App/WorldLifecycleFacade.cpp
    src/App/ResourcePackBootstrap.cpp
    src/App/ResourcePackSelectionUtil.cpp
    src/App/LegacyConfigAdapter.cpp
    src/App/Settings/RenderSettings.cpp
    src/App/Settings/UiSettings.cpp
    src/Blocks/Input/BlockInputController.cpp
    src/Render/Primitives/Cube.cpp
    src/Render/Primitives/CubeGL.cpp
    src/World/Objects/ObjectLibrary.cpp
    src/Render/Textures/TextureBase.cpp
    src/Render/Textures/TextureCube.cpp
    src/Creatures/Player/User.cpp
    src/Render/Engine/ViewEngine.cpp
    src/World/Core/World.cpp
    src/World/Adapters/WorldRenderReadAdapter.cpp
    src/World/Core/WorldViewBinding.cpp
    src/World/Core/RuntimeTuning.cpp
    src/World/Core/WorldFluidFacade.cpp
    src/World/Core/WorldCreatureFacade.cpp
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/World/Collision/WorldCollision.cpp
    src/World/Collision/VoxelDdaTraversal.cpp
    src/World/Core/WorldCooperativeOps.cpp
    src/World/Core/WorldLoadDiagnostics.cpp
    src/World/Environment/WorldEnvironment.cpp
    src/World/Environment/EnvironmentConfig.cpp
    src/World/Environment/WeatherBiomeUtil.cpp
    src/World/Environment/WeatherAutoController.cpp
    src/World/Lighting/LightUtil.cpp
    src/World/Lighting/ChunkLighting.cpp
    src/World/Lighting/ChunkRelightSnapshot.cpp
    src/World/Lighting/AsyncRelightBuilder.cpp
    src/World/Persistence/WorldPersistence.cpp
    src/World/Streaming/WorldStreaming.cpp
    src/World/Streaming/ChunkEmergeCoordinator.cpp
    src/World/Physics/WorldPhysicsScheduler.cpp
    src/World/Physics/WorldMovementPhysicsService.cpp
    src/World/Physics/WorldBlockPhysicsService.cpp
    src/World/Physics/WorldChunkDirtyService.cpp
    src/World/Physics/ChunkRebuildQueue.cpp
    src/World/Physics/IUChunkDirtyService.cpp
    src/World/Physics/BlockUpdateQueue.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/FallingBlockRules.cpp
    src/World/Physics/FallingBlockRulesRegistry.cpp
    src/World/Physics/FallingBlocksSystem.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidReflowScan.cpp
    src/World/Math/FluidCellState.cpp
    src/World/Physics/PrimitivePhysicsProfile.cpp
    src/World/Physics/StandardPhysicsProfile.cpp
    src/World/Physics/AdvancedPhysicsProfile.cpp
    src/World/Physics/PhysicsProfileFactory.cpp
    src/World/Physics/CollisionReadiness.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/World/Physics/ChunkPhysicsSeed.cpp
    src/World/Physics/MaterialReactionRules.cpp
    src/World/Physics/MaterialReactionRulesRegistry.cpp
    src/World/Physics/Replay/ReplayWorldFixture.cpp
    src/World/Diagnostics/MovementDiagnosticsRecorder.cpp
    src/World/Mesh/WorldMeshService.cpp
    src/World/Mesh/WorldMeshDirtyPolicy.cpp
    src/App/Platform/WindowManager.cpp
    src/Render/Engine/GeometryEngine.cpp
    src/Render/Engine/FluidSurfaceMap.cpp
    src/Render/Engine/UnderwaterFogPass.cpp
    src/Render/Engine/SkyGradientPass.cpp
    src/Render/Engine/CreatureDrawPass.cpp
    src/Render/Engine/RenderMeshSink.cpp
    src/Render/Engine/GreedyGpuBackend.cpp
    src/Render/Engine/GreedyVertexPool.cpp
    src/Render/Engine/CrossGpuBackend.cpp
    src/Render/Engine/DistanceFog.cpp
    src/Render/Engine/HorizonFogColor.cpp
    src/Render/Pipeline/GlStateScope.cpp
    src/Render/Pipeline/GreedyShaderMode.cpp
    src/Render/Pipeline/GreedyTransparentSort.cpp
    src/Render/Pipeline/TransparentPass.cpp
    src/Render/Pipeline/GreedyTransparentPipeline.cpp
    src/Render/Pipeline/GreedyDrawCategory.cpp
    src/Render/Pipeline/OpaqueDepthCapture.cpp
    src/Render/Pipeline/WeatherDepthCapture.cpp
    src/Render/Weather/WeatherParticleSystem.cpp
    src/Render/Weather/WeatherRenderPass.cpp
    src/Render/Weather/WeatherExposure.cpp
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
    src/World/Core/BlockCountTracker.cpp
    src/WorldGen/Core/Noise.cpp
    src/WorldGen/Core/ProceduralSettings.cpp
    src/WorldGen/Core/ProceduralConfigIO.cpp
    src/WorldGen/Core/WorldSeedParser.cpp
    src/WorldGen/Core/WorldGeneratorRegistry.cpp
    src/WorldGen/Core/WorldGenSets.cpp
    src/WorldGen/Core/WorldGenContentReload.cpp
    src/World/Objects/ObjectUtil.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenStageMask.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/BlockWorldColumnWriter.cpp
    src/WorldGen/Core/ColumnWriteContext.h
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/WorldGenPack.cpp
    src/WorldGen/Core/IUWorldGenPipeline.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Stages/DensityTerrainFill.cpp
    src/WorldGen/Sampling/OverworldHeightSampler.cpp
    src/WorldGen/Sampling/ClimateSampler.cpp
    src/WorldGen/Sampling/TerrainClimateMapper.cpp
    src/WorldGen/Sampling/DensityRouter.cpp
    src/WorldGen/Sampling/DensityFieldSampler.cpp
    src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
    src/WorldGen/Pipelines/WorldGenStageRunner.cpp
    src/WorldGen/Pipelines/ColumnGenerationService.cpp
    src/WorldGen/Sampling/BiomeSampler.cpp
    src/WorldGen/Sampling/BiomeRegistry.cpp
    src/WorldGen/Sampling/ColumnSample.cpp
    src/WorldGen/Features/CaveCarver.cpp
    src/WorldGen/Features/CaveDepthBand.cpp
    src/WorldGen/Features/RavineCarver.cpp
    src/WorldGen/Features/OreVeinPlacer.cpp
    src/WorldGen/Features/ObjectFeatureConfig.cpp
    src/WorldGen/Features/ObjectPlacementConstraints.cpp
    src/WorldGen/Features/ObjectFeaturePlacer.cpp
    src/WorldGen/Features/BuiltinWorldGenFeatures.cpp
    src/WorldGen/Core/PipelineChunkPopulator.cpp
    src/WorldGen/Core/ProceduralWorldGenFactory.cpp
    src/World/Raycast/BlockRaycast.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/Render/Mesh/ChunkDirtySet.cpp
    src/Render/Mesh/ChunkMeshCache.cpp
    src/Render/Mesh/FluidSurfaceColumnSlice.cpp
    src/World/Chunks/ChunkStreamer.cpp
    src/World/Chunks/TerrainColumnUtil.cpp
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Chunks/ChunkGenerationToken.cpp
    src/World/Chunks/ChunkLoadScheduler.cpp
    src/World/Chunks/ChunkLoadPriority.cpp
    src/World/Chunks/StreamingAltitudePolicy.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/IO/AsyncChunkIO.cpp
    src/World/IO/ChunkStorageTypes.cpp
    src/World/IO/JsonChunkSerializer.cpp
    src/World/IO/BinaryChunkSerializer.cpp
    src/World/IO/ChunkStorageService.cpp
    src/Core/Jobs/JobThreadPool.cpp
    src/Core/Jobs/JobThreadBudget.cpp
    src/Core/Progress/LatestProgressSink.cpp
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/AsyncMeshBuilder.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/App/Platform/InputManager.cpp
    src/Creatures/Locomotion/LocomotionTypes.cpp
    src/Creatures/Locomotion/LocomotionStateDerive.cpp
    src/Render/Engine/ShaderManager.cpp
    src/Render/Engine/TextRenderer.cpp
    src/App/Application.cpp
    src/App/Navigation/ScreenNavigator.cpp
    src/App/Input/InputRouter.cpp
    src/App/WorldOperationRunner.cpp
    src/App/Platform/CursorCapture.cpp
    src/Game/GameSession.cpp
    src/Game/CreatureVisualQaSpawner.cpp
    src/Content/ContentTypeRegistry.cpp
    src/Commands/CommandRegistry.cpp
    src/Commands/WorldCommands.cpp
    src/Console/ConsoleCommandHistory.cpp
    src/Console/ConsoleInputSanitize.cpp
    src/Gui/Core/GuiTheme.cpp
    src/Gui/Core/GuiScale.cpp
    src/Gui/Core/GuiMetrics.cpp
    src/Gui/Core/GuiRenderer.cpp
    src/Gui/Batch/UiQuadBatch.cpp
    src/Gui/Batch/UiTexturedQuadBatch.cpp
    src/Gui/Core/GuiIconSource.cpp
    src/Gui/Preview/ContentPreviewRenderer.cpp
    src/Gui/Preview/CreaturePreviewRenderer.cpp
    src/Gui/Preview/CreaturePreviewLayout.cpp
    src/Gui/Preview/ObjectPreviewLayout.cpp
    src/Gui/Preview/ContentPreviewDock.cpp
    src/Gui/Layout/DockedOverlayLayout.cpp
    src/Gui/Cache/InventoryIconService.cpp
    src/Gui/Cache/ObjectIconCache.cpp
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
    src/Gui/Widgets/GuiProgressBar.cpp
    src/Gui/Widgets/GuiSlider.cpp
    src/Gui/Widgets/GuiTextInput.cpp
    src/Gui/Widgets/GuiPopupMenu.cpp
    src/Gui/Widgets/GuiListView.cpp
    src/Gui/Widgets/GuiCheckList.cpp
    src/Gui/Widgets/GuiTabBar.cpp
    src/Gui/Widgets/GuiScrollView.cpp
    src/Gui/Widgets/GuiDialogFrame.cpp
    src/Gui/Widgets/GuiWindow.cpp
    src/Gui/Widgets/GuiSlot.cpp
    src/Gui/Widgets/GuiPreviewViewport.cpp
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
    src/Gui/Screens/WorldGenPaletteScreen.cpp
    src/Gui/Screens/WorldProgressScreen.cpp
    src/App/Platform/AppRunner.cpp
    src/App/Platform/DesktopPlatformPaths.cpp
    src/App/Platform/GameDataRoot.cpp
    src/App/Platform/DesktopPlatformWindow.cpp
    src/App/Platform/IUPlatformPaths.cpp
    src/App/Platform/Log.cpp
)

set(HEADERS
    src/Render/Camera/Camera.h
    src/App/Settings/AppSettingsSnapshot.h
    src/App/Core.h
    src/App/WorldLifecycleFacade.h
    src/App/ResourcePackBootstrap.h
    src/App/ResourcePackSelectionUtil.h
    src/Render/Primitives/Cube.h
    src/Render/Primitives/CubeGL.h
    src/World/Objects/ObjectLibrary.h
    src/World/Objects/ObjectUtil.h
    src/Render/Textures/TextureBase.h
    src/Render/Textures/TextureCube.h
    src/Creatures/Player/User.h
    src/Render/Engine/ViewEngine.h
    src/World/Core/World.h
    src/World/Core/WorldViewBinding.h
    src/World/Collision/WorldCollision.h
    src/World/Core/WorldCooperativeOps.h
    src/World/Core/WorldLoadDiagnostics.h
    src/World/Environment/WorldEnvironment.h
    src/World/Environment/EnvironmentConfig.h
    src/World/Environment/WeatherBiomeUtil.h
    src/World/Environment/WeatherAutoController.h
    src/World/Lighting/LightUtil.h
    src/World/Lighting/ChunkLighting.h
    src/World/Lighting/ChunkRelightSnapshot.h
    src/World/Lighting/AsyncRelightBuilder.h
    src/World/Persistence/WorldPersistence.h
    src/World/Streaming/WorldStreaming.h
    src/World/Streaming/ChunkEmergeCoordinator.h
    src/World/Interfaces/IUWorldMeshSink.h
    src/World/Interfaces/IUWorldRenderReadModel.h
    src/World/Adapters/WorldRenderReadAdapter.h
    src/World/Mesh/WorldMeshService.h
    src/App/Platform/WindowManager.h
    src/Blocks/Input/BlockInputController.h
    src/Render/Engine/GeometryEngine.h
    src/Render/Engine/UnderwaterFogPass.h
    src/Render/Engine/GreedyGpuBackend.h
    src/Render/Engine/CrossGpuBackend.h
    src/Render/Engine/DistanceFog.h
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
    src/WorldGen/Core/WorldGenSets.h
    src/WorldGen/Core/WorldGenPack.h
    src/WorldGen/Core/WorldGenStageId.h
    src/WorldGen/Core/WorldGenStageMask.h
    src/WorldGen/Core/ColumnHash.h
    src/WorldGen/Core/IUColumnWriter.h
    src/WorldGen/Core/BlockWorldColumnWriter.h
    src/WorldGen/Core/WorldGenBlockResolver.h
    src/WorldGen/Core/ColumnWriteContext.h
    src/WorldGen/Pipelines/ColumnGenerationService.h
    src/WorldGen/Features/ObjectPlacementConstraints.h
    src/WorldGen/Features/BuiltinWorldGenFeatures.h
    src/WorldGen/Sampling/BiomeRegistry.h
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
    src/World/Mesh/IUChunkMeshReader.h
    src/Render/Mesh/GreedyMeshBatch.h
    src/App/Platform/InputManager.h
    src/Render/Engine/ShaderManager.h
    src/Render/Engine/TextRenderer.h
    src/ThirdParty/stb_image.h
    src/ThirdParty/stb_image_write.h
    src/App/Application.h
    src/App/Navigation/ScreenNavigator.h
    src/App/Input/InputRouter.h
    src/App/Platform/CursorCapture.h
    src/Game/Interfaces/IUGameContent.h
    src/Game/GameSession.h
    src/Content/ContentType.h
    src/Content/ContentTypeRegistry.h
    src/Commands/CommandRegistry.h
    src/Commands/CommandResult.h
    src/Console/ConsoleCommandHistory.h
    src/Console/ConsoleInputSanitize.h
    src/Gui/Core/GuiTypes.h
    src/Gui/Core/GuiTheme.h
    src/Gui/Core/GuiMetrics.h
    src/Gui/Core/GuiBreakpoints.h
    src/Gui/Core/GuiRenderer.h
    src/Gui/Batch/UiQuadBatch.h
    src/Gui/Batch/UiTexturedQuadBatch.h
    src/Gui/Core/GuiIconSource.h
    src/Gui/Preview/ContentPreviewRenderer.h
    src/Gui/Preview/CreaturePreviewRenderer.h
    src/Gui/Preview/CreaturePreviewLayout.h
    src/Gui/Preview/ObjectPreviewLayout.h
    src/Gui/Preview/ContentPreviewDock.h
    src/Gui/Layout/DockedOverlayLayout.h
    src/Gui/Cache/InventoryIconService.h
    src/Gui/Cache/ObjectIconCache.h
    src/Gui/Interfaces/IUGuiIconSource.h
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
    src/Gui/Widgets/GuiProgressBar.h
    src/Gui/Widgets/GuiSlider.h
    src/Gui/Widgets/GuiTextInput.h
    src/Gui/Widgets/GuiPopupMenu.h
    src/Gui/Widgets/GuiListView.h
    src/Gui/Widgets/GuiCheckList.h
    src/Gui/Widgets/GuiTabBar.h
    src/Gui/Widgets/GuiScrollView.h
    src/Gui/Widgets/GuiDialogFrame.h
    src/Gui/Widgets/GuiWindow.h
    src/Gui/Widgets/GuiSlot.h
    src/Gui/Widgets/GuiPreviewViewport.h
    src/Gui/Widgets/GuiCheckbox.h
    src/Gui/Widgets/WorldGenSettingsForm.h
    src/Gui/Widgets/ResourcePackPickerForm.h
    src/Gui/Interfaces/IUGuiMenuHost.h
    src/Gui/Interfaces/IUGuiGameActions.h
    src/Gui/Interfaces/IUHotbarViewModel.h
    src/Gui/Interfaces/IUContentCatalog.h
    src/Gui/Interfaces/IUGameCommandContext.h
    src/Gui/Interfaces/IUGuiClipboard.h
    src/Gui/Screens/MainMenuScreen.h
    src/Gui/Screens/SettingsScreen.h
    src/Gui/Screens/NewWorldScreen.h
    src/Gui/Screens/LoadWorldScreen.h
    src/Gui/Screens/InGameHudScreen.h
    src/Gui/Screens/ConsoleScreen.h
    src/Gui/Screens/WorldGenPaletteScreen.h
    src/Gui/Screens/CreativePaletteScreen.h
    src/Gui/Screens/WorldProgressScreen.h
    src/Core/Progress/ProgressTypes.h
    src/Core/Progress/IUProgressSink.h
    src/Core/Sort/CatalogSortUtil.h
    src/App/WorldOperationRunner.h
)

if(WIN32)
    set(CUBATARIUM_WIN_RC "${CMAKE_SOURCE_DIR}/resources/branding/Cubatarium.rc")
endif()

# --- Android native library ---
if(ANDROID)
    set(CUBATARIUM_DESKTOP_ONLY
        src/main.cpp
        src/App/Utils.cpp
    src/App/CreateWorldCli.cpp
        src/App/Platform/WindowManager.cpp
        src/App/Platform/InputManager.cpp
        src/App/Platform/CursorCapture.cpp
        src/App/Platform/DesktopPlatformPaths.cpp
        src/App/Platform/GameDataRoot.cpp
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
        src/App/Platform/IUPlatformPaths.cpp
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
    target_link_options(cubatarium PRIVATE -rdynamic)

    return()
endif()

set(CUBATARIUM_RUN_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Runtime directory (executable + game data)")
file(MAKE_DIRECTORY "${CUBATARIUM_RUN_DIR}")

set(CUBATARIUM_DESKTOP_SOURCES ${SOURCES} ${HEADERS})
if(WIN32 AND EXISTS "${CUBATARIUM_WIN_RC}")
    list(APPEND CUBATARIUM_DESKTOP_SOURCES "${CUBATARIUM_WIN_RC}")
endif()

add_executable(${PROJECT_NAME} ${CUBATARIUM_DESKTOP_SOURCES})

add_executable(chunk_load_priority_test
    src/World/Chunks/ChunkLoadPriority.cpp
    src/Test/ChunkLoadPriorityTest.cpp)
target_include_directories(chunk_load_priority_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(chunk_load_priority_test glm::glm)

add_executable(terrain_column_util_test
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Chunks/TerrainColumnUtil.cpp
    src/World/Core/BlockWorld.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Math/FluidCellState.cpp
    src/World/Math/GridMath.cpp
    src/Test/TerrainColumnUtilTest.cpp)
target_include_directories(terrain_column_util_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(terrain_column_util_test glm::glm)

add_executable(world_mesh_service_test
    src/Render/Mesh/ChunkDirtySet.cpp
    src/Test/WorldMeshServiceTest.cpp)
target_include_directories(world_mesh_service_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(world_mesh_service_test glm::glm)

add_executable(navigation_pathfinder_test
    src/Navigation/UNavigationPathfinder.cpp
    src/Navigation/UWaypointFollower.cpp
    src/Test/NavigationPathfinderTest.cpp)
target_include_directories(navigation_pathfinder_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(navigation_pathfinder_test glm::glm)

add_executable(creature_activity_steering_test
    src/Activity/Helpers/CreatureActivitySteering.cpp
    src/Test/CreatureActivitySteeringTest.cpp)
target_include_directories(creature_activity_steering_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_activity_steering_test glm::glm)

add_executable(wander_interval_test
    src/Activity/Agents/WanderActivityAgent.cpp
    src/Activity/Helpers/CreatureActivitySteering.cpp
    src/Test/WanderIntervalTest.cpp)
target_include_directories(wander_interval_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(wander_interval_test glm::glm)

add_executable(creature_skinned_draw_test
    src/Creatures/Visual/CreatureBonePaletteGpu.cpp
    src/Test/CreatureSkinnedDrawTest.cpp)
target_include_directories(creature_skinned_draw_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_skinned_draw_test OpenGL::GL glm::glm GLEW::GLEW)

add_executable(creature_bone_skeleton_loader_test
    src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
    src/Test/CreatureBoneSkeletonLoaderTest.cpp)
target_include_directories(creature_bone_skeleton_loader_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_bone_skeleton_loader_test glm::glm nlohmann_json::nlohmann_json)

add_executable(creature_gltf_loader_test
    src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp
    src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp
    src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
    src/Test/CreatureGltfLoaderTest.cpp)
target_include_directories(creature_gltf_loader_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_gltf_loader_test glm::glm nlohmann_json::nlohmann_json)

set(PHYSICS_BLOCKWORLD_TEST_SOURCES
    src/World/Core/BlockWorld.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Math/GridMath.cpp
    src/World/Math/FluidCellState.cpp
    src/Blocks/BlockDefinitionStorage.cpp
    src/Blocks/BlockDefinition.cpp)

set(PHYSICS_FLUID_SIM_SOURCES
    src/World/Core/RuntimeTuning.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp)

set(PHYSICS_LIGHT_REGISTRY_DEF CUTUM_PHYSICS_LIGHT_REGISTRY)

set(PHYSICS_COLLISION_UNIT_SOURCES
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Blocks/BlockRegistry.cpp
    src/World/Core/RuntimeTuning.cpp
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/World/Physics/MaterialReactionRulesRegistry.cpp
    src/World/Collision/WorldCollision.cpp
    src/World/Collision/VoxelDdaTraversal.cpp
    src/World/Physics/CollisionReadiness.cpp
    src/World/Raycast/BlockRaycast.cpp
    src/Creatures/Core/CreatureBounds.cpp
    src/Render/Primitives/Cube.cpp)

set(PHYSICS_MATERIAL_UNIT_SOURCES
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Blocks/BlockRegistry.cpp
    src/World/Core/RuntimeTuning.cpp
    src/World/Physics/MaterialReactionRules.cpp
    src/World/Physics/MaterialReactionRulesRegistry.cpp)

add_executable(block_update_queue_purge_test
    src/World/Physics/BlockUpdateQueue.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/BlockUpdateQueuePurgeTest.cpp)
target_include_directories(block_update_queue_purge_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(block_update_queue_purge_test glm::glm)

add_executable(liquid_queue_priority_test
    src/World/Physics/FluidUpdateSet.cpp
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/LiquidQueuePriorityTest.cpp)
target_include_directories(liquid_queue_priority_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(liquid_queue_priority_test glm::glm)

add_executable(chunk_rebuild_queue_test
    src/World/Physics/ChunkRebuildQueue.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/ChunkRebuildQueueTest.cpp)
target_include_directories(chunk_rebuild_queue_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(chunk_rebuild_queue_test glm::glm)

add_executable(block_update_queue_order_test
    src/World/Physics/BlockUpdateQueue.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/BlockUpdateQueueOrderTest.cpp)
target_include_directories(block_update_queue_order_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(block_update_queue_order_test glm::glm)

add_executable(voxel_dda_traversal_test
    src/World/Collision/VoxelDdaTraversal.cpp
    src/Test/VoxelDdaTraversalTest.cpp)
target_include_directories(voxel_dda_traversal_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(voxel_dda_traversal_test glm::glm)

add_executable(physics_profile_parse_test
    src/Test/PhysicsProfileParseTest.cpp)
target_include_directories(physics_profile_parse_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(physics_profile_parse_test glm::glm)

add_executable(inventory_icon_fingerprint_test
    src/App/Platform/IUPlatformPaths.cpp
    src/ThirdParty/stb_image.cpp
    src/ThirdParty/stb_image_write.cpp
    src/Gui/Cache/InventoryIconService.cpp
    src/Test/InventoryIconFingerprintTest.cpp)
target_include_directories(inventory_icon_fingerprint_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(inventory_icon_fingerprint_test
    OpenGL::GL
    GLEW::GLEW
    nlohmann_json::nlohmann_json
    glm::glm)

set(PHYSICS_REPLAY_TEST_SOURCES
    src/World/Physics/BlockUpdateQueue.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/ChunkRebuildQueue.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    src/World/Physics/Replay/ReplayInputLog.cpp
    src/World/Physics/Replay/ReplayWorldFixture.cpp
    src/World/Physics/Replay/WorldStateHasher.cpp
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES})

add_executable(deterministic_replay_test
    ${PHYSICS_REPLAY_TEST_SOURCES}
    src/Test/DeterministicReplayTest.cpp)
target_include_directories(deterministic_replay_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(deterministic_replay_test glm::glm)

add_executable(physics_budget_guard_test
    ${PHYSICS_REPLAY_TEST_SOURCES}
    src/Test/PhysicsBudgetGuardTest.cpp)
target_include_directories(physics_budget_guard_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(physics_budget_guard_test glm::glm)

add_executable(liquid_queue_backpressure_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/World/Physics/FluidUpdateSet.cpp
    src/Test/LiquidQueueBackpressureTest.cpp)
target_include_directories(liquid_queue_backpressure_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(liquid_queue_backpressure_test glm::glm)

add_executable(collision_readiness_gate_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/World/Physics/CollisionReadiness.cpp
    src/Test/CollisionReadinessGateTest.cpp)
target_include_directories(collision_readiness_gate_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(collision_readiness_gate_test glm::glm)

add_executable(movement_chunk_boundary_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/MovementChunkBoundaryTest.cpp)
target_include_directories(movement_chunk_boundary_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(movement_chunk_boundary_test glm::glm)

add_executable(falling_blocks_stability_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/World/Physics/FallingBlockRules.cpp
    src/World/Physics/Replay/WorldStateHasher.cpp
    src/Test/FallingBlocksStabilityTest.cpp)
target_include_directories(falling_blocks_stability_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(falling_blocks_stability_test glm::glm nlohmann_json::nlohmann_json)

set(PHYSICS_REGISTRY_TEST_SOURCES
    src/Blocks/BlockRegistry.cpp
    src/Render/Textures/TextureBase.cpp
    src/Render/Textures/TextureCube.cpp)

add_executable(fluid_state_pack_test
    src/Test/FluidStatePackTest.cpp
    src/World/Math/FluidCellState.cpp)
target_include_directories(fluid_state_pack_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_state_pack_test glm::glm)

add_executable(chunk_light_pack_test
    src/Test/ChunkLightPackTest.cpp)
target_include_directories(chunk_light_pack_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(chunk_light_pack_test glm::glm)

add_executable(block_light_emission_parse_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Blocks/BlockDefinition.cpp
    src/Blocks/BlockDefinitionStorage.cpp
    src/Test/BlockLightEmissionParseTest.cpp)
target_include_directories(block_light_emission_parse_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(block_light_emission_parse_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(block_light_emission_parse_test glm::glm nlohmann_json::nlohmann_json)

add_executable(chunk_lighting_propagation_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Core/BlockWorld.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Lighting/LightUtil.cpp
    src/World/Lighting/ChunkLighting.cpp
    src/World/Math/FluidCellState.cpp
    src/World/Math/GridMath.cpp
    src/Test/ChunkLightingPropagationTest.cpp)
target_include_directories(chunk_lighting_propagation_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(chunk_lighting_propagation_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(chunk_lighting_propagation_test glm::glm nlohmann_json::nlohmann_json)

add_executable(chunk_bulk_blocklight_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Core/BlockWorld.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Lighting/LightUtil.cpp
    src/World/Lighting/ChunkLighting.cpp
    src/World/Math/FluidCellState.cpp
    src/World/Math/GridMath.cpp
    src/Test/ChunkBulkBlockLightTest.cpp)
target_include_directories(chunk_bulk_blocklight_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(chunk_bulk_blocklight_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(chunk_bulk_blocklight_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_chunk_io_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/IO/BinaryChunkSerializer.cpp
    src/Test/FluidChunkIoTest.cpp)
target_include_directories(fluid_chunk_io_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_chunk_io_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_chunk_io_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_spread_level_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/Test/FluidSpreadLevelTest.cpp)
target_include_directories(fluid_spread_level_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_spread_level_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_mesh_faces_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidMeshFacesTest.cpp)
target_include_directories(fluid_mesh_faces_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_mesh_faces_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_mesh_faces_test glm::glm nlohmann_json::nlohmann_json)

add_executable(underwater_fog_column_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/Test/UnderwaterFogColumnTest.cpp)
target_include_directories(underwater_fog_column_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(underwater_fog_column_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(underwater_fog_column_test glm::glm nlohmann_json::nlohmann_json)

add_executable(camera_stability_test src/Test/CameraStabilityTest.cpp)
target_include_directories(camera_stability_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(camera_stability_test glm::glm)

add_executable(docked_overlay_layout_test src/Test/DockedOverlayLayoutTest.cpp
    src/Gui/Layout/DockedOverlayLayout.cpp)
target_include_directories(docked_overlay_layout_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(docked_overlay_layout_test glm::glm)

add_executable(touch_input_bridge_lifecycle_test
    src/App/Platform/TouchInputBridge.cpp
    src/Test/TouchInputBridgeLifecycleTest.cpp)
target_include_directories(touch_input_bridge_lifecycle_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(touch_input_bridge_lifecycle_test glm::glm)

add_executable(fluid_surface_slice_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/Render/Mesh/FluidSurfaceColumnSlice.cpp
    src/Test/FluidSurfaceSliceTest.cpp)
target_include_directories(fluid_surface_slice_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_surface_slice_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_surface_slice_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_surface_map_logic_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/World/Core/RuntimeTuning.cpp
    src/Test/FluidSurfaceMapLogicTest.cpp)
target_include_directories(fluid_surface_map_logic_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_surface_map_logic_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_surface_map_logic_test glm::glm nlohmann_json::nlohmann_json)

add_executable(liquid_viscosity_gate_test
    ${PHYSICS_FLUID_SIM_SOURCES}
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/Test/LiquidViscosityGateTest.cpp)
target_include_directories(liquid_viscosity_gate_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(liquid_viscosity_gate_test glm::glm)

add_executable(liquid_non_renewable_flow_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/Test/LiquidNonRenewableFlowTest.cpp)
target_include_directories(liquid_non_renewable_flow_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(liquid_non_renewable_flow_test glm::glm nlohmann_json::nlohmann_json)

add_executable(liquid_flow_scenarios_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/Test/LiquidFlowScenariosTest.cpp)
target_include_directories(liquid_flow_scenarios_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(liquid_flow_scenarios_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_queue_integration_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/World/Physics/FluidUpdateSet.cpp
    src/Test/FluidQueueIntegrationTest.cpp)
target_include_directories(fluid_queue_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_queue_integration_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_stable_puddle_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/World/Physics/FluidUpdateSet.cpp
    src/Test/FluidStablePuddleTest.cpp)
target_include_directories(fluid_stable_puddle_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_stable_puddle_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_queue_drain_integration_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/World/Physics/FluidUpdateSet.cpp
    src/Test/FluidQueueDrainIntegrationTest.cpp)
target_include_directories(fluid_queue_drain_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_queue_drain_integration_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_sim_mesh_integration_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidSimMeshIntegrationTest.cpp)
target_include_directories(fluid_sim_mesh_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_sim_mesh_integration_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_sim_mesh_integration_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_placement_liquid_decor_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidPlacementLiquidDecorTest.cpp)
target_include_directories(fluid_placement_liquid_decor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_placement_liquid_decor_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_placement_liquid_decor_test glm::glm nlohmann_json::nlohmann_json)

add_executable(decor_mesh_integration_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Core/Jobs/JobThreadPool.cpp
    src/Core/Jobs/JobThreadBudget.cpp
    src/Render/Engine/DistanceFog.cpp
    src/Render/Engine/HorizonFogColor.cpp
    src/Render/Mesh/AsyncMeshBuilder.cpp
    src/Render/Mesh/ChunkDirtySet.cpp
    src/Render/Mesh/ChunkMeshCache.cpp
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/FluidSurfaceColumnSlice.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/DecorMeshIntegrationTest.cpp)
target_include_directories(decor_mesh_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(decor_mesh_integration_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(decor_mesh_integration_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_worldgen_seal_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/ResourcePacks/BlockNameUtil.cpp
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/Noise.cpp
    src/Test/FluidWorldGenSealTest.cpp)
target_include_directories(fluid_worldgen_seal_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_worldgen_seal_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_worldgen_seal_test glm::glm nlohmann_json::nlohmann_json)

add_executable(worldgen_vegetation_placement_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/ResourcePacks/BlockNameUtil.cpp
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/Noise.cpp
    src/Test/WorldGenVegetationPlacementTest.cpp)
target_include_directories(worldgen_vegetation_placement_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(worldgen_vegetation_placement_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(worldgen_vegetation_placement_test glm::glm nlohmann_json::nlohmann_json)

add_executable(worldgen_cave_depth_test
    src/WorldGen/Features/CaveDepthBand.cpp
    src/Test/WorldGenCaveDepthTest.cpp)
target_include_directories(worldgen_cave_depth_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worldgen_cave_depth_test glm::glm)

set(WORLDGEN_PIPELINE_TEST_SOURCES
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/ResourcePacks/BlockNameUtil.cpp
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/Noise.cpp)

add_executable(worldgen_fluid_vegetation_pipeline_test
    ${WORLDGEN_PIPELINE_TEST_SOURCES}
    src/Test/WorldGenFluidVegetationPipelineTest.cpp)
target_include_directories(worldgen_fluid_vegetation_pipeline_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(worldgen_fluid_vegetation_pipeline_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(worldgen_fluid_vegetation_pipeline_test glm::glm nlohmann_json::nlohmann_json)

add_executable(worldgen_hills_vegetation_gate_test
    src/World/Core/RuntimeTuning.cpp
    src/Test/WorldGenHillsVegetationGateTest.cpp)
target_include_directories(worldgen_hills_vegetation_gate_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(worldgen_hills_vegetation_gate_test glm::glm)

add_executable(chunk_generation_registry_test
    src/World/Chunks/ChunkGenerationToken.cpp
    src/Test/ChunkGenerationRegistryTest.cpp)
target_include_directories(chunk_generation_registry_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(chunk_generation_registry_test glm::glm)

add_executable(worldgen_scatter_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/WorldGenScatterTest.cpp)
target_include_directories(worldgen_scatter_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(worldgen_scatter_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(worldgen_scatter_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_tuning_defaults_test
    src/World/Core/RuntimeTuning.cpp
    src/Test/FluidTuningDefaultsTest.cpp)
target_include_directories(fluid_tuning_defaults_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_tuning_defaults_test glm::glm)

add_executable(fluid_block_resolver_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidBlockResolverTest.cpp)
target_include_directories(fluid_block_resolver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_block_resolver_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_block_resolver_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_flood_service_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidFloodServiceTest.cpp)
target_include_directories(fluid_flood_service_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_flood_service_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_flood_service_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_fill_policy_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidFillPolicyTest.cpp)
target_include_directories(fluid_fill_policy_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_fill_policy_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_fill_policy_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_kind_resolver_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidKindResolverTest.cpp)
target_include_directories(fluid_kind_resolver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_kind_resolver_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_kind_resolver_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_kind_preset_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/World/Physics/FluidBlockResolver.cpp
    src/Test/FluidKindPresetTest.cpp)
target_include_directories(fluid_kind_preset_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_kind_preset_test glm::glm nlohmann_json::nlohmann_json)

add_executable(object_placement_mode_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Objects/ObjectUtil.cpp
    src/Test/ObjectPlacementModeTest.cpp)
target_include_directories(object_placement_mode_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(object_placement_mode_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(object_placement_mode_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_permeable_decor_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidPermeableDecorTest.cpp)
target_include_directories(fluid_permeable_decor_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_permeable_decor_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_permeable_decor_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_permeable_block_flag_test
    src/Blocks/BlockDefinition.cpp
    src/Test/FluidPermeableBlockFlagTest.cpp)
target_include_directories(fluid_permeable_block_flag_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(fluid_permeable_block_flag_test
    nlohmann_json::nlohmann_json)

add_executable(fluid_gameplay_fixes_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/Test/FluidGameplayFixesTest.cpp)
target_include_directories(fluid_gameplay_fixes_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_gameplay_fixes_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_gameplay_fixes_test glm::glm nlohmann_json::nlohmann_json)

set(WORLDGEN_FLUID_CHUNK_TEST_SOURCES
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/Render/Textures/TextureBase.cpp
    src/ResourcePacks/ResourcePack.cpp
    src/ResourcePacks/BlockNameUtil.cpp
    src/ResourcePacks/TextureOverrides.cpp
    src/ThirdParty/stb_image.cpp
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Chunks/ChunkGenerationToken.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Objects/ObjectLibrary.cpp
    src/World/Objects/ObjectUtil.cpp
    src/World/Physics/FluidSpreadSystem.cpp
    src/World/Physics/FluidFloodService.cpp
    src/World/Physics/FluidFillPolicy.cpp
    src/World/Physics/FluidTransformSim.cpp
    src/World/Physics/FluidBlockResolver.cpp
    src/World/Physics/LiquidDebugTrace.cpp
    src/WorldGen/Core/Noise.cpp
    src/WorldGen/Core/ProceduralSettings.cpp
    src/WorldGen/Core/WorldSeedParser.cpp
    src/WorldGen/Core/WorldGeneratorRegistry.cpp
    src/WorldGen/Core/WorldGenSets.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenStageMask.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/BlockWorldColumnWriter.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/WorldGenPack.cpp
    src/WorldGen/Core/IUWorldGenPipeline.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Stages/DensityTerrainFill.cpp
    src/WorldGen/Sampling/OverworldHeightSampler.cpp
    src/WorldGen/Sampling/ClimateSampler.cpp
    src/WorldGen/Sampling/TerrainClimateMapper.cpp
    src/WorldGen/Sampling/DensityRouter.cpp
    src/WorldGen/Sampling/DensityFieldSampler.cpp
    src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
    src/WorldGen/Pipelines/WorldGenStageRunner.cpp
    src/WorldGen/Pipelines/ColumnGenerationService.cpp
    src/WorldGen/Sampling/BiomeSampler.cpp
    src/WorldGen/Sampling/BiomeRegistry.cpp
    src/WorldGen/Sampling/ColumnSample.cpp
    src/WorldGen/Features/CaveCarver.cpp
    src/WorldGen/Features/CaveDepthBand.cpp
    src/WorldGen/Features/RavineCarver.cpp
    src/WorldGen/Features/OreVeinPlacer.cpp
    src/WorldGen/Features/ObjectFeatureConfig.cpp
    src/WorldGen/Features/ObjectPlacementConstraints.cpp
    src/WorldGen/Features/ObjectFeaturePlacer.cpp
    src/WorldGen/Features/BuiltinWorldGenFeatures.cpp
    src/WorldGen/Core/PipelineChunkPopulator.cpp
    src/WorldGen/Core/ProceduralWorldGenFactory.cpp)

add_executable(fluid_worldgen_chunk_test
    ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES}
    src/Test/FluidWorldGenChunkTest.cpp)
target_include_directories(fluid_worldgen_chunk_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_worldgen_chunk_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_worldgen_chunk_test glm::glm nlohmann_json::nlohmann_json)

add_executable(physics_integration_test
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/Test/PhysicsIntegrationTest.cpp)
target_include_directories(physics_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(physics_integration_test glm::glm nlohmann_json::nlohmann_json)

add_executable(physics_profile_hash_test
    ${PHYSICS_REPLAY_TEST_SOURCES}
    src/Test/PhysicsProfileHashTest.cpp)
target_include_directories(physics_profile_hash_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(physics_profile_hash_test glm::glm)

set(PHYSICS_UWORLD_TEST_SOURCES
    ${PHYSICS_REGISTRY_TEST_SOURCES}
    ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
    src/World/Collision/WorldCollision.cpp
    src/World/Collision/VoxelDdaTraversal.cpp
    src/World/Physics/MaterialReactionRules.cpp
    src/World/Physics/MaterialReactionRulesRegistry.cpp
    src/World/Physics/CollisionReadiness.cpp
    src/World/Physics/PhysicsChunkDistance.cpp
    src/World/Physics/PhysicsQueuePurge.cpp
    src/World/Physics/PhysicsProfileFactory.cpp
    src/World/Physics/PrimitivePhysicsProfile.cpp
    src/World/Physics/StandardPhysicsProfile.cpp
    src/World/Physics/AdvancedPhysicsProfile.cpp
    src/World/Physics/WorldPhysicsScheduler.cpp
    src/World/Physics/WorldMovementPhysicsService.cpp
    src/World/Physics/WorldBlockPhysicsService.cpp
    src/World/Physics/WorldChunkDirtyService.cpp
    src/World/Physics/BlockUpdateQueue.cpp
    src/World/Physics/FluidUpdateSet.cpp
    src/World/Physics/ChunkRebuildQueue.cpp
    src/World/Physics/FallingBlockRules.cpp
    src/World/Physics/FallingBlockRulesRegistry.cpp
    src/World/Physics/FallingBlocksSystem.cpp
    ${PHYSICS_FLUID_SIM_SOURCES}
    src/World/Physics/ChunkPhysicsSeed.cpp
    src/Render/Primitives/Cube.cpp
    src/Creatures/Core/CreatureBounds.cpp
    src/World/Raycast/BlockRaycast.cpp
    src/World/Core/World.cpp
    src/World/Core/WorldViewBinding.cpp
    src/World/Environment/WorldEnvironment.cpp
    src/World/Mesh/WorldMeshService.cpp
    src/World/Streaming/WorldStreaming.cpp
    src/World/Streaming/ChunkEmergeCoordinator.cpp
    src/World/Persistence/WorldPersistence.cpp
    src/World/Core/WorldCooperativeOps.cpp
    src/World/Diagnostics/MovementDiagnosticsRecorder.cpp)

add_executable(collision_broadphase_subchunk_test
    ${PHYSICS_COLLISION_UNIT_SOURCES}
    src/Test/CollisionBroadphaseSubchunkTest.cpp)
target_include_directories(collision_broadphase_subchunk_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(collision_broadphase_subchunk_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(collision_broadphase_subchunk_test glm::glm nlohmann_json::nlohmann_json)

add_executable(fluid_placement_test
    ${PHYSICS_COLLISION_UNIT_SOURCES}
    src/Test/FluidPlacementTest.cpp)
target_include_directories(fluid_placement_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(fluid_placement_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(fluid_placement_test glm::glm nlohmann_json::nlohmann_json)

add_executable(block_placement_raycast_test
    ${PHYSICS_COLLISION_UNIT_SOURCES}
    src/Test/BlockPlacementRaycastTest.cpp)
target_include_directories(block_placement_raycast_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(block_placement_raycast_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(block_placement_raycast_test glm::glm nlohmann_json::nlohmann_json)

add_executable(material_reaction_rules_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Test/MaterialReactionRulesTest.cpp)
target_include_directories(material_reaction_rules_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(material_reaction_rules_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(material_reaction_rules_test glm::glm nlohmann_json::nlohmann_json)

add_executable(movement_integration_test
    ${PHYSICS_COLLISION_UNIT_SOURCES}
    src/Test/MovementIntegrationTest.cpp)
target_include_directories(movement_integration_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(movement_integration_test PRIVATE
    ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(movement_integration_test glm::glm nlohmann_json::nlohmann_json)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CUBATARIUM_GENERATED_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    GLEW::GLEW
    Freetype::Freetype
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "WIN32" GLEW_STATIC)
    target_link_libraries(${PROJECT_NAME} PRIVATE dbghelp)
endif()

if(UNIX AND NOT ANDROID)
    target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:CUBATARIUM_DEBUG>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CUBATARIUM_RUN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CUBATARIUM_RUN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CUBATARIUM_RUN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CUBATARIUM_RUN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL ${CUBATARIUM_RUN_DIR}
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSYNC_SRC="${CMAKE_SOURCE_DIR}/shaders"
        -DSYNC_DST="${CUBATARIUM_RUN_DIR}/shaders"
        -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
    COMMENT "Syncing shaders to runtime directory"
)

# Seed bin/config.json from template only on first run (never overwrite runtime config).
if(EXISTS "${CMAKE_SOURCE_DIR}/config.json.example")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_SRC="${CMAKE_SOURCE_DIR}/config.json.example"
            -DCONFIG_DST="${CUBATARIUM_RUN_DIR}/config.json"
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_config_if_missing.cmake"
        COMMENT "Seeding config.json next to executable if missing"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/prefabs")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/prefabs"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/prefabs"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing prefabs to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/models")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/models"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/models"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing models to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/textures")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/textures"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/textures"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing textures to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/objects")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/objects"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/objects"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing objects to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/content")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/content"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/content"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing content data to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/resource_packs")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/resource_packs"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/resource_packs"
            -DSYNC_EXCLUDE_PREFIX="_build_staging_"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing resource packs to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/fonts")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/fonts"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/fonts"
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing fonts to runtime directory"
    )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/resources/branding/icon-64.png")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/resources/branding/icon-64.png"
            "${CUBATARIUM_RUN_DIR}/icon.png"
        COMMENT "Copying window icon to runtime directory"
    )
endif()

if(WIN32)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND PowerShell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_SOURCE_DIR}/scripts/build/verify-windows-exe.ps1"
            -ExePath "$<TARGET_FILE:${PROJECT_NAME}>"
        COMMENT "Verifying static Windows executable (no vcpkg/VC++ DLL deps)"
        VERBATIM
    )
endif()
