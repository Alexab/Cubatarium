cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Static vcpkg triplet on Windows hosts. /MT runtime is applied after project()
# only for MSVC / clang-cl Р вЂћLL GNU-style Clang (linux-ninja preset) must not get /Zc flags.
if(WIN32)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet" FORCE)
    set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Copy DLL deps next to exe" FORCE)
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "vcpkg manifest mode" FORCE)
endif()

project(Cubatarium VERSION 0.0.1.1 LANGUAGES CXX)

enable_testing()

# Helper for the hand-maintained unit-test executables: one call replaces the
# add_executable + include/definitions/libraries boilerplate each test used to
# repeat. Sources stay per-test on purpose (tests link minimal source subsets
# with stubs instead of a monolithic library).
function(cubatarium_add_test)
    cmake_parse_arguments(CAT "" "NAME"
        "SOURCES;DEFINITIONS;LIBRARIES;WIN32_LIBRARIES" ${ARGN})
    if(NOT CAT_NAME)
        message(FATAL_ERROR "cubatarium_add_test requires NAME")
    endif()
    add_executable(${CAT_NAME} ${CAT_SOURCES})
    if("src/Creatures/Core/CreatureInventory.cpp" IN_LIST CAT_SOURCES)
        target_sources(${CAT_NAME} PRIVATE
            src/Audio/AudioApi.cpp src/Audio/AudioEventQueue.cpp)
    endif()
    target_include_directories(${CAT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(CAT_DEFINITIONS)
        target_compile_definitions(${CAT_NAME} PRIVATE ${CAT_DEFINITIONS})
    endif()
    if(CAT_LIBRARIES)
        target_link_libraries(${CAT_NAME} ${CAT_LIBRARIES})
    endif()
    if(CAT_WIN32_LIBRARIES AND WIN32)
        target_link_libraries(${CAT_NAME} ${CAT_WIN32_LIBRARIES})
    endif()
endfunction()



# MSVC desktop static CRT (/MT). Skip for GNU-style Clang on Windows (e.g. linux-ninja preset).
set(CUBATARIUM_MSVC_DESKTOP FALSE)
if(WIN32)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        set(CUBATARIUM_MSVC_DESKTOP TRUE)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
           AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        set(CUBATARIUM_MSVC_DESKTOP TRUE)
    endif()
endif()
if(CUBATARIUM_MSVC_DESKTOP)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library (/MT)" FORCE)
    include(${CMAKE_SOURCE_DIR}/cmake/AssertWindowsStatic.cmake)
elseif(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
    # clang++ on Windows with x64-windows-static vcpkg deps must use /MT too.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "MSVC runtime library (/MT for static vcpkg)" FORCE)
endif()

option(CUBATARIUM_ENABLE_TSAN "Build with ThreadSanitizer (Linux Clang/GCC)" OFF)
if(CUBATARIUM_ENABLE_TSAN)
    if((CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU") AND NOT WIN32)
        add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    else()
        message(WARNING "CUBATARIUM_ENABLE_TSAN is only supported on Linux with Clang/GCC")
    endif()
endif()

option(CUBATARIUM_ENABLE_TRACY "Embed Tracy profiler client (zones + FrameMark)" OFF)
if(CUBATARIUM_ENABLE_TRACY)
    include(FetchContent)
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_CALLSTACK ON CACHE BOOL "" FORCE)
    # Avoid Tracy GUI / capture tools in the game target; client only.
    set(TRACY_STATIC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.11.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tracy)
    message(STATUS "CUBATARIUM_ENABLE_TRACY=ON — TracyClient linked into Cubatarium")
endif()

# Version.h: generated at configure time (so it always exists) and refreshed on
# every build by the cubatarium_version target (see cmake/GenerateVersionHeader.cmake).
set(CUBATARIUM_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${CUBATARIUM_GENERATED_DIR}")
set(_cubatarium_version_args
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DTEMPLATE=${CMAKE_SOURCE_DIR}/src/Version.h.in
    -DOUTPUT=${CUBATARIUM_GENERATED_DIR}/Version.h
    -DFALLBACK_VERSION=${PROJECT_VERSION}
)
execute_process(COMMAND ${CMAKE_COMMAND} ${_cubatarium_version_args}
    -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersionHeader.cmake)
add_custom_target(cubatarium_version ALL
    COMMAND ${CMAKE_COMMAND} ${_cubatarium_version_args}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateVersionHeader.cmake
    BYPRODUCTS ${CUBATARIUM_GENERATED_DIR}/Version.h
    COMMENT "Refreshing Cubatarium version from git"
    VERBATIM
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
    if(CUBATARIUM_MSVC_DESKTOP)
        include(cmake/CubatariumGlog.cmake)
    else()
        find_package(glog CONFIG REQUIRED)
    endif()
    # vcpkg glog exports /Zc:__cplusplus for MSVC ABI; clang++ (GNU driver) rejects it.
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU")
        set_target_properties(glog::glog PROPERTIES INTERFACE_COMPILE_OPTIONS "")
    endif()
endif()

# Vendored stb headers (stb_image v2.30, stb_image_write v1.16) are tracked in
# git. Do not silently download unpinned code from master at configure time;
# fail loudly instead so builds stay reproducible.
set(STB_IMAGE_HEADER "${CMAKE_SOURCE_DIR}/src/ThirdParty/stb_image.h")
set(STB_IMAGE_WRITE_HEADER "${CMAKE_SOURCE_DIR}/src/ThirdParty/stb_image_write.h")
foreach(_stb_header IN ITEMS "${STB_IMAGE_HEADER}" "${STB_IMAGE_WRITE_HEADER}")
    if(NOT EXISTS "${_stb_header}")
        message(FATAL_ERROR
            "Vendored stb header missing: ${_stb_header}\n"
            "stb headers are tracked in git (src/ThirdParty/). "
            "Restore it with: git checkout -- ${_stb_header}")
    endif()
endforeach()

set(SOURCES
    src/ThirdParty/stb_image.cpp
    src/ThirdParty/stb_image_write.cpp
    src/ThirdParty/miniaudio.cpp
    src/main.cpp
    src/Audio/AudioApi.cpp
    src/Audio/AudioDirector.cpp
    src/Audio/AudioEngine.cpp
    src/Audio/AudioEventQueue.cpp
    src/Audio/MaterialSoundMap.cpp
    src/Audio/MusicManifest.cpp
    src/Audio/SoundBank.cpp
    src/Audio/SoundManifest.cpp
    src/App/Utils.cpp
    src/App/Settings/AudioSettings.cpp
    src/App/CreateWorldCli.cpp
    src/Render/Camera/Camera.cpp
    src/Render/Camera/CameraLens.cpp
    src/Render/Camera/Control/FpsGameplayViewController.cpp
    src/Render/Camera/Control/IsoGameplayViewController.cpp
    src/Render/Camera/Control/GameplayViewControllerSelect.cpp
    src/Creatures/Core/CreatureBounds.cpp
    src/Creatures/Core/CreatureCatalogTypes.cpp
    src/Creatures/Locomotion/CreatureLocomotionController.cpp
    src/Creatures/Locomotion/CreatureMotor.cpp
    src/Creatures/Locomotion/CreatureLocomotionFacts.cpp
    src/Creatures/Core/Creature.cpp
    src/Creatures/Core/UCreatureFactory.cpp
    src/Creatures/Spawning/UCreatureSpawnPlanner.cpp
    src/Creatures/Core/CreatureInventory.cpp
    src/Creatures/Stats/CreatureStatsDefaults.cpp
    src/Creatures/Stats/CreatureStatsJson.cpp
    src/Creatures/Stats/CreatureVitalsSystem.cpp
    src/Creatures/Combat/CreatureCombat.cpp
    src/Items/ItemDefinitionStorage.cpp
    src/Items/ItemUseRegistry.cpp
    src/Items/ToolCapabilities.cpp
    src/Items/ItemWieldRenderer.cpp
    src/Items/FpViewmodelRenderer.cpp
    src/Items/ItemGltfTextureCache.cpp
    src/Items/ItemRenderAsset.cpp
    src/Items/ItemLabDiagnostics.cpp
    src/Items/ItemVisualState.cpp
    src/Items/ItemSurfaceRenderer.cpp
    src/Game/Survival/SurvivalSystems.cpp
    src/World/Items/WorldItemSystem.cpp
    src/Items/ItemToolInfluenceProvider.cpp
    src/Items/ItemVisualPresetLibrary.cpp
    src/Creatures/Influence/InfluenceHitMath.cpp
    src/Creatures/Influence/BareHandToolInfluenceProvider.cpp
    src/Creatures/Influence/InfluenceEvent.cpp
    src/Creatures/Influence/InfluenceResolver.cpp
    src/Creatures/Influence/InfluenceApplier.cpp
    src/Creatures/Influence/StatusEffectCatalog.cpp
    src/Creatures/Influence/StatusEffectSystem.cpp
    src/Render/Effects/InfluenceFxSystem.cpp
    src/Render/Effects/InfluenceFxPass.cpp
    src/Creatures/Player/Player.cpp
    src/Activity/CreatureActivityDirector.cpp
    src/Activity/WorldCreatureActivitySink.cpp
    src/Activity/CreatureActivityRegistry.cpp
    src/Activity/Agents/WanderActivityAgent.cpp
    src/Activity/Helpers/CreatureActivitySteering.cpp
    src/Activity/Helpers/CreatureActivityNavigation.cpp
    src/Activity/Agents/FleeActivityAgent.cpp
    src/Activity/Agents/MeleeAttackActivityAgent.cpp
    src/Activity/Agents/BotPlayerActivityAgent.cpp
    src/Activity/Brain/SimpleFsmBrain.cpp
    src/Activity/Helpers/CreatureHostility.cpp
    src/Navigation/WorldNavigationQueries.cpp
    src/Navigation/UNavigationPathfinder.cpp
    src/Navigation/UWaypointFollower.cpp
    src/Navigation/NavigationPathBudget.cpp
    src/World/Environment/CreatureSpatialIndex.cpp
    src/Creatures/Core/WorldCreatures.cpp
    src/Creatures/Environment/CreatureEnvironment.cpp
    src/Creatures/Environment/CreatureTraverseQueries.cpp
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
    src/Creatures/Visual/WornEquipmentDrawer.cpp
    src/Creatures/Visual/CreatureVisualGltf.cpp
    src/Creatures/Visual/CreatureVisualSprite.cpp
    src/Creatures/Visual/CreatureVisualBackendSelect.cpp
    src/Creatures/Visual/CreatureVisualBase.cpp
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
    src/App/Settings/GraphicsQualityProfile.cpp
    src/App/Settings/UiSettings.cpp
    src/Blocks/Input/BlockInputController.cpp
    src/Blocks/Input/PlayerInteractionRouter.cpp
    src/Render/Primitives/Cube.cpp
    src/Render/Primitives/CubeGL.cpp
    src/World/Objects/ObjectLibrary.cpp
    src/Render/Textures/TextureBase.cpp
    src/Render/Textures/TextureCube.cpp
    src/Creatures/Player/User.cpp
    src/Render/Engine/ViewEngine.cpp
    src/World/Core/World.cpp
    src/World/Core/WorldOwnedStorage.cpp
    src/World/Core/WorldMemberCallbackStorage.cpp
    src/Core/Memory/RootStorageSource.cpp
    src/World/Core/MarkRelitInstall.cpp
    src/World/Adapters/WorldRenderReadAdapter.cpp
    src/World/Core/WorldViewBinding.cpp
    src/World/Diagnostics/CreatureTickTrace.cpp
    src/World/Core/RuntimeTuning.cpp
    src/World/Core/WorldFluidFacade.cpp
    src/World/Core/FluidColumnSurfaceQuery.cpp
    src/World/Collision/WorldCollision.cpp
    src/World/Collision/VoxelDdaTraversal.cpp
    src/World/Core/WorldCooperativeOps.cpp
    src/World/Core/WorldLoadDiagnostics.cpp
    src/World/Environment/WorldEnvironment.cpp
    src/World/Environment/EnvironmentConfig.cpp
    src/World/View/WorldViewSettings.cpp
    src/World/View/ViewRayMath.cpp
    src/World/Environment/WeatherBiomeUtil.cpp
    src/World/Environment/WeatherAutoController.cpp
    src/World/Environment/WeatherField.cpp
    src/World/Lighting/LightUtil.cpp
    src/World/Lighting/ChunkLighting.cpp
    src/World/Lighting/ChunkRelightSnapshot.cpp
    src/World/Lighting/AsyncRelightBuilder.cpp
    src/World/Lighting/FullLightingPipeline.cpp
    src/World/Lighting/FlatLightingPipeline.cpp
    src/World/Lighting/LightingPipelineFactory.cpp
    src/World/Lighting/CpuLightingSeedBackend.cpp
    src/World/Lighting/GpuLightingSeedBackend.cpp
    src/World/Lighting/LightingSeedBackendFactory.cpp
    src/World/Persistence/WorldPersistence.cpp
    src/World/Streaming/WorldStreaming.cpp
    src/World/Streaming/MemoryBudgetController.cpp
    src/World/Streaming/ChunkEmergeCoordinator.cpp
    src/World/Streaming/ColumnFlowScheduler.cpp
    src/World/Streaming/ColumnFlowExecutor.cpp
    src/World/Streaming/ColumnRecordCoordinator.cpp
    src/World/Streaming/EnterVisualGate.cpp
    src/World/Physics/WorldPhysicsScheduler.cpp
    src/World/Physics/WorldMovementPhysicsService.cpp
    src/World/Physics/WorldBlockPhysicsService.cpp
    src/World/Interaction/BlockBreakService.cpp
    src/World/Interaction/BlockPlacementService.cpp
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
    src/World/Diagnostics/FramePerfMonitor.cpp
    src/World/Diagnostics/JobStageTrace.cpp
    src/World/Streaming/ChunkRenderDemand.cpp
    src/World/Diagnostics/EnterLitDiagnostics.cpp
    src/World/Diagnostics/BlockInspectDiagnostics.cpp
    src/World/Mesh/Diagnostics/MeshAuditDiagnostics.cpp
    src/World/Diagnostics/CreatureMovementDiagnostics.cpp
    src/World/Mesh/WorldMeshService.cpp
    src/World/Mesh/WorldMeshDirtyPolicy.cpp
    src/World/Mesh/EditMeshRemeshPolicy.cpp
    src/App/Platform/WindowManager.cpp
    src/App/Platform/SampleProfiler.cpp
    src/Render/Engine/GeometryEngine.cpp
    src/Render/Engine/FluidSurfaceMap.cpp
    src/Render/Engine/UnderwaterFogPass.cpp
    src/Render/Engine/SkyGradientPass.cpp
    src/Render/Engine/CreatureDrawPass.cpp
    src/Creatures/Diagnostics/CreaturePassTimingProbe.cpp
    src/Render/Engine/RenderMeshSink.cpp
    src/Render/Engine/GreedyGpuBackend.cpp
    src/Render/Engine/GreedyGpuPublication.cpp
    src/Render/Engine/GreedyVertexPool.cpp
    src/Render/Engine/GreedyVertexPoolLifetime.cpp
    src/Render/Lighting/SectionLightAtlas.cpp
    src/Render/Lighting/SectionLightBindingTable.cpp
    src/Render/Lighting/SectionRenderLightBindings.cpp
    src/Render/Lighting/SectionLightDrawTable.cpp
    src/Render/Engine/CrossGpuBackend.cpp
    src/Render/Backend/RenderBackendFactory.cpp
    src/Render/Backend/RenderBackendSelect.cpp
    src/Render/Backend/RenderBackendCaps.cpp
    src/Render/Backend/RenderBackendCapsProbe.cpp
    src/Render/Backend/AndroidGpuPolicy.cpp
    src/Render/Backend/GpuHotPathFallback.cpp
    src/Render/Gl/ComputeProgram.cpp
    src/Render/Mesh/CpuFrustumCull.cpp
    src/Render/Mesh/GpuFrustumCull.cpp
    src/Render/Mesh/GpuGreedyMesher.cpp
    src/Render/Mesh/AndroidGpuGreedyMesher.cpp
    src/Render/Mesh/GpuGreedyOpaqueEmit.cpp
    src/Render/Mesh/GpuMeshPipeline.cpp
    src/Render/Mesh/GpuMeshSlotAllocator.cpp
    src/Render/Pipeline/GpuTransparentSort.cpp
    src/Render/Mesh/GpuFluidColumnScan.cpp
    src/World/Lighting/GpuSkylightColumnSeed.cpp
    src/World/Lighting/GpuBlocklightFlood.cpp
    src/World/Lighting/GpuLightTransport.cpp
    src/Render/Engine/MdiVertexPoolStore.cpp
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
    src/Render/Blocks/BlockCrackOverlayPass.cpp
    src/Render/Blocks/BlockBreakParticleSystem.cpp
    src/Render/Blocks/BlockBreakFxPass.cpp
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
    src/WorldGen/Core/WorldGenContentPin.cpp
    src/WorldGen/Core/WorldGenContentPinTls.cpp
    src/WorldGen/Core/IUWorldGenPipeline.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Stages/MudflowErosion.cpp
    src/WorldGen/Stages/DensityTerrainFill.cpp
    src/WorldGen/Sampling/OverworldHeightSampler.cpp
    src/WorldGen/Sampling/ClimateSampler.cpp
    src/WorldGen/Sampling/TerrainClimateMapper.cpp
    src/WorldGen/Sampling/DensityRouter.cpp
    src/WorldGen/Sampling/DensityFieldSampler.cpp
    src/WorldGen/Sampling/AquiferSampler.cpp
    src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
    src/WorldGen/Pipelines/WorldGenStageRunner.cpp
    src/WorldGen/Pipelines/ColumnGenerationService.cpp
    src/WorldGen/Sampling/BiomeSampler.cpp
    src/WorldGen/Sampling/BiomeRegistry.cpp
    src/WorldGen/Sampling/ColumnSample.cpp
    src/WorldGen/Sampling/CoarseHeightCache.cpp
    src/WorldGen/Features/CaveCarver.cpp
    src/WorldGen/Features/CaveDepthBand.cpp
    src/WorldGen/Features/RavineCarver.cpp
    src/WorldGen/Features/ValleyCarver.cpp
    src/WorldGen/Features/LakeCarver.cpp
    src/WorldGen/Features/OreVeinPlacer.cpp
    src/WorldGen/Features/ObjectFeatureConfig.cpp
    src/WorldGen/Features/ObjectPlacementConstraints.cpp
    src/WorldGen/Features/ObjectFeaturePlacer.cpp
    src/WorldGen/Features/BuiltinWorldGenFeatures.cpp
    src/WorldGen/Core/PipelineChunkPopulator.cpp
    src/WorldGen/Core/ProceduralWorldGenFactory.cpp
    src/World/Raycast/BlockRaycast.cpp
    src/World/Raycast/OpaqueVoxelRayTrace.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkManager.cpp
    src/Render/Mesh/ChunkDirtySet.cpp
    src/Render/Mesh/ChunkMeshCache.cpp
    src/Render/Mesh/ChunkMeshRevisionRegistry.cpp
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
    src/Core/Jobs/PipelineAdmission.cpp
    src/Core/Progress/LatestProgressSink.cpp
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/MeshCaptureStore.cpp
    src/Render/Mesh/MeshCaptureWorker.cpp
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
    src/Game/Economy/ResourceEconomy.cpp
    src/Game/Crafting/RecipeRegistry.cpp
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
    src/Gui/Preview/ItemPreviewRenderer.cpp
    src/Gui/Preview/ItemLabSceneRenderer.cpp
    src/Gui/Preview/ItemLabOverlayRenderer.cpp
    src/Gui/Screens/ItemLabScreen.cpp
    src/Gui/Preview/CreaturePreviewRenderer.cpp
    src/Gui/Preview/CreaturePreviewLayout.cpp
    src/Gui/Preview/ObjectPreviewLayout.cpp
    src/Gui/Preview/ContentPreviewDock.cpp
    src/Gui/Layout/DockedOverlayLayout.cpp
    src/Gui/Cache/InventoryIconService.cpp
    src/Gui/Cache/ObjectIconCache.cpp
    src/Gui/Cache/ItemIconCache.cpp
    src/Gui/Cache/CreatureIconCache.cpp
    src/Gui/Core/GuiInputRouter.cpp
    src/Gui/Core/GuiFocus.cpp
    src/Gui/Core/GuiContext.cpp
    src/Gui/Core/GuiScreenBase.cpp
    src/Gui/Layout/GuiLayout.cpp
    src/Gui/Layout/GuiTooltipLayout.cpp
    src/Gui/Core/GuiScrollbarController.cpp
    src/Gui/Widgets/GuiWidget.cpp
    src/Gui/Widgets/GuiPanel.cpp
    src/Gui/Widgets/GuiButton.cpp
    src/Gui/Widgets/GuiLabel.cpp
    src/Gui/Widgets/GuiProgressBar.cpp
    src/Gui/Widgets/GuiSlider.cpp
    src/Gui/Widgets/GuiSliderField.cpp
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
    src/Gui/Widgets/WorldViewSettingsForm.cpp
    src/Gui/Widgets/ResourcePackPickerForm.cpp
    src/Gui/Screens/MainMenuScreen.cpp
    src/Gui/Screens/SettingsScreen.cpp
    src/Gui/Screens/NewWorldScreen.cpp
    src/Gui/Screens/LoadWorldScreen.cpp
    src/Gui/Screens/WorldResourcePacksScreen.cpp
    src/Gui/Screens/InGameHudScreen.cpp
    src/Gui/Screens/ConsoleScreen.cpp
    src/Gui/Screens/PlayerWindowScreen.cpp
    src/Gui/Screens/CharacterSheetScreen.cpp
    src/Gui/Screens/CreativePaletteScreen.cpp
    src/Gui/Screens/SurvivalInventoryScreen.cpp
    src/Gui/Screens/DeathScreen.cpp
    src/Gui/Screens/CraftingScreen.cpp
    src/Gui/Screens/AnvilScreen.cpp
    src/Gui/Screens/WorldGenPaletteScreen.cpp
    src/Gui/Screens/WorldProgressScreen.cpp
    src/App/Platform/AppRunner.cpp
    src/App/Platform/StreamingSchedulerReinitProbe.cpp
    src/App/Diagnostics/DiagnosticFlightController.cpp
    src/App/Platform/DesktopPlatformPaths.cpp
    src/App/Platform/GameDataRoot.cpp
    src/App/Platform/DesktopPlatformWindow.cpp
    src/App/Platform/IUPlatformPaths.cpp
    src/App/Platform/Log.cpp
    src/App/Platform/WorkerJobKind.cpp
)

# AppRunner includes the opt-in native diagnostics. Its Release object may
# exceed the standard COFF section count as those typed checks grow.
if(CUBATARIUM_MSVC_DESKTOP)
  set_property(SOURCE src/App/Platform/AppRunner.cpp APPEND PROPERTY
      COMPILE_OPTIONS "$<$<CONFIG:Release>:/bigobj>")
endif()

set(HEADERS
    src/Render/Camera/Camera.h
    src/Render/Camera/CameraLens.h
    src/Render/Camera/ProjectionMode.h
    src/Render/Camera/Control/IUGameplayViewController.h
    src/Render/Camera/Control/FpsGameplayViewController.h
    src/Render/Camera/Control/IsoGameplayViewController.h
    src/Render/Camera/IsoViewPreset.h
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
    src/World/View/WorldViewSettings.h
    src/World/View/ViewRayMath.h
    src/World/Environment/WeatherBiomeUtil.h
    src/World/Environment/WeatherAutoController.h
    src/World/Lighting/LightUtil.h
    src/World/Lighting/ChunkLighting.h
    src/World/Lighting/ChunkRelightSnapshot.h
    src/World/Lighting/LightCalculationSettlement.h
    src/World/Lighting/AsyncRelightBuilder.h
    src/World/Lighting/IULightingPipeline.h
    src/World/Lighting/FullLightingPipeline.h
    src/World/Lighting/FlatLightingPipeline.h
    src/World/Lighting/LightingPipelineFactory.h
    src/App/Settings/GraphicsQualityProfile.h
    src/App/Settings/AudioSettings.h
    src/Audio/AudioApi.h
    src/Audio/AudioDirector.h
    src/Audio/AudioEngine.h
    src/Audio/AudioEvent.h
    src/Audio/AudioEventQueue.h
    src/Audio/MaterialSoundMap.h
    src/Audio/MusicManifest.h
    src/Audio/SoundBank.h
    src/Audio/SoundManifest.h
    src/World/Persistence/WorldPersistence.h
    src/World/Streaming/WorldStreaming.h
    src/World/Streaming/ChunkEmergeCoordinator.h
    src/World/Interfaces/IUWorldMeshSink.h
    src/World/Interfaces/IUWorldRenderReadModel.h
    src/World/Adapters/WorldRenderReadAdapter.h
    src/World/Mesh/WorldMeshService.h
    src/App/Platform/WindowManager.h
    src/Blocks/Input/BlockInputController.h
    src/Blocks/Input/PlayerInteractionRouter.h
    src/Render/Engine/GeometryEngine.h
    src/Render/Blocks/BlockCrackOverlayPass.h
    src/Render/Blocks/BlockBreakParticleSystem.h
    src/Render/Blocks/BlockBreakFxPass.h
    src/Render/Engine/UnderwaterFogPass.h
    src/Render/Engine/GreedyGpuBackend.h
    src/Render/Engine/CrossGpuBackend.h
    src/World/View/DistanceFog.h
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
    src/Gui/Widgets/GuiSliderField.h
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
    src/Gui/Widgets/WorldViewSettingsForm.h
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
    src/Gui/Screens/SurvivalInventoryScreen.h
    src/Gui/Screens/DeathScreen.h
    src/Gui/Screens/CraftingScreen.h
    src/Gui/Screens/AnvilScreen.h
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
    FetchContent_Declare(
        glog
        GIT_REPOSITORY https://github.com/google/glog.git
        GIT_TAG v0.6.0
    )
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(WITH_GFLAGS OFF CACHE BOOL "" FORCE)
    set(WITH_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(glm json freetype glog)

    add_library(cubatarium SHARED
        ${CUBATARIUM_COMMON_SOURCES}
        ${CUBATARIUM_ANDROID_SOURCES}
    )
    add_dependencies(cubatarium cubatarium_version)

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
        glog::glog
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

# Visual Studio/MSBuild tracks incremental state per project. Keeping hundreds
# of translation units in one project makes an interrupted build discard the
# results of every unfinished compiler batch. Use small OBJECT libraries so
# each subsystem has its own project, object directory and MSBuild tlog files.
# Other generators retain the original single-target build (Ninja already
# schedules and tracks translation units independently).
if(MSVC AND CMAKE_GENERATOR MATCHES "^Visual Studio")
    add_library(CubatariumCompileConfig INTERFACE)
    target_include_directories(CubatariumCompileConfig INTERFACE
        ${CMAKE_SOURCE_DIR}/src
        ${CUBATARIUM_GENERATED_DIR}
    )
    target_link_libraries(CubatariumCompileConfig INTERFACE
        OpenGL::GL
        glfw
        glm::glm
        GLEW::GLEW
        Freetype::Freetype
        nlohmann_json::nlohmann_json
        glog::glog
    )
    target_compile_options(CubatariumCompileConfig INTERFACE /MP)
    target_compile_definitions(CubatariumCompileConfig INTERFACE
        $<$<CONFIG:Debug>:CUBATARIUM_DEBUG>
    )
    if(CUBATARIUM_MSVC_DESKTOP)
        target_compile_definitions(CubatariumCompileConfig INTERFACE CUBATARIUM_GLOG_RESOURCE_SOURCE=1)
    endif()
    if(WIN32)
        target_compile_definitions(CubatariumCompileConfig INTERFACE WIN32 GLEW_STATIC)
    endif()
    if(CUBATARIUM_ENABLE_TRACY)
        target_link_libraries(CubatariumCompileConfig INTERFACE TracyClient)
        target_compile_definitions(CubatariumCompileConfig INTERFACE CUBATARIUM_ENABLE_TRACY)
    endif()

    foreach(_source IN LISTS SOURCES)
        if(_source MATCHES "^src/([^/]+/[^/]+)/")
            set(_group "${CMAKE_MATCH_1}")
        else()
            get_filename_component(_group "${_source}" DIRECTORY)
        endif()
        string(TOLOWER "${_group}" _group)
        string(REGEX REPLACE "[^a-z0-9_]" "_" _group "${_group}")
        list(APPEND CUBATARIUM_OBJECT_SOURCES_${_group} "${_source}")
        list(APPEND CUBATARIUM_OBJECT_GROUPS "${_group}")
    endforeach()
    list(REMOVE_DUPLICATES CUBATARIUM_OBJECT_GROUPS)

    set(CUBATARIUM_DESKTOP_SOURCES ${HEADERS})
    foreach(_group IN LISTS CUBATARIUM_OBJECT_GROUPS)
        set(_object_target "CubatariumObjects_${_group}")
        add_library(${_object_target} OBJECT ${CUBATARIUM_OBJECT_SOURCES_${_group}})
        # The executable's dependency only orders its own sources/link. These
        # object libraries also include Version.h and must see the refreshed
        # header before their compiler dependency scan, including MSBuild's.
        add_dependencies(${_object_target} cubatarium_version)
        target_link_libraries(${_object_target} PRIVATE CubatariumCompileConfig)
        set_property(TARGET ${_object_target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        list(APPEND CUBATARIUM_DESKTOP_SOURCES $<TARGET_OBJECTS:${_object_target}>)
    endforeach()
    if(WIN32 AND EXISTS "${CUBATARIUM_WIN_RC}")
        list(APPEND CUBATARIUM_DESKTOP_SOURCES "${CUBATARIUM_WIN_RC}")
    endif()
endif()

add_executable(${PROJECT_NAME} ${CUBATARIUM_DESKTOP_SOURCES})
add_dependencies(${PROJECT_NAME} cubatarium_version)
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /MP)
endif()

cubatarium_add_test(NAME position_assignment_audit_test
    SOURCES src/Test/PositionAssignmentAuditTest.cpp
    LIBRARIES glm::glm)
add_test(NAME position_assignment_audit_test COMMAND $<TARGET_FILE:position_assignment_audit_test>)
set_tests_properties(position_assignment_audit_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME world_vertical_profile_test
    SOURCES src/Test/WorldVerticalProfileTest.cpp src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)
add_test(NAME world_vertical_profile_test COMMAND $<TARGET_FILE:world_vertical_profile_test>)
set_tests_properties(world_vertical_profile_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME mesh_reconcile_snapshot_test
    SOURCES src/Test/MeshReconcileSnapshotTest.cpp
    LIBRARIES glm::glm)
add_test(NAME mesh_reconcile_snapshot_test COMMAND $<TARGET_FILE:mesh_reconcile_snapshot_test>)
set_tests_properties(mesh_reconcile_snapshot_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME light_cache_certificate_test
    SOURCES src/Test/LightCacheCertificateTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME light_cache_certificate_test COMMAND $<TARGET_FILE:light_cache_certificate_test>)
set_tests_properties(light_cache_certificate_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME diagnostic_flight_controller_test
    SOURCES src/Test/DiagnosticFlightControllerTest.cpp
            src/App/Diagnostics/DiagnosticFlightController.cpp
    LIBRARIES glm::glm)
add_test(NAME diagnostic_flight_controller_test COMMAND $<TARGET_FILE:diagnostic_flight_controller_test>)
set_tests_properties(diagnostic_flight_controller_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME diagnostic_flight_replay_test
    SOURCES src/Test/DiagnosticFlightReplayTest.cpp
            src/App/Diagnostics/DiagnosticFlightController.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME diagnostic_flight_replay_test COMMAND $<TARGET_FILE:diagnostic_flight_replay_test>)
set_tests_properties(diagnostic_flight_replay_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME diagnostic_flight_replay
    SOURCES src/App/Diagnostics/DiagnosticFlightReplayMain.cpp
            src/App/Diagnostics/DiagnosticFlightController.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME diagnostic_world_actions_test
    SOURCES src/Test/DiagnosticWorldActionsTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME diagnostic_world_actions_test COMMAND $<TARGET_FILE:diagnostic_world_actions_test>)
set_tests_properties(diagnostic_world_actions_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME framebuffer_surface_cell_test
    SOURCES src/Test/FramebufferSurfaceCellTest.cpp src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)
add_test(NAME framebuffer_surface_cell_test COMMAND $<TARGET_FILE:framebuffer_surface_cell_test>)
set_tests_properties(framebuffer_surface_cell_test PROPERTIES LABELS "unit;rendering")

cubatarium_add_test(NAME chunk_load_priority_test
    SOURCES
        src/World/Chunks/ChunkLoadPriority.cpp
        src/Test/ChunkLoadPriorityTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME completed_job_queue_test
    SOURCES
        src/Test/CompletedJobQueueTest.cpp)

cubatarium_add_test(NAME ready_data_admission_test
    SOURCES src/Test/ReadyDataAdmissionTest.cpp
    LIBRARIES glm::glm)
add_test(NAME ready_data_admission_test COMMAND $<TARGET_FILE:ready_data_admission_test>)
set_tests_properties(ready_data_admission_test PROPERTIES LABELS "unit;streaming")

cubatarium_add_test(NAME audio_event_queue_test
    SOURCES
        src/Audio/AudioEventQueue.cpp
        src/Test/AudioEventQueueTest.cpp)

cubatarium_add_test(NAME audio_settings_json_test
    SOURCES
        src/App/Settings/AudioSettings.cpp
        src/Test/AudioSettingsTest.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME material_sound_map_test
    SOURCES
        src/Audio/MaterialSoundMap.cpp
        src/Test/MaterialSoundMapTest.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME footstep_phase_wrap_test
    SOURCES
        src/Creatures/Locomotion/CreatureLocomotionFacts.cpp
        src/Creatures/Locomotion/LocomotionStateDerive.cpp
        src/Test/FootstepPhaseTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME music_manifest_test
    SOURCES
        src/Audio/MusicManifest.cpp
        src/Test/MusicManifestTest.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME terrain_column_util_test
    SOURCES
        src/World/Chunks/Chunk.cpp
        src/World/Chunks/ChunkManager.cpp
        src/World/Chunks/ChunkBuffer.cpp
        src/World/Chunks/TerrainColumnUtil.cpp
        src/World/Core/BlockWorld.cpp
        src/World/Core/IUBlockWriter.cpp
        src/Blocks/BlockRegistry.cpp
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Physics/MaterialReactionRules.cpp
        src/World/Physics/MaterialReactionRulesRegistry.cpp
        src/World/Math/FluidCellState.cpp
        src/World/Math/GridMath.cpp
        src/Test/TerrainColumnUtilTest.cpp
    DEFINITIONS CUTUM_PHYSICS_LIGHT_REGISTRY
    LIBRARIES glm::glm)

cubatarium_add_test(NAME chunk_residency_trace_test
    SOURCES src/Test/ChunkResidencyTraceTest.cpp
    LIBRARIES glm::glm Threads::Threads)
add_test(NAME chunk_residency_trace_overflow_test
    COMMAND $<TARGET_FILE:chunk_residency_trace_test> overflow)
set_tests_properties(chunk_residency_trace_overflow_test PROPERTIES LABELS "unit")
add_test(NAME chunk_residency_trace_start_column_test
    COMMAND $<TARGET_FILE:chunk_residency_trace_test> start)
set_tests_properties(chunk_residency_trace_start_column_test PROPERTIES LABELS "unit")
add_test(NAME chunk_residency_trace_secondary_test
    COMMAND $<TARGET_FILE:chunk_residency_trace_test> secondary)
add_test(NAME chunk_residency_trace_secondary_overflow_test
    COMMAND $<TARGET_FILE:chunk_residency_trace_test> secondary-overflow)
set_tests_properties(chunk_residency_trace_secondary_test
    chunk_residency_trace_secondary_overflow_test PROPERTIES LABELS "unit")

cubatarium_add_test(NAME chunk_streamer_committed_column_test
    SOURCES
        src/World/IO/BinaryChunkSerializer.cpp
        src/World/Chunks/Chunk.cpp
        src/World/Chunks/ChunkManager.cpp
        src/World/Chunks/ChunkBuffer.cpp
        src/World/Chunks/TerrainColumnUtil.cpp
        src/World/Chunks/ChunkStreamer.cpp
        src/World/Chunks/ChunkLoadPriority.cpp
        src/World/Physics/CollisionReadiness.cpp
        src/World/Core/RuntimeTuning.cpp
        src/World/Core/BlockWorld.cpp
        src/World/Core/IUBlockWriter.cpp
        src/Blocks/BlockRegistry.cpp
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Physics/MaterialReactionRules.cpp
        src/World/Physics/MaterialReactionRulesRegistry.cpp
        src/World/Math/FluidCellState.cpp
        src/World/Math/GridMath.cpp
        src/Test/ChunkStreamerCommittedColumnTest.cpp
    DEFINITIONS CUTUM_PHYSICS_LIGHT_REGISTRY
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME negative_height_storage_test
    SOURCES
        src/Core/Jobs/JobThreadPool.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/World/IO/AsyncChunkIO.cpp
        src/World/Chunks/Chunk.cpp
        src/World/Chunks/ChunkManager.cpp
        src/World/Chunks/ChunkBuffer.cpp
        src/World/Chunks/TerrainColumnUtil.cpp
        src/World/Core/BlockWorld.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/World/Lighting/LightUtil.cpp
        src/World/Core/IUBlockWriter.cpp
        src/World/IO/BinaryChunkSerializer.cpp
        src/World/IO/JsonChunkSerializer.cpp
        src/World/IO/ChunkStorageService.cpp
        src/World/IO/ChunkStorageTypes.cpp
        src/Blocks/BlockRegistry.cpp
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Physics/MaterialReactionRules.cpp
        src/World/Physics/MaterialReactionRulesRegistry.cpp
        src/World/Math/FluidCellState.cpp
        src/World/Math/GridMath.cpp
        src/Test/NegativeHeightStorageTest.cpp
    DEFINITIONS CUTUM_PHYSICS_LIGHT_REGISTRY
    LIBRARIES glm::glm nlohmann_json::nlohmann_json Threads::Threads)

cubatarium_add_test(NAME world_mesh_service_test
    SOURCES
        src/Render/Mesh/ChunkDirtySet.cpp
        src/Test/WorldMeshServiceTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME chunk_mesh_revision_test
    SOURCES
        src/Render/Mesh/ChunkMeshRevisionRegistry.cpp
        src/Test/ChunkMeshRevisionTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME terrain_mesh_dirty_seam_test
    SOURCES
        src/World/Math/GridMath.cpp
        src/Test/TerrainMeshDirtySeamTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME packed_quad_parity_test
    SOURCES
        src/Test/PackedQuadParityTest.cpp
    DEFINITIONS CUBATARIUM_PACKED_SHADER_PATH="${CMAKE_SOURCE_DIR}/shaders/vshader_packed_greedy.glsl"
    LIBRARIES glm::glm)

cubatarium_add_test(NAME navigation_pathfinder_test
    SOURCES
        src/Navigation/UNavigationPathfinder.cpp
        src/Navigation/UWaypointFollower.cpp
        src/Navigation/NavigationPathBudget.cpp
        src/Test/NavigationPathfinderTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME creature_activity_steering_test
    SOURCES
        src/Activity/Helpers/CreatureActivitySteering.cpp
        src/Test/CreatureActivitySteeringTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_activity_steering_test
    COMMAND $<TARGET_FILE:creature_activity_steering_test>)
set_tests_properties(creature_activity_steering_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME locomotion_state_derive_test
    SOURCES
        src/Creatures/Locomotion/LocomotionStateDerive.cpp
        src/Creatures/Locomotion/CreatureLocomotionFacts.cpp
        src/World/Math/GridMath.cpp
        src/Test/LocomotionStateDeriveTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME creature_movement_diag_test
    SOURCES
        src/World/Diagnostics/CreatureMovementDiagnostics.cpp
        src/Test/CreatureMovementDiagTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME rider_carry_test
    SOURCES
        src/Test/RiderCarryTest.cpp
    LIBRARIES glm::glm)
add_test(NAME rider_carry_test COMMAND $<TARGET_FILE:rider_carry_test>)
set_tests_properties(rider_carry_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_reach_test
    SOURCES
        src/Test/CreatureReachTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_reach_test COMMAND $<TARGET_FILE:creature_reach_test>)
set_tests_properties(creature_reach_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_removal_queue_test
    SOURCES
        src/Test/CreatureRemovalQueueTest.cpp)
add_test(NAME creature_removal_queue_test COMMAND $<TARGET_FILE:creature_removal_queue_test>)
set_tests_properties(creature_removal_queue_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_gltf_clip_test
    SOURCES
        src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp
        src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
        src/Test/CreatureGltfClipTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_gltf_clip_test COMMAND $<TARGET_FILE:creature_gltf_clip_test>)
set_tests_properties(creature_gltf_clip_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_light_cache_test
    SOURCES
        src/Test/CreatureLightCacheTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_light_cache_test COMMAND $<TARGET_FILE:creature_light_cache_test>)
set_tests_properties(creature_light_cache_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME item_model_cache_revision_test
    SOURCES
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/ItemModelCacheRevisionTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME item_model_cache_revision_test COMMAND $<TARGET_FILE:item_model_cache_revision_test>)
set_tests_properties(item_model_cache_revision_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_tick_budget_test
    SOURCES
        src/Test/CreatureTickBudgetTest.cpp)
add_test(NAME creature_tick_budget_test COMMAND $<TARGET_FILE:creature_tick_budget_test>)
set_tests_properties(creature_tick_budget_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME wander_interval_test
    SOURCES
        src/Activity/Agents/WanderActivityAgent.cpp
        src/Activity/Helpers/CreatureActivitySteering.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/World/Diagnostics/CreatureMovementDiagnostics.cpp
        src/Test/WanderIntervalTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME wander_interval_test COMMAND $<TARGET_FILE:wander_interval_test>)
set_tests_properties(wander_interval_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_stats_test
    SOURCES
        src/Creatures/Stats/CreatureStatsDefaults.cpp
        src/Creatures/Stats/CreatureStatsJson.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Definition/CreatureDefinitionStorage.cpp
        src/Creatures/Visual/CreatureRigidModelLoader.cpp
        src/Test/CreatureStatsTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_stats_test COMMAND $<TARGET_FILE:creature_stats_test>)
set_tests_properties(creature_stats_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME influence_hit_math_test
    SOURCES
        src/Creatures/Influence/InfluenceHitMath.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/InfluenceHitMathTest.cpp
    LIBRARIES glm::glm)
add_test(NAME influence_hit_math_test COMMAND $<TARGET_FILE:influence_hit_math_test>)
set_tests_properties(influence_hit_math_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME tool_capabilities_test
    SOURCES
        src/Items/ToolCapabilities.cpp
        src/Test/ToolCapabilitiesTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME armor_equipment_test
    SOURCES
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/ArmorEquipmentTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME item_render_asset_test
    SOURCES src/Items/ItemRenderAsset.cpp src/Items/ItemVisualState.cpp src/Items/ItemLabDiagnostics.cpp src/Test/ItemRenderAssetTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME item_render_asset_test COMMAND $<TARGET_FILE:item_render_asset_test>)
set_tests_properties(item_render_asset_test PROPERTIES LABELS "unit;items")

cubatarium_add_test(NAME ownership_snapshot_test SOURCES src/Test/OwnershipSnapshotTest.cpp LIBRARIES nlohmann_json::nlohmann_json)
add_test(NAME ownership_snapshot_test COMMAND $<TARGET_FILE:ownership_snapshot_test>)
set_tests_properties(ownership_snapshot_test PROPERTIES LABELS "unit;items")

cubatarium_add_test(NAME throw_math_test SOURCES src/Test/ThrowMathTest.cpp LIBRARIES glm::glm)
add_test(NAME throw_math_test COMMAND $<TARGET_FILE:throw_math_test>)
set_tests_properties(throw_math_test PROPERTIES LABELS "unit;items")

cubatarium_add_test(NAME inventory_transactions_test
    SOURCES
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Game/Crafting/RecipeRegistry.cpp
        src/Test/InventoryTransactionsTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME inventory_transactions_test COMMAND $<TARGET_FILE:inventory_transactions_test>)
set_tests_properties(inventory_transactions_test PROPERTIES LABELS "unit;items")
target_compile_definitions(armor_equipment_test PRIVATE CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

cubatarium_add_test(NAME creature_skinned_draw_test
    SOURCES
        src/Creatures/Visual/CreatureBonePaletteGpu.cpp
        src/Test/CreatureSkinnedDrawTest.cpp
    LIBRARIES OpenGL::GL glm::glm GLEW::GLEW)

cubatarium_add_test(NAME creature_bone_skeleton_loader_test
    SOURCES
        src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
        src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
        src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
        src/Test/CreatureBoneSkeletonLoaderTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

# Explicit research utility, not a test that can grant art/contact acceptance.
add_executable(creature_bone_motion_audit EXCLUDE_FROM_ALL
    tools/research/creature_bone_motion_audit.cpp
    src/Creatures/Stats/CreatureStatsDefaults.cpp
    src/Creatures/Stats/CreatureStatsJson.cpp
    src/Creatures/Locomotion/LocomotionTypes.cpp
    src/Creatures/Core/CreatureCatalogTypes.cpp
    src/Creatures/Definition/CreatureDefinitionStorage.cpp
    src/Creatures/Visual/CreatureRigidModelLoader.cpp
    src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
    src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
    src/Pose/BoneSkeleton/BoneSkeletonPoseEngine.cpp
    src/Pose/BoneSkeleton/SkeletalAnimationProfiles.cpp)
target_include_directories(creature_bone_motion_audit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_bone_motion_audit PRIVATE glm::glm nlohmann_json::nlohmann_json)

add_executable(creature_gltf_render_envelope_audit EXCLUDE_FROM_ALL
    tools/research/creature_gltf_render_envelope_audit.cpp
    src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp
    src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
    src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp)
target_include_directories(creature_gltf_render_envelope_audit PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(creature_gltf_render_envelope_audit PRIVATE glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME creature_bone_ground_clearance_test
    SOURCES src/Test/CreatureBoneGroundClearanceTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_bone_ground_clearance_test COMMAND $<TARGET_FILE:creature_bone_ground_clearance_test>)
set_tests_properties(creature_bone_ground_clearance_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_gltf_transition_ground_clearance_test
    SOURCES src/Test/CreatureGltfTransitionGroundClearanceTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_gltf_transition_ground_clearance_test COMMAND $<TARGET_FILE:creature_gltf_transition_ground_clearance_test>)
set_tests_properties(creature_gltf_transition_ground_clearance_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_gltf_render_envelope_test
    SOURCES src/Test/CreatureGltfRenderEnvelopeTest.cpp
        src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
        src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_gltf_render_envelope_test COMMAND $<TARGET_FILE:creature_gltf_render_envelope_test>)
set_tests_properties(creature_gltf_render_envelope_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_draw_clip_bounds_test
    SOURCES src/Test/CreatureDrawClipBoundsTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_draw_clip_bounds_test COMMAND $<TARGET_FILE:creature_draw_clip_bounds_test>)
set_tests_properties(creature_draw_clip_bounds_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME known_air_terrain_slice_policy_test
    SOURCES src/Test/KnownAirTerrainSlicePolicyTest.cpp)
add_test(NAME known_air_terrain_slice_policy_test COMMAND $<TARGET_FILE:known_air_terrain_slice_policy_test>)
set_tests_properties(known_air_terrain_slice_policy_test PROPERTIES LABELS "unit;creatures;streaming")

cubatarium_add_test(NAME creature_equipment_fit_test
    SOURCES src/Test/CreatureEquipmentFitTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_equipment_fit_test COMMAND $<TARGET_FILE:creature_equipment_fit_test>)
set_tests_properties(creature_equipment_fit_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_gltf_loader_test
    SOURCES
        src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp
        src/Creatures/Visual/Gltf/CreatureGltfAnimPlayer.cpp
        src/Creatures/Visual/Gltf/CreatureGltfBonePalette.cpp
        src/Test/CreatureGltfLoaderTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

set(PHYSICS_BLOCKWORLD_TEST_SOURCES
    src/World/Core/BlockWorld.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Math/GridMath.cpp
    src/World/Math/FluidCellState.cpp
    src/Blocks/BlockDefinitionStorage.cpp
    src/Blocks/BlockDefinition.cpp)

cubatarium_add_test(NAME capture_worker_integration_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Core/Jobs/JobThreadPool.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/MeshCaptureWorker.cpp
        src/Test/CaptureWorkerIntegrationTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME capture_incremental_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Core/Jobs/PipelineAdmission.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/MeshCaptureStore.cpp
        src/Test/CaptureIncrementalTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME capture_token_validation_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Core/Jobs/JobThreadPool.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/MeshCaptureStore.cpp
        src/Render/Mesh/MeshCaptureWorker.cpp
        src/Test/CaptureTokenValidationTest.cpp
    LIBRARIES glm::glm)

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

cubatarium_add_test(NAME block_update_queue_purge_test
    SOURCES
        src/World/Physics/BlockUpdateQueue.cpp
        src/World/Physics/PhysicsChunkDistance.cpp
        src/World/Physics/PhysicsQueuePurge.cpp
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/BlockUpdateQueuePurgeTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME liquid_queue_priority_test
    SOURCES
        src/World/Physics/FluidUpdateSet.cpp
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/LiquidQueuePriorityTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME chunk_rebuild_queue_test
    SOURCES
        src/World/Physics/ChunkRebuildQueue.cpp
        src/World/Physics/PhysicsChunkDistance.cpp
        src/World/Physics/PhysicsQueuePurge.cpp
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/ChunkRebuildQueueTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME block_update_queue_order_test
    SOURCES
        src/World/Physics/BlockUpdateQueue.cpp
        src/World/Physics/PhysicsChunkDistance.cpp
        src/World/Physics/PhysicsQueuePurge.cpp
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/BlockUpdateQueueOrderTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME voxel_dda_traversal_test
    SOURCES
        src/World/Collision/VoxelDdaTraversal.cpp
        src/Test/VoxelDdaTraversalTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME physics_profile_parse_test
    SOURCES
        src/Test/PhysicsProfileParseTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME inventory_icon_fingerprint_test
    SOURCES
        src/App/Platform/IUPlatformPaths.cpp
        src/ThirdParty/stb_image.cpp
        src/ThirdParty/stb_image_write.cpp
        src/Gui/Cache/InventoryIconService.cpp
        src/Test/InventoryIconFingerprintTest.cpp
    LIBRARIES OpenGL::GL GLEW::GLEW nlohmann_json::nlohmann_json glm::glm)

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

cubatarium_add_test(NAME deterministic_replay_test
    SOURCES
        ${PHYSICS_REPLAY_TEST_SOURCES}
        src/Test/DeterministicReplayTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME physics_budget_guard_test
    SOURCES
        ${PHYSICS_REPLAY_TEST_SOURCES}
        src/Test/PhysicsBudgetGuardTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME liquid_queue_backpressure_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/World/Physics/FluidUpdateSet.cpp
        src/Test/LiquidQueueBackpressureTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME collision_readiness_gate_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/World/Physics/CollisionReadiness.cpp
        src/Test/CollisionReadinessGateTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME movement_chunk_boundary_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/MovementChunkBoundaryTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME falling_blocks_stability_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FallingBlockRules.cpp
        src/World/Physics/Replay/WorldStateHasher.cpp
        src/Test/FallingBlocksStabilityTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

set(PHYSICS_REGISTRY_TEST_SOURCES
    src/Blocks/BlockRegistry.cpp
    src/Render/Textures/TextureBase.cpp
    src/Render/Textures/TextureCube.cpp)

cubatarium_add_test(NAME fluid_state_pack_test
    SOURCES
        src/Test/FluidStatePackTest.cpp
        src/World/Math/FluidCellState.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME world_view_settings_test
    SOURCES
        src/Test/WorldViewSettingsTest.cpp
        src/World/View/WorldViewSettings.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME view_ray_math_test
    SOURCES
        src/Test/ViewRayMathTest.cpp
        src/World/View/ViewRayMath.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME environment_config_celestial_test
    SOURCES
        src/Test/EnvironmentConfigCelestialTest.cpp
        src/World/Environment/EnvironmentConfig.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME weather_front_field_test
    SOURCES
        src/Test/WeatherFrontFieldTest.cpp
        src/World/Environment/WeatherField.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME chunk_light_pack_test
    SOURCES
        src/Test/ChunkLightPackTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME block_light_emission_parse_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/Test/BlockLightEmissionParseTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME block_hardness_parse_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/Test/BlockHardnessParseTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME block_break_duration_test
    SOURCES
        src/Test/BlockBreakDurationTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME inventory_mode_test
    SOURCES
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/InventoryModeTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME influence_use_test
    SOURCES
        src/Items/ItemUseRegistry.cpp
        src/Test/InfluenceUseTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME recipe_registry_test
    SOURCES
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Game/Crafting/RecipeRegistry.cpp
        src/Test/RecipeRegistryTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME resource_economy_test
    SOURCES
        src/Creatures/Core/CreatureInventory.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Game/Economy/ResourceEconomy.cpp
        src/Test/ResourceEconomyTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

target_compile_definitions(resource_economy_test PRIVATE CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

cubatarium_add_test(NAME item_survival_hooks_test
    SOURCES
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Core/CreatureInventory.cpp
        src/Creatures/Definition/CreatureDefinitionStorage.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/Creatures/Stats/CreatureStatsDefaults.cpp
        src/Creatures/Stats/CreatureStatsJson.cpp
        src/Creatures/Visual/CreatureRigidModelLoader.cpp
        src/Game/Economy/ResourceEconomy.cpp
        src/Items/ItemDefinitionStorage.cpp
        src/Items/ItemUseRegistry.cpp
        src/Items/ItemVisualPresetLibrary.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/ItemSurvivalHooksTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME item_survival_hooks_test COMMAND $<TARGET_FILE:item_survival_hooks_test>)
set_tests_properties(item_survival_hooks_test PROPERTIES LABELS "unit;items")

cubatarium_add_test(NAME mode_policy_test
    SOURCES
        src/Test/ModePolicyTest.cpp
    LIBRARIES glm::glm)
add_test(NAME mode_policy_test COMMAND $<TARGET_FILE:mode_policy_test>)
set_tests_properties(mode_policy_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_spawn_selection_test
    SOURCES
        src/Test/CreatureSpawnSelectionTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_spawn_selection_test
    COMMAND $<TARGET_FILE:creature_spawn_selection_test>)
set_tests_properties(creature_spawn_selection_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_spatial_index_test
    SOURCES
        src/World/Environment/CreatureSpatialIndex.cpp
        src/World/Math/GridMath.cpp
        src/Test/CreatureSpatialIndexTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_spatial_index_test
    COMMAND $<TARGET_FILE:creature_spatial_index_test>)
set_tests_properties(creature_spatial_index_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_visual_backend_select_test
    SOURCES
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Visual/CreatureVisualBackendSelect.cpp
        src/Test/CreatureVisualBackendSelectTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_visual_backend_select_test
    COMMAND $<TARGET_FILE:creature_visual_backend_select_test>)
set_tests_properties(creature_visual_backend_select_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_appearance_test
    SOURCES
        src/Creatures/Stats/CreatureStatsDefaults.cpp
        src/Creatures/Stats/CreatureStatsJson.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Definition/CreatureDefinitionStorage.cpp
        src/Creatures/Definition/SkinDefinitionStorage.cpp
        src/Creatures/Visual/CreatureRigidModelLoader.cpp
        src/Creatures/Visual/CreatureAppearance.cpp
        src/Test/CreatureAppearanceTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_appearance_test
    COMMAND $<TARGET_FILE:creature_appearance_test>)
set_tests_properties(creature_appearance_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME progress_snapshot_test
    SOURCES
        src/Core/Progress/LatestProgressSink.cpp
        src/Test/ProgressSnapshotTest.cpp
    LIBRARIES glm::glm)
add_test(NAME progress_snapshot_test
    COMMAND $<TARGET_FILE:progress_snapshot_test>)
set_tests_properties(progress_snapshot_test PROPERTIES LABELS "unit;gui")

cubatarium_add_test(NAME creature_model_integrity_test
    SOURCES
        src/Creatures/Stats/CreatureStatsDefaults.cpp
        src/Creatures/Stats/CreatureStatsJson.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Definition/CreatureDefinitionStorage.cpp
        src/Creatures/Visual/CreatureRigidModelLoader.cpp
        src/Creatures/Visual/CreatureVisualBackendSelect.cpp
        src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
        src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
        src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
        src/Creatures/Visual/Gltf/CreatureGltfLoader.cpp
        src/Pose/BoneSkeleton/BoneSkeletonPoseEngine.cpp
        src/Pose/BoneSkeleton/SkeletalAnimationProfiles.cpp
        src/Test/CreatureModelIntegrityTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_model_integrity_test
    COMMAND $<TARGET_FILE:creature_model_integrity_test>)
set_tests_properties(creature_model_integrity_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_shader_parity_test
    SOURCES
        src/Test/CreatureShaderParityTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME creature_shader_parity_test
    COMMAND $<TARGET_FILE:creature_shader_parity_test>)
set_tests_properties(creature_shader_parity_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_aquatic_spawn_test
    SOURCES
        src/Test/CreatureAquaticSpawnTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LIBRARIES glm::glm)
add_test(NAME creature_aquatic_spawn_test
    COMMAND $<TARGET_FILE:creature_aquatic_spawn_test>)
set_tests_properties(creature_aquatic_spawn_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_spawn_zones_test
    SOURCES
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/Test/CreatureSpawnZonesTest.cpp
    DEFINITIONS CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_spawn_zones_test
    COMMAND $<TARGET_FILE:creature_spawn_zones_test>)
set_tests_properties(creature_spawn_zones_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME environment_light_test
    SOURCES
        src/Test/EnvironmentLightTest.cpp
    LIBRARIES glm::glm)
add_test(NAME environment_light_test
    COMMAND $<TARGET_FILE:environment_light_test>)
set_tests_properties(environment_light_test PROPERTIES LABELS "unit;creatures")

# Collision boxes must match the visible glTF model (see tools/creature_bounds_audit.py).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  foreach(engine_tool_test IN ITEMS engine_smoke_verdict engine_smoke_run)
    add_test(NAME ${engine_tool_test}_test
        COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/test_${engine_tool_test}.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_tests_properties(${engine_tool_test}_test PROPERTIES LABELS "unit;streaming")
  endforeach()
  add_test(NAME creature_bounds_audit_test
      COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/creature_bounds_audit.py" --check
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
  set_tests_properties(creature_bounds_audit_test PROPERTIES LABELS "unit;creatures")
endif()

cubatarium_add_test(NAME creature_influence_channels_test
    SOURCES
        src/Creatures/Influence/InfluenceHitMath.cpp
        src/Items/ToolCapabilities.cpp
        src/Test/CreatureInfluenceChannelsTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_influence_channels_test
    COMMAND $<TARGET_FILE:creature_influence_channels_test>)
set_tests_properties(creature_influence_channels_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_spawn_planner_test
    SOURCES
        src/Creatures/Definition/CreatureDefinitionStorage.cpp
        src/Creatures/Stats/CreatureStatsDefaults.cpp
        src/Creatures/Stats/CreatureStatsJson.cpp
        src/Creatures/Locomotion/LocomotionTypes.cpp
        src/Creatures/Core/CreatureCatalogTypes.cpp
        src/Creatures/Visual/CreatureRigidModelLoader.cpp
        src/Creatures/Spawning/UCreatureSpawnPlanner.cpp
        src/Test/CreatureSpawnPlannerTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_spawn_planner_test
    COMMAND $<TARGET_FILE:creature_spawn_planner_test>)
set_tests_properties(creature_spawn_planner_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_draw_queue_test
    SOURCES
        src/Creatures/Visual/CreatureDrawQueue.cpp
        src/Test/CreatureDrawQueueTest.cpp
    LIBRARIES glm::glm)
add_test(NAME creature_draw_queue_test
    COMMAND $<TARGET_FILE:creature_draw_queue_test>)
set_tests_properties(creature_draw_queue_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME chunk_lighting_propagation_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/ChunkLighting.cpp
        src/World/Lighting/FullLightingPipeline.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/Test/GpuBlocklightFloodHostStub.cpp
        src/Test/GpuSkylightSeedHostStub.cpp
        src/Test/ChunkLightingPropagationTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

foreach(_sync_light_mode sync-noop sync-neighbor sync-halo unknown-boundary sparse-sky sync-parity edit-contract six-faces)
  add_test(NAME light_${_sync_light_mode}_test
      COMMAND $<TARGET_FILE:chunk_lighting_propagation_test> --${_sync_light_mode})
  set_tests_properties(light_${_sync_light_mode}_test PROPERTIES LABELS "unit;lighting;streaming")
endforeach()

cubatarium_add_test(NAME chunk_bulk_blocklight_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/ChunkLighting.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/Test/GpuBlocklightFloodHostStub.cpp
        src/Test/GpuSkylightSeedHostStub.cpp
        src/Test/ChunkBulkBlockLightTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME chunk_bulk_blocklight_test
    COMMAND $<TARGET_FILE:chunk_bulk_blocklight_test>)
set_tests_properties(chunk_bulk_blocklight_test PROPERTIES LABELS "unit;lighting")

cubatarium_add_test(NAME fluid_chunk_io_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/IO/BinaryChunkSerializer.cpp
        src/World/IO/JsonChunkSerializer.cpp
        src/Test/FluidChunkIoTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_spread_level_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/Test/FluidSpreadLevelTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_mesh_faces_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/GreedyMesher.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidMeshFacesTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fog_range_smoother_test
    SOURCES
        src/Test/FogRangeSmootherTest.cpp)

cubatarium_add_test(NAME seam_absent_neighbor_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/GreedyMesher.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/SeamAbsentNeighborTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME greedy_cross_plant_ground_face_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/GreedyMesher.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/GreedyCrossPlantGroundFaceTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME underwater_fog_column_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Core/FluidColumnSurfaceQuery.cpp
        src/Test/UnderwaterFogColumnTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME camera_stability_test
    SOURCES
        src/Test/CameraStabilityTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME streaming_render_ready_invariants_test
    SOURCES
        src/Test/StreamingRenderReadyInvariantsTest.cpp
        src/World/Streaming/ColumnFlowScheduler.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mesh_neighbor_policy_test
    SOURCES
        src/Test/MeshNeighborPolicyTest.cpp
        src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mesh_publish_contract_test
    SOURCES
        src/Test/MeshPublishContractTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME miss_first_mesh_class_test
    SOURCES
        src/Test/MissFirstMeshClassTest.cpp
        src/World/Streaming/EnterVisualGate.cpp
        src/World/Streaming/MemoryBudgetController.cpp
        src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mark_relit_characterization_test
    SOURCES
        src/Test/MarkRelitCharacterizationTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mesh_light_stale_policy_test
    SOURCES
        src/Test/MeshLightStalePolicyTest.cpp)

cubatarium_add_test(NAME relight_install_planner_test
    SOURCES
        src/Test/RelightInstallPlannerTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME section_light_field_ownership_test
    SOURCES
        src/Test/SectionLightFieldOwnershipTest.cpp
        src/World/Chunks/Chunk.cpp
        src/World/Chunks/ChunkManager.cpp
        src/World/Math/FluidCellState.cpp
        src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)
add_test(NAME section_light_field_ownership_test
    COMMAND $<TARGET_FILE:section_light_field_ownership_test>)
set_tests_properties(section_light_field_ownership_test PROPERTIES LABELS "unit;lighting;streaming")

cubatarium_add_test(NAME chunk_render_demand_test
    SOURCES
        src/Test/ChunkRenderDemandTest.cpp
        src/World/Streaming/ChunkRenderDemand.cpp
        src/World/Math/GridMath.cpp
        src/World/Chunks/Chunk.cpp
        src/World/Math/FluidCellState.cpp
        src/World/Diagnostics/JobStageTrace.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME async_relight_lifecycle_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/World/Lighting/AsyncRelightBuilder.cpp
        src/World/Diagnostics/JobStageTrace.cpp
        src/Core/Jobs/JobThreadPool.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Test/AsyncRelightLifecycleTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

if(CUBATARIUM_MSVC_DESKTOP)
  cubatarium_add_test(NAME log_allocation_lifecycle_test
      SOURCES src/App/Platform/Log.cpp src/App/Platform/WorkerJobKind.cpp src/WorldGen/Core/Noise.cpp src/Test/LogAllocationLifecycleTest.cpp
      LIBRARIES glog::glog nlohmann_json::nlohmann_json
      WIN32_LIBRARIES dbghelp)
  add_test(NAME log_allocation_lifecycle_test COMMAND $<TARGET_FILE:log_allocation_lifecycle_test>
      ${CMAKE_CURRENT_BINARY_DIR}/log-allocation-lifecycle.json)
  set_tests_properties(log_allocation_lifecycle_test PROPERTIES LABELS "integration;memory;logging" TIMEOUT 120)
  add_test(NAME log_runtime_source_test COMMAND $<TARGET_FILE:log_allocation_lifecycle_test>
      --source-profile ${CMAKE_CURRENT_BINARY_DIR}/log-runtime-source.json)
  set_tests_properties(log_runtime_source_test PROPERTIES LABELS "integration;memory;logging" TIMEOUT 120)
  add_test(NAME glog_resource_source_test COMMAND $<TARGET_FILE:log_allocation_lifecycle_test>
      --resources ${CMAKE_CURRENT_BINARY_DIR}/glog-resource-source.json)
  set_tests_properties(glog_resource_source_test PROPERTIES LABELS "integration;memory;logging" TIMEOUT 120)
endif()
add_test(NAME async_relight_lifecycle_test
    COMMAND $<TARGET_FILE:async_relight_lifecycle_test>)
set_tests_properties(async_relight_lifecycle_test PROPERTIES TIMEOUT 20)
add_test(NAME catalog_relight_queue_owners_test
    COMMAND $<TARGET_FILE:async_relight_lifecycle_test> --catalog-queue-owners
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-relight-queue-owners.json)
set_tests_properties(catalog_relight_queue_owners_test PROPERTIES LABELS "integration;catalog" TIMEOUT 120)

cubatarium_add_test(NAME relight_result_install_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/World/Streaming/ChunkRenderDemand.cpp
        src/World/Diagnostics/JobStageTrace.cpp
        src/Test/RelightResultInstallTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

add_test(NAME pending_light_coverage_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --pending-coverage)
set_tests_properties(pending_light_coverage_test PROPERTIES LABELS "unit;streaming;lighting")
add_test(NAME light_calculation_certificate_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --light-proof)
set_tests_properties(light_calculation_certificate_test PROPERTIES LABELS "unit;streaming;lighting")
add_test(NAME section_light_geometry_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --geometry-halo)
set_tests_properties(section_light_geometry_test PROPERTIES LABELS "unit;streaming;lighting;cpu")
add_test(NAME section_light_job_order_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --job-order)
set_tests_properties(section_light_job_order_test PROPERTIES LABELS "unit;streaming;lighting;cpu" TIMEOUT 120)
add_test(NAME demanded_air_lifetime_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --demand-air)
set_tests_properties(demanded_air_lifetime_test PROPERTIES LABELS "unit;streaming;lighting;cpu" TIMEOUT 30)
add_test(NAME section_skylight_frontier_test
    COMMAND $<TARGET_FILE:relight_result_install_test> --sky-frontier)
set_tests_properties(section_skylight_frontier_test PROPERTIES LABELS "unit;streaming;lighting;cpu" TIMEOUT 30)

cubatarium_add_test(NAME gpu_skylight_seed_fluid_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Blocks/BlockDefinitionStorage.cpp
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/GpuSkylightColumnSeed.cpp
        src/Test/GpuSkylightSeedFluidTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME required_data_coverage_test
    SOURCES src/Test/RequiredDataCoverageTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mark_relit_integration_test
    SOURCES
        src/Test/MarkRelitIntegrationTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME focus_ingress_policy_test
    SOURCES
        src/Test/FocusIngressPolicyTest.cpp)

cubatarium_add_test(NAME nearest_visible_slice_test
    SOURCES src/Test/NearestVisibleSliceTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME frame_streaming_budget_test
    SOURCES
        src/Test/FrameStreamingBudgetTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME idle_recovery_policy_test
    SOURCES
        src/Test/IdleRecoveryPolicyTest.cpp)

cubatarium_add_test(NAME seed_decision_policy_test
    SOURCES
        src/Test/SeedDecisionPolicyTest.cpp)

cubatarium_add_test(NAME column_desired_stage_test
    SOURCES
        src/Test/ColumnDesiredStageTest.cpp)

cubatarium_add_test(NAME cruise_sot_phase_test
    SOURCES
        src/Test/CruiseSoTPhaseTest.cpp
        src/World/Streaming/ColumnRecordCoordinator.cpp
    LIBRARIES glm::glm glog::glog)

cubatarium_add_test(NAME visible_black_attribution_test
    SOURCES
        src/Test/VisibleBlackAttributionTest.cpp)

cubatarium_add_test(NAME draw_oracle_gate_test
    SOURCES
        src/Test/DrawOracleGateTest.cpp)

cubatarium_add_test(NAME block_catalog_queries_test
    SOURCES
        src/Test/BlockCatalogQueriesTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME mesh_inputs_catalog_test
    SOURCES
        src/Test/MeshInputsCatalogTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME normal_shutdown_test
    SOURCES
        src/Test/NormalShutdownTest.cpp
        src/Core/Jobs/JobThreadPool.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Core/Jobs/JobThreadBudget.cpp)

cubatarium_add_test(NAME frame_deadline_test
    SOURCES
        src/Test/FrameDeadlineTest.cpp)

cubatarium_add_test(NAME streamer_deadline_policy_test
    SOURCES
        src/Test/StreamerDeadlinePolicyTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME streamer_amortize_policy_test
    SOURCES
        src/Test/StreamerAmortizePolicyTest.cpp)

cubatarium_add_test(NAME sticky_overlay_remesh_policy_test
    SOURCES
        src/Test/StickyOverlayRemeshPolicyTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME column_emerge_bump_test
    SOURCES
        src/Test/ColumnEmergeBumpTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME visual_ready_for_ring_policy_test
    SOURCES
        src/Test/VisualReadyForRingPolicyTest.cpp)

cubatarium_add_test(NAME frustum_clip_test
    SOURCES
        src/Test/FrustumClipTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME column_flow_scheduler_test
    SOURCES
        src/Test/ColumnFlowSchedulerTest.cpp
        src/World/Streaming/ColumnFlowScheduler.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME column_scheduler_audit_repro
    SOURCES
        tools/research/ColumnSchedulerAuditRepro.cpp
        src/World/Streaming/ColumnFlowScheduler.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME edit_mesh_remesh_policy_test
    SOURCES
        src/Test/EditMeshRemeshPolicyTest.cpp
        src/World/Mesh/EditMeshRemeshPolicy.cpp
        src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME dig_seam_queue_test
    SOURCES
        src/Test/DigSeamQueueTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME render_backend_factory_test
    SOURCES
        src/Test/RenderBackendFactoryTest.cpp
        src/Render/Backend/RenderBackendSelect.cpp
        src/Render/Backend/RenderBackendCaps.cpp)

cubatarium_add_test(NAME android_gpu_policy_test
    SOURCES
        src/Test/AndroidGpuPolicyTest.cpp
        src/Render/Backend/AndroidGpuPolicy.cpp
        src/Render/Backend/RenderBackendCaps.cpp
    LIBRARIES glog::glog)

cubatarium_add_test(NAME mesh_gpu_store_mdi_test
    SOURCES
        src/Test/MeshGpuStoreMdiTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME greedy_vertex_pool_lifetime_test
    SOURCES
        src/Test/GreedyVertexPoolLifetimeTest.cpp
        src/Render/Engine/GreedyVertexPoolLifetime.cpp)

cubatarium_add_test(NAME gpu_greedy_face_extract_test
    SOURCES
        src/Test/GpuGreedyFaceExtractTest.cpp
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Math/GridMath.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME gpu_extract_mesher_parity_test
    SOURCES
        src/Test/GpuExtractMesherParityTest.cpp
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/GreedyMesher.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF} CUBATARIUM_GPU_EMIT_SOURCE_PATH="${CMAKE_SOURCE_DIR}/src/Render/Mesh/GpuGreedyOpaqueEmit.cpp"
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME greedy_transparent_sort_test
    SOURCES
        src/Test/GreedyTransparentSortTest.cpp
        src/Render/Pipeline/GreedyTransparentSort.cpp
        src/Render/Backend/RenderBackendCaps.cpp
        src/Render/Backend/GpuHotPathFallback.cpp
        src/Test/GpuTransparentSortHostStub.cpp
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Math/GridMath.cpp
    DEFINITIONS CUBATARIUM_GLES ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME gpu_skylight_column_seed_test
    SOURCES
        src/Test/GpuSkylightColumnSeedTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME gpu_skylight_merge_test
    SOURCES
        src/World/Chunks/Chunk.cpp
        src/World/Math/FluidCellState.cpp
        src/Test/GpuSkylightMergeTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME gpu_fluid_column_scan_test
    SOURCES
        src/Test/GpuFluidColumnScanTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME docked_overlay_layout_test
    SOURCES
        src/Test/DockedOverlayLayoutTest.cpp
        src/Gui/Layout/DockedOverlayLayout.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME gui_scrollbar_controller_test
    SOURCES
        src/Test/GuiScrollbarControllerTest.cpp
        src/Gui/Core/GuiScrollbarController.cpp)

cubatarium_add_test(NAME touch_input_bridge_lifecycle_test
    SOURCES
        src/App/Platform/TouchInputBridge.cpp
        src/Test/TouchInputBridgeLifecycleTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME fluid_surface_slice_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Core/FluidColumnSurfaceQuery.cpp
        src/Render/Mesh/FluidSurfaceColumnSlice.cpp
        src/Test/GpuFluidColumnScanPreferStub.cpp
        src/Test/FluidSurfaceSliceTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_surface_pack_reuse_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Core/FluidColumnSurfaceQuery.cpp
        src/Render/Mesh/FluidSurfaceColumnSlice.cpp
        src/Test/GpuFluidColumnScanPreferStub.cpp
        src/Test/FluidSurfacePackReuseTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_surface_map_logic_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Core/FluidColumnSurfaceQuery.cpp
        src/World/Core/RuntimeTuning.cpp
        src/Render/Mesh/FluidSurfaceColumnSlice.cpp
        src/Test/GpuFluidColumnScanPreferStub.cpp
        src/Test/FluidSurfaceMapLogicTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME underwater_waterline_test
    SOURCES
        src/Test/UnderwaterWaterlineTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME liquid_viscosity_gate_test
    SOURCES
        ${PHYSICS_FLUID_SIM_SOURCES}
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/LiquidViscosityGateTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME liquid_non_renewable_flow_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/Test/LiquidNonRenewableFlowTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME liquid_flow_scenarios_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/Test/LiquidFlowScenariosTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_queue_integration_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/World/Physics/FluidUpdateSet.cpp
        src/Test/FluidQueueIntegrationTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_stable_puddle_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/World/Physics/FluidUpdateSet.cpp
        src/Test/FluidStablePuddleTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_queue_drain_integration_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/World/Physics/FluidUpdateSet.cpp
        src/Test/FluidQueueDrainIntegrationTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_sim_mesh_integration_test
    SOURCES
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
        src/Test/FluidSimMeshIntegrationTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_placement_liquid_decor_test
    SOURCES
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
        src/Test/FluidPlacementLiquidDecorTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME decor_mesh_integration_test
    SOURCES
        src/World/Mesh/WorldMeshService.cpp
        src/World/Mesh/EditMeshRemeshPolicy.cpp
        src/Test/WorldMeshServiceCameraStub.cpp
        src/World/IO/BinaryChunkSerializer.cpp
        src/Render/Engine/ShaderManager.cpp
        src/App/Platform/IUPlatformPaths.cpp
        src/Render/Engine/CrossGpuBackend.cpp
        src/Render/Engine/GreedyGpuBackend.cpp
        src/Render/Engine/GreedyGpuPublication.cpp
        src/Render/Engine/GreedyVertexPool.cpp
        src/Render/Engine/MdiVertexPoolStore.cpp
        src/Render/Backend/GpuHotPathFallback.cpp
        src/World/Core/RuntimeTuning.cpp
        src/Render/Lighting/SectionLightDrawTable.cpp
        src/Render/Lighting/SectionLightAtlas.cpp
        src/Render/Lighting/SectionLightBindingTable.cpp
        src/Render/Lighting/SectionRenderLightBindings.cpp
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Lighting/LightUtil.cpp
        src/World/Lighting/ChunkRelightSnapshot.cpp
        src/World/Lighting/FlatLightingPipeline.cpp
        src/Core/Jobs/JobThreadPool.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/Render/Engine/DistanceFog.cpp
        src/Render/Engine/HorizonFogColor.cpp
        src/Render/Gl/ComputeProgram.cpp
        src/Render/Mesh/AsyncMeshBuilder.cpp
        src/Render/Mesh/ChunkDirtySet.cpp
        src/Render/Mesh/ChunkMeshCache.cpp
        src/World/Streaming/ChunkRenderDemand.cpp
        src/World/Diagnostics/JobStageTrace.cpp
        src/Render/Mesh/ChunkMeshRevisionRegistry.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/MeshCaptureStore.cpp
        src/Render/Mesh/MeshCaptureWorker.cpp
        src/Render/Mesh/FluidSurfaceColumnSlice.cpp
        src/Render/Mesh/GpuGreedyOpaqueEmit.cpp
        src/Render/Mesh/GpuGreedyMesher.cpp
        src/Render/Mesh/GpuMeshPipeline.cpp
        src/Render/Mesh/GpuMeshSlotAllocator.cpp
        src/Render/Backend/RenderBackendCaps.cpp
        src/Test/GpuFluidColumnScanPreferStub.cpp
        src/Render/Mesh/GreedyMesher.cpp
        src/World/Core/FluidColumnSurfaceQuery.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/App/Platform/Log.cpp
        src/Test/DecorMeshIntegrationTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog OpenGL::GL GLEW::GLEW)
if(WIN32)
    target_link_libraries(decor_mesh_integration_test dbghelp)
endif()
add_test(NAME catalog_publication_roots_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --catalog-publication-roots
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-publication-roots-cpu.json)
set_tests_properties(catalog_publication_roots_test PROPERTIES
    LABELS "integration;catalog" TIMEOUT 120)
add_test(NAME catalog_mesh_queue_owners_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --catalog-queue-owners
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-mesh-queue-owners.json)
set_tests_properties(catalog_mesh_queue_owners_test PROPERTIES LABELS "integration;catalog" TIMEOUT 120)
if(NOT ANDROID AND NOT CUBATARIUM_GLES)
  target_link_libraries(decor_mesh_integration_test glfw)
  target_compile_definitions(decor_mesh_integration_test PRIVATE SECTION_LIGHT_BINDING_DRIVER=1 CUBATARIUM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  add_test(NAME catalog_publication_roots_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --catalog-publication-roots-driver
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-publication-roots-gl.json)
  set_tests_properties(catalog_publication_roots_driver_test PROPERTIES
    LABELS "gpu;integration;catalog" SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_test(NAME gpu_material_visibility_gles_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --face-visibility-gles-driver)
  set_tests_properties(gpu_material_visibility_gles_driver_test PROPERTIES
    LABELS "gpu;integration;gles" SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_test(NAME gpu_material_visibility_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --face-visibility-driver)
  set_tests_properties(gpu_material_visibility_driver_test PROPERTIES
    LABELS "gpu;integration" SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_test(NAME section_render_light_bindings_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --light-binding-driver)
  set_tests_properties(section_render_light_bindings_driver_test PROPERTIES
    LABELS "gpu;integration" SKIP_RETURN_CODE 77 TIMEOUT 60)
  add_test(NAME gpu_geometry_light_lifecycle_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --gpu-geometry-light-lifecycle-driver)
  set_tests_properties(gpu_geometry_light_lifecycle_driver_test PROPERTIES
    LABELS "gpu;integration;streaming" SKIP_RETURN_CODE 77 TIMEOUT 90)
  add_test(NAME cross_light_bindings_gles_driver_test
    COMMAND $<TARGET_FILE:decor_mesh_integration_test> --cross-light-gles-driver)
  set_tests_properties(cross_light_bindings_gles_driver_test PROPERTIES
    LABELS "gpu;integration;gles" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

cubatarium_add_test(NAME mesh_schedule_retry_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Core/Jobs/JobThreadPool.cpp
        src/App/Platform/WorkerJobKind.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Render/Mesh/MeshCaptureWorker.cpp
        src/Test/MeshScheduleRetryTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME column_job_graph_stage_test
    SOURCES
        src/Test/ColumnJobGraphStageTest.cpp)

cubatarium_add_test(NAME fluid_worldgen_seal_test
    SOURCES
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
        src/WorldGen/Sampling/AquiferSampler.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/WorldGen/Core/Noise.cpp
        src/Test/ChunkLightingTestStubs.cpp
        src/Test/FluidWorldGenSealTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME ravine_carver_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/ResourcePacks/BlockNameUtil.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Features/RavineCarver.cpp
        src/Test/RavineCarverTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME valley_carver_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/ResourcePacks/BlockNameUtil.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/ClimateSampler.cpp
        src/WorldGen/Sampling/TerrainClimateMapper.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/WorldGen/Features/ValleyCarver.cpp
        src/WorldGen/Features/LakeCarver.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/ThirdParty/stb_image.cpp
        src/Test/ValleyCarverTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME mudflow_erosion_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/ResourcePacks/BlockNameUtil.cpp
        src/World/Objects/ObjectUtil.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/WorldGen/Stages/MudflowErosion.cpp
        src/Test/MudflowErosionTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME density_refine_parity_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/ResourcePacks/BlockNameUtil.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/BiomeSampler.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/WorldGen/Sampling/ClimateSampler.cpp
        src/WorldGen/Sampling/TerrainClimateMapper.cpp
        src/WorldGen/Sampling/OverworldHeightSampler.cpp
        src/WorldGen/Sampling/DensityRouter.cpp
        src/WorldGen/Sampling/DensityFieldSampler.cpp
        src/WorldGen/Sampling/AquiferSampler.cpp
        src/WorldGen/Sampling/ColumnSample.cpp
        src/WorldGen/Sampling/CoarseHeightCache.cpp
        src/WorldGen/Features/CaveDepthBand.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/ThirdParty/stb_image.cpp
        src/Test/DensityRefineParityTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_surface_perf_probe
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/ResourcePacks/BlockNameUtil.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/DensityRouter.cpp
        src/WorldGen/Sampling/DensityFieldSampler.cpp
        src/WorldGen/Sampling/ClimateSampler.cpp
        src/WorldGen/Sampling/TerrainClimateMapper.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/WorldGen/Features/CaveDepthBand.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/Test/WorldGenSurfacePerfProbe.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME noise_permutation_cache_test
    SOURCES
        src/WorldGen/Core/Noise.cpp
        src/Test/NoisePermutationCacheTest.cpp)
if(CUBATARIUM_MSVC_DESKTOP)
  cubatarium_add_test(NAME noise_storage_source_test
      SOURCES src/WorldGen/Core/Noise.cpp src/Test/NoiseStorageSourceTest.cpp
      LIBRARIES nlohmann_json::nlohmann_json)
  add_test(NAME noise_storage_source_test COMMAND $<TARGET_FILE:noise_storage_source_test>
      ${CMAKE_CURRENT_BINARY_DIR}/noise-storage-source.json)
  set_tests_properties(noise_storage_source_test PROPERTIES LABELS "integration;memory;noise" TIMEOUT 120)
endif()

cubatarium_add_test(NAME chunk_buffer_capture_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Test/ChunkBufferCaptureTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_smoothness_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/ResourcePacks/BlockNameUtil.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/DensityRouter.cpp
        src/WorldGen/Sampling/DensityFieldSampler.cpp
        src/WorldGen/Sampling/BiomeSampler.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/WorldGen/Sampling/ClimateSampler.cpp
        src/WorldGen/Sampling/TerrainClimateMapper.cpp
        src/WorldGen/Sampling/OverworldHeightSampler.cpp
        src/WorldGen/Sampling/ColumnSample.cpp
        src/WorldGen/Sampling/CoarseHeightCache.cpp
        src/WorldGen/Features/CaveDepthBand.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/ThirdParty/stb_image.cpp
        src/Test/WorldGenSmoothnessTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_vegetation_placement_test
    SOURCES
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
        src/WorldGen/Sampling/AquiferSampler.cpp
        src/WorldGen/Core/WorldGenContext.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/WorldGen/Features/ObjectPlacementConstraints.cpp
        src/WorldGen/Core/Noise.cpp
        src/App/Platform/Log.cpp
        src/Test/WorldGenVegetationPlacementTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog)

cubatarium_add_test(NAME worldgen_cave_depth_test
    SOURCES
        src/WorldGen/Features/CaveDepthBand.cpp
        src/Test/WorldGenCaveDepthTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME worldgen_aquifer_test
    SOURCES
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/AquiferSampler.cpp
        src/Test/WorldGenAquiferTest.cpp
    LIBRARIES glm::glm)

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
    src/WorldGen/Sampling/AquiferSampler.cpp
    src/WorldGen/Core/WorldGenContext.cpp
    src/WorldGen/Core/WorldGenBlockResolver.cpp
    src/WorldGen/Core/WorldGenRefs.cpp
    src/WorldGen/Core/WorldGenPack.cpp
    src/WorldGen/Core/WorldGenStageMask.cpp
    src/WorldGen/Sampling/BiomeRegistry.cpp
    src/ThirdParty/stb_image.cpp
    src/WorldGen/Core/WorldGenContentPin.cpp
    src/WorldGen/Core/WorldGenContentPinTls.cpp
    src/WorldGen/Features/ObjectFeatureConfig.cpp
    src/WorldGen/Core/Noise.cpp)

cubatarium_add_test(NAME worldgen_fluid_vegetation_pipeline_test
    SOURCES
        ${WORLDGEN_PIPELINE_TEST_SOURCES}
        src/Test/ChunkLightingTestStubs.cpp
        src/Test/WorldGenFluidVegetationPipelineTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_hills_vegetation_gate_test
    SOURCES
        src/World/Core/RuntimeTuning.cpp
        src/Test/WorldGenHillsVegetationGateTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME chunk_generation_registry_test
    SOURCES
        src/World/Chunks/ChunkGenerationToken.cpp
        src/Test/ChunkGenerationRegistryTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME stbi_source_envelope_test
    SOURCES src/Test/StbiSourceEnvelopeTest.cpp src/ThirdParty/stb_image.cpp src/ThirdParty/stb_image_write.cpp
    DEFINITIONS CUBA_STBI_SOURCE_ENVELOPE_TEST
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME catalog_json_source_envelope_test
    SOURCES src/Test/CatalogJsonSourceEnvelopeTest.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_biome_features_test
    SOURCES
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Core/Noise.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/ThirdParty/stb_image.cpp
        src/Test/WorldGenBiomeFeaturesTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_structure_slope_gate_test
    SOURCES
        ${WORLDGEN_PIPELINE_TEST_SOURCES}
        src/WorldGen/Features/ObjectPlacementConstraints.cpp
        src/Test/WorldGenStructureSlopeGateTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_scatter_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Objects/ObjectUtil.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/WorldGenScatterTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME worldgen_scatter_block_resolve_test
    SOURCES
        src/Core/Jobs/PipelineAdmission.cpp
        src/Blocks/BlockRegistry.cpp
        src/Blocks/BlockDefinition.cpp
        src/Blocks/BlockDefinitionStorage.cpp
        src/ResourcePacks/BlockMergeRegistry.cpp
        src/ResourcePacks/BlockNameUtil.cpp
        src/ResourcePacks/ResourcePack.cpp
        src/ResourcePacks/TextureOverrides.cpp
        src/ResourcePacks/PlaceholderTextureCache.cpp
        src/Render/Textures/TextureBase.cpp
        src/Render/Textures/TextureCube.cpp
        src/World/Physics/MaterialReactionRulesRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenBlockResolver.cpp
        src/WorldGen/Core/WorldGenRefs.cpp
        src/WorldGen/Core/WorldGenSets.cpp
        src/WorldGen/Core/WorldGenPack.cpp
        src/WorldGen/Core/WorldGenStageMask.cpp
        src/WorldGen/Sampling/BiomeRegistry.cpp
        src/ThirdParty/stb_image.cpp
        src/WorldGen/Core/WorldGenContentPin.cpp
        src/WorldGen/Core/WorldGenContentPinTls.cpp
        src/WorldGen/Features/ObjectFeatureConfig.cpp
        src/Test/WorldGenScatterBlockResolveTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json OpenGL::GL GLEW::GLEW)
add_test(NAME catalog_owner_composition_test
    COMMAND $<TARGET_FILE:worldgen_scatter_block_resolve_test> --catalog-owner-composition
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-owner-composition.json)
set_tests_properties(catalog_owner_composition_test PROPERTIES LABELS "integration;catalog" TIMEOUT 120)
if(CUBATARIUM_MSVC_DESKTOP)
  add_test(NAME pipeline_weak_shell_test
      COMMAND $<TARGET_FILE:worldgen_scatter_block_resolve_test> --pipeline-weak-shells
          ${CMAKE_CURRENT_BINARY_DIR}/pipeline-weak-shell.json)
  set_tests_properties(pipeline_weak_shell_test PROPERTIES LABELS "integration;memory;catalog" TIMEOUT 120)
endif()
add_test(NAME catalog_settings_owner_binding_test
    COMMAND $<TARGET_FILE:worldgen_scatter_block_resolve_test> --settings-owner-binding
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-settings-owner-binding.json)
add_test(NAME catalog_cpu_texture_owner_binding_test
    COMMAND $<TARGET_FILE:worldgen_scatter_block_resolve_test> --texture-owner-binding
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-cpu-texture-owner-binding.json)
add_test(NAME catalog_prefab_owner_binding_test
    COMMAND $<TARGET_FILE:worldgen_sets_source_envelope_test> --prefab-owner-binding
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-prefab-owner-binding.json)
set_tests_properties(catalog_settings_owner_binding_test catalog_cpu_texture_owner_binding_test catalog_prefab_owner_binding_test
    PROPERTIES LABELS "integration;catalog" TIMEOUT 120)

cubatarium_add_test(NAME fluid_tuning_defaults_test
    SOURCES
        src/World/Core/RuntimeTuning.cpp
        src/Test/FluidTuningDefaultsTest.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME fluid_block_resolver_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidBlockResolverTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_flood_service_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidFloodServiceTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_fill_policy_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidFillPolicyTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_kind_resolver_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidKindResolverTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_kind_preset_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/World/Physics/FluidBlockResolver.cpp
        src/Test/FluidKindPresetTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME object_placement_mode_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/App/Platform/Log.cpp
        src/ResourcePacks/BlockNameUtil.cpp
        src/World/Objects/ObjectLibrary.cpp
        src/World/Objects/ObjectUtil.cpp
        src/Test/ObjectPlacementModeTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog)

cubatarium_add_test(NAME fluid_permeable_decor_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Physics/FluidSpreadSystem.cpp
        src/World/Physics/FluidFloodService.cpp
        src/World/Physics/FluidFillPolicy.cpp
        src/World/Physics/FluidTransformSim.cpp
        src/World/Physics/FluidBlockResolver.cpp
        src/World/Physics/FluidUpdateSet.cpp
        src/World/Physics/LiquidDebugTrace.cpp
        src/Test/FluidPermeableDecorTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_permeable_block_flag_test
    SOURCES
        src/Blocks/BlockDefinition.cpp
        src/Test/FluidPermeableBlockFlagTest.cpp
    LIBRARIES nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME fluid_gameplay_fixes_test
    SOURCES
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
        src/Test/FluidGameplayFixesTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME leaf_break_cascade_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/World/Objects/ObjectUtil.cpp
        src/Test/LeafBreakCascadeTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME leaf_break_cascade_test
    COMMAND $<TARGET_FILE:leaf_break_cascade_test>)
set_tests_properties(leaf_break_cascade_test PROPERTIES LABELS "unit;blocks")

set(WORLDGEN_FLUID_CHUNK_TEST_SOURCES
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/GreedyMesher.cpp
    src/Render/Textures/TextureBase.cpp
    src/ResourcePacks/ResourcePack.cpp
    src/ResourcePacks/BlockNameUtil.cpp
    src/ResourcePacks/TextureOverrides.cpp
    src/ThirdParty/stb_image.cpp
    src/World/Chunks/ChunkGenerationToken.cpp
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
    src/WorldGen/Core/WorldGenContentPin.cpp
    src/WorldGen/Core/WorldGenContentPinTls.cpp
    src/WorldGen/Core/IUWorldGenPipeline.cpp
    src/WorldGen/Stages/WorldGenStages.cpp
    src/WorldGen/Stages/MudflowErosion.cpp
    src/WorldGen/Stages/DensityTerrainFill.cpp
    src/WorldGen/Sampling/OverworldHeightSampler.cpp
    src/WorldGen/Sampling/ClimateSampler.cpp
    src/WorldGen/Sampling/TerrainClimateMapper.cpp
    src/WorldGen/Sampling/DensityRouter.cpp
    src/WorldGen/Sampling/DensityFieldSampler.cpp
    src/WorldGen/Sampling/AquiferSampler.cpp
    src/WorldGen/Pipelines/ComposableWorldGenerator.cpp
    src/WorldGen/Pipelines/WorldGenStageRunner.cpp
    src/WorldGen/Pipelines/ColumnGenerationService.cpp
    src/WorldGen/Sampling/BiomeSampler.cpp
    src/WorldGen/Sampling/BiomeRegistry.cpp
    src/WorldGen/Sampling/ColumnSample.cpp
    src/WorldGen/Sampling/CoarseHeightCache.cpp
    src/WorldGen/Features/CaveCarver.cpp
    src/WorldGen/Features/CaveDepthBand.cpp
    src/WorldGen/Features/RavineCarver.cpp
    src/WorldGen/Features/ValleyCarver.cpp
    src/WorldGen/Features/LakeCarver.cpp
    src/WorldGen/Features/OreVeinPlacer.cpp
    src/WorldGen/Features/ObjectFeatureConfig.cpp
    src/WorldGen/Features/ObjectPlacementConstraints.cpp
    src/WorldGen/Features/ObjectFeaturePlacer.cpp
    src/WorldGen/Features/BuiltinWorldGenFeatures.cpp
    src/WorldGen/Core/PipelineChunkPopulator.cpp
    src/WorldGen/Core/ProceduralWorldGenFactory.cpp
    src/App/Platform/Log.cpp
    src/App/Platform/WorkerJobKind.cpp)

cubatarium_add_test(NAME fluid_worldgen_chunk_test
    SOURCES
        ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES}
        src/Test/FluidWorldGenChunkTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog)
if(WIN32)
  target_link_libraries(fluid_worldgen_chunk_test dbghelp)
endif()

if(CUBATARIUM_MSVC_DESKTOP)
cubatarium_add_test(NAME pipeline_retained_storage_test
    SOURCES ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES} src/Test/PipelineRetainedStorageTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog
    WIN32_LIBRARIES dbghelp)
add_test(NAME pipeline_retained_storage_test COMMAND $<TARGET_FILE:pipeline_retained_storage_test>
    ${CMAKE_CURRENT_BINARY_DIR}/pipeline-retained-storage.json)
set_tests_properties(pipeline_retained_storage_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
cubatarium_add_test(NAME runtime_name_storage_test
    SOURCES ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES} src/Test/RuntimeNameStorageTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog
    WIN32_LIBRARIES dbghelp)
add_test(NAME runtime_name_storage_test COMMAND $<TARGET_FILE:runtime_name_storage_test>
    ${CMAKE_CURRENT_BINARY_DIR}/runtime-name-storage.json)
set_tests_properties(runtime_name_storage_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
cubatarium_add_test(NAME engine_thread_storage_test
    SOURCES src/Test/EngineThreadStorageTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME engine_thread_storage_test COMMAND $<TARGET_FILE:engine_thread_storage_test>
    ${CMAKE_CURRENT_BINARY_DIR}/engine-thread-storage.json)
set_tests_properties(engine_thread_storage_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
if(TARGET CubatariumCompileConfig)
  foreach(_storage_spec IN ITEMS "world_owned_storage_test|WorldOwnedStorageTest|world-owned-storage" "member_callback_storage_test|MemberCallbackStorageTest|member-callback-storage" "root_storage_source_test|RootStorageSourceTest|root-storage-source")
    string(REPLACE "|" ";" _storage_fields "${_storage_spec}")
    list(GET _storage_fields 0 _storage_target)
    list(GET _storage_fields 1 _storage_source)
    list(GET _storage_fields 2 _storage_report)
    cubatarium_add_test(NAME ${_storage_target}
      SOURCES src/Test/${_storage_source}.cpp
      LIBRARIES CubatariumCompileConfig
      WIN32_LIBRARIES dbghelp)
  foreach(_world_source_group IN LISTS CUBATARIUM_OBJECT_GROUPS)
    if(_world_source_group STREQUAL "src")
      if(NOT CUBATARIUM_OBJECT_SOURCES_src STREQUAL "src/main.cpp")
        message(FATAL_ERROR "world_owned_storage_test requires explicit handling of new root sources")
      endif()
    else()
      target_sources(${_storage_target} PRIVATE $<TARGET_OBJECTS:CubatariumObjects_${_world_source_group}>)
    endif()
  endforeach()
    add_test(NAME ${_storage_target} COMMAND $<TARGET_FILE:${_storage_target}>
      ${CMAKE_CURRENT_BINARY_DIR}/${_storage_report}.json)
    set_tests_properties(${_storage_target} PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
  endforeach()
endif()
endif()

cubatarium_add_test(NAME worldgen_populate_parity_probe
    SOURCES
        ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES}
        src/Test/WorldGenPopulateParityProbe.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog)
if(WIN32)
  target_link_libraries(worldgen_populate_parity_probe dbghelp)
endif()

cubatarium_add_test(NAME worldgen_sets_source_envelope_test
    SOURCES ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES} src/Test/WorldGenSetsSourceEnvelopeTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog
    WIN32_LIBRARIES dbghelp)

if(CUBATARIUM_MSVC_DESKTOP)
  cubatarium_add_test(NAME allocator_owner_source_test
      SOURCES src/Test/AllocatorOwnerSourceTest.cpp
      LIBRARIES glm::glm nlohmann_json::nlohmann_json)
  add_test(NAME allocator_owner_source_test COMMAND $<TARGET_FILE:allocator_owner_source_test>
      ${CMAKE_CURRENT_BINARY_DIR}/allocator-owner-source.json)
  set_tests_properties(allocator_owner_source_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
  add_test(NAME allocator_owner_census_test COMMAND $<TARGET_FILE:allocator_owner_source_test>
      --census ${CMAKE_CURRENT_BINARY_DIR}/allocator-owner-census.json)
  set_tests_properties(allocator_owner_census_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
  cubatarium_add_test(NAME runtime_inline_source_test
      SOURCES ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES} src/Test/RuntimeInlineSourceTest.cpp
      DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
      LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog
      WIN32_LIBRARIES dbghelp)
  add_test(NAME runtime_inline_source_test COMMAND $<TARGET_FILE:runtime_inline_source_test>
      ${CMAKE_CURRENT_BINARY_DIR}/runtime-inline-source.json)
  set_tests_properties(runtime_inline_source_test PROPERTIES LABELS "integration;memory;runtime" TIMEOUT 120)
endif()

cubatarium_add_test(NAME catalog_worker_retirement_test
    SOURCES ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES}
        src/Core/Jobs/JobThreadPool.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
        src/World/Chunks/ChunkLoadScheduler.cpp
        src/Test/CatalogWorkerRetirementTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog
    WIN32_LIBRARIES dbghelp)
add_test(NAME catalog_runtime_domain_test
    COMMAND $<TARGET_FILE:catalog_worker_retirement_test> --runtime-domain
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-runtime-domain-unit.json)
set_tests_properties(catalog_runtime_domain_test PROPERTIES LABELS "integration;catalog" TIMEOUT 120)
if(CUBATARIUM_MSVC_DESKTOP)
add_test(NAME allocator_owner_graph_test COMMAND $<TARGET_FILE:catalog_worker_retirement_test>
    --allocator-graph ${CMAKE_CURRENT_BINARY_DIR}/allocator-owner-graph.json)
set_tests_properties(allocator_owner_graph_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
add_test(NAME allocator_worker_threads_test COMMAND $<TARGET_FILE:catalog_worker_retirement_test>
    --allocator-workers ${CMAKE_CURRENT_BINARY_DIR}/allocator-worker-threads.json)
set_tests_properties(allocator_worker_threads_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
add_test(NAME allocator_returned_controls_test COMMAND $<TARGET_FILE:catalog_worker_retirement_test>
    --returned-controls ${CMAKE_CURRENT_BINARY_DIR}/allocator-returned-controls.json)
set_tests_properties(allocator_returned_controls_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
add_test(NAME generation_producer_retirement_test COMMAND $<TARGET_FILE:catalog_worker_retirement_test>
    --producer-retirement ${CMAKE_CURRENT_BINARY_DIR}/generation-producer-retirement.json)
set_tests_properties(generation_producer_retirement_test PROPERTIES LABELS "integration;memory;ownership" TIMEOUT 120)
endif()
add_test(NAME scheduler_control_refusal_test
    COMMAND $<TARGET_FILE:catalog_worker_retirement_test> --control-refusal
        ${CMAKE_CURRENT_BINARY_DIR}/scheduler-control-refusal.json)
set_tests_properties(scheduler_control_refusal_test PROPERTIES LABELS "integration;memory;streaming" TIMEOUT 120)
add_test(NAME catalog_cooperative_owners_test
    COMMAND $<TARGET_FILE:catalog_worker_retirement_test> --cooperative-owners
        ${CMAKE_CURRENT_BINARY_DIR}/catalog-cooperative-owners.json)
set_tests_properties(catalog_cooperative_owners_test PROPERTIES LABELS "integration;catalog" TIMEOUT 120)

cubatarium_add_test(NAME worldgen_vegetation_placement_integration_test
    SOURCES
        ${WORLDGEN_FLUID_CHUNK_TEST_SOURCES}
        src/Test/WorldGenVegetationPlacementIntegrationTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json glog::glog)
if(WIN32)
  target_link_libraries(worldgen_vegetation_placement_integration_test dbghelp)
endif()

cubatarium_add_test(NAME physics_integration_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        ${PHYSICS_FLUID_SIM_SOURCES}
        src/Test/PhysicsIntegrationTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME physics_profile_hash_test
    SOURCES
        ${PHYSICS_REPLAY_TEST_SOURCES}
        src/Test/PhysicsProfileHashTest.cpp
    LIBRARIES glm::glm)

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
    src/World/Diagnostics/CreatureTickTrace.cpp
    src/World/Environment/WorldEnvironment.cpp
    src/World/Mesh/WorldMeshService.cpp
    src/World/Streaming/WorldStreaming.cpp
    src/World/Streaming/MemoryBudgetController.cpp
    src/World/Streaming/ChunkEmergeCoordinator.cpp
    src/World/Streaming/ColumnFlowScheduler.cpp
    src/World/Streaming/ColumnFlowExecutor.cpp
    src/World/Streaming/ColumnRecordCoordinator.cpp
    src/World/Streaming/EnterVisualGate.cpp
    src/World/Lighting/CpuLightingSeedBackend.cpp
    src/World/Lighting/GpuLightingSeedBackend.cpp
    src/World/Lighting/LightingSeedBackendFactory.cpp
    src/World/Persistence/WorldPersistence.cpp
    src/World/Core/WorldCooperativeOps.cpp
    src/World/Diagnostics/MovementDiagnosticsRecorder.cpp)

cubatarium_add_test(NAME collision_broadphase_subchunk_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/WorldCollisionCreatureStubs.cpp
        src/Test/CollisionBroadphaseSubchunkTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME collision_broadphase_subchunk_test
    COMMAND $<TARGET_FILE:collision_broadphase_subchunk_test>)
set_tests_properties(collision_broadphase_subchunk_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME fluid_placement_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/FluidPlacementTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME block_placement_raycast_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/WorldCollisionCreatureStubs.cpp
        src/Test/BlockPlacementRaycastTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME block_raycast_target_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/BlockRaycastTargetTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME block_raycast_target_test
    COMMAND $<TARGET_FILE:block_raycast_target_test>)
set_tests_properties(block_raycast_target_test PROPERTIES LABELS "unit;blocks")

cubatarium_add_test(NAME material_reaction_rules_test
    SOURCES
        ${PHYSICS_MATERIAL_UNIT_SOURCES}
        src/Test/MaterialReactionRulesTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)

cubatarium_add_test(NAME movement_integration_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/MovementIntegrationTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME movement_integration_test
    COMMAND $<TARGET_FILE:movement_integration_test>)
set_tests_properties(movement_integration_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME player_clearance_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/PlayerClearanceTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME player_clearance_test COMMAND $<TARGET_FILE:player_clearance_test>)
set_tests_properties(player_clearance_test PROPERTIES LABELS "unit;creatures")

cubatarium_add_test(NAME creature_terrestrial_gate_test
    SOURCES
        ${PHYSICS_COLLISION_UNIT_SOURCES}
        src/Test/CreatureTerrestrialGateTest.cpp
    DEFINITIONS ${PHYSICS_LIGHT_REGISTRY_DEF}
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME creature_terrestrial_gate_test
    COMMAND $<TARGET_FILE:creature_terrestrial_gate_test>)
set_tests_properties(creature_terrestrial_gate_test PROPERTIES LABELS "unit;creatures")

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

if(CUBATARIUM_MSVC_DESKTOP
   OR (WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
       AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU"))
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CUBATARIUM_GENERATED_DIR}
)

if(CUBATARIUM_MSVC_DESKTOP)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CUBATARIUM_GLOG_RESOURCE_SOURCE=1)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE
    OpenGL::GL
    glfw
    glm::glm
    GLEW::GLEW
    Freetype::Freetype
    nlohmann_json::nlohmann_json
    glog::glog
)

if(CUBATARIUM_ENABLE_TRACY)
    target_link_libraries(${PROJECT_NAME} PRIVATE TracyClient)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CUBATARIUM_ENABLE_TRACY)
endif()

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
    # Release (and ship configs) → bin/ for flight-sim + IDE Run.
    # Debug → build tree so Debug/Release never fight over bin/Cubatarium.exe.
    RUNTIME_OUTPUT_DIRECTORY ${CUBATARIUM_RUN_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
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
            -DSYNC_MIRROR=ON
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing models to runtime directory (mirror prune)"
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
            -DSYNC_MIRROR=ON
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Syncing content data to runtime directory (mirror prune)"
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

# Cubatarium AssetGen may remove generated blocks and textures after a catalog
# change. Mirror only its owned pack so deleted assets cannot remain in bin and
# create conflicting block IDs in previously saved worlds; user-installed packs
# beside it stay untouched.
if(EXISTS "${CMAKE_SOURCE_DIR}/resource_packs/cubatarium_worldgen_cc0")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSYNC_SRC="${CMAKE_SOURCE_DIR}/resource_packs/cubatarium_worldgen_cc0"
            -DSYNC_DST="${CUBATARIUM_RUN_DIR}/resource_packs/cubatarium_worldgen_cc0"
            -DSYNC_MIRROR=ON
            -P "${CMAKE_SOURCE_DIR}/cmake/sync_runtime_subdir.cmake"
        COMMENT "Pruning removed generated assets from Cubatarium Worldgen runtime pack"
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

add_custom_target(m4_ownership_lint
    COMMAND python "${CMAKE_SOURCE_DIR}/bin/research_mesh_arch_20260901/scripts/m4_ownership_lint.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "M4 mesh ownership lint")

# CTest registry (M01 / A14): unit/streaming/policy gates must run, not only build.
cubatarium_add_test(NAME job_admission_lifetime_test
    SOURCES
        src/Test/JobAdmissionLifetimeTest.cpp
        src/Core/Jobs/JobThreadPool.cpp
        src/Core/Jobs/JobThreadBudget.cpp
        src/Core/Jobs/PipelineAdmission.cpp
    LIBRARIES Threads::Threads)
cubatarium_add_test(NAME chunk_input_stamp_test
    SOURCES
        src/Test/ChunkInputStampTest.cpp
        src/World/Chunks/Chunk.cpp
        src/World/Math/FluidCellState.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME light_sample_prediction_test
    SOURCES
        src/Test/LightSamplePredictionTest.cpp
        src/World/Math/GridMath.cpp
    LIBRARIES glm::glm)

cubatarium_add_test(NAME inputs_still_valid_visual_test
    SOURCES
        ${PHYSICS_BLOCKWORLD_TEST_SOURCES}
        src/Render/Mesh/ChunkMeshSnapshot.cpp
        src/Test/InputsStillValidVisualTest.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
if(NOT ANDROID AND NOT CUBATARIUM_GLES)
  add_executable(gpu_relight_snapshot_driver_test
    ${PHYSICS_MATERIAL_UNIT_SOURCES}
    src/Blocks/BlockDefinitionStorage.cpp
    src/World/Lighting/LightUtil.cpp
    src/World/Lighting/ChunkRelightSnapshot.cpp
    src/World/Lighting/ChunkLighting.cpp
    src/World/Lighting/FullLightingPipeline.cpp
    src/World/Lighting/GpuLightTransport.cpp
    src/Test/GpuRelightSnapshotDriverTest.cpp)
  target_include_directories(gpu_relight_snapshot_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_compile_definitions(gpu_relight_snapshot_driver_test PRIVATE ${PHYSICS_LIGHT_REGISTRY_DEF})
  target_link_libraries(gpu_relight_snapshot_driver_test PRIVATE
    glm::glm nlohmann_json::nlohmann_json GLEW::GLEW OpenGL::GL glfw)
  if(WIN32)
    target_compile_definitions(gpu_relight_snapshot_driver_test PRIVATE GLEW_STATIC)
  endif()
  add_test(NAME gpu_relight_snapshot_driver_test COMMAND $<TARGET_FILE:gpu_relight_snapshot_driver_test>)
  set_tests_properties(gpu_relight_snapshot_driver_test PROPERTIES
    LABELS "gpu;integration;lighting;streaming" SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_test(NAME gpu_relight_snapshot_gl33_driver_test
    COMMAND $<TARGET_FILE:gpu_relight_snapshot_driver_test> --gl33)
  set_tests_properties(gpu_relight_snapshot_gl33_driver_test PROPERTIES
    LABELS "gpu;integration;lighting;streaming" SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_executable(gpu_light_transport_driver_test
    src/World/Lighting/GpuLightTransport.cpp
    src/Test/GpuLightTransportDriverTest.cpp)
  target_include_directories(gpu_light_transport_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(gpu_light_transport_driver_test PRIVATE GLEW::GLEW OpenGL::GL glfw)
  if(WIN32)
    target_compile_definitions(gpu_light_transport_driver_test PRIVATE GLEW_STATIC)
  endif()
  add_test(NAME gpu_light_transport_driver_test COMMAND $<TARGET_FILE:gpu_light_transport_driver_test>)
  set_tests_properties(gpu_light_transport_driver_test PROPERTIES
    LABELS "gpu;integration;lighting" SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_executable(section_light_atlas_driver_test
    src/Render/Lighting/SectionLightAtlas.cpp
    src/Render/Lighting/SectionLightBindingTable.cpp
    src/Test/SectionLightAtlasDriverTest.cpp)
  target_include_directories(section_light_atlas_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(section_light_atlas_driver_test PRIVATE
    glm::glm GLEW::GLEW OpenGL::GL glfw)
  add_test(NAME section_light_atlas_driver_test
    COMMAND $<TARGET_FILE:section_light_atlas_driver_test>)
  set_tests_properties(section_light_atlas_driver_test PROPERTIES
    LABELS "gpu;integration" SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_test(NAME section_light_atlas_gles_driver_test
    COMMAND $<TARGET_FILE:section_light_atlas_driver_test> --gles)
  set_tests_properties(section_light_atlas_gles_driver_test PROPERTIES
    LABELS "gpu;integration;gles" SKIP_RETURN_CODE 77 TIMEOUT 30)
  if(WIN32)
    target_compile_definitions(section_light_atlas_driver_test PRIVATE GLEW_STATIC)
  endif()
  add_executable(bound_section_light_probe_driver_test
    src/Render/Lighting/SectionLightAtlas.cpp
    src/Render/Lighting/SectionLightBindingTable.cpp
    src/Render/Lighting/SectionLightDrawTable.cpp
    src/Test/BoundSectionLightProbeDriverTest.cpp)
  target_include_directories(bound_section_light_probe_driver_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(bound_section_light_probe_driver_test PRIVATE
    glm::glm GLEW::GLEW OpenGL::GL glfw)
  if(WIN32)
    target_compile_definitions(bound_section_light_probe_driver_test PRIVATE GLEW_STATIC)
  endif()
  add_test(NAME bound_section_light_probe_driver_test COMMAND $<TARGET_FILE:bound_section_light_probe_driver_test>)
  set_tests_properties(bound_section_light_probe_driver_test PROPERTIES LABELS "gpu;integration" SKIP_RETURN_CODE 77 TIMEOUT 30)
  add_executable(greedy_vertex_pool_production_test
    src/Render/Lighting/SectionLightDrawTable.cpp
    src/Test/GreedyVertexPoolProductionTest.cpp
    src/Test/GreedyVertexPoolDriverTest.cpp
    src/Render/Engine/GreedyVertexPool.cpp
    src/Render/Engine/GreedyGpuPublication.cpp
    src/Render/Mesh/ChunkMeshRevisionRegistry.cpp
    src/World/Diagnostics/JobStageTrace.cpp
    src/World/Streaming/ChunkRenderDemand.cpp)
  target_include_directories(greedy_vertex_pool_production_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(greedy_vertex_pool_production_test PRIVATE
    glm::glm GLEW::GLEW OpenGL::GL glog::glog glfw)
  add_test(NAME greedy_vertex_pool_driver_test
    COMMAND $<TARGET_FILE:greedy_vertex_pool_production_test> --driver)
  set_tests_properties(greedy_vertex_pool_driver_test PROPERTIES
    LABELS "gpu;integration" SKIP_RETURN_CODE 77 TIMEOUT 30)
  if(WIN32)
    target_compile_definitions(greedy_vertex_pool_production_test PRIVATE GLEW_STATIC)
  endif()
  # Audit 2026-09-16 publication ownership counterexamples (R01–R04).
  # WILL_FAIL until S1 (R01) + S2 (empty/reorder) land; still executes as stop-criterion.
  add_executable(publication_audit
    src/Render/Lighting/SectionLightDrawTable.cpp
    docs/streaming/audit_2026_09_16/PublicationRepro.cpp
    src/Render/Engine/GreedyVertexPool.cpp
    src/Render/Engine/GreedyGpuPublication.cpp
    src/Render/Mesh/ChunkMeshRevisionRegistry.cpp
    src/World/Diagnostics/JobStageTrace.cpp
    src/World/Streaming/ChunkRenderDemand.cpp)
  target_include_directories(publication_audit PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(publication_audit PRIVATE
    glm::glm GLEW::GLEW OpenGL::GL glog::glog glfw)
  if(WIN32)
    target_compile_definitions(publication_audit PRIVATE GLEW_STATIC)
  endif()
  add_test(NAME publication_audit COMMAND $<TARGET_FILE:publication_audit>)
  set_tests_properties(publication_audit PROPERTIES
    LABELS "unit;streaming;audit" TIMEOUT 30)

  # A21: CurrentContractRepro — remaining audit counters after P1/P2.
  add_executable(current_contract_repro
    docs/streaming/audit_2026_09_21/CurrentContractRepro.cpp
    src/World/Core/BlockWorld.cpp
    src/World/Core/IUBlockWriter.cpp
    src/World/Chunks/ChunkManager.cpp
    src/World/Chunks/Chunk.cpp
    src/World/Chunks/ChunkBuffer.cpp
    src/World/Math/GridMath.cpp
    src/World/Math/FluidCellState.cpp
    src/Blocks/BlockDefinitionStorage.cpp
    src/Blocks/BlockDefinition.cpp
    src/Core/Jobs/PipelineAdmission.cpp
    src/Render/Mesh/ChunkMeshSnapshot.cpp
    src/Render/Mesh/MeshCaptureStore.cpp)
  target_include_directories(current_contract_repro PRIVATE ${CMAKE_SOURCE_DIR}/src)
  target_link_libraries(current_contract_repro PRIVATE glm::glm)
  add_test(NAME current_contract_repro COMMAND $<TARGET_FILE:current_contract_repro>)
  set_tests_properties(current_contract_repro PROPERTIES
    LABELS "unit;streaming;audit;a21"
    TIMEOUT 60)
endif()
set(CUBATARIUM_CTEST_UNIT_TARGETS
  completed_job_queue_test
  view_ray_math_test
  environment_config_celestial_test
  weather_front_field_test
  fluid_chunk_io_test
  worldgen_biome_features_test
  fluid_worldgen_seal_test
  worldgen_aquifer_test
  job_admission_lifetime_test
  chunk_input_stamp_test
  inputs_still_valid_visual_test
  visible_black_attribution_test
  nearest_visible_slice_test
  draw_oracle_gate_test
  block_catalog_queries_test
  mesh_inputs_catalog_test
  normal_shutdown_test
  frame_deadline_test
  streamer_deadline_policy_test
  streamer_amortize_policy_test
  sticky_overlay_remesh_policy_test
  greedy_vertex_pool_production_test
  column_flow_scheduler_test
  frustum_clip_test
  column_scheduler_audit_repro
  streaming_render_ready_invariants_test
  miss_first_mesh_class_test
  mesh_publish_contract_test
  mesh_gpu_store_mdi_test
  greedy_vertex_pool_lifetime_test
  capture_token_validation_test
  cruise_sot_phase_test
  decor_mesh_integration_test
  relight_result_install_test
  required_data_coverage_test
  chunk_streamer_committed_column_test
  mesh_neighbor_policy_test
  mesh_light_stale_policy_test
  column_emerge_bump_test
  frame_streaming_budget_test
  idle_recovery_policy_test
  chunk_render_demand_test
  packed_quad_parity_test
  seam_absent_neighbor_test
  fog_range_smoother_test
  gpu_extract_mesher_parity_test
)
foreach(_t IN LISTS CUBATARIUM_CTEST_UNIT_TARGETS)
  if(TARGET ${_t})
    add_test(NAME ${_t} COMMAND $<TARGET_FILE:${_t}>)
    set_tests_properties(${_t} PROPERTIES LABELS "unit;streaming")
  endif()
endforeach()

# Auto-register every remaining *_test executable so a single `ctest` runs the
# whole unit suite (tests above keep their unit;streaming labels; explicitly
# registered GPU/audit tests keep theirs). CI's `ctest -L streaming` selection
# is unaffected: these carry the plain "unit" label only.
get_property(CUBATARIUM_REGISTERED_TESTS DIRECTORY PROPERTY TESTS)
get_property(CUBATARIUM_ALL_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS CUBATARIUM_ALL_TARGETS)
  if(NOT _t MATCHES "_test$")
    continue()
  endif()
  if(_t IN_LIST CUBATARIUM_REGISTERED_TESTS)
    continue()
  endif()
  # Registered separately as a --driver invocation with its own GPU gating.
  if(_t STREQUAL "greedy_vertex_pool_production_test")
    continue()
  endif()
  add_test(NAME ${_t} COMMAND $<TARGET_FILE:${_t}>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(${_t} PROPERTIES LABELS "unit" TIMEOUT 300)
endforeach()

# Keep the lightweight test source sets linkable as the production chunk
# sources acquire registry lookups and diagnostics. Compile these support
# implementations once instead of copying test-only stubs into each target.
add_library(physics_block_registry_test_support OBJECT
  src/Blocks/BlockRegistry.cpp
  src/World/Physics/MaterialReactionRulesRegistry.cpp)
target_include_directories(physics_block_registry_test_support PRIVATE
  ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(physics_block_registry_test_support PRIVATE
  ${PHYSICS_LIGHT_REGISTRY_DEF})
target_link_libraries(physics_block_registry_test_support PUBLIC glm::glm)

add_library(platform_log_test_support OBJECT
  src/App/Platform/Log.cpp)
target_include_directories(platform_log_test_support PRIVATE
  ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(platform_log_test_support PUBLIC glog::glog)
if(WIN32)
  target_link_libraries(platform_log_test_support PUBLIC dbghelp)
endif()

add_library(platform_worker_job_kind_test_support OBJECT
  src/App/Platform/WorkerJobKind.cpp)
target_include_directories(platform_worker_job_kind_test_support PRIVATE
  ${CMAKE_SOURCE_DIR}/src)

get_property(_cutum_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_test_target IN LISTS _cutum_directory_targets)
  if(_test_target STREQUAL PROJECT_NAME OR _test_target MATCHES "^CubatariumObjects_")
    continue()
  endif()
  get_target_property(_test_sources ${_test_target} SOURCES)
  if(NOT _test_sources)
    continue()
  endif()

  set(_has_chunk_buffer FALSE)
  set(_has_block_registry FALSE)
  set(_has_chunk_mesh_snapshot FALSE)
  set(_has_chunk_lighting FALSE)
  set(_has_chunk_lighting_stubs FALSE)
  set(_has_world_collision FALSE)
  set(_has_world_collision_creature_stubs FALSE)
  set(_has_platform_log FALSE)
  set(_has_worker_job_kind FALSE)
  set(_has_test_log_stub FALSE)
  set(_has_job_stage_trace_stub FALSE)
  foreach(_source IN LISTS _test_sources)
    get_filename_component(_source_name "${_source}" NAME)
    if(_source_name STREQUAL "ChunkBuffer.cpp")
      set(_has_chunk_buffer TRUE)
    elseif(_source_name STREQUAL "BlockRegistry.cpp")
      set(_has_block_registry TRUE)
    elseif(_source_name STREQUAL "ChunkMeshSnapshot.cpp")
      set(_has_chunk_mesh_snapshot TRUE)
    elseif(_source_name STREQUAL "ChunkLighting.cpp")
      set(_has_chunk_lighting TRUE)
    elseif(_source_name STREQUAL "ChunkLightingTestStubs.cpp")
      set(_has_chunk_lighting_stubs TRUE)
    elseif(_source_name STREQUAL "WorldCollision.cpp")
      set(_has_world_collision TRUE)
    elseif(_source_name STREQUAL "WorldCollisionCreatureStubs.cpp")
      set(_has_world_collision_creature_stubs TRUE)
    elseif(_source_name STREQUAL "Log.cpp")
      set(_has_platform_log TRUE)
    elseif(_source_name STREQUAL "WorkerJobKind.cpp")
      set(_has_worker_job_kind TRUE)
    endif()
    if(_source_name MATCHES "Test\\.cpp$")
      if(IS_ABSOLUTE "${_source}")
        set(_source_path "${_source}")
      else()
        set(_source_path "${CMAKE_SOURCE_DIR}/${_source}")
      endif()
      if(EXISTS "${_source_path}")
        file(STRINGS "${_source_path}" _logger_stub_lines
          REGEX "void[ \t]+CubatariumLogInfo\\(")
        if(_logger_stub_lines)
          set(_has_test_log_stub TRUE)
        endif()
        file(STRINGS "${_source_path}" _job_stage_trace_stub_lines
          REGEX "UJobStageTrace::GetStreamingFrameEpoch\\(")
        if(_job_stage_trace_stub_lines)
          set(_has_job_stage_trace_stub TRUE)
        endif()
      endif()
    endif()
  endforeach()

  if(_has_chunk_buffer AND NOT _has_block_registry)
    target_sources(${_test_target} PRIVATE
      $<TARGET_OBJECTS:physics_block_registry_test_support>)
  endif()
  if(_has_world_collision AND NOT _has_world_collision_creature_stubs)
    target_sources(${_test_target} PRIVATE
      src/Test/WorldCollisionCreatureStubs.cpp)
  endif()
  if(_has_chunk_lighting AND NOT _has_chunk_lighting_stubs
      AND NOT _has_job_stage_trace_stub)
    target_sources(${_test_target} PRIVATE
      src/Test/ChunkLightingTestStubs.cpp)
  endif()
  set(_needs_platform_log FALSE)
  if(_has_chunk_mesh_snapshot AND NOT _has_platform_log
      AND NOT _has_test_log_stub)
    target_sources(${_test_target} PRIVATE
      $<TARGET_OBJECTS:platform_log_test_support>)
    set_property(TARGET ${_test_target} APPEND PROPERTY LINK_LIBRARIES
      glog::glog)
    if(WIN32)
      set_property(TARGET ${_test_target} APPEND PROPERTY LINK_LIBRARIES
        dbghelp)
    endif()
    set(_needs_platform_log TRUE)
  elseif(_has_platform_log)
    set(_needs_platform_log TRUE)
  endif()
  if(_needs_platform_log
      AND NOT _has_worker_job_kind)
    target_sources(${_test_target} PRIVATE
      $<TARGET_OBJECTS:platform_worker_job_kind_test_support>)
  endif()
endforeach()

cubatarium_add_test(NAME human_motion_camera_test
    SOURCES src/Test/HumanMotionCameraTest.cpp
            src/Items/ItemRenderAsset.cpp
            src/Items/ItemVisualState.cpp
            src/Creatures/Visual/BoneSkeleton/BoneSkeletonCubeMeshBuilder.cpp
            src/Creatures/Visual/BoneSkeleton/CreatureBoneSkeletonLoader.cpp
            src/Creatures/Visual/BoneSkeleton/BoneSkeletonHierarchy.cpp
    LIBRARIES glm::glm nlohmann_json::nlohmann_json)
add_test(NAME human_motion_camera_test COMMAND $<TARGET_FILE:human_motion_camera_test>
    "${CMAKE_SOURCE_DIR}/models/creatures/human/appearance/male_short.geo.json")
set_tests_properties(human_motion_camera_test PROPERTIES LABELS "unit;player;camera;items")

# Slot/economy regressions are required in Release CI, not merely built.
foreach(_item_test inventory_transactions_test armor_equipment_test resource_economy_test
        recipe_registry_test inventory_mode_test item_survival_hooks_test)
    if(NOT TEST ${_item_test})
        add_test(NAME ${_item_test} COMMAND $<TARGET_FILE:${_item_test}>)
    endif()
    set_tests_properties(${_item_test} PROPERTIES LABELS "unit;items")
endforeach()

if(NOT ANDROID AND TARGET glfw AND TARGET GLEW::GLEW)
  cubatarium_add_test(NAME texture_publication_driver_test
    SOURCES
      src/Render/Textures/TextureBase.cpp
      src/Render/Textures/TextureCube.cpp
      src/Blocks/BlockDefinition.cpp
      src/Blocks/BlockDefinitionStorage.cpp
      src/World/Physics/MaterialReactionRulesRegistry.cpp
      src/ThirdParty/stb_image.cpp
      src/Test/TexturePublicationDriverTest.cpp
    DEFINITIONS CUBA_TEXTURE_PUBLICATION_TEST=1
    LIBRARIES glm::glm nlohmann_json::nlohmann_json GLEW::GLEW OpenGL::GL glfw)
  if(WIN32)
    target_compile_definitions(texture_publication_driver_test PRIVATE GLEW_STATIC)
  endif()
  add_test(NAME texture_publication_driver_test COMMAND $<TARGET_FILE:texture_publication_driver_test>)
  add_test(NAME catalog_gpu_texture_owner_binding_test COMMAND $<TARGET_FILE:texture_publication_driver_test>
      ${CMAKE_CURRENT_BINARY_DIR}/catalog-gpu-texture-owner-binding.json)
  set_tests_properties(catalog_gpu_texture_owner_binding_test PROPERTIES LABELS "gpu;integration;catalog" SKIP_RETURN_CODE 77 TIMEOUT 120)
  set_tests_properties(texture_publication_driver_test PROPERTIES LABELS "gpu;integration;texture;memory" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
