
#include "shader.h"
#include "rendertarget.h"
#include "model.h"
#include "model_tools.h"
#include "textgenerator.h"
#include "profiler.h"
#include "font.h"
#include "windowHelper.h"

#include <math.h>
#include <stdlib.h> // rand,srand
#include <time.h>   // time
#include <string>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  tre::windowHelper myWindow;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "test Demo scene", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

  // - random generator
  srand(time(nullptr));

  // load meshes and textures

  tre::modelStaticIndexed3D worldSkyboxModel;
  {
    const GLfloat cubepos[] = {
      -10.f, -10.f,  10.f,
      -10.f,  10.f,  10.f,
      -10.f, -10.f, -10.f,
      -10.f,  10.f, -10.f,
       10.f, -10.f,  10.f,
       10.f,  10.f,  10.f,
       10.f, -10.f, -10.f,
       10.f,  10.f, -10.f };
    const GLuint cubeind[] = {
       1, 2, 0,       3, 6, 2,
       7, 4, 6,       5, 0, 4,
       6, 0, 2,       3, 5, 7,
       1, 3, 2,       3, 7, 6,
       7, 5, 4,       5, 1, 0,
       6, 4, 0,       3, 1, 5 };

    worldSkyboxModel.createPartFromIndexes(cubeind, 36, cubepos, nullptr);

    worldSkyboxModel.loadIntoGPU();
  }

  tre::texture worldSkyBoxTex;
  {
    const std::array<SDL_Surface*, 6> cubeFaces = { tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.xpos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.xneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.ypos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.yneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.zpos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_inside_UVcoords.zneg.bmp"), };
    worldSkyBoxTex.loadCube(cubeFaces, tre::texture::MMASK_MIPMAP | tre::texture::MMASK_COMPRESS, true);
  }

  tre::modelStaticIndexed3D worldMesh(tre::modelStaticIndexed3D::VB_NORMAL |
                                      tre::modelStaticIndexed3D::VB_TANGENT |
                                      tre::modelStaticIndexed3D::VB_UV);
  {
    if (!worldMesh.loadfromWavefront(TESTIMPORTPATH "resources/scenes.obj"))
    {
      TRE_LOG("Fail to load scenes.obj.");
      myWindow.OpenGLQuit();
      myWindow.SDLQuit();
      return -3;
    }
    std::vector<std::string> reOrg;
    reOrg.push_back(std::string("SceneGround"));
    reOrg.push_back(std::string("SceneWall"));
    reOrg.push_back(std::string("SceneCube"));
    worldMesh.reorganizeParts(reOrg);
    tre::modelTools::computeTangentFromUV(worldMesh.layout(), worldMesh.partInfo(0));
    worldMesh.mergeParts(1,2);
    worldMesh.loadIntoGPU();
  }

  tre::modelStaticIndexed3D worldObjects(tre::modelStaticIndexed3D::VB_NORMAL);
  {
    if (!worldObjects.loadfromWavefront(TESTIMPORTPATH "resources/objects.obj", TESTIMPORTPATH "resources/objects.mtl"))
    {
      TRE_LOG("Fail to load objects.obj");
      myWindow.OpenGLQuit();
      myWindow.SDLQuit();
      return -3;
    }
    std::vector<std::string> reOrg;
    reOrg.push_back("CubeFlat");
    reOrg.push_back("Icosphere");
    if (!worldObjects.reorganizeParts(reOrg))
    {
      TRE_LOG("Fail to get the mesh parts in objects.obj");
      myWindow.OpenGLQuit();
      myWindow.SDLQuit();
      return -4;
    }
    worldObjects.loadIntoGPU();
  }

  tre::texture worldGroundColor;
  worldGroundColor.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_COLOR.bmp"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC, true);

  tre::texture worldGroundNormal;
  worldGroundNormal.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_NRM.bmp"), tre::texture::MMASK_MIPMAP, true);

  tre::texture worldGroundMetalRough;
  {
    SDL_Surface *texM = tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_METALLIC.bmp");
    SDL_Surface *texR = tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/Asphalt_004_ROUGH.bmp");
    if (texM != nullptr && texR != nullptr)
    {
      SDL_Surface *texMR = tre::texture::combine(texM, texR);
      worldGroundMetalRough.load(texMR, tre::texture::MMASK_RG_ONLY | tre::texture::MMASK_MIPMAP, true);
    }
    if (texM != nullptr) SDL_FreeSurface(texM);
    if (texR != nullptr) SDL_FreeSurface(texR);
  }

  tre::texture worldTex_UVcoords;
  {
    const std::array<SDL_Surface*, 6> cubeFaces = { tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.xpos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.xneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.ypos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.yneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.zpos.bmp"),
                                                    tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/cubemap_outside_UVcoords.zneg.bmp"), };
    worldTex_UVcoords.loadCube(cubeFaces, tre::texture::MMASK_MIPMAP, true);
  }

  tre::modelInstancedBillboard worldParticlesBB(tre::modelInstanced::VI_COLOR | tre::modelInstanced::VI_ROTATION);
  worldParticlesBB.createBillboard();
  worldParticlesBB.loadIntoGPU();

  tre::modelInstancedMesh worldParticlesMesh(tre::modelInstanced::VI_ORIENTATION | tre::modelInstanced::VI_COLOR | tre::modelInstancedMesh::VB_NORMAL);
  worldParticlesMesh.createPartFromPrimitive_box(glm::mat4(1.f), 0.5f);
  worldParticlesMesh.loadIntoGPU();

  tre::texture  worldParticlesTex;
  worldParticlesTex.load(tre::texture::loadTextureFromBMP(TESTIMPORTPATH "resources/quad.bmp"), 0, true);

  struct s_particleStream
  {
    std::vector<glm::vec4> m_pos;   // pos(x,y,z) + size(w)
    std::vector<glm::vec4> m_vel;   // dpos(x,y,z) + dsize(w)
    std::vector<glm::vec2> m_life;  // life + invLifeRatio
    std::vector<glm::vec4> m_rot;   // scalar rotation or quaternion
    std::vector<glm::vec4> m_color; // color

    const bool m_withRotation;
    const bool m_withColor;

    s_particleStream(bool withRotation, bool withColor) : m_withRotation(withRotation), m_withColor(withColor) {}

    inline std::size_t size() const { return m_pos.size(); }

    void resize(std::size_t count)
    {
      m_pos.resize(count);
      m_vel.resize(count);
      m_life.resize(count, glm::vec2(2.f, 1.f));
      if (m_withRotation)
        m_rot.resize(count);
      if (m_withColor)
        m_color.resize(count);
    }
  };

  s_particleStream worldParticlesBBStream(true, false);
  worldParticlesBBStream.resize(1024);

  s_particleStream worldParticlesMeshStream(true, true);
  worldParticlesMeshStream.resize(512);

  tre::font          worldHUDFont;
  {
    SDL_Surface *surf;
    tre::font::s_fontMap map;
    tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88", surf, map);
    worldHUDFont.load({ surf }, { map}, true);
  }

  tre::modelRaw2D    worldHUDModel;
  {
    static const char* txts[5] = { "FPS",
                                   "right clic: lock/unlock camera",
                                   "F5: show/hide shadow maps",
                                   "F6: enable/disable blur",
                                   "F7: enable/disable MSAA"
                                 };

    for (uint it = 0; it < 5; ++it)
    {
      tre::textgenerator::s_textInfo tInfo;
      tInfo.setupBasic(&worldHUDFont, 0.06f, txts[it], glm::vec2(0.f, -0.08f - 0.08f * it));
      worldHUDModel.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text));
      tre::textgenerator::generate(tInfo, &worldHUDModel, it, 0, nullptr);
    }

    worldHUDModel.loadIntoGPU();
  }

  // material (shaders)

  tre::shader  shaderSkybox;
  shaderSkybox.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_CUBEMAPED);

  tre::shader shaderMaterialUni;
  shaderMaterialUni.setShadowSunSamplerCount(1);
  shaderMaterialUni.setShadowPtsSamplerCount(1);
  shaderMaterialUni.loadShader(tre::shader::PRGM_3D,
                               tre::shader::PRGM_UNICOLOR |
                               tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                               tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                               tre::shader::PRGM_UNIBRDF);

  tre::shader shaderMaterialMapped;
  shaderMaterialMapped.setShadowSunSamplerCount(1);
  shaderMaterialMapped.setShadowPtsSamplerCount(1);
  shaderMaterialMapped.loadShader(tre::shader::PRGM_3D,
                                  tre::shader::PRGM_TEXTURED | tre::shader::PRGM_MAPNORMAL |
                                  tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                                  tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                                  tre::shader::PRGM_MAPBRDF);

  tre::shader::s_UBOdata_sunLight sunLight_Data;
  sunLight_Data.color = glm::vec3(0.9f,0.9f,0.9f);
  sunLight_Data.colorAmbiant = glm::vec3(0.1f,0.1f,0.1f);

  tre::shader::s_UBOdata_sunShadow sunShadow_Data;
  sunShadow_Data.nShadow = 1;

  tre::shader::s_UBOdata_ptstLight ptsLight_Data;
  ptsLight_Data.nLight = 1;

  tre::shader::s_UBOdata_ptsShadow ptsShadow_Data;

  tre::shader shaderShadow;
  shaderShadow.loadShader(tre::shader::PRGM_3D_DEPTH,0);

  tre::shader  shaderSphere;
  shaderSphere.setShadowSunSamplerCount(1);
  shaderSphere.setShadowPtsSamplerCount(1);
  shaderSphere.loadShader(tre::shader::PRGM_3D,
                          tre::shader::PRGM_CUBEMAPED |
                          tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                          tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                          tre::shader::PRGM_UNIBRDF);

  tre::shader shaderInstancedBB;
  shaderInstancedBB.setShadowSunSamplerCount(1);
  shaderInstancedBB.loadShader(tre::shader::PRGM_2Dto3D,
                               tre::shader::PRGM_TEXTURED |
                               tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                               tre::shader::PRGM_UNIPHONG |
                               tre::shader::PRGM_INSTANCED | tre::shader::PRGM_INSTCOLOR | tre::shader::PRGM_ROTATION |
                               tre::shader::PRGM_SOFT);

  tre::shader shaderInstancedMesh;
  shaderInstancedMesh.setShadowSunSamplerCount(1);
  shaderInstancedMesh.loadShader(tre::shader::PRGM_3D,
                                 tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN | tre::shader::PRGM_NO_SELF_SHADOW |
                                 tre::shader::PRGM_INSTANCED | tre::shader::PRGM_INSTCOLOR | tre::shader::PRGM_ORIENTATION);

  tre::shader shaderSolid2D;
  shaderSolid2D.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR);

  tre::shader shaderText2D;
  shaderText2D.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_TEXTURED);

  // debug shadow

  struct
  {
    tre::shader     shader;
    tre::shader     shaderCubeFace;
    tre::modelRaw2D geom;
  } sunShadow_debug;
  {
    tre::shader::s_layout shLayout(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);

    const char * SourceShader2DTexturedDepth_FragmentMain =
    "void main(){\n"
    "  float d = texture(TexDiffuse, pixelUV).r;\n"
    "  color = vec4(d,0.f,0.1f,1.f);\n"
    "}\n";
    sunShadow_debug.shader.loadCustomShader(shLayout,
                                            SourceShader2DTexturedDepth_FragmentMain,
                                            "2DTexDepth_debug");

    tre::shader::s_layout shLayoutCubeFace(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);
    shLayoutCubeFace.hasSMP_Diffuse = false;
    shLayoutCubeFace.hasSMP_Cube = true;

    const char * SourceShaderCubeTexturedDepth_FragmentMain =
    "uniform int iface;\n"
    "void main(){\n"
    "  vec2 uv = pixelUV * 2.f - 1.f;\n"
    "  vec3 uvw = vec3(0.f, 0.f, 0.f);\n"
    "  if (iface == 0) uvw = vec3(1.f, uv.y, uv.x);\n"
    "  if (iface == 1) uvw = vec3(-1.f, uv.y, -uv.x);\n"
    "  if (iface == 2) uvw = vec3(uv.x, 1.f, uv.y);\n"
    "  if (iface == 3) uvw = vec3(uv.x, -1.f, -uv.y);\n"
    "  if (iface == 4) uvw = vec3(-uv.x, uv.y, 1.f);\n"
    "  if (iface == 5) uvw = vec3(uv.x, uv.y, -1.f);\n"
    "  float d = texture(TexCube, uvw).r;\n"
    "  float dRemap = exp(30.f*(d-1.f)) * d;\n"
    "  color = vec4(dRemap,0.f,0.1f,1.f);\n"
    "}\n";
    sunShadow_debug.shaderCubeFace.loadCustomShader(shLayoutCubeFace,
                                                     SourceShaderCubeTexturedDepth_FragmentMain,
                                                     "CubeTexDepth_debug");


    // squared-map
    sunShadow_debug.geom.createPart(6);
    sunShadow_debug.geom.fillDataRectangle(0, 0, glm::vec4(-1.f, -1.f, 1.f, 1.f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));

    // cube-map
    sunShadow_debug.geom.createPart(6); //+X
    sunShadow_debug.geom.fillDataRectangle(1, 0, glm::vec4( 0.0f, 0.0f, 0.5f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    sunShadow_debug.geom.createPart(6); //-X
    sunShadow_debug.geom.fillDataRectangle(2, 0, glm::vec4(-1.0f, 0.0f, -0.5f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    sunShadow_debug.geom.createPart(6); //+Y
    sunShadow_debug.geom.fillDataRectangle(3, 0, glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    sunShadow_debug.geom.createPart(6); //-Y
    sunShadow_debug.geom.fillDataRectangle(4, 0, glm::vec4(-0.5f, -0.5f, 0.0f, 0.f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    sunShadow_debug.geom.createPart(6); //+Z
    sunShadow_debug.geom.fillDataRectangle(5, 0, glm::vec4(0.5f, 0.0f, 1.0f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    sunShadow_debug.geom.createPart(6); //-Z
    sunShadow_debug.geom.fillDataRectangle(6, 0, glm::vec4(-0.5f, 0.0f, 0.0f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));

    sunShadow_debug.geom.loadIntoGPU();
  }

  // Profiler

  tre::profiler_updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
  {
    glm::mat3 mModel_prof = glm::mat3(1.f);
    mModel_prof[0][0] = 1.f;
    mModel_prof[1][1] = 1.f;
    mModel_prof[2][0] =  -0.95f / myWindow.m_matProjection2D[0][0];
    mModel_prof[2][1] =  -0.8f;
    tre::profiler_updateModelMatrix(mModel_prof);
  }

  tre::profiler_loadShader();
  tre::profiler_loadIntoGPU(&worldHUDFont);

  tre::profiler_initThread();

  // Render-targets and Post-Effects

  tre::renderTarget_ShadowMap sunLight_ShadowMap;
  sunLight_ShadowMap.load(2048,2048);
  sunLight_ShadowMap.setSceneBox(worldMesh.partInfo(0).m_bbox + worldMesh.partInfo(1).m_bbox);

  tre::renderTarget_ShadowCubeMap ptsLight_ShadowMap;
  ptsLight_ShadowMap.load(1024);
  ptsLight_ShadowMap.setRenderingLimits(0.2f, 20.f);

  tre::renderTarget rtMultisampled(tre::renderTarget::RT_COLOR_AND_DEPTH | tre::renderTarget::RT_MULTISAMPLED /*| tre::renderTarget::RT_COLOR_HDR : TODO: fix PBR material with Roughness = 0 that causes visual glitch.*/);
  const bool canMSAA = rtMultisampled.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::renderTarget rtResolveMSAA(tre::renderTarget::RT_COLOR_AND_DEPTH | tre::renderTarget::RT_SAMPLABLE /*| tre::renderTarget::RT_COLOR_HDR : TODO: fix PBR material with Roughness = 0 that causes visual glitch.*/);
  rtResolveMSAA.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::postFX_Blur postEffectBlur(3, 7, false /*true: TODO: have tre::renderTarget::RT_COLOR_HDR*/);
  postEffectBlur.set_threshold(0.7f);
  postEffectBlur.set_multiplier(2.f);
  postEffectBlur.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::renderTarget rtAfterBlur(tre::renderTarget::RT_COLOR | tre::renderTarget::RT_COLOR_SAMPLABLE /*| tre::renderTarget::RT_COLOR_HDR : TODO: fix PBR material with Roughness = 0 that causes visual glitch.*/);
  rtAfterBlur.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::postFX_ToneMapping postEffectToneMapping;
  postEffectToneMapping.load();

  // End Init

  tre::IsOpenGLok("main: initialization");

  tre::checkLayoutMatch_Shader_Model(&shaderShadow, &worldMesh);
  tre::checkLayoutMatch_Shader_Model(&shaderShadow, &worldObjects);
  tre::checkLayoutMatch_Shader_Model(&shaderSkybox, &worldSkyboxModel);
  tre::checkLayoutMatch_Shader_Model(&shaderMaterialMapped, &worldMesh);
  tre::checkLayoutMatch_Shader_Model(&shaderMaterialUni, &worldMesh);
  tre::checkLayoutMatch_Shader_Model(&shaderMaterialUni, &worldObjects);
  tre::checkLayoutMatch_Shader_Model(&shaderSphere, &worldObjects);
  tre::checkLayoutMatch_Shader_Model(&shaderInstancedBB, &worldParticlesBB);
  tre::checkLayoutMatch_Shader_Model(&shaderInstancedMesh, &worldParticlesMesh);

  // - event and time variables

  SDL_Event event;

  myWindow.m_timing.initialize();

  myWindow.m_view3D.m_matView[3] = glm::vec4(0.f, -3.f, -9.8f, 1.f);
  myWindow.m_view3D.setScreenBoundsMotion(true);
  myWindow.m_view3D.setKeyBinding(true);

  bool showShadowMaps = false;
  bool withMSAA = canMSAA;
  bool withBlur = true;

  // MAIN LOOP

  while(!myWindow.m_controls.m_quit)
  {
    tre::profiler_newFrame();

    // event actions + updates -------

    {
      TRE_PROFILEDSCOPE("events", ev)

      myWindow.m_controls.newFrame();
      myWindow.m_timing.newFrame(0, myWindow.m_controls.m_pause);

      while(SDL_PollEvent(&event) == 1)
      {
        myWindow.SDLEvent_onWindow(event);
        myWindow.m_controls.treatSDLEvent(event);

        tre::profiler_acceptEvent(event);

        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F5) showShadowMaps = !showShadowMaps;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F6) withBlur = !withBlur;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F7) withMSAA = !withMSAA && canMSAA;
      }

      if (myWindow.m_controls.m_hasFocus)
        myWindow.m_view3D.treatControlEvent(myWindow.m_controls, myWindow.m_timing.frametime);

      if (myWindow.m_controls.m_mouseRIGHT & myWindow.m_controls.MASK_BUTTON_RELEASED)
        myWindow.m_view3D.setMouseBinding(!myWindow.m_view3D.m_mouseBound);

      if (myWindow.m_controls.m_viewportResized)
      {
        rtMultisampled.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
        rtResolveMSAA.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
        postEffectBlur.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
        rtAfterBlur.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
        tre::profiler_updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

        {
          glm::mat3 mModel_prof = glm::mat3(1.f);
          mModel_prof[0][0] = 1.f;
          mModel_prof[1][1] = 1.f;
          mModel_prof[2][0] =  -0.95f / myWindow.m_matProjection2D[0][0];
          mModel_prof[2][1] =  -0.8f;
          tre::profiler_updateModelMatrix(mModel_prof);
        }
      }
    } // end events

    // world simulation -------------------

    if (!myWindow.m_controls.m_pause)
    {
      TRE_PROFILEDSCOPE("particles", particles);

      // particle evolve
      {
        TRE_PROFILEDSCOPE("evolve", particlesE);
        const float sceneDt = myWindow.m_timing.frametime;
        for (std::size_t ip = 0; ip < worldParticlesBBStream.size(); ++ip)
        {
          worldParticlesBBStream.m_vel[ip] *= 0.99f;
          worldParticlesBBStream.m_vel[ip] += glm::vec4(0.f, 0.1f, 0.f, 0.005f);
          worldParticlesBBStream.m_pos[ip] += worldParticlesBBStream.m_vel[ip] * sceneDt;
          worldParticlesBBStream.m_life[ip].x += sceneDt * worldParticlesBBStream.m_life[ip].y;
        }
        for (std::size_t ip = 0; ip < worldParticlesMeshStream.size(); ++ip)
        {
          worldParticlesMeshStream.m_vel[ip] *= 0.99f;
          worldParticlesMeshStream.m_vel[ip] += sceneDt * glm::vec4(0.f, -1.f, 0.f, 0.01f);
          worldParticlesMeshStream.m_pos[ip] += worldParticlesMeshStream.m_vel[ip] * sceneDt;
          worldParticlesMeshStream.m_life[ip].x += sceneDt * worldParticlesMeshStream.m_life[ip].y;
        }
      }
      // particle kill-born
      {
        TRE_PROFILEDSCOPE("kill-born", particlesKB);
        for (std::size_t ip = 0; ip < worldParticlesBBStream.size(); ++ip)
        {
          if (worldParticlesBBStream.m_life[ip].x >= 1.f)
          {
            const float rand0 = (rand() * 2.f / RAND_MAX) - 1.f;
            const float rand1 = (rand() * 2.f / RAND_MAX) - 1.f;
            const float randL = rand() * 10.f / RAND_MAX;
            const float randR = rand() * 6.28f / RAND_MAX;
            worldParticlesBBStream.m_pos[ip] = glm::vec4(0.f, 2.f, 0.f, 0.04f);
            worldParticlesBBStream.m_vel[ip] = glm::vec4(rand0 * 0.1f, 0.f, rand1 * 0.1f, 0.f);
            worldParticlesBBStream.m_life[ip] = glm::vec2(0.f, 1.f / randL);
            worldParticlesBBStream.m_rot[ip] = glm::vec4(randR, 0.f, 0.f, 0.f);
          }
        }
        for (std::size_t ip = 0; ip < worldParticlesMeshStream.size(); ++ip)
        {
          if (worldParticlesMeshStream.m_life[ip].x >= 1.f)
          {
            const float rand0 = (rand() * 2.f / RAND_MAX) - 1.f;
            const float rand1 = (rand() * 2.f / RAND_MAX) - 1.f;
            const float randL = rand() * 10.f / RAND_MAX;
            worldParticlesMeshStream.m_pos[ip] = glm::vec4(4.f, 6.f, 4.f, 0.1f);
            worldParticlesMeshStream.m_vel[ip] = glm::vec4(rand0 * 0.1f, 0.5f * (rand0*rand0 + rand1*rand1), rand1 * 0.1f, 0.f);
            worldParticlesMeshStream.m_rot[ip] = glm::vec4(cosf(rand0) * cosf(rand1), sinf(rand0) * cosf(rand1), 0.f, sinf(rand1));
            worldParticlesMeshStream.m_life[ip] = glm::vec2(0.f, 1.f / randL);
            worldParticlesMeshStream.m_color[ip] = glm::vec4(rand1, 1.f - rand0, 1.f - rand1, 1.f);
          }
        }
      }
      // update data
      {
        TRE_PROFILEDSCOPE_COLORED("gpu-buffers", particlesDATA, glm::vec4(0.4f, 0.2f, 0.9f, 1.f));
        {
          const std::size_t nParticleToDraw = worldParticlesBBStream.size();
          {
            // TRE_PROFILEDSCOPE("sort", particlesBB_sort);
            // TODO : sort transparent particles
          }

          worldParticlesBB.reserveInstance(nParticleToDraw); // or resize ??
          auto posIt = worldParticlesBB.layout().m_instancedPositions.begin<glm::vec4>();
          auto colIt = worldParticlesBB.layout().m_instancedColors.begin<glm::vec4>();
          auto rotIt = worldParticlesBB.layout().m_instancedRotations.begin<float>();
          for (std::size_t ip = 0; ip < nParticleToDraw; ++ip)
          {
            *posIt++ = worldParticlesBBStream.m_pos[ip];
            const float plifeR = worldParticlesBBStream.m_life[ip].x;
            *colIt++ = glm::vec4(1.f, 1.f, 1.f, 0.5f - plifeR * plifeR * 0.3f);
            *rotIt++ = worldParticlesBBStream.m_rot[ip].x;
          }
          {
            TRE_PROFILEDSCOPE("upload", upload);
            worldParticlesBB.updateIntoGPU();
          }
        }
        {
          const std::size_t nParticleToDraw = worldParticlesMeshStream.size();
          worldParticlesMesh.reserveInstance(nParticleToDraw); // or resize ??
          glm::vec4 * bufferX4 = reinterpret_cast<glm::vec4*>(worldParticlesMesh.bufferInstanced()); // unsafe ...
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedPositions.isMatching(4, 20));
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedColors.isMatching(4, 20));
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedOrientations.isMatching(12, 20));
          for (std::size_t ip = 0; ip < nParticleToDraw; ++ip)
          {
            // pos
            *bufferX4++ = worldParticlesMeshStream.m_pos[ip];
            // color
            *bufferX4++ = worldParticlesMeshStream.m_color[ip];
            // orientation
            const glm::vec4 qq2 = 2.f * worldParticlesMeshStream.m_rot[ip] * worldParticlesMeshStream.m_rot[ip];
            const glm::vec4 qw2 = 2.f * worldParticlesMeshStream.m_rot[ip] * worldParticlesMeshStream.m_rot[ip].w;
            const float     qxy2 = 2.f * worldParticlesMeshStream.m_rot[ip].x * worldParticlesMeshStream.m_rot[ip].y;
            const float     qxz2 = 2.f * worldParticlesMeshStream.m_rot[ip].x * worldParticlesMeshStream.m_rot[ip].z;
            const float     qyz2 = 2.f * worldParticlesMeshStream.m_rot[ip].y * worldParticlesMeshStream.m_rot[ip].z;
            *bufferX4++ = glm::vec4(1.f - qq2.y - qq2.z, qxy2 - qw2.z       , qxz2 + qw2.y       , 0.f);
            *bufferX4++ = glm::vec4(qxy2 + qw2.z       , 1.f - qq2.x - qq2.z, qyz2 - qw2.x       , 0.f);
            *bufferX4++ = glm::vec4(qxz2 - qw2.y       , qyz2 + qw2.x       , 1.f - qq2.x - qq2.y, 0.f);
          }
          {
            TRE_PROFILEDSCOPE("upload", upload);
            worldParticlesMesh.updateIntoGPU();
          }
        }
      }
    }

    // prepare render ----------------------

    {
      TRE_PROFILEDSCOPE("prepare", prepare)

      const float sunTheta = myWindow.m_timing.scenetime*6.28f*0.02f;
      sunLight_Data.direction = glm::normalize( glm::vec3( 0.2f, -fabsf(cosf(sunTheta)), sinf(sunTheta) ) );

      sunLight_ShadowMap.computeUBO_forMap(sunLight_Data, sunShadow_Data, 0);

      tre::shader::updateUBO_sunLight(sunLight_Data);
      tre::shader::updateUBO_sunShadow(sunShadow_Data);

      const float ptsLightTheta = myWindow.m_timing.scenetime*6.28f*0.11f;
      const glm::vec3 ptsLightPos = glm::vec3(-3.f * cosf(ptsLightTheta), 3.f + 1.f * cosf(sunTheta), 3.f * sinf(ptsLightTheta));

      ptsLight_Data.col(0) = glm::vec4(1.0f, 1.0f, 0.2f, 9.0f);
      ptsLight_Data.pos(0) = glm::vec4(ptsLightPos, 1.f);

      ptsLight_ShadowMap.computeUBO_forMap(ptsLight_Data, 0, ptsShadow_Data);

      tre::shader::updateUBO_ptsLight(ptsLight_Data);
      tre::shader::updateUBO_ptsShadow(ptsShadow_Data);
    }

    glm::mat4 mModelCube;
    mModelCube = glm::translate(glm::mat4(1.f),glm::vec3(0.f,2.5f,0.f));
    mModelCube = glm::rotate(mModelCube,myWindow.m_timing.scenetime*6.28f*0.2f,glm::vec3(0.8f,0.f,0.6f));

    glm::mat4 mModelSphere;
    mModelSphere = glm::translate(glm::mat4(1.f),glm::vec3(7.f,2.5f,0.f));
    mModelSphere = glm::rotate(mModelSphere,myWindow.m_timing.scenetime*6.28f*0.2f,glm::vec3(0.8f,0.f,0.6f));

    const glm::mat4 mPV = myWindow.m_matProjection3D * myWindow.m_view3D.m_matView;

    // shadow-map render pass --------------

    {
      TRE_PROFILEDSCOPE("shadow", shadow)

      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);

      {
        sunLight_ShadowMap.bindForWritting();
        glViewport(0, 0, sunLight_ShadowMap.w(), sunLight_ShadowMap.h());
        glClear(GL_DEPTH_BUFFER_BIT);

        const glm::mat4 localMPV = sunLight_ShadowMap.mProj() * sunLight_ShadowMap.mView();

        glUseProgram(shaderShadow.m_drawProgram);

        shaderShadow.setUniformMatrix(localMPV);

        worldMesh.drawcallAll();

        shaderShadow.setUniformMatrix(localMPV * mModelCube, mModelCube);

        worldObjects.drawcall(0,1);

        shaderShadow.setUniformMatrix(localMPV * mModelSphere, mModelSphere);

        worldObjects.drawcall(1,1,false);

        // TODO: mesh-particles should cast shadow from the sun light.
      }

      tre::IsOpenGLok("shadow-map render pass");

      glViewport(0, 0, ptsLight_ShadowMap.w(), ptsLight_ShadowMap.h());

      for (unsigned iFace = 0; iFace < 6; ++iFace)
      {
        ptsLight_ShadowMap.bindForWritting(GL_TEXTURE_CUBE_MAP_POSITIVE_X + iFace);
        glClear(GL_DEPTH_BUFFER_BIT);
        const glm::mat4 &localMProj = ptsLight_ShadowMap.mProj();
        const glm::mat4 &localMView = ptsLight_ShadowMap.mView(GL_TEXTURE_CUBE_MAP_POSITIVE_X + iFace);
        const glm::mat4 localMPV = localMProj * localMView;

        glUseProgram(shaderShadow.m_drawProgram);

        shaderShadow.setUniformMatrix(localMPV);

        worldMesh.drawcallAll();

        shaderShadow.setUniformMatrix(localMPV * mModelCube, mModelCube);

        worldObjects.drawcall(0,1);

        shaderShadow.setUniformMatrix(localMPV * mModelSphere, mModelSphere);

        worldObjects.drawcall(1,1,false);

        // optim: mesh-particles don't cast shadow form the point-light.
      }

      tre::IsOpenGLok("shadow-cubemap render passes");
    }

    // opaque render pass ----------------

    {
      TRE_PROFILEDSCOPE("render-opaque", render)

      if (withMSAA)
        rtMultisampled.bindForWritting();
      else
        rtResolveMSAA.bindForWritting();

      glViewport(0, 0, rtMultisampled.w(), rtMultisampled.h());
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glDisable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);

      glUseProgram(shaderSkybox.m_drawProgram);

      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_CUBE_MAP,worldSkyBoxTex.m_handle);
      glUniform1i(shaderSkybox.getUniformLocation(tre::shader::TexCube),3);

      glm::mat4 MViewBox(myWindow.m_view3D.m_matView);
      MViewBox[3] = glm::vec4(0.f,0.f,0.f,1.f); // no translation

      shaderSkybox.setUniformMatrix(myWindow.m_matProjection3D * MViewBox, glm::mat4(1.f), MViewBox);

      worldSkyboxModel.drawcallAll();

      tre::IsOpenGLok("opaque render pass - draw SkyBox");

      glEnable(GL_DEPTH_TEST);

      glUseProgram(shaderMaterialMapped.m_drawProgram);

      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D,sunLight_ShadowMap.depthHandle());
      glUniform1i(shaderMaterialMapped.getUniformLocation(tre::shader::TexShadowSun0),2);

      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ptsLight_ShadowMap.depthHandle());
      glUniform1i(shaderMaterialMapped.getUniformLocation(tre::shader::TexShadowPts0),3);

      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D,worldGroundColor.m_handle);
      glUniform1i(shaderMaterialMapped.getUniformLocation(tre::shader::TexDiffuse),4);

      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D,worldGroundNormal.m_handle);
      glUniform1i(shaderMaterialMapped.getUniformLocation(tre::shader::TexNormal),5);

      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D,worldGroundMetalRough.m_handle);
      glUniform1i(shaderMaterialMapped.getUniformLocation(tre::shader::TexBRDF),6);

      shaderMaterialMapped.setUniformMatrix(mPV, glm::mat4(1.f), myWindow.m_view3D.m_matView);

      worldMesh.drawcall(0, 1);

      const glm::vec4 ucolorMain(0.5f,0.5f,0.5f,1.f);

      glUseProgram(shaderMaterialUni.m_drawProgram);

      glUniform1i(shaderMaterialUni.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderMaterialUni.getUniformLocation(tre::shader::TexShadowPts0),3);

      shaderMaterialUni.setUniformMatrix(mPV, glm::mat4(1.f), myWindow.m_view3D.m_matView);

      glUniform2f(shaderMaterialUni.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.7f);

      glUniform4fv(shaderMaterialUni.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMain));

      worldMesh.drawcall(1, 1, false);

      tre::IsOpenGLok("opaque render pass - draw Room");

      const glm::vec4 ucolorMove(0.2f,1.0f,0.2f,1.f);

      shaderMaterialUni.setUniformMatrix(mPV * mModelCube, mModelCube, myWindow.m_view3D.m_matView);

      glUniform2f(shaderMaterialUni.getUniformLocation(tre::shader::uniBRDF), 1.f, 0.1f);

      glUniform4fv(shaderMaterialUni.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMove));

      worldObjects.drawcall(0,1);

      tre::IsOpenGLok("opaque render pass - draw Object");

      glUseProgram(shaderSphere.m_drawProgram);

      glUniform1i(shaderSphere.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderSphere.getUniformLocation(tre::shader::TexShadowPts0),3);

      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_CUBE_MAP,worldTex_UVcoords.m_handle);
      glUniform1i(shaderSphere.getUniformLocation(tre::shader::TexCube),7);

      glUniform2f(shaderSphere.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.5f);

      shaderSphere.setUniformMatrix(mPV * mModelSphere, mModelSphere, myWindow.m_view3D.m_matView);

      worldObjects.drawcall(1,1);

      tre::IsOpenGLok("opaque render pass - draw Object");

      glUseProgram(shaderInstancedMesh.m_drawProgram);

      glUniform1i(shaderInstancedMesh.getUniformLocation(tre::shader::TexShadowSun0),2); // sunLight_ShadowMap already bound to GL_TEXTURE2

      shaderInstancedMesh.setUniformMatrix(mPV);

      const std::size_t nParticleToDraw = worldParticlesMeshStream.size();
      worldParticlesMesh.drawInstanced(0, 0, nParticleToDraw);

      tre::IsOpenGLok("opaque render pass - draw Particle Mesh");
    }

    // transparent render pass ------

    if (withMSAA)
    {
      // in the transparent pass, we may need to read the depth-buffer.
      // So we resolve the MSAA now.
      rtMultisampled.resolve(rtResolveMSAA);
    }

    {
      TRE_PROFILEDSCOPE("render-transparent", render);

      glEnable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE); // uggly: we'll read the current FBO depth buffer, which is permitted when the writes to the depth buffer are disabled.

      rtResolveMSAA.bindForWritting();

      glUseProgram(shaderInstancedBB.m_drawProgram);

      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexShadowSun0),2); // sunLight_ShadowMap already bound to GL_TEXTURE2

      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D,worldParticlesTex.m_handle);
      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexDiffuse),4);

      glUniform3f(shaderInstancedBB.getUniformLocation(tre::shader::uniPhong), 2.f, 0.7f, 0.8f);

      glUniform3f(shaderInstancedBB.getUniformLocation(tre::shader::SoftDistance), 0.2f, myWindow.m_near, myWindow.m_far);

      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, rtResolveMSAA.depthHandle());
      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexDepth),5);

      glm::mat3 invView = glm::mat3(myWindow.m_view3D.m_matView);
      invView = glm::transpose(invView);

      shaderInstancedBB.setUniformMatrix(mPV);

      glUniformMatrix3fv(shaderInstancedBB.getUniformLocation(tre::shader::MOrientation), 1, GL_FALSE, glm::value_ptr(invView));

      const std::size_t nParticleToDraw = worldParticlesBBStream.size();
      worldParticlesBB.drawInstanced(0, 0, nParticleToDraw);

      tre::IsOpenGLok("main render pass - draw Particle BB");
    }

    // post-effects ----------------

    {
      TRE_PROFILEDSCOPE("postFX (cpu cost)", postFX)

      if (withBlur)
      {
        postEffectBlur.resolveBlur(rtResolveMSAA.colorHandle(), rtAfterBlur);
        postEffectToneMapping.resolveToneMapping(rtAfterBlur.colorHandle(), myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
      }
      else
      {
        postEffectToneMapping.resolveToneMapping(rtResolveMSAA.colorHandle(), myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
      }

      tre::IsOpenGLok("post-effect pass");
    }

    // UI-render pass -------------

    {
      TRE_PROFILEDSCOPE("UI", render)

      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

      glEnable(GL_BLEND);
      glDisable(GL_DEPTH_TEST);
      glDepthMask(GL_FALSE);

      if (showShadowMaps)
      {
        glUseProgram(sunShadow_debug.shader.m_drawProgram);

        glUniform1i(sunShadow_debug.shader.getUniformLocation(tre::shader::TexDiffuse),2); // texture already bind

        const glm::mat3 mModel_sunDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                    0.0f, 0.3f, 0.f,
                                                    1.0f, 0.6f, 1.f);

        sunShadow_debug.shader.setUniformMatrix(myWindow.m_matProjection2D * mModel_sunDebug);

        sunShadow_debug.geom.drawcall(0, 1);

        glUseProgram(sunShadow_debug.shaderCubeFace.m_drawProgram);

        glUniform1i(sunShadow_debug.shaderCubeFace.getUniformLocation(tre::shader::TexCube),3); // texture already bind

        const glm::mat3 mModel_cubeDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                     0.0f, 0.3f, 0.f,
                                                     1.0f, -0.1f, 1.f);

        sunShadow_debug.shaderCubeFace.setUniformMatrix(myWindow.m_matProjection2D * mModel_cubeDebug);

        for (unsigned iFace = 0; iFace < 6; ++iFace)
        {
          glUniform1i(sunShadow_debug.shaderCubeFace.getUniformLocation("iface"),GLint(iFace));
          sunShadow_debug.geom.drawcall(1 + iFace, 1);
        }

      }

      if (true)
      {
        char txtFPS[128];
        snprintf(txtFPS, 127, "%03d fps (Work-elapsed = %03d ms, Swap-latency = %03d ms)",
                 int(1.f/myWindow.m_timing.frametime),
                 int(myWindow.m_timing.worktime * 1000),
                 int((myWindow.m_timing.frametime - myWindow.m_timing.worktime) * 1000));
        tre::textgenerator::s_textInfo tInfo;
        tInfo.setupBasic(&worldHUDFont, 0.06f, txtFPS, glm::vec2(0.f, -0.08f - 0.08f * 0));
        worldHUDModel.resizePart(0, tre::textgenerator::geometry_VertexCount(tInfo.m_text));
        tre::textgenerator::generate(tInfo, &worldHUDModel, 0, 0, nullptr);

        worldHUDModel.colorizePart(2, showShadowMaps ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(0.8f));
        worldHUDModel.colorizePart(3, withBlur ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(0.8f));
        worldHUDModel.colorizePart(4, canMSAA ? (withMSAA ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(0.8f)) : glm::vec4(1.f, 0.3f, 0.3f, 1.f));

        worldHUDModel.updateIntoGPU();

        glm::mat3 mViewModel_hud = glm::mat3(1.f);
        mViewModel_hud[0][0] = 1.f;
        mViewModel_hud[1][1] = 1.f;
        mViewModel_hud[2][0] =  -0.95f / myWindow.m_matProjection2D[0][0];
        mViewModel_hud[2][1] =   0.95f;

        glUseProgram(shaderText2D.m_drawProgram);
        shaderText2D.setUniformMatrix(myWindow.m_matProjection2D * mViewModel_hud);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D,worldHUDFont.get_texture().m_handle);
        glUniform1i(shaderText2D.getUniformLocation(tre::shader::TexDiffuse),3);

        worldHUDModel.drawcallAll(true);
      }

      tre::IsOpenGLok("UI render pass - draw UI");
    }

    // profiler ---------------------

    tre::profiler_endframe();

    tre::profiler_updateIntoGPU();
    tre::profiler_draw();

    // end render pass --------------

    myWindow.m_timing.endFrame_beforeGPUPresent();

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  TRE_LOG("Main loop exited");
  TRE_LOG("Average work elapsed-time needed for each frame: " << myWindow.m_timing.worktime_average * 1000 << " ms");
  TRE_LOG("Average frame elapsed-time needed for each frame (Vsync enabled): " << myWindow.m_timing.frametime_average * 1000 << " ms");

  // Finalize
  worldSkyboxModel.clearGPU();
  worldMesh.clearGPU();
  worldObjects.clearGPU();
  worldParticlesBB.clearGPU();
  worldParticlesMesh.clearGPU();

  worldSkyBoxTex.clear();
  worldTex_UVcoords.clear();
  worldParticlesTex.clear();
  worldGroundColor.clear();
  worldGroundNormal.clear();
  worldGroundMetalRough.clear();

  worldHUDModel.clearGPU();
  worldHUDFont.clear();

  tre::profiler_clearGPU();
  tre::profiler_clearShader();

  shaderSkybox.clearShader();
  shaderMaterialUni.clearShader();
  shaderMaterialMapped.clearShader();
  shaderSphere.clearShader();
  shaderInstancedBB.clearShader();
  shaderInstancedMesh.clearShader();
  shaderSolid2D.clearShader();
  shaderText2D.clearShader();
  shaderShadow.clearShader();

  tre::shader::clearUBO();

  sunShadow_debug.shader.clearShader();
  sunShadow_debug.shaderCubeFace.clearShader();
  sunShadow_debug.geom.clearGPU();

  sunLight_ShadowMap.clear();
  ptsLight_ShadowMap.clear();
  postEffectToneMapping.clear();
  postEffectBlur.clear();
  rtMultisampled.clear();
  rtResolveMSAA.clear();
  rtAfterBlur.clear();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
