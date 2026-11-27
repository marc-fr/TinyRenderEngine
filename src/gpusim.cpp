#include "tre_gpusim.h"

#include <string>

namespace tre {

// ============================================================================
// GLSL sources

static const char *kGLSL_Library =
  "uniform float gsDt;\n"
  "uint gsHash(uint x)\n"
  "{\n"
  "  x ^= x >> 16; x *= 0x7feb352du;\n"
  "  x ^= x >> 15; x *= 0x846ca68bu;\n"
  "  x ^= x >> 16;\n"
  "  return x;\n"
  "}\n"
  "float gsRand01(inout uint s) { s = gsHash(s); return float(s >> 8) * (1.0 / 16777216.0); }\n"
  "uint  gsSlotIndex(ivec2 ij)  { return uint(ij.x + (ij.y << GS_LOG2)); }\n"
  "ivec2 gsSlotTexel(uint slot) { int s = int(slot); return ivec2(s & GS_MASK, s >> GS_LOG2); }\n";

static const char *kGLSL_FullScreenVertex =
  "void main()\n"
  "{\n"
  "  // full-screen triangle: (-1,-1), (3,-1), (-1,3)\n"
  "  vec2 p = vec2(float((gl_VertexID & 1) << 2) - 1.0, float((gl_VertexID & 2) << 1) - 1.0);\n"
  "  gl_Position = vec4(p, 0.0, 1.0);\n"
  "}\n";

// ----------------------------------------------------------------------------

static std::string _glslDefines(const gpuSim::s_config &c)
{
  const unsigned size = 1u << c.poolSizeLog2;
  std::string s;
  s += "#define GS_NFIELDS " + std::to_string(c.fieldCount) + "\n";
  s += "#define GS_NFIELDS4 " + std::to_string((c.fieldCount + 3) / 4) + "\n";
  s += "#define GS_NINSTANCE " + std::to_string(c.instanceVec4Count) + "\n";
  s += "#define GS_LOG2 " + std::to_string(c.poolSizeLog2) + "\n";
  s += "#define GS_SIZE " + std::to_string(size) + "\n";
  s += "#define GS_MASK " + std::to_string(size - 1) + "\n";
  return s;
}

static std::string _glslCommon(const gpuSim::s_config &c)
{
  std::string s = kGLSL_Library;
  s += "// user common\n";
  s += c.glslCommon;
  s += "\n";
  return s;
}

static std::string _glslFieldSamplers(unsigned texCount)
{
  std::string s;
  for (unsigned t = 0; t < texCount; ++t) s += "uniform sampler2D gsField" + std::to_string(t) + ";\n";
  return s;
}

static std::string _glslFetchState(unsigned texCount, const char *texel)
{
  std::string s = "  vec4 field[GS_NFIELDS4];\n";
  for (unsigned t = 0; t < texCount; ++t)
    s += "  field[" + std::to_string(t) + "] = texelFetch(gsField" + std::to_string(t) + ", " + texel + ", 0);\n";
  return s;
}

static std::string _glslStateOutputs(unsigned texCount)
{
  std::string s;
  for (unsigned t = 0; t < texCount; ++t)
    s += "layout(location = " + std::to_string(t) + ") out vec4 gsOut" + std::to_string(t) + ";\n";
  return s;
}

static std::string _glslWriteState(unsigned texCount)
{
  std::string s;
  for (unsigned t = 0; t < texCount; ++t)
    s += "  gsOut" + std::to_string(t) + " = field[" + std::to_string(t) + "];\n";
  return s;
}

// ============================================================================

unsigned gpuSim::maxFieldCount()
{
  GLint maxDrawBuffers = 0, maxAttachments = 0;
  glGetIntegerv(GL_MAX_DRAW_BUFFERS, &maxDrawBuffers);
  glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, &maxAttachments);
  const int n = std::min(std::min(maxDrawBuffers, maxAttachments), int(kMaxTextures));
  return n > 0 ? 4u * unsigned(n) : 0u;
}

// ----------------------------------------------------------------------------

unsigned gpuSim::maxInstanceVec4Count()
{
  // The draw texture is (2^N * instanceVec4Count) texels wide. load() checks it against the actual pool size.
  GLint maxTexSize = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
  return std::min(kMaxInstanceVec4, unsigned(std::max(maxTexSize / 2, 0)));
}

// ----------------------------------------------------------------------------

bool gpuSim::load(const s_config &config)
{
  TRE_ASSERT(!m_isLoaded);
  m_config = config;
  const s_config &c = m_config;

  // - checks

  GLint maxTexSize = 0, maxUnits = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
  glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS, &maxUnits);

  const unsigned texCount = (c.fieldCount + 3) / 4;

  const char *err = nullptr;
  if      (c.poolSizeLog2 < 1 || c.poolSizeLog2 > 12)                                 err = "poolSizeLog2 must be in [1, 12]";
  else if (int(textureSize()) > maxTexSize)                                           err = "the pool texture exceeds GL_MAX_TEXTURE_SIZE";
  else if (c.fieldCount < 1 || c.fieldCount > maxFieldCount())                        err = "fieldCount exceeds the draw-buffers limit (4 * GL_MAX_DRAW_BUFFERS)";
  else if (c.invLife >= c.fieldCount)                                                 err = "invLife must be in [0, fieldCount-1]";
  else if (c.instanceVec4Count < 1 || c.instanceVec4Count > maxInstanceVec4Count())   err = "instanceVec4Count must be in [1, maxInstanceVec4Count()-1]";
  else if (int64_t(textureSize()) * c.instanceVec4Count > maxTexSize)                 err = "the draw texture (2^N * instanceVec4Count) exceeds GL_MAX_TEXTURE_SIZE";
  else if (int(c.firstTextureUnit + texCount) > maxUnits)                             err = "firstTextureUnit is too high (GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS)";
  else if (c.kernelEvolve == nullptr)                                                 err = "kernelEvolve is missing";
  else if (c.kernelDraw == nullptr)                                                   err = "kernelDraw is missing";
  if (err != nullptr)
  {
    TRE_LOG("gpuSim '" << c.name << "': invalid configuration: " << err);
    return false;
  }

  m_texCount = texCount;

  // - resources

  glGenVertexArrays(1, &m_vaoEmpty);

  if (!_createTargets() || !_createPrograms())
  {
    m_isLoaded = true;
    clear();
    return false;
  }

  m_timeLeft.assign(poolSize(), 0.f);
  m_spawnSlots.reserve(poolSize());

  m_isLoaded = true;
  reset();

  return IsOpenGLok("gpuSim::load");
}

// ----------------------------------------------------------------------------

bool gpuSim::_createTargets()
{
  const GLsizei size = GLsizei(textureSize());

  auto createTexture = [](GLuint handle, GLsizei w, GLsizei h)
  {
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  };

  auto checkFBO = [this](const char *what) -> bool
  {
    const GLenum fboStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStatus == GL_FRAMEBUFFER_COMPLETE) return true;
    TRE_LOG("gpuSim '" << m_config.name << "': the " << what << " FBO is incomplete (status " << fboStatus << "). Float color-buffers may not be supported (EXT_color_buffer_float).");
    return false;
  };

  GLenum drawBufs[kMaxTextures];
  for (unsigned i = 0; i < kMaxTextures; ++i) drawBufs[i] = GL_COLOR_ATTACHMENT0 + i;

  bool status = true;

  // state (ping-pong)

  glGenFramebuffers(2, m_fboState);
  for (unsigned ip = 0; ip < 2; ++ip)
  {
    glGenTextures(GLsizei(m_texCount), m_texState[ip]);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboState[ip]);
    for (unsigned it = 0; it < m_texCount; ++it)
    {
      createTexture(m_texState[ip][it], size, size);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + it, GL_TEXTURE_2D, m_texState[ip][it], 0);
    }
    glDrawBuffers(GLsizei(m_texCount), drawBufs);
    status &= checkFBO("state");
  }

  // draw (instance data)

  glGenFramebuffers(1, &m_fboDraw);
  glGenTextures(1, &m_texDraw);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fboDraw);
  createTexture(m_texDraw, size * GLsizei(m_config.instanceVec4Count), size);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texDraw, 0);
  glDrawBuffers(1, drawBufs);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  status &= checkFBO("draw");

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindTexture(GL_TEXTURE_2D, 0);

  status &= IsOpenGLok("gpuSim::createTargets");
  return status;
}

// ----------------------------------------------------------------------------

bool gpuSim::_createPrograms()
{
  const s_config    &c       = m_config;
  const std::string defines = _glslDefines(c);
  const std::string common  = _glslCommon(c);

  shader::s_layout layout(shader::PRGM_3D, 0);
  layout.hasUNI_MPVM   = false;
  layout.hasOUT_Color0 = false;
  layout.hasOUT_Color1 = false;
  layout.hasOUT_Depth  = true; // hack: it does not generate any declaration, but it is needed by the layout checks.

  // - GPU-spawn:

  if (c.kernelSpawn != nullptr)
  {
    const std::string batch4 = std::to_string(kSpawnBatch / 4);

    const std::string srcV = defines +
      "uniform ivec4 gsSpawnSlots[" + batch4 + "];\n"
      "uniform vec4 gsSpawnParam; // (baseCount, user0-1-2)\n"
      "flat out uint gsV_index;\n"
      "void main()\n"
      "{\n"
      "  int  slot = gsSpawnSlots[gl_VertexID >> 2][gl_VertexID & 3];\n"
      "  vec2 p = (vec2(float(slot & GS_MASK), float(slot >> GS_LOG2)) + 0.5) * (2.0 / float(GS_SIZE)) - 1.0;\n"
      "  gl_Position = vec4(p, 0.0, 1.0);\n"
      "  gsV_index = uint(gl_VertexID);\n"
      "}\n";

    const std::string srcF = defines + common + _glslStateOutputs(m_texCount) +
      "uniform vec4 gsSpawnParam; // (baseCount, user0-1-2)\n"
      "flat in uint gsV_index;\n"
      "// ---- user spawn kernel\n"
      "void gsSpawn(out vec4 field[GS_NFIELDS4], in uint index)\n"
      "{\n" + c.kernelSpawn + "\n}\n"
      "void main()\n"
      "{\n"
      "  vec4 field[GS_NFIELDS4];\n"
      "  gsSpawn(field, gsV_index);\n" +
      _glslWriteState(m_texCount) +
      "}\n";

    if (!m_shaderSpawn.loadCustomShaderVF(layout, srcV.c_str(), srcF.c_str(), (c.name + "_spawn").c_str())) return false;

    m_shaderSpawn_dt         = m_shaderSpawn.getUniformLocation("gsDt");
    m_shaderSpawn_spawnSlots = m_shaderSpawn.getUniformLocation("gsSpawnSlots");
    m_shaderSpawn_spawnParam = m_shaderSpawn.getUniformLocation("gsSpawnParam");
  }

  // - evolve:

  {
    const std::string srcV = defines + kGLSL_FullScreenVertex;
    const std::string srcF = defines + common + _glslFieldSamplers(m_texCount) + _glslStateOutputs(m_texCount) +
      "// ---- user evolve kernel\n"
      "void gsEvolve(inout vec4 field[GS_NFIELDS4], in uint index)\n"
      "{\n" + c.kernelEvolve + "\n}\n"
      "void main()\n"
      "{\n"
      "  ivec2 ij = ivec2(gl_FragCoord.xy);\n" +
      _glslFetchState(m_texCount, "ij") +
      "  gsEvolve(field, gsSlotIndex(ij));\n" +
      _glslWriteState(m_texCount) +
      "}\n";

    if (!m_shaderEvolve.loadCustomShaderVF(layout, srcV.c_str(), srcF.c_str(), (c.name + "_evolve").c_str())) return false;

    m_shaderEvolve_dt = m_shaderEvolve.getUniformLocation("gsDt");
  }

  // - fill draw-buffer:

  {
    const std::string srcV = defines + kGLSL_FullScreenVertex;
    const std::string srcF = defines + common + _glslFieldSamplers(m_texCount) +
      "layout(location = 0) out vec4 gsOut;\n"
      "// ---- user draw kernel\n"
      "void gsInstance(in vec4 field[GS_NFIELDS4], in uint index, out vec4 inst[GS_NINSTANCE])\n"
      "{\n" + c.kernelDraw + "\n}\n"
      "void main()\n"
      "{\n"
      "  ivec2 xy = ivec2(gl_FragCoord.xy);\n"
      "  int   ic = xy.x % GS_NINSTANCE;\n"
      "  ivec2 ij = ivec2(xy.x / GS_NINSTANCE, xy.y);\n" +
      _glslFetchState(m_texCount, "ij") +
      "  vec4 inst[GS_NINSTANCE];\n"
      "  gsInstance(field, gsSlotIndex(ij), inst);\n"
      "  gsOut = inst[ic];\n"
      "}\n";

    if (!m_shaderDraw.loadCustomShaderVF(layout, srcV.c_str(), srcF.c_str(), (c.name + "_draw").c_str())) return false;
  }

  // - static bindings (texture units, UBO)

  for (shader *sh : { &m_shaderSpawn, &m_shaderEvolve, &m_shaderDraw })
  {
    const GLuint prg = sh->m_drawProgram;
    if (prg == 0) continue; // no spawn program with SPAWN_FROM_CPU
    glUseProgram(prg);
    for (unsigned t = 0; t < m_texCount; ++t)
    {
      const GLint loc = sh->getUniformLocation(("gsField" + std::to_string(t)).c_str());
      if (loc >= 0) glUniform1i(loc, GLint(c.firstTextureUnit + t));
    }
  }
  glUseProgram(0);

  return IsOpenGLok("gpuSim::createPrograms");
}

// ----------------------------------------------------------------------------

void gpuSim::clear()
{
  if (!m_isLoaded) return;

  m_shaderSpawn.clearShader();
  m_shaderEvolve.clearShader();
  m_shaderDraw.clearShader();

  for (unsigned ip = 0; ip < 2; ++ip)
  {
    if (m_texState[ip][0] != 0) glDeleteTextures(GLsizei(m_texCount), m_texState[ip]);
    for (GLuint &t : m_texState[ip]) t = 0;
  }
  if (m_fboState[0] != 0) glDeleteFramebuffers(2, m_fboState);
  m_fboState[0] = m_fboState[1] = 0;

  if (m_texDraw != 0) glDeleteTextures(1, &m_texDraw);
  m_texDraw = 0;
  if (m_fboDraw != 0) glDeleteFramebuffers(1, &m_fboDraw);
  m_fboDraw = 0;

  if (m_vaoEmpty != 0) glDeleteVertexArrays(1, &m_vaoEmpty);
  m_vaoEmpty = 0;

  m_timeLeft.clear();
  m_freeSlots.clear();
  m_spawnSlots.clear();
  m_spawnTexels.clear();

  m_isLoaded = false;

  IsOpenGLok("gpuSim::clear");
}

// ----------------------------------------------------------------------------

void gpuSim::reset()
{
  if (!m_isLoaded) return;

  // GPU: all fields to one (lifeRatio = 1, dead)
  for (unsigned ip = 0; ip < 2; ++ip)
  {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboState[ip]);
    for (unsigned it = 0; it < m_texCount; ++it)
    {
      GLfloat value[4] = { 1.f, 1.f, 1.f, 1.f };
      glClearBufferfv(GL_COLOR, GLint(it), value);
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  m_current = 0;

  // CPU: all slots are free
  const unsigned n = poolSize();
  std::fill(m_timeLeft.begin(), m_timeLeft.end(), 0.f);
  m_freeSlots.resize(n);
  for (unsigned i = 0; i < n; ++i) m_freeSlots[i] = n - 1 - i;

  IsOpenGLok("gpuSim::reset");
}

// ----------------------------------------------------------------------------

unsigned gpuSim::spawn(float dt, const float *fields, unsigned count)
{
  if (fields == nullptr) return 0;
  const unsigned F = m_config.fieldCount;

  m_spawnSlots.clear();
  count = std::min(count, unsigned(m_freeSlots.size()));
  for (unsigned k = 0; k < count; ++k)
  {
    const std::uint32_t slot = m_freeSlots.back();
    m_freeSlots.pop_back();
    m_timeLeft[slot] = 1.f / fields[k * F + m_config.invLife];
    m_spawnSlots.push_back(slot);
  }
  if (count == 0) return 0;

  const unsigned mask = textureSize() - 1;

  glActiveTexture(GL_TEXTURE0 + m_config.firstTextureUnit);

  // Upload directly into the current state
  for (unsigned k0 = 0; k0 < count; )
  {
    const std::uint32_t slot0 = m_spawnSlots[k0];
    unsigned            len   = 1;
    while (k0 + len < count && m_spawnSlots[k0 + len] == slot0 + len && ((slot0 + len) & mask) != 0) ++len;

    const GLint x0 = GLint(slot0 & mask);
    const GLint y  = GLint(slot0 >> m_config.poolSizeLog2);

    m_spawnTexels.resize(len);
    for (unsigned t = 0; t < m_texCount; ++t)
    {
      for (unsigned j = 0; j < len; ++j)
      {
        const float *src = fields + std::size_t(k0 + j) * F;
        glm::vec4    v(0.f);
        for (unsigned ic = 0; ic < 4; ++ic)
        {
          const unsigned f = 4 * t + ic;
          if (f < F) v[ic] = src[f];
        }
        m_spawnTexels[j] = v;
      }
      glBindTexture(GL_TEXTURE_2D, m_texState[m_current][t]);
      glTexSubImage2D(GL_TEXTURE_2D, 0, x0, y, GLsizei(len), 1, GL_RGBA, GL_FLOAT, m_spawnTexels.data());
    }

    k0 += len;
    m_spawnCounter += len;
  }

  IsOpenGLok("gpuSim::spawn (CPU)");
  return count;
}

// ----------------------------------------------------------------------------

unsigned gpuSim::spawn(float dt, unsigned count, float lifeTimeMax, const glm::vec3 userParam)
{
  if (m_shaderSpawn.m_drawProgram == 0) return 0;

  m_spawnSlots.clear();
  count = std::min(count, unsigned(m_freeSlots.size()));
  for (unsigned k = 0; k < count; ++k)
  {
    const std::uint32_t slot = m_freeSlots.back();
    m_freeSlots.pop_back();
    m_timeLeft[slot] = 1.f / lifeTimeMax;
    m_spawnSlots.push_back(slot);
  }
  if (count == 0) return 0;

  // scatter: one point per object, written into the current state

  glBindVertexArray(m_vaoEmpty);
  _setPassStates();
  glBindFramebuffer(GL_FRAMEBUFFER, m_fboState[m_current]);
  glViewport(0, 0, GLsizei(textureSize()), GLsizei(textureSize()));

  glUseProgram(m_shaderSpawn.m_drawProgram);
  glUniform1f(m_shaderSpawn_dt, dt);

  GLint slots[kSpawnBatch];
  for (unsigned base = 0; base < count; base += kSpawnBatch)
  {
    const unsigned n = std::min(kSpawnBatch, count - base);
    for (unsigned k = 0; k < kSpawnBatch; ++k) slots[k] = (k < n) ? GLint(m_spawnSlots[base + k]) : 0;
    const glm::vec4 param = glm::vec4(m_spawnCounter + 0.5f, userParam);
    glUniform4fv(m_shaderSpawn_spawnParam, 1, glm::value_ptr(param));
    glDrawArrays(GL_POINTS, 0, GLsizei(n));
    m_spawnCounter += n;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindVertexArray(0);

  IsOpenGLok("gpuSim::spawn (GPU)");
  return count;
}

// ----------------------------------------------------------------------------

void gpuSim::_bindStateTextures(unsigned pingpong) const
{
  for (unsigned it = 0; it < m_texCount; ++it)
  {
    glActiveTexture(GL_TEXTURE0 + m_config.firstTextureUnit + it);
    glBindTexture(GL_TEXTURE_2D, m_texState[pingpong][it]);
  }
}

// ----------------------------------------------------------------------------

void gpuSim::_setPassStates() const
{
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
}

// ----------------------------------------------------------------------------

void gpuSim::simulate(float dt)
{
  if (!m_isLoaded) return;

  // GPU: evolve

  const unsigned next = 1 - m_current;

  glBindVertexArray(m_vaoEmpty);
  _setPassStates();
  glBindFramebuffer(GL_FRAMEBUFFER, m_fboState[next]);
  glViewport(0, 0, GLsizei(textureSize()), GLsizei(textureSize()));

  _bindStateTextures(m_current);

  glUseProgram(m_shaderEvolve.m_drawProgram);
  glUniform1f(m_shaderEvolve_dt, dt);

  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindVertexArray(0);

  m_current = next;

  // CPU: life-time, release the expired slots

  const unsigned n = poolSize();
  for (unsigned slot = 0; slot < n; ++slot)
  {
    float &tl = m_timeLeft[slot];
    if (tl <= 0.f) continue;
    tl -= dt;
    if (tl <= 0.f)
    {
      tl = 0.f;
      m_freeSlots.push_back(slot);
    }
  }

  IsOpenGLok("gpuSim::simulate");
}

// ----------------------------------------------------------------------------

void gpuSim::exportInstances(GLuint instanceBuffer, GLintptr byteOffset) const
{
  if (!m_isLoaded) return;
  TRE_ASSERT(instanceBuffer != 0);
  TRE_ASSERT(byteOffset % 4 == 0);

  const GLsizei S = GLsizei(textureSize());
  const GLsizei W = S * GLsizei(m_config.instanceVec4Count);

  // render the instance data into the draw texture

  glBindVertexArray(m_vaoEmpty);
  _setPassStates();
  glBindFramebuffer(GL_FRAMEBUFFER, m_fboDraw);
  glViewport(0, 0, W, S);

  _bindStateTextures(m_current);

  glUseProgram(m_shaderDraw.m_drawProgram);

  glDrawArrays(GL_TRIANGLES, 0, 3);

  // GPU-GPU copy

  glBindBuffer(GL_PIXEL_PACK_BUFFER, instanceBuffer);
#ifdef TRE_DEBUG
  {
    const GLsizeiptr byteSize = GLsizeiptr(poolSize()) * GLsizeiptr(m_config.instanceVec4Count) * 16;
    GLint bufferSize = 0;
    glGetBufferParameteriv(GL_PIXEL_PACK_BUFFER, GL_BUFFER_SIZE, &bufferSize);
    TRE_ASSERT(GLsizeiptr(bufferSize) >= byteOffset + byteSize);
  }
#endif
  glReadPixels(0, 0, W, S, GL_RGBA, GL_FLOAT, reinterpret_cast<void*>(byteOffset));
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glBindVertexArray(0);

  IsOpenGLok("gpuSim::exportInstances");
}

// ----------------------------------------------------------------------------

} // namespace
