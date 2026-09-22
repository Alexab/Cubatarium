cmake_minimum_required(VERSION 3.21)
project(Audit20260921 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DEPS "${REPO}/build/desktop-msvc/vcpkg_installed/x64-windows-static" CACHE PATH "Existing dependencies")
set(SOURCES World/Core/BlockWorld.cpp World/Core/IUBlockWriter.cpp
  World/Chunks/ChunkManager.cpp World/Chunks/Chunk.cpp World/Chunks/ChunkBuffer.cpp
  World/Math/GridMath.cpp World/Math/FluidCellState.cpp
  Blocks/BlockDefinitionStorage.cpp Blocks/BlockDefinition.cpp
  Core/Jobs/PipelineAdmission.cpp Render/Mesh/ChunkMeshSnapshot.cpp
  Render/Mesh/MeshCaptureStore.cpp)
list(TRANSFORM SOURCES PREPEND "${REPO}/src/")
add_executable(current_contract_repro CurrentContractRepro.cpp ${SOURCES})
target_include_directories(current_contract_repro PRIVATE "${REPO}/src" "${DEPS}/include")
if(MSVC)
  target_compile_options(current_contract_repro PRIVATE /EHsc /utf-8)
endif()
