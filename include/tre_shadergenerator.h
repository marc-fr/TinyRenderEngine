#ifndef SHADERGENERATOR_H
#define SHADERGENERATOR_H

#include <vector>
#include <string>

namespace tre {

/**
 * @brief The shader generator class
 * It handles the creation of GLSL program source.
 *
 * The shader conventions are the following:
 *
 * Varying variables:
 * N: Name with 2D shaders     | Name with 3D shaders
 * 0: vertexPosition(vec2)     | vertexPosition(vec3)
 * 1: --                       | vertexNormal(vec3)
 * 2: vertexUV(vec2)           | vertexUV(vec2)
 * 3: vertexColor(vec4)        | vertexColor(vec4)
 * 4: --                       | vertexTangentU(vec4)
 * 5: instancePosition(vec4)   | instancePosition(vec4)
 * 6: instanceColor(vec4)      | instanceColor(vec4)
 * 7: instanceAtlasBlend(vec4) | instanceAtlasBlend(vec4)
 * 8-9-10: --                  | instanceOrientation(mat3 -> 3 * vec3.xyz0)
 * 11: instanceRotation(float) | instanceRotation(float)
 *
 *
 * Uniform variables
 * Matrix(2D)    : MPVM(mat3)
 * Matrix(3D)    : MPVM(mat4), MView(mat4), MModel(mat4), MOrientation(mat3)
 * Color         : unicolor(vec4)
 * Texture       : AtlasInvDim(vec2), uniBlend(vec2)
 * Phong-lighting: uniPhong(vec3){hardness, normal-remap, spec-intensity}
 * BRDF-lighting : uniBRDF(vec2){metal, roughness}
 * Transparent   : SoftDistance(vec3){distance, near, far}
 *
 * Samplers
 * Diffuse       : TexDiffuse(sampler2D),TexDiffuseB(sample2D),
 *                 TexCube(sampleCube),TexCubeB(sampleCube)
 * Geometry      : TexNormal(sampler2D)
 * BRDF-lighting : TexBRDF(sample2D)
 * Shadow        : TexShadowSun[0-N](sampler2D), TexShadowPts[0-N](sampler3D)
 * Self-Depth    : TexDepth(sample2D)
 *
 * Uniform-Buffer-Objects
 * - SunLight
 * - SunShadow
 * - PtsLight
 * - PtsShadow
 *
 */
class shaderGenerator
{
public:
  shaderGenerator() : m_layout(PRGM_2D) {}

  enum e_category
  {
    PRGM_2D      , ///< use 2D-layout and 2D-projection
    PRGM_2Dto3D  , ///< use 2D-layout and 3D-projection
    PRGM_3D      , ///< use 3D-layout and 3D-projection
    PRGM_3D_DEPTH, ///< use 3D-layout and 3D-projection, but write in the depth buffer only
  };

  // options - diffuse color
  static const int PRGM_UNICOLOR   = 0x100000; ///< use an uniform-color multiply-mask
  static const int PRGM_TEXTURED   = 0x200000; ///< use diffuse-color texture
  static const int PRGM_CUBEMAPED  = 0x400000; ///< use cube-maped texture
  static const int PRGM_COLOR      = 0x800000; ///< use color from array-buffer
  // options - ligthing
  static const int PRGM_MASK_LIGHT = 0x00F000;
  static const int PRGM_LIGHT_SUN  = 0x001000; ///< enable uni-directional light
  static const int PRGM_LIGHT_PTS  = 0x002000; ///< enable point lights
  static const int PRGM_SHADOW_SUN = 0x004000; ///< enable shadow cast with PRGM_LIGHT_SUN
  static const int PRGM_SHADOW_PTS = 0x008000; ///< enable shadow cast with PRGM_LIGHT_PTS
  static const int PRGM_NO_SELF_SHADOW = 0x010000; ///< disable self-shadowing (no-bias)
  // options - material
  static const int PRGM_MASK_BRDF  = 0x000300;
  static const int PRGM_UNIBRDF    = 0x000100; ///< enable BRDF lighting, with material properties (metallic, roughness) as uniform variable
  static const int PRGM_MAPBRDF    = 0x000200; ///< enable BRDF lighting, with material properties (metallic, roughness) as map (2D-map only)
  static const int PRGM_MAPNORMAL  = 0x000400; ///< enable Normal map
  static const int PRGM_UNIPHONG   = 0x000800; ///< with Phong lighting, have metarieal properties (hardness, normalRemap) as uniform variable
  // options - texture
  static const int PRGM_BLEND      = 0x000010; ///< enable texture blending (with PRGM_TEXTURED or PRGM_CUBEMAPED)
  static const int PRGM_SOFT       = 0x000020; ///< enable soft-distance transparency.
  static const int PRGM_OPT_SKYBOX = 0x000040; ///< enable skybox-optimization: draw skybox at the end, with glDepthFunc(GL_LEQUAL)
  // options - instanced
  static const int PRGM_INSTANCED  = 0x000001;
  static const int PRGM_ORIENTATION= 0x000002;
  static const int PRGM_INSTCOLOR  = 0x000004;
  static const int PRGM_ATLAS      = 0x000008; ///< enable texture atlas (with PRGM_INSTANCED and PRGM_TEXTURED)
  static const int PRGM_ROTATION   = 0x000080; ///< enable instanced rotation (with PRGM_INSTANCED)

  struct s_layout
  {
    e_category category;
    // vertex-buffers
    bool hasBUF_Normal;
    bool hasBUF_UV;                   ///< Implicitly, "pixelUV" is available in the fragment shader
    bool hasBUF_Color;                ///< Implicitly, "pixelColor" is available in the fragment shader
    bool hasBUF_TangentU;             ///< Implicitly, "pixelTangU" and "pixelTangV" are available in the fragment shader
    bool hasBUF_InstancedPosition;
    bool hasBUF_InstancedColor;       ///< Implicitly, "pixelColor" is available in the fragment shader
    bool hasBUF_InstancedAtlasBlend;
    bool hasBUF_InstancedOrientation;
    bool hasBUF_InstancedRotation;
    // additional data (to be supplied to the fragment shader)
    bool hasPIX_UVW;
    bool hasPIX_Position;
    bool hasPIX_Normal;
    bool hasPIX_Position_clipspace;
    // Uniforms
    bool hasUNI_MPVM;
    bool hasUNI_MView;
    bool hasUNI_MModel;
    bool hasUNI_MOrientation;
    bool hasUNI_uniColor;
    bool hasUNI_uniBRDF;
    bool hasUNI_uniPhong;
    bool hasUNI_uniBlend;
    bool hasUNI_AtlasInvDim;
    bool hasUNI_SoftDistance;
    // UBO
    bool hasUBO_sunlight;
    bool hasUBO_sunshadow;
    bool hasUBO_ptslight;
    bool hasUBO_ptsshadow;
    // Texture sampler
    bool hasSMP_Diffuse;
    bool hasSMP_DiffuseB;
    bool hasSMP_Cube;
    bool hasSMP_CubeB;
    bool hasSMP_Normal;
    bool hasSMP_BRDF;
    bool hasSMP_ShadowSun;
    bool hasSMP_ShadowPts;
    bool hasSMP_Depth;
    // Output(s) of the fragment shader
    bool hasOUT_Color0;
    bool hasOUT_Color1;
    bool hasOUT_Depth;
    // Miscellaneous
    bool hasOPT_NoDepthTest;
    // Pipeline
    bool hasPIP_Geom;

    s_layout(const e_category cat, const int flags = 0);

    bool is2D() const { return category == PRGM_2D; }
    bool is3D() const { return category == PRGM_3D || category == PRGM_2Dto3D || category == PRGM_3D_DEPTH; }
  };

  const s_layout &layout() const { return m_layout; }

protected:

  static const unsigned SHADOW_SUN_MAX = 4;
  static const unsigned LIGHT_PTS_MAX = 8;

  void createShaderFunctions_Light_BlinnPhong(std::string & outstring); ///< Generic functions for Blinn-Phong lighting
  void createShaderFunctions_Light_BRDF(std::string & outstring); ///< Generic functions for BRDF lighting
  void createShaderFunctions_Shadow(std::string & outstring); ///< Generic functions for Shadowing

  void createShaderFunction_Diffuse(const int flags, std::string & gatherColors); ///< Part of createShaderSource_FragmentMain
  void createShaderFunction_Light(const int flags, std::string & gatherLights); ///< Part of createShaderSource_FragmentMain
  void createShaderFunction_Transparent(const int flags, std::string &transparentFn); ///< Part of createShaderSource_FragmentMain

  void createShaderSource_Layout(std::string & sourceVertex, std::string & sourceFragment); ///< Create only the shader headers. If withGeom, only the vertex-shader is set.
  void createShaderSource_VertexMain(std::string & sourceVertex); ///< Create the "main" in the vertex-shader
  void createShaderSource_FragmentMain(const int flags, std::string & sourceFragment); ///< Create the "main" in the fragment-shader

  s_layout m_layout;
  unsigned m_shadowSun_count = 0; ///< It defines how many samplers are defined in the shader, at the build time (not dynamic).
  unsigned m_shadowPts_count = 0; ///< It defines how many samplers are defined in the shader, at the build time (not dynamic).
};

} // namespace

#endif // SHADERGENERATOR_H
