project(TinyRenderEngine CXX)
cmake_minimum_required(VERSION 3.1)

# set(CMAKE_VERBOSE_MAKEFILE ON) # Debug

#
# This project works both as stand-alone and included as a sub-project.
#

set(CMAKE_CXX_STANDARD 11)

## Main target

set(SRC_LIST_LIB
  src/audio.cpp
  src/baker.cpp
  src/contact_2D.cpp
  src/contact_3D.cpp
  src/font.cpp
  src/gizmo.cpp
  src/model.cpp
  src/model_base.cpp
  src/model_generator.cpp
  src/model_layout.cpp
  src/model_tools2D.cpp
  src/model_tools3D.cpp
  src/profiler.cpp
  src/rendertarget.cpp
  src/shader.cpp
  src/shadergenerator.cpp
  src/textgenerator.cpp
  src/texture.cpp
  src/texture_compress.cpp
  src/textureSampler.cpp
  src/ui.cpp
  src/ui_layout.cpp
  src/ui_widget.cpp
  src/ui_window.cpp
  src/utils.cpp
  src/windowHelper.cpp
)
add_library(${PROJECT_NAME} STATIC ${SRC_LIST_LIB})

## Architecture

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-msse4.1>$<$<CONFIG:RelWithDebInfo>:-msse4.1>)
  message(STATUS "${PROJECT_NAME}: Compiler GNU: target-architecture in release is SSE4.1.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Intel")
  target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/QxAVX>$<$<CONFIG:RelWithDebInfo>:/QxAVX>)
  message(STATUS "${PROJECT_NAME}: Compiler Intel: target-architecture in release is AVX.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/arch:SSE2>$<$<CONFIG:RelWithDebInfo>:/arch:SSE2>)
  message(STATUS "${PROJECT_NAME}: Compiler MSVC: target-architecture in release is SSE2.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:-msse4.1>$<$<CONFIG:RelWithDebInfo>:-msse4.1>)
  message(STATUS "${PROJECT_NAME}: Compiler Clang: target-architecture in release is SSE4.1.")
else()
  message(WARNING "${PROJECT_NAME}: Compiler Id Unknown: target-architecture is not set.")
endif()

## Build config (Debug, RelWithDebInfo, Release)

target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<CONFIG:Debug>:TRE_DEBUG;TRE_PRINTS;TRE_PROFILE>)
target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<CONFIG:RelWithDebInfo>:TRE_PRINTS;TRE_PROFILE>)

## Options

option(ENABLE_OPENGL_ES  "Build and link with OpenGL ES 3" OFF)
if (ENABLE_OPENGL_ES)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_OPENGL_ES)
endif()

option(ENABLE_EMSCRIPTEN "Build with Emscripten tookkit"   OFF)
if (ENABLE_EMSCRIPTEN)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_EMSCRIPTEN)
endif()

## Dependencies

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/glm)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (WIN32)
  if (${CMAKE_GENERATOR} MATCHES Visual)
    set(CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/vs2017)
    find_package(GLEW   REQUIRED)
    target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vs2017/include)
  else()
    set(GLEW_LIBRARY "glew32.lib")
  endif()
endif()

find_package(SDL2       REQUIRED)
find_package(SDL2_image )
find_package(OpenGL     REQUIRED)
find_package(Freetype   )
find_package(TIFF       )
find_package(Opus       )

target_include_directories(${PROJECT_NAME} PUBLIC ${SDL2_INCLUDE_DIR})

option(ENABLE_SDL2_IMAGE "Build and link with SDL2_Image when possible" ON)
if (ENABLE_SDL2_IMAGE AND SDL2_IMAGE_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_SDL2_IMAGE)
else()
  unset(SDL2_IMAGE_LIBRARY)
endif()

option(ENABLE_FREETYPE "Build and link with Freetype when possible" ON)
if (ENABLE_FREETYPE AND FREETYPE_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_FREETYPE)
  target_include_directories(${PROJECT_NAME} PRIVATE ${FREETYPE_INCLUDE_DIRS})
else()
  unset(FREETYPE_LIBRARIES)
endif()

option(ENABLE_TIFF "Build and link with libTiff when possible" ON)
if (ENABLE_TIFF AND TIFF_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_LIBTIFF)
  target_include_directories(${PROJECT_NAME} PUBLIC ${TIFF_INCLUDE_DIRS})
else()
  unset(TIFF_LIBRARIES)
endif()

option(ENABLE_OPUS "Build and link with Opus when possible" ON)
if (ENABLE_OPUS AND OPUS_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_OPUS)
  target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
else()
  unset(OPUS_LIBRARIES)
endif()

## Export library configuration

get_directory_property(hasParent PARENT_DIRECTORY)
if(hasParent)
  message(STATUS "${PROJECT_NAME}: included from another project, skip testing.")
  set(${PROJECT_NAME}_LIBRARY ${PROJECT_NAME} ${GLEW_LIBRARY} ${OPENGL_LIBRARY} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES} PARENT_SCOPE)
  message(STATUS "${PROJECT_NAME}_LIBRARY = ${PROJECT_NAME} ${GLEW_LIBRARY} ${OPENGL_LIBRARY} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES}")
else()
  add_subdirectory(test)
endif()
