#ifndef SHADER_H
#define SHADER_H

#include "tre_utils.h"
#include "tre_shadergenerator.h"

#include <vector>
#include <string>

namespace tre {

/**
 * @brief The shader class
 * It handles the creation and usage of GLSL programs.
 *
 * See @class shaderGenerator for shader conventions.
 */
class shader : public shaderGenerator
{
public:
  shader() : shaderGenerator() { m_uniformVars.fill(-2); }
  shader(const shader &) = delete;
  ~shader() { TRE_ASSERT(m_drawProgram==0); }

  shader & operator =(const shader &) = delete;

  /// @name Compile
  /// @{
public:
  bool loadShader(e_category cat, int flags, const char *pname = nullptr); ///< compile and load a shader into the GPU
  bool loadShaderWireframe(e_category cat, int flags, const char *pname = nullptr); ///< compile and load a shader that transforms triangles to lines.

  bool loadCustomShader(const s_layout & shaderLayout, const char * sourceMainFrag, const char *pname); ///< compile and load a shader, with a custom fragment stage
  bool loadCustomShaderVF(const s_layout & shaderLayout, const char * sourceMainVert, const char * sourceMainFrag, const char *pname); ///< compile and load a shader, with a custom vertex and fragment stages
  bool loadCustomShaderGF(const s_layout & shaderLayout, const char * sourceFullGeom, const char * sourceFullFrag, const char *pname); ///< compile and load a shader, with a custom geometry and fragment stages
  bool loadCustomShaderVGF(const s_layout & shaderLayout, const char * sourceMainVert, const char * sourceFullGeom, const char * sourceFullFrag, const char *pname); ///< compile and load a shader, with custom stages.

  const std::string & getName() const { return m_name; }

  void clearShader(); ///< free linked shaders-program. Warning: does not free the UBOs (they may be shared between shaders)

  GLuint m_drawProgram = 0;

  /// @}

  /// @name Uniforms
  /// @{
public:
  enum uniformname { MPVM, MView, MModel, MOrientation,
                     uniBlend, uniColor, uniBRDF, uniPhong,
                     AtlasInvDim, SoftDistance,
                     TexDiffuse, TexDiffuseB, TexCube, TexCubeB,
                     TexNormal, TexBRDF,
                     TexShadowSun0, TexShadowSun1, TexShadowSun2, TexShadowSun3,
                     TexShadowPts0,
                     TexDepth,
                     NCOMUNIFORMVAR };
  GLint getUniformLocation(const uniformname utype) const; ///< get very-common uniform variables
  GLint getUniformLocation(const char * uname) const; ///< get other uniform variables

  void setUniformMatrix(const glm::mat3 & MPVM, const glm::mat3 & MModel = glm::mat3(1.f)) const;
  void setUniformMatrix(const glm::mat4 & MPVM, const glm::mat4 & MModel = glm::mat4(1.f), const glm::mat4 & MView = glm::mat4(1.f)) const;

  void  setShadowSunSamplerCount(uint count); ///< Before the shader compilation, set the nbr of maximal sun-shadows. By default, no sampler will be declared (value = 0).
  void  setShadowPtsSamplerCount(uint count); ///< Before the shader compilation, set the nbr of maximal pts-shadows. By default, no sampler will be declared (value = 0).
  /// @}

  /// @name UBO
  /// @{
public:

  /// UBO sun-light
  struct s_UBOdata_sunLight
  {
    glm::vec3 direction = glm::vec3(0.f,-1.f,0.f);
    float unused_padding_1;
    glm::vec3 color = glm::vec3(0.f);
    float unused_padding_2;
    glm::vec3 colorAmbiant = glm::vec3(0.f);
    float unused_padding_3;
  };
  bool activeUBO_sunLight(); ///< Create the UBO if needed, and bind it to the current shader
  static void updateUBO_sunLight(const s_UBOdata_sunLight &data) { UBOhandle_sunLight.update(reinterpret_cast<const void*>(&data)); }

  /// UBO sun-shadow
  struct s_UBOdata_sunShadow
  {
    glm::mat4 mPV[SHADOW_SUN_MAX];
    glm::vec4 mapBoxUVNF[SHADOW_SUN_MAX];      ///< For each shadow-map, the world-dimension of the shadow-map "frustrum" zone (dX, dY, near, far)
    glm::vec4 mapInvDimension[SHADOW_SUN_MAX]; ///< For each shadow-map, the inverse of the dimension of the texture (1/width, 1/height)
    uint  nShadow = 0;
    float     unused_padding[3];

    glm::mat4 & matPV(const uint i) { TRE_ASSERT(i<SHADOW_SUN_MAX); return mPV[i]; } ///< Get the proj-view matrix in a safe way
    glm::vec4 & mapBox(const uint i) { TRE_ASSERT(i<SHADOW_SUN_MAX); return mapBoxUVNF[i]; } ///< Get the bbox of the shadow-map (in world dimenison) in a safe way. Contains (dX, dV, near, far)
    glm::vec2 & mapInvDim(const uint i) { TRE_ASSERT(i<SHADOW_SUN_MAX); return *reinterpret_cast<glm::vec2*>(&mapInvDimension[i]); } ///< Get the shadow-map dimenison (1/width, 1/height) in a safe way.
  };
  bool activeUBO_sunShadow(); ///< Create the UBO if needed, and bind it to the current shader
  static void updateUBO_sunShadow(const s_UBOdata_sunShadow &data) { UBOhandle_sunShadow.update(reinterpret_cast<const void*>(&data)); }

  /// UBO pts-light
  struct s_UBOdata_ptstLight
  {
    glm::vec4  position[LIGHT_PTS_MAX];                                 ///< position[].w is unused
    glm::vec4  color[LIGHT_PTS_MAX];                                    ///< color[].w is the intensity
    glm::uvec4 castsShadow[LIGHT_PTS_MAX] = {glm::uvec4(uint(-1))}; ///< if the light does not cast shadow, set '-1'
    uint   nLight = 0;

    glm::vec4 & pos(const uint i) { TRE_ASSERT(i<LIGHT_PTS_MAX); return position[i]; } ///< Get the position in a safe way
    glm::vec4 & col(const uint i) { TRE_ASSERT(i<LIGHT_PTS_MAX); return color[i]; } ///< Get the color in a safe way
    uint  & shadow(const uint i) { TRE_ASSERT(i<LIGHT_PTS_MAX); return *reinterpret_cast<uint*>(&castsShadow[i].x); } ///< Get the shadow-id in a safe way
  };
  bool activeUBO_ptsLight(); ///< Create the UBO if needed, and bind it to the current shader
  static void updateUBO_ptsLight(const s_UBOdata_ptstLight &data) { UBOhandle_ptsLight.update(static_cast<const void*>(&data)); }

  /// UBO pts-light
  struct s_UBOdata_ptsShadow
  {
    glm::vec4 mapBoxUVNF;       ///< the world-dimension of the shadow-map "frustrum" zone (dX, dY, near, far)
    glm::vec2 mapInvDimension;  ///< the inverse of the dimension of the texture (1/width, 1/height)
    float     unused_padding[2];
  };
  bool activeUBO_ptsShadow(); ///< Create the UBO if needed, and bind it to the current shader
  static void updateUBO_ptsShadow(const s_UBOdata_ptsShadow &data) { UBOhandle_ptsShadow.update(static_cast<const void*>(&data)); }

  static void clearUBO(); ///< Clear all (predefined) UBOs

  /**
   * @brief The s_UBOhandle struct
   */
  struct s_UBOhandle
  {
    GLuint  m_handle = 0;             ///< GPU buffer attached to the UBO
    GLuint  m_bindpoint = GLuint(-1); ///< shader binding-point for the UBO
    uint    m_buffersize = 0;         ///< Buffer byte-size
    s_UBOhandle() {}
    ~s_UBOhandle() { TRE_ASSERT(m_handle==0); }
    void create(uint sizeofdata);
    void update(const void* data);
    void clear();
  };

  bool activeUBO_custom(const s_UBOhandle &ubo, const char *name); ///< Bind a custom UBO to the current shader

private:

  bool linkProgram(const char * sourceVert, const char * sourceFrag);
  bool linkProgram(const char * sourceVert, const char * sourceGeom, const char * sourceFrag);
  bool compileShader(GLuint & shaderHandle, const GLenum shaderType, const char * shaderSource);

  std::string m_name;
  void compute_name(e_category cat, int flags, const char * pname); ///< Compute the name of the shader.

  std::array<GLint, NCOMUNIFORMVAR> m_uniformVars;

  static GLuint UBObindpoint_incr;

  static s_UBOhandle UBOhandle_sunLight;
  static s_UBOhandle UBOhandle_sunShadow;
  static s_UBOhandle UBOhandle_ptsLight;
  static s_UBOhandle UBOhandle_ptsShadow;
};


} // namespace

#endif // SHADER_H
