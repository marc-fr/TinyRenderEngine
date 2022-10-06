# TinyRenderEngine

Toolkit aimed to provide a foundation for the development of games or game engines

## Description

TinyRnederEngine provides utilities for the development of games or game engines.
It is written in C++11, and the STL is used (so exeptions should not be disabled even if the library does not throw exceptions by itself).
Usage of third-parties is kept limited, or is optional.
It contains:
- contact algorithm (2D and 3D), 
- mesh utilities (handle mesh data, baking, wrapper to OpenGL API, transform meshes, compute bounds, compute LOD),
- texture utilities (baking, wrapper to OpenGL API, sample textures, compute cube-map),
- shader utilities (wrapper to OpenGL API, basic materials),
- render-target utilities (wrapper to OpenGL API, basic post-effects),
- text rendering,
- ui (widget based)
- profiler
- audio-mixer (wrapper to SDL2 API)

It is not a game engine, as this library does not contain entity-managment, neither game-loop implementation, neither game editor.

The main target platform is desktop on Windows and Linux.
The web-assembly platform is also supported, through the compatibility with OpenGL ES 3 and [Emscriten](https://emscripten.org/).

## Dependencies

* OpenGL

* SDL2

* SDL_Image (optionnal)

* [glm](https://glm.g-truc.net/0.9.9/index.html)

* freetype (optionnal)

* libTIFF (optionnal)

* [opus](https://opus-codec.org) (optionnal)

## Code sample

```cpp
int main(int argc, char **argv)
{
  tre::windowHelper myWindow; // a helper provided by the library

  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "window name", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

  // - Upload mesh

  tre::modelRaw2D mesh2D;

  // [...] <- my mesh loading (WaveFront file, procedural, ...)

  mesh2D.loadIntoGPU();

  // - Load shaders

  tre::shader shaderMainMaterial;
  shaderMainMaterial.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR); // just a solid-color mat, without lighting.

  tre::shader shaderDataVisu;
  {
    const char *srcFrag_Color = /* ... */;

    tre::shader::s_layout shLayout(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_UNICOLOR);

    shaderDataVisu.loadCustomShader(shLayout, srcFrag_Color, "dataVisualisation");
  }

  // - load UI

  tre::font font;
#ifdef TRE_WITH_FREETYPE
  std::vector<unsigned> texSizes = { 64, 20, 12 };
  font.loadNewFontMapFromTTF("...", texSizes);
#else
  font.loadNewFontMapFromBMPandFNT("...");
#endif

  tre::baseUI2D bUI_main;
  bUI_main.set_defaultFont(&font);
  tre::ui::window &wUI_main = *bUI_main.create_window(); // this is bad (no check on null-ptr dereference)
  wUI_main.set_layoutGrid(3, 2);
  
  // [...] <- my UI creation

  bUI_main.loadShader();
  bUI_main.loadIntoGPU();

  // - scene and event variables

  SDL_Event event;

  // - init rendering

  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glDisable(GL_DEPTH_TEST);

  // - MAIN LOOP ------------

  while(!myWindow.m_controls.m_quit)
  {
    // event actions + updates --------

    myWindow.m_controls.newFrame();

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myWindow.m_controls.treatSDLEvent(event);
      // [...] <- my events
    }

    // [ ... ] <- my logic

    // [ ... ] <- and maybe mesh updates

    mesh2D.updateIntoGPU();

    // main render pass -------------

    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glClear(GL_COLOR_BUFFER_BIT);

    // - render mesh

    mesh2D.drawcall(0, 0, true); // just bind the VAO

    glUseProgram(shaderDataVisu.m_drawProgram);
    shaderDataVisu.setUniformMatrix(myWindow.m_matProjection2D);

    const glm::vec4 uChoiceVisu(visuMode == 0 ? 1.f : 0.f,
                                visuMode == 1 ? 1.f : 0.f,
                                visuMode == 2 ? 1.f : 0.f,
                                visuMode == 3 ? 1.f : 0.f);
    glUniform4fv(shaderDataVisu.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uChoiceVisu));

    mesh2D.drawcall(data.m_meshPartTriangulated, 1, false, GL_TRIANGLES);

    glUseProgram(shaderMainMaterial.m_drawProgram);
    shaderMainMaterial.setUniformMatrix(myWindow.m_matProjection2D);

    mesh2D.drawcall(data.m_meshPartWireframe, 1, false, GL_LINES);

    mesh2D.drawcall(data.m_meshPartEnvelop, 1, false, GL_LINES);

    tre::IsOpenGLok("main render pass - draw meshes");

    // - render UI

    bUI_main.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
    bUI_main.updateIntoGPU();
    bUI_main.draw();

    tre::IsOpenGLok("UI render pass");

    // end render pass --------------

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  shaderMainMaterial.clearShader();
  shaderDataVisu.clearShader();

  mesh2D.clearGPU();

  font.clear();

  bUI_main.clear();
  bUI_main.clearGPU();
  bUI_main.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
```
