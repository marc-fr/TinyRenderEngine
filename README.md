# TinyRenderEngine

Toolkit aimed at providing a foundation for the development of games or game engines.

## Description

TinyRnederEngine provides various utilities for the development of games or game engines,
such as mesh operations, contact algorithm, UI and wrappers for OpenGL API.
It is designed to empower game programmers to create prototypes or released game.
It favors simplicity to embed and to use. 

It is written in C++11, with the usage of the STL.
Usage of third-parties is kept limited, or is optional.
The main target platform is desktop that supports OpenGL (primilary Windows and Linux).
The web-assembly platform is also supported, with the help of [Emscriten](https://emscripten.org/) and the compatibility with OpenGL ES 3. 

TinyRnederEngine is not a game engine.
Indeed, it does not implement an entity managment, neither game-loop logic, neither a game editor.

## Usage

No specific build process is required. 
You can either build a static library from it (a CMake project is included, that handles being included from your root project),
either add the sources files into your existing project.

Some examples are provided in the [tests](./tests) directory.

## Features

### Contact algorithms (2D and 3D)

Contact algorithms with various geometric objects, in 2D and 3D, are implemented:

* point in object: it can additionally return the penetration distance to the closest border  
  ![contact-2D-point](doc/contact-2D-point.jpg)

* raycast in object
  ![doc/contact-2D-raycast](doc/contact-2D-raycast.jpg)

* object in object: it can additionally return the barycenter of the interpenetration, with the minimal translation needed to make the objects not penetrating with each others.
  ![contact-2D-shape-penetration](doc/contact-2D-shape-penetration.jpg)
  ![contact-3D-box-penetration](doc/contact-3D-box-penetration.jpg)
  ![contact-3D-shape-penetration](doc/contact-3D-shape-penetration.jpg)   


### Mesh operations

Besides a light wrapper to OpenGL that holds GPU-buffers handlers and that encapsulates draw-call commands,
some mesh-processing algorithms are implemented.
These algorithms are not designed to be run in a game context or a real-time context,
but for pre-processing purpose.

* compute of the bounding-box

* compute of the barycentric center, the aera (in 2D) and the volume (3D)

* compute of the convexe hull
  ![mesh-3D-convexe-hull](doc/mesh-3D-convexe-hull.jpg)

* simplification by reducing the number of vertices (**WIP**)
  ![mesh-3D-simplify](doc/mesh-3D-simplify.jpg)

* triangulation (in 2D) and tetrahedrization (in 3D) (**WIP**)
  ![mesh-2D-triangulation](doc/mesh-2D-triangulation.jpg)
  ![mesh-3D-tetrahedrization](doc/mesh-3D-tetrahedrization.jpg)


### Textures utilities

Handeling of textures can be tricky when dealing with formats and compression across OpenGL versions.
A wrapper to OpenGL is implemented to take account of various cases:
```
tre::texture::MMASK_MIPMAP             : tell to OpenGL to automatically generate the mipmaps.
tre::texture::MMASK_COMPRESS           : compress to native OpenGL compressed texture
tre::texture::MMASK_ANISOTROPIC        : tell to OpenGL to use the anisotropic filtering (it requires MIPMAP)
tre::texture::MMASK_ALPHA_ONLY         : only take the alpha channel
tre::texture::MMASK_RG_ONLY            : only take the red-green channels
tre::texture::MMASK_FORCE_NO_ALPHA     : only take the red-green-blue channels
tre::texture::MMASK_NEAREST_MAG_FILTER : tell to OpenGL to use the nearest magnificate-filtering (default is linear)
```

Compression algorithms have been implemented, even if the OpenGL drivers can acheive it on desktop platforms.
The proposed implementation is simple but has limited features compared to standard implementations, like [ETCPACK](https://github.com/Ericsson/ETCPACK)

Re-sampling methods are also available, that can transform the UV space.
A good example is to generate the cube-map faces from a single map in the equirectangular-spherical projection.


### Basic materials

A basic material library is proposed, that allows quick prototyping with a lot of combinaisons of texturing and lighting.
The generated shaders can be overwritten for more complexe rendering.

* Diffuse color options: uniform-color, vertex-color, textured
* Lighting options: unidirectional light, point lights, shadow casting from the unidirectional light and the point lights
* Lighting models: Phong, BRDF
* Other features: texture altas and blending, soft-distance fadeout, instanced orientation and color  

Example: instanced billboards, textured and lighted:
```
  shaderInstancedBB.loadShader(tre::shader::PRGM_2Dto3D,
                               tre::shader::PRGM_TEXTURED |
                               tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                               tre::shader::PRGM_UNIPHONG |
                               tre::shader::PRGM_INSTANCED | tre::shader::PRGM_INSTCOLOR | tre::shader::PRGM_ROTATION);

```


### Graphical user interface

A G.U.I toolkit is provided, that comes with a text-rendering helper and a font management.
Note that [ImGUI](https://github.com/ocornut/imgui) is a powerful alternative to the present implementation,
even though the present implementation allows to bind callbacks and supports animations.

![UI-example](doc/UI-example.jpg)


### Profiler

A basic profiler is provided.

The code should be intrumentalized with `TRE_PROFILEDSCOPE("name", scoped-unique-cpp-name);`
, resulting ![screenshot](doc/profiler-screenshot-1.jpg) 


### Audio mixer

A wrapper to the SDL2 audio API is implemented.
It also helps to bake audio files.
This is an optional alternative to SDL_Mixer.


## Dependencies

* OpenGL

* SDL2

* [glm](https://glm.g-truc.net/0.9.9/index.html)

* freetype (optional)

* SDL_Image (optional)

* [libTIFF](http://www.libtiff.org) (optional)

* [opus](https://opus-codec.org) (optional)
