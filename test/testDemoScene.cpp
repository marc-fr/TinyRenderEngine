
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_textgenerator.h"
#include "tre_profiler.h"
#include "tre_font.h"
#include "tre_windowContext.h"

#include <math.h>
#include <stdlib.h> // rand,srand
#include <time.h>   // time
#include <string>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// ============================================================================
// Particles

struct s_particleStream
{
  std::vector<glm::vec4> m_pos;    // pos(x,y,z) + size(w)
  std::vector<glm::vec4> m_vel;    // dpos(x,y,z) + dsize(w)
  std::vector<glm::vec2> m_life;   // life + invLifeRatio
  std::vector<glm::vec4> m_rot;    // scalar rotation or quaternion
  std::vector<glm::vec4> m_color;  // color
  std::vector<unsigned>  m_meshId; // mesh-id (in [0,1])

  std::array<unsigned, 2> m_countPerMeshId; // only 2 meshes supported.

  const bool m_withRotation;
  const bool m_withColor;
  const bool m_withMeshId;

  s_particleStream(bool withRotation, bool withColor, bool withMeshId) : m_withRotation(withRotation), m_withColor(withColor), m_withMeshId(withMeshId) {}

  inline std::size_t size() const { return m_pos.size(); }

  void resize(std::size_t count)
  {
    m_pos.resize(count);
    m_vel.resize(count);
    m_life.resize(count, glm::vec2(2.f, 1.f));
    if (m_withRotation) m_rot.resize(count);
    if (m_withColor) m_color.resize(count);
    if (m_withMeshId) m_meshId.resize(count);
  }
};

static void _particleUpdate(s_particleStream &worldParticlesBBStream, s_particleStream &worldParticlesMeshStream, float sceneDt)
{
  // particle evolve
  {
    TRE_PROFILEDSCOPE("evolve", particlesE);
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
        worldParticlesMeshStream.m_meshId[ip] = (rand() & 0x1);
      }
    }
  }
}

// ============================================================================
// Shadow-debug

struct s_shadowDebug
{
  tre::shader     shader;
  tre::shader     shaderCubeFace;
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
    if (!shaderCubeFace.loadCustomShader(shLayoutCubeFace, SourceShaderCubeTexturedDepth_FragmentMain, "CubeTexDepth_debug"))
      return;

    // squared-map
    geom.createPart(6);
    geom.fillDataRectangle(0, 0, glm::vec4(-1.f, -1.f, 1.f, 1.f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));

    // cube-map
    geom.createPart(6); //+X
    geom.fillDataRectangle(1, 0, glm::vec4( 0.0f, 0.0f, 0.5f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    geom.createPart(6); //-X
    geom.fillDataRectangle(2, 0, glm::vec4(-1.0f, 0.0f, -0.5f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    geom.createPart(6); //+Y
    geom.fillDataRectangle(3, 0, glm::vec4(-0.5f, 0.5f, 0.0f, 1.0f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    geom.createPart(6); //-Y
    geom.fillDataRectangle(4, 0, glm::vec4(-0.5f, -0.5f, 0.0f, 0.f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    geom.createPart(6); //+Z
    geom.fillDataRectangle(5, 0, glm::vec4(0.5f, 0.0f, 1.0f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));
    geom.createPart(6); //-Z
    geom.fillDataRectangle(6, 0, glm::vec4(-0.5f, 0.0f, 0.0f, 0.5f), glm::vec4(), glm::vec4(0.f, 0.f, 1.f, 1.f));

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

    glUseProgram(shaderCubeFace.m_drawProgram);

    glUniform1i(shaderCubeFace.getUniformLocation(tre::shader::TexCube),3); // texture already bind

    const glm::mat3 mModel_cubeDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                 0.0f, 0.3f, 0.f,
                                                 1.0f, -0.1f, 1.f);

    shaderCubeFace.setUniformMatrix(mProj2D * mModel_cubeDebug);

    for (unsigned iFace = 0; iFace < 6; ++iFace)
    {
      glUniform1i(shaderCubeFace.getUniformLocation("iface"),GLint(iFace));
      geom.drawcall(1 + iFace, 1);
    }
  }

  void clear()
  {
    shader.clearShader();
    shaderCubeFace.clearShader();
    geom.clearGPU();
  }

};

// ============================================================================
// Static scene

struct s_worldScene
{
  tre::modelStaticIndexed3D mesh;
  GLuint                    partId_Main = 0;
  GLuint                    partId_Tower = 0;
  GLuint                    partId_TreeTrunck = 0;
  GLuint                    partId_TreeLeaves = 0;
  GLuint                    partId_Sphere = 0;
  tre::texture              texGrass;
  tre::texture              texWall;
  tre::texture              texWall_normal;
  tre::texture              texWood;
  tre::texture              texWood_normal;
  tre::texture              texTreeTrunck;
  tre::texture              texTreeLeaves;
  tre::texture              texSteal;
  tre::texture              texSteal_mr; // metallic-roughness
  glm::mat4                 instance_Tower = glm::mat4(1.f);
  std::vector<glm::mat4>    instances_Tree;
  std::vector<glm::mat4>    instances_Sphere; // (rotation will evolve)

  void clear()
  {
    mesh.clearGPU();
    texGrass.clear();
    texWall.clear();
    texWall_normal.clear();
    texWood.clear();
    texWood_normal.clear();
    texTreeTrunck.clear();
    texTreeLeaves.clear();
    texSteal.clear();
    texSteal_mr.clear();
  }

  tre::s_boundbox getBoundingBox() const
  {
    return mesh.partInfo(partId_Main).m_bbox; // + wall + tower + extend to a tree's up-point
  }
};

// ============================================================================
// MAIN

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;
  tre::windowContext::s_timer myTimings;
  tre::windowContext::s_view3D myView3D(&myWindow);

  if (!myWindow.SDLInit(SDL_INIT_VIDEO) || !myWindow.SDLImageInit(IMG_INIT_JPG))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Demo Scene", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - random generator

  srand(time(nullptr)); // TODO: have proper c++11 rand generators

  // load meshes

  tre::modelStaticIndexed3D worldSkyboxModel;
  worldSkyboxModel.createPartFromPrimitive_box(glm::mat4(1.f), 10.f);
  worldSkyboxModel.loadIntoGPU();

  s_worldScene worldScene;

  {
    worldScene.mesh.setFlags(tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_TANGENT | tre::modelStaticIndexed3D::VB_UV);
    tre::modelImporter::s_modelHierarchy worldHierarchy;
    if (tre::modelImporter::addFromGLTF(worldScene.mesh, worldHierarchy, TESTIMPORTPATH "resources/scene/scene.gltf"))
    {
      // get the part-ids (by looking at the flat part names)
      worldScene.partId_Main = worldScene.mesh.getPartWithName("Grid");
      worldScene.partId_Tower = worldScene.mesh.getPartWithName("watchtower");
      worldScene.partId_Sphere = worldScene.mesh.getPartWithName("Ball");
      worldScene.partId_TreeTrunck = worldScene.mesh.getPartWithName("treeT2");
      worldScene.partId_TreeLeaves = worldScene.mesh.getPartWithName("leavesT2");

      // get the object instances (tower + balls + trees)
      for (const auto &mh : worldHierarchy.m_childs)
      {
        if (mh.m_partId == worldScene.partId_Sphere)
          worldScene.instances_Sphere.push_back(mh.m_transform);
        else if (mh.m_partId == worldScene.partId_TreeTrunck)
          worldScene.instances_Tree.push_back(mh.m_transform);
        else if (mh.m_partId == worldScene.partId_Tower)
          worldScene.instance_Tower = mh.m_transform;
      }

      //tre::modelTools::computeTangentFromUV(worldScene.mesh.layout(), worldScene.mesh.partInfo(worldScene.partId_Main));
      tre::modelTools::computeTangentFromUV(worldScene.mesh.layout(), worldScene.mesh.partInfo(worldScene.partId_Tower));
    }
    else
    {
      TRE_LOG("Fail to load scene/scene.gltf.");

      worldScene.mesh.clearParts();
      worldScene.mesh.createPartFromPrimitive_square(glm::mat4(1.f), 100.f);
    }

    worldScene.mesh.loadIntoGPU();
  }

  // load textures

  TRE_LOG("... loading textures ...");

  tre::texture worldSkyBoxTex;
  {
    // The textures are generated from "testTextureSampling", with TRE_WITH_SDL2_IMAGE and TRE_WITH_TIFF enabled
    const std::array<SDL_Surface*, 6> cubeFaces = { tre::texture::loadTextureFromFile("imageTIFF.1024.inside.xpos.png"),
                                                    tre::texture::loadTextureFromFile("imageTIFF.1024.inside.xneg.png"),
                                                    tre::texture::loadTextureFromFile("imageTIFF.1024.inside.ypos.png"),
                                                    tre::texture::loadTextureFromFile("imageTIFF.1024.inside.yneg.png"),
                                                    tre::texture::loadTextureFromFile("imageTIFF.1024.inside.zpos.png"),
                                                    tre::texture::loadTextureFromFile("imageTIFF.1024.inside.zneg.png"), };
    worldSkyBoxTex.loadCube(cubeFaces, tre::texture::MMASK_MIPMAP | tre::texture::MMASK_COMPRESS, true);
  }

  if (!worldScene.texGrass.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/wispy-grass-meadow_albedo.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_SRBG_SPACE, true))
    worldScene.texGrass.loadWhite();

  if (!worldScene.texWood.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/Wood_Tower_Col.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_SRBG_SPACE, true))
    worldScene.texWood.loadWhite();

  if (!worldScene.texWood_normal.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/Wood_Tower_Nor_jpg.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC, true))
    worldScene.texWood_normal.loadColor(0xFF8080FF);

  if (!worldScene.texSteal.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/rusted-steel_albedo.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_SRBG_SPACE, true))
    worldScene.texSteal.loadWhite();

  if (!worldScene.texSteal_mr.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/rusted-steel_metallic-rusted-steel_roughness.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_RG_ONLY, true))
    worldScene.texSteal_mr.loadWhite();

  if (!worldScene.texTreeTrunck.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/BarkDecidious0194_7_S_jpg.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_SRBG_SPACE, true))
    worldScene.texTreeTrunck.loadWhite();

  if (!worldScene.texTreeLeaves.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/scene/Leaves0120_35_S_png.jpg"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC | tre::texture::MMASK_SRBG_SPACE, true))
    worldScene.texTreeLeaves.loadWhite();

  // load Particles

  tre::modelInstancedBillboard worldParticlesBB(tre::modelInstanced::VI_COLOR | tre::modelInstanced::VI_ROTATION);
  worldParticlesBB.createBillboard();
  worldParticlesBB.loadIntoGPU();

  tre::modelInstancedMesh worldParticlesMesh(tre::modelInstancedMesh::VB_NORMAL, tre::modelInstanced::VI_ORIENTATION | tre::modelInstanced::VI_COLOR);
  worldParticlesMesh.createPartFromPrimitive_box(glm::mat4(1.f), 0.5f);
  worldParticlesMesh.createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), 0.5f, 6, 6);
  worldParticlesMesh.loadIntoGPU();

  tre::texture  worldParticlesTex;
  if (!worldParticlesTex.load(tre::texture::loadTextureFromFile(TESTIMPORTPATH "resources/quad.png"), tre::texture::MMASK_MIPMAP | tre::texture::MMASK_FORCE_NO_ALPHA, true))
    worldParticlesTex.loadWhite();

  s_particleStream worldParticlesBBStream(true, false, false);
  worldParticlesBBStream.resize(1024);

  s_particleStream worldParticlesMeshStream(true, true, true);
  worldParticlesMeshStream.resize(512);

  // load HUD

  TRE_LOG("... loading HUD ...");

  tre::font          worldHUDFont;

  worldHUDFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  tre::modelRaw2D    worldHUDModel;
  {
    static const char* txts[5] = { "FPS",
                                   "right clic: lock/unlock camera",
                                   "F5: show/hide render-target",
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

    const float r = float(myWindow.m_resolutioncurrent.x) / float(myWindow.m_resolutioncurrent.y);

    worldHUDModel.createPart(6);
    worldHUDModel.fillDataRectangle(5, 0, glm::vec4(-r, -1.f, -r + 0.3f * r, -1.f + 0.3f), glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    worldHUDModel.createPart(6);
    worldHUDModel.fillDataRectangle(6, 0, glm::vec4(-r, -1.f + 0.35f, -r + 0.3f * r, -1.f + 0.65f), glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    worldHUDModel.createPart(6);
    worldHUDModel.fillDataRectangle(7, 0, glm::vec4(-r, -1.f + 0.70f, -r + 0.3f * r, -1.f + 1.00f), glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    worldHUDModel.loadIntoGPU();
  }

  // material (shaders)

  TRE_LOG("... loading material (shaders) ...");

  tre::shader  shaderSkybox;
  shaderSkybox.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_CUBEMAPED | tre::shader::PRGM_BACKGROUND);

  tre::shader shaderMaterialFlat;
  shaderMaterialFlat.setShadowSunSamplerCount(1);
  shaderMaterialFlat.setShadowPtsSamplerCount(1);
  shaderMaterialFlat.loadShader(tre::shader::PRGM_3D,
                               tre::shader::PRGM_TEXTURED |
                               tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                               tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                               tre::shader::PRGM_UNIBRDF);

  tre::shader shaderMaterialBumped;
  shaderMaterialBumped.setShadowSunSamplerCount(1);
  shaderMaterialBumped.setShadowPtsSamplerCount(1);
  shaderMaterialBumped.loadShader(tre::shader::PRGM_3D,
                                  tre::shader::PRGM_TEXTURED | tre::shader::PRGM_MAPNORMAL |
                                  tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                                  tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                                  tre::shader::PRGM_UNIBRDF);

  tre::shader shaderMaterialBrdf;
  shaderMaterialBrdf.setShadowSunSamplerCount(1);
  shaderMaterialBrdf.setShadowPtsSamplerCount(1);
  shaderMaterialBrdf.loadShader(tre::shader::PRGM_3D,
                                tre::shader::PRGM_TEXTURED |
                                tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN |
                                tre::shader::PRGM_LIGHT_PTS | tre::shader::PRGM_SHADOW_PTS |
                                tre::shader::PRGM_MAPBRDF);

  tre::shader shaderMetarialDsidedMask;
  {
    const char *shSource = "void main()\n"
                           "{\n"
                           " vec3 cDiffuse = texture(TexDiffuse, pixelUV).xyz;\n"
                           " if (cDiffuse.g < 0.05f) discard; // MASK\n"
                           "vec3 V = - normalize((MView * vec4(pixelPosition, 1.f)).xyz);\n"
                           "vec3 rawN = normalize(pixelNormal);\n"
                           "vec3 N = normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n"
                           "vec2 matMetalRough = uniBRDF;\n"
                           "vec3 L = - normalize((MView * vec4(m_sunlight.direction, 0.f)).xyz);\n"
                           "vec3 Ndsided = (dot(N,L) >= 0.f ? 1.f : -1.f) * N;\n"
                           "float tanTheta = tan(acos(clamp(dot(rawN,normalize(-m_sunlight.direction)), 1.e-3f, 1.f)));\n"
                           "float islighted_sun = ShadowOcclusion_sun(tanTheta, rawN);\n"
                           "vec3 lsun = BRDFLighting(cDiffuse, m_sunlight.color, Ndsided, L, V, uniBRDF.x, uniBRDF.y);\n"
                           "vec3 lamb = BRDFLighting_ambiante(cDiffuse, m_sunlight.colorAmbiant, Ndsided, V, uniBRDF.x, uniBRDF.y);\n"
                           " color.xyz = lsun * islighted_sun + lamb;\n"
                           " color.w = 1.f;\n"
                           "}\n";

    shaderMetarialDsidedMask.setShadowSunSamplerCount(1);
    tre::shaderGenerator::s_layout sl(tre::shader::PRGM_3D, tre::shader::PRGM_TEXTURED | tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_SHADOW_SUN | tre::shader::PRGM_UNIBRDF);
    shaderMetarialDsidedMask.loadCustomShader(sl, shSource, "3d_leaves");
  }

  tre::shader::s_UBOdata_sunLight sunLight_Data;
  sunLight_Data.color = glm::vec3(1.8f);
  sunLight_Data.colorAmbiant = glm::vec3(0.1f);

  tre::shader::s_UBOdata_sunShadow sunShadow_Data;
  sunShadow_Data.nShadow = 1;

  tre::shader::s_UBOdata_ptstLight ptsLight_Data;
  ptsLight_Data.nLight = 1;

  tre::shader::s_UBOdata_ptsShadow ptsShadow_Data;

  tre::shader shaderShadow;
  shaderShadow.loadShader(tre::shader::PRGM_3D_DEPTH,0);

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
                                 tre::shader::PRGM_UNIPHONG |
                                 tre::shader::PRGM_INSTANCED | tre::shader::PRGM_INSTCOLOR | tre::shader::PRGM_ORIENTATION);

  tre::shader shaderText2D;
  shaderText2D.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_TEXTURED);

  // debug shadow

  s_shadowDebug sunShadow_debug;

  sunShadow_debug.load();

  // Profiler

  TRE_LOG("... loading tre::profiler ...");

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

  TRE_LOG("... loading post-effects ...");

  tre::renderTarget_ShadowMap sunLight_ShadowMap;
  sunLight_ShadowMap.load(2048,2048);
  sunLight_ShadowMap.setSceneBox(worldScene.getBoundingBox());

  tre::renderTarget_ShadowCubeMap ptsLight_ShadowMap;
  ptsLight_ShadowMap.load(1024);
  ptsLight_ShadowMap.setRenderingLimits(0.2f, 20.f);

  tre::renderTarget rtMultisampled(tre::renderTarget::RT_COLOR_AND_DEPTH | tre::renderTarget::RT_MULTISAMPLED | tre::renderTarget::RT_COLOR_HDR);
  const bool canMSAA = rtMultisampled.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::renderTarget rtResolveMSAA(tre::renderTarget::RT_COLOR_AND_DEPTH | tre::renderTarget::RT_SAMPLABLE | tre::renderTarget::RT_COLOR_HDR);
  rtResolveMSAA.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::postFX_Blur postEffectBlur(3, false);
  postEffectBlur.set_threshold(0.95f);
  postEffectBlur.set_multiplier(2.f);
  postEffectBlur.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
  postEffectBlur.loadCombine();

  tre::renderTarget rtAfterBlur(tre::renderTarget::RT_COLOR | tre::renderTarget::RT_COLOR_SAMPLABLE | tre::renderTarget::RT_COLOR_HDR);
  rtAfterBlur.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  tre::postFX_ToneMapping postEffectToneMapping;
  postEffectToneMapping.set_gamma(2.2f);
  postEffectToneMapping.set_exposure(1.1f);
  postEffectToneMapping.load();

  // End Init

  TRE_LOG("... loading completed");

  tre::IsOpenGLok("main: initialization");

  tre::checkLayoutMatch_Shader_Model(&shaderShadow, &worldScene.mesh);
  tre::checkLayoutMatch_Shader_Model(&shaderSkybox, &worldSkyboxModel);
  tre::checkLayoutMatch_Shader_Model(&shaderMaterialBumped, &worldScene.mesh);
  tre::checkLayoutMatch_Shader_Model(&shaderInstancedBB, &worldParticlesBB);
  tre::checkLayoutMatch_Shader_Model(&shaderInstancedMesh, &worldParticlesMesh);

  // - event and time variables

  SDL_Event event;

  myTimings.initialize();

  myView3D.m_matView[3] = glm::vec4(0.f, -3.f, -9.8f, 1.f);
  myView3D.setScreenBoundsMotion(true);
  myView3D.setKeyBinding(true);

  bool showMaps = false;
  bool withMSAA = canMSAA;
  bool withBlur = true;

  // MAIN LOOP

  while(!myWindow.m_quit && !myControls.m_quit)
  {
    myWindow.SDLEvent_newFrame();
    myControls.newFrame();
    myTimings.newFrame(0, myControls.m_pause);
    tre::profiler_newFrame();

    // event actions + updates -------

    {
      TRE_PROFILEDSCOPE("events", ev)

      while(SDL_PollEvent(&event) == 1)
      {
        myWindow.SDLEvent_onWindow(event);
        myControls.treatSDLEvent(event);

        tre::profiler_acceptEvent(event);

        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F5) showMaps = !showMaps;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F6) withBlur = !withBlur;
        if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F7) withMSAA = !withMSAA && canMSAA;
      }

      if (myWindow.m_hasFocus)
        myView3D.treatControlEvent(myControls, myTimings.frametime);

      if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED)
        myView3D.setMouseBinding(!myView3D.m_mouseBound);

      if (myWindow.m_viewportResized)
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

    if (!myControls.m_pause)
    {
      TRE_PROFILEDSCOPE("evolve", evolve);

      // sun and lights
      {
        const float sunTheta = myTimings.scenetime*6.28f*0.02f;
        sunLight_Data.direction = glm::normalize( glm::vec3( 0.2f, -fabsf(cosf(sunTheta)), sinf(sunTheta) ) );

        const float ptsLightTheta = myTimings.scenetime*6.28f*0.11f;
        const glm::vec3 ptsLightPos = glm::vec3(-3.f * cosf(ptsLightTheta), 3.f + 1.f * cosf(sunTheta), 3.f * sinf(ptsLightTheta));

        ptsLight_Data.col(0) = glm::vec4(1.0f, 1.0f, 0.2f, 9.0f);
        ptsLight_Data.pos(0) = glm::vec4(ptsLightPos, 1.f);
      }

      // spheres
      {
        for (auto &si : worldScene.instances_Sphere)
        {
          const glm::vec3 rAxis = glm::normalize(glm::vec3(si[3]) + glm::vec3(0.f, 0.f, 0.4f));
          si = glm::rotate(si, myTimings.frametime * 6.28f*0.2f, rAxis);
        }
      }

      // particle simulation
      {
        _particleUpdate(worldParticlesBBStream, worldParticlesMeshStream, myTimings.frametime);
      }

      // particle pre-render (fill GPU buffer + sort transparent billboards)
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
        }
        {
          const std::size_t nParticleToDraw = worldParticlesMeshStream.size();
          worldParticlesMesh.reserveInstance(nParticleToDraw); // or resize ??
          worldParticlesMeshStream.m_countPerMeshId.fill(0u); // reset count
          glm::vec4 * bufferX4 = reinterpret_cast<glm::vec4*>(worldParticlesMesh.bufferInstanced()); // unsafe ...
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedPositions.isMatching(4, 20));
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedColors.isMatching(4, 20));
          TRE_ASSERT(worldParticlesMesh.layout().m_instancedOrientations.isMatching(12, 20));
          // -> count
          for (std::size_t ip = 0; ip < nParticleToDraw; ++ip)
          {
            ++worldParticlesMeshStream.m_countPerMeshId[worldParticlesMeshStream.m_meshId[ip]];
          }
          // -> fill
          for (std::size_t ip = 0, ip0 = 0, ip1 = worldParticlesMeshStream.m_countPerMeshId[0]; ip < nParticleToDraw; ++ip)
          {
            glm::vec4 *localBufferX4 = (worldParticlesMeshStream.m_meshId[ip] == 0) ? bufferX4 + (5 * ip0++) : bufferX4 + (5 * ip1++);
            // pos
            *localBufferX4++ = worldParticlesMeshStream.m_pos[ip];
            // color
            *localBufferX4++ = worldParticlesMeshStream.m_color[ip];
            // orientation
            const glm::vec4 qq2 = 2.f * worldParticlesMeshStream.m_rot[ip] * worldParticlesMeshStream.m_rot[ip];
            const glm::vec4 qw2 = 2.f * worldParticlesMeshStream.m_rot[ip] * worldParticlesMeshStream.m_rot[ip].w;
            const float     qxy2 = 2.f * worldParticlesMeshStream.m_rot[ip].x * worldParticlesMeshStream.m_rot[ip].y;
            const float     qxz2 = 2.f * worldParticlesMeshStream.m_rot[ip].x * worldParticlesMeshStream.m_rot[ip].z;
            const float     qyz2 = 2.f * worldParticlesMeshStream.m_rot[ip].y * worldParticlesMeshStream.m_rot[ip].z;
            *localBufferX4++ = glm::vec4(1.f - qq2.y - qq2.z, qxy2 - qw2.z       , qxz2 + qw2.y       , 0.f);
            *localBufferX4++ = glm::vec4(qxy2 + qw2.z       , 1.f - qq2.x - qq2.z, qyz2 - qw2.x       , 0.f);
            *localBufferX4++ = glm::vec4(qxz2 - qw2.y       , qyz2 + qw2.x       , 1.f - qq2.x - qq2.y, 0.f);
          }
        }
      }

      // upload to GPU
      {
        TRE_PROFILEDSCOPE("upload", upload);
        worldParticlesBB.updateIntoGPU();
        worldParticlesMesh.updateIntoGPU();
      }
    }

    // prepare render ----------------------

    {
      TRE_PROFILEDSCOPE("prepare", prepare)

      sunLight_ShadowMap.computeUBO_forMap(sunLight_Data, sunShadow_Data, 0);

      tre::shader::updateUBO_sunLight(sunLight_Data);
      tre::shader::updateUBO_sunShadow(sunShadow_Data);

      ptsLight_ShadowMap.computeUBO_forMap(ptsLight_Data, 0, ptsShadow_Data);

      tre::shader::updateUBO_ptsLight(ptsLight_Data);
      tre::shader::updateUBO_ptsShadow(ptsShadow_Data);
    }

    const glm::mat4 mPV = myWindow.m_matProjection3D * myView3D.m_matView;

    // shadow-map render pass --------------

    {
      TRE_PROFILEDSCOPE("shadow", shadow)

      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);

      {
        sunLight_ShadowMap.bindForWritting();
        glClear(GL_DEPTH_BUFFER_BIT);

        const glm::mat4 localMPV = sunLight_ShadowMap.mProj() * sunLight_ShadowMap.mView();

        glUseProgram(shaderShadow.m_drawProgram);

        shaderShadow.setUniformMatrix(localMPV);

        worldScene.mesh.drawcall(worldScene.partId_Main, 1, true);

        shaderShadow.setUniformMatrix(localMPV * worldScene.instance_Tower);
        worldScene.mesh.drawcall(worldScene.partId_Tower, 1, false);

        for (const auto &ti : worldScene.instances_Tree)
        {
          shaderShadow.setUniformMatrix(localMPV * ti);
          worldScene.mesh.drawcall(worldScene.partId_TreeTrunck, 1, false);
          worldScene.mesh.drawcall(worldScene.partId_TreeLeaves, 1, false);
        }

        for (const auto &si : worldScene.instances_Sphere)
        {
          shaderShadow.setUniformMatrix(localMPV * si);
          worldScene.mesh.drawcall(worldScene.partId_Sphere, 1, false);
        }
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

        worldScene.mesh.drawcall(worldScene.partId_Main, 1, true);

        shaderShadow.setUniformMatrix(localMPV * worldScene.instance_Tower);
        worldScene.mesh.drawcall(worldScene.partId_Tower, 1, false);

        for (const auto &ti : worldScene.instances_Tree)
        {
          shaderShadow.setUniformMatrix(localMPV * ti);
          worldScene.mesh.drawcall(worldScene.partId_TreeTrunck, 1, false);
          //worldScene.mesh.drawcall(worldScene.partId_TreeLeaves, 1, false);
        }

        for (const auto &si : worldScene.instances_Sphere)
        {
          shaderShadow.setUniformMatrix(localMPV * si);
          worldScene.mesh.drawcall(worldScene.partId_Sphere, 1, false);
        }
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

      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glDisable(GL_BLEND);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);

      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D,sunLight_ShadowMap.depthHandle());

      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_CUBE_MAP, ptsLight_ShadowMap.depthHandle());

      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, worldScene.texGrass.m_handle);

      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D,worldScene.texWood.m_handle);

      glActiveTexture(GL_TEXTURE6);
      glBindTexture(GL_TEXTURE_2D,worldScene.texWood_normal.m_handle);

      glActiveTexture(GL_TEXTURE7);
      glBindTexture(GL_TEXTURE_2D,worldScene.texTreeTrunck.m_handle);

      glActiveTexture(GL_TEXTURE8);
      glBindTexture(GL_TEXTURE_2D,worldScene.texTreeLeaves.m_handle);

      glActiveTexture(GL_TEXTURE9);
      glBindTexture(GL_TEXTURE_2D,worldScene.texSteal.m_handle);

      glActiveTexture(GL_TEXTURE10);
      glBindTexture(GL_TEXTURE_2D,worldScene.texSteal_mr.m_handle);

      tre::IsOpenGLok("opaque render pass - bind textures");

      glUseProgram(shaderMaterialFlat.m_drawProgram);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowPts0),3);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexDiffuse),4);
      glUniform2f(shaderMaterialFlat.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.98f);
      shaderMaterialFlat.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
      worldScene.mesh.drawcall(worldScene.partId_Main, 1, true);

      glUseProgram(shaderMaterialBumped.m_drawProgram);
      glUniform1i(shaderMaterialBumped.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderMaterialBumped.getUniformLocation(tre::shader::TexShadowPts0),3);
      glUniform1i(shaderMaterialBumped.getUniformLocation(tre::shader::TexDiffuse),5);
      glUniform1i(shaderMaterialBumped.getUniformLocation(tre::shader::TexNormal),6);
      glUniform2f(shaderMaterialBumped.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.5f);
      shaderMaterialBumped.setUniformMatrix(mPV * worldScene.instance_Tower, worldScene.instance_Tower, myView3D.m_matView);
      worldScene.mesh.drawcall(worldScene.partId_Tower, 1, false);

      glUseProgram(shaderMaterialFlat.m_drawProgram);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowPts0),3);
      glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexDiffuse),7);
      glUniform2f(shaderMaterialFlat.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.9f);
      for (const auto &ti : worldScene.instances_Tree)
      {
        shaderMaterialFlat.setUniformMatrix(mPV * ti, ti, myView3D.m_matView);
        worldScene.mesh.drawcall(worldScene.partId_TreeTrunck, 1, false);
      }

      glUseProgram(shaderMetarialDsidedMask.m_drawProgram);
      glUniform1i(shaderMetarialDsidedMask.getUniformLocation(tre::shader::TexShadowSun0),2);
      //glUniform1i(shaderMetarialDsidedMask.getUniformLocation(tre::shader::TexShadowPts0),3);
      glUniform1i(shaderMetarialDsidedMask.getUniformLocation(tre::shader::TexDiffuse),8);
      glUniform2f(shaderMetarialDsidedMask.getUniformLocation(tre::shader::uniBRDF), 0.f, 0.5f);
      for (const auto &ti : worldScene.instances_Tree)
      {
        shaderMetarialDsidedMask.setUniformMatrix(mPV * ti, ti, myView3D.m_matView);
        worldScene.mesh.drawcall(worldScene.partId_TreeLeaves, 1, false);
      }

      glUseProgram(shaderMaterialBrdf.m_drawProgram);
      glUniform1i(shaderMaterialBrdf.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform1i(shaderMaterialBrdf.getUniformLocation(tre::shader::TexShadowPts0),3);
      glUniform1i(shaderMaterialBrdf.getUniformLocation(tre::shader::TexDiffuse),9);
      glUniform1i(shaderMaterialBrdf.getUniformLocation(tre::shader::TexBRDF),10);
      for (const auto &si : worldScene.instances_Sphere)
      {
        shaderMaterialBrdf.setUniformMatrix(mPV * si, si, myView3D.m_matView);
        worldScene.mesh.drawcall(worldScene.partId_Sphere, 1, false);
      }

      tre::IsOpenGLok("opaque render pass - draw Scene");

      glUseProgram(shaderInstancedMesh.m_drawProgram);
      glUniform1i(shaderInstancedMesh.getUniformLocation(tre::shader::TexShadowSun0),2);
      glUniform3f(shaderInstancedMesh.getUniformLocation(tre::shader::uniPhong), 2.f, 0.7f, 0.8f);
      shaderInstancedMesh.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
      worldParticlesMesh.drawInstanced(0, 0, worldParticlesMeshStream.m_countPerMeshId[0], true);
      worldParticlesMesh.drawInstanced(1, worldParticlesMeshStream.m_countPerMeshId[0], worldParticlesMeshStream.m_countPerMeshId[1], false);

      tre::IsOpenGLok("opaque render pass - draw Particle (Mesh)");

      glDepthFunc(GL_LEQUAL);

      glUseProgram(shaderSkybox.m_drawProgram);

      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_CUBE_MAP,worldSkyBoxTex.m_handle);
      glUniform1i(shaderSkybox.getUniformLocation(tre::shader::TexCube),3);

      glm::mat4 MViewBox(myView3D.m_matView);
      MViewBox[3] = glm::vec4(0.f,0.f,0.f,1.f); // no translation

      shaderSkybox.setUniformMatrix(myWindow.m_matProjection3D * MViewBox, glm::mat4(1.f), MViewBox);

      worldSkyboxModel.drawcallAll();

      glDepthFunc(GL_LESS);

      tre::IsOpenGLok("opaque render pass - draw drawground (skyBox)");
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

      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexShadowSun0),2);

      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D,worldParticlesTex.m_handle);
      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexDiffuse),4);

      glUniform3f(shaderInstancedBB.getUniformLocation(tre::shader::uniPhong), 2.f, 0.7f, 0.8f);

      glUniform3f(shaderInstancedBB.getUniformLocation(tre::shader::SoftDistance), 0.2f, myWindow.m_near, myWindow.m_far);

      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, rtResolveMSAA.depthHandle());
      glUniform1i(shaderInstancedBB.getUniformLocation(tre::shader::TexDepth),5);

      glm::mat3 invView = glm::mat3(myView3D.m_matView);
      invView = glm::transpose(invView);

      shaderInstancedBB.setUniformMatrix(mPV);

      glUniformMatrix3fv(shaderInstancedBB.getUniformLocation(tre::shader::MOrientation), 1, GL_FALSE, glm::value_ptr(invView));

      const std::size_t nParticleToDraw = worldParticlesBBStream.size();
      worldParticlesBB.drawInstanced(0, 0, nParticleToDraw);

      tre::IsOpenGLok("main render pass - draw Particle BB");
    }

    // post-effects ----------------

    {
      TRE_PROFILEDSCOPE("postFX", postFX)

      if (withBlur)
      {
        postEffectBlur.processBlur(rtResolveMSAA.colorHandle());

        rtAfterBlur.bindForWritting();
        postEffectBlur.renderBlur(rtResolveMSAA.colorHandle());

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

      if (showMaps)
      {
        sunShadow_debug.draw(myWindow.m_matProjection2D);
      }

      if (true)
      {
        char txtFPS[128];
        snprintf(txtFPS, 127, "%03d fps (Work-elapsed = %03d ms, Swap-latency = %03d ms)",
                 int(1.f/myTimings.frametime),
                 int(myTimings.worktime * 1000),
                 int((myTimings.frametime - myTimings.worktime) * 1000));
        tre::textgenerator::s_textInfo tInfo;
        tInfo.setupBasic(&worldHUDFont, 0.06f, txtFPS, glm::vec2(0.f, -0.08f - 0.08f * 0));
        worldHUDModel.resizePart(0, tre::textgenerator::geometry_VertexCount(tInfo.m_text));
        tre::textgenerator::generate(tInfo, &worldHUDModel, 0, 0, nullptr);

        worldHUDModel.colorizePart(2, showMaps ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(0.8f));
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

        worldHUDModel.drawcall(0, 5, true);
      }

      if (showMaps && withBlur)
      {
        glUseProgram(shaderText2D.m_drawProgram);
        shaderText2D.setUniformMatrix(myWindow.m_matProjection2D);
        glUniform1i(shaderText2D.getUniformLocation(tre::shader::TexDiffuse),3);

        glActiveTexture(GL_TEXTURE3);

        glBindTexture(GL_TEXTURE_2D,postEffectBlur.get_blurTextureUnit(0));
        worldHUDModel.drawcall(5, 1, false);

        glBindTexture(GL_TEXTURE_2D,postEffectBlur.get_blurTextureUnit(1));
        worldHUDModel.drawcall(6, 1, false);

        glBindTexture(GL_TEXTURE_2D,postEffectBlur.get_blurTextureUnit(2));
        worldHUDModel.drawcall(7, 1, false);
      }

      tre::IsOpenGLok("UI render pass - draw UI");
    }

    // profiler ---------------------

    tre::profiler_endframe();

    tre::profiler_updateIntoGPU();
    tre::profiler_draw();

    // end render pass --------------

    myTimings.endFrame_beforeGPUPresent();

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  TRE_LOG("Main loop exited");
  TRE_LOG("Average work elapsed-time needed for each frame: " << myTimings.worktime_average * 1000 << " ms");
  TRE_LOG("Average frame elapsed-time needed for each frame (Vsync enabled): " << myTimings.frametime_average * 1000 << " ms");

  // Finalize

  worldSkyboxModel.clearGPU();
  worldParticlesBB.clearGPU();
  worldParticlesMesh.clearGPU();

  worldScene.clear();

  worldSkyBoxTex.clear();
  worldParticlesTex.clear();

  worldHUDModel.clearGPU();
  worldHUDFont.clear();

  tre::profiler_clearGPU();
  tre::profiler_clearShader();

  shaderShadow.clearShader();
  shaderSkybox.clearShader();
  shaderMaterialFlat.clearShader();
  shaderMaterialBumped.clearShader();
  shaderMaterialBrdf.clearShader();
  shaderMetarialDsidedMask.clearShader();
  shaderInstancedBB.clearShader();
  shaderInstancedMesh.clearShader();
  shaderText2D.clearShader();

  tre::shader::clearUBO();

  sunShadow_debug.clear();

  sunLight_ShadowMap.clear();
  ptsLight_ShadowMap.clear();
  postEffectToneMapping.clear();
  postEffectBlur.clear();
  rtMultisampled.clear();
  rtResolveMSAA.clear();
  rtAfterBlur.clear();

  myWindow.OpenGLQuit();
  myWindow.SDLImageQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
