#include "tre_utils.h"

#include "tre_model.h"
#include "tre_shader.h"

#include <glm/gtx/extended_min_max.hpp>

namespace tre {

// ============================================================================

#ifdef TRE_PRINTS
#ifdef _WIN32

#define NOMINMAX
#include <windows.h>
#include <debugapi.h>

void debugPrint_Win32(const char *msg)
{
  OutputDebugStringA(msg);
}

#undef near
#undef far

#endif
#endif

// ============================================================================

bool IsOpenGLok(const char * msg)
{
  bool status = true;
  GLenum myglerror;
  while ( (myglerror = glGetError()) != GL_NO_ERROR )
  {
#ifdef TRE_PRINTS
    const char *errorTxt = " (unkown error code)";
    if      (myglerror==GL_INVALID_ENUM     ) errorTxt = " (GL_INVALID_ENUM)";
    else if (myglerror==GL_INVALID_VALUE    ) errorTxt = " (GL_INVALID_VALUE)";
    else if (myglerror==GL_INVALID_OPERATION) errorTxt = " (GL_INVALID_OPERATION)";
    else if (myglerror==GL_STACK_OVERFLOW   ) errorTxt = " (GL_STACK_OVERFLOW)";
    else if (myglerror==GL_STACK_UNDERFLOW  ) errorTxt = " (GL_STACK_UNDERFLOW)";
    else if (myglerror==GL_OUT_OF_MEMORY    ) errorTxt = " (GL_OUT_OF_MEMORY)";
    else if (myglerror==GL_INVALID_FRAMEBUFFER_OPERATION) errorTxt = " (GL_INVALID_FRAMEBUFFER_OPERATION)";
    if (msg!=nullptr) { TRE_LOG("IsOpenGlok: (" << msg << ") OpenGL Error code has been gerenated: " << myglerror << errorTxt); }
    else              { TRE_LOG("IsOpenGLok: OpenGL Error code has been gerenated: " << myglerror << errorTxt); }
#endif
    status = false;
  }
  return status;
}

// ============================================================================

void compute3DFrustumProjection(glm::mat4 &MP, float invratio, float fov, float near, float far)
{
  const float noverdy = tanf(float(M_PI * 0.5) - fov * 0.5f);
  // warning : column-major indexes
  MP[0] = glm::vec4( noverdy*invratio , 0.f     , 0.f                    ,  0.f  );
  MP[1] = glm::vec4( 0.f              , noverdy , 0.f                    ,  0.f  );
  MP[2] = glm::vec4( 0.f              , 0.f     , (far+near)/(near-far)  , -1.f  );
  MP[3] = glm::vec4( 0.f              , 0.f     , 2.f*near*far/(near-far),  0.f  );
}

// ----------------------------------------------------------------------------

void compute3DOrthoProjection(glm::mat4 &MP, float dx, float dy, float near, float far)
{
  // warning : column-major indexes
  MP[0] = glm::vec4( 1.f/dx , 0.f     , 0.f                   , 0.f  );
  MP[1] = glm::vec4( 0.f    , 1.f/dy  , 0.f                   , 0.f  );
  MP[2] = glm::vec4( 0.f    , 0.f     , 2.f/(near-far)        , 0.f  );
  MP[3] = glm::vec4( 0.f    , 0.f     , (far+near)/(near-far) , 1.f  );
}

// ----------------------------------------------------------------------------

void compute2DOrthoProjection(glm::mat3 & MP, const float invratio)
{
  // warning : column-major indexes
  MP[0] = glm::vec3( invratio , 0.f , 0.f );
  MP[1] = glm::vec3( 0.f      , 1.f , 0.f );
  MP[2] = glm::vec3( 0.f      , 0.f , 1.f );
}

// ============================================================================

float fastAtan2(const float y, const float x)
{
  const float ax = fabsf(x) + 1e-10f; // prevent (0,0) condition
  const float ay = fabsf(y);
  const float mx = fmaxf(ay, ax);
  const float mn = fminf(ay, ax);
  const float z = mn / mx;
  /* approximation of atan(a) on [0,1] */
  //float alpha = z;
  float alpha = (-0.19194795f * z * z + 0.97239411f) * z;
  /* Map to full circle */
  if (ay > ax) alpha = 1.57079637f - alpha;
  if (x < 0.f) alpha = 3.14159274f - alpha;
  if (y < 0.f) alpha = -alpha;
  return alpha;
}

// ============================================================================

s_boundbox s_boundbox::transform(const glm::mat4 &transform) const
{
  const glm::vec3 center = 0.5f * (m_min + m_max);
  const glm::vec3 centerTr = transform * glm::vec4(center, 1.f);
  const glm::vec3 extend = 0.5f * (m_max - m_min);
  const glm::vec3 extendTr_x_abs = glm::abs(extend.x * transform[0]);
  const glm::vec3 extendTr_y_abs = glm::abs(extend.y * transform[1]);
  const glm::vec3 extendTr_z_abs = glm::abs(extend.z * transform[2]);
  const glm::vec3 extendTr = extendTr_x_abs + extendTr_y_abs + extendTr_z_abs;
  return s_boundbox(centerTr - extendTr, centerTr + extendTr);
}

// ----------------------------------------------------------------------------

s_boundbox & s_boundbox::operator+=(const s_boundbox &other)
{
  m_min = glm::min(m_min, other.m_min);
  m_max = glm::max(m_max, other.m_max);
  return *this;
}

// ----------------------------------------------------------------------------

s_boundbox s_boundbox::operator+(const s_boundbox &other) const
{
  s_boundbox result(*this);
  result += other;
  return result;
}

// ----------------------------------------------------------------------------

s_boundbox & s_boundbox::operator*=(const float scale)
{
  m_min *= scale;
  m_max *= scale;
  return *this;
}

// ----------------------------------------------------------------------------

void s_boundbox::addPointInBox(const glm::vec3 &pt)
{
  m_min = glm::min(m_min, pt);
  m_max = glm::max(m_max, pt);
}

// ----------------------------------------------------------------------------

void s_boundbox::read(std::istream & inbuffer)
{
  float buf[6];
  inbuffer.read(reinterpret_cast<char*>(&buf), sizeof(buf));
  m_min.x = buf[0];
  m_max.x = buf[1];
  m_min.y = buf[2];
  m_max.y = buf[3];
  m_min.z = buf[4];
  m_max.z = buf[5];
}

// ----------------------------------------------------------------------------

void s_boundbox::write(std::ostream & outbuffer) const
{
  const float buf[6] = { m_min.x, m_max.x, m_min.y, m_max.y, m_min.z, m_max.z };
  outbuffer.write(reinterpret_cast<const char*>(&buf), sizeof(buf));
}

// ============================================================================

#define EPSILON 1.e-6f

bool triangleRaytrace3D(const glm::vec3 &v00, const glm::vec3 &v01, const glm::vec3 &v10, const glm::vec3 &origin, const glm::vec3 &direction, glm::vec3 * coordUVT)
{
  const glm::vec3 edge1 = v01 - v00;
  const glm::vec3 edge2 = v10 - v00;
  const glm::vec3 pvec = glm::cross(direction, edge2);
  // get determinant of 3*3 matrix of the system: det( [-dir,edge1,edge2] )  = (dir*edge2).edge1
  const GLfloat det = glm::dot(pvec, edge1);
  if (det>-EPSILON && det<EPSILON) return false; // parallel ray and triangle
  const GLfloat invdet = 1.f/det; // needed because we don't know the sign of det.
  // get the right-hand side member: AP
  const glm::vec3 AP = origin - v00;
  // solve the system: get the "u"
  const GLfloat coordu = invdet * glm::dot(pvec, AP); // u = (dir*edge2).AP / det
  if (coordu < 0.f || coordu > 1.f) return false; // out-of-bound
  // solve the system: get the "v"
  const glm::vec3 qvec = glm::cross(AP, edge1);
  const GLfloat coordv = invdet * glm::dot(direction, qvec); // v = (AP*edge1).dir / det
  if (coordv < 0.f || (coordu+coordv) > 1.f) return false; // out-of-bound
  // solve the system: get the "t"
  const GLfloat coordt = invdet * glm::dot(edge2, qvec); // t = (AP*edge1).edge2 / det
  if (coordt < EPSILON ) return false;
  // we have a shot !
  if (coordUVT != nullptr) *coordUVT = glm::vec3(coordu, coordv, coordt);
  return true;
}

float triangleProject3D(const glm::vec3 &v00, const glm::vec3 &v01, const glm::vec3 &v10, const glm::vec3 &origin, const glm::vec3 &direction)
{
  const glm::vec3 edge1 = v01 - v00;
  const glm::vec3 edge2 = v10 - v00;
  const glm::vec3 pvec = glm::cross(direction, edge2);
  // get determinant of 3*3 matrix of the system: det( [-dir,edge1,edge2] )  = (dir*edge2).edge1
  const GLfloat det = glm::dot(pvec, edge1);
  if (det>-EPSILON && det<EPSILON) return std::numeric_limits<float>::infinity(); // parallel ray and triangle
  const GLfloat invdet = 1.f/det; // needed because we don't know the sign of det.
  // get the right-hand side member: AP
  const glm::vec3 AP = origin - v00;
  // solve the system: get the "u"
  const GLfloat coordu = invdet * glm::dot(pvec, AP); // u = (dir*edge2).AP / det
  if (coordu < 0.f || coordu > 1.f) return std::numeric_limits<float>::infinity(); // out-of-bound
  // solve the system: get the "v"
  const glm::vec3 qvec = glm::cross(AP, edge1);
  const GLfloat coordv = invdet * glm::dot(direction, qvec); // v = (AP*edge1).dir / det
  if (coordv < 0.f || (coordu+coordv) > 1.f) return std::numeric_limits<float>::infinity(); // out-of-bound
  // solve the system: get the "t"
  const GLfloat coordt = invdet * glm::dot(edge2, qvec); // t = (AP*edge1).edge2 / det
  return coordt;
}

// ----------------------------------------------------------------------------

void triangleQuality(const glm::vec3 &vA, const glm::vec3 &vB, const glm::vec3 &vC, float *area, float *quality)
{
  TRE_ASSERT(area != nullptr || quality != nullptr);

  const glm::vec3 edge01 = vB - vA;
  const glm::vec3 edge02 = vC - vA;

  const glm::vec3 normal = 0.5f * glm::cross(edge01, edge02);
  const float triArea2 = glm::dot(normal, normal);
  if (triArea2 == 0.f) // quick exit
  {
    if (area    != nullptr) *area = 0.f;
    if (quality != nullptr) *quality = 0.f;
    return;
  }

  if (area != nullptr)
  {
    *area = sqrtf(triArea2);
  }
  if (quality != nullptr)
  {
    const glm::vec3 edge12 = vC - vB;

    const float edge01length = glm::length(edge01);
    const float edge02length = glm::length(edge02);
    const float edge12length = glm::length(edge12);

    // triQualityInv = R / r, where R is the radius of the circumscribed circle, r is the radius of the inscribed circle.
    // triQualityInv is in range [2, +inf[, 2 is reached for an equilateral triangle.
    // R = edge01length * edge02length * edge12length / 4 Aera
    // r = Area / p (where p is the half-perimeter)
    const float triQualityInv = edge01length * edge02length * edge12length * (edge01length + edge02length + edge12length) * 0.5f / (4.f * triArea2);

    TRE_ASSERT(triQualityInv >= 2.f - 1.e-5f); // float-point precision

    *quality = (triQualityInv < 2.f) ? 1.f : 2.f/triQualityInv;
  }
}

// ----------------------------------------------------------------------------

void tetrahedronQuality(const glm::vec3 &vA, const glm::vec3 &vB, const glm::vec3 &vC, const glm::vec3 &vD, float *volume, float *quality)
{
  TRE_ASSERT(volume != nullptr || quality != nullptr);

  const glm::vec3 eAB = vB - vA;
  const glm::vec3 eAC = vC - vA;
  const glm::vec3 eAD = vD - vA;

  const float vol = glm::abs(glm::dot(eAB, glm::cross(eAC, eAD))) / 6.f;

  if (volume != nullptr)
  {
    *volume = vol;
  }

  if (quality != nullptr)
  {
    if (vol == 0.f)
    {
      *quality = 0.f;
    }
    else
    {
      // tetraQualityInv = R / r, where R is the radius of the circumscribed circle, r is the radius of the inscribed circle.
      // tetraQualityInv is in range [3, +inf[, 3 is reached for a regular tetrahedron.
      // R = sqrt( ( lAD * lBC  + lBD * lAC + lCD * lAB) * (lAD * lBC  + lBD * lAC - lCD * lAB) *
      //           (-lAD * lBC  + lBD * lAC + lCD * lAB) * (lAD * lBC  - lBD * lAC + lCD * lAB) ) / (24 volume) where lAB is the lenght of edge AB, ...
      // r = 3 volume / (W1 + W2 + W3 + W4) where Wi is the area of face 'i'.

      const glm::vec3 eBC = vC - vB;
      const glm::vec3 eBD = vD - vB;

      const float lAB = glm::length(eAB);
      const float lAC = glm::length(eAC);
      const float lAD = glm::length(eAD);
      const float lBC = glm::length(eBC);
      const float lBD = glm::length(eBD);
      const float lCD = glm::length(vD - vC);
      const float Rsquared_p1 = ( lAD * lBC  + lBD * lAC + lCD * lAB) * (lAD * lBC  + lBD * lAC - lCD * lAB) / (24.f * vol);
      const float Rsquared_p2 = (-lAD * lBC  + lBD * lAC + lCD * lAB) * (lAD * lBC  - lBD * lAC + lCD * lAB) / (24.f * vol);
      const float R = sqrtf(fabsf(Rsquared_p1 * Rsquared_p2));

      const float W1 = 0.5f * glm::length(glm::cross(eAB, eAC));
      const float W2 = 0.5f * glm::length(glm::cross(eAB, eAD));
      const float W3 = 0.5f * glm::length(glm::cross(eAC, eAD));
      const float W4 = 0.5f * glm::length(glm::cross(eBC, eBD));
      const float r = 3.f * vol / (W1 + W2 + W3 + W4);

      const float tetraQualityInv = R / r;

      TRE_ASSERT(tetraQualityInv >= 3.f - 1.e-5f); // float-point precision

      *quality = (tetraQualityInv < 3.f) ? 1.f : 3.f/tetraQualityInv;
    }
  }
}

// ============================================================================

void sortAndUniqueCounting(std::vector<uint> &array)
{
  if (array.empty()) return;
  uint vmin = array[0];
  uint vmax = array[0];
  for (uint v : array)
  {
    vmin = std::min(vmin, v);
    vmax = std::max(vmax, v);
  }

  if (vmax == vmin)
  {
    array.resize(1);
    array[0] = vmax;
    return;
  }

  std::vector<bool> key(vmax - vmin + 1, false);

  for (uint v : array) key[v - vmin] = true;

  uint ikey = vmin;
  uint outValue = 0;
  for (bool b : key)
  {
    if (b)
    {
      array[outValue++] = ikey;
    }
    ++ikey;
  }
  TRE_ASSERT(outValue <= array.size());
  TRE_ASSERT(ikey == vmax + 1);
  array.resize(outValue);
}

// ----------------------------------------------------------------------------

void sortAndUniqueBull(std::vector<uint> &array)
{
  if (array.empty()) return;

  for (uint ip = 0; ip < array.size(); ++ip)
  {
    for (uint ik = ip + 1; ik < array.size(); ++ik)
    {
      if (array[ip] == array[ik]) // remove duplicate
      {
        array[ik--] = array.back();
        array.pop_back();
      }
      else if (array[ip] > array[ik]) // swap 2 values
      {
        std::swap(array[ip], array[ik]);
      }
    }
  }
}

// ============================================================================

bool checkLayoutMatch_Shader_Model(shader *pshader, model *pmodel)
{
  const shader::s_layout  &shaderL = pshader->layout();
  const s_modelDataLayout &modelL = pmodel->layout();
  bool  result = true;

  const std::string modelName = pmodel->partCount() == 0 ? "no-part" : "part0=" + pmodel->partInfo(0).m_name;
  const std::string msgPrefix = "model (" + modelName + ") cannot be rendered by the shader (" + pshader->getName() + "): ";

  if (shaderL.category == shader::PRGM_2D || shaderL.category == shader::PRGM_2Dto3D)
  {
    if (modelL.m_positions.m_size < 2)
    {
      TRE_LOG(msgPrefix << "mismatch on 2D-position buffer");
      result = false;
    }
  }

  if (shaderL.category == shader::PRGM_3D || shaderL.category == shader::PRGM_3D_DEPTH)
  {
    if (modelL.m_positions.m_size < 3)
    {
      TRE_LOG(msgPrefix << "mismatch on 3D-position buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_Normal)
  {
    if (modelL.m_normals.m_size < 3)
    {
      TRE_LOG(msgPrefix << "mismatch on normal buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_TangentU)
  {
    if (modelL.m_tangents.m_size < 4)
    {
      TRE_LOG(msgPrefix << "mismatch on tangent buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_UV)
  {
    if (modelL.m_uvs.m_size < 2)
    {
      TRE_LOG(msgPrefix << "mismatch on uv buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_Color)
  {
    if (modelL.m_colors.m_size < 4)
    {
      TRE_LOG(msgPrefix << "mismatch on color buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_InstancedPosition)
  {
    if (modelL.m_instancedPositions.m_size < 3 && (shaderL.category == shader::PRGM_3D || shaderL.category == shader::PRGM_3D_DEPTH))
    {
      TRE_LOG(msgPrefix << "mismatch on position instance buffer");
      result = false;
    }
    if (modelL.m_instancedPositions.m_size < 2 && (shaderL.category == shader::PRGM_2D || shaderL.category == shader::PRGM_2Dto3D))
    {
      TRE_LOG(msgPrefix << "mismatch on position instance buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_InstancedOrientation)
  {
    if (modelL.m_instancedOrientations.m_size < 12)
    {
      TRE_LOG(msgPrefix << "mismatch on orientation instance buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_InstancedAtlasBlend)
  {
    if (modelL.m_instancedAtlasBlends.m_size < 4)
    {
      TRE_LOG(msgPrefix << "mismatch on blend-atals instance buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_InstancedColor)
  {
    if (modelL.m_instancedColors.m_size < 4)
    {
      TRE_LOG(msgPrefix << "mismatch on color instance buffer");
      result = false;
    }
  }

  if (shaderL.hasBUF_InstancedRotation)
  {
    if (modelL.m_instancedRotations.m_size < 1)
    {
      TRE_LOG(msgPrefix << "mismatch on rotation instance buffer");
      result = false;
    }
  }

  return result;
}

// ============================================================================

} // namespace
