
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model_importer.h"
#include "tre_ui.h"
#include "tre_font.h"
#include "tre_windowContext.h"

#include <math.h>
#include <string>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

int main(int argc, char **argv)
{
  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Lighting", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // Arguments

  std::string meshFilePath = TESTIMPORTPATH "resources/objects.obj";

  if (argc > 1) meshFilePath = argv[1];

  // Resources: Mesh

  tre::modelStaticIndexed3D meshes(tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_UV);
  glm::mat4 mModel;
  {
    if (!tre::modelImporter::addFromWavefront(meshes, meshFilePath))
    {
      TRE_LOG("Fail to load the mesh from file " << meshFilePath << ". Abort.");
      myWindow.OpenGLQuit();
      myWindow.SDLQuit();
      return -3;
    }
    meshes.loadIntoGPU();
  }

  // Resources: Textures

  tre::texture texAsphaltColor;
  texAsphaltColor.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_COLOR.bmp"), tre::texture::MMASK_FORCE_NO_ALPHA | tre::texture::MMASK_MIPMAP, true);

  tre::texture texAsphaltNormal;
  texAsphaltNormal.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_NRM.bmp"), tre::texture::MMASK_FORCE_NO_ALPHA | tre::texture::MMASK_MIPMAP, true);

  tre::texture texSkyboxColor;
  texSkyboxColor.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords"), tre::texture::MMASK_MIPMAP, true);

  // Material (shader)

  tre::shader shaderPhong;
  shaderPhong.loadShader(tre::shader::PRGM_3D,
                         tre::shader::PRGM_UNICOLOR |
                         tre::shader::PRGM_LIGHT_SUN |
                         tre::shader::PRGM_UNIPHONG);

  tre::shader shaderBRDF;
  shaderBRDF.loadShader(tre::shader::PRGM_3D,
                        tre::shader::PRGM_UNICOLOR |
                        tre::shader::PRGM_LIGHT_SUN |
                        tre::shader::PRGM_UNIBRDF);

  tre::shader shaderFresnelValue;
  {
    tre::shader::s_layout layout(tre::shader::PRGM_3D, tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_UNIBRDF);

    const char * srcFrag = "void main()\n"
                           "{\n"
                           "  vec3 N = normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n"
                           "  vec3 L = - normalize((MView * vec4(m_sunlight.direction, 0.f)).xyz);\n"
                           "  vec3 V = - normalize((MView * vec4(pixelPosition, 1.f)).xyz);\n"
                           "  float NdotL = max(dot(N, L), 0.f); // Light-incidence\n"
                           "  vec3 H = normalize(L + V); // Half-way vector\n"
                           "  float LdotH = max(dot(V, H), 0.f);\n"
                           "  vec3 F0     = mix(vec3(0.04f), uniColor.xyz, uniBRDF.x);;\n"
                           "  vec3 kRefl  = _fresnelSchlick(F0, LdotH); // ratio of reflected light over total light\n"
                           "  color.xyz = kRefl * m_sunlight.color * NdotL;\n"
                           "  color.w = 1.f;\n"
                           "}\n";

    shaderFresnelValue.loadCustomShader(layout, srcFrag, "FresnelValue");
  }

  tre::shader shaderScreenSpaceNormal;
  {
    tre::shader::s_layout layout(tre::shader::PRGM_3D /*, tre::shader::PRGM_MAPNORMAL*/);
    layout.hasBUF_Normal = true;
    layout.hasPIX_Normal = true;
    layout.hasOUT_Color0 = true;

    const char * srcFrag = "void main()\n"
                           "{\n"
                           "  color.xyz = 0.5f + 0.5f * normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n"
                           "  color.w = 1.f;\n"
                           "}\n";

    shaderScreenSpaceNormal.loadCustomShader(layout, srcFrag, "ScreenSpaceNormal");
  }

  // Post Effects

  tre::renderTarget rtMultisampled(tre::renderTarget::RT_COLOR | tre::renderTarget::RT_DEPTH | tre::renderTarget::RT_MULTISAMPLED);
  rtMultisampled.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::renderTarget rtResolveMSAA(tre::renderTarget::RT_COLOR | tre::renderTarget::RT_COLOR_SAMPLABLE);
  rtResolveMSAA.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::postFX_ToneMapping postEffectToneMapping;
  postEffectToneMapping.load();

  // Scene variables + initialisation

  glm::vec4 mViewEulerAndDistance = glm::vec4(0.f, 0.f, 0.f, 4.f);

  unsigned modelPart = 0;
  const unsigned NmodelPart = meshes.partCount();

  unsigned shaderMode = 1;
  const unsigned NshaderMode = 4;
  tre::shader* listShader[NshaderMode] = { &shaderPhong, &shaderBRDF, &shaderFresnelValue, &shaderScreenSpaceNormal };
  std::string listShaderName[NshaderMode] = { "Phong", "BRDF", "Fresnel", "Normal"};

  glm::vec4 colorDiffuse = glm::vec4(1.f,0.f,0.f,1.f);
  float materialMetalness = 0.01f;
  float materialRoughness = 0.1f;

  const glm::vec3 sunLightColor = glm::vec3(0.9f,1.3f,0.9f);
  const glm::vec3 ambiantLightColor = glm::vec3(0.9f,0.9f,1.3f);
  tre::shader::s_UBOdata_sunLight sunLight;
  sunLight.direction = glm::normalize(glm::vec3(-0.243f,-0.970f,0.f));
  sunLight.color = 0.9f * sunLightColor;
  sunLight.colorAmbiant = 0.2f * ambiantLightColor;

  // U.I

  tre::font worldFont;
  worldFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  tre::baseUI2D worldUI;
  tre::ui::window *worldWin;
  {
    worldUI.set_defaultFont(&worldFont);

    worldUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

    worldWin = worldUI.create_window();
    worldWin->set_colormask(glm::vec4(0.7f,1.f,0.7f,0.8f));
    worldWin->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    worldWin->set_cellMargin(tre::ui::s_size(4, tre::ui::SIZE_PIXEL));
    worldWin->set_topbar("Parameters", true, false);
    {
      glm::mat3 wmat(1.f);
      wmat[2][0] = -1.f / myWindow.m_matProjection2D[0][0] + 0.01f;
      wmat[2][1] = 0.99f;
      worldWin->set_mat3(wmat);
    }
    worldWin->set_layoutGrid(14,2);

    worldWin->create_widgetText(0, 0)->set_text("model (F1:F4):")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(0, 1); // model name

    worldWin->create_widgetText(1, 0)->set_text("diffuse R");
    tre::ui::widget * wCR = worldWin->create_widgetBar(1, 1)->set_value(colorDiffuse.r)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCR->wcb_modified_ongoing = [&colorDiffuse](tre::ui::widget* myself) { colorDiffuse.r = static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(2, 0)->set_text("diffuse G");
    tre::ui::widget * wCG = worldWin->create_widgetBar(2, 1)->set_value(colorDiffuse.g)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCG->wcb_modified_ongoing = [&colorDiffuse](tre::ui::widget* myself) { colorDiffuse.g = static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(3, 0)->set_text("diffuse B");
    tre::ui::widget * wCB = worldWin->create_widgetBar(3, 1)->set_value(colorDiffuse.b)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCB->wcb_modified_ongoing = [&colorDiffuse](tre::ui::widget* myself) { colorDiffuse.b = static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(4, 0)->set_text("diffuse Map (F9)");
    worldWin->create_widgetBoxCheck(4, 1)->set_value(false);

    worldWin->create_widgetText(5, 0)->set_text("normal Map");
    worldWin->create_widgetBoxCheck(5, 1)->set_value(false);

    worldWin->create_widgetText(6, 0)->set_text("shader (F5:F8):")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(6, 1); // shader name

    worldWin->create_widgetText(7, 0, 1, 2)->set_text("material:")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));

    worldWin->create_widgetText(8, 0)->set_text("metalness");
    tre::ui::widget * wMM = worldWin->create_widgetBar(8, 1)->set_value(materialMetalness)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wMM->wcb_modified_ongoing = [&materialMetalness](tre::ui::widget* myself) { materialMetalness = static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(9, 0)->set_text("roughness");
    tre::ui::widget * wMR = worldWin->create_widgetBar(9, 1)->set_value(materialRoughness)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wMR->wcb_modified_ongoing = [&materialRoughness](tre::ui::widget* myself) { materialRoughness = static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(10, 0, 1, 2)->set_text("environment (F9:F12):")->set_fontsizeModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));

    worldWin->create_widgetText(11, 0)->set_text("light intensity");
    tre::ui::widget * wEI = worldWin->create_widgetBar(11, 1)->set_value(0.9f)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wEI->wcb_modified_ongoing = [&sunLight, &sunLightColor](tre::ui::widget* myself) { sunLight.color = sunLightColor * static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(12, 0)->set_text("ambiant intensity");
    tre::ui::widget * wEA = worldWin->create_widgetBar(12, 1)->set_value(0.2f)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wEA->wcb_modified_ongoing = [&sunLight, &ambiantLightColor](tre::ui::widget* myself) { sunLight.colorAmbiant = ambiantLightColor * static_cast<tre::ui::widgetBar*>(myself)->get_value(); };

    worldWin->create_widgetText(13, 0)->set_text("sky-box Map (F10)");
    worldWin->create_widgetBoxCheck(13, 1)->set_value(false);

    // load
    worldUI.loadIntoGPU();
    worldUI.loadShader();
  }

  // End Init

  tre::IsOpenGLok("main: initialization");

  for (unsigned i = 0; i < NshaderMode; ++i)
    tre::checkLayoutMatch_Shader_Model(listShader[i], &meshes);

  // - event and time variables

  SDL_Event event;

  // MAIN LOOP

  while(!myControls.m_quit)
  {
    // event actions + updates -------

    myWindow.SDLEvent_newFrame();
    myControls.newFrame();

    worldWin->get_widget(1, 1)->set_isactive(shaderMode == 1 || shaderMode == 2);
    worldWin->get_widget(2, 1)->set_isactive(shaderMode == 1 || shaderMode == 2 || shaderMode == 0);

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myControls.treatSDLEvent(event);
      worldUI.acceptEvent(event);

      if (event.type == SDL_KEYDOWN)
      {
        if      (event.key.keysym.sym == SDLK_F1) { modelPart = 0; }
        else if (event.key.keysym.sym == SDLK_F2) { modelPart = modelPart == 0 ? NmodelPart - 1 : modelPart - 1; }
        else if (event.key.keysym.sym == SDLK_F3) { modelPart = modelPart == NmodelPart - 1 ? 0 : modelPart + 1; }
        else if (event.key.keysym.sym == SDLK_F4) { modelPart = NmodelPart - 1; }
        else if (event.key.keysym.sym == SDLK_F5) { shaderMode = 0; }
        else if (event.key.keysym.sym == SDLK_F6) { shaderMode = shaderMode == 0 ? NshaderMode - 1 : shaderMode - 1; }
        else if (event.key.keysym.sym == SDLK_F7) { shaderMode = shaderMode == NshaderMode - 1 ? 0 : shaderMode + 1; }
        else if (event.key.keysym.sym == SDLK_F8) { shaderMode = NshaderMode - 1; }
      }
    }

    if (myControls.m_keyUP)    mViewEulerAndDistance.x += 0.01f;
    if (myControls.m_keyDOWN)  mViewEulerAndDistance.x -= 0.01f;
    if (myControls.m_keyLEFT)  mViewEulerAndDistance.y += 0.01f;
    if (myControls.m_keyRIGHT) mViewEulerAndDistance.y -= 0.01f;

    if (myControls.m_keyCTRL)  mViewEulerAndDistance.w += 0.1f;
    if (myControls.m_keySHIFT) mViewEulerAndDistance.w -= 0.1f;

    if (myWindow.m_viewportResized)
    {
      worldUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
      rtMultisampled.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
      rtResolveMSAA.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

      glm::mat3 wmat(1.f);
      wmat[2][0] = -1.f / myWindow.m_matProjection2D[0][0] + 0.01f;
      wmat[2][1] = 0.99f;
      worldWin->set_mat3(wmat);
    }

    // compute the view

    glm::mat4 mView = glm::eulerAngleXYZ(mViewEulerAndDistance.x,
                                         mViewEulerAndDistance.y,
                                         mViewEulerAndDistance.z);
    mView[3][0] = 0.f;
    mView[3][1] = 0.f;
    mView[3][2] = -mViewEulerAndDistance.w;

    // main render pass -------------

    tre::shader::updateUBO_sunLight(sunLight);

    glEnable(GL_DEPTH_TEST);

    rtMultisampled.bindForWritting();
    glViewport(0, 0, rtMultisampled.w(), rtMultisampled.h());
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    tre::shader & curShader = * listShader[shaderMode];

    const glm::mat4 mPV(myWindow.m_matProjection3D * mView);

    mModel = glm::mat4(1.f);

    glUseProgram(curShader.m_drawProgram);

    curShader.setUniformMatrix(mPV * mModel, mModel, mView);

    if (curShader.layout().hasUNI_uniColor)
    {
      glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(colorDiffuse));
    }

    if (curShader.layout().hasUNI_uniPhong)
    {
      glUniform3f(curShader.getUniformLocation(tre::shader::uniPhong), 2.f / (0.99f * materialRoughness * materialRoughness + 0.01f), 0.f, 0.5f);
    }

    if (curShader.layout().hasUNI_uniBRDF)
    {
      glUniform2f(curShader.getUniformLocation(tre::shader::uniBRDF), materialMetalness, materialRoughness);
    }

    meshes.drawcall(modelPart, 1);

    tre::IsOpenGLok("main render pass - draw Object");

    // post-effects -------------

    glDisable(GL_DEPTH_TEST);

    rtMultisampled.resolve(rtResolveMSAA);

    postEffectToneMapping.resolveToneMapping(rtResolveMSAA.colorHandle(), myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

    tre::IsOpenGLok("post-effect pass");

    // UI-render pass -------------

    glDisable(GL_DEPTH_TEST);

    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    worldWin->get_widgetText(0,1)->set_text(meshes.partInfo(modelPart).m_name);
    worldWin->get_widgetText(6,1)->set_text(listShaderName[shaderMode]);

    worldUI.updateIntoGPU();

    worldUI.draw();

    tre::IsOpenGLok("UI pass");

    // end render pass --------------

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  TRE_LOG("Main loop exited");

  // Finalize
  meshes.clearGPU();

  worldUI.clear();
  worldUI.clearGPU();
  worldUI.clearShader();
  worldFont.clear();

  shaderPhong.clearShader();
  shaderBRDF.clearShader();
  shaderFresnelValue.clearShader();
  shaderScreenSpaceNormal.clearShader();

  tre::shader::clearUBO();

  texAsphaltColor.clear();
  texAsphaltNormal.clear();
  texSkyboxColor.clear();

  rtMultisampled.clear();
  rtResolveMSAA.clear();
  postEffectToneMapping.clear();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
