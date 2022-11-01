
#include "tre_shader.h"
#include "tre_model.h"
#include "tre_ui.h"
#include "tre_font.h"
#include "tre_windowContext.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <vector>
#include <string>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

// Default resources

static const std::vector<std::string> texturesPathDefault = { TESTIMPORTPATH "resources/Asphalt_004_COLOR.bmp",
                                                              TESTIMPORTPATH "resources/Asphalt_004_NRM.bmp",
                                                              TESTIMPORTPATH "resources/quad.bmp",
                                                              TESTIMPORTPATH "resources/font_arial_88.bmp",
                                                              TESTIMPORTPATH "resources/debug.png",
                                                            };

// Other resources

static tre::windowContext myWindow;
static tre::windowContext::s_controls myControls;

static std::string       texturePathFromArg;
static tre::texture      textureREF;
static tre::texture      textureCOMP;
static tre::modelRaw2D   meshSquare;
static tre::shader       shaderSampleDirect;
static tre::shader       shaderCompare;
static tre::font         worldFont;
static tre::baseUI2D     worldUI;
static tre::ui::window  *worldWin;

// Scene variables

static unsigned textureId = 0;
static unsigned textureId_prev = -1u;
static const unsigned texturesCount = unsigned(texturesPathDefault.size());

static unsigned shaderMode = 0;
static const unsigned NshaderMode = 3;
static tre::shader* listShader[NshaderMode] = { &shaderSampleDirect, &shaderSampleDirect, &shaderCompare };
static std::string listShaderName[NshaderMode] = { "REF", "Compressed", "Compare"};
static glm::mat3 mModel = glm::mat3(1.f);
static glm::mat3 mView = glm::mat3(1.f);

// =============================================================================

static int app_init(int argc, char **argv)
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "test Texture compression", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

#ifdef TRE_OPENGL_ES
  TRE_LOG("extensions: " << reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)));
#endif

  // Arguments

#ifndef TRE_EMSCRIPTEN
  if (argc > 1) texturePathFromArg = argv[1];
#endif

  // GPU resources

  meshSquare.createPart(6);
  meshSquare.fillDataRectangle(0, 0, glm::vec4(-1.f, -1.f, 1.f, 1.f), glm::vec4(1.f), glm::vec4(0.f, 1.f, 1.f, 0.f));
  meshSquare.loadIntoGPU();

  shaderSampleDirect.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);

  {
    tre::shader::s_layout layout(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);
    layout.hasSMP_DiffuseB = true;

    const char * srcFrag = "void main()\n"
                           "{\n"
                           "  vec4 colorREF  = texture(TexDiffuse , pixelUV);\n"
                           "  vec4 colorCOMP = texture(TexDiffuseB, pixelUV);\n"
                           "  vec4 colorDIFF = abs(colorCOMP - colorREF);\n"
                           "  float errInf = min(1.f, 5.f * max(max(colorDIFF.x, colorDIFF.y), colorDIFF.z));\n"
                           "  float grey = colorREF.r * 0.3f + colorREF.g * 0.3f + colorREF.b * 0.3f;\n"
                           "  color.xyz = vec3(0.9f * errInf + 0.1f * grey, 0.1f * grey, 0.1f * grey);\n"
                           "  color.w = 1.f;\n"
                           "}\n";

    shaderCompare.loadCustomShader(layout, srcFrag, "shaderCompare");
  }

  // U.I

  {
    worldFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);
  }

  {
    worldUI.set_defaultFont(&worldFont);

    worldUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

    worldWin = worldUI.create_window();
    worldWin->set_colormask(glm::vec4(0.7f,1.f,0.7f,0.9f));
    worldWin->set_colorAlpha(0.7f);
    worldWin->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    worldWin->set_cellMargin(tre::ui::s_size(4, tre::ui::SIZE_PIXEL));
    worldWin->set_topbar("Parameters", true, false);
    {
      glm::mat3 wmat(1.f);
      wmat[2][0] = -1.f / myWindow.m_matProjection2D[0][0] + 0.01f;
      wmat[2][1] = 0.99f;
      worldWin->set_mat3(wmat);
    }
    worldWin->set_layoutGrid(3,2);

    if (texturePathFromArg.empty()) worldWin->create_widgetText(0, 0)->set_text("texture (F1:F4):")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(0, 1); // texture name

    worldWin->create_widgetText(1, 0)->set_text("visualization (F5:F8):")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(1, 1); // visu name

    worldWin->create_widgetText(2, 0)->set_text("status:")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(2, 1); // status

    // load
    worldUI.loadIntoGPU();
    worldUI.loadShader();
  }

  return tre::IsOpenGLok("main: initialization") ? 0 : -3;
}

// =============================================================================

static void app_update()
{
  SDL_Event event;

  // event actions + updates -------

  myWindow.SDLEvent_newFrame();
  myControls.newFrame();

  //-> SDL events
  while(SDL_PollEvent(&event) == 1)
  {
    myWindow.SDLEvent_onWindow(event);
    myControls.treatSDLEvent(event);
    worldUI.acceptEvent(event);

    if (event.type == SDL_KEYDOWN)
    {
      if      (event.key.keysym.sym == SDLK_F1) { textureId = 0; }
      else if (event.key.keysym.sym == SDLK_F2) { textureId = textureId == 0 ? texturesCount - 1 : textureId - 1; }
      else if (event.key.keysym.sym == SDLK_F3) { textureId = textureId == texturesCount - 1 ? 0 : textureId + 1; }
      else if (event.key.keysym.sym == SDLK_F4) { textureId = texturesCount - 1; }
      else if (event.key.keysym.sym == SDLK_F5) { shaderMode = 0; }
      else if (event.key.keysym.sym == SDLK_F6) { shaderMode = shaderMode == 0 ? NshaderMode - 1 : shaderMode - 1; }
      else if (event.key.keysym.sym == SDLK_F7) { shaderMode = shaderMode == NshaderMode - 1 ? 0 : shaderMode + 1; }
      else if (event.key.keysym.sym == SDLK_F8) { shaderMode = NshaderMode - 1; }

      else if (event.key.keysym.sym == SDLK_HOME) { mView = glm::mat3(1.f); }
    }
  }

  if (myControls.m_keyUP)    mView[2][1] -= 0.01f;
  if (myControls.m_keyDOWN)  mView[2][1] += 0.01f;
  if (myControls.m_keyLEFT)  mView[2][0] += 0.01f;
  if (myControls.m_keyRIGHT) mView[2][0] -= 0.01f;
  if (myControls.m_keySHIFT) { mView *= 1.02f; mView[2][2] = 1.f; }
  if (myControls.m_keyCTRL)  { mView /= 1.02f; mView[2][2] = 1.f; }

  if (myWindow.m_viewportResized)
  {
    worldUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

    glm::mat3 wmat(1.f);
    wmat[2][0] = -1.f / myWindow.m_matProjection2D[0][0] + 0.01f;
    wmat[2][1] = 0.99f;
    worldWin->set_mat3(wmat);
  }

  // load texture if neeeded

  if (!texturePathFromArg.empty()) textureId = 0;

  if (textureId_prev != textureId)
  {
    bool success = true;

    textureREF.clear();
    textureCOMP.clear();

    const std::string &texCurrentPath = (texturePathFromArg.empty() ?  texturesPathDefault[textureId] : texturePathFromArg);
#ifdef TRE_WITH_SDL2_IMAGE
    SDL_Surface *surf = tre::texture::loadTextureFromFile(texCurrentPath);
#else
    SDL_Surface *surf = tre::texture::loadTextureFromBMP(texCurrentPath);
#endif
    if (surf != nullptr)
    {
      success &= textureREF.load(surf, tre::texture::MMASK_NEAREST_MAG_FILTER, false);
      success &= textureCOMP.load(surf, tre::texture::MMASK_NEAREST_MAG_FILTER | tre::texture::MMASK_COMPRESS, true);
    }
    else
    {
      success = false;
    }

    textureId_prev = textureId;
    worldWin->get_widgetText(2,1)->set_text(success ? "load completed" : "load failed");
  }

  // render

  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND); // render with "alpha"

  glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  tre::shader & curShader = * listShader[shaderMode];

  const glm::mat3 mPVM(myWindow.m_matProjection2D * mView * mModel);

  glUseProgram(curShader.m_drawProgram);

  curShader.setUniformMatrix(mPVM, mModel);

  glActiveTexture(GL_TEXTURE2);
  if (shaderMode == 1) glBindTexture(GL_TEXTURE_2D, textureCOMP.m_handle);
  else                 glBindTexture(GL_TEXTURE_2D, textureREF.m_handle);

  glUniform1i(curShader.getUniformLocation(tre::shader::TexDiffuse),2);

  if (curShader.layout().hasSMP_DiffuseB)
  {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, textureCOMP.m_handle);

    glUniform1i(curShader.getUniformLocation(tre::shader::TexDiffuseB),3);
  }

  meshSquare.drawcallAll();

  tre::IsOpenGLok("render");

  // UI-render

  if (texturePathFromArg.empty()) worldWin->get_widgetText(0,1)->set_text(texturesPathDefault[textureId]);
  else                            worldWin->get_widgetText(0,1)->set_text(texturePathFromArg);
  worldWin->get_widgetText(1,1)->set_text(listShaderName[shaderMode]);

  worldUI.updateIntoGPU();

  worldUI.draw();

  tre::IsOpenGLok("UI pass");

  // end render pass --------------

  SDL_GL_SwapWindow( myWindow.m_window );
}

// =============================================================================

static void app_quit()
{
  TRE_LOG("Main loop exited");

  meshSquare.clearGPU();

  worldUI.clear();
  worldUI.clearGPU();
  worldUI.clearShader();
  worldFont.clear();

  shaderSampleDirect.clearShader();
  shaderCompare.clearShader();

  textureREF.clear();
  textureCOMP.clear();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("app_quit: Program finalized with success");
}

// =============================================================================

int main(int argc, char **argv)
{
  if (app_init(argc, argv) != 0)
    return -9;

#ifdef TRE_EMSCRIPTEN
  //emscripten_request_animation_frame_loop(app_update, nullptr);
  emscripten_set_main_loop(app_update, 0, true);

  // emscripten_set_fullscreenchange_callback
  // emscripten_set_canvas_element_size
#else
  while(!myWindow.m_quit && !myControls.m_quit)
  {
    app_update();
  }

  app_quit();

#endif

  return 0;
}
