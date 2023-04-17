## Setup

set(LINK_LIB_LIST ${PROJECT_NAME} ${GLEW_LIBRARY} ${OPENGL_LIBRARY} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES})
message(STATUS "${PROJECT_NAME} : LINK_LIB_LIST is ${LINK_LIB_LIST}")

add_definitions(-DTESTIMPORTPATH="${CMAKE_SOURCE_DIR}/test/")

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

## Add console-tests

add_executable(testIO testIO.cpp)
target_link_libraries(testIO ${LINK_LIB_LIST})

add_executable(testBenchmark testBenchmark.cpp)
target_link_libraries(testBenchmark ${LINK_LIB_LIST})

add_executable(testTextureSampling testTextureSampling.cpp)
target_link_libraries(testTextureSampling ${LINK_LIB_LIST})

## Add graphic-tests

add_executable(testAudioMixer WIN32 testAudioMixer.cpp)
target_link_libraries(testAudioMixer ${LINK_LIB_LIST})

add_executable(testBasic WIN32 testBasic.cpp)
target_link_libraries(testBasic ${LINK_LIB_LIST})

add_executable(testDemoScene WIN32 testDemoScene.cpp)
target_link_libraries(testDemoScene ${LINK_LIB_LIST})

add_executable(testGui WIN32 testGui.cpp)
target_link_libraries(testGui ${LINK_LIB_LIST})

add_executable(testMesh2D WIN32 testMesh2D.cpp)
target_link_libraries(testMesh2D ${LINK_LIB_LIST})

if(NOT ENABLE_OPENGL_ES)
add_executable(testMesh3D WIN32 testMesh3D.cpp)
target_link_libraries(testMesh3D ${LINK_LIB_LIST})
endif()

add_executable(testContact2D WIN32 testContact2D.cpp)
target_link_libraries(testContact2D ${LINK_LIB_LIST})

add_executable(testContact3D WIN32 testContact3D.cpp)
target_link_libraries(testContact3D ${LINK_LIB_LIST})

add_executable(testLighting WIN32 testLighting.cpp)
target_link_libraries(testLighting ${LINK_LIB_LIST})

add_executable(testTextureCompression WIN32 testTextureCompression.cpp)
target_link_libraries(testTextureCompression ${LINK_LIB_LIST})

