
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_ui.h"
#include "tre_font.h"
#include "tre_windowContext.h"
#include "tre_contact_3D.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <math.h>
#include <string>
#include <random>
#include <chrono>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

static tre::windowContext             myWindow;
static tre::windowContext::s_controls myControls;

static tre::modelStaticIndexed3D meshRoom = tre::modelStaticIndexed3D(tre::modelStaticIndexed3D::VB_NORMAL /*| tre::modelStaticIndexed3D::VB_UV*/);
const float roomSize = 4.f; // full extend

static tre::modelStaticIndexed3D meshes = tre::modelStaticIndexed3D(tre::modelStaticIndexed3D::VB_NORMAL /*| tre::modelStaticIndexed3D::VB_UV*/);
std::size_t modelPart = 0;
std::size_t NmodelPart = 0;
glm::mat4 modelTransform = glm::mat4(1.f);
std::vector<tre::s_contact3D::s_skinKdTree> meshesSkin;

static tre::modelRaw2D meshQuad;

tre::shader shaderPhong;
tre::shader shaderGGX;

enum e_lightingModel
{
  MODEL_PHONG,
  MODEL_GGX,
  MODELSCOUNT
};
e_lightingModel lightingModel = MODEL_GGX;

tre::shader shaderScreenSpaceNormal; // debug
tre::shader shaderRaytraced; // reference

tre::renderTarget       rtMain(tre::renderTarget::RT_COLOR | tre::renderTarget::RT_DEPTH | tre::renderTarget::RT_COLOR_SAMPLABLE | tre::renderTarget::RT_COLOR_HDR);
tre::postFX_ToneMapping postEffectToneMapping;

const      glm::vec4 roomDiffuse = glm::vec4(0.7f, 0.7f, 0.7f, 1.f);
constexpr float      roomMetalness = 0.f;
constexpr float      roomRoughness = 0.8f;

glm::vec4 meshDiffuse = glm::vec4(1.f,0.f,0.f,1.f);
float     meshMetalness = 0.f;
float     meshRoughness = 0.1f;

bool showRoom = true;
const glm::vec3 sunLightIncomingDir = glm::normalize(glm::vec3(-0.243f,-0.970f,0.f));
const glm::vec3 sunLightColor = glm::vec3(0.9f,1.3f,0.9f);
const glm::vec3 ambiantLightColor = glm::vec3(0.9f,0.9f,1.3f);
float sunLightIntensity = 1.f;
float ambiantLightIntensity = 0.1f;

bool showNormal = false;
bool showRaytrace = false;
bool showRandOnSphere = false;

glm::vec4 mViewEulerAndDistance = glm::vec4(0.f, 0.f, 0.f, 4.f);
glm::mat4 mView = glm::mat4(1.f);

tre::font       worldFont;
tre::baseUI2D   worldUI;
tre::ui::window *worldWin = nullptr;

std::mt19937    rng; // global generator

std::uniform_real_distribution<float> rand01(0.f, 1.f);

typedef std::chrono::steady_clock systemclock;
typedef systemclock::time_point   systemtick;

// =============================================================================

namespace rayTracer
{
  glm::ivec2             res = glm::ivec2(0);
  float                  accumCount = 0.f;
  std::vector<glm::vec4> accumBuffer;
  int                    processStep = 0;
  int                    bounceLimit = 1;

  bool isDurty = false;
  float lastElapsedTime = 0.f; // [s]

  tre::texture textureForRender;

  glm::vec3 genDir_uniform(const glm::vec3 &normal, const glm::vec3 &tangentU, const glm::vec3 &tangentV, float cosThetaMin)
  {
    // distribution = 1 / (2 pi)
    const float cosTheta = cosThetaMin + (1.f - cosThetaMin) * rand01(rng);
    const float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
    const float phi = 2.f * 3.14159265f * rand01(rng);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    return normal * (cosTheta) + tangentU * (sinTheta * cosPhi) + tangentV * (sinTheta * sinPhi);
  }

  glm::vec3 genDir_cosine(const glm::vec3 &normal, const glm::vec3 &tangentU, const glm::vec3 &tangentV, float cosThetaMin)
  {
    // distribution = cos(theta) / pi
    const float cosTheta = std::sqrt(cosThetaMin * cosThetaMin + (1.f - cosThetaMin * cosThetaMin) * rand01(rng));
    const float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
    const float phi = 2.f * 3.14159265f * rand01(rng);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    return normal * (cosTheta) + tangentU * (sinTheta * cosPhi) + tangentV * (sinTheta * sinPhi);
  }

  glm::vec3 genDir_blinnPhong(const glm::vec3 &normal, const glm::vec3 &tangentU, const glm::vec3 &tangentV, float cosThetaMin, float pw)
  {
    // distribution = (cosPower + 1) cos^{pw}(theta) / (2 pi)  * cos(theta)
    //                                                           ^^^^^^^^^^ keep a cosine-weighted term
    const float cosThetaMinPow = std::pow(cosThetaMin, 2.f + pw);
    const float cosTheta = std::pow(cosThetaMinPow + (1.f - cosThetaMinPow) * rand01(rng), 1.f / (2.f + pw));
    const float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
    const float phi = 2.f * 3.14159265f * rand01(rng);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    return normal * (cosTheta) + tangentU * (sinTheta * cosPhi) + tangentV * (sinTheta * sinPhi);
  }

  glm::vec3 genDir_beckmann(const glm::vec3 &normal, const glm::vec3 &tangentU, const glm::vec3 &tangentV, float cosThetaMin, float sigma)
  {
    // distribution = exp(-tan(theta)^2 / sigma^2) / ( sigma^2 cos^4(theta)) * cos(theta)
    //                                                                         ^^^^^^^^^^ keep a cosine-weighted term
    (void)cosThetaMin;
    TRE_ASSERT(1.f / std::sqrt(1.f - sigma * sigma * std::log(1.f - 1.f / 3.141592f)) >= cosThetaMin); // the min value is already greater than cosThetaMin.
    const float cosTheta = 1.f / std::sqrt(1.f - sigma * sigma * std::log(1.f - rand01(rng) / 3.141592f));
    const float sinTheta = std::sqrt(1.f - cosTheta * cosTheta);
    const float phi = 2.f * 3.14159265f * rand01(rng);
    const float cosPhi = std::cos(phi);
    const float sinPhi = std::sin(phi);
    return normal * (cosTheta) + tangentU * (sinTheta * cosPhi) + tangentV * (sinTheta * sinPhi);
  }

  glm::vec3 ray(glm::vec3 pos, glm::vec3 dir, int bounceCount = 0)
  {
    tre::s_contact3D hitInfo;
    hitInfo.penet = std::numeric_limits<float>::infinity();

    // Room hit
    if (showRoom)
    {
      const float invDirX = (dir.x == 0.f) ? 0.f : 1.f /dir.x;
      const float invDirY = (dir.y == 0.f) ? 0.f : 1.f /dir.y;
      const float invDirZ = (dir.z == 0.f) ? 0.f : 1.f /dir.z;
      {
        // bottom
        const float     dist = (-0.5f * roomSize - pos.y) * invDirY;
        const glm::vec3 pt = pos + dist * dir;
        const bool      inRange = std::abs(pt.x) < 0.5f * roomSize && std::abs(pt.z) < 0.5f * roomSize;
        if (dist > 0.f && inRange && dist < hitInfo.penet) { hitInfo.penet = dist; hitInfo.pt = pt; hitInfo.normal = glm::vec3(0.f, 1.f, 0.f); }
      }
      {
        // left
        const float     dist = (-0.5f * roomSize - pos.x) * invDirX;
        const glm::vec3 pt = pos + dist * dir;
        const bool      inRange = std::abs(pt.y) < 0.5f * roomSize && std::abs(pt.z) < 0.5f * roomSize;
        if (dist > 0.f && inRange && dist < hitInfo.penet) { hitInfo.penet = dist; hitInfo.pt = pt; hitInfo.normal = glm::vec3(1.f, 0.f, 0.f); }
      }
      {
        // right
        const float     dist = (0.5f * roomSize - pos.x) * invDirX;
        const glm::vec3 pt = pos + dist * dir;
        const bool      inRange = std::abs(pt.y) < 0.5f * roomSize && std::abs(pt.z) < 0.5f * roomSize;
        if (dist > 0.f && inRange && dist < hitInfo.penet) { hitInfo.penet = dist; hitInfo.pt = pt; hitInfo.normal = glm::vec3(-1.f, 0.f, 0.f); }
      }
      {
        // back
        const float     dist = (-0.5f * roomSize - pos.z) * invDirZ;
        const glm::vec3 pt = pos + dist * dir;
        const bool      inRange = std::abs(pt.x) < 0.5f * roomSize && std::abs(pt.y) < 0.5f * roomSize;
        if (dist > 0.f && inRange && dist < hitInfo.penet) { hitInfo.penet = dist; hitInfo.pt = pt; hitInfo.normal = glm::vec3(0.f, 0.f, 1.f); }
      }
    }

    bool hitMesh = false;

    // Mesh hit
    {
      tre::s_contact3D hitInfoMesh;
      const bool earlyCullMesh = tre::s_contact3D::point_box(pos, meshes.partInfo(modelPart).m_bbox) || tre::s_contact3D::raytrace_box(hitInfoMesh, pos, dir, meshes.partInfo(modelPart).m_bbox, 0.f);
      if (!earlyCullMesh || !tre::s_contact3D::raytrace_skin(hitInfoMesh, pos, dir, meshesSkin[modelPart], 0.f)) hitInfoMesh.penet = 0.f;
      if (hitInfoMesh.penet > 0.f && hitInfoMesh.penet < hitInfo.penet) { hitInfo = hitInfoMesh; hitMesh = true; }
    }

    if (!std::isfinite(hitInfo.penet))
    {
      // no hit: background + sun-light
      return (glm::dot(dir, sunLightIncomingDir) < -0.99f) * sunLightIntensity * sunLightColor + ambiantLightIntensity * ambiantLightColor;
    }

    if (bounceCount >= bounceLimit)
    {
      // bounce limit
      return glm::vec3(0.f);
    }

    // double-sided

    if (glm::dot(-dir, hitInfo.normal) < 0.f) hitInfo.normal = -hitInfo.normal;

    const float dotN = glm::dot(-dir, hitInfo.normal); // >= 0.
    TRE_ASSERT(dotN <= 1.f);

    // there is a hit

    const glm::vec3 matColor = hitMesh ? meshDiffuse   : roomDiffuse;
    const float     matMetal = hitMesh ? meshMetalness : roomMetalness;
    const float     matR2    = hitMesh ? meshRoughness * meshRoughness : roomRoughness * roomRoughness;

    const glm::vec3 tangentU = glm::normalize(glm::cross(hitInfo.normal, -dir));
    const glm::vec3 tangentV = glm::normalize(glm::cross(hitInfo.normal, tangentU));

#if 0
    // choose a facet:
    glm::vec3 facetNormal;
    while (true)
    {
      facetNormal = genDir_uniform(hitInfo.normal, tangentU, tangentV, 1.f - matR2);

      if (glm::dot(facetNormal, -dir) > 0.f) break; // ok, the facet is visible. (TODO: take also account of the facet self-shadowing)
    }
#endif

    // choose outputs direction:
    //const glm::vec3 dirOutReflect = dir - 2.f * glm::dot(dir, facetNormal) * facetNormal;
    glm::vec3 dirOutDiffuse;
    while (true)
    {
      dirOutDiffuse = genDir_uniform(hitInfo.normal, tangentU, tangentV, 0.001f);
      break; //if (glm::dot(dirOutDiffuse, facetNormal) > 0.f) break; // ok, the out-direction is visible from the facet (TODO: take also account of the facet self-shadowing)
    }

    // Evaluating the rendering equation with Monte-Carlo method: L = intg_hemisphere( fr Li n.w dw ) ~= 2 pi Mean_{w uniform distribution}(fr Li n.w)
    // We can use importance-sampling (cosine-weighted integrale): L = intg_hemisphere( fr Li n.w dw ) ~= pi Mean_{w cosine-weighted distribution}(fr Li)

    // Note: We still compute the NDF, but we could also include in the importance-sampling, by selecting facet-normal. (TODO)
    const glm::vec3 half = glm::normalize(-dir + dirOutDiffuse);
    const float dotNH = glm::dot(hitInfo.normal, half);
    const float dotVH = glm::dot(-dir, half);
    float ndf; // note: "pi" factor moved away
    switch (lightingModel)
    {
      case MODEL_PHONG:
      {
        const float matR4 = matR2 * matR2;
        ndf = std::pow(dotNH, 2.f / matR4 - 2.f) / matR4;
      }
      break;
      case MODEL_GGX:
      {
        const float matR4 = matR2 * matR2;
        const float dm = dotNH * dotNH * (matR4 - 1.f) + 1.f;
        ndf = matR4 / (dm * dm);
      }
      break;
      default:
      TRE_FATAL("Not reached: bad model");
    }
    TRE_ASSERT(std::isfinite(ndf));

    // BRDF (Cook-Torrance model):
    const glm::vec3 F0     = glm::mix(glm::vec3(0.04f), matColor, matMetal);
    const glm::vec3 kRefl  = F0 + (1.f - F0) * std::pow(1.f - dotVH, 5.f);
    const glm::vec3 kRefr  = (1.f - kRefl) * (1.f - matMetal);
    TRE_ASSERT(glm::all(glm::isfinite(kRefl)));

    return (kRefr * matColor + ndf * kRefl) /* * pi */ * glm::dot(dirOutDiffuse, hitInfo.normal) * 2.f * ray(hitInfo.pt + 0.001f * dirOutDiffuse, dirOutDiffuse, bounceCount + 1);
  }

  void update()
  {
    if (accumBuffer.size() != res.x * res.y)
    {
      accumBuffer.resize(res.x * res.y);
      textureForRender.clear();
      textureForRender.loadFloat(nullptr, res.x, res.y, tre::texture::MMASK_NEAREST_MAG_FILTER);
      isDurty = true;
    }
    if (isDurty)
    {
      accumCount = 0.f;
      std::memset(accumBuffer.data(), 0, sizeof(glm::vec4) * res.x * res.y);
      processStep = 0;
      isDurty = false;
    }

    // Ray-trace (a single pass at quarter-res)
    {
      const systemtick tickStart = systemclock::now();
      const glm::vec4 mProjInvRed = glm::vec4(1.f/myWindow.m_matProjection3D[0][0], 1.f/myWindow.m_matProjection3D[1][1], 1.f, 1.f / myWindow.m_near);
      const glm::mat3 mViewRotInv = glm::mat3(glm::transpose(mView));
      const glm::vec3 camPos = glm::vec3(- glm::transpose(mView) * mView[3]);
      const int x0 = int(processStep / 2);
      const int y0 = int(processStep % 2);
      for (int y = y0; y < res.y; y += 2)
      {
        for (int x = x0; x < res.x; x += 2)
        {
          const glm::vec4 pxCoordClipSpace = glm::vec4( (float(x) + 0.5f) / float(res.x) * 2.f - 1.f, (float(y) + 0.5f) / float(res.y) * 2.f - 1.f, -1.f, 1.f);
          glm::vec4 pxCoordViewSpace = mProjInvRed * pxCoordClipSpace;
          //pxCoordViewSpace /= pxCoordViewSpace.w; // ok because "w" is positive
          pxCoordViewSpace.w = 0.f;
          const glm::vec3 camDir = glm::normalize(mViewRotInv * glm::vec3(pxCoordViewSpace));

          const glm::vec3 c = ray(camPos, camDir);
          accumBuffer[x + y * res.x] = (accumBuffer[x + y * res.x] * accumCount + glm::vec4(c, 1.f)) / (accumCount + 1.f);
          accumBuffer[x + y * res.x].w = 1.f;
        }
      }
      processStep = (processStep + 1) % 4;
      if (processStep == 0) accumCount += 1.f;
      const systemtick tickEnd = systemclock::now();
      lastElapsedTime = std::chrono::duration<float, std::milli>(tickEnd - tickStart).count() * 1.e-3f;
    }
    TRE_LOG("rayTrace hakf-res: " << int(lastElapsedTime * 1.e4f) * 1.e-1f << " ms"); // tmp here

    // Upload the texture
    {
      textureForRender.updateFloat(accumBuffer.data(), res.x, res.y, false);
    }
  }
}

// =============================================================================

namespace randOnSphere
{
  std::vector<glm::vec3>  points;
  constexpr std::size_t   pointsMaxCount = 20000;

  std::array<float, 128>  distribution;

  tre::modelSemiDynamic3D meshForRender = tre::modelSemiDynamic3D(0, tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);

  bool isDurty = false;

  enum e_Distribution
  {
    DISTRIBUTION_Uniform,
    DISTRIBUTION_CosineWeighted,
  };
  e_Distribution distributionModel = DISTRIBUTION_Uniform;
  float          cosThetaMin = 0.f;

  void addSamples(std::size_t nSamples)
  {
    TRE_ASSERT(points.size() + nSamples <= pointsMaxCount);
    const std::size_t oldCount = points.size();
    points.reserve(oldCount + nSamples);
    for (std::size_t i = 0; i < nSamples; ++i)
    {
      glm::vec3 dir = glm::vec3(0.f, 1.f, 0.f);
      switch (distributionModel)
      {
        case DISTRIBUTION_Uniform:
          dir = rayTracer::genDir_uniform(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), cosThetaMin);
          break;
        case DISTRIBUTION_CosineWeighted:
          dir = rayTracer::genDir_cosine(glm::vec3(0.f, 1.f, 0.f), glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 0.f, 1.f), cosThetaMin);
          break;
      }
      points.push_back(dir);
      const float cosTheta = dir.y;
      const std::size_t cosThetaIdx = std::size_t(cosTheta * float(distribution.size() - 1) + 0.5f);
      distribution[cosThetaIdx] += 1.f;
    }
  }

  void update()
  {
    if (isDurty)
    {
      points.clear();
      distribution.fill(0.f);
      isDurty = false;
    }
    const std::size_t samplesNew = std::min(pointsMaxCount - points.size(), std::size_t(128));
    if (samplesNew == 0) return;
    addSamples(samplesNew);

    // generate the geometry for rendering
    {
      meshForRender.clearParts();
      {
        meshForRender.createRawPart(points.size() * 3);
        auto posIt = meshForRender.layout().m_positions.begin<glm::vec3>();
        auto norIt = meshForRender.layout().m_normals.begin<glm::vec3>();
        for (const auto &p : points)
        {
          *posIt++ = p + 0.01f * glm::vec3(-0.5f, 0.f,  0.2886f); *norIt++ = glm::vec3(0.f, 1.f, 0.f);
          *posIt++ = p + 0.01f * glm::vec3( 0.5f, 0.f,  0.2886f); *norIt++ = glm::vec3(0.f, 1.f, 0.f);
          *posIt++ = p + 0.01f * glm::vec3( 0.0f, 0.f, -0.5773f); *norIt++ = glm::vec3(0.f, 1.f, 0.f);
        }
      }
      for (std::size_t id = 0; id < distribution.size(); ++id)
      {
        const float dvalue = 5.f * distribution[id] / float(points.size());
        constexpr float dp = 1.f / float(distribution.size());
        const glm::mat4 bar = glm::mat4(dvalue, 0.f, 0.f, 0.f,
                                        0.f, dp, 0.f, 0.f,
                                        0.f, 0.f, dp, 0.f,
                                        1.5f + 0.5f * dvalue, (id + 0.5f) * dp, -0.5f * dp, 1.f);
        meshForRender.createPartFromPrimitive_box(bar, 1.f);
      }
    }
    meshForRender.updateIntoGPU();
  }
}

// =============================================================================

int app_init(std::string meshPath)
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);
#ifdef TRE_DEBUG
  const int targetW = int(currentdm.w * 0.3 / 8)*8;
  const int targetH = targetW;
#else
  const int targetW = int(currentdm.w * 0.6 / 8)*8;
  const int targetH = int(currentdm.h * 0.6 / 8)*8;
#endif

  if (!myWindow.SDLCreateWindow(targetW, targetH, "test Lighting", SDL_WINDOW_RESIZABLE) ||
      !myWindow.OpenGLInit())
  {
    TRE_LOG("Error on OpenGL init. Abort.");
    return -2;
  }

  // TMP info (when doing the ray-tracer on GPU ...)
  GLint mb = 0;
  glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &mb);
  TRE_LOG("Max UBO size = " << (mb / 1024) << " KB ; " << (mb / sizeof(float)) << " floats.");

  if (meshPath.empty()) meshPath = TESTIMPORTPATH "resources/objects.obj";

  // Resources: Mesh

  {
    if (!tre::modelImporter::addFromWavefront(meshes, meshPath))
    {
      TRE_LOG("Fail to load the mesh from file " << meshPath << ". Abort.");
      return -3;
    }

    meshesSkin.resize(meshes.partCount());
    for (std::size_t i = 0; i < meshes.partCount(); ++i)
    {
      tre::modelTools::computeSkin3D(meshes.layout(), meshes.partInfo(i), meshesSkin[i].skin);
      meshesSkin[i].computeTree();
    }

    meshes.loadIntoGPU();
  }
  NmodelPart = meshes.partCount();

  {
    static const glm::vec3 quadU[4] = { glm::vec3(1.f, 0.f, 0.f), glm::vec3() };

    std::size_t offsetV = 0;
    meshRoom.createPart(meshRoom.fillDataSquare_ISize() * 4, meshRoom.fillDataSquare_VSize() * 4, offsetV);
    std::size_t offsetI = 0;
    meshRoom.fillDataSquare(0, offsetI, offsetV, glm::mat4(1.f, 0.f, 0.f, 0.f,
                                                           0.f, 1.f, 0.f, 0.f,
                                                           0.f, 0.f, 1.f, 0.f,
                                                           0.f,-0.5f*roomSize, 0.f, 1.f), roomSize, glm::vec4(0.f));
    offsetI += meshRoom.fillDataSquare_ISize(); offsetV += meshRoom.fillDataSquare_VSize();
    meshRoom.fillDataSquare(0, offsetI, offsetV, glm::mat4(0.f, 1.f, 0.f, 0.f,
                                                           1.f, 0.f, 0.f, 0.f,
                                                           0.f, 0.f,-1.f, 0.f,
                                                          -0.5f*roomSize, 0.f, 0.f, 1.f), roomSize, glm::vec4(0.f));
    offsetI += meshRoom.fillDataSquare_ISize(); offsetV += meshRoom.fillDataSquare_VSize();
    meshRoom.fillDataSquare(0, offsetI, offsetV, glm::mat4(0.f, 1.f, 0.f, 0.f,
                                                          -1.f, 0.f, 0.f, 0.f,
                                                           0.f, 0.f, 1.f, 0.f,
                                                           0.5f*roomSize, 0.f, 0.f, 1.f), roomSize, glm::vec4(0.f));
    offsetI += meshRoom.fillDataSquare_ISize(); offsetV += meshRoom.fillDataSquare_VSize();
    meshRoom.fillDataSquare(0, offsetI, offsetV, glm::mat4(1.f, 0.f, 0.f, 0.f,
                                                           0.f, 0.f, 1.f, 0.f,
                                                           0.f,-1.f, 0.f, 0.f,
                                                           0.f, 0.f,-0.5f*roomSize, 1.f), roomSize, glm::vec4(0.f));

    meshRoom.loadIntoGPU();
  }

  {
    meshQuad.createPart(6);
    meshQuad.fillDataRectangle(0, 0, glm::vec4(-1.f,-1.f, 1.f, 1.f), glm::vec4(1.f), glm::vec4(0.f,0.f, 1.f,1.f));
    meshQuad.loadIntoGPU();
  }

  // Material (shader)

  shaderPhong.loadShader(tre::shader::PRGM_3D,
                         tre::shader::PRGM_UNICOLOR |
                         tre::shader::PRGM_LIGHT_SUN |
                         tre::shader::PRGM_MODELPHONG);

  shaderGGX.loadShader(tre::shader::PRGM_3D,
                        tre::shader::PRGM_UNICOLOR |
                        tre::shader::PRGM_LIGHT_SUN);

  {
    tre::shader::s_layout layout(tre::shader::PRGM_3D);
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

  shaderRaytraced.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED);

  // Post Effects

  rtMain.load(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  postEffectToneMapping.set_gamma(2.2f);
  postEffectToneMapping.load();

  // RNG

  rng.seed();

  // Ray-Tracer

  rayTracer::res = myWindow.m_resolutioncurrent;

  randOnSphere::meshForRender.loadIntoGPU();

  // U.I

  worldFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

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
    worldWin->set_layoutGrid(24,2);

    unsigned rowIdx = -1;

    worldWin->create_widgetText(++rowIdx, 0)->set_text("model (F1:F4):")->set_heightModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(rowIdx, 1)->wcb_animate = [](tre::ui::widget* myself, float)
    {
      static_cast<tre::ui::widgetText*>(myself)->set_text(meshes.partInfo(modelPart).m_name);
    };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("diffuse R");
    tre::ui::widget * wCR = worldWin->create_widgetBar(rowIdx, 1)->set_value(meshDiffuse.r)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCR->wcb_modified_ongoing = [](tre::ui::widget* myself) { meshDiffuse.r = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("diffuse G");
    tre::ui::widget * wCG = worldWin->create_widgetBar(rowIdx, 1)->set_value(meshDiffuse.g)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCG->wcb_modified_ongoing = [](tre::ui::widget* myself) { meshDiffuse.g = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("diffuse B");
    tre::ui::widget * wCB = worldWin->create_widgetBar(rowIdx, 1)->set_value(meshDiffuse.b)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wCB->wcb_modified_ongoing = [](tre::ui::widget* myself) { meshDiffuse.b = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("lighting model (F5):")->set_heightModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));
    worldWin->create_widgetText(rowIdx, 1)->wcb_animate = [](tre::ui::widget* myself, float)
    {
      static const std::string listShaderName[MODELSCOUNT] = { "Phong", "GGX" };
      static_cast<tre::ui::widgetText*>(myself)->set_text(listShaderName[lightingModel]);
    };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("ray-tracer bounce limit:");
    worldWin->create_widgetSliderInt(rowIdx, 1)->set_value(rayTracer::bounceLimit)->set_valuemin(1)->set_valuemax(8)->set_withtext(true)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_ongoing = [](tre::ui::widget* myself)
    {
      rayTracer::bounceLimit = static_cast<tre::ui::widgetSliderInt*>(myself)->get_value();
      rayTracer::isDurty = true;
    };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("ray-tracer accum:");
    worldWin->create_widgetText(rowIdx, 1)->wcb_animate = [](tre::ui::widget* myself, float)
    {
      char txt[16];
      std::snprintf(txt, sizeof(txt), "%d", int(rayTracer::accumCount + 0.5f));
      static_cast<tre::ui::widgetText*>(myself)->set_text(txt);
    };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("Test rand: distr:");
    static const std::vector<std::string> listDistName = { "Uniform", "Cos-Weighted" };
    worldWin->create_widgetLineChoice(rowIdx, 1)->set_values(listDistName)->set_selectedIndex(static_cast<unsigned>(randOnSphere::distributionModel))->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget* myself)
    {
      randOnSphere::distributionModel = static_cast<randOnSphere::e_Distribution>(static_cast<tre::ui::widgetLineChoice*>(myself)->get_selectedIndex());
      randOnSphere::isDurty = true;
    };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("Test rand: min cos T:");
    worldWin->create_widgetBar(rowIdx, 1)->set_value(randOnSphere::cosThetaMin)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_ongoing = [](tre::ui::widget* myself) { randOnSphere::cosThetaMin = static_cast<tre::ui::widgetBar*>(myself)->get_value(); randOnSphere::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0, 1, 2)->set_text("material:")->set_heightModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));

    worldWin->create_widgetText(++rowIdx, 0)->set_text("metalness");
    tre::ui::widget * wMM = worldWin->create_widgetBar(rowIdx, 1)->set_value(meshMetalness)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wMM->wcb_modified_ongoing = [](tre::ui::widget* myself) { meshMetalness = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("roughness");
    tre::ui::widget * wMR = worldWin->create_widgetBar(rowIdx, 1)->set_value(meshRoughness)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wMR->wcb_modified_ongoing = [](tre::ui::widget* myself) { meshRoughness = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0, 1, 2)->set_text("environment:")->set_heightModifier(1.2f)->set_color(glm::vec4(1.f, 1.f, 0.2f, 1.f));

    worldWin->create_widgetText(++rowIdx, 0, 1, 2)->set_text("show normal (F6)\nshow ray-trace (F7)\nshow rand on sphere (F8)");

    worldWin->create_widgetText(++rowIdx, 0)->set_text("show room");
    tre::ui::widget *wRoom = worldWin->create_widgetBoxCheck(rowIdx, 1)->set_value(showRoom)->set_iseditable(true)->set_isactive(true);
    wRoom->wcb_modified_finished = [](tre::ui::widget* myself) { showRoom = static_cast<tre::ui::widgetBoxCheck*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("gamma correction");
    tre::ui::widget * wGC = worldWin->create_widgetBoxCheck(rowIdx, 1)->set_value(true)->set_iseditable(true)->set_isactive(true);
    wGC->wcb_modified_finished = [](tre::ui::widget* myself) { postEffectToneMapping.set_gamma(static_cast<tre::ui::widgetBoxCheck*>(myself)->get_value() ? 2.2f : 1.f); };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("exposure");
    tre::ui::widget * wEP = worldWin->create_widgetBar(rowIdx, 1)->set_value(1.4f)->set_valuemax(2.f)->set_iseditable(true)->set_isactive(true);
    wEP->wcb_modified_ongoing = [](tre::ui::widget* myself) { postEffectToneMapping.set_exposure(static_cast<tre::ui::widgetBar*>(myself)->get_value()); };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("light intensity");
    tre::ui::widget * wEI = worldWin->create_widgetBar(rowIdx, 1)->set_value(sunLightIntensity)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wEI->wcb_modified_ongoing = [](tre::ui::widget* myself) { sunLightIntensity = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    worldWin->create_widgetText(++rowIdx, 0)->set_text("ambiant intensity");
    tre::ui::widget * wEA = worldWin->create_widgetBar(rowIdx, 1)->set_value(ambiantLightIntensity)->set_withtext(true)->set_withborder(true)->set_iseditable(true)->set_isactive(true);
    wEA->wcb_modified_ongoing = [](tre::ui::widget* myself) { ambiantLightIntensity = static_cast<tre::ui::widgetBar*>(myself)->get_value(); rayTracer::isDurty = true; };

    // load
    worldUI.loadIntoGPU();
    worldUI.loadShader();
  }

  // End Init

  tre::IsOpenGLok("main: initialization");

  tre::checkLayoutMatch_Shader_Model(&shaderPhong, &meshes);
  tre::checkLayoutMatch_Shader_Model(&shaderGGX, &meshes);

  myWindow.OpenGLCamera(0.05f, 50.f, 60.f);

  return 0;
}

// =============================================================================

void app_update()
{
  SDL_Event event;

  tre::shader::s_UBOdata_sunLight sunLight;
  sunLight.direction = sunLightIncomingDir;
  sunLight.color = sunLightIntensity * sunLightColor;
  sunLight.colorAmbiant = ambiantLightIntensity * ambiantLightColor;

  {
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
        if      (event.key.keysym.sym == SDLK_F1) { modelPart = 0; rayTracer::isDurty = true; }
        else if (event.key.keysym.sym == SDLK_F2) { modelPart = modelPart == 0 ? NmodelPart - 1 : modelPart - 1; rayTracer::isDurty = true; }
        else if (event.key.keysym.sym == SDLK_F3) { modelPart = modelPart == NmodelPart - 1 ? 0 : modelPart + 1; rayTracer::isDurty = true; }
        else if (event.key.keysym.sym == SDLK_F4) { modelPart = NmodelPart - 1; rayTracer::isDurty = true; }
        else if (event.key.keysym.sym == SDLK_F5) { lightingModel = static_cast<e_lightingModel>((lightingModel + 1) % MODELSCOUNT); rayTracer::isDurty = true; }
        else if (event.key.keysym.sym == SDLK_F6) { showNormal = !showNormal; showRaytrace = false; showRandOnSphere = false; }
        else if (event.key.keysym.sym == SDLK_F7) { showRaytrace = !showRaytrace; showNormal = false; showRandOnSphere = false; }
        else if (event.key.keysym.sym == SDLK_F8) { showRandOnSphere = !showRandOnSphere; showNormal = false; }
      }
    }

    if (myControls.m_keyUP)    { mViewEulerAndDistance.x += 0.01f; rayTracer::isDurty = true; }
    if (myControls.m_keyDOWN)  { mViewEulerAndDistance.x -= 0.01f; rayTracer::isDurty = true; }
    if (myControls.m_keyLEFT)  { mViewEulerAndDistance.y += 0.01f; rayTracer::isDurty = true; }
    if (myControls.m_keyRIGHT) { mViewEulerAndDistance.y -= 0.01f; rayTracer::isDurty = true; }

    if (myControls.m_keyCTRL)  { mViewEulerAndDistance.w += 0.1f; rayTracer::isDurty = true; }
    if (myControls.m_keySHIFT) { mViewEulerAndDistance.w -= 0.1f; rayTracer::isDurty = true; }

    if (myWindow.m_viewportResized)
    {
      worldUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
      rtMain.resize(myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

      glm::mat3 wmat(1.f);
      wmat[2][0] = -1.f / myWindow.m_matProjection2D[0][0] + 0.01f;
      wmat[2][1] = 0.99f;
      worldWin->set_mat3(wmat);

      rayTracer::res = myWindow.m_resolutioncurrent;
    }

    // compute the view

    mView = glm::eulerAngleXYZ(mViewEulerAndDistance.x, mViewEulerAndDistance.y, mViewEulerAndDistance.z);
    mView[3][0] = 0.f;
    mView[3][1] = 0.f;
    mView[3][2] = -mViewEulerAndDistance.w;

    // main update ------------------

    if (showRaytrace)
    {
      rayTracer::update();
    }

    if (showRandOnSphere)
    {
      randOnSphere::update();
    }

    // main render pass -------------

    tre::shader::updateUBO_sunLight(sunLight);

    rtMain.bindForWritting();

    if (showRandOnSphere)
    {
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      const glm::mat4 mPV(myWindow.m_matProjection3D * mView);
      glUseProgram(shaderPhong.m_drawProgram);
      shaderPhong.setUniformMatrix(mPV * modelTransform, modelTransform, mView);
      if (shaderPhong.layout().hasUNI_uniColor) glUniform4f(shaderPhong.getUniformLocation(tre::shader::uniColor), 0.2f, 1.f, 0.2f, 1.f);
      if (shaderPhong.layout().hasUNI_uniMat)   glUniform2f(shaderPhong.getUniformLocation(tre::shader::uniMat), 0.f, 0.9f);
      randOnSphere::meshForRender.drawcallAll();
    }
    else if (showRaytrace)
    {
      glDisable(GL_DEPTH_TEST);
      glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, rayTracer::textureForRender.m_handle);

      glUseProgram(shaderRaytraced.m_drawProgram);
      shaderRaytraced.setUniformMatrix(glm::mat3(1.f));
      glUniform1i(shaderRaytraced.getUniformLocation(tre::shader::TexDiffuse),2);
      meshQuad.drawcall(0, 1);
    }
    else
    {
      glEnable(GL_DEPTH_TEST);
      glClearColor(ambiantLightIntensity * ambiantLightColor.r, ambiantLightIntensity * ambiantLightColor.g, ambiantLightIntensity * ambiantLightColor.b, 1.f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glClearColor(0.f, 0.f, 0.f, 0.f);
      tre::shader & curShader = showNormal ? shaderScreenSpaceNormal : ((lightingModel == 1) ? shaderGGX : shaderPhong);
      const glm::mat4 mPV(myWindow.m_matProjection3D * mView);
      glUseProgram(curShader.m_drawProgram);

      if (showRoom)
      {
        curShader.setUniformMatrix(mPV, glm::mat4(1.f), mView);
        if (curShader.layout().hasUNI_uniColor) glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(roomDiffuse));
        if (curShader.layout().hasUNI_uniMat)   glUniform2f(curShader.getUniformLocation(tre::shader::uniMat), roomMetalness, roomRoughness);
        meshRoom.drawcall(0, 1);
      }

      curShader.setUniformMatrix(mPV * modelTransform, modelTransform, mView);
      if (curShader.layout().hasUNI_uniColor) glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(meshDiffuse));
      if (curShader.layout().hasUNI_uniMat)   glUniform2f(curShader.getUniformLocation(tre::shader::uniMat), meshMetalness, meshRoughness);
      meshes.drawcall(modelPart, 1);
    }

    // Post-Effects ---------------

    glDisable(GL_DEPTH_TEST);
    postEffectToneMapping.resolveToneMapping(rtMain.colorHandle(), myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

    // UI-render pass -------------

    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);

    worldUI.animate(0.016f);
    worldUI.updateIntoGPU();

    worldUI.draw();

    tre::IsOpenGLok("UI pass");

    // end render pass --------------

    SDL_GL_SwapWindow( myWindow.m_window );
  }
}

// =============================================================================

void app_quit()
{
  meshRoom.clearGPU();
  meshes.clearGPU();
  meshQuad.clearGPU();

  worldUI.clear();
  worldUI.clearGPU();
  worldUI.clearShader();
  worldFont.clear();

  shaderPhong.clearShader();
  shaderGGX.clearShader();
  shaderScreenSpaceNormal.clearShader();
  shaderRaytraced.clearShader();

  tre::shader::clearUBO();

  rtMain.clear();
  postEffectToneMapping.clear();

  rayTracer::textureForRender.clear();
  randOnSphere::meshForRender.clearGPU();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");
}

// =============================================================================

int main(int argc, char **argv)
{

  if (app_init((argc > 1) ? argv[1] : "") != 0)
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
