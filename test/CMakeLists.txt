## Setup

set(LINK_LIB_LIST ${PROJECT_NAME} ${GLEW_LIBRARY} ${OPENGL_LIBRARY} ${SDL2_LIBRARY} ${SDL2_IMAGE_LIBRARY} ${FREETYPE_LIBRARIES} ${TIFF_LIBRARIES} ${OPUS_LIBRARIES})
message(STATUS "${PROJECT_NAME} : LINK_LIB_LIST is ${LINK_LIB_LIST}")

add_definitions(-DTESTIMPORTPATH="${CMAKE_SOURCE_DIR}/test/")

include_directories(${CMAKE_SOURCE_DIR})

## Add console-tests

add_executable(testIO testIO.cpp)
target_link_libraries(testIO ${LINK_LIB_LIST})

add_executable(testBenchmark testBenchmark.cpp)
target_link_libraries(testBenchmark ${LINK_LIB_LIST})

add_executable(testTextureSampling testTextureSampling.cpp)
target_link_libraries(testTextureSampling ${LINK_LIB_LIST})

## Add graphic-tests

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testAudioMixer WIN32 testAudioMixer.cpp)
else()
add_executable(testAudioMixer testAudioMixer.cpp)
endif()
target_link_libraries(testAudioMixer ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testBasic WIN32 testBasic.cpp)
else()
add_executable(testBasic testBasic.cpp)
endif()
target_link_libraries(testBasic ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testDemoScene WIN32 testDemoScene.cpp)
else()
add_executable(testDemoScene testDemoScene.cpp)
endif()
target_link_libraries(testDemoScene ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testGui WIN32 testGui.cpp)
else()
add_executable(testGui testGui.cpp)
endif()
target_link_libraries(testGui ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testMesh2D WIN32 testMesh2D.cpp)
else()
add_executable(testMesh2D testMesh2D.cpp)
endif()
target_link_libraries(testMesh2D ${LINK_LIB_LIST})

if(NOT ENABLE_OPENGL_ES)
if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testMesh3D WIN32 testMesh3D.cpp)
else()
add_executable(testMesh3D testMesh3D.cpp)
endif()
target_link_libraries(testMesh3D ${LINK_LIB_LIST})
endif()

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testContact2D WIN32 testContact2D.cpp)
else()
add_executable(testContact2D testContact2D.cpp)
endif()
target_link_libraries(testContact2D ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testContact3D WIN32 testContact3D.cpp)
else()
add_executable(testContact3D testContact3D.cpp)
endif()
target_link_libraries(testContact3D ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testLighting WIN32 testLighting.cpp)
else()
add_executable(testLighting testLighting.cpp)
endif()
target_link_libraries(testLighting ${LINK_LIB_LIST})

if(WIN32 AND ${CMAKE_BUILD_TYPE} MATCHES Rel)
add_executable(testTextureCompression WIN32 testTextureCompression.cpp)
else()
add_executable(testTextureCompression testTextureCompression.cpp)
endif()
target_link_libraries(testTextureCompression ${LINK_LIB_LIST})
