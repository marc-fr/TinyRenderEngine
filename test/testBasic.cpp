
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model.h"
#include "tre_font.h"
#include "tre_textgenerator.h"
#include "tre_windowContext.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <math.h>
#include <stdlib.h> // rand,srand
#include <time.h>   // time
#include <string>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

#define TEST_WITH_SHADOW
#define TEST_WITH_SKYBOX
#define TEST_WITH_FPS

// =============================================================================

static tre::windowContext myWindow;
static tre::windowContext::s_controls myControls;
static tre::windowContext::s_timer myTimings;
static tre::windowContext::s_view3D myView3D(&myWindow);

static tre::modelStaticIndexed3D    meshCube(tre::modelStaticIndexed3D::VB_NORMAL);
static tre::modelInstancedBillboard meshParticleBB(tre::modelInstanced::VI_COLOR | tre::modelInstanced::VI_ROTATION);

static tre::shader::s_UBOdata_sunLight  sunLight_Data;

static tre::shader shaderInstancedBB;
static tre::shader shaderMesh3D;

static tre::texture texture2D;

#ifdef TEST_WITH_SHADOW
static tre::renderTarget_ShadowMap      sunLight_ShadowMap;
static tre::shader::s_UBOdata_sunShadow sunShadow_Data;
static tre::shader                      shaderShadow;
#endif

#ifdef TEST_WITH_SKYBOX
static tre::texture              textureSkyBox;
static tre::shader               shaderSkyBox;
static tre::modelStaticIndexed3D meshSkyBox;
#endif

#ifdef TEST_WITH_FPS
static tre::shader        shaderFps;
static tre::font          font;
static tre::modelRaw2D    meshFps;
#endif

template <std::size_t _size>
struct s_particleBatch
{
private:
  std::array<glm::vec4, _size> m_pos; // 3D-position + initial-rotation
  std::array<glm::vec4, _size> m_color;
  std::array<float, _size>     m_life;
  std::array<float, _size>     m_lifeEnd;

public:
  std::size_t size() const { return _size; }

  void reset()
  {
    m_life.fill(1.f);
    m_lifeEnd.fill(0.f);
  }

  void update(float dt)
  {
    static const float invRandMax = 1.f / float(RAND_MAX);
    for (std::size_t i = 0; i < _size; ++i)
    {
      if ((m_life[i] += dt) > m_lifeEnd[i])
      {
        m_pos[i] = glm::vec4(std::rand() * 2.f * invRandMax - 1.f, -1.f, std::rand() * 2.f * invRandMax - 1.f, std::rand() * 6.28f * invRandMax);
        const float colorCursor = std::rand() * invRandMax;
        m_color[i] = glm::vec4(colorCursor, 2.f * colorCursor * (1.f - colorCursor), 1.f - colorCursor, 1.f);
        m_life[i] = 0.f;
        m_lifeEnd[i] = 1.f + std::rand() * 4.f * invRandMax;
      }
    }
  }

  void fillGPUBuffer(const tre::s_modelDataLayout &layout, std::size_t offset, std::size_t count) const
  {
    TRE_ASSERT(count <= size());

    auto posIt = layout.m_instancedPositions.begin<glm::vec4>(offset);
    auto rotIt = layout.m_instancedRotations.begin<float>(offset);
    auto colIt = layout.m_instancedColors.begin<glm::vec4>(offset);

    for (std::size_t i = 0; i < count; ++i)
    {
      *posIt++ = glm::vec4(m_pos[i].x, m_pos[i].y, m_pos[i].z, 0.015f);
      *rotIt++ = m_pos[i].w + 0.2f * m_life[i];
      *colIt++ = m_color[i];
    }
  }
};

static s_particleBatch<1024> particleBatch;

// ----------------------------------------------------------------------------

static int app_init()
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Basic", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - random generator
  srand(time(nullptr));

  // - load mesh

  meshCube.createPartFromPrimitive_box(glm::mat4(1.f), 0.5f);
  meshCube.loadIntoGPU();

  meshParticleBB.createBillboard();
  meshParticleBB.loadIntoGPU();

#ifdef TEST_WITH_SKYBOX
  meshSkyBox.createPartFromPrimitive_box(glm::mat4(1.f), 10.f);
  meshSkyBox.loadIntoGPU();
#endif

#ifdef TEST_WITH_FPS
  meshFps.loadIntoGPU();
#endif

  tre::IsOpenGLok("app_init: load mesh");

  // material (shaders)

  sunLight_Data.color = glm::vec3(0.9f,0.9f,0.9f);
  sunLight_Data.colorAmbiant = glm::vec3(0.1f,0.1f,0.1f);

#ifdef TEST_WITH_SHADOW
  sunShadow_Data.nShadow = 1;

  // Render-targets

  sunLight_ShadowMap.load(2048,2048);
  sunLight_ShadowMap.setSceneBox(tre::s_boundbox(glm::vec3(-2.f), glm::vec3(2.f)));

  const int flagShaderShadow = tre::shader::PRGM_SHADOW_SUN;

  shaderInstancedBB.setShadowSunSamplerCount(1);
  shaderMesh3D.setShadowSunSamplerCount(1);

#else
  const int flagShaderShadow = 0;
#endif

  tre::IsOpenGLok("app_init: load render-targets");

  // Shaders

#ifdef TEST_WITH_SHADOW
  shaderShadow.loadShader(tre::shader::PRGM_3D_DEPTH,0);
#endif

  shaderInstancedBB.loadShader(tre::shader::PRGM_2Dto3D,
                               tre::shader::PRGM_TEXTURED |
                               tre::shader::PRGM_LIGHT_SUN | flagShaderShadow |
                               tre::shader::PRGM_UNIPHONG |
                               tre::shader::PRGM_INSTANCED | tre::shader::PRGM_INSTCOLOR | tre::shader::PRGM_ROTATION);

  shaderMesh3D.loadShader(tre::shader::PRGM_3D,
                          tre::shader::PRGM_UNIBRDF |
                          tre::shader::PRGM_LIGHT_SUN | flagShaderShadow);

#ifdef TEST_WITH_FPS
  shaderFps.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_TEXTURED);
#endif

#ifdef TEST_WITH_SKYBOX
  shaderSkyBox.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_CUBEMAPED);
#endif

  tre::IsOpenGLok("app_init: load shaders");

  // Textures

  if (!texture2D.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/quad.png"), tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_MIPMAP | tre::texture::MMASK_SRBG_SPACE, true))
    texture2D.loadWhite();

#ifdef TEST_WITH_SKYBOX
  {
    // The textures are generated from "testTextureSampling", with TRE_WITH_SDL2_IMAGE enabled
    const std::array<SDL_Surface*, 6> cubeFaces = { tre::texture::loadTextureFromFile("imageSDL.1024.inside.xpos.png"),
                                                    tre::texture::loadTextureFromFile("imageSDL.1024.inside.xneg.png"),
                                                    tre::texture::loadTextureFromFile("imageSDL.1024.inside.ypos.png"),
                                                    tre::texture::loadTextureFromFile("imageSDL.1024.inside.yneg.png"),
                                                    tre::texture::loadTextureFromFile("imageSDL.1024.inside.zpos.png"),
                                                    tre::texture::loadTextureFromFile("imageSDL.1024.inside.zneg.png"), };
    textureSkyBox.loadCube(cubeFaces, tre::texture::MMASK_COMPRESS, true);
  }
#endif

#ifdef TEST_WITH_FPS
  if (!font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true)) font.loadProceduralLed(2, 0);
  meshFps.createPart(128);
#endif

  tre::IsOpenGLok("app_init: load textures");

  // Particles

  particleBatch.reset();

  // End Init

  tre::checkLayoutMatch_Shader_Model(&shaderInstancedBB, &meshParticleBB);
  tre::checkLayoutMatch_Shader_Model(&shaderMesh3D, &meshCube);
#ifdef TEST_WITH_SHADOW
  tre::checkLayoutMatch_Shader_Model(&shaderShadow, &meshCube);
#endif

  myView3D.m_matView[3] = glm::vec4(0.f, 0.f, -2.f, 1.f);
  myView3D.setScreenBoundsMotion(false);
  myView3D.setKeyBinding(true);

  myTimings.initialize();

  TRE_LOG("app_init: ok.");
  return 0;
}

// ----------------------------------------------------------------------------

static void app_update()
{
  const float dt = myTimings.frametime;

  SDL_Event event;

  // event actions + updates -------

  {
    myWindow.SDLEvent_newFrame();
    myControls.newFrame();
    myTimings.newFrame(0, myControls.m_pause);

    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myControls.treatSDLEvent(event);
    }

    if (myWindow.m_hasFocus)
      myView3D.treatControlEvent(myControls, myTimings.frametime);

    if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED)
      myView3D.setMouseBinding(!myView3D.m_mouseBound);
    // Note: with Emscripten, the user needs to left click on the canvas to have the mouse lock active (after that the bounding is requested)

    if (myWindow.m_viewportResized)
    {
    }
  } // end events

  // world simulation -------------------

  const glm::mat4 mModelCube = glm::rotate(glm::mat4(1.f),0.1f + myTimings.scenetime * 6.28f*0.2f,glm::vec3(0.8f,0.f,0.6f));

  if (!myControls.m_pause)
  {
    particleBatch.update(dt);

    meshParticleBB.resizeInstance(particleBatch.size());

    particleBatch.fillGPUBuffer(meshParticleBB.layout(), 0, particleBatch.size());

    meshParticleBB.updateIntoGPU();
  }

  // prepare render ----------------------

  {
    sunLight_Data.direction = glm::vec3(0.f, -1.f, 0.f);

    tre::shader::updateUBO_sunLight(sunLight_Data);

#ifdef TEST_WITH_SHADOW
    sunLight_ShadowMap.computeUBO_forMap(sunLight_Data, sunShadow_Data, 0);
    tre::shader::updateUBO_sunShadow(sunShadow_Data);
#endif
  }

  // shadow-map render pass --------------
#ifdef TEST_WITH_SHADOW
  {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);

    sunLight_ShadowMap.bindForWritting();
    glClear(GL_DEPTH_BUFFER_BIT);

    const glm::mat4 localMPV = sunLight_ShadowMap.mProj() * sunLight_ShadowMap.mView();

    glUseProgram(shaderShadow.m_drawProgram);

    shaderShadow.setUniformMatrix(localMPV * mModelCube, mModelCube);

    meshCube.drawcallAll();
  }

  tre::IsOpenGLok("shadow-map render pass");
#endif

  // Start render

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
  glDepthMask(GL_TRUE);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // skybox render pass ----------------

#ifdef TEST_WITH_SKYBOX
  {
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glUseProgram(shaderSkyBox.m_drawProgram);

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_CUBE_MAP,textureSkyBox.m_handle);
    glUniform1i(shaderSkyBox.getUniformLocation(tre::shader::TexCube),3);

    glm::mat4 MViewBox(myView3D.m_matView);
    MViewBox[3] = glm::vec4(0.f,0.f,0.f,1.f); // no translation

    shaderSkyBox.setUniformMatrix(myWindow.m_matProjection3D * MViewBox, glm::mat4(1.f), MViewBox);

    meshSkyBox.drawcallAll();
  }

  tre::IsOpenGLok("skybox render pass");
#endif

  // opaque render pass ----------------

  const glm::mat4 mPV = myWindow.m_matProjection3D * myView3D.m_matView;

  {
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

#ifdef TEST_WITH_SHADOW
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sunLight_ShadowMap.depthHandle());
#endif

    const glm::vec4 ucolorMain(0.5f,0.5f,0.5f,1.f);

    glUseProgram(shaderMesh3D.m_drawProgram);

#ifdef TEST_WITH_SHADOW
    glUniform1i(shaderMesh3D.getUniformLocation(tre::shader::TexShadowSun0),2);
#endif

    shaderMesh3D.setUniformMatrix(mPV * mModelCube, mModelCube, myView3D.m_matView);

    glUniform2f(shaderMesh3D.getUniformLocation(tre::shader::uniBRDF), 0.1f, 0.7f);

    glUniform4fv(shaderMesh3D.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMain));

    meshCube.drawcallAll();
  }

   tre::IsOpenGLok("opaque render pass - draw Opaque Mesh");

  // transparent render pass ------

  {
    glEnable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE); // just testing, as this is need when the option "soft-particle" is used.

    glUseProgram(shaderInstancedBB.m_drawProgram);

#ifdef TEST_WITH_SHADOW
    glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexShadowSun0),2);
#endif

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture2D.m_handle);
    glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexDiffuse), 1);

    glUniform3f(shaderInstancedBB.getUniformLocation(tre::shader::uniPhong), 2.f, 0.7f, 0.8f);

    const glm::mat3 matOrient = glm::mat3(-1.f, 0.f, 0.f,
                                           0.f, 0.f, 1.f,
                                           0.f, 1.f, 0.f);

    shaderInstancedBB.setUniformMatrix(mPV);

    glUniformMatrix3fv(shaderInstancedBB.getUniformLocation(tre::shader::MOrientation), 1, GL_FALSE, glm::value_ptr(matOrient));

    meshParticleBB.drawInstanced(0, 0, particleBatch.size());

    tre::IsOpenGLok("main render pass - draw Transparent Particle BB");
  }

  // UI-render pass -------------
#ifdef TEST_WITH_FPS
  {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    {
      char txtFPS[128];
      snprintf(txtFPS, 127, "%03d fps (Work-elapsed = %03d ms, Swap-latency = %03d ms)",
               int(1.f/myTimings.frametime),
               int(myTimings.worktime * 1000),
               int((myTimings.frametime - myTimings.worktime) * 1000));
      tre::textgenerator::s_textInfo tInfo;
      tInfo.setupBasic(&font, txtFPS);
      tInfo.setupSize(0.05f);
      tInfo.m_fontPixelSize = 16;
      meshFps.resizePart(0, tre::textgenerator::geometry_VertexCount(tInfo.m_text));
      tre::textgenerator::generate(tInfo, &meshFps, 0, 0, nullptr);
      meshFps.updateIntoGPU();

      glm::mat3 mViewModel_hud = glm::mat3(1.f);
      mViewModel_hud[0][0] = 1.f;
      mViewModel_hud[1][1] = 1.f;
      mViewModel_hud[2][0] =  -0.95f / myWindow.m_matProjection2D[0][0];
      mViewModel_hud[2][1] =   0.9f;

      glUseProgram(shaderFps.m_drawProgram);
      shaderFps.setUniformMatrix(myWindow.m_matProjection2D * mViewModel_hud);

      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D,font.get_texture().m_handle);
      glUniform1i(shaderFps.getUniformLocation(tre::shader::TexDiffuse),0);

      meshFps.drawcallAll(true);
    }
  }
#endif

  // present -----------------

  myTimings.endFrame_beforeGPUPresent();

  SDL_GL_SwapWindow( myWindow.m_window );
}

// ----------------------------------------------------------------------------

static void app_quit()
{
  TRE_LOG("Main loop exited");
  TRE_LOG("Average work elapsed-time needed for each frame: " << myTimings.worktime_average * 1000 << " ms");
  TRE_LOG("Average frame elapsed-time needed for each frame (Vsync enabled): " << myTimings.frametime_average * 1000 << " ms");

  // Finalize
  meshCube.clearGPU();
  meshParticleBB.clearGPU();

  texture2D.clear();

  shaderInstancedBB.clearShader();
  shaderMesh3D.clearShader();

#ifdef TEST_WITH_SHADOW
  shaderShadow.clearShader();
  sunLight_ShadowMap.clear();
#endif

#ifdef TEST_WITH_SKYBOX
  shaderSkyBox.clearShader();
  textureSkyBox.clear();
  meshSkyBox.clearGPU();
#endif

#ifdef TEST_WITH_FPS
  shaderFps.clearShader();
  font.clear();
  meshFps.clearGPU();
#endif

  tre::shader::clearUBO();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("app_quit: Program finalized with success");
}

// =============================================================================

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  if (app_init() != 0)
    return -1;

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
