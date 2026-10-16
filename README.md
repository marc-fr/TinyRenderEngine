# TinyRenderEngine

TinyRenderEngine is a lightweight C++ toolkit for building games and game engines.
It provides ready-to-use building blocks (mesh operations, contact algorithms, materials, a GUI, audio and more)
so that game programmers can get prototypes up and running quickly.

The library favors simplicity: it is easy to embed in an existing project and easy to use.

- **Language:** C++17, using the STL
- **Third-party dependencies:** kept to a minimum (see [Dependencies](#dependencies))
- **Platforms:**
  - Desktop with OpenGL 3.3 (primarily Windows and Linux)
  - WebAssembly, via [Emscripten](https://emscripten.org/) and OpenGL ES 3.0 (WebGL 2)

## Getting started

No specific build process is required. You can either:

- **build it as a library** (static or shared) using the included CMake project, which also works when added as a sub-project of your own CMake build, or
- **add the source files directly** to your existing project.

Sample programs are available in the [`tests`](./tests) directory, and additional tools in the [`tools`](./tools) directory.

## Features

### Contact algorithms (2D and 3D)

The contact algorithms test for intersection between two objects.
When the objects overlap, they can also return the barycenter of the interpenetration region,
along with the minimal translation needed to separate the objects.

### Mesh operations

TinyRenderEngine includes a light wrapper around OpenGL that manages GPU buffer handles and encapsulates draw calls.

It also implements several mesh-processing algorithms. These are intended for pre-processing, not for real-time use:

- bounding-box computation
- barycenter, area (2D) and volume (3D) computation
- convex hull computation (**WIP**)
- simplification by reducing the number of vertices (**WIP**)
- triangulation (2D) and tetrahedralization (3D)

### Texture utilities

Handling textures can be tricky when dealing with formats and compression across OpenGL versions.
TinyRenderEngine wraps the `glTexImage2D` calls to take care of these details.

Texture compression algorithms are also implemented, even though OpenGL drivers can perform compression on desktop platforms.
The implementation is intentionally simple and has fewer features than reference implementations such as
[ETCPACK](https://github.com/Ericsson/ETCPACK).

Resampling methods are available to transform the UV space. For example, they can generate the six faces of a cube map
from a single equirectangular (spherical) map.

### Basic materials

A basic material library is provided for quick prototyping.
The generated shaders can be overridden when more complex rendering is needed.

| Category        | Options                                                                                     |
|-----------------|---------------------------------------------------------------------------------------------|
| Diffuse color   | uniform color, vertex color, textured                                                       |
| Lighting        | directional light (sun), point lights, shadow casting from both the directional and point lights |
| Lighting models | Phong, BRDF                                                                                 |
| Other           | texture atlas and blending, soft distance fade-out, per-instance orientation and color      |

### Graphical user interface

A GUI toolkit is provided, together with a text-rendering helper and font management.
It supports binding callbacks and animations to widgets.

If you need a more full-featured solution, [Dear ImGui](https://github.com/ocornut/imgui) is a powerful alternative.

### Profiler

A basic profiler is included. To profile a block of code, instrument it with the `TRE_PROFILEDSCOPE` macro:

```cpp
{
  TRE_PROFILEDSCOPE("name", scopedUniqueCppName);
  // ... code to profile ...
}
```

> **Note:** the profiler currently supports a single thread only.

### Audio

A wrapper around the SDL2 audio API is provided as an alternative to SDL_mixer.

It can read WAV, MP3 and Opus files, and can bake audio data either as raw samples or compressed with Opus.

## Dependencies

| Dependency                                        | Required | Purpose                      |
|---------------------------------------------------|----------|------------------------------|
| OpenGL (3.3, or ES 3.0 for WebAssembly)           | Yes      | Rendering                    |
| [SDL2](https://www.libsdl.org/)                   | Yes      | Windowing, input, audio      |
| [GLM](https://github.com/g-truc/glm)              | Yes      | Math                         |
| [FreeType](https://freetype.org/)                 | Optional | Font loading                 |
| [SDL_image](https://github.com/libsdl-org/SDL_image) | Optional | Image loading             |
| [libtiff](https://libtiff.gitlab.io/libtiff/)     | Optional | TIFF image loading           |
| [Opus](https://opus-codec.org/)                   | Optional | Opus audio encoding/decoding |
| [minimp3](https://github.com/lieff/minimp3)       | Optional | MP3 decoding (embedded as a single header) |