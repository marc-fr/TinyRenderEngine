
#include "tre_shader.h"
#include "tre_rendertarget.h"
#include "tre_model_importer.h"
#include "tre_font.h"
#include "tre_textgenerator.h"
#include "tre_windowContext.h"
#include "tre_gpusim.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <random>
#include <string>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// Debug ======================================================================

struct s_shadowDebug
{
  tre::shader     shader;
  tre::modelRaw2D geom;
  bool            isLoaded = false;

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

    isLoaded = true;
  }

  void draw(const glm::mat3 &mProj2D, bool withNearMap) const
  {
    if (!isLoaded) return;

    glUseProgram(shader.m_drawProgram);

    {
      glUniform1i(shader.getUniformLocation(tre::shader::TexDiffuse), 2);
      const glm::mat3 mModel_sunDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                  0.0f, 0.3f, 0.f,
                                                  1.0f, 0.6f, 1.f);
      shader.setUniformMatrix(mProj2D * mModel_sunDebug);
      geom.drawcall(0, 1);
    }

    if (withNearMap)
    {
      glUniform1i(shader.getUniformLocation(tre::shader::TexDiffuse), 3);
      const glm::mat3 mModel_sunDebug = glm::mat3(0.3f, 0.0f, 0.f,
                                                  0.0f, 0.3f, 0.f,
                                                  1.0f, -0.6f, 1.f);
      shader.setUniformMatrix(mProj2D * mModel_sunDebug);
      geom.drawcall(0, 1);
    }
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

static constexpr float kBound = 100.f;

static std::uniform_real_distribution kRand01(0.f, 1.f);
static std::mt19937                   kRNG;

// Rigid-body simulation ======================================================

// Pool of 2^N x 2^N slots (one slot = one instance), for the CPU and the GPU simulations.
static constexpr unsigned kPoolSizeLog2 = 6;
static constexpr unsigned kPoolSize     = 1u << (2 * kPoolSizeLog2);

static constexpr float kSpawnRate = 20.f; ///< [#/s]
static constexpr float kLifeMin   = 40.f; ///< [s]
static constexpr float kLifeMax   = 60.f; ///< [s]
static constexpr float kFadeTime  = 1.f;  ///< [s] the bodies shrink before their death

static float randomLifeTime() { return kLifeMin + kRand01(kRNG) * (kLifeMax - kLifeMin); }

static constexpr float kRB_HalfSize    = 0.5f;
static constexpr float kRB_InvMass     = 1.f;
static constexpr float kRB_InvInertia  = 6.f; // cube: I = m * s^2 / 6 (isotropic)
static constexpr float kRB_Restitution = 0.8f;

static glm::mat3 quatToMat3(const glm::vec4 &q) // q = (x,y,z,w), normalized
{
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  return glm::mat3(1.f - 2.f * (y * y + z * z), 2.f * (x * y + w * z), 2.f * (x * z - w * y),
                   2.f * (x * y - w * z), 1.f - 2.f * (x * x + z * z), 2.f * (y * z + w * x),
                   2.f * (x * z + w * y), 2.f * (y * z - w * x), 1.f - 2.f * (x * x + y * y));
}

/// Cubes of edge 1 (mass 1), drifting without gravity, bouncing on the infinite plane y=0.
/// Contact: the deepest corner only, position correction + normal impulse (no friction).
struct s_rigidBody
{
  // Same layout as the GPU fields (16 floats)
  glm::vec3 pos;    float lifeRatio = 1.f;
  glm::vec4 rot; ///< quaternion (x,y,z,w)
  glm::vec3 vel;    float invLifeTime = 0.f;
  glm::vec3 angVel; float _pad = 0.f;

  static constexpr unsigned kFieldCount   = 16;
  static constexpr unsigned kFieldInvLife = 11;

  bool alive() const { return lifeRatio < 1.f; }

  /// Spawn a body with a life-time, already "age" seconds old.
  void spawn(float lifeTime, float age = 0.f)
  {
    pos = glm::vec3(kRand01(kRNG) * 2.f - 1.f, kRand01(kRNG), kRand01(kRNG) * 2.f - 1.f) * 0.9f * kBound;
    vel = (glm::vec3(kRand01(kRNG), kRand01(kRNG), kRand01(kRNG)) * 2.f - 1.f) * 0.5f;
    angVel = glm::normalize(glm::vec3(kRand01(kRNG) * 2.f - 1.f, 1.f, kRand01(kRNG) * 2.f - 1.f)) * kRand01(kRNG) * 0.4f;
    rot = glm::vec4(0.f, 0.f, 0.f, 1.f);
    invLifeTime = 1.f / lifeTime;
    lifeRatio = age * invLifeTime;
    _pad = 0.f;
  }

  /// Advance an alive body.
  void update(float dt)
  {
    if (std::abs(pos.x) > kBound || pos.y > kBound || std::abs(pos.z) > kBound) { lifeRatio = 1.f; return; } // kill

    lifeRatio += dt * invLifeTime;

    // integrate
    pos += vel * dt;
    const glm::vec4 dq = glm::vec4(rot.w * angVel + glm::cross(angVel, glm::vec3(rot)), -glm::dot(angVel, glm::vec3(rot)));
    rot = glm::normalize(rot + (0.5f * dt) * dq);

    // contact with the plane y=0, at the deepest corner
    const glm::mat3 R = quatToMat3(rot);
    const glm::vec3 r = -kRB_HalfSize * (glm::sign(R[0].y) * R[0] + glm::sign(R[1].y) * R[1] + glm::sign(R[2].y) * R[2]);
    const float     depth = -(pos.y + r.y);
    if (depth <= 0.f) return;
    pos.y += depth;
    const float vn = vel.y + glm::cross(angVel, r).y;
    if (vn >= 0.f) return;
    const glm::vec3 rxn = glm::cross(r, glm::vec3(0.f, 1.f, 0.f));
    const float     jn = -(1.f + kRB_Restitution) * vn / (kRB_InvMass + kRB_InvInertia * glm::dot(rxn, rxn));
    vel.y  += jn * kRB_InvMass;
    angVel += (jn * kRB_InvInertia) * rxn;
  }

  /// Instance data: (position, scale), then the 3 columns of the orientation. A dead body has a null scale (hidden).
  void writeInstance(glm::vec4 *dst) const
  {
    if (!alive())
    {
      for (int i = 0; i < 4; ++i) dst[i] = glm::vec4(0.f);
      return;
    }
    const float     scale = glm::clamp((1.f - lifeRatio) / (kFadeTime * invLifeTime), 0.f, 1.f);
    const glm::mat3 rotM  = quatToMat3(rot);
    dst[0] = glm::vec4(pos, scale);
    dst[1] = glm::vec4(rotM[0], 0.f);
    dst[2] = glm::vec4(rotM[1], 0.f);
    dst[3] = glm::vec4(rotM[2], 0.f);
  }
};
static_assert(sizeof(s_rigidBody) == s_rigidBody::kFieldCount * sizeof(float), "s_rigidBody must match the GPU fields layout");

// CPU rigid-bodies ===========================================================

struct s_cpuRigidBodies
{
  std::vector<s_rigidBody>   bodies;    ///< per slot
  std::vector<float>         timeLeft;  ///< per slot: time before the slot is released. 0 means free.
  std::vector<std::uint32_t> freeSlots; ///< stack of the free slots

  unsigned reservedCount() const { return kPoolSize - unsigned(freeSlots.size()); }

  void reset()
  {
    bodies.assign(kPoolSize, s_rigidBody());
    timeLeft.assign(kPoolSize, 0.f);
    rebuildFreeSlots();
  }

  void rebuildFreeSlots() // the stack pops the lowest slots first
  {
    freeSlots.clear();
    for (unsigned slot = kPoolSize; slot-- != 0; )
      if (timeLeft[slot] <= 0.f) freeSlots.push_back(slot);
  }

  bool spawn(const s_rigidBody &body, float lifeTime)
  {
    TRE_ASSERT(lifeTime > 0.f);
    if (freeSlots.empty()) return false;
    const std::uint32_t slot = freeSlots.back();
    freeSlots.pop_back();
    bodies[slot]   = body;
    timeLeft[slot] = lifeTime;
    return true;
  }

  void simulate(float dt)
  {
    for (unsigned slot = 0; slot < kPoolSize; ++slot)
    {
      if (bodies[slot].alive()) bodies[slot].update(dt);
      float &tl = timeLeft[slot];
      if (tl <= 0.f) continue;
      tl -= dt;
      if (tl <= 0.f)
      {
        tl = 0.f;
        freeSlots.push_back(slot);
      }
    }
  }

  void writeInstances(glm::vec4 *dst) const
  {
    for (const s_rigidBody &b : bodies) { b.writeInstance(dst); dst += 4; }
  }
};

s_cpuRigidBodies cpuRigidBodies;

// GPU rigid-bodies ===========================================================

tre::gpuSim gpuRigidBodies;

static const char *kKernelEvolve =
  "  vec3  pos     = field[0].xyz;\n"
  "  float lifeR   = field[0].w;\n"
  "  vec4  rot     = field[1];\n"
  "  vec3  vel     = field[2].xyz;\n"
  "  float invLife = field[2].w;\n"
  "  vec3  angVel  = field[3].xyz;\n"
  "  if (lifeR >= 1.0) { return; } // killed\n"
  "  if (abs(pos.x) > kBound || pos.y > kBound || abs(pos.z) > kBound) { field[0].w = 1.0; return; } // kill\n"
  "  // integrate\n"
  "  pos += vel * gsDt;\n"
  "  vec4 dq = vec4(rot.w * angVel + cross(angVel, rot.xyz), -dot(angVel, rot.xyz));\n"
  "  rot = normalize(rot + (0.5 * gsDt) * dq);\n"
  "  // contact with the plane y=0, at the deepest corner\n"
  "  mat3  R = quatToMat3(rot);\n"
  "  vec3  r = -kHalfSize * (sign(R[0].y) * R[0] + sign(R[1].y) * R[1] + sign(R[2].y) * R[2]);\n"
  "  float depth = -(pos.y + r.y);\n"
  "  if (depth > 0.0)\n"
  "  {\n"
  "    pos.y += depth;\n"
  "    float vn = vel.y + cross(angVel, r).y;\n"
  "    if (vn < 0.0)\n"
  "    {\n"
  "      vec3  rxn = cross(r, vec3(0.0, 1.0, 0.0));\n"
  "      float jn = -(1.0 + kRestitution) * vn / (kInvMass + kInvInertia * dot(rxn, rxn));\n"
  "      vel.y  += jn * kInvMass;\n"
  "      angVel += (jn * kInvInertia) * rxn;\n"
  "    }\n"
  "  }\n"
  "\n"
  "  field[0] = vec4(pos, lifeR + gsDt * invLife); // pos + lifeRatio\n"
  "  field[1] = rot;\n"
  "  field[2] = vec4(vel, invLife);\n"
  "  field[3] = vec4(angVel, 0.0);\n";

static const char *kKernelDraw =
  "  float scale = 0.0;\n"
  "  if (field[0].w < 1.0) scale = clamp((1.0 - field[0].w) / (kFadeTime * field[2].w), 0.0, 1.0);\n"
  "  mat3 R = quatToMat3(field[1]);\n"
  "  inst[0] = vec4(field[0].xyz, scale);\n"
  "  inst[1] = vec4(R[0], 0.0);\n"
  "  inst[2] = vec4(R[1], 0.0);\n"
  "  inst[3] = vec4(R[2], 0.0);\n";

static bool loadGpuRigidBodies()
{
  tre::gpuSim::s_config c;
  c.name              = "GPURigidBodies";
  c.poolSizeLog2      = kPoolSizeLog2;
  c.fieldCount        = s_rigidBody::kFieldCount;
  c.invLife           = s_rigidBody::kFieldInvLife;
  c.instanceVec4Count = 4;
  c.firstTextureUnit  = 5; // units 2,3,4 are used by the shadow-maps and the HUD

  c.glslCommon = std::string() +
    "const float kHalfSize    = " + std::to_string(kRB_HalfSize) + ";\n"
    "const float kInvMass     = " + std::to_string(kRB_InvMass) + ";\n"
    "const float kInvInertia  = " + std::to_string(kRB_InvInertia) + ";\n"
    "const float kRestitution = " + std::to_string(kRB_Restitution) + ";\n"
    "const float kBound       = " + std::to_string(kBound) + ";\n"
    "const float kFadeTime    = " + std::to_string(kFadeTime) + ";\n"
    "mat3 quatToMat3(vec4 q)\n"
    "{\n"
    "  float x = q.x, y = q.y, z = q.z, w = q.w;\n"
    "  return mat3(1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y + w * z), 2.0 * (x * z - w * y),\n"
    "              2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z), 2.0 * (y * z + w * x),\n"
    "              2.0 * (x * z + w * y), 2.0 * (y * z - w * x), 1.0 - 2.0 * (x * x + y * y));\n"
    "}\n";

  c.kernelEvolve = kKernelEvolve;
  c.kernelDraw   = kKernelDraw;

  return gpuRigidBodies.load(c);
}

// CPU <-> GPU ================================================================

bool simOnGPU = false;

static void transferCpuToGpu()
{
  gpuRigidBodies.reset();
  for (unsigned k = 0; k < kPoolSize; ++k)
  {
    if (cpuRigidBodies.bodies[k].lifeRatio >= 1.f) continue;
    gpuRigidBodies.spawn(0.f, reinterpret_cast<const float*>(&cpuRigidBodies.bodies[k]), 1);
    // Note: GPU-sim will have the current objects with lifeRatio = 0.
  }
}

static void transferGpuToCpu()
{
  const unsigned texCount = (gpuRigidBodies.config().fieldCount + 3) / 4;
  const GLsizei  size     = GLsizei(gpuRigidBodies.textureSize());

  std::vector<glm::vec4> planes(std::size_t(texCount) * kPoolSize);

  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  for (unsigned t = 0; t < texCount; ++t)
  {
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gpuRigidBodies.fieldTexture(t), 0);
    glReadPixels(0, 0, size, size, GL_RGBA, GL_FLOAT, &planes[std::size_t(t) * kPoolSize]);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);

  tre::IsOpenGLok("GPU rigid-bodies: download");

  cpuRigidBodies.reset();
  for (unsigned slot = 0; slot < kPoolSize; ++slot)
  {
    const float tl = gpuRigidBodies.timeLeft(slot);
    if (tl <= 0.f) continue;
    glm::vec4 *dst = reinterpret_cast<glm::vec4*>(&cpuRigidBodies.bodies[slot]);
    for (unsigned t = 0; t < texCount; ++t) dst[t] = planes[std::size_t(t) * kPoolSize + slot];
    cpuRigidBodies.timeLeft[slot] = tl;
  }
  cpuRigidBodies.rebuildFreeSlots();
}

/// Spawn one body, into the active simulation.
static void spawnBody(float lifeTime, float age = 0.f)
{
  s_rigidBody b;
  b.spawn(lifeTime, age);
  const float tl = lifeTime - age;
  if (tl <= 0.f) return;
  if (simOnGPU) gpuRigidBodies.spawn(0.f, reinterpret_cast<const float*>(&b), 1);
  else          cpuRigidBodies.spawn(b, tl);
}

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

tre::renderTarget_ShadowMap sunLight_ShadowMapFar;
tre::renderTarget_ShadowMap sunLight_ShadowMapNear;

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

  std::random_device rd;
  kRNG.seed(rd());

  // load meshes

  meshPlane.setFlags(tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
  meshPlane.createPartFromPrimitive_square(glm::mat4(1.f), kBound * 2.f);
  meshPlane.loadIntoGPU();

  meshes.setFlags(tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
  meshes.setFlagsInstanced(tre::modelInstanced::VI_POSITION | tre::modelInstanced::VI_ORIENTATION);
  meshes.createPartFromPrimitive_box(glm::mat4(1.f), 1.f);
  meshes.loadIntoGPU();

  // render targets

  sunLight_ShadowMapFar.load(512, 512);
  sunLight_ShadowMapNear.load(1024, 1024);

  // load HUD

  worldHUDFont.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  {
    static const char* txts[5] = { "FPS",
                                   "right clic: lock/unlock camera",
                                   "F5: show/hide render-target",
                                   "F6: enable/disable near-map",
                                   "F7: switch CPU/GPU simulation",
                                 };

    for (int it = 0; it < 5; ++it)
    {
      tre::textgenerator::s_textInfo tInfo;
      tInfo.setupBasic(&worldHUDFont, txts[it], glm::vec2(0.f, -0.08f * it));
      tInfo.setupSize(0.06f);
      tInfo.m_fontPixelSize = 14;
      worldHUDModel.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text));
      tre::textgenerator::generate(tInfo, &worldHUDModel, std::size_t(it), 0, nullptr);
    }

    worldHUDModel.loadIntoGPU();
  }

  // material (shaders)

  shaderMaterialFlat.setShadowSunSamplerCount(2);
  shaderMaterialFlat.loadShader(tre::shader::PRGM_3D,
                               tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN);

  shaderMaterialInstanced.setShadowSunSamplerCount(2);
  shaderMaterialInstanced.loadShader(tre::shader::PRGM_3D,
                                  tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN |
                                  tre::shader::PRGM_INSTANCED | tre::shader::PRGM_ORIENTATION);

  shaderDepth.loadShader(tre::shader::PRGM_3D_DEPTH, 0);

  shaderDepthInstanced.loadShader(tre::shader::PRGM_3D_DEPTH, tre::shader::PRGM_INSTANCED | tre::shader::PRGM_ORIENTATION);

  shaderText2D.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_TEXTURED | tre::shader::PRGM_COLOR);

  // debug shadow

  sunShadow_debug.load();

  // GPU rigid-bodies

  if (!loadGpuRigidBodies())
    TRE_LOG("GPU rigid-bodies: failed to load, only the CPU simulation is available.");

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

  meshes.resizeInstance(kPoolSize);
  meshes.updateIntoGPU();

  cpuRigidBodies.reset();
  const unsigned initialCount = unsigned(kSpawnRate * 0.5f * (kLifeMin + kLifeMax));
  for (unsigned i = 0; i < initialCount; ++i)
  {
    const float lifeTime = randomLifeTime();
    spawnBody(lifeTime, kRand01(kRNG) * lifeTime);
  }

  return 0;
}

// ============================================================================

bool  showMaps = false;
bool  nearMap = true;
float spawnAccumulator = 0.f; ///< fractional nbr of bodies to spawn

void app_update()
{
  // - global

  tre::shader::s_UBOdata_sunLight sunLight_Data;
  sunLight_Data.color = glm::vec3(0.8f);
  sunLight_Data.colorAmbiant = glm::vec3(0.1f);

  sunLight_Data.nShadow = nearMap ? 2 : 1;

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
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F6) nearMap = !nearMap;
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_F7 && gpuRigidBodies.isLoaded())
      {
        simOnGPU = !simOnGPU;
        if (simOnGPU) transferCpuToGpu();
        else          transferGpuToCpu();
      }
    }

    if (myWindow.m_hasFocus) myView3D.treatControlEvent(myControls, myTimings.frametime);

    if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED) myView3D.setMouseBinding(!myView3D.m_mouseBound);
  }

  // world simulation -------------------

  // sun
  {
    const float sunTheta = 0.4f + 0.3f * std::sin(myTimings.scenetime * (6.28f * 0.02f));
    sunLight_Data.direction = -glm::vec3( 0.f, std::cos(sunTheta), std::sin(sunTheta) );
  }

  const float simDt = std::min(myTimings.frametime, 1.f / 20.f);

  if (!myControls.m_pause)
  {
    // spawn at a constant rate, into the active simulation
    spawnAccumulator += kSpawnRate * simDt;
    while (spawnAccumulator >= 1.f)
    {
      spawnBody(randomLifeTime());
      spawnAccumulator -= 1.f;
    }

    if (simOnGPU) gpuRigidBodies.simulate(simDt);
    else          cpuRigidBodies.simulate(simDt);
  }

  if (!simOnGPU)
  {
    // fill the instance buffer
    TRE_ASSERT(meshes.sizeInstance() == kPoolSize);
    TRE_ASSERT(meshes.layout().m_instancedPositions.m_stride == 16);
    cpuRigidBodies.writeInstances(reinterpret_cast<glm::vec4*>(meshes.bufferInstanced())); // unsafe ...
  }
  else
  {
    gpuRigidBodies.exportInstances(meshes.instancedBufferHandle()); // GPU -> GPU (render-to-texture + pixel-pack buffer)
  }

  // shadow maps
  {
    sunLight_ShadowMapFar.setSceneBox(tre::s_boundbox(glm::vec3(-kBound*1.1f), glm::vec3(kBound*1.1f)));
    sunLight_ShadowMapFar.computeUBO_forMap(sunLight_Data, 0);

    const glm::vec3 boxCenter = myView3D.position();
    sunLight_ShadowMapNear.setSceneBox(tre::s_boundbox(boxCenter - 0.1f * kBound, boxCenter + 0.1f * kBound));
    sunLight_ShadowMapNear.computeUBO_forMap(sunLight_Data, 1, 1.f, -kBound);
  }

  // upload
  {
    tre::shader::updateUBO_sunLight(sunLight_Data);

    if (!simOnGPU) meshes.updateIntoGPU();
  }

  const glm::mat4 mPV = myWindow.m_matProjection3D * myView3D.m_matView;

  // shadow-map render pass --------------

  {
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);

    // Far map
    {
      sunLight_ShadowMapFar.bindForWritting();
      glClear(GL_DEPTH_BUFFER_BIT);
      const glm::mat4 localMPV = sunLight_ShadowMapFar.mProj() * sunLight_ShadowMapFar.mView();

      glUseProgram(shaderDepthInstanced.m_drawProgram);
      shaderDepthInstanced.setUniformMatrix(localMPV);
      meshes.drawInstanced(0, 0, kPoolSize);
    }

    if (nearMap) // Near map
    {
      sunLight_ShadowMapNear.bindForWritting();
      glClear(GL_DEPTH_BUFFER_BIT);
      const glm::mat4 localMPV = sunLight_ShadowMapNear.mProj() * sunLight_ShadowMapNear.mView();

      glUseProgram(shaderDepthInstanced.m_drawProgram);
      shaderDepthInstanced.setUniformMatrix(localMPV);
      meshes.drawInstanced(0, 0, kPoolSize);
    }

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
    glBindTexture(GL_TEXTURE_2D,sunLight_ShadowMapFar.depthHandle());

    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D,sunLight_ShadowMapNear.depthHandle());

    glUseProgram(shaderMaterialFlat.m_drawProgram);
    glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowSun0),2);
    glUniform1i(shaderMaterialFlat.getUniformLocation(tre::shader::TexShadowSun1),3);
    glUniform4f(shaderMaterialFlat.getUniformLocation(tre::shader::uniColor), 0.6f, 0.6f, 0.6f, 1.f);
    glUniform2f(shaderMaterialFlat.getUniformLocation(tre::shader::uniMat), 0.f, 0.98f);
    shaderMaterialFlat.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
    meshPlane.drawcallAll();

    glUseProgram(shaderMaterialInstanced.m_drawProgram);
    glUniform1i(shaderMaterialInstanced.getUniformLocation(tre::shader::TexShadowSun0),2);
    glUniform1i(shaderMaterialInstanced.getUniformLocation(tre::shader::TexShadowSun1),3);
    glUniform4f(shaderMaterialInstanced.getUniformLocation(tre::shader::uniColor), 1.f, 0.6f, 0.6f, 1.f);
    glUniform2f(shaderMaterialInstanced.getUniformLocation(tre::shader::uniMat), 0.f, 0.5f);
    shaderMaterialInstanced.setUniformMatrix(mPV, glm::mat4(1.f), myView3D.m_matView);
    meshes.drawInstanced(0, 0, kPoolSize);

    tre::IsOpenGLok("opaque render pass");
  }

  // UI-render pass -------------

  {
    glDisable(GL_DEPTH_TEST);

    if (showMaps)
    {
      glDisable(GL_BLEND);
      sunShadow_debug.draw(myWindow.m_matProjection2D, nearMap);
    }

    // HUD
    {
      glEnable(GL_BLEND);
      char txtFPS[128];
      const unsigned reserved = simOnGPU ? gpuRigidBodies.reservedCount() : cpuRigidBodies.reservedCount();
      std::snprintf(txtFPS, 127, "%03d fps - %u/%u slots used - simulated on %s",
                    int(1.f/myTimings.frametime), reserved, kPoolSize, simOnGPU ? "GPU" : "CPU");
      tre::textgenerator::s_textInfo tInfo;
      tInfo.setupBasic(&worldHUDFont, txtFPS, glm::vec2(0.f, - 0.08f * 0));
      tInfo.setupSize(0.06f);
      tInfo.m_fontPixelSize = 14;
      worldHUDModel.resizePart(0, tre::textgenerator::geometry_VertexCount(tInfo.m_text));
      tre::textgenerator::generate(tInfo, &worldHUDModel, 0, 0, nullptr);

      worldHUDModel.colorizePart(2, showMaps ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(1.f));
      worldHUDModel.colorizePart(3, nearMap ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(1.f));
      worldHUDModel.colorizePart(4, simOnGPU ? glm::vec4(0.f, 1.f, 0.f, 1.f) : glm::vec4(1.f));

      worldHUDModel.updateIntoGPU();

      glm::mat3 mViewModel_hud = glm::mat3(1.f);
      mViewModel_hud[0][0] = 1.f;
      mViewModel_hud[1][1] = 1.f;
      mViewModel_hud[2][0] = -0.98f / myWindow.m_matProjection2D[0][0];
      mViewModel_hud[2][1] =  0.98f;

      glUseProgram(shaderText2D.m_drawProgram);
      shaderText2D.setUniformMatrix(myWindow.m_matProjection2D * mViewModel_hud);

      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D,worldHUDFont.get_texture().m_handle);
      glUniform1i(shaderText2D.getUniformLocation(tre::shader::TexDiffuse),4);

      worldHUDModel.drawcallAll();
    }

    glEnable(GL_BLEND);

    tre::IsOpenGLok("UI render pass - draw UI");
  }

  // end render pass --------------

  myTimings.endFrame_beforeGPUPresent();

  SDL_GL_SwapWindow( myWindow.m_window );
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

  gpuRigidBodies.clear();

  sunLight_ShadowMapFar.clear();
  sunLight_ShadowMapNear.clear();

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
