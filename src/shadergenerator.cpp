#include "tre_shadergenerator.h"

#include "tre_utils.h"

namespace tre {

// ============================================================================

shaderGenerator::s_layout::s_layout(const e_category cat, const int flags)
{
  category = cat;

  // do some checks

  TRE_ASSERT(is2D() || is3D());

  // fill layout ...

  hasBUF_Normal    = (flags & (PRGM_MASK_LIGHT)) && (!(cat == PRGM_2D || cat == PRGM_2Dto3D));
  hasBUF_UV        = flags & (PRGM_TEXTURED | PRGM_MAPNORMAL | PRGM_MAPBRDF);
  hasBUF_Color     = flags & (PRGM_COLOR);
  hasBUF_TangentU  = flags & (PRGM_MAPNORMAL) && (!(cat == PRGM_2D || cat == PRGM_2Dto3D));
  hasBUF_InstancedPosition    = flags & (PRGM_INSTANCED);
  hasBUF_InstancedColor       = (flags & (PRGM_INSTANCED)) && (flags & (PRGM_INSTCOLOR));
  hasBUF_InstancedAtlasBlend  = (flags & (PRGM_INSTANCED)) && (flags & (PRGM_BLEND | PRGM_ATLAS));
  hasBUF_InstancedOrientation = (flags & (PRGM_INSTANCED)) && (flags & (PRGM_ORIENTATION));
  hasBUF_InstancedRotation    = (flags & (PRGM_INSTANCED)) && (flags & (PRGM_ROTATION));

  hasPIX_UVW                = flags & (PRGM_CUBEMAPED);
  hasPIX_Position           = flags & (PRGM_MASK_LIGHT);
  hasPIX_Normal             = flags & (PRGM_MASK_LIGHT) || (flags & (PRGM_MAPNORMAL));
  hasPIX_Position_clipspace = flags & (PRGM_SOFT);

  hasUNI_MPVM      = (flags != 0) || is3D();
  hasUNI_MView     = (flags & (PRGM_MASK_LIGHT)) || (flags & (PRGM_CUBEMAPED)) || (flags & (PRGM_MAPNORMAL));
  hasUNI_MModel    = (flags & (PRGM_MASK_LIGHT)) || (flags & (PRGM_INSTANCED));
  hasUNI_MOrientation = (flags & (PRGM_INSTANCED)) && is3D() && !(flags & (PRGM_ORIENTATION));
  hasUNI_uniColor  = flags & (PRGM_UNICOLOR);
  hasUNI_uniBRDF   = flags & (PRGM_UNIBRDF);
  hasUNI_uniPhong  = flags & (PRGM_UNIPHONG);
  hasUNI_uniBlend = !(flags & (PRGM_INSTANCED)) && (flags & (PRGM_BLEND));
  hasUNI_AtlasInvDim   = (flags & (PRGM_INSTANCED)) && (flags & (PRGM_ATLAS));
  hasUNI_SoftDistance = flags & (PRGM_SOFT);

  hasUBO_sunlight  = flags & (PRGM_LIGHT_SUN);
  hasUBO_sunshadow = flags & (PRGM_SHADOW_SUN);
  hasUBO_ptslight  = flags & (PRGM_LIGHT_PTS);
  hasUBO_ptsshadow = flags & (PRGM_SHADOW_PTS);

  hasSMP_Diffuse   = flags & (PRGM_TEXTURED);
  hasSMP_DiffuseB  = hasSMP_Diffuse && (flags & (PRGM_BLEND)) && !(flags & (PRGM_ATLAS) );
  hasSMP_Cube      = flags & (PRGM_CUBEMAPED);
  hasSMP_CubeB     = hasSMP_Cube && (flags & (PRGM_BLEND)) && !(flags & (PRGM_ATLAS) );
  hasSMP_Normal    = flags & (PRGM_MAPNORMAL);
  hasSMP_BRDF      = flags & (PRGM_MAPBRDF);
  hasSMP_ShadowSun = flags & (PRGM_SHADOW_SUN);
  hasSMP_ShadowPts = flags & (PRGM_SHADOW_PTS);
  hasSMP_Depth     = flags & (PRGM_SOFT);

  hasOUT_Color0    = (flags != 0) && (cat != PRGM_3D_DEPTH);
  hasOUT_Color1    = false;
  hasOUT_Depth     = (cat == PRGM_3D_DEPTH);

  hasOPT_DepthOne = flags & (PRGM_BACKGROUND);

  hasPIP_Geom      = false;
}

// ============================================================================

void shaderGenerator::createShaderFunctions_Light_BlinnPhong(std::string &outstring)
{
  outstring +=
      "const float PI = 3.14159265359;\n"
      "vec3 BlinnPhong(vec3 albedo, vec3 radiance, vec3 N, vec3 L, vec3 V, float hardness, float normalRemap, float specFactor)\n"
      "{\n"
      "  vec3 H = normalize(L + V);\n"
      "  float NdotL = clamp(dot(N,L) * (1.f - normalRemap) + normalRemap, 0.f, 1.f);\n"
      "  float NdotH = max(dot(N,H), 0.f);\n"
      "  return (albedo / PI * NdotL + vec3(1.f) * specFactor / PI * pow(NdotH, hardness) * (hardness + 8.f) / (8.f * PI) ) * radiance;\n"
      "}\n"
      "vec3 BlinnPhong_ambiante(vec3 albedo, vec3 ambiante, vec3 N, vec3 L, float normalRemap, float specFactor)\n"
      "{\n"
      "  float NdotL_ao = clamp(dot(N,L) * 0.7f * (1.f - normalRemap) + 0.9f + normalRemap * 0.7f, 0.f, 1.f); // pseudo Ambiant-Occlusion\n"
      "  return (albedo + vec3(1.f) * specFactor) / PI * ambiante * NdotL_ao;\n"
      "}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderFunctions_Light_BRDF(std::string &outstring)
{
  /// credits: https://learnopengl.com/PBR/Lighting
  outstring +=
      "const float PI = 3.14159265359;\n"
      "vec3 _fresnelSchlick(vec3 F0, float cosTheta)\n"
      "{\n"
      "  return F0 + (vec3(1.f) - F0) * pow(1.f - cosTheta, 5.f);\n"
      "}\n"
      "float _DistributionGGX(float cosTheta, float roughness)\n"
      "{\n"
      "  float a2    = roughness * roughness;\n"
      "  float denom = cosTheta * cosTheta * (a2 - 1.f) + 1.f;\n"
      "  denom = PI * denom * denom;\n"
      "  return a2 / denom;\n"
      "}\n"
      "float _GeometrySchlickGGX(float cosTheta, float roughness)\n"
      "{\n"
      "  float r = roughness + 1.f;\n"
      "  float k = r * r / 8.f;\n"
      "  return cosTheta / (cosTheta * (1.f - k) + k);\n"
      "}\n"
      "float _GeometrySmith(float NdotV, float NdotL, float roughness)\n"
      "{\n"
      "  return _GeometrySchlickGGX(NdotV, roughness) * _GeometrySchlickGGX(NdotL, roughness);\n"
      "}\n"
      "vec3 BRDFLighting(vec3 albedo, vec3 radiance, vec3 N, vec3 L, vec3 V, float metallic, float roughnessUser)\n"
      "{\n"
      "  float roughness = clamp(roughnessUser * roughnessUser, 1.e-3f, 1.f);\n"
      "  float NdotL = max(dot(N, L), 0.f);           // Light-incidence\n"
      "  float NdotV = clamp(dot(N, V), 1.e-3f, 1.f); // View-incidence\n"
      "  vec3 H = normalize(L + V);                   // Half-way vector\n"
      "  float VdotH = max(dot(V, H), 0.f);\n"
      "  float NdotH = max(dot(N, H), 0.f);\n"
      "  float LdotH = max(dot(L, H), 0.f);\n"
      "  // Material coef. of refraction and reflection\n"
      "  vec3 F0     = min(mix(vec3(0.04f), albedo, metallic), vec3(1.f));\n"
      "  vec3 kRefl  = _fresnelSchlick(F0, LdotH); // ratio of reflected light over total light\n"
      "  vec3 kRefr  = (vec3(1.f) - kRefl) * (1.f - metallic);\n"
      "  // Cook-torrance BRDF\n"
      "  float NDis  = _DistributionGGX(NdotH, roughness); //approximate amount of microfacets alligned with H\n"
      "  float Geom  = _GeometrySmith(NdotV, NdotL, roughness); //approximate amount of microfactes obstruction from the View(V) and the Light(L)\n"
      "  // Outgoing radiance\n"
      "  return (kRefr * albedo / PI + NDis * Geom * kRefl / max(4.0 * NdotV * NdotL, 0.001) ) * radiance * NdotL;\n"
      "}\n"
      "vec3 BRDFLighting_ambiante(vec3 albedo, vec3 ambiante, vec3 N, vec3 V, float metallic, float roughnessUser)\n"
      "{\n"
      "  float roughness = clamp(roughnessUser * roughnessUser, 1.e-3f, 1.f);\n"
      "  float NdotV = max(dot(N, V), 0.f);\n"
      "  // Material coef. of refraction and reflection\n"
      "  vec3 F0     = mix(vec3(0.04f), albedo, metallic);\n"
      "  vec3 F90    = mix(vec3(1.f), albedo, metallic);\n"
      "  vec3 kRefl  = F0 + (F90 - F0) * pow(1.f - NdotV, 5.f) * (0.7f - 0.5f * roughness);\n"
      "  vec3 kRefr  = (F90 - kRefl) * (1.f - metallic);\n"
      "  // Outgoing radiance\n"
      "  return (kRefr * albedo + kRefl) / PI * ambiante;\n"
      "}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderFunctions_Shadow(std::string &outstring)
{
  outstring.reserve(outstring.size() + 4096);

  if (m_layout.hasSMP_ShadowSun)
  {
    outstring += "float ShadowOcclusion_sun(float tanTheta, vec3 rawN)\n"
                 "{\n"
                 "  float islighted_sun = 1.f;\n";

    for (uint iS = 0; iS < m_shadowSun_count; ++iS)
    {
      const std::string str_iS = std::to_string(iS);
      outstring += "  if (" + str_iS + " < m_sunshadow.nShadow)\n"
                   "  {\n"
                   "    vec2  dxy = m_sunshadow.mapBoxUVNF[" + str_iS + "].xy * m_sunshadow.mapInvDimension[" + str_iS + "];\n"
                  "     float dz = 1.5f * max(dxy.x, dxy.y) * clamp(tanTheta, 1.e-2f, 1.e2f);\n"
                   "    float bias = dz / (m_sunshadow.mapBoxUVNF[" + str_iS + "].w - m_sunshadow.mapBoxUVNF[" + str_iS + "].z);\n"
                   "    vec4 projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition,1.f);\n"
                   "    vec3 uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "    float texDepth0 = texture(TexShadowSun" + str_iS + ",uvw.xy).r;\n"
                   "    if (tanTheta > 1.e2f)\n"
                   "    {\n"
                   "      islighted_sun = min(islighted_sun, 0.3f);\n"
                   "    }\n"
                   "    else if (texDepth0 > 0.f && texDepth0 < 1.f) // neither the near, neither the far plane of the map\n"
                   "    {\n"
                   "      float islighted_sun_local = 0.f;\n"
                   "      if (texDepth0 - uvw.z > -bias) { islighted_sun_local += 0.4f; }\n"
                   "      vec3 dxP = vec3(dxy.x, 0.f, 0.f) - dxy.x * rawN.x * rawN;\n"
                   "      projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition - dxP,1.f);\n"
                   "      uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy).r - uvw.z > -bias) { islighted_sun_local += 0.15f; }\n"
                   "      projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition + dxP,1.f);\n"
                   "      uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy).r - uvw.z > -bias) { islighted_sun_local += 0.15f; }\n"
                   "      vec3 dyP = vec3(0.f, dxy.y, 0.f) - dxy.y * rawN.y * rawN;\n"
                   "      projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition - dyP,1.f);\n"
                   "      uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy).r - uvw.z > -bias) { islighted_sun_local += 0.15f; }\n"
                   "      projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition + dyP,1.f);\n"
                   "      uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy).r - uvw.z > -bias) { islighted_sun_local += 0.15f; }\n"
                   "      islighted_sun = min(islighted_sun, islighted_sun_local);\n"
                   "    }\n"
                   "  }\n";
    }

    outstring += "  return islighted_sun;\n"
                 "}\n";

    outstring += "float ShadowOcclusion_sun_nobias()\n"
                 "{\n"
                 "  float islighted_sun = 1.f;\n";

    for (uint iS = 0; iS < m_shadowSun_count; ++iS)
    {
      const std::string str_iS = std::to_string(iS);
      outstring += "  if (" + str_iS + " < m_sunshadow.nShadow)\n"
                   "  {\n"
                   "    vec4 projInMap = m_sunshadow.mPV[" + str_iS + "] * vec4(pixelPosition,1.f);\n"
                   "    vec3 uvw = 0.5f + 0.5f * projInMap.xyz / projInMap.w;\n"
                   "    float texDepth0 = texture(TexShadowSun" + str_iS + ",uvw.xy).r;\n"
                   "    if (texDepth0 > 0.f && texDepth0 < 1.f) // neither the near, neither the far plane of the map\n"
                   "    {\n"
                   "      float islighted_sun_local = 0.f;\n"
                   "      if (texDepth0 - uvw.z > 0.f) { islighted_sun_local += 0.4f; }\n"
                   "      vec2  du  = vec2(m_sunshadow.mapInvDimension[" + str_iS + "].x, 0.f);\n"
                   "      vec2  dv  = vec2(0.f, m_sunshadow.mapInvDimension[" + str_iS + "].y);\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy + du).r - uvw.z > 0.f) { islighted_sun_local += 0.15f; }\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy - du).r - uvw.z > 0.f) { islighted_sun_local += 0.15f; }\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy + dv).r - uvw.z > 0.f) { islighted_sun_local += 0.15f; }\n"
                   "      if (texture(TexShadowSun" + str_iS + ",uvw.xy - dv).r - uvw.z > 0.f) { islighted_sun_local += 0.15f; }\n"
                   "      islighted_sun = min(islighted_sun, islighted_sun_local);\n"
                   "    }\n"
                   "  }\n";
    }

    outstring += "  return islighted_sun;\n"
                 "}\n";
  }

  if (m_layout.hasSMP_ShadowPts)
  {
    outstring += "float vectorToCubeDepth(vec3 vector, float n, float f)\n"
                 "{\n"
                 "  vec3 absVec = abs(vector);\n"
                 "  float localZ_wspace = max(absVec.x, max(absVec.y, absVec.z));\n"
                 "  float localZ_cspace = (f+n) / (f-n) - (2.f * f * n) / (f-n) / localZ_wspace;\n"
                 "  return 0.5f + 0.5f * localZ_cspace;\n"
                 "}\n";

    outstring += "float ShadowOcclusion_pts(int lightID)\n"
                 "{\n"
                 "  float bias = 0.003f;\n"
                 "  float islighted_pts = 1.f;\n";

    outstring += "  vec3 lightVector = pixelPosition - m_ptslight.position[lightID];\n"
                 "  float distance = vectorToCubeDepth(lightVector, m_ptsshadow.mapBoxUVNF.z, m_ptsshadow.mapBoxUVNF.w);\n"
                 "  float sampledDistance = texture(TexShadowPts0, lightVector).r;\n"
                 "  if (distance > sampledDistance + bias) islighted_pts = 0.f;\n";

    outstring += "  return islighted_pts;\n"
                 "}\n";

    outstring += "float ShadowOcclusion_pts_nobias(int lightID)\n"
                 "{\n"
                 "  float islighted_pts = 1.f;\n";

    outstring += "  vec3 lightVector = pixelPosition - m_ptslight.position[lightID];\n"
                 "  float distance = vectorToCubeDepth(lightVector, m_ptsshadow.mapBoxUVNF.z, m_ptsshadow.mapBoxUVNF.w);\n"
                 "  float sampledDistance = texture(TexShadowPts0, lightVector).r;\n"
                 "  if (distance > sampledDistance) islighted_pts = 0.f;\n";

    outstring += "  return islighted_pts;\n"
                 "}\n";
  }
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderFunction_Diffuse(const int flags, std::string & gatherColors)
{
  if (m_layout.category == PRGM_3D_DEPTH) return;

  gatherColors.reserve(gatherColors.size() + 1024);
  gatherColors += "vec4 Diffuse()\n"
                  "{\n";
  std::string returnValue;
  returnValue.reserve(1024);
  returnValue += "  return ";

  bool isfirst = true;

  if (flags & PRGM_UNICOLOR)
  {
    if (!isfirst) returnValue += " * ";
    isfirst = false;
    returnValue += "uniColor";
  }
  if (flags & (PRGM_COLOR | PRGM_INSTCOLOR))
  {
    if (!isfirst) returnValue += " * ";
    isfirst = false;
    returnValue += "pixelColor";
  }
  if (flags & PRGM_TEXTURED)
  {
    if (!isfirst) returnValue += " * ";
    isfirst = false;
    if (flags & (PRGM_BLEND | PRGM_ATLAS))
    {
      if (flags & PRGM_INSTANCED)
      {
        gatherColors += "  vec2 atlas = floor(pixelAtlasBlend.xy);\n";
        gatherColors += "  vec2 blend = pixelAtlasBlend.zw;\n";
      }
      else if (flags & PRGM_BLEND)
      {
        gatherColors += "  vec2 blend = uniBlend;\n";
      }
      else if (flags & PRGM_ATLAS)
      {
        TRE_ASSERT(false); // Not implemented - but dont fail on release.
      }

      if ((flags & PRGM_BLEND) && (flags | PRGM_ATLAS))
      {
        gatherColors += "  vec2 offsetUV;\n"
                        "  offsetUV.y = floor(atlas.x * AtlasInvDim.x);\n"
                        "  offsetUV.x = floor(atlas.x - offsetUV.y / AtlasInvDim.x + 0.01f);\n";
        gatherColors += "  vec2 offsetUVB;\n"
                        "  offsetUVB.y = floor(atlas.y * AtlasInvDim.x);\n"
                        "  offsetUVB.x = floor(atlas.y - offsetUVB.y / AtlasInvDim.x + 0.01f);\n";
        returnValue += "( blend.x * texture(TexDiffuse, (offsetUV  + pixelUV) * AtlasInvDim) + "
                        " blend.y * texture(TexDiffuse, (offsetUVB + pixelUV) * AtlasInvDim) )";
      }
      else if (flags & PRGM_BLEND)
      {
        returnValue += "( blend.x * texture(TexDiffuse, pixelUV) + blend.y * texture(TexDiffuseB, pixelUV))";
      }
      else if (flags & PRGM_ATLAS)
      {
        gatherColors += "  vec2 offsetUV;\n"
                        "  offsetUV.y = floor(atlas.x * AtlasInvDim.x);\n"
                        "  offsetUV.x = floor(atlas.x - offsetUV.y / AtlasInvDim.x + 0.01f);\n";
        returnValue += "texture(TexDiffuse, (offsetUV  + pixelUV) * AtlasInvDim)";
      }
    }
    else
    {
      returnValue += "texture(TexDiffuse, pixelUV)";
    }
  }
  if (flags & PRGM_CUBEMAPED)
  {
    TRE_ASSERT(!(flags & PRGM_ATLAS)); // not implemented
    if (!isfirst) gatherColors += " * ";
    isfirst = false;
    if (flags & PRGM_BLEND)
      returnValue += "(uniBlend.x * texture(TexCube, pixelUVW) + uniBlend.y * texture(TexCubeB, pixelUVW))";
    else
      returnValue += "texture(TexCube, pixelUVW)";
  }

  if (isfirst) returnValue += "vec4(1.f)";

  gatherColors += returnValue + ";\n}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderFunction_Light(const int flags, std::string &gatherLights)
{
  if (m_layout.category == PRGM_3D_DEPTH || m_layout.is2D()) return;

  //-- Constant strings
  const std::string str_LIGTH_PTS_MAX = std::to_string(LIGHT_PTS_MAX);

  //-- Function implementation.

  gatherLights.reserve(gatherLights.size() + 1024);
  gatherLights += "vec3 Light(vec3 albedo)\n"
                  "{\n";

  std::string returnval;

  if (flags & PRGM_MASK_LIGHT)
  {
    TRE_ASSERT(m_layout.hasPIX_Position);
    TRE_ASSERT(m_layout.hasPIX_Normal);

    gatherLights += "  vec3 V = - normalize((MView * vec4(pixelPosition, 1.f)).xyz);\n";

    if (flags & (PRGM_SHADOW_SUN | PRGM_MAPNORMAL))
      gatherLights += "  vec3 rawN = normalize(pixelNormal);\n";

    if (flags & PRGM_MAPNORMAL)
    {
      gatherLights += "  vec3 rawT = normalize(pixelTangU);\n"
                      "  vec3 rawB = normalize(pixelTangV);\n"
                      "  mat3 TBN = mat3(rawT, rawB, rawN);\n"
                      "  vec3 Normal_raw = TBN * (texture(TexNormal, pixelUV).xyz * 2.f - 1.f);\n"
                      "  vec4 Normal_vs = MView * vec4(Normal_raw, 0.f);\n"
                      "  vec3 N = normalize(Normal_vs.xyz);\n";
    }
    else
    {
      gatherLights += "  vec3 N = normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n";
    }

    if (flags & PRGM_MASK_BRDF)
      gatherLights += (flags & PRGM_UNIBRDF) ? "  vec2 matMetalRough = uniBRDF;\n" : "  vec2 matMetalRough = texture(TexBRDF, pixelUV).xy;\n";
    else
      gatherLights += (flags & PRGM_UNIPHONG) ? "  vec3 matHardRemapSpec = uniPhong;\n" : "  vec3 matHardRemapSpec = vec3(3.f, 0.f, 1.f);\n";

  }

  if (flags & PRGM_LIGHT_SUN)
  {
    gatherLights += "  vec3 L = - normalize((MView * vec4(m_sunlight.direction, 0.f)).xyz);\n";
    if (flags & PRGM_SHADOW_SUN)
    {
      if (flags & PRGM_NO_SELF_SHADOW)
        gatherLights += "  float islighted_sun = ShadowOcclusion_sun_nobias();\n";
      else
        gatherLights += "  float tanTheta = tan(acos(clamp(dot(rawN,normalize(-m_sunlight.direction)), 1.e-3f, 1.f)));\n"
                        "  float islighted_sun = ShadowOcclusion_sun(tanTheta, rawN);\n";
    }
    else
    {
      gatherLights += "  float islighted_sun = 1.f;\n";
    }
    if (flags & PRGM_MASK_BRDF)
    {
      gatherLights += "  vec3 lsun = BRDFLighting(albedo, m_sunlight.color,\n"
                      "                           N, L, V,\n"
                      "                           matMetalRough.x, matMetalRough.y);\n"
                      "  vec3 lamb = BRDFLighting_ambiante(albedo, m_sunlight.colorAmbiant,\n"
                      "                                    N, V,\n"
                      "                                    matMetalRough.x, matMetalRough.y);\n";
    }
    else
    {
      gatherLights += "  vec3 lsun = BlinnPhong(albedo, m_sunlight.color,\n"
                      "                         N, L, V,\n"
                      "                         matHardRemapSpec.x, matHardRemapSpec.y, matHardRemapSpec.z);\n"
                      "  vec3 lamb = BlinnPhong_ambiante(albedo, m_sunlight.colorAmbiant,\n"
                      "                                  N, L, matHardRemapSpec.y, matHardRemapSpec.z);\n";
    }
    if (!returnval.empty()) returnval += " + ";
    returnval += "lsun * islighted_sun + lamb";
  }
  if (flags & PRGM_LIGHT_PTS)
  {

    gatherLights += "  vec3 lightColor_pts = vec3(0.0f,0.f,0.f);\n"
                    "  for (int iL=0;iL<" + str_LIGTH_PTS_MAX + ";++iL)\n"
                    "  {\n"
                    "    if (iL>=m_ptslight.nLight) continue; // bug work-around\n"
                    "    vec3 lightVector = pixelPosition - m_ptslight.position[iL].xyz;\n"
                    "    vec3 L = - normalize((MView * vec4(lightVector, 0.f)).xyz);\n"
                    "    float distance = length(lightVector);\n"
                    "    vec3 lcolor = m_ptslight.color[iL].xyz * m_ptslight.color[iL].w / (distance * distance + 0.01f);\n";

    if (flags & PRGM_SHADOW_PTS)
    {
      if (flags & PRGM_NO_SELF_SHADOW)
        gatherLights += "    if (m_ptslight.castsShadow[iL] == 0) lcolor *= ShadowOcclusion_pts_nobias(iL);\n";
      else
        gatherLights += "    if (m_ptslight.castsShadow[iL] == 0) lcolor *= ShadowOcclusion_pts(iL);\n";
    }

    if (flags & PRGM_MASK_BRDF)
    {
      gatherLights += "    lightColor_pts += BRDFLighting(albedo, lcolor,\n"
                      "                                   N, L, V,\n"
                      "                                   matMetalRough.x, matMetalRough.y);\n";
    }
    else
    {
      gatherLights += "    lightColor_pts += BlinnPhong(albedo, lcolor,\n"
                      "                                 N, L, V,\n"
                      "                                 matHardRemapSpec.x, matHardRemapSpec.y, matHardRemapSpec.z);\n";
    }
    gatherLights += "  }\n";
    if (!returnval.empty()) returnval += " + ";;
    returnval += "lightColor_pts";
  }
  if (returnval.empty())
    gatherLights += "  return albedo;\n";
  else
    gatherLights += "  return " + returnval + ";\n";

  gatherLights += "}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderFunction_Transparent(const int flags, std::string &transparent)
{
  if (m_layout.category == PRGM_3D_DEPTH) return;

  //-- Function implementation.

  transparent.reserve(transparent.size() + 1024);

  if (flags & PRGM_SOFT)
  {
    transparent += "float LinearDepth(float inDepth)\n"
                   "{\n"
                   "  float n = SoftDistance.y;\n"
                   "  float f = SoftDistance.z;\n"
                   "  return n * f / (f + inDepth * (n - f));\n"
                   "}\n";

    transparent += "float Transparent(float inW)\n"
                   "{\n"
                   "  float fragDepth = LinearDepth(pixelPosition_clipspace.z * 0.5f + 0.5f);\n"
                   "  float sceneDepth01 = texture(TexDepth, pixelPosition_clipspace.xy * 0.5f + 0.5f).r;\n"
                   "  float sceneDepth = LinearDepth(sceneDepth01);\n"
                   "  float alpha = clamp((sceneDepth - fragDepth) / SoftDistance.x, 0.f, 1.f);\n"
                   "  return alpha * inW;\n"
                   "}\n";
  }
  else
  {
    transparent += "float Transparent(float inW)\n"
                   "{\n"
                   "  return inW;\n"
                   "}\n";
  }
}

// ============================================================================

void shaderGenerator::createShaderSource_Layout(std::string &sourceVertex, std::string &sourceFragment)
{
  sourceVertex.clear();
  sourceFragment.clear();

  // constant str
  const std::string str_LIGHT_PTS_MAX = std::to_string(LIGHT_PTS_MAX);
  const std::string str_SHADOW_SUN_MAX = std::to_string(SHADOW_SUN_MAX);

  const std::string prefixOut = (m_layout.hasPIP_Geom) ? "geom" : "pixel";

  // header
#ifdef TRE_OPENGL_ES
  sourceVertex += "#version 300 es\nprecision mediump float;\n";
  sourceFragment += "#version 300 es\nprecision mediump float;\n";
#else
  sourceVertex += "#version 330 core\n";
  sourceFragment += "#version 330 core\n";
#endif

  // layout

  if (m_layout.category == PRGM_2D || m_layout.category == PRGM_2Dto3D)
  {
    sourceVertex += "layout(location = 0) in vec2 vertexPosition;\n";
  }
  else if (m_layout.category == PRGM_3D || m_layout.category == PRGM_3D_DEPTH)
  {
    sourceVertex += "layout(location = 0) in vec3 vertexPosition;\n";
  }
  else
  {
    TRE_FATAL("bad shader type");
  }

  if (m_layout.hasBUF_Normal)
  {
    TRE_ASSERT(m_layout.is3D());
    sourceVertex += "layout(location = 1) in vec3 vertexNormal;\n";
  }
  if (m_layout.hasBUF_UV)
  {
    sourceVertex += "layout(location = 2) in vec2 vertexUV;\n"
                    "out vec2 " + prefixOut + "UV;\n";
    sourceFragment += "in vec2 " "pixel" "UV;\n"; // implicit
  }
  if (m_layout.hasBUF_Color)
  {
    sourceVertex += "layout(location = 3) in vec4 vertexColor;\n"
                    "out vec4 " + prefixOut + "Color;\n";
    sourceFragment += "in vec4 " "pixel" "Color;\n"; // implicit
  }
  if (m_layout.hasBUF_TangentU)
  {
    sourceVertex += "layout(location = 4) in vec4 vertexTangentU;\n"
                    "out vec3 " + prefixOut + "TangU;\n"
                    "out vec3 " + prefixOut + "TangV;\n";
    sourceFragment += "in vec3 " "pixel" "TangU;\n"
                      "in vec3 " "pixel" "TangV;\n"; // implicit
  }
  if (m_layout.hasBUF_InstancedPosition)
  {
    sourceVertex += "layout(location = 5) in vec4 instancedPosition;\n";
  }
  if (m_layout.hasBUF_InstancedColor)
  {
    sourceVertex += "layout(location = 6) in vec4 instancedColor;\n";
    if (!m_layout.hasBUF_Color)
    {
      sourceVertex += "out vec4 " + prefixOut + "Color;\n";
      sourceFragment += "in vec4 " "pixel" "Color;\n";
    }
  }
  if (m_layout.hasBUF_InstancedAtlasBlend)
  {
    sourceVertex += "layout(location = 7) in vec4 instancedAtlasBlend;\n"
                    "out vec4 " + prefixOut + "AtlasBlend;\n";
    sourceFragment += "in vec4 " "pixel" "AtlasBlend;\n";
  }
  if (m_layout.hasBUF_InstancedOrientation)
  {
    sourceVertex += "layout(location =  8) in vec4 instancedOrientationCX;\n";
    sourceVertex += "layout(location =  9) in vec4 instancedOrientationCY;\n";
    sourceVertex += "layout(location = 10) in vec4 instancedOrientationCZ;\n";
  }
  if (m_layout.hasBUF_InstancedRotation)
  {
    sourceVertex += "layout(location = 11) in float instancedRotation;\n";
  }

  if (m_layout.hasPIX_UVW)
  {
    sourceVertex += "out vec3 " + prefixOut + "UVW;\n";
    sourceFragment += "in vec3 " "pixel" "UVW;\n";
  }
  if (m_layout.hasPIX_Position)
  {
    sourceVertex += "out vec3 " + prefixOut + "Position;\n";
    sourceFragment += "in vec3 " "pixel" "Position;\n";
  }
  if (m_layout.hasPIX_Normal)
  {
    sourceVertex += "out vec3 " + prefixOut + "Normal;\n";
    sourceFragment += "in vec3 " "pixel" "Normal;\n";
  }
  if (m_layout.hasPIX_Position_clipspace)
  {
    sourceVertex += "out vec3 " + prefixOut + "Position_clipspace;\n";
    sourceFragment += "in vec3 " "pixel" "Position_clipspace;\n";
  }

  // uniform
  if (m_layout.hasUNI_MPVM)
  {
    if (m_layout.is2D()) sourceVertex += "uniform mat3 MPVM;\n";
    else                 sourceVertex += "uniform mat4 MPVM;\n";
  }
  if (m_layout.hasUNI_MView)         sourceFragment += "uniform mat4 MView;\n";
  if (m_layout.hasUNI_MModel)        sourceVertex   += "uniform mat4 MModel;\n";
  if (m_layout.hasUNI_MOrientation)  sourceVertex   += "uniform mat3 MOrientation;\n";
  if (m_layout.hasUNI_uniBlend)      sourceFragment += "uniform vec2 uniBlend;\n";
  if (m_layout.hasUNI_uniColor)      sourceFragment += "uniform vec4 uniColor;\n";
  if (m_layout.hasUNI_uniBRDF)       sourceFragment += "uniform vec2 uniBRDF;\n";
  if (m_layout.hasUNI_uniPhong)      sourceFragment += "uniform vec3 uniPhong;\n";
  if (m_layout.hasUNI_AtlasInvDim)   sourceFragment += "uniform vec2 AtlasInvDim;\n";
  if (m_layout.hasUNI_SoftDistance)  sourceFragment += "uniform vec3 SoftDistance;\n";
  if (m_layout.hasSMP_Diffuse)       sourceFragment += "uniform sampler2D TexDiffuse;\n";
  if (m_layout.hasSMP_DiffuseB)      sourceFragment += "uniform sampler2D TexDiffuseB;\n";
  if (m_layout.hasSMP_Cube)          sourceFragment += "uniform samplerCube TexCube;\n";
  if (m_layout.hasSMP_CubeB)         sourceFragment += "uniform samplerCube TexCubeB;\n";
  if (m_layout.hasSMP_Normal)        sourceFragment += "uniform sampler2D TexNormal;\n";
  if (m_layout.hasSMP_BRDF)          sourceFragment += "uniform sampler2D TexBRDF;\n";
  if (m_layout.hasSMP_ShadowSun && m_shadowSun_count >= 1) sourceFragment += "uniform sampler2D TexShadowSun0;\n";
  if (m_layout.hasSMP_ShadowSun && m_shadowSun_count >= 2) sourceFragment += "uniform sampler2D TexShadowSun1;\n";
  if (m_layout.hasSMP_ShadowSun && m_shadowSun_count >= 3) sourceFragment += "uniform sampler2D TexShadowSun2;\n";
  if (m_layout.hasSMP_ShadowSun && m_shadowSun_count >= 4) sourceFragment += "uniform sampler2D TexShadowSun3;\n";
  if (m_layout.hasSMP_ShadowPts && m_shadowPts_count >= 1) sourceFragment += "uniform samplerCube TexShadowPts0;\n";
  if (m_layout.hasSMP_Depth)         sourceFragment += "uniform sampler2D TexDepth;\n";

  TRE_ASSERT(m_layout.hasOUT_Color0 || m_layout.hasOUT_Color1 || m_layout.hasOUT_Depth);
  if (m_layout.hasOUT_Color0)
    sourceFragment += "layout(location = 0) out vec4 color;\n";
  if (m_layout.hasOUT_Color1)
    sourceFragment += "layout(location = 1) out vec4 color1;\n";

  // UBO
  if (m_layout.hasUBO_sunlight)
  {
    TRE_ASSERT(m_layout.category == PRGM_3D || m_layout.category == PRGM_2Dto3D);
    const std::string def =  "layout (std140) uniform s_sunlight\n"
                             "{\n"
                             "  vec3 direction;\n"
                             "  vec3 color;\n"
                             "  vec3 colorAmbiant;\n"
                             "} m_sunlight;\n";
    sourceFragment += def;
  }
  if (m_layout.hasUBO_sunshadow)
  {
    TRE_ASSERT(m_layout.category == PRGM_3D || m_layout.category == PRGM_2Dto3D);
    const std::string def =  "layout (std140) uniform s_sunshadow\n"
                             "{\n"
                             "  mat4 mPV[" + str_SHADOW_SUN_MAX + "];\n"
                             "  vec4 mapBoxUVNF[" + str_SHADOW_SUN_MAX + "];\n"
                             "  vec2 mapInvDimension[" + str_SHADOW_SUN_MAX + "];\n"
                             "  int  nShadow;\n"
                             "} m_sunshadow;\n";
    sourceFragment += def;
  }
  if (m_layout.hasUBO_ptslight)
  {
    TRE_ASSERT(m_layout.category == PRGM_3D || m_layout.category == PRGM_2Dto3D);
    const std::string def =  "layout (std140) uniform s_ptslight\n"
                             "{\n"
                             "  vec3 position[" + str_LIGHT_PTS_MAX + "];\n"
                             "  vec4 color[" + str_LIGHT_PTS_MAX + "];\n"
                             "  int  castsShadow[" + str_LIGHT_PTS_MAX + "];\n"
                             "  int  nLight;\n"
                             "} m_ptslight;\n";
    sourceFragment += def;
  }
  if (m_layout.hasUBO_ptsshadow)
  {
    TRE_ASSERT(m_layout.category == PRGM_3D || m_layout.category == PRGM_2Dto3D);
    const std::string def =  "layout (std140) uniform s_ptsshadow\n"
                             "{\n"
                             "  vec4 mapBoxUVNF;\n"
                             "  vec2 mapInvDimension;\n"
                             "} m_ptsshadow;\n";
    sourceFragment += def;
  }

  // generic functions
  if (m_layout.hasUBO_ptslight || m_layout.hasUBO_sunlight)
  {
    if (m_layout.hasUNI_uniBRDF || m_layout.hasSMP_BRDF)
      createShaderFunctions_Light_BRDF(sourceFragment);
    else
      createShaderFunctions_Light_BlinnPhong(sourceFragment);

    if (m_layout.hasSMP_ShadowSun || m_layout.hasSMP_ShadowPts)
      createShaderFunctions_Shadow(sourceFragment);
  }
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderSource_VertexMain(std::string &sourceVertex)
{
  const std::string prefixOut = m_layout.hasPIP_Geom ? "geom" : "pixel";

  // process
  sourceVertex += "void main()\n{\n";

  // -> position

  if (m_layout.hasBUF_InstancedPosition)
  {
    // orientation
    if (m_layout.hasBUF_InstancedOrientation && m_layout.is3D())
    {
      TRE_ASSERT(!m_layout.hasUNI_MOrientation);
      sourceVertex += "  mat3 MInstante = mat3(instancedOrientationCX.xyz, \n"
                      "                        instancedOrientationCY.xyz, \n"
                      "                        instancedOrientationCZ.xyz);\n";
    }
    else if (m_layout.is2D())
    {
      TRE_ASSERT(!m_layout.hasUNI_MOrientation);
      sourceVertex += "  mat3 MInstante = mat3(1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f);\n";
    }
    else
    {
      TRE_ASSERT(m_layout.hasUNI_MOrientation);
      sourceVertex += "  mat3 MInstante = MOrientation;\n";
    }

    // rotation
    if (m_layout.hasBUF_InstancedRotation)
    {
      sourceVertex += "  float rotcos = cos(instancedRotation);\n"
                      "  float rotsin = sin(instancedRotation);\n"
                      "  mat3 MRotateZ = mat3(rotcos,rotsin,0.f,\n"
                      "                       -rotsin,rotcos,0.f,\n"
                      "                       0.f,0.f,1.f);\n"
                      "  MInstante = MInstante * MRotateZ;\n";
    }

    if (m_layout.category == PRGM_3D || m_layout.category == PRGM_3D_DEPTH)
      sourceVertex += "  vec3 posLocal = instancedPosition.w * (MInstante * vertexPosition);\n"
                      "  vec3 out_Position = instancedPosition.xyz + posLocal;\n"
                      "  gl_Position = MPVM * vec4(out_Position,1.f);\n";
    else if (m_layout.category == PRGM_2Dto3D)
      sourceVertex += "  vec3 posLocal = instancedPosition.w * (MInstante * vec3(vertexPosition,0.f));\n"
                      "  vec3 out_Position = instancedPosition.xyz + posLocal;\n"
                      "  gl_Position = MPVM * vec4(out_Position,1.f);\n";
    else if (m_layout.category == PRGM_2D)
    {
      sourceVertex += "  vec3 posLocal = instancedPosition.w * (MInstante * vec3(vertexPosition,0.f));\n"
                      "  vec2 out_Position = instancedPosition.xy + posLocal.xy;\n"
                      "  gl_Position.xyz = MPVM * vec3(out_Position,1.f);\n"
                      "  gl_Position.z = 0.1f;\n"
                      "  gl_Position.w = 1.0f;\n";
      TRE_ASSERT(!m_layout.hasPIX_Position);
    }
    else
    {
      TRE_FATAL("invalid shader category");
    }
  }
  else if (m_layout.hasUNI_MPVM)
  {
    if (m_layout.category == PRGM_2D)
    {
      sourceVertex += "  gl_Position.xyz = MPVM * vec3(vertexPosition,1.f);\n"
                      "  gl_Position.z = 0.1f;\n"
                      "  gl_Position.w = 1.0f;\n";
      TRE_ASSERT(!m_layout.hasPIX_Position);
    }
    else if (m_layout.category == PRGM_2Dto3D)
    {
      sourceVertex += "  vec3 out_Position = vec3(vertexPosition,0.f);\n"
                      "  gl_Position = MPVM * vec4(out_Position,1.f);\n";
    }
    else if (m_layout.category == PRGM_3D || m_layout.category == PRGM_3D_DEPTH)
    {
      sourceVertex += "  vec3 out_Position = vertexPosition;\n"
                      "  gl_Position = MPVM * vec4(out_Position,1.f);\n";
    }
    else
    {
      TRE_FATAL("Wrong shader type");
    }
  }
  else
  {
    TRE_ASSERT(m_layout.category == PRGM_2D);
    TRE_ASSERT(!m_layout.hasPIX_Position_clipspace);
    sourceVertex += "  gl_Position = vec4(vertexPosition,0.1f,1.f);\n";
  }

  if (m_layout.hasPIX_Position)
  {
    if (m_layout.is2D()) sourceVertex += "  " + prefixOut + "Position = vec3(vertexPosition, 0.f);\n";
    else                 sourceVertex += "  " + prefixOut + "Position = (MModel * vec4(out_Position, 1.f)).xyz;\n";
  }

  if (m_layout.hasPIX_Position_clipspace)
    sourceVertex += "  " + prefixOut + "Position_clipspace = gl_Position.xyz / gl_Position.w;\n";

  if (m_layout.hasOPT_DepthOne)
    sourceVertex += "  gl_Position.z = gl_Position.w;\n";

  // -> normal & tangent

  if (m_layout.hasPIX_Normal)
  {
    if (m_layout.hasBUF_Normal)
    {
      if (m_layout.hasBUF_InstancedPosition)
      {
        TRE_ASSERT(m_layout.is3D() && m_layout.category != PRGM_2Dto3D);
        sourceVertex += "  " + prefixOut + "Normal = (MModel * vec4(MInstante * vertexNormal,0.f)).xyz;\n";
      }
      else
      {
        TRE_ASSERT(m_layout.is3D() && m_layout.category != PRGM_2Dto3D);
        sourceVertex += "  " + prefixOut + "Normal = (MModel * vec4(vertexNormal,0.f)).xyz;\n";
      }
    }
    else if (m_layout.hasBUF_InstancedPosition)
    {
      TRE_ASSERT(m_layout.is3D());
      sourceVertex += "  " + prefixOut + "Normal = (MModel * vec4(MInstante * vec3(0.f,0.f,1.f),0.f)).xyz;\n";
    }
    else
    {
      TRE_FATAL("not reached !");
    }
  }

  if (m_layout.hasBUF_TangentU)
  {
    TRE_ASSERT(m_layout.hasBUF_Normal);
    TRE_ASSERT(m_layout.hasPIX_Normal);
    TRE_ASSERT(m_layout.is3D() && m_layout.category != PRGM_2Dto3D);
    if (m_layout.hasBUF_InstancedPosition)
    {
      sourceVertex += "  " + prefixOut + "TangU = (MModel * vec4(MInstante * vertexTangentU.xyz,0.f)).xyz;\n"
                      "  " + prefixOut + "TangV = cross(" + prefixOut + "Normal," + prefixOut + "TangU) * vertexTangentU.w;\n";
    }
    else
    {
      sourceVertex += "  " + prefixOut + "TangU = (MModel * vec4(vertexTangentU.xyz,0.f)).xyz;\n"
                      "  " + prefixOut + "TangV = cross(" + prefixOut + "Normal," + prefixOut + "TangU) * vertexTangentU.w;\n";
    }
  }

  // -> color

  if (m_layout.hasBUF_Color || m_layout.hasBUF_InstancedColor)
  {
    sourceVertex += "  " + prefixOut + "Color = vec4(1.f);\n";
    if (m_layout.hasBUF_Color)          sourceVertex += "  " + prefixOut + "Color *= vertexColor;\n";
    if (m_layout.hasBUF_InstancedColor) sourceVertex += "  " + prefixOut + "Color *= instancedColor;\n";
  }

  // -> rest ...

  if (m_layout.hasBUF_UV) sourceVertex += "  " + prefixOut + "UV = vertexUV;\n";

  if (m_layout.hasBUF_InstancedAtlasBlend) sourceVertex += "  " + prefixOut + "AtlasBlend = instancedAtlasBlend;\n";

  if (m_layout.hasPIX_UVW) sourceVertex += "  " + prefixOut + "UVW = vertexPosition.xyz;\n";

  sourceVertex += "}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderSource_GeomWireframe(std::string & sourceGeom)
{
  sourceGeom.clear();

  TRE_ASSERT(m_layout.hasPIP_Geom);

  // header
#ifdef TRE_OPENGL_ES
  TRE_FATAL("Geometry shaders are not supported with OpenGL-ES 3.0");
  sourceGeom += "#version 300 es\nprecision mediump float;\n";
#else
  sourceGeom += "#version 330 core\n";
#endif

  // geometry definition
  sourceGeom += "layout(triangles) in;\n"
                "layout(line_strip, max_vertices = 4) out;\n";

  // layout

#define EMITDECL(tname, varname) sourceGeom += "in " tname "geom" varname ";\n" "out " tname "pixel" varname ";\n"

  if (m_layout.hasBUF_UV)
  {
    EMITDECL("vec2", "UV");
  }
  if (m_layout.hasBUF_Color)
  {
    EMITDECL("vec4", "Color");
  }
  if (m_layout.hasBUF_TangentU)
  {
    EMITDECL("vec3", "TangU");
    EMITDECL("vec3", "TangV");
  }
  if (m_layout.hasBUF_InstancedAtlasBlend)
  {
    EMITDECL("vec4", "AtlasBlend");
  }
  if (m_layout.hasPIX_UVW)
  {
    EMITDECL("vec3", "UVW");
  }
  if (m_layout.hasPIX_Position)
  {
    EMITDECL("vec3", "Position");
  }
  if (m_layout.hasPIX_Normal)
  {
    EMITDECL("vec3", "Normal");
  }
  if (m_layout.hasPIX_Position_clipspace)
  {
    EMITDECL("vec3", "Position_clipspace");
  }

#undef EMITDECL

  // emit

  sourceGeom += "void main()\n{\n";

#define EMITVAR(varname) \
  sourceGeom += "  pixel" varname " = gl_in["; \
  sourceGeom += ve; \
  sourceGeom += "].geom" varname ";\n"

  const char vEmit[4] = { '0', '1', '2', '0' };
  for (const char ve : vEmit)
  {
    sourceGeom += "  gl_Position = gl_in[";
    sourceGeom += ve;
    sourceGeom += "].gl_Position;\n";

    if (m_layout.hasBUF_UV)
    {
      EMITVAR("UV");
    }
    if (m_layout.hasBUF_Color)
    {
      EMITVAR("Color"); // note: if from instancing, we can do this only once.
    }
    if (m_layout.hasBUF_TangentU)
    {
      EMITVAR("TangU");
      EMITVAR("TangV");
    }
    if (m_layout.hasBUF_InstancedAtlasBlend)
    {
      EMITVAR("AtlasBlend"); // can be done only once
    }
    if (m_layout.hasPIX_UVW)
    {
      EMITVAR("UVW");
    }
    if (m_layout.hasPIX_Position)
    {
      EMITVAR("Position");
    }
    if (m_layout.hasPIX_Normal)
    {
      EMITVAR("Normal");
    }
    if (m_layout.hasPIX_Position_clipspace)
    {
      EMITVAR("Position_clipspace");
    }

    sourceGeom +="  EmitVertex();\n";
  }

#undef EMITVAR

  sourceGeom += "  EndPrimitive();\n}\n";
}

// ----------------------------------------------------------------------------

void shaderGenerator::createShaderSource_FragmentMain(const int flags, std::string &sourceFragment)
{
   // process
  createShaderFunction_Diffuse(flags, sourceFragment);
  createShaderFunction_Light(flags, sourceFragment);
  createShaderFunction_Transparent(flags, sourceFragment);

  sourceFragment += "void main()\n{\n";

  if (m_layout.category == PRGM_2D)
  {
    sourceFragment += "  vec4 diffuseColor = Diffuse();\n";
    sourceFragment += "  color.xyz = diffuseColor.xyz;\n"
                      "  color.w   = Transparent(diffuseColor.w);\n";
  }
  else if (m_layout.category == PRGM_3D || m_layout.category == PRGM_2Dto3D)
  {
    sourceFragment += "  vec4 diffuseColor = Diffuse();\n";
    sourceFragment += "  color.xyz = Light(diffuseColor.xyz);\n"
                      "  color.w   = Transparent(diffuseColor.w);\n";
  }
  else if (m_layout.category == PRGM_3D_DEPTH)
  {
    TRE_ASSERT(flags == 0);
  }
  else
  {
    TRE_LOG("bad flag value");
  }

  sourceFragment += "}\n";
}

// ============================================================================

} // namespace
