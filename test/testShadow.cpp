
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model_importer.h"
#include "tre_font.h"
#include "tre_textgenerator.h"
#include "tre_windowContext.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <math.h>
#include <time.h>   // time
#include <string>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// Debug ======================================================================

struct s_shadowDebug
{
  tre::shader     shader;
  tre::modelRaw2D geom;
  bool            _isLoaded = false;

  void load()
  {
    tre::shader::s_layout shLayout(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);

    const char * SourceShader2DTexturedDepth_FragmentMain =
    "void main(){\n"
    "  float d = texture(TexDiffuse, pixelUV).r;\n"
    "  color = vec4(d,0.f,0.1f,1.f);\n"
    "}\n";
    if (!shader.loadCustomShader(shLayout, SourceShader2DTexturedDepth_FragmentMain, "2DTexDepth_debug"))
      return;

    // squared-map
    geom.createPart(6);
    geom.fillDataRectangle(0, 0, glm::vec4(-1.f, -1.f, 1.f, 1.f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));

    if (!geom.loadIntoGPU())
      return;

    _isLoaded = true;
  }

  void draw(const glm::mat3 &mProj2D)
  {
    if (!_isLoaded) return;

    glUseProgram(shader.m_drawProgram);

    glUniform1i(shader.getUniformLocation(tre::shader::TexDiffuse),2); // texture already bind

    const glm::mat3 mModel_sunDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                0.0f, 0.3f, 0.f,
                                                1.0f, 0.6f, 1.f);

    shader.setUniformMatrix(mProj2D * mModel_sunDebug);

    geom.drawcall(0, 1);
  }

  void clear()
  {
    shader.clearShader();
    geom.clearGPU();
  }
};

// Static scene ===============================================================

tre::modelStaticIndexed3D meshPlane;
tre::modelInstancedMesh   meshes;

static float rand01() { return float(std::rand() & 0xFFFF) / float(0xFFFF);  }

static constexpr float kBound = 100.f;

struct s_meshInstance
{
  glm::vec3 pos;
  glm::vec3 vel;
  glm::vec3 rotAxis;
  float     rot;
  float     rotVel;

  void initialize()
  {
    pos = (glm::vec3(rand01() * 2.f - 1.f, rand01(), rand01() * 2.f - 1.f)) * 0.9f * kBound;
    vel = (glm::vec3(rand01(), rand01(), rand01()) * 2.f - 1.f) * 0.5f;
    rotAxis = glm::normalize(glm::vec3(rand01() * 2.f - 1.f, 1.f, rand01() * 2.f - 1.f));
    rot = 0.f;
    rotVel = rand01() * 0.4f;
  }
  bool inBounds() const
  {
    return (pos.x > -kBound && pos.x < kBound &&
            pos.y > 0.f     && pos.y < kBound &&
            pos.z > -kBound && pos.z < kBound);
  }
};
std::vector<s_meshInstance> instances;

// ============================================================================

tre::windowContext             myWindow;
tre::windowContext::s_controls myControls;
tre::windowContext::s_view3D   myView3D(&myWindow);
tre::windowContext::s_timer    myTimings;

tre::shader shaderMaterialFlat;
tre::shader shaderMaterialInstanced;

tre::shader shaderDepth;
tre::shader shaderDepthInstanced;

tre::font          worldHUDFont;
tre::modelRaw2D    worldHUDModel;
tre::shader        shaderText2D;

s_shadowDebug sunShadow_debug;

tre::renderTarget_ShadowMap sunLight_ShadowMap;

// ============================================================================

int app_init()
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

#ifdef TRE_WITH_SDL2_IMAGE
  if (!myWindow.SDLImageInit(IMG_INIT_JPG))
    return -1;
 #endif

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Shadow", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - random generator

  srand(time(nullptr)); // TODO: have proper c++11 rand generators

  // load meshes

  meshPlane.setFlags(tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
  meshPlane.createPartFromPrimitive_square(glm::mat4(1.f), kBound * 2.f);
  meshPlane.loadIntoGPU();

  meshes.setFlags(tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
  meshes.setFlagsInstanced(tre::modelInstanced::VI_POSITION | tre::modelInstanced::VI_ORIENTATION);
  meshes.createPartFromPrimitive_box(glm::mat4(1.f), 1.f);
  meshes.loadIntoGPU();

  // render targets

  sunLight_ShadowMap.load(1024, 1024);

  // load HUD

  worldHUDFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  {
    static const char* txts[3] = { "FPS",
                                   "right clic: lock/unlock camera",
                                   "F5: show/hide render-target",
                                 };

    for (uint it = 0; it < 3; ++it)
    {
      tre::textgenerator::s_textInfo tInfo;
      tInfo.setupBasic(&worldHUDFont, txts[it], glm::vec2(0.f, -0.08f - 0.08f * it));
      tInfo.setupSize(0.06f);
      tInfo.m_fontPixelSize = 14;
      worldHUDModel.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text));
      tre::textgenerator::generate(tInfo, &worldHUDModel, it, 0, nullptr);
    }

    const float r = float(myWindow.m_resolutioncurrent.x) / float(myWindow.m_resolutioncurrent.y);

    worldHUDModel.loadIntoGPU();
  }

  // material (shaders)

  shaderMaterialFlat.setShadowSunSamplerCount(1);
  shaderMaterialFlat.loadShader(tre::shader::PRGM_3D,
                               tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN);

  shaderMaterialInstanced.setShadowSunSamplerCount(1);
  shaderMaterialInstanced.loadShader(tre::shader::PRGM_3D,
                                  tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN |
                                  tre::shader::PRGM_INSTANCED | tre::shader::PRGM_ORIENTATION);

  shaderDepth.loadShader(tre::shader::PRGM_3D_DEPTH, 0);

  shaderDepthInstanced.loadShader(tre::shader::PRGM_3D_DEPTH, tre::shader::PRGM_INSTANCED | tre::shader::PRGM_ORIENTATION);

  shaderText2D.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);

  // debug shadow

  sunShadow_debug.load();

  // End Init

  tre::checkLayoutMatch_Shader_Model(&shaderMaterialFlat, &meshPlane);
  tre::checkLayoutMatch_Shader_Model(&shaderMaterialInstanced, &meshes);

  tre::checkLayoutMatch_Shader_Model(&shaderDepth, &meshPlane);
  tre::checkLayoutMatch_Shader_Model(&shaderDepthInstanced, &meshes);

  tre::IsOpenGLok("main: initialization");

  myView3D.m_matView[3] = glm::vec4(0.f, -3.f, 0.f, 1.f);
  myView3D.setScreenBoundsMotion(true);
  myView3D.setKeyBinding(true);

  myTimings.initialize();

  instances.resize(128);
  for (auto &si : instances) si.initialize();

  return 0;
}

// ============================================================================

bool showMaps = false;

void app_update()
{
  // - global

  tre::shader::s_UBOdata_sunLight sunLight_Data;
  sunLight_Data.color = glm::vec3(0.8f);
  sunLight_Data.colorAmbiant = glm::vec3(0.1f);

  sunLight_Data.nShadow = 1; // TODO: cascaded shadow maps

  // - start frame

  SDL_Event event;

  myWindow.SDLEvent_newFrame();
  myControls.newFrame();
  myTimings.newFrame(60, myControls.m_pause);

  // event actions + updates -------

  {
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myControls.treatSDLEvent(event);

      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F5) showMaps = !showMaps;
    }

    if (myWindow.m_hasFocus) myView3D.treatControlEvent(myControls, myTimings.frametime);

    if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED) myView3D.setMouseBinding(!myView3D.m_mouseBound);
  } // end events

  // world simulation -------------------

  if (!myControls.m_pause)
  {

    // sun
    {
      const float sunTheta = 0.4 + 0.3f * std::sin(myTimings.scenetime * (6.28f * 0.02f));
      sunLight_Data.direction = -glm::vec3( 0.f, std::cos(sunTheta), std::sin(sunTheta) );
    }

    // meshes instances
    {
      for (auto &si : instances)
      {
        si.pos += si.vel * myTimings.frametime;
        si.rot += si.rotVel * myTimings.frametime;
        if (!si.inBounds()) si.initialize();
      }

      // fill GPU buffer
      meshes.resizeInstance(instances.size());
      TRE_ASSERT(meshes.layout().m_instancedPositions.m_stride == 16);
      glm::vec4 * bufferX4 = reinterpret_cast<glm::vec4*>(meshes.bufferInstanced()); // unsafe ...
      for (const auto &si : instances)
      {
        *bufferX4++ = glm::vec4(si.pos, 1.f);

        const glm::mat4 rotM = glm::rotate(glm::mat4(1.f), si.rot, si.rotAxis);
        *bufferX4++ = rotM[0];
        *bufferX4++ = rotM[1];
        *bufferX4++ = rotM[2];
      }
    }

    // shadow maps
    {
      sunLight_ShadowMap.setSceneBox(tre::s_boundbox(glm::vec3(-kBound*1.1f), glm::vec3(kBound*1.1f)));
      sunLight_ShadowMap.computeUBO_forMap(sunLight_Data, 0);
    }

    // prepare render ----------------------

    {
      tre::shader::updateUBO_sunLight(sunLight_Data);

      meshes.updateIntoGPU();
    }

    const glm::mat4 mPV = myWindow.m_matProjection3D * myView3D.m_matView;

    // shadow-map render pass --------------

    {
      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);

      sunLight_ShadowMap.bindForWritting();
      glClear(GL_DEPTH_BUFFER_BIT);

      const glm::mat4 localMPV = sunLight_ShadowMap.mProj() * sunLight_ShadowMap.mView();

      glUseProgram(shaderDepth.m_drawProgram);
      shaderDepth.setUniformMatrix(localMPV);
      meshPlane.drawcallAll();

      glUseProgram(shaderDepthInstanced.m_drawProgram);
      shaderDepthInstanced.setUniformMatrix(localMPV);
      meshes.drawInstanced(0, 0, instances.size());

      tre::IsOpenGLok("shadow render pass");
    }

    // opaque render pass ----------------

    {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);

      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D,sunLight_ShadowMap.depthHandle());

      glUseProgram(shaderMaterialFlat.m_drawProgram);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform4f(shaderMaterialFlat.getUniformLocation(tre::shader::uniColor), 0.6f, 0.6f, 0.6f, 1.f);
      glUniform2f(shaderMaterialFlat.getUniformLocation(tre::shader::uniMat), 0.f, 0.98f);
      shaderMaterialFlat.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
      meshPlane.drawcallAll();

      glUseProgram(shaderMaterialInstanced.m_drawProgram);
      glUniform1i(shaderMaterialInstanced.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform4f(shaderMaterialInstanced.getUniformLocation(tre::shader::uniColor), 1.f, 0.6f, 0.6f, 1.f);
      glUniform2f(shaderMaterialInstanced.getUniformLocation(tre::shader::uniMat), 0.f, 0.5f);
      shaderMaterialInstanced.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
      meshes.drawInstanced(0, 0, instances.size());

      tre::IsOpenGLok("opaque render pass");
    }

    // UI-render pass -------------

    {
      glDisable(GL_DEPTH_TEST);

      if (showMaps)
      {
        glDisable(GL_BLEND);
        sunShadow_debug.draw(myWindow.m_matProjection2D);
      }

      if (true)
      {
        glEnable(GL_BLEND);
        char txtFPS[128];
        std::snprintf(txtFPS, 127, "%03d fps", int(1.f/myTimings.frametime));
        tre::textgenerator::s_textInfo tInfo;
        tInfo.setupBasic(&worldHUDFont, txtFPS, glm::vec2(0.f, -0.08f - 0.08f * 0));
        tInfo.setupSize(0.06f);
        tInfo.m_fontPixelSize = 14;
        worldHUDModel.resizePart(0, tre::textgenerator::geometry_VertexCount(tInfo.m_text));
        tre::textgenerator::generate(tInfo, &worldHUDModel, 0, 0, nullptr);

        worldHUDModel.colorizePart(2, showMaps ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(0.8f));

        worldHUDModel.updateIntoGPU();

        glm::mat3 mViewModel_hud = glm::mat3(1.f);
        mViewModel_hud[0][0] = 1.f;
        mViewModel_hud[1][1] = 1.f;
        mViewModel_hud[2][0] = -0.98f / myWindow.m_matProjection2D[0][0];
        mViewModel_hud[2][1] =  0.98f;

        glUseProgram(shaderText2D.m_drawProgram);
        shaderText2D.setUniformMatrix(myWindow.m_matProjection2D * mViewModel_hud);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,worldHUDFont.get_texture().m_handle);
        glUniform1i(shaderText2D.getUniformLocation(tre::shader::TexDiffuse),3);

        worldHUDModel.drawcallAll();
      }

      glEnable(GL_BLEND);

      tre::IsOpenGLok("UI render pass - draw UI");
    }

    // end render pass --------------

    myTimings.endFrame_beforeGPUPresent();

    SDL_GL_SwapWindow( myWindow.m_window );
  }
}

// ============================================================================

void app_quit()
{
  // Finalize

  meshPlane.clearGPU();
  meshes.clearGPU();

  worldHUDModel.clearGPU();
  worldHUDFont.clear();

  shaderMaterialFlat.clearShader();
  shaderMaterialInstanced.clearShader();
  shaderDepth.clearShader();
  shaderDepthInstanced.clearShader();

  shaderText2D.clearShader();

  tre::shader::clearUBO();

  sunShadow_debug.clear();

  sunLight_ShadowMap.clear();

  myWindow.OpenGLQuit();
  myWindow.SDLImageQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");
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
