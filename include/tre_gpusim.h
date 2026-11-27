#pragma once

#include "tre_utils.h"
#include "tre_shader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tre {

/**
 * @brief The gpuSim class
 * GPU simulation of a pool of objects with a life-time.
 *
 * Storage: the pool is a 2^N x 2^N texture, one texel per object, and double-buffered (ping-pong between 2 FBOs)
 * Each object has "float" fields, packed 4 by 4 into GS_NFIELDS4 RGBA32F textures (MRT).
 *
 * Life-time: Every object is spawned with a life-time (in seconds); they can be "killed" before, but their slot will be still used until the given life-time.
 * Because of this rule, the CPU knows which slots are free, without any readback.
 *
 * GLSL conventions:
 * - #define GS_NFIELDS (nbr of floats), GS_NFIELDS4, GS_NINSTANCE, GS_LOG2 (N), GS_SIZE (2^N), GS_MASK (2^N - 1)
 * - uniform float gsDt;   // dt of the current step
 * - uint  gsHash(uint x);           // integer hash
 * - float gsRand01(inout uint s);   // random in [0,1), updates the seed
 * - uniform sampler2D gsField0 .. gsField<GS_NFIELDS4-1> (readable for interactions between objects ...)
 * - uniform vec4 gsSpawnParam; // (baseCount, user0-1-2); with GPU spawn ONLY
 */
class gpuSim
{
public:

  struct s_config
  {
    unsigned      poolSizeLog2 = 7;       ///< N: the pool is a 2^N x 2^N texture, it holds 2^(2N) objects. N in [1, 12].
    unsigned      fieldCount = 1;         ///< Nbr of float fields per object, in [1, maxFieldCount()]
    unsigned      invLife = 0;            ///< Index of the "1/timeLife" field, in [0, fieldCount-1]
    unsigned      instanceVec4Count = 1;  ///< Nbr of vec4 written per object into the instance buffer, in [1, maxInstanceVec4Count()].
    unsigned      firstTextureUnit = 8;   ///< First texture unit used
    std::string   glslCommon;             ///< Optional user declarations, functions, uniforms, available in every kernel.
    const char *  kernelEvolve = nullptr; ///< Evolve kernel, content of "void gsEvolve(inout vec4 field[GS_NFIELDS4], in uint index)"
    const char *  kernelSpawn = nullptr;  ///< [optionnal] Spawn kernel, content of "void gsSpawn(out vec4 field[GS_NFIELDS4], in uint index)"
    const char *  kernelDraw = nullptr;   ///< Fill draw-buffer kernel, content of "void gsInstance(in vec4 field[GS_NFIELDS4], in uint index, out vec4 inst[GS_NINSTANCE])"
    std::string   name = "gpuSim";        ///< Used in the logs.
  };

  gpuSim() {}
  gpuSim(const gpuSim &) = delete;
  ~gpuSim() { TRE_ASSERT(!m_isLoaded); }

  gpuSim & operator =(const gpuSim &) = delete;

  /// @name Life-cycle
  /// @{
public:
  bool load(const s_config &config); ///< Create the GPU resources and compile the kernels. All objects are dead.
  void clear();                      ///< Free the GPU resources.
  void reset();                      ///< Kill all objects, and reset the time.
  bool isLoaded() const { return m_isLoaded; }

  static unsigned maxFieldCount();        ///< Limit of the fields per object (needs an OpenGL context).
  static unsigned maxInstanceVec4Count(); ///< Limit of the vec4 per object in the instance buffer (needs an OpenGL context).
  /// @}

  /// @name Spawn
  /// @{
public:

  unsigned spawn(float dt, const float *fields, unsigned count); ///< "fields" holds the CPU array of struct object. Returns the number of accepted objects
  unsigned spawn(float dt, unsigned count, float lifeTimeMax, const glm::vec3 userParam = glm::vec3(0.f)); ///< call the spawn kernel with accepted count. Returns the number of accepted objects

  unsigned freeCount() const { return unsigned(m_freeSlots.size()); }  ///< Nbr of slots available for a spawn now.
  unsigned reservedCount() const { return poolSize() - freeCount(); }  ///< Nbr of reserved slots. The alive objects are a subset of them.
  float    timeLeft(unsigned slot) const { return m_timeLeft[slot]; }  ///< Time before the slot is released. 0 for a free slot.
  /// @}

  /// @name Simulation
  /// @{
public:

  void simulate(float dt);

  void exportInstances(GLuint instanceBuffer, GLintptr byteOffset = 0) const; ///< Write to the instance buffer
  /// @}

  /// @name Accessors
  /// @{
public:
  unsigned      poolSize() const { return 1u << (2 * m_config.poolSizeLog2); }
  unsigned      textureSize() const { return 1u << m_config.poolSizeLog2; }
  const s_config &config() const { return m_config; }

  GLuint programSpawn() const { return m_shaderSpawn.m_drawProgram; }
  GLuint programEvolve() const { return m_shaderEvolve.m_drawProgram; }
  GLuint programInstance() const { return m_shaderDraw.m_drawProgram; }

  GLuint fieldTexture(unsigned tex) const { TRE_ASSERT(tex < m_texCount); return m_texState[m_current][tex]; } ///< Current state, texture in [0, GS_NFIELDS4-1].
  /// @}

private:
  static constexpr unsigned kMaxTextures     = 16;
  static constexpr unsigned kMaxInstanceVec4 = 16;
  static constexpr unsigned kSpawnBatch      = 128; ///< Slots per draw-call of the GPU spawn (uniform array of 32 ivec4)

  bool     _createTargets();
  bool     _createPrograms();
  void     _bindStateTextures(unsigned pingpong) const;
  void     _setPassStates() const;

  s_config m_config;
  bool     m_isLoaded = false;
  unsigned m_texCount = 0; ///< GS_NFIELDS4

  GLuint   m_texState[2][kMaxTextures] = {};
  GLuint   m_fboState[2] = {};
  unsigned m_current = 0;
  GLuint   m_texDraw = 0; ///< (2^N * instanceVec4Count) x 2^N
  GLuint   m_fboDraw = 0;
  GLuint   m_vaoEmpty = 0;

  shader m_shaderSpawn;
  shader m_shaderEvolve;
  shader m_shaderDraw;

  GLint m_shaderSpawn_dt = -1;
  GLint m_shaderSpawn_spawnSlots = -1;
  GLint m_shaderSpawn_spawnParam = -1;
  GLint m_shaderEvolve_dt = -1;

  // CPU-side allocator
  std::vector<float>      m_timeLeft;   ///< per slot. 0 means free.
  std::vector<uint32_t>   m_freeSlots;  ///< stack of the free slots
  std::vector<uint32_t>   m_spawnSlots; ///< scratch: slots of the current spawn() call
  std::vector<glm::vec4>  m_spawnTexels;///< scratch: texels uploaded by the CPU spawn
  uint32_t                m_spawnCounter = 0;
};

} // namespace
