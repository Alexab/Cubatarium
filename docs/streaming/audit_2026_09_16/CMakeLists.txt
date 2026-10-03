cmake_minimum_required(VERSION 3.21)
project(CurrentPublicationAudit LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DEPS "${REPO}/build/desktop-msvc/vcpkg_installed/x64-windows-static" CACHE PATH "Existing static vcpkg installation")
add_executable(publication_audit PublicationRepro.cpp
  "${REPO}/src/Render/Engine/GreedyVertexPool.cpp"
  "${REPO}/src/Render/Engine/GreedyGpuPublication.cpp")
target_include_directories(publication_audit PRIVATE "${REPO}/src" "${DEPS}/include")
target_compile_definitions(publication_audit PRIVATE GLEW_STATIC GLOG_USE_GLOG_EXPORT GLOG_USE_GFLAGS GLOG_NO_ABBREVIATED_SEVERITIES GFLAGS_IS_A_DLL=0)
target_compile_options(publication_audit PRIVATE /EHsc /utf-8)
target_link_libraries(publication_audit PRIVATE "${DEPS}/lib/libglew32.lib" "${DEPS}/lib/glog.lib" "${DEPS}/lib/gflags_static.lib" opengl32 dbghelp shlwapi)
