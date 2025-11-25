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

  enum e_flags
  {
    // options - diffuse color
    PRGM_UNICOLOR   = 0x100000, ///< use an uniform-color multiply-mask
    PRGM_TEXTURED   = 0x200000, ///< use diffuse-color texture
    PRGM_CUBEMAPED  = 0x400000, ///< use cube-maped texture
    PRGM_COLOR      = 0x800000, ///< use color from array-buffer
    // options - ligthing
    PRGM_MASK_LIGHT = 0x00F000,
    PRGM_LIGHT_SUN  = 0x001000, ///< enable uni-directional light
    PRGM_LIGHT_PTS  = 0x002000, ///< enable point lights
    PRGM_SHADOW_SUN = 0x004000, ///< enable shadow cast with PRGM_LIGHT_SUN
    PRGM_SHADOW_PTS = 0x008000, ///< enable shadow cast with PRGM_LIGHT_PTS
    PRGM_NO_SELF_SHADOW = 0x010000, ///< disable self-shadowing (no-bias)
    PRGM_AO         = 0x020000, ///< enable Ambiant-Occlusion
    // options - material
    PRGM_MODELPHONG = 0x000800, ///< choose Phong-Blinn lighting, instead of GGX
    PRGM_MAPMAT     = 0x000200, ///< have material properties (metallic, roughness) as map (2D-texture). Otherwise, as uniform.
    PRGM_MAPNORMAL  = 0x000400, ///< enable Normal map
    // options - texture
    PRGM_BLEND      = 0x000010, ///< enable texture blending (with PRGM_TEXTURED or PRGM_CUBEMAPED)
    PRGM_SOFT       = 0x000020, ///< enable soft-distance transparency.
    PRGM_BACKGROUND = 0x000040, ///< enable write depth = 1 (often used with glDepthFunc(GL_LEQUAL))
    // options - instanced
    PRGM_INSTANCED  = 0x000001,
    PRGM_ORIENTATION= 0x000002,
    PRGM_INSTCOLOR  = 0x000004,
    PRGM_ATLAS      = 0x000008, ///< enable texture atlas (with PRGM_INSTANCED and PRGM_TEXTURED)
    PRGM_ROTATION   = 0x000080, ///< enable instanced rotation (with PRGM_INSTANCED)
  };

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
    bool hasPIX_Position;
    bool hasPIX_Normal;
    bool hasPIX_PositionClipspace;
    // Uniforms
    bool hasUNI_MPVM;
    bool hasUNI_MView;
    bool hasUNI_MModel;
    bool hasUNI_MOrientation;
    bool hasUNI_uniColor;
    bool hasUNI_uniMat;
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
    bool hasSMP_Mat;
    bool hasSMP_ShadowSun;
    bool hasSMP_ShadowPts;
    bool hasSMP_AO;
    bool hasSMP_Depth;
    // Output(s) of the fragment shader
    bool hasOUT_Color0;
    bool hasOUT_Color1;
    bool hasOUT_Depth;
    // Miscellaneous
    bool hasOPT_DepthOne;
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

  void createShaderFunctions_Light(std::string & outstring); ///< Generic functions for BRDF lighting
  void createShaderFunctions_Shadow(std::string & outstring); ///< Generic functions for Shadowing

  void createShaderFunction_Diffuse(const int flags, std::string & gatherColors); ///< Part of createShaderSource_FragmentMain
  void createShaderFunction_Light(const int flags, std::string & gatherLights); ///< Part of createShaderSource_FragmentMain
  void createShaderFunction_Transparent(const int flags, std::string &transparentFn); ///< Part of createShaderSource_FragmentMain

  void createShaderSource_Layout(std::string & sourceVertex, std::string & sourceFragment); ///< Create only the shader headers.
  void createShaderSource_VertexMain(std::string & sourceVertex); ///< Create the "main" in the vertex-shader
  void createShaderSource_GeomWireframe(std::string & sourceGeom); ///< Create the full geometry-shader (expecting triangles as input, outputs lines)
  void createShaderSource_FragmentMain(const int flags, std::string & sourceFragment); ///< Create the "main" in the fragment-shader

  s_layout m_layout;
  unsigned m_shadowSun_count = 0; ///< It defines how many samplers are defined in the shader, at the build time (not dynamic).
  unsigned m_shadowPts_count = 0; ///< It defines how many samplers are defined in the shader, at the build time (not dynamic).
};

} // namespace

#endif // SHADERGENERATOR_H
