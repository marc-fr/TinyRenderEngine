## Setup

set(LINK_LIB_LIST ${PROJECT_NAME} ${OPENGL_LIBRARY} ${GLEW_SHARED_LIBRARY_RELEASE} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES})
message(STATUS "${PROJECT_NAME} : LINK_LIB_LIST is ${LINK_LIB_LIST}")

add_definitions(-DTESTIMPORTPATH=\"${CMAKE_SOURCE_DIR}/\")

include_directories(${CMAKE_SOURCE_DIR})

## Architecture

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "GNU")
  target_compile_options(${PROJECT_NAME} PUBLIC -msse4.1)
  add_compile_options(-msse4.1)
  message(STATUS "${PROJECT_NAME}: Compiler GNU: target-architecture is SSE4.1.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Intel")
  target_compile_options(${PROJECT_NAME} PUBLIC /QxAVX)
  add_compile_options(/QxAVX)
  message(STATUS "${PROJECT_NAME}: Compiler Intel: target-architecture is AVX.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "MSVC")
  target_compile_options(${PROJECT_NAME} PUBLIC /arch:AVX)
  add_compile_options(/arch:AVX)
  message(STATUS "${PROJECT_NAME}: Compiler MSVC: target-architecture is AVX.")
elseif("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  target_compile_options(${PROJECT_NAME} PUBLIC -msse4.1)
  add_compile_options(-msse4.1)
  message(STATUS "${PROJECT_NAME}: Compiler Clang: target-architecture is SSE4.1.")
else()
  message(WARNING "${PROJECT_NAME}: Compiler Id Unknown: target-architecture is not set.")
endif()

## Add executables

add_executable(meshViewer WIN32 meshViewer.cpp)
target_link_libraries(meshViewer ${LINK_LIB_LIST})

add_executable(tickCompanion WIN32 tickCompanion.cpp)
target_link_libraries(tickCompanion ${LINK_LIB_LIST})
