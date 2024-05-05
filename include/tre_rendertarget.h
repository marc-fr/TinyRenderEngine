#ifndef RENDERTARGET_H
#define RENDERTARGET_H

#include "tre_utils.h"
#include "tre_shader.h"
#include "tre_model.h"

namespace tre {

// ============================================================================

/**
 * @brief This class handles the creation and usage of FBO (Frame Buffer Object).
 * It supports multisampled and HDR modes.
 */
class renderTarget
{
public:
  static const int RT_COLOR           = 0x1000;
  static const int RT_DEPTH           = 0x2000;
  static const int RT_COLOR_SAMPLABLE = 0x0100;
  static const int RT_DEPTH_SAMPLABLE = 0x0200;
  static const int RT_COLOR_HDR       = 0x0010;
  static const int RT_MULTISAMPLED    = 0x0003;

  static const int RT_COLOR_AND_DEPTH = RT_COLOR | RT_DEPTH;
  static const int RT_SAMPLABLE       = RT_COLOR_SAMPLABLE | RT_DEPTH_SAMPLABLE;

  renderTarget(const int flags) : m_flags(flags) {}
  ~renderTarget() { TRE_ASSERT(m_drawFBO == 0); }

  bool load(const int pwidth, const int pheigth);
  bool resize(const int pwidth, const int pheigth);
  void clear();

  void bindForWritting() const;
  void bindForReading() const;

  void resolve(const int outwidth, const int outheigth) const;
  void resolve(renderTarget & targetFBO) const;

  int w()               const { return m_w; }
  int h()               const { return m_h; }
  bool hasColor()       const { return (m_flags & RT_COLOR) != 0; }
  bool hasDepth()       const { return (m_flags & RT_DEPTH) != 0; }
  bool isHDR()          const { return (m_flags & RT_COLOR_HDR) != 0; }
  bool isMultisampled() const { return (m_flags & RT_MULTISAMPLED) != 0; }
  GLuint colorHandle()  const { TRE_ASSERT(m_colorhandle != 0); return m_colorhandle; }
  GLuint depthHandle()  const { TRE_ASSERT(m_depthhandle != 0); return m_depthhandle; }

protected:
  int     m_flags;
  GLsizei m_w = 0, m_h = 0;
  GLuint  m_drawFBO = 0;
  GLuint  m_colorhandle = 0, m_depthhandle = 0;
  GLuint  m_colorbuffer = 0, m_depthbuffer = 0;

  bool hasValidFlags() const;
};

// ============================================================================

/**
 * @brief This class extends the class renderTarget for shadow-map purpose.
 *
 */
class renderTarget_ShadowMap : public renderTarget
{
public:
  renderTarget_ShadowMap() : renderTarget(RT_DEPTH | RT_DEPTH_SAMPLABLE), m_sceneBox(1.f,1.f,1.f), m_direction(0.f,-1.f,0.f) {}
  ~renderTarget_ShadowMap() {}

  void setSceneBox(const s_boundbox &newbox) { m_sceneBox = newbox; }
  void computeUBO_forMap(const shader::s_UBOdata_sunLight &uboLight, shader::s_UBOdata_sunShadow & uboShadow, uint shadowIndex); ///< Get the light direction from the UBO, compute the Proj-view matrix, fill data in the UBO (matrices, invDimension).

  const glm::mat4 &mProj() const { return m_mProj; }
  const glm::mat4 &mView() const { return m_mView; }

protected:
  s_boundbox m_sceneBox;
  glm::mat4  m_mProj;
  glm::mat4  m_mView;
  glm::vec3  m_direction;
};

// ============================================================================

/**
 * @brief This class provides render-targets and helpers for cube shadow-maps
 *
 */
class renderTarget_ShadowCubeMap
{
public:
  renderTarget_ShadowCubeMap() { setRenderingLimits(0.1f, 1000.f); computeMViews(); }
  ~renderTarget_ShadowCubeMap() { TRE_ASSERT(m_drawFBO == 0); }

  bool load(const int texSize);
  void clear();

  void bindForWritting(GLenum cubeFace) const;
  void bindForReading(GLenum cubeFace) const;

  int w() const { return m_w; }
  int h() const { return m_h; }
  GLuint depthHandle() const { TRE_ASSERT(m_depthhandle != 0); return m_depthhandle; }

  const glm::mat4 &mProj() const { return m_mProj; }
  const glm::mat4 &mView(GLenum cubeFace) const { return m_mViewes[cubeFace - GL_TEXTURE_CUBE_MAP_POSITIVE_X]; }

  void setRenderingLimits(float near, float far);

  void computeUBO_forMap(shader::s_UBOdata_ptstLight & uboLight, uint lightIndex, shader::s_UBOdata_ptsShadow & uboShadow); ///< Get the light center from the UBO, compute the Proj-view matrices, fill data in the UBO (remapZ, invDimension).

protected:

  void computeMViews();

  GLsizei       m_w,m_h;
  GLuint        m_drawFBO = 0;

  GLuint        m_depthhandle = 0; ///< cube-map texture.
  glm::mat4     m_mViewes[6];

  glm::mat4     m_mProj;

  glm::vec3     m_cubeCenter = glm::vec3(0.f);
  float         m_near, m_far;
};

// ============================================================================

/**
 * @brief This class provides render-targets for a deferred-rendering.
 *
 */
class renderTarget_GBuffer
{
public:
  enum GBufferIndex { GBUFFERINDEX_DIFFUSE,
                      GBUFFERINDEX_NORMAL,
                      GBUFFERINDEX_MATERIAL,
                      GBUFFER_NUM };

  renderTarget_GBuffer(const bool hdr) : m_isHDR(hdr) {}
  ~renderTarget_GBuffer() {}

  bool load(const int pwidth, const int pheigth);
  bool resize(const int pwidth, const int pheigth);
  void clear();

  void bindForWritting() const;
  void bindForReading() const;

  int w() const { return m_w; }
  int h() const { return m_h; }
  bool isHDR() const { return m_isHDR; }
  GLuint colorHandle(GBufferIndex index) const { return m_colorhandles[index]; }
  GLuint depthHandle() const { return m_depthhandle; }

protected:

  bool    m_isHDR = false;
  GLsizei m_w = 0, m_h = 0;
  GLuint  m_drawFBO = 0;
  GLuint  m_colorhandles[GBUFFER_NUM];
  GLuint  m_depthhandle;
};

// ============================================================================

/**
 * @brief This class implements a blur post-FX, with a bright pass and blur passes.
 * The bright-pass function is f(x) = x*x / (0.25 + x*x)
 * if the output-target is LDR, the color is scaled in [0,1] range. So x = max(r-offset,b-offset,g-offset,0)
 * if the output-target is HDR, the color is scaled. Here, x = gray(max(rgb-offset,000))
 */
class postFX_Blur
{
public:
  postFX_Blur(const uint NbrPass, const bool outHDR = false) :
      m_quadFullScreen(),
      m_renderDownsample(NbrPass, {renderTarget::RT_COLOR | renderTarget::RT_COLOR_SAMPLABLE | (outHDR ? renderTarget::RT_COLOR_HDR : 0)}),
      m_renderInvScreenSize(NbrPass, glm::vec2(0.f)),
      m_paramBrightPass(glm::vec2(1.f, 1.f)),
      m_Npass(NbrPass), m_isOutHDR(outHDR)
    { TRE_ASSERT(m_Npass>0); }
  ~postFX_Blur() {}

  bool load(const int pwidth, const int pheigth);
  bool resize(const int pwidth, const int pheigth);
  void clear();

  void processBlur(GLuint inputTextureHandle);
  GLuint get_blurTextureUnit(const std::size_t pass = std::size_t(-1) /* last */) const { return m_renderDownsample[std::min(pass, m_renderDownsample.size() - 1)].colorHandle(); }

  bool loadCombine(); ///< [optionnal] load the shader that combines the input with the blur
  void renderBlur(GLuint inputTextureHandle); ///< [optionnal] render the combined input and blur. The destination render-target must be bound by the caller.

  void set_threshold(const float newThreshold) { m_paramBrightPass.x = newThreshold; }
  void set_multiplier(const float newMul) { m_paramBrightPass.y = newMul; }

protected:
  modelRaw2D                m_quadFullScreen;
  std::vector<renderTarget> m_renderDownsample;
  std::vector<glm::vec2>    m_renderInvScreenSize;
  shader                    m_shaderBrightPass;
  shader                    m_shaderDownSample;
  shader                    m_shaderCombine;
  glm::vec2                 m_paramBrightPass;
  const uint                m_Npass;
  const bool                m_isOutHDR;
};

// ============================================================================

/**
 * @brief This class implements a tone-mapping. A vignetting is also included.
 * Simple tone-mapping: colorMAPPED.rgb = 1.f - exp(-colorHDR.rgb * exposure)
 *                      colorOUT.rgb = pow(colorMAPPED.rgb, 1./gamma) if gamma != 1
 * Simple Vignetting:
 */
class postFX_ToneMapping
{
public:
  postFX_ToneMapping() {}
  ~postFX_ToneMapping() {}

  bool load();
  void clear();

  void resolveToneMapping(GLuint inputTextureHandle, const int outwidth, const int outheigth);
  void resolveToneMapping(GLuint inputTextureHandle, renderTarget & targetFBO);

  void set_exposure(const float exposure) { m_params.x = exposure; }
  void set_gamma(const float gamma) { m_params.y = gamma; }
  void set_saturation(const float saturation) { m_params.z = saturation; }

  void set_vignetteColor(const glm::vec3 &color) { m_vignetteColor = color; }
  void set_vignettingColorIntensity(const float intensity) { m_vignettingParams.x = intensity; }
  void set_vignettingDesaturationIntensity(const float intensity) { m_vignettingParams.y = intensity; }
  void set_vignettingRoundness(const float roundness) { m_vignettingParams.z = roundness; }
  void set_vignettingSmoothness(const float smoothness) { m_vignettingParams.w = smoothness; }

protected:
  modelRaw2D m_quadFullScreen;
  shader     m_shaderToneMap;
  glm::vec4  m_params = glm::vec4(1.4f, 1.f, 0.f, 0.f); ///< packed parameters: (exposure, gamma, saturation-modifier, <unsued>)
  glm::vec4  m_vignettingParams = glm::vec4(0.5f, 0.2f, 0.8f, 0.5f); ///< packed parameters: (color-intensity, desaturation-intensity, roundness, smoothness)
  glm::vec3  m_vignetteColor = glm::vec3(0.f);
};

// ============================================================================

} // namespace

#endif // RENDERTARGET_H
