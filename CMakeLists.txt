cmake_minimum_required(VERSION 3.16)
project(Mythbound LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

# --- Vulkan headers (we load every Vulkan function ourselves at runtime, so no
#     Vulkan SDK or import library is needed -- only the headers). ------------
find_path(VULKAN_HEADER_DIR vulkan/vulkan.h HINTS $ENV{VULKAN_SDK}/include)
if(NOT VULKAN_HEADER_DIR)
  message(STATUS "Vulkan headers not found locally - fetching Vulkan-Headers")
  FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.3.275)
  FetchContent_MakeAvailable(vulkan_headers)
  set(VULKAN_HEADER_DIR ${vulkan_headers_SOURCE_DIR}/include)
endif()

# --- GLFW (window + input only) ---------------------------------------------
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  message(STATUS "GLFW not found locally - fetching GLFW")
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.10)
  FetchContent_MakeAvailable(glfw)
endif()

# --- Shaders: GLSL -> SPIR-V headers. The compiled headers are committed in
#     shaders/spv so the game builds even without glslangValidator. -----------
set(SHADER_NAMES lit.vert lit.frag shadow.vert water.vert water.frag sky.vert sky.frag ui.vert ui.frag)
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin)
set(SHADER_HEADERS)
foreach(S ${SHADER_NAMES})
  string(REPLACE "." "_" VAR ${S})
  set(OUT ${CMAKE_SOURCE_DIR}/shaders/spv/${VAR}.h)
  if(GLSLANG_VALIDATOR)
    add_custom_command(
      OUTPUT ${OUT}
      COMMAND ${GLSLANG_VALIDATOR} -V --vn spv_${VAR} -o ${OUT} ${CMAKE_SOURCE_DIR}/shaders/${S}
      DEPENDS ${CMAKE_SOURCE_DIR}/shaders/${S} ${CMAKE_SOURCE_DIR}/shaders/scene_ubo.glsl ${CMAKE_SOURCE_DIR}/shaders/common_light.glsl
      COMMENT "Compiling shader ${S}")
  endif()
  list(APPEND SHADER_HEADERS ${OUT})
endforeach()
add_custom_target(shaders DEPENDS ${SHADER_HEADERS})

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(mythbound ${SOURCES})
add_dependencies(mythbound shaders)
target_include_directories(mythbound PRIVATE ${VULKAN_HEADER_DIR} ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/shaders/spv)
target_link_libraries(mythbound PRIVATE glfw)
target_compile_definitions(mythbound PRIVATE VK_NO_PROTOTYPES GLFW_INCLUDE_NONE)

if(MSVC)
  target_compile_options(mythbound PRIVATE /W3 /O2 /fp:fast)
  set_property(TARGET mythbound PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
else()
  target_compile_options(mythbound PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(mythbound PRIVATE -O3 -ffast-math)
  endif()
  if(UNIX AND NOT APPLE)
    target_link_libraries(mythbound PRIVATE dl pthread)
  endif()
endif()
