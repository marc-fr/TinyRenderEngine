cmake_minimum_required(VERSION 3.9)
project(TinyRenderEngine CXX)

#
# This project works both as stand-alone and included as a sub-project.
#

set(CMAKE_CXX_STANDARD 17)

## Main target

set(SRC_LIST_LIB
  src/audio.cpp
  src/baker.cpp
  src/contact_2D.cpp
  src/contact_3D.cpp
  src/font.cpp
  src/gizmo.cpp
  src/model.cpp
  src/model_importer.cpp
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
  src/windowContext.cpp
)
add_library(${PROJECT_NAME} STATIC ${SRC_LIST_LIB})

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
  set(CMAKE_PREFIX_PATH "C:/Users/Marc/Desktop/Programmes/3rdParties")
  find_package(GLEW   REQUIRED )
  target_include_directories(${PROJECT_NAME} PUBLIC ${GLEW_INCLUDE_DIR})
endif()

find_package(SDL2       REQUIRED)
find_package(SDL2_image )

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL     REQUIRED)

find_package(Freetype   )
find_package(TIFF       )
find_package(Opus       )

target_include_directories(${PROJECT_NAME} PUBLIC ${SDL2_INCLUDE_DIR})

option(ENABLE_SDL2_IMAGE "Build and link with SDL2_Image when possible" ON)
if (ENABLE_SDL2_IMAGE AND SDL2_IMAGE_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_SDL2_IMAGE)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_IMAGE_INCLUDE_DIR})
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
  target_include_directories(${PROJECT_NAME} PUBLIC ${TIFF_INCLUDE_DIR})
else()
  unset(TIFF_LIBRARIES)
endif()

option(ENABLE_OPUS "Build and link with Opus when possible" ON)
if (ENABLE_OPUS AND OPUS_FOUND)
  target_compile_definitions(${PROJECT_NAME} PUBLIC TRE_WITH_OPUS)
  target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIR})
else()
  unset(OPUS_LIBRARIES)
endif()

## Export library configuration

get_directory_property(hasParent PARENT_DIRECTORY)
if(hasParent)
  message(STATUS "${PROJECT_NAME}: included from another project, skip testing.")
  set(${PROJECT_NAME}_LIBRARY ${PROJECT_NAME} ${OPENGL_LIBRARY} ${GLEW_SHARED_LIBRARY_RELEASE} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES} PARENT_SCOPE)
  message(STATUS "${PROJECT_NAME}_LIBRARY = ${PROJECT_NAME} ${OPENGL_LIBRARY} ${GLEW_SHARED_LIBRARY_RELEASE} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES}")
else()
  add_subdirectory(test)
endif()
