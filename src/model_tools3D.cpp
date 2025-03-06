#include "tre_model_tools.h"

#include "tre_model.h"
#include "tre_contact_3D.h"

#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/norm.hpp> // for glm::length2()

//#define MESH_DEBUG // Mesh debugging
#ifdef MESH_DEBUG
#include <fstream>
#include <string>

static std::ostream& operator<<(std::ostream& out, const glm::vec3 &pt)
{
  out << pt.x << ' ' <<  pt.y << ' ' << pt.z;
  return out;
}

#define osprinti3(_i) (_i < 100 ? "0" : "") << (_i < 10 ? "0" : "") << _i

#endif // MESH_DEBUG

#include <algorithm> // for std::find

namespace tre {

// == Helpers =================================================================

static void _computeConnectivity_vert2tri(const s_modelDataLayout &data, std::size_t offset, std::size_t count, std::vector<std::vector<uint> > &vertexToTriangles)
{
  TRE_ASSERT(data.m_indexCount > 0);
  vertexToTriangles.clear();
  vertexToTriangles.resize(data.m_vertexCount);
  const std::size_t istop = offset + count;
  for (std::size_t i = offset, iT = 0; i < istop; i+=3, iT++)
  {
    vertexToTriangles[data.m_index[i + 0]].push_back(iT);
    vertexToTriangles[data.m_index[i + 1]].push_back(iT);
    vertexToTriangles[data.m_index[i + 2]].push_back(iT);
  }
}

// ----------------------------------------------------------------------------

struct s_starCollapser
{
  struct s_pt
  {
    float x,y,z; uint ind;
    s_pt() : x(0.f), y(0.f), z(0.f), ind(0) {}
    s_pt(const glm::vec3 &p, uint i) : x(p.x), y(p.y), z(p.z), ind(i) {}
    glm::vec3 operator-(const s_pt &other) const { return glm::vec3(other.x - x, other.y - y, other.z - z); }
  };
  s_pt              center;
  std::size_t       ntri;
  std::vector<s_pt> edges;
  std::vector<s_pt> belt;

  void init(const s_modelDataLayout &layout, const uint indCenter, const std::size_t triangleCount)
  {
    center = s_pt(layout.m_positions.get<glm::vec3>(indCenter), indCenter);
    ntri = triangleCount;
    edges.clear();
    edges.reserve(2 * triangleCount);
    belt.clear();
  }

  bool addTri(const s_modelDataLayout &layout, uint ai, uint bi, uint ci)
  {
    ai = layout.m_index[ai];
    bi = layout.m_index[bi];
    ci = layout.m_index[ci];
    if ((ai == bi) || (ai == ci) || (bi == ci)) return false;
    const bool ax = (ai == center.ind);
    const bool bx = (bi == center.ind);
    const bool cx = (ci == center.ind);
    const glm::vec3 &av = layout.m_positions.get<glm::vec3>(ai);
    const glm::vec3 &bv = layout.m_positions.get<glm::vec3>(bi);
    const glm::vec3 &cv = layout.m_positions.get<glm::vec3>(ci);
    if (ax + bx + cx != 1) return false;
    if (ax) { edges.emplace_back(bv, bi); edges.emplace_back(cv, ci); }
    if (bx) { edges.emplace_back(cv, ci); edges.emplace_back(av, ai); }
    if (cx) { edges.emplace_back(av, ai); edges.emplace_back(bv, bi); }
    return true;
  }

  bool computeClosedBelt()
  {
    TRE_ASSERT(ntri * 2 == edges.size());
    if (edges.size() / 2 < 3) return false;
    belt.reserve(ntri);
    // first
    belt.push_back(edges[0]);
    belt.push_back(edges[1]);
    edges[1] = edges.back(); edges.pop_back();
    edges[0] = edges.back(); edges.pop_back();
    // other
    for (std::size_t is = 1; is < ntri; ++is)
    {
      for (std::size_t ic = 0; ic < edges.size() / 2; ++ic)
      {
        const uint indB1 = edges[ic * 2 + 0].ind;
        const uint indB2 = edges[ic * 2 + 1].ind;
        if (indB1 == belt.back().ind)
        {
          belt.push_back(edges[ic * 2 + 1]);
          edges[ic * 2 + 1] = edges.back(); edges.pop_back();
          edges[ic * 2 + 0] = edges.back(); edges.pop_back();
          break;
        }
        else if (indB2 == belt.back().ind)
        {
          TRE_LOG("s_starCollapser: wrong index order. It may produce bad result. The input mesh probably does not follow the clockwise index ordering for the triangles.");
          belt.push_back(edges[ic * 2 + 0]);
          edges[ic * 2 + 1] = edges.back(); edges.pop_back();
          edges[ic * 2 + 0] = edges.back(); edges.pop_back();
          break;
        }
      }
    }
    if (belt.size() != ntri + 1 || belt.front().ind != belt.back().ind) { belt.clear(); return false; } // the contour is not closed.
    belt.pop_back();
    return true;
  }

  float computeMeanCurvature() const
  {
    TRE_ASSERT(belt.size() == ntri && edges.empty());
    // Using the discrete Laplace operator: dP{i} = MeanCurvature{i} Normal{i}
    //                                            = 1/(Area/3) sum_{j}[ (cot(angleOppTriA_ij) + cot(angleOppTriB_ij))/2 (p_j-p_i) ]
    float     area = 0.f;
    glm::vec3 dP = glm::vec3(0.f);
    for (std::size_t j = 0; j < ntri; ++j)
    {
      const s_pt &ptA = belt[j != 0 ? j - 1 : ntri - 1];
      const s_pt &ptB = belt[j];
      // triangle <center, A, B>
      const glm::vec3 edgeCA = ptA - center;
      const glm::vec3 edgeCB = ptB - center;
      const glm::vec3 edgeAB = ptB - ptA;
      const float triArea = 0.5f * std::abs(glm::length(glm::cross(edgeCA, edgeCB)));
      if (triArea < 1.e-8f) return std::numeric_limits<float>::infinity();
      area +=triArea;
      const glm::vec3 edgeCA_n = glm::normalize(edgeCA);
      const glm::vec3 edgeCB_n = glm::normalize(edgeCB);
      const glm::vec3 edgeAB_n = glm::normalize(edgeAB);
      const float cosAngleA = glm::dot(edgeAB_n, -edgeCA_n);
      const float sinAngleA = glm::length(glm::cross(edgeAB_n, -edgeCA_n)); // or sqrt(1-cos**2) ?
      const float cosAngleB = glm::dot(edgeAB_n, edgeCB_n);
      const float sinAngleB = glm::length(glm::cross(edgeAB_n, edgeCB_n));
      dP += 0.5f * (cosAngleA/sinAngleA) * edgeCB;
      dP += 0.5f * (cosAngleB/sinAngleB) * edgeCA;
    }
    dP = dP * (1.f / area);
    const float ret = glm::length(dP);
    return ret;
  }

  bool collapse(std::vector<uint> &outIndex)
  {
    TRE_ASSERT(belt.size() == ntri && edges.empty());
    outIndex.clear();
    outIndex.reserve(3 * ntri);
    // 2D-triangulation
    // -> find the plane's normal and project
    glm::vec3 normal = glm::vec3(0.f);
    for (std::size_t j = 0; j < ntri; ++j)
    {
      const s_pt &ptA = belt[j != 0 ? j - 1 : ntri - 1];
      const s_pt &ptB = belt[j];
      const glm::vec3 edgeCA = ptA - center;
      const glm::vec3 edgeCB = ptB - center;
      normal += glm::cross(edgeCA, edgeCB);
    }
    normal = glm::normalize(normal);
    const glm::vec3 tangU = glm::normalize(glm::cross(normal, glm::vec3(belt[0].x - center.x, belt[0].y - center.y, belt[0].z - center.z)));
    const glm::vec3 tangV = glm::normalize(glm::cross(normal, tangU));
    if (glm::any(glm::isnan(tangU)) || glm::any(glm::isnan(tangV))) return false;
    std::vector<glm::vec2> ptsRing2D(ntri);
    for (std::size_t j = 0; j < ntri; ++j)
    {
      const glm::vec3 edgeCP = belt[j] - center;
      ptsRing2D[j] = glm::vec2(glm::dot(edgeCP, tangU), glm::dot(edgeCP, tangV));
    }
    // -> run the triangulation
    modelTools::triangulate(ptsRing2D, outIndex);
    if (outIndex.size() != 3 * (ntri - 2)) return false;
    // -> check
#ifdef TRE_DEBUG
    for (std::size_t iT = 0; iT < ntri - 2; ++iT)
    {
      const s_pt &pt0 = belt[outIndex[iT * 3 + 0]];
      const s_pt &pt1 = belt[outIndex[iT * 3 + 1]];
      const s_pt &pt2 = belt[outIndex[iT * 3 + 2]];
      const glm::vec3 edge01 = glm::normalize(pt1 - pt0);
      const glm::vec3 edge02 = glm::normalize(pt2 - pt0);
      const float normalAlignCos = glm::dot(glm::cross(edge01, edge02), normal);
      TRE_ASSERT(normalAlignCos > 0.f);
    }
#endif
    // -> end
    for (uint &ind : outIndex) ind = belt[ind].ind;
    return true;
    // naive (and invalid !)
    for (std::size_t ib = 0; ib < belt.size() - 2; ++ib)
    {
      const GLuint indA = belt[0].ind;
      const GLuint indB = belt[ib + 1].ind;
      const GLuint indC = belt[ib + 2].ind;
      outIndex.push_back(indA);
      outIndex.push_back(indB);
      outIndex.push_back(indC);
    }
    return true;
  }
};

// == Model Tools =============================================================

namespace modelTools {

// ============================================================================

glm::vec4 computeBarycenter3D(const s_modelDataLayout &layout, const s_partInfo &part)
{
  TRE_ASSERT(layout.m_vertexCount > 0);
  TRE_ASSERT(layout.m_positions.m_size == 3);

  const std::size_t count = part.m_size;
  if (count == 0) return glm::vec4(0.f);
  TRE_ASSERT(count % 3 == 0);
  const std::size_t offset = part.m_offset;
  const std::size_t end = offset + count;

  glm::vec3 pt = glm::vec3(0.f);
  float     volume = 0.f;

  // Sum of the signed volume of all tetrahedrons formed by each (oriented) surface and the origin: (Origin, PA_Surf, PB_Surf, PC_Surf).

  if (layout.m_indexCount == 0)
  {
    for (std::size_t ipt = offset; ipt < end; ipt += 3)
    {
      const glm::vec3 ptA = layout.m_positions.get<glm::vec3>(ipt + 0);
      const glm::vec3 ptB = layout.m_positions.get<glm::vec3>(ipt + 1);
      const glm::vec3 ptC = layout.m_positions.get<glm::vec3>(ipt + 2);

      const float v = glm::dot(glm::cross(ptB, ptC), ptA);
      volume += v;
      pt += v * (ptA + ptB + ptC) / 4.f;
    }
  }
  else
  {
    for (std::size_t ipt = offset; ipt < end; ipt += 3)
    {
      const glm::vec3 ptA = layout.m_positions.get<glm::vec3>(layout.m_index[ipt + 0]);
      const glm::vec3 ptB = layout.m_positions.get<glm::vec3>(layout.m_index[ipt + 1]);
      const glm::vec3 ptC = layout.m_positions.get<glm::vec3>(layout.m_index[ipt + 2]);

      const float v = glm::dot(glm::cross(ptB, ptC), ptA);
      volume += v;
      pt += v * (ptA + ptB + ptC) / 4.f;
    }
  }

  if (fabsf(volume) < 1.e-10f) return glm::vec4(0.f);

  pt /= volume;
  volume = fabsf(volume);

  return glm::vec4(pt, volume);
}

// ============================================================================

void computeConvexeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold, std::vector<glm::vec3> &outSkinTri)
{
  TRE_ASSERT(layout.m_vertexCount > 0);
  TRE_ASSERT(layout.m_positions.m_size == 3);

  outSkinTri.clear();

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  const std::size_t end = offset + count;

  const glm::vec3 boxExtend = part.m_bbox.extend();
  const float radiusThreshold = threshold * fmaxf(boxExtend.x, boxExtend.y);
  const float thresholdSquared = radiusThreshold * radiusThreshold;

  // extract points
  std::vector<glm::vec3> points;
  points.resize(count);
  if (layout.m_indexCount == 0)
  {
    for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
    {
      points[j] = layout.m_positions.get<glm::vec3>(ipt);
    }
  }
  else
  {
    for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
    {
      points[j] = layout.m_positions.get<glm::vec3>(layout.m_index[ipt]);
    }
  }

  // divide and conquere ...

  TRE_FATAL("not implemented");
}

// ============================================================================

void computeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<glm::vec3> &outSkinTri)
{
  TRE_ASSERT(layout.m_vertexCount > 0);
  TRE_ASSERT(layout.m_positions.m_size == 3);

  outSkinTri.clear();

  if (part.m_size == 0) return;
  TRE_ASSERT(part.m_size % 3 == 0);

  outSkinTri.resize(part.m_size);
  glm::vec3 *outSkinPtr = outSkinTri.data();

  TRE_ASSERT(layout.m_indexCount > 0); // only the "indexed" version is implemented ...

  if (layout.m_normals.m_size == 3) //
  {
    for (std::size_t iT = 0; iT < part.m_size; iT += 3)
    {
      const uint ind0 = layout.m_index[part.m_offset + iT + 0];
      const uint ind1 = layout.m_index[part.m_offset + iT + 1];
      const uint ind2 = layout.m_index[part.m_offset + iT + 2];
      const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(ind0);
      const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(ind1);
      const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(ind2);
      const glm::vec3 n0 = layout.m_normals.get<glm::vec3>(ind0);
      const glm::vec3 nL = glm::cross(pt1 - pt0, pt2 - pt0);
      if (glm::dot(nL, nL) == 0.f)
      {
        TRE_LOG("computeSkin3D: the model has degenerated triangle (indices " << ind0 << ", " << ind1 << ", " << ind2 << ")");
        continue;
      }
      const bool      nSide = glm::dot(n0, nL) >= 0.f;
      *outSkinPtr++ = pt0;
      *outSkinPtr++ = nSide ? pt1 : pt2;
      *outSkinPtr++ = nSide ? pt2 : pt1;
    }
  }
  else
  {
    for (std::size_t iT = 0; iT < part.m_size; iT += 3)
    {
      const uint ind0 = layout.m_index[part.m_offset + iT + 0];
      const uint ind1 = layout.m_index[part.m_offset + iT + 1];
      const uint ind2 = layout.m_index[part.m_offset + iT + 2];
      const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(ind0);
      const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(ind1);
      const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(ind2);
      const glm::vec3 nL = glm::cross(pt1 - pt0, pt2 - pt0);
      if (glm::dot(nL, nL) == 0.f)
      {
        TRE_LOG("computeSkin3D: the model has degenerated triangle (indices " << ind0 << ", " << ind1 << ", " << ind2 << ")");
        continue;
      }
      *outSkinPtr++ = pt0;
      *outSkinPtr++ = pt1;
      *outSkinPtr++ = pt2;
    }
  }

  outSkinTri.resize(std::size_t(outSkinPtr - outSkinTri.data()));
}

// ============================================================================

void computeOutNormal(const s_modelDataLayout &layout, const s_partInfo &part, const bool clockwisedTriangles)
{
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  TRE_ASSERT(count % 3 == 0);

  // compute data per triangle
  std::vector<glm::vec3> normalPerTri;

  const std::size_t nTriangles = count / 3;
  normalPerTri.resize(nTriangles);

  {
    if (layout.m_indexCount > 0)
    {
      for (std::size_t i = offset, Eind = offset + count, iT = 0; i < Eind; i += 3, ++iT)
      {
        const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(layout.m_index[i    ]);
        const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(layout.m_index[i + 1]);
        const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(layout.m_index[i + 2]);
        normalPerTri[iT] = glm::cross(pt1 - pt0, pt2 - pt0);
      }
    }
    else
    {
      for (std::size_t ind = offset, Eind = offset + count, iT = 0; ind < Eind; ind += 3, ++iT)
      {
        const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(ind    );
        const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(ind + 1);
        const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(ind + 2);
        normalPerTri[iT] = glm::cross(pt1 - pt0, pt2 - pt0);
      }
    }
  }

  if (!clockwisedTriangles)
  {
    // oriente normals (per triangle)
    TRE_FATAL("Not implemented.");
  }

  // finally, synthetize normals
  if (layout.m_indexCount == 0)
  {
    TRE_ASSERT(offset + count <= layout.m_vertexCount);
    s_modelDataLayout::s_vertexData::iterator<glm::vec3> normalIt = layout.m_normals.begin<glm::vec3>(offset);
    for (std::size_t i = 0, iT = 0; i < count; i += 3, ++iT)
    {
      const glm::vec3 n = glm::normalize(normalPerTri[iT]);
      *normalIt++ = n;
      *normalIt++ = n;
      *normalIt++ = n;
    }
  }
  else
  {
    TRE_ASSERT(offset + count <= layout.m_indexCount);
    std::vector<std::vector<uint> > connectivity;
    _computeConnectivity_vert2tri(layout, offset, count, connectivity);
    for (std::size_t iV = 0; iV < layout.m_vertexCount; ++iV)
    {
      if (connectivity[iV].empty()) continue;
      glm::vec3 normal = glm::vec3(0.f);
      for (const uint tri : connectivity[iV]) normal += normalPerTri[tri]; // for each vertex, the normal is the weigthed normal of all neighbor triangles
      layout.m_normals.get<glm::vec3>(iV) = glm::normalize(normal);
    }
  }
}

// ============================================================================

void computeTangentFromUV(const s_modelDataLayout &layout, const s_partInfo &part)
{
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  TRE_ASSERT(count % 3 == 0);

  std::vector<glm::vec3> tanU(layout.m_vertexCount, glm::vec3(0.f));
  std::vector<glm::vec3> tanV(layout.m_vertexCount, glm::vec3(0.f));
  std::vector<bool>      inside(layout.m_vertexCount, false);

  if (layout.m_indexCount == 0)
  {
    TRE_ASSERT(offset + count <= layout.m_vertexCount);

    // Loop over triangle
    const std::size_t Sver = offset;
    const std::size_t Ever = offset + count;
    for (std::size_t Iver = Sver; Iver < Ever; Iver += 3)
    {
      const glm::vec3 eC = layout.m_positions.get<glm::vec3>(Iver + 1) - layout.m_positions.get<glm::vec3>(Iver);
      const glm::vec3 eD = layout.m_positions.get<glm::vec3>(Iver + 2) - layout.m_positions.get<glm::vec3>(Iver);

      const glm::vec2 uC = layout.m_uvs.get<glm::vec2>(Iver + 1) - layout.m_uvs.get<glm::vec2>(Iver);
      const glm::vec2 uD = layout.m_uvs.get<glm::vec2>(Iver + 2) - layout.m_uvs.get<glm::vec2>(Iver);

      const GLfloat det = 1.f / (uC.x * uD.y - uC.y * uD.x);

      // local tangent "r,s" along "u,v" parametric-parameter
      const glm::vec3 r = ( uD.y * eC - uC.y * eD ) * det;
      const glm::vec3 s = ( uC.x * eD - uD.x * eC ) * det;

      inside[Iver] = true; inside[Iver + 1] = true; inside[Iver + 2] = true;
      tanU[Iver] = r; tanU[Iver + 1] = r; tanU[Iver + 2] = r;
      tanV[Iver] = s; tanV[Iver + 1] = s; tanV[Iver + 2] = s;
    }
  }
  else
  {
    TRE_ASSERT(offset + count <= layout.m_indexCount);

    // Loop over triangle
    const std::size_t Sind = offset;
    const std::size_t Eind = offset + count;
    for (std::size_t Iind = Sind; Iind < Eind; Iind += 3)
    {
      const GLuint i0 = layout.m_index[Iind    ];
      const GLuint i1 = layout.m_index[Iind + 1];
      const GLuint i2 = layout.m_index[Iind + 2];

      const glm::vec3 eC = layout.m_positions.get<glm::vec3>(i1) - layout.m_positions.get<glm::vec3>(i0);
      const glm::vec3 eD = layout.m_positions.get<glm::vec3>(i2) - layout.m_positions.get<glm::vec3>(i0);

      const glm::vec2 uC = layout.m_uvs.get<glm::vec2>(i1) - layout.m_uvs.get<glm::vec2>(i0);
      const glm::vec2 uD = layout.m_uvs.get<glm::vec2>(i2) - layout.m_uvs.get<glm::vec2>(i0);

      const GLfloat det = 1.f / (uC.x * uD.y - uC.y * uD.x);

      // local tangent "r,s" along "u,v" parametric-parameter
      const glm::vec3 r = ( uD.y * eC - uC.y * eD ) * det;
      const glm::vec3 s = ( uC.x * eD - uD.x * eC ) * det;

      inside[i0] = true; inside[i1] = true; inside[i2] = true;
      tanU[i0] += r; tanU[i1] += r; tanU[i2] += r;
      tanV[i0] += s; tanV[i1] += s; tanV[i2] += s;
    }
  }

  // assign tangents
  for (std::size_t iver = 0; iver < layout.m_vertexCount; ++iver)
  {
    if (!inside[iver]) continue;
    const glm::vec3 normal = layout.m_normals.get<glm::vec3>(iver);
    const glm::vec3 tangent = glm::normalize(tanU[iver] - normal * glm::dot(tanU[iver],normal));
    // handness
    const float handness = glm::dot( glm::cross(normal,tangent), tanV[iver]) < 0.f ? -1.f : 1.f;
    // store the tangent along "u"
    layout.m_tangents.get<glm::vec4>(iver) = glm::vec4(tangent, handness);
  }

}

// ============================================================================

#ifdef MESH_DEBUG

struct s_decimateExporter
{
  std::ofstream rawOBJ;
  uint          offsetVert = 1;

  s_decimateExporter(const std::string &filename) { rawOBJ.open(filename.c_str(), std::ofstream::out); if (rawOBJ.is_open()) rawOBJ << "# WAVEFRONT data - export skin" << std::endl; }
  ~s_decimateExporter() { rawOBJ.close(); }

  uint writeVertex(const s_modelDataLayout &layout, const uint vind, std::vector<glm::uvec2> &remapInd)
  {
    uint       vIndE = uint(-1);
    for (const auto &r : remapInd) { if (r.x == vind) { vIndE = r.y; break; } }
    if (vIndE == uint(-1))
    {
      rawOBJ << "v " << layout.m_positions.get<glm::vec3>(vind) << std::endl;
      vIndE = offsetVert++;
      remapInd.emplace_back(vind, vIndE);
    }
    return vIndE;
  }

  void report_processedStar(const std::size_t iter, const uint ivert, const std::size_t indexOffset, const std::vector<uint> &inTriangles, const std::vector<uint> &outIndices, const s_modelDataLayout &layout)
  {
    if (!rawOBJ.is_open()) return;
    std::vector<glm::uvec2> remapInd; // {.x: index in the mesh, .y: index in the export}

    rawOBJ << "o Step_" << osprinti3(iter) << "_Vert" << osprinti3(ivert) << "_0InZone" << std::endl;
    remapInd.clear();
    for (std::size_t i = 0; i < inTriangles.size(); ++i)
    {
      uint triIndE[3];
      triIndE[0] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 0], remapInd);
      triIndE[1] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 1], remapInd);
      triIndE[2] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 2], remapInd);
      rawOBJ << "f " << triIndE[0] << ' ' << triIndE[1] << ' ' << triIndE[2] << ' ' << std::endl;
    }

    rawOBJ << "o Step_" << osprinti3(iter) << "_Vert" << osprinti3(ivert) << "_0OutZone" << std::endl;
    remapInd.clear();
    for (std::size_t i = 0; i < outIndices.size(); i += 3)
    {
      uint triIndE[3];
      triIndE[0] = writeVertex(layout, outIndices[i + 0], remapInd);
      triIndE[1] = writeVertex(layout, outIndices[i + 1], remapInd);
      triIndE[2] = writeVertex(layout, outIndices[i + 2], remapInd);
      rawOBJ << "f " << triIndE[0] << ' ' << triIndE[1] << ' ' << triIndE[2] << ' ' << std::endl;
    }
  }

  void report_invalidStar(const std::size_t iter, const uint ivert, const std::size_t indexOffset, const std::vector<uint> &inTriangles, const s_modelDataLayout &layout, const char *msg)
  {
    if (!rawOBJ.is_open()) return;
    std::vector<glm::uvec2> remapInd; // {.x: index in the mesh, .y: index in the export}

    rawOBJ << "o Step_" << osprinti3(iter) << "_Vert" << osprinti3(ivert) << "_0Err_" << msg << std::endl;
    remapInd.clear();
    for (std::size_t i = 0; i < inTriangles.size(); ++i)
    {
      uint triIndE[3];
      triIndE[0] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 0], remapInd);
      triIndE[1] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 1], remapInd);
      triIndE[2] = writeVertex(layout, layout.m_index[indexOffset + inTriangles[i] * 3 + 2], remapInd);
      rawOBJ << "f " << triIndE[0] << ' ' << triIndE[1] << ' ' << triIndE[2] << ' ' << std::endl;
    }
  }
};

#else // MESH_DEBUG

struct s_decimateExporter
{
  s_decimateExporter(const char *) {};

  void report_processedStar(const std::size_t, const uint, const std::size_t, const std::vector<uint>&, const std::vector<uint>&, const s_modelDataLayout&) {}
  void report_invalidStar(const std::size_t, const uint, const std::size_t, const std::vector<uint>&, const s_modelDataLayout&, const char*) {}
};

#endif // MESH_DEBUG

// ----------------------------------------------------------------------------

std::size_t decimateCurvature(modelIndexed &model, const std::size_t ipartIn, const float threshold)
{
  const s_modelDataLayout &layout = model.layout();

  TRE_ASSERT(layout.m_indexCount > 0); // needed for connectivity
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t countIn = model.partInfo(ipartIn).m_size;

  TRE_ASSERT(countIn % 3 == 0);

  if (countIn == 0 || threshold == 0.f) return std::size_t(-1);

  s_decimateExporter exporter("decimateCurvature.obj");

  // decimate patterm: star-collapsing
  // collapse vertices considering the local re-meshing of the direct neighbor triangles
  // reconstructing the surface without the vertex results to new triangles, 2 in less.

  // TODO: rework this to process on larger zone, rather than a zone limited to the connected triangle by a vertex (the "star" collapsing).

  TRE_LOG("decimateCurvature: processing mesh (Ntri = " << countIn / 3 << ")");

  // 1. prepare data

  std::size_t  vertexAddOffset = 0;
  const size_t ipartOut = model.createPart(countIn, 0, vertexAddOffset);
  if (ipartOut == std::size_t(-1)) return std::size_t(-1);
  const std::size_t indexOffset = model.partInfo(ipartOut).m_offset;
  layout.copyIndex(model.partInfo(ipartIn).m_offset, countIn, indexOffset);

  // 2. initialize loop

  std::size_t Niter = 0;
  std::size_t Ntri = countIn / 3;

  std::vector<std::vector<uint> > vertexToTri;
  std::vector<bool>               triangleProcessed;
  std::vector<uint>               triangleToRemove;

  while(++Niter < 1024)
  {
    // 2.1 prepare data
    triangleProcessed.clear();
    triangleProcessed.resize(Ntri, false);

    triangleToRemove.clear();

    // 2.2 compute connectivity (from scratch)
    _computeConnectivity_vert2tri(layout, indexOffset, 3 * Ntri, vertexToTri);

    // 2.3 loop over vertices
    for (std::size_t ivert = 0; ivert < layout.m_vertexCount; ++ivert)
    {
      const std::vector<uint> &triangles = vertexToTri[ivert];

      // -> ignore stars with less than 3 triangles
      if (triangles.size() < 3) continue;

      // -> ignore stars with processed triangle(s) because the connectivity has not been updated yet.
      {
        bool hasProcessedTri = false;
        for (uint tri : triangles) hasProcessedTri |= triangleProcessed[tri];
        if (hasProcessedTri) continue;
      }

      s_starCollapser star;
      {
        bool starValid = true;
        star.init(layout, ivert, triangles.size());
        for (uint tri : triangles) starValid &= star.addTri(layout, indexOffset + tri * 3 + 0, indexOffset + tri * 3 + 1, indexOffset + tri * 3 + 2);
        if (!starValid) continue;
      }
      if (!star.computeClosedBelt())
      {
        exporter.report_invalidStar(Niter, ivert, indexOffset, triangles, layout, "belt");
        continue;
      }
      if (star.computeMeanCurvature() > threshold)
      {
        //exporter.report_invalidStar(Niter, ivert, indexOffset, triangles, layout, "curvature");
        continue;
      }

      std::vector<uint> indicesNew;
      if (!star.collapse(indicesNew))
      {
        exporter.report_invalidStar(Niter, ivert, indexOffset, triangles, layout, "collapse");
        continue;
      }
      TRE_ASSERT(indicesNew.size() == 3 * (triangles.size() - 2));

      exporter.report_processedStar(Niter, ivert, indexOffset, triangles, indicesNew, layout);

      // set new indices in the mesh's index-buffer
      for (std::size_t it = 0; it < triangles.size() - 2; ++it)
      {
        const uint Itri = triangles[it];
        layout.m_index[indexOffset + Itri * 3 + 0] = indicesNew[3 * it + 0];
        layout.m_index[indexOffset + Itri * 3 + 1] = indicesNew[3 * it + 1];
        layout.m_index[indexOffset + Itri * 3 + 2] = indicesNew[3 * it + 2];
      }
      triangleToRemove.push_back(triangles[triangles.size() - 2]);
      triangleToRemove.push_back(triangles[triangles.size() - 1]);
      for (uint tri : triangles) triangleProcessed[tri] = true;
    }

    // 3. Clear triangles
    TRE_LOG("decimate Curvature: New step with Ntriangles = " << Ntri << ", Triangles removed = " << triangleToRemove.size());
    if (triangleToRemove.empty()) break;
    sortQuick<uint>(triangleToRemove);
    for (std::size_t k = 0; k < triangleToRemove.size(); ++k)
    {
      const uint Itri = triangleToRemove[triangleToRemove.size() - k - 1];
      // copy the last into the triangle "Itri"
      Ntri--;
      layout.m_index[indexOffset + Itri * 3 + 0] = layout.m_index[indexOffset + Ntri * 3 + 0];
      layout.m_index[indexOffset + Itri * 3 + 1] = layout.m_index[indexOffset + Ntri * 3 + 1];
      layout.m_index[indexOffset + Itri * 3 + 2] = layout.m_index[indexOffset + Ntri * 3 + 2];
    }
  }

  TRE_ASSERT(Ntri * 3 <= countIn);
  model.resizeRawPart(ipartOut, Ntri * 3); // ok, we're not growing.

  return ipartOut;
}

// ============================================================================

std::size_t decimateVoxel(modelIndexed &model, const std::size_t ipartIn, const float gridResolution, const bool keepSharpEdges)
{
  const s_modelDataLayout &layout = model.layout();

  TRE_ASSERT(layout.m_indexCount > 0); // needed for connectivity
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t offsetIn = model.partInfo(ipartIn).m_offset;
  const std::size_t countIn = model.partInfo(ipartIn).m_size;
  const glm::vec3   bbMin = model.partInfo(ipartIn).m_bbox.m_min;
  const glm::vec3   bbMax = model.partInfo(ipartIn).m_bbox.m_max;

  TRE_ASSERT(countIn % 3 == 0);
  if (countIn == 0 || gridResolution <= 0.f) return std::size_t(-1);

  const glm::vec3  gridMin = bbMin - 0.05f * (bbMax - bbMin);
  const glm::ivec3 gridDim = glm::ivec3(glm::ceil((bbMax - bbMin) * 1.1f / gridResolution));

  // it collapses all vertices that are in the same cell of the 3D-grid.
  // it creates a new vertex when needed, and it removes degenerated triangles.

  std::size_t Ntri = countIn / 3;

  TRE_LOG("decimateVoxel: processing mesh (Ntri = " << Ntri << ") with grid " << gridDim.x <<" x " << gridDim.y << " x " << gridDim.z);

  // pre-step: fill the 3d-grid

  std::vector<uint> gridVind(gridDim.x * gridDim.y * gridDim.z, 0);
  for (std::size_t i = offsetIn, stop = offsetIn + countIn; i < stop; ++i)
  {
    const uint ind = layout.m_index[i];
    const glm::vec3  pos = layout.m_positions.get<glm::vec3>(ind);
    const glm::ivec3 posI =  glm::ivec3((pos - gridMin) / gridResolution);
    const int        g = (posI.z * gridDim.y + posI.y) * gridDim.x + posI.x;
    TRE_ASSERT(posI.x >= 0 && posI.x < gridDim.x);
    TRE_ASSERT(posI.y >= 0 && posI.y < gridDim.y);
    TRE_ASSERT(posI.z >= 0 && posI.z < gridDim.z);
    gridVind[g] += 1;
  }

  // count before allocation

  std::size_t verticesNew = 0;
  if (keepSharpEdges)
  {
    for (const uint gvind : gridVind) verticesNew += gvind;
  }
  else
  {
    for (const uint gvind : gridVind) verticesNew += (gvind >= 2) ? 1 : 0;
  }

  // allocate mesh data

  std::size_t  vertexAddOffset = 0;
  const size_t ipartOut = model.createPart(countIn, verticesNew, vertexAddOffset);
  if (ipartOut == std::size_t(-1)) return std::size_t(-1);
  const std::size_t offsetOut = model.partInfo(ipartOut).m_offset;
  layout.copyIndex(model.partInfo(ipartIn).m_offset, countIn, offsetOut);

  // create the new vertices

  std::vector<glm::uvec2> triCollected, triToProcess;
  triCollected.reserve(16);
  triToProcess.reserve(16);

  for (int ik = 0, gc = int(gridVind.size()); ik < gc; ++ik)
  {
    if (gridVind[ik] >= 2)
    {
      // collect triangles
      triCollected.clear();
      glm::vec3 vpos = glm::vec3(0.f);
      int       c = 0;
      for (std::size_t iT = 0; iT < Ntri; ++iT)
      {
        const uint indA = layout.m_index[offsetOut + iT * 3 + 0];
        const uint indB = layout.m_index[offsetOut + iT * 3 + 1];
        const uint indC = layout.m_index[offsetOut + iT * 3 + 2];
        const glm::ivec3 posA = glm::ivec3((layout.m_positions.get<glm::vec3>(indA) - gridMin) / gridResolution);
        const glm::ivec3 posB = glm::ivec3((layout.m_positions.get<glm::vec3>(indB) - gridMin) / gridResolution);
        const glm::ivec3 posC = glm::ivec3((layout.m_positions.get<glm::vec3>(indC) - gridMin) / gridResolution);
        const int gA = (posA.z * gridDim.y + posA.y) * gridDim.x + posA.x;
        const int gB = (posB.z * gridDim.y + posB.y) * gridDim.x + posB.x;
        const int gC = (posC.z * gridDim.y + posC.y) * gridDim.x + posC.x;
        const uint flag = (gA == ik) * 0x1 + (gB == ik) * 0x2 + (gC == ik) * 0x4;
        if (gA == ik) { vpos += layout.m_positions.get<glm::vec3>(indA); c += 1; }
        if (gB == ik) { vpos += layout.m_positions.get<glm::vec3>(indB); c += 1; }
        if (gC == ik) { vpos += layout.m_positions.get<glm::vec3>(indC); c += 1; }
        if (flag != 0) triCollected.push_back(glm::uvec2(iT, flag));
      }
      TRE_ASSERT(!triCollected.empty());
      TRE_ASSERT(c > 1);
      vpos /= float(c);
      //
      while (!triCollected.empty())
      {
        // assign a unique vertex per each set of triangles ...
        triToProcess.clear();
        triToProcess.push_back(triCollected.back()); triCollected.pop_back();
        if (keepSharpEdges) // .. that share vertices
        {
          for (std::size_t j = 0; j < triCollected.size();)
          {
            const glm::uvec2 tri = triCollected[j];
            const uint indA = layout.m_index[offsetOut + tri.x * 3 + 0];
            const uint indB = layout.m_index[offsetOut + tri.x * 3 + 1];
            const uint indC = layout.m_index[offsetOut + tri.x * 3 + 2];
            bool hasSharedPoint = false;
            for (const auto &triB : triToProcess)
            {
              const uint otherA = layout.m_index[offsetOut + triB.x * 3 + 0];
              const uint otherB = layout.m_index[offsetOut + triB.x * 3 + 1];
              const uint otherC = layout.m_index[offsetOut + triB.x * 3 + 2];
              const int cmm = (indA == otherA) | (indA == otherB) | (indA == otherC) |
                              (indB == otherA) | (indB == otherB) | (indB == otherC) |
                              (indC == otherA) | (indC == otherB) | (indC == otherC);
              if (cmm != 0) { hasSharedPoint = true; break; }
            }
            if (hasSharedPoint)
            {
              triToProcess.push_back(tri);
              triCollected[j] = triCollected.back();
              triCollected.pop_back();
              j = 0;
            }
            else
            {
              ++j;
            }
          }
        }
        else // ... of all the triangles
        {
          for (const auto &tri : triCollected) triToProcess.push_back(tri);
          triCollected.clear();
        }
        // create the vertex and assign triangles
        bool duplicated = false;
        for (const auto &tri : triToProcess)
        {
          uint &indA = layout.m_index[offsetOut + tri.x * 3 + 0];
          uint &indB = layout.m_index[offsetOut + tri.x * 3 + 1];
          uint &indC = layout.m_index[offsetOut + tri.x * 3 + 2];
          if ((tri.y & 0x1) != 0)
          {
            if (!duplicated) layout.copyVertex(indA, 1, vertexAddOffset);
            duplicated = true;
            indA = vertexAddOffset;
          }
          if ((tri.y & 0x2) != 0)
          {
            if (!duplicated) layout.copyVertex(indB, 1, vertexAddOffset);
            duplicated = true;
            indB = vertexAddOffset;
          }
          if ((tri.y & 0x4) != 0)
          {
            if (!duplicated) layout.copyVertex(indC, 1, vertexAddOffset);
            duplicated = true;
            indC = vertexAddOffset;
          }
        }
        TRE_ASSERT(duplicated);
        layout.m_positions.get<glm::vec3>(vertexAddOffset) = vpos;
        ++vertexAddOffset;
      }
    }
    else if (keepSharpEdges && gridVind[ik] == 1)
    {
      // duplicate also the alone vertices
      bool duplicated = false;
      for (std::size_t i = offsetOut, stop = offsetOut + countIn; i < stop; ++i)
      {
        uint &ind = layout.m_index[i];
        const glm::vec3  pos = layout.m_positions.get<glm::vec3>(ind);
        const glm::ivec3 posI =  glm::ivec3((pos - gridMin) / gridResolution);
        const int        g = (posI.z * gridDim.y + posI.y) * gridDim.x + posI.x;
        if (g == ik)
        {
          if (!duplicated) layout.copyVertex(ind, 1, vertexAddOffset);
          duplicated = true;
          ind = vertexAddOffset;
        }
      }
      TRE_ASSERT(duplicated);
      ++vertexAddOffset;
    }
  }

  // remove degenerated triangles

  for (std::size_t iT = 0; iT < Ntri; )
  {
    uint &indA = layout.m_index[offsetOut + iT * 3 + 0];
    uint &indB = layout.m_index[offsetOut + iT * 3 + 1];
    uint &indC = layout.m_index[offsetOut + iT * 3 + 2];
    if (indA == indB || indA == indC || indB == indC)
    {
      --Ntri;
      indA = layout.m_index[offsetOut + Ntri * 3 + 0];
      indB = layout.m_index[offsetOut + Ntri * 3 + 1];
      indC = layout.m_index[offsetOut + Ntri * 3 + 2];
    }
    else
    {
      ++iT;
    }
  }

  TRE_ASSERT(Ntri * 3 <= countIn);

  model.resizeRawPart(ipartOut, Ntri * 3); // ok, we're not growing.

  TRE_LOG("decimateVoxel: Completed, Ntriangles = " << Ntri << ", new vertices = " << verticesNew);

  return ipartOut;
}

// ============================================================================

struct s_tetrahedron
{
  const glm::vec3 *ptA, *ptB, *ptC, *ptD;
  s_tetrahedron *adjBCD, *adjACD, *adjABD, *adjABC;
  uint metadata;
  float volume;

  s_tetrahedron() : ptA(nullptr), ptB(nullptr), ptC(nullptr), ptD(nullptr), metadata(0)
  {
  }

  s_tetrahedron(const glm::vec3 *pa, const glm::vec3 *pb, const glm::vec3 *pc, const glm::vec3 *pd,
               s_tetrahedron *tBCD, s_tetrahedron *tACD, s_tetrahedron *tABD, s_tetrahedron *tABC)
  : ptA(pa), ptB(pb), ptC(pc), ptD(pd), adjBCD(tBCD), adjACD(tACD), adjABD(tABD), adjABC(tABC), metadata(1 << 16)
  {
    const glm::vec3 eAB = *pb - *pa;
    const glm::vec3 eAC = *pc - *pa;
    const glm::vec3 eAD = *pd - *pa;
    volume = glm::abs(glm::dot(eAB, glm::cross(eAC, eAD))) / 6.f;
    TRE_ASSERT(volume != 0.f);
  }

  bool hasPoint(const glm::vec3 *pt) const { return (ptA == pt) | (ptB == pt) | (ptC == pt) | (ptD == pt); }
  bool hasAdjacant(const s_tetrahedron *other) const { return (adjBCD == other) | (adjACD == other) | (adjABD == other) | (adjABC == other); }

  bool metaContainsPoint() const { return metadata & (1 << 16); }
  void metaSetNoMorePoints() { metadata &= ~(1 << 16); }

  bool metaSideInterior() const { return metadata & (1 << 8); }
  void metaSetSideInterior() { metadata |= (1 << 8); }
  bool metaSideExterior() const { return metadata & (1 << 9); }
  void metaSetSideExterior() { metadata |= (1 << 9); }

  bool valid() const
  {
    const bool pV = (ptA != nullptr);
    TRE_ASSERT((!pV) || (ptB != nullptr && ptC != nullptr && ptD != nullptr));
    TRE_ASSERT((!pV) || (ptA != ptB && ptA != ptC && ptA != ptD && ptB != ptC && ptB != ptD && ptC != ptD));
    TRE_ASSERT((!pV) || (adjABC == nullptr) || (adjABC->hasPoint(ptA) && adjABC->hasPoint(ptB) && adjABC->hasPoint(ptC)));
    TRE_ASSERT((!pV) || (adjABD == nullptr) || (adjABD->hasPoint(ptA) && adjABD->hasPoint(ptB) && adjABD->hasPoint(ptD)));
    TRE_ASSERT((!pV) || (adjACD == nullptr) || (adjACD->hasPoint(ptA) && adjACD->hasPoint(ptC) && adjACD->hasPoint(ptD)));
    TRE_ASSERT((!pV) || (adjBCD == nullptr) || (adjBCD->hasPoint(ptB) && adjBCD->hasPoint(ptC) && adjBCD->hasPoint(ptD)));
    TRE_ASSERT((!pV) || (adjABC == nullptr) || (adjABC != adjABD && adjABC != adjACD && adjABC != adjBCD));
    TRE_ASSERT((!pV) || (adjABD == nullptr) || (adjABD != adjABC && adjABD != adjACD && adjABD != adjBCD));
    TRE_ASSERT((!pV) || (adjACD == nullptr) || (adjACD != adjABC && adjACD != adjABD && adjACD != adjBCD));
    TRE_ASSERT((!pV) || (adjBCD == nullptr) || (adjBCD != adjABC && adjBCD != adjABD && adjBCD != adjACD));
    return pV;
  }

  void clear()
  {
    if (adjBCD != nullptr) adjBCD->replaceNeigbourTetra(this, nullptr);
    if (adjACD != nullptr) adjACD->replaceNeigbourTetra(this, nullptr);
    if (adjABD != nullptr) adjABD->replaceNeigbourTetra(this, nullptr);
    if (adjABC != nullptr) adjABC->replaceNeigbourTetra(this, nullptr);
    ptA = ptB = ptC = ptD = nullptr;
  }

  void replaceNeigbourTetra(s_tetrahedron *oldtetra, s_tetrahedron *newtetra)
  {
    TRE_ASSERT(oldtetra != nullptr);
    TRE_ASSERT((adjBCD == oldtetra) + (adjACD == oldtetra) + (adjABD == oldtetra) + (adjABC == oldtetra) == 1);
    if      (adjBCD == oldtetra)   adjBCD = newtetra;
    else if (adjACD == oldtetra)   adjACD = newtetra;
    else if (adjABD == oldtetra)   adjABD = newtetra;
    else /* (adjABC == oldtetra)*/ adjABC = newtetra;
  }

  void setNeigbourTetra(const glm::vec3 *p, const glm::vec3 *q, const glm::vec3 *r, s_tetrahedron *newtetra)
  {
    const bool hasA = (p == ptA) | (q == ptA) | (r == ptA);
    const bool hasB = (p == ptB) | (q == ptB) | (r == ptB);
    const bool hasC = (p == ptC) | (q == ptC) | (r == ptC);
    const bool hasD = (p == ptD) | (q == ptD) | (r == ptD);
    TRE_ASSERT(hasA + hasB + hasC + hasD == 3);
    if      (!hasA)   adjBCD = newtetra;
    else if (!hasB)   adjACD = newtetra;
    else if (!hasC)   adjABD = newtetra;
    else /* (!hasD)*/ adjABC = newtetra;
  }

  bool pointInCircumsphere(const glm::vec3 &pt) const
  {
    const glm::vec3 vPA = *ptA - pt;
    const glm::vec3 vPB = *ptB - pt;
    const glm::vec3 vPC = *ptC - pt;
    const glm::vec3 vPD = *ptD - pt;
    const float     lPA = glm::dot(vPA, vPA);
    const float     lPB = glm::dot(vPB, vPB);
    const float     lPC = glm::dot(vPC, vPC);
    const float     lPD = glm::dot(vPD, vPD);

    const glm::mat3 mSpin = glm::mat3(*ptB - *ptA, *ptC - *ptA, *ptD - *ptA);
    const float     mSpinDet = -glm::determinant(mSpin);
    TRE_ASSERT(mSpinDet != 0.f);
    const float     mSpinSign = mSpinDet > 0.f ? 1.f : -1.f;

    const glm::mat4 m4 = glm::mat4(vPA.x, vPA.y, vPA.z, lPA,
                                   vPB.x, vPB.y, vPB.z, lPB,
                                   vPC.x, vPC.y, vPC.z, lPC,
                                   vPD.x, vPD.y, vPD.z, lPD);

    const float   m4det = mSpinSign * glm::determinant(m4); // suffer from precision issues when fabsf(m4det) is near 0.f

    return m4det > -1.e-6f;
  }

  void swapPoints_A_B() { std::swap(ptA, ptB); std::swap(adjACD, adjBCD); }
  void swapPoints_A_C() { std::swap(ptA, ptC); std::swap(adjABD, adjBCD); }
  void swapPoints_A_D() { std::swap(ptA, ptD); std::swap(adjABC, adjBCD); }
  void swapPoints_B_C() { std::swap(ptB, ptC); std::swap(adjABD, adjACD); }
  void swapPoints_B_D() { std::swap(ptB, ptD); std::swap(adjABC, adjACD); }
  void swapPoints_C_D() { std::swap(ptC, ptD); std::swap(adjABC, adjABD); }

  static void conformSharedPoints(s_tetrahedron &t1, s_tetrahedron &t2) // both unshared points are labeled "ptA", shared points have the same labels
  {
    TRE_ASSERT(t1.valid() && t2.valid());
    // swap points to have the exterior points labeled "ptA"
    if      (!t2.hasPoint(t1.ptB)) t1.swapPoints_A_B();
    else if (!t2.hasPoint(t1.ptC)) t1.swapPoints_A_C();
    else if (!t2.hasPoint(t1.ptD)) t1.swapPoints_A_D();
    TRE_ASSERT(!t2.hasPoint(t1.ptA));
    if      (!t1.hasPoint(t2.ptB)) t2.swapPoints_A_B();
    else if (!t1.hasPoint(t2.ptC)) t2.swapPoints_A_C();
    else if (!t1.hasPoint(t2.ptD)) t2.swapPoints_A_D();
    TRE_ASSERT(!t1.hasPoint(t2.ptA));
    TRE_ASSERT(t1.adjBCD == &t2);
    TRE_ASSERT(t2.adjBCD == &t1);
    // swap points to have identical shared-point "ptB"
    if      (t1.ptB == t2.ptC) t2.swapPoints_B_C();
    else if (t1.ptB == t2.ptD) t2.swapPoints_B_D();
    TRE_ASSERT(t1.ptB == t2.ptB);
    // swap points to have identical shared-point "ptC" (and "ptD")
    if (t1.ptC == t2.ptD) t2.swapPoints_C_D();
    TRE_ASSERT(t1.ptC == t2.ptC);
    TRE_ASSERT(t1.ptD == t2.ptD);
  }

  static void split_2tetras_to_3tetras(s_tetrahedron &t1, s_tetrahedron &t2, s_tetrahedron &t3)
  {
    // T1(A1 - B C D) <-linked-> T2(B C D - A2)
    // It cuts by creating the edge (A1,A2) and renamed into (A,B) in the outcome.
    TRE_ASSERT(t1.valid() && t2.valid() && !t3.valid());
    TRE_ASSERT(t1.ptB == t2.ptB && t1.ptC == t2.ptC && t1.ptD == t2.ptD); // both tetrahedrons must have been "conformed" together
    // replace neighbors
    if (t1.adjABC != nullptr) t1.adjABC->replaceNeigbourTetra(&t1, &t3);
    if (t1.adjABD != nullptr) t1.adjABD->replaceNeigbourTetra(&t1, &t1);
    if (t1.adjACD != nullptr) t1.adjACD->replaceNeigbourTetra(&t1, &t2);
    if (t2.adjABC != nullptr) t2.adjABC->replaceNeigbourTetra(&t2, &t3);
    if (t2.adjABD != nullptr) t2.adjABD->replaceNeigbourTetra(&t2, &t1);
    if (t2.adjACD != nullptr) t2.adjACD->replaceNeigbourTetra(&t2, &t2);
    // split
    const glm::vec3 *A1 = t1.ptA, *A2 = t2.ptA;
    s_tetrahedron   *t2_adjABD = t2.adjABD;
    t3 = s_tetrahedron(A1, A2, t1.ptB, t1.ptC, t2.adjABC, t1.adjABC, &t2, &t1);
    t2 = s_tetrahedron(A1, A2, t1.ptC, t1.ptD, t2.adjACD, t1.adjACD, &t1, &t3);
    t1 = s_tetrahedron(A1, A2, t1.ptD, t1.ptB, t2_adjABD, t1.adjABD, &t3, &t2);
    TRE_ASSERT(t1.valid() && t2.valid() && t3.valid());
  }

  static void swap_Pyramid(s_tetrahedron &tR1, s_tetrahedron &tR2, s_tetrahedron &tS1, s_tetrahedron &tS2)
  {
    // tR1(A1 - B C DR) <-linked-> tR2(B C DR - A2)
    // tS1(A1 - B C DS) <-linked-> tS2(B C DS - A2)
    // It removes the existing edge (B,C) and creates the edge (A1,A2) that is renamed into (A,B) in the outcome.
    TRE_ASSERT(tR1.valid() && tR2.valid() && tS1.valid() && tS2.valid());
    TRE_ASSERT(tR1.ptB == tR2.ptB && tR1.ptC == tR2.ptC && tR1.ptD == tR2.ptD); // tetrahedrons must have been "conformed" together
    TRE_ASSERT(tS1.ptB == tS2.ptB && tS1.ptC == tS2.ptC && tS1.ptD == tS2.ptD); // tetrahedrons must have been "conformed" together
    TRE_ASSERT(tS1.ptA == tR1.ptA && tS2.ptA == tR2.ptA); // tetrahedrons must have been "conformed" together
    // swap points to have identical shared-point "ptB" (on the 4 tetras)
    if (!tS1.hasPoint(tR1.ptB)) { tR1.swapPoints_B_D(); tR2.swapPoints_B_D(); }
    if      (tR1.ptB == tS1.ptC) { tS1.swapPoints_B_C(); tS2.swapPoints_B_C(); }
    else if (tR1.ptB == tS1.ptD) { tS1.swapPoints_B_D(); tS2.swapPoints_B_D(); }
    TRE_ASSERT(tR1.ptB == tR2.ptB && tR1.ptB == tS1.ptB && tR1.ptB == tS2.ptB);
    // swap points to have identical shared-point "ptC" (on the 4 tetras)
    if (!tS1.hasPoint(tR1.ptC)) { tR1.swapPoints_C_D(); tR2.swapPoints_C_D(); }
    if      (tR1.ptC == tS1.ptD) { tS1.swapPoints_C_D(); tS2.swapPoints_C_D(); }
    TRE_ASSERT(tR1.ptC == tR2.ptC && tR1.ptC == tS1.ptC && tR1.ptC == tS2.ptC);
    // replace neighbors
    if (tR1.adjABD != nullptr) tR1.adjABD->replaceNeigbourTetra(&tR1, &tR1);
    if (tR1.adjACD != nullptr) tR1.adjACD->replaceNeigbourTetra(&tR1, &tR2);
    if (tS1.adjABD != nullptr) tS1.adjABD->replaceNeigbourTetra(&tS1, &tS1);
    if (tS1.adjACD != nullptr) tS1.adjACD->replaceNeigbourTetra(&tS1, &tS2);
    if (tR2.adjABD != nullptr) tR2.adjABD->replaceNeigbourTetra(&tR2, &tR1);
    if (tR2.adjACD != nullptr) tR2.adjACD->replaceNeigbourTetra(&tR2, &tR2);
    if (tS2.adjABD != nullptr) tS2.adjABD->replaceNeigbourTetra(&tS2, &tS1);
    if (tS2.adjACD != nullptr) tS2.adjACD->replaceNeigbourTetra(&tS2, &tS2);
    // replace points
    const glm::vec3 *A1 = tR1.ptA, *A2 = tR2.ptA;
    s_tetrahedron *tS1_adjACD = tS1.adjACD, *tR1_adjACD = tR1.adjACD;
    tR1 = s_tetrahedron(A1, A2, tR1.ptB, tR1.ptD, tR2.adjABD, tR1.adjABD, &tR2, &tS1);
    tS1 = s_tetrahedron(A1, A2, tS1.ptB, tS1.ptD, tS2.adjABD, tS1.adjABD, &tS2, &tR1);
    tR2 = s_tetrahedron(A1, A2, tR2.ptC, tR2.ptD, tR2.adjACD, tR1_adjACD, &tR1, &tS2);
    tS2 = s_tetrahedron(A1, A2, tS2.ptC, tS2.ptD, tS2.adjACD, tS1_adjACD, &tS1, &tR2);
    TRE_ASSERT(tR1.valid() && tR2.valid() && tS1.valid() && tS2.valid());
  }
};

// ----------------------------------------------------------------------------

struct s_surface
{
  const glm::vec3 *pt1, *pt2, *pt3;
  s_tetrahedron *tetraExterior;
  s_surface *surf12, *surf13, *surf23;
  glm::vec3 normalOut;

  s_surface(const glm::vec3 *p1, const glm::vec3 *p2, const glm::vec3 *p3, s_tetrahedron *tExterior, const glm::vec3 &pInterior)
  : pt1(p1), pt2(p2), pt3(p3), tetraExterior(tExterior), surf12(nullptr), surf13(nullptr), surf23(nullptr)
  {
    TRE_ASSERT(p1 != p2 && p1 != p3 && p2 != p3);
    normalOut = glm::cross(*p2 - *p1, *p3 - *p1);
    if (glm::dot(normalOut, pInterior - *p1) > 0.f) normalOut = -normalOut;
  }

  bool hasPoint(const glm::vec3 *pt) const
  {
    return (pt1 == pt) | (pt2 == pt) | (pt3 == pt);
  }

  float volumeSignedWithPoint(const glm::vec3 &pInsert) const
  {
    return glm::dot(normalOut, pInsert - *pt1) / 6.f;
  }

  static void generateOutSurface(const std::vector<s_tetrahedron*> &listTetra, std::vector<s_surface> &listSurf)
  {
    // find surfaces (n*n algo)
    for (s_tetrahedron *t : listTetra)
    {
      bool isEdge_BCD = true;
      bool isEdge_ACD = true;
      bool isEdge_ABD = true;
      bool isEdge_ABC = true;
      s_tetrahedron *tBCD = t->adjBCD;
      s_tetrahedron *tACD = t->adjACD;
      s_tetrahedron *tABD = t->adjABD;
      s_tetrahedron *tABC = t->adjABC;
      for (const s_tetrahedron *tt : listTetra)
      {
        if (tBCD == tt) isEdge_BCD = false;
        if (tACD == tt) isEdge_ACD = false;
        if (tABD == tt) isEdge_ABD = false;
        if (tABC == tt) isEdge_ABC = false;
      }
      if (isEdge_BCD) listSurf.push_back(s_surface(t->ptB, t->ptC, t->ptD, tBCD, *t->ptA));
      if (isEdge_ACD) listSurf.push_back(s_surface(t->ptA, t->ptC, t->ptD, tACD, *t->ptB));
      if (isEdge_ABD) listSurf.push_back(s_surface(t->ptA, t->ptB, t->ptD, tABD, *t->ptC));
      if (isEdge_ABC) listSurf.push_back(s_surface(t->ptA, t->ptB, t->ptC, tABC, *t->ptD));
    }
  }

  static void computeNeighbors(std::vector<s_surface> &listSurf)
  {
    // naive n*n algo
    const std::size_t count = listSurf.size();
    for (std::size_t i = 0; i < count; ++i)
    {
      s_surface &si = listSurf[i];
      for (std::size_t j = 0; j < i; ++j)
      {
        s_surface &sj = listSurf[j];
        const uint key = (si.pt1 == sj.pt1) * 0x001 | (si.pt1 == sj.pt2) * 0x010 | (si.pt1 == sj.pt3) * 0x100 |
                         (si.pt2 == sj.pt1) * 0x002 | (si.pt2 == sj.pt2) * 0x020 | (si.pt2 == sj.pt3) * 0x200 |
                         (si.pt3 == sj.pt1) * 0x004 | (si.pt3 == sj.pt2) * 0x040 | (si.pt3 == sj.pt3) * 0x400;
        if ((key & 0x111) != 0 && (key & 0x222) != 0) { TRE_ASSERT(si.surf12 == nullptr); si.surf12 = &sj; }
        if ((key & 0x111) != 0 && (key & 0x444) != 0) { TRE_ASSERT(si.surf13 == nullptr); si.surf13 = &sj; }
        if ((key & 0x222) != 0 && (key & 0x444) != 0) { TRE_ASSERT(si.surf23 == nullptr); si.surf23 = &sj; }
        if ((key & 0x007) != 0 && (key & 0x070) != 0) { TRE_ASSERT(sj.surf12 == nullptr); sj.surf12 = &si; }
        if ((key & 0x007) != 0 && (key & 0x700) != 0) { TRE_ASSERT(sj.surf13 == nullptr); sj.surf13 = &si; }
        if ((key & 0x070) != 0 && (key & 0x700) != 0) { TRE_ASSERT(sj.surf23 == nullptr); sj.surf23 = &si; }
      }
    }
    // check (closed surface)
#ifdef TRE_DEBUG
    for (const s_surface &s : listSurf)
    {
      TRE_ASSERT(s.surf12 != nullptr && s.surf13 != nullptr && s.surf23 != nullptr);
    }
#endif
  }
};

// ----------------------------------------------------------------------------

static bool _checkInsertionValidity(const std::vector<s_tetrahedron *> &listTetra, s_tetrahedron &tAdd, const glm::vec3 &pt, const float minVolume)
{
  // Check only surfaces that would be exterior if the tetrahedron is added.

  // get exterior surfaces
  s_tetrahedron *tadjABC = tAdd.adjABC;
  s_tetrahedron *tadjABD = tAdd.adjABD;
  s_tetrahedron *tadjACD = tAdd.adjACD;
  s_tetrahedron *tadjBCD = tAdd.adjBCD;
  for (s_tetrahedron *tbis : listTetra)
  {
    if      (tbis == tadjABC) tadjABC = nullptr;
    else if (tbis == tadjABD) tadjABD = nullptr;
    else if (tbis == tadjACD) tadjACD = nullptr;
    else if (tbis == tadjBCD) tadjBCD = nullptr;
  }
  TRE_ASSERT((tadjABC != nullptr) + (tadjABD != nullptr) + (tadjACD != nullptr) + (tadjBCD != nullptr) < 4);

  // check surface ABC
  if (tadjABC != nullptr && s_surface(tAdd.ptA, tAdd.ptB, tAdd.ptC, nullptr, *tAdd.ptD).volumeSignedWithPoint(pt) > -minVolume)
      return false;

  // check surface ABD
  if (tadjABD != nullptr && s_surface(tAdd.ptA, tAdd.ptB, tAdd.ptD, nullptr, *tAdd.ptC).volumeSignedWithPoint(pt) > -minVolume)
      return false;

  // check surface ACD
  if (tadjACD != nullptr && s_surface(tAdd.ptA, tAdd.ptC, tAdd.ptD, nullptr, *tAdd.ptB).volumeSignedWithPoint(pt) > -minVolume)
      return false;

  // check surface BCD
  if (tadjBCD != nullptr && s_surface(tAdd.ptB, tAdd.ptC, tAdd.ptD, nullptr, *tAdd.ptA).volumeSignedWithPoint(pt) > -minVolume)
      return false;

  return true;
}

// ----------------------------------------------------------------------------

static void _walkTetrahedronOnEdge(std::vector<s_tetrahedron *> &listTetra /* 1 or 2 elements*/, const glm::vec3 *ptEdge1, const glm::vec3 *ptEdge2)
{
  TRE_ASSERT(ptEdge1 != ptEdge2);
  TRE_ASSERT(listTetra.size() >= 1 && listTetra.size() <= 2);

  s_tetrahedron *tPrev = (listTetra.size() == 2) ? listTetra[0] : nullptr;
  s_tetrahedron *tCurr =  listTetra.back();

  while (listTetra.size() < 128 /* will certainly fail if this arbitrary size is reached */)
  {
    s_tetrahedron *tAdv = nullptr;

#ifdef TRE_DEBUG
    const bool onEdgePtA = (ptEdge1 == tCurr->ptA) | (ptEdge2 == tCurr->ptA);
#endif
    const bool onEdgePtB = (ptEdge1 == tCurr->ptB) | (ptEdge2 == tCurr->ptB);
    const bool onEdgePtC = (ptEdge1 == tCurr->ptC) | (ptEdge2 == tCurr->ptC);
    const bool onEdgePtD = (ptEdge1 == tCurr->ptD) | (ptEdge2 == tCurr->ptD);
    TRE_ASSERT(onEdgePtA + onEdgePtB + onEdgePtC + onEdgePtD == 2);
    const uint key = onEdgePtB * 1 + onEdgePtC * 2 + onEdgePtD * 4;
    switch (key)
    {
    case 1: // A-B
      TRE_ASSERT(tPrev == nullptr || tCurr->adjABC == tPrev || tCurr->adjABD == tPrev);
      tAdv = (tCurr->adjABC == tPrev) ? tCurr->adjABD : tCurr->adjABC;
      break;
    case 2: // A-C
      TRE_ASSERT(tPrev == nullptr || tCurr->adjABC == tPrev || tCurr->adjACD == tPrev);
      tAdv = (tCurr->adjABC == tPrev) ? tCurr->adjACD : tCurr->adjABC;
      break;
    case 3: // B-C
      TRE_ASSERT(tPrev == nullptr || tCurr->adjABC == tPrev || tCurr->adjBCD == tPrev);
      tAdv = (tCurr->adjABC == tPrev) ? tCurr->adjBCD : tCurr->adjABC;
      break;
    case 4: // A-D
      TRE_ASSERT(tPrev == nullptr || tCurr->adjABD == tPrev || tCurr->adjACD == tPrev);
      tAdv = (tCurr->adjABD == tPrev) ? tCurr->adjACD : tCurr->adjABD;
      break;
    case 5: // B-D
      TRE_ASSERT(tPrev == nullptr || tCurr->adjABD == tPrev || tCurr->adjBCD == tPrev);
      tAdv = (tCurr->adjABD == tPrev) ? tCurr->adjBCD : tCurr->adjABD;
      break;
    case 6: // C-D
      TRE_ASSERT(tPrev == nullptr || tCurr->adjACD == tPrev || tCurr->adjBCD == tPrev);
      tAdv = (tCurr->adjACD == tPrev) ? tCurr->adjBCD : tCurr->adjACD;
      break;
    }
    if (tAdv == listTetra[0]) break;
    TRE_ASSERT(tAdv != nullptr && std::find(listTetra.begin(), listTetra.end(), tAdv) == listTetra.end());
    listTetra.push_back(tAdv);
    tPrev = tCurr;
    tCurr = tAdv;
  }
}

// ----------------------------------------------------------------------------

struct s_meshTriangle
{
  const glm::vec3 *ptF, *ptG, *ptH;
  s_tetrahedron   *tetraNearby;
  glm::vec3       normalOut;

  s_meshTriangle() : ptF(nullptr), ptG(nullptr), ptH(nullptr), tetraNearby(nullptr), normalOut(0.f) {}
  s_meshTriangle(const glm::vec3 *pF, const glm::vec3 *pG, const glm::vec3 *pH, const glm::vec3 &nOut) : ptF(pF), ptG(pG), ptH(pH), tetraNearby(nullptr), normalOut(nOut) {}

  bool hasPoint(const glm::vec3 *pt) const { return (pt == ptF) || (pt == ptG) || (pt == ptH); }
};

// ----------------------------------------------------------------------------

#ifdef MESH_DEBUG

struct s_tetraReporter
{
  std::ofstream rawOBJ;
  std::size_t   stepId = 1;
  std::size_t   offsetVert = 1;

  s_tetraReporter(const std::string &filename) { rawOBJ.open(filename.c_str(), std::ofstream::out); if (rawOBJ.is_open()) rawOBJ << "# WAVEFRONT data - export tetra" << std::endl; }
  ~s_tetraReporter() { rawOBJ.close(); }

  void writeTetra(const s_tetrahedron &t, const float scale = 1.f)
  {
    const glm::vec3 ptCenter = 0.25f * (*t.ptA + *t.ptB + *t.ptC + *t.ptD);
    rawOBJ << "v " << (1.f - scale) * ptCenter + scale * (*t.ptA) << std::endl;
    rawOBJ << "v " << (1.f - scale) * ptCenter + scale * (*t.ptB) << std::endl;
    rawOBJ << "v " << (1.f - scale) * ptCenter + scale * (*t.ptC) << std::endl;
    rawOBJ << "v " << (1.f - scale) * ptCenter + scale * (*t.ptD) << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 3 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 2 << ' ' << offsetVert + 3 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << offsetVert + 3 << ' ' << std::endl;
    offsetVert += 4;
  }

  void writeSurface(const s_surface &s)
  {
    rawOBJ << "v " << *s.pt1 << std::endl;
    rawOBJ << "v " << *s.pt2 << std::endl;
    rawOBJ << "v " << *s.pt3 << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << std::endl;
    offsetVert += 3;
  }

  void writeTriangle(const s_meshTriangle &tri)
  {
    rawOBJ << "v " << *tri.ptF << std::endl;
    rawOBJ << "v " << *tri.ptG << std::endl;
    rawOBJ << "v " << *tri.ptH << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << std::endl;
    offsetVert += 3;
  }

  void writePoint(const glm::vec3 &pt)
  {
    rawOBJ << "v " << pt.x - 0.002f << ' ' <<  pt.y - 0.002f << ' ' << pt.z - 0.002f << std::endl;
    rawOBJ << "v " << pt.x - 0.002f << ' ' <<  pt.y + 0.002f << ' ' << pt.z + 0.002f << std::endl;
    rawOBJ << "v " << pt.x + 0.002f << ' ' <<  pt.y - 0.002f << ' ' << pt.z + 0.002f << std::endl;
    rawOBJ << "v " << pt.x + 0.002f << ' ' <<  pt.y + 0.002f << ' ' << pt.z - 0.002f << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 3 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 2 << ' ' << offsetVert + 3 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << offsetVert + 3 << ' ' << std::endl;
    offsetVert += 4;
  }

  void report_Step1_Fail(const std::vector<s_tetrahedron *> &tetraToProcess, const s_surface *s, const glm::vec3 &ptToAdd, uint ptOnSurfCount)
  {
    for (std::size_t i = 0; i < tetraToProcess.size(); ++i)
    {
      rawOBJ  << "o Step1_" << osprinti3(stepId) << "_FAILED_T" << osprinti3(i) << std::endl;
      writeTetra(*tetraToProcess[i]);
    }
    if (s != nullptr)
    {
      rawOBJ << "o Step1_" << osprinti3(stepId) << "_FAILED_BadSurf" << std::endl;
      writeSurface(*s);
    }
    {
      rawOBJ << "o Step1_" << osprinti3(stepId) << "_FAILED_PtOn" << ptOnSurfCount << "Surfs" << std::endl;
      writePoint(ptToAdd);
    }
    ++stepId;
  }

  void report_Step2(const std::vector<s_tetrahedron *> &tetras, const glm::vec3 &ptEdgeBegin, const glm::vec3 &ptEdgeEnd, const char *label)
  {
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ  << "o Step2_" << osprinti3(stepId) << "_" << label << "_T" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_" << label << "_EdgePtBegin" << std::endl;
      writePoint(ptEdgeBegin);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_" << label << "_EdgePtEnd" << std::endl;
      writePoint(ptEdgeEnd);
    }
    ++stepId;
  }

  void report_Step2(const std::vector<s_tetrahedron *> &tetras, const s_meshTriangle &tri, const char *label)
  {
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ  << "o Step2_" << osprinti3(stepId) << "_" << label << "_T" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_" << label << "_Triangle" << std::endl;
      writeTriangle(tri);
    }
    ++stepId;
  }

  void report_Step3_Interior_Exterior(const tre::chunkVector<s_tetrahedron, 128> &allTetras, const char *label, const bool withDetails = false)
  {
    if (!withDetails) rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Interior_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && t.metaSideInterior() && !t.metaSideExterior())
      {
        if (!withDetails) rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        else              rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Interior_" << label << "_T" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    if (!withDetails) rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Exterior_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && !t.metaSideInterior() && t.metaSideExterior())
      {
        if (!withDetails) rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        else              rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Exterior_" << label << "_T" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Both_" << label << std::endl; // no details for bad ones
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && t.metaSideInterior() && t.metaSideExterior())
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_None_" << label << std::endl; // no details for bad ones
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && !t.metaSideInterior() && !t.metaSideExterior())
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    ++stepId;
  }
};

#else // MESH_DEBUG

struct s_tetraReporter
{
  s_tetraReporter(const char *) {}

  void report_Step1_Fail(const std::vector<s_tetrahedron*>&, const s_surface*, const glm::vec3&, uint) {}
  void report_Step2(const std::vector<s_tetrahedron*>&, const glm::vec3&, const glm::vec3&, const char *) {}
  void report_Step2(const std::vector<s_tetrahedron*>&, const s_meshTriangle&, const char*) {}
  void report_Step3_Interior_Exterior(const tre::chunkVector<s_tetrahedron, 128>&, const char*, const bool withDetails = false) {}
};

#endif // MESH_DEBUG

// ----------------------------------------------------------------------------

bool tetrahedralize(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<uint> &listTetrahedrons,
                    std::function<void(float progress)> *progressNotifier)
{
  if (progressNotifier != nullptr) (*progressNotifier)(0.f);

  if (part.m_size < 4) return false;

  s_tetraReporter exporter("tetrahedralization.obj");

  GLuint                                *indicesData = layout.m_index.getPointer(part.m_offset);
  const s_modelDataLayout::s_vertexData &inPos = layout.m_positions;
  const s_modelDataLayout::s_vertexData &inNormal = layout.m_normals;

  // compute the list of points (index list)

  std::vector<uint> indices;
  indices.resize(part.m_size);
  memcpy(indices.data(), indicesData, sizeof(uint) * part.m_size);
  sortAndUniqueCounting(indices);
  TRE_ASSERT(!indices.empty());

  // compute b-box

  const glm::vec3 boxExtend = part.m_bbox.extend();
  const float     boxVolume = boxExtend.x * boxExtend.y * boxExtend.z;
  if (boxVolume == 0.f) return false;
  const glm::vec3 boxMin = part.m_bbox.m_min - 0.4f * boxExtend;
  const glm::vec3 boxMax = part.m_bbox.m_max + 0.4f * boxExtend;

  // compute minimal volume allowed for the tetrahedron (more precisely, the min radius of the inscribed sphere)

  std::vector<s_meshTriangle> inTriangles;
  inTriangles.reserve(part.m_size / 3);

  float minEdgeLength = std::numeric_limits<float>::infinity();
  float minSurfaceAera = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < part.m_size; i += 3)
  {
    const glm::vec3 &v0 = inPos.get<glm::vec3>(indicesData[i + 0]);
    const glm::vec3 &v1 = inPos.get<glm::vec3>(indicesData[i + 1]);
    const glm::vec3 &v2 = inPos.get<glm::vec3>(indicesData[i + 2]);
    const glm::vec3 e01 = v1 - v0;
    const glm::vec3 e02 = v2 - v0;
    const glm::vec3 e12 = v2 - v1;
    const glm::vec3 n = glm::cross(e01, e02);
    const float nSign = (inNormal.hasData() && glm::dot(n, inNormal.get<glm::vec3>(indicesData[i + 0]) + inNormal.get<glm::vec3>(indicesData[i + 1]) + inNormal.get<glm::vec3>(indicesData[i + 2])) < 0.f) ? -1.f : 1.f;
    const float     a = 0.5f * glm::length(n);
    if (a < 1.e-7f) continue; // degenerated triangle
    inTriangles.emplace_back(&v0, &v1, &v2,  nSign * n);
    minSurfaceAera = std::min(minSurfaceAera, a);
    const float     l01 = glm::length(e01);
    const float     l02 = glm::length(e02);
    const float     l12 = glm::length(e12);
    const float     l = std::min(std::min(l01, l02), l12);
    minEdgeLength = std::min(minEdgeLength, l);
  }
  const float tetraMinVolumeAA = minSurfaceAera * minEdgeLength / 3.f;
  const float tetraMinVolume = glm::max(boxVolume / indices.size() * 1.e-3f, 1.e-7f /* fp-precision limit */); // TODO !!!

  if (progressNotifier != nullptr) (*progressNotifier)(0.1f);

  const std::size_t indicesInitialSize = indices.size();
  TRE_LOG("tetrahedralize: will process " << indicesInitialSize << " points (over a part of " << part.m_size / 3 << " triangles)");

  // index remapper

  std::vector<uint> indexRemapper;
  uint              indexRemapperOffset = indices[0];
  {
    uint indexMax = indexRemapperOffset;
    for (uint ind : indices)
    {
      indexRemapperOffset = glm::min(indexRemapperOffset, ind);
      indexMax = glm::max(indexMax, ind);
    }
    indexRemapper.resize(indexMax + 1 - indexRemapperOffset);
  }
  for (uint i = 0; i < indexRemapper.size(); ++i) indexRemapper[i] = indexRemapperOffset + i;

  // working data

  chunkVector<s_tetrahedron, 128> listTetra; // the data must not be re-located because the algo works with pointers.

  // generate the "master"-cube (5 tetrahedrons)

  const glm::vec3 boxAAA = boxMin;                                   // tetra-vertices: 0A
  const glm::vec3 boxABA = glm::vec3(boxMin.x, boxMax.y, boxMin.z);  // tetra-vertices: 0D 1D    3D 4D
  const glm::vec3 boxBAA = glm::vec3(boxMax.x, boxMin.y, boxMin.z);  // tetra-vertices: 0B 1B 2B    4A
  const glm::vec3 boxBBA = glm::vec3(boxMax.x, boxMax.y, boxMin.z);  // tetra-vertices:    1A
  const glm::vec3 boxAAB = glm::vec3(boxMin.x, boxMin.y, boxMax.z);  // tetra-vertices: 0C    2C 3C 4C
  const glm::vec3 boxABB = glm::vec3(boxMin.x, boxMax.y, boxMax.z);  // tetra-vertices:          3A
  const glm::vec3 boxBAB = glm::vec3(boxMax.x, boxMin.y, boxMax.z);  // tetra-vertices:       2A
  const glm::vec3 boxBBB = boxMax;                                   // tetra-vertices:    1C 2D 3B 4B

  listTetra.resize(5);
  listTetra[0] = s_tetrahedron(&boxAAA, &boxBAA, &boxAAB, &boxABA, &listTetra[4], nullptr, nullptr, nullptr);
  listTetra[1] = s_tetrahedron(&boxBBA, &boxBAA, &boxBBB, &boxABA, &listTetra[4], nullptr, nullptr, nullptr);
  listTetra[2] = s_tetrahedron(&boxBAB, &boxBAA, &boxAAB, &boxBBB, &listTetra[4], nullptr, nullptr, nullptr);
  listTetra[3] = s_tetrahedron(&boxABB, &boxBBB, &boxAAB, &boxABA, &listTetra[4], nullptr, nullptr, nullptr);
  listTetra[4] = s_tetrahedron(&boxBAA, &boxBBB, &boxAAB, &boxABA, &listTetra[3], &listTetra[0], &listTetra[1], &listTetra[2]);

#define _indP(_p) uint(reinterpret_cast<const float*>(_p) - inPos.m_data) / inPos.m_stride

  // main-step 1: Delaunay triangulation (based on Bowyer-Watson algorithm) without surface-constrain

  std::vector<s_tetrahedron*> listTetraToProcess;
  std::vector<s_tetrahedron*> listTetraNew;
  std::vector<s_surface>      listSurface;

  uint indicesFailedCount = 0;

  while (!indices.empty())
  {
    // get a point to add
    const glm::vec3 *pt = nullptr;

    {
      // -> loop over existing tetrahedra, and get the biggest with points inside.
      s_tetrahedron *tBest = nullptr;
      for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
      {
        s_tetrahedron &t = listTetra[iT];
        if (!t.metaContainsPoint()) continue;
        if (tBest == nullptr || tBest->volume < t.volume) tBest = &t;
      }
      if (tBest == nullptr) break; // no tetra ?? exit.

      // -> loop over points, and get the best one
      std::size_t      iptBest = std::size_t(-1);
      const glm::vec3 *ptBest = nullptr;
      float            d2Best = std::numeric_limits<float>::infinity();
      bool             tHasPoint = false;

      const glm::vec3  tetraCenter = 0.25f * (*tBest->ptA + *tBest->ptB + *tBest->ptC + *tBest->ptD);
      const glm::mat3  transf = glm::mat3(*tBest->ptB - *tBest->ptA, *tBest->ptC - *tBest->ptA, *tBest->ptD - *tBest->ptA); // copy from s_contact3D::point_treta
      TRE_ASSERT(fabsf(glm::determinant(transf)) > 1.e-12f); // fast but unsafe if the tetrahedron is degenerated.
      const glm::mat3  transfInv = glm::inverse(transf);

      for (std::size_t iP = 0; iP < indices.size(); ++iP)
      {
        const glm::vec3 *p = &inPos.get<glm::vec3>(indices[iP]);
        const float     d2 = glm::length2(tetraCenter - *p);
        const bool      isBetter = (d2 < d2Best);
        if (tHasPoint && !isBetter) continue;
        const glm::vec3 coordUVW = transfInv * (*p - *tBest->ptA);
        const bool      isInside = !((glm::any(glm::lessThan(coordUVW, glm::vec3(0.f))) || (coordUVW.x + coordUVW.y + coordUVW.z) > 1.f));
        tHasPoint |= isInside;
        if (isBetter && isInside)
        {
          iptBest = iP;
          ptBest = p;
          d2Best = d2;
        }
      }
      if (!tHasPoint)
      {
        tBest->metaSetNoMorePoints();
        continue; // go to the next iteration
      }

      // ok
      TRE_ASSERT(std::isfinite(d2Best));
      listTetraToProcess.clear();
      listTetraToProcess.push_back(tBest);
      pt = ptBest;
      indices[iptBest] = indices.back(); indices.pop_back(); // remove the index from the list
    }

    if (progressNotifier != nullptr) (*progressNotifier)(0.1f + 0.5f * (1.f - float(indices.size())/float(indicesInitialSize)));

    TRE_ASSERT(listTetraToProcess.size() == 1);
    TRE_ASSERT(listTetraToProcess[0]->pointInCircumsphere(*pt));

    // -> prepare the insertion geometry
    s_tetrahedron &tRoot = *listTetraToProcess[0];
    // pt on vertex, edge or surface ?
    const bool onSurfABC = std::abs(s_surface(tRoot.ptA, tRoot.ptB, tRoot.ptC, nullptr, *tRoot.ptD).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfABD = std::abs(s_surface(tRoot.ptA, tRoot.ptB, tRoot.ptD, nullptr, *tRoot.ptC).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfACD = std::abs(s_surface(tRoot.ptA, tRoot.ptC, tRoot.ptD, nullptr, *tRoot.ptB).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfBCD = std::abs(s_surface(tRoot.ptB, tRoot.ptC, tRoot.ptD, nullptr, *tRoot.ptA).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const uint onSurfCount = onSurfABC + onSurfABD + onSurfACD + onSurfBCD;
    if (onSurfCount >= 3) // pt on vertex
    {
      const uint iptA = _indP(tRoot.ptA);
      const uint iptB = _indP(tRoot.ptB);
      const uint iptC = _indP(tRoot.ptC);
      const uint iptD = _indP(tRoot.ptD);
      if (onSurfABC && onSurfABD && onSurfACD && iptA < layout.m_vertexCount) indexRemapper[_indP(pt) - indexRemapperOffset] = iptA;
      if (onSurfABC && onSurfABD && onSurfBCD && iptB < layout.m_vertexCount) indexRemapper[_indP(pt) - indexRemapperOffset] = iptB;
      if (onSurfABC && onSurfACD && onSurfBCD && iptC < layout.m_vertexCount) indexRemapper[_indP(pt) - indexRemapperOffset] = iptC;
      if (onSurfABD && onSurfACD && onSurfBCD && iptD < layout.m_vertexCount) indexRemapper[_indP(pt) - indexRemapperOffset] = iptD;
      continue; // duplicated point, go to the next point
    }
    else if (onSurfCount == 2) // pt on edge
    {
      const glm::vec3 *ptEdge1 = nullptr;
      const glm::vec3 *ptEdge2 = nullptr;
      s_tetrahedron   *tNext = nullptr;
      if (onSurfABC && onSurfABD)
      {
        ptEdge1 = tRoot.ptA;
        ptEdge2 = tRoot.ptB;
        tNext = tRoot.adjABC;
      }
      else if (onSurfABC && onSurfACD)
      {
        ptEdge1 = tRoot.ptA;
        ptEdge2 = tRoot.ptC;
        tNext = tRoot.adjABC;
      }
      else if (onSurfABD && onSurfACD)
      {
        ptEdge1 = tRoot.ptA;
        ptEdge2 = tRoot.ptD;
        tNext = tRoot.adjABD;
      }
      else if (onSurfABC && onSurfBCD)
      {
        ptEdge1 = tRoot.ptB;
        ptEdge2 = tRoot.ptC;
        tNext = tRoot.adjABC;
      }
      else if (onSurfABD && onSurfBCD)
      {
        ptEdge1 = tRoot.ptB;
        ptEdge2 = tRoot.ptD;
        tNext = tRoot.adjABD;
      }
      else /* (onSurfACD && onSurfBCD) */
      {
        ptEdge1 = tRoot.ptC;
        ptEdge2 = tRoot.ptD;
        tNext = tRoot.adjACD;
      }
      TRE_ASSERT(tNext != nullptr);
      listTetraToProcess.push_back(tNext);
      _walkTetrahedronOnEdge(listTetraToProcess, ptEdge1, ptEdge2);
    }
    else if (onSurfCount == 1) // pt on surface
    {
      if      (onSurfABC)   listTetraToProcess.push_back(tRoot.adjABC);
      else if (onSurfABD)   listTetraToProcess.push_back(tRoot.adjABD);
      else if (onSurfACD)   listTetraToProcess.push_back(tRoot.adjACD);
      else  /*(onSurfBCD)*/ listTetraToProcess.push_back(tRoot.adjBCD);
      TRE_ASSERT(listTetraToProcess.back() != nullptr);
    }

    // -> generate the insertion-geometry (add tetra by checking for neighbors recursively)
    std::size_t queueStart = 0;
    std::size_t queueEnd = listTetraToProcess.size();
    while (queueStart < queueEnd)
    {
      for (std::size_t i = queueStart; i < queueEnd; ++i)
      {
        s_tetrahedron &t = *listTetraToProcess[i];
        s_tetrahedron *tadjABC = t.adjABC;
        s_tetrahedron *tadjABD = t.adjABD;
        s_tetrahedron *tadjACD = t.adjACD;
        s_tetrahedron *tadjBCD = t.adjBCD;
        for (s_tetrahedron *tbis : listTetraToProcess)
        {
          if      (tbis == tadjABC) tadjABC = nullptr;
          else if (tbis == tadjABD) tadjABD = nullptr;
          else if (tbis == tadjACD) tadjACD = nullptr;
          else if (tbis == tadjBCD) tadjBCD = nullptr;
        }
        if (tadjABC != nullptr && tadjABC->pointInCircumsphere(*pt) && _checkInsertionValidity(listTetraToProcess, *tadjABC, *pt, tetraMinVolume))
          listTetraToProcess.push_back(tadjABC);
        if (tadjABD != nullptr && tadjABD->pointInCircumsphere(*pt) && _checkInsertionValidity(listTetraToProcess, *tadjABD, *pt, tetraMinVolume))
          listTetraToProcess.push_back(tadjABD);
        if (tadjACD != nullptr && tadjACD->pointInCircumsphere(*pt) && _checkInsertionValidity(listTetraToProcess, *tadjACD, *pt, tetraMinVolume))
          listTetraToProcess.push_back(tadjACD);
        if (tadjBCD != nullptr && tadjBCD->pointInCircumsphere(*pt) && _checkInsertionValidity(listTetraToProcess, *tadjBCD, *pt, tetraMinVolume))
          listTetraToProcess.push_back(tadjBCD);
      }
      queueStart = queueEnd;
      queueEnd = listTetraToProcess.size();
    }

    listSurface.clear();
    s_surface::generateOutSurface(listTetraToProcess, listSurface);

    // -> final check of the insertion-geometry
    bool isInsertionGeomValid = true;
    for (const s_surface &s : listSurface)
    {
      if (s.volumeSignedWithPoint(*pt) > -tetraMinVolume)
      {
        isInsertionGeomValid = false;
        exporter.report_Step1_Fail(listTetraToProcess, &s, *pt, onSurfCount);
        break;
      }
    }
    if (!isInsertionGeomValid)
    {
      indexRemapper[_indP(pt) - indexRemapperOffset] = uint(-1);
      ++indicesFailedCount;
      continue; // insertion failed with this point
    }

    s_surface::computeNeighbors(listSurface);

    const std::size_t nOldTetra = listTetraToProcess.size();
    const std::size_t nSurf = listSurface.size();

    // -> compute pointers in which new tetrahedra will be created
    TRE_ASSERT(listTetraNew.empty());
    listTetraNew.resize(nSurf, nullptr);
    {
      const std::size_t m = std::min(nSurf, nOldTetra);
      const std::size_t listTetraPrevSize = listTetra.size();
      listTetra.resize(listTetraPrevSize + nSurf - m);
      for (std::size_t iTnew = 0; iTnew < m; ++iTnew)
        listTetraNew[iTnew] = listTetraToProcess[iTnew];
      for (std::size_t iTnew = m, iTmain = listTetraPrevSize; iTnew < nSurf; ++iTnew, ++iTmain)
        listTetraNew[iTnew] = &listTetra[iTmain];
    }

    for (s_tetrahedron *t : listTetraToProcess) *t = s_tetrahedron();
    listTetraToProcess.clear();

    // -> generate new tetrahedra, 1 per surface
    for (std::size_t iS = 0; iS < nSurf; ++iS)
    {
      const s_surface &surf = listSurface[iS];
      const uint surfInd_12 = uint(surf.surf12 - listSurface.data());
      const uint surfInd_23 = uint(surf.surf23 - listSurface.data());
      const uint surfInd_31 = uint(surf.surf13 - listSurface.data());
      *listTetraNew[iS] = s_tetrahedron(pt, surf.pt1, surf.pt2, surf.pt3, surf.tetraExterior, listTetraNew[surfInd_23], listTetraNew[surfInd_31], listTetraNew[surfInd_12]);
      if (surf.tetraExterior != nullptr) surf.tetraExterior->setNeigbourTetra(surf.pt1, surf.pt2, surf.pt3, listTetraNew[iS]);
    }

    for (std::size_t iS = 0; iS < nSurf; ++iS)
    {
      TRE_ASSERT(listTetraNew[iS]->valid());
    }

    listSurface.clear();
    listTetraNew.clear();

    if (nSurf < nOldTetra)
    {
      // re-pack "listTetra"
      for (std::size_t iT = listTetra.size(); iT-- != 0; )
      {
        if (!listTetra[iT].valid())
        {
          const std::size_t jT = listTetra.size() - 1;
          if (iT != jT)
          {
            // move listTetra[jT] into listTetra[iT]
            listTetra[iT] = listTetra[jT];
            if (listTetra[iT].adjABC != nullptr) listTetra[iT].adjABC->replaceNeigbourTetra(&listTetra[jT], &listTetra[iT]);
            if (listTetra[iT].adjABD != nullptr) listTetra[iT].adjABD->replaceNeigbourTetra(&listTetra[jT], &listTetra[iT]);
            if (listTetra[iT].adjACD != nullptr) listTetra[iT].adjACD->replaceNeigbourTetra(&listTetra[jT], &listTetra[iT]);
            if (listTetra[iT].adjBCD != nullptr) listTetra[iT].adjBCD->replaceNeigbourTetra(&listTetra[jT], &listTetra[iT]);
          }
          listTetra.resize(jT);
        }
      }
    }
  }

  if (!indices.empty())
  {
    for (uint ind : indices) exporter.report_Step1_Fail({}, nullptr, inPos.get<glm::vec3>(ind), 99);
    for (uint ind : indices) indexRemapper[ind - indexRemapperOffset] = uint(-1);
    indicesFailedCount += indices.size();
    indices.clear();
  }

  for (std::size_t iS = 0; iS < listTetra.size(); ++iS)
  {
    TRE_ASSERT(listTetra[iS].valid());
  }

  TRE_LOG("tetrahedralize: main-step 1: there are " << indicesFailedCount << " insertion-fails, over " << indicesInitialSize << " vertices");

  // main-step 2: apply the surface-constrain (enforce there is existing surface in the tetrahedralization for each mesh's triangle)

  const std::size_t Ntriangles = inTriangles.size();
  std::size_t       NtrianglesDirectMatch = 0;

  for (std::size_t jT = 0; jT < Ntriangles; ++jT)
  {
    if (progressNotifier != nullptr) (*progressNotifier)(0.6f + 0.1f * float(jT)/float(Ntriangles-1));

    s_meshTriangle &tri = inTriangles[jT];

    const uint indF_raw = _indP(tri.ptF);
    const uint indG_raw = _indP(tri.ptG);
    const uint indH_raw = _indP(tri.ptH);

    const uint indF = indexRemapper[indF_raw - indexRemapperOffset];
    const uint indG = indexRemapper[indG_raw - indexRemapperOffset];
    const uint indH = indexRemapper[indH_raw - indexRemapperOffset];

    if (indF != uint(-1)) tri.ptF = &inPos.get<glm::vec3>(indF);
    if (indG != uint(-1)) tri.ptG = &inPos.get<glm::vec3>(indG);
    if (indH != uint(-1)) tri.ptH = &inPos.get<glm::vec3>(indH);

    if (((indF | indG | indH) == uint(-1)) || (indF == indG) || (indF == indH) || (indG == indH))
      continue; // the mesh's triangle cannot be reconstructed. Skip it.

    uint keyBest = 0;
    for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
    {
      s_tetrahedron &t = listTetra[iT];
      TRE_ASSERT(t.valid());
      const uint key = t.hasPoint(tri.ptF) + t.hasPoint(tri.ptG) + t.hasPoint(tri.ptH);
      if (key > keyBest)
      {
        tri.tetraNearby = &t;
        keyBest = key;
        if (key == 3) break;
      }
    }

    if (keyBest == 3)
    {
      ++NtrianglesDirectMatch;
      continue; // good!
    }

    if (tri.tetraNearby == nullptr)
    {
      exporter.report_Step2({}, tri, "NotReached");
      continue;
    }

    // try to reconstruct the missing edges

    for (int iEdge = 0; iEdge < 3; ++iEdge)
    {
      const glm::vec3 *pt1 = (iEdge < 2) ? tri.ptF : tri.ptG;
      const glm::vec3* pt2 = (iEdge < 1) ? tri.ptG : tri.ptH;
      TRE_ASSERT(pt1 != pt2);
      {
        const bool hasP1 = tri.tetraNearby->hasPoint(pt1);
        const bool hasP2 = tri.tetraNearby->hasPoint(pt2);
        if (hasP1 && hasP2) continue; // edge exists
        if (!hasP1 && !hasP2) continue; // bad, none of both points belongs to the tetra-nearby.
        if (hasP2) std::swap(pt1, pt2);
      }
      TRE_ASSERT(tri.tetraNearby->hasPoint(pt1));
      // list tetra around "pt1"
      listTetraToProcess.clear();
      listTetraToProcess.push_back(tri.tetraNearby);
      {
        std::size_t queueStart = 0;
        std::size_t queueEnd   = 1;
        while (queueStart < queueEnd)
        {
          for (std::size_t i = queueStart; i < queueEnd; ++i)
          {
            s_tetrahedron &t = *listTetraToProcess[i];
            s_tetrahedron *tadjABC = t.adjABC;
            s_tetrahedron *tadjABD = t.adjABD;
            s_tetrahedron *tadjACD = t.adjACD;
            s_tetrahedron *tadjBCD = t.adjBCD;
            for (s_tetrahedron *tbis : listTetraToProcess)
            {
              if      (tbis == tadjABC) tadjABC = nullptr;
              else if (tbis == tadjABD) tadjABD = nullptr;
              else if (tbis == tadjACD) tadjACD = nullptr;
              else if (tbis == tadjBCD) tadjBCD = nullptr;
            }
            if (tadjABC != nullptr && (tadjABC->hasPoint(pt1))) listTetraToProcess.push_back(tadjABC);
            if (tadjABD != nullptr && (tadjABD->hasPoint(pt1))) listTetraToProcess.push_back(tadjABD);
            if (tadjACD != nullptr && (tadjACD->hasPoint(pt1))) listTetraToProcess.push_back(tadjACD);
            if (tadjBCD != nullptr && (tadjBCD->hasPoint(pt1))) listTetraToProcess.push_back(tadjBCD);
          }
          queueStart = queueEnd;
          queueEnd = listTetraToProcess.size();
        }
      }
      // pre-check: the dege exist in another tetra
      bool edgeConstructed = false;
      for (s_tetrahedron *t1 : listTetraToProcess)
      {
        if (t1->hasPoint(pt2))
        {
          exporter.report_Step2({tri.tetraNearby, t1}, tri, "EdgeEXISTinOther");
          exporter.report_Step2(listTetraToProcess, *pt1, *pt2, "EdgeEXISTinOtherBIS");
          // TODO: try to do something!
          edgeConstructed = true;
          break; // loop on tetra
        }
      }
      if (edgeConstructed) continue; // loop on edges
      // simple case: - the new-edge is crossing only 1 surface on its interior (split 2 tetrahedrons into 3)
      //              - the new-edge is crossing only 1 surface on 1 of its edge (swap the edge from a 4-tetrahedrons dual-pyramid)
      //              - the new-edge is crossing only 1 surface on 1 of its vertex (resolve the edge that existing but split into 2 edges)
      for (s_tetrahedron *t1 : listTetraToProcess)
      {
        s_tetrahedron *t2 = nullptr;
        if (t1->ptA == pt1 && t1->adjBCD != nullptr && t1->adjBCD->hasPoint(pt2)) t2 = t1->adjBCD;
        if (t1->ptB == pt1 && t1->adjACD != nullptr && t1->adjACD->hasPoint(pt2)) t2 = t1->adjACD;
        if (t1->ptC == pt1 && t1->adjABD != nullptr && t1->adjABD->hasPoint(pt2)) t2 = t1->adjABD;
        if (t1->ptD == pt1 && t1->adjABC != nullptr && t1->adjABC->hasPoint(pt2)) t2 = t1->adjABC;
        if (t2 == nullptr) continue;
        s_tetrahedron::conformSharedPoints(*t1, *t2);
        TRE_ASSERT(t1->ptA == pt1 && t2->ptA == pt2);
        const float vtBC = s_surface(t1->ptA, t1->ptB, t1->ptC, nullptr, *t1->ptD).volumeSignedWithPoint(*t2->ptA);
        const float vtCD = s_surface(t1->ptA, t1->ptC, t1->ptD, nullptr, *t1->ptB).volumeSignedWithPoint(*t2->ptA);
        const float vtDB = s_surface(t1->ptA, t1->ptD, t1->ptB, nullptr, *t1->ptC).volumeSignedWithPoint(*t2->ptA);
        const bool interiorBC = vtBC < -tetraMinVolume;
        const bool interiorCD = vtCD < -tetraMinVolume;
        const bool interiorDB = vtDB < -tetraMinVolume;
        const int  interiorCount = interiorBC + interiorCD + interiorDB;
        const bool onBC = std::abs(vtBC) <= tetraMinVolume;
        const bool onCD = std::abs(vtCD) <= tetraMinVolume;
        const bool onDB = std::abs(vtDB) <= tetraMinVolume;
        if (interiorCount == 3)
        {
          listTetra.push_back(s_tetrahedron());
          s_tetrahedron *t3 = &listTetra[listTetra.size()-1];
          s_tetrahedron::split_2tetras_to_3tetras(*t1, *t2, *t3);
          listTetraToProcess.push_back(t3);
          exporter.report_Step2({t1, t2, t3}, *pt1, *pt2, "EdgeCREATEDSplit");
          edgeConstructed = true;
          break; // loop on tetra
        }
        if (interiorCount == 2)
        {
          s_tetrahedron *tR1 = nullptr, *tR2 = nullptr, *tS1 = nullptr, *tS2 = nullptr;
          tR1 = t1;
          tR2 = t2;
          if (onBC && t1->adjABC != nullptr && t2->adjABC != nullptr && t1->adjABC->hasAdjacant(t2->adjABC)) { tS1 = t1->adjABC; tS2 = t2->adjABC; }
          if (onCD && t1->adjABD != nullptr && t2->adjABD != nullptr && t1->adjABD->hasAdjacant(t2->adjABD)) { tS1 = t1->adjABD; tS2 = t2->adjABD; }
          if (onCD && t1->adjACD != nullptr && t2->adjACD != nullptr && t1->adjACD->hasAdjacant(t2->adjACD)) { tS1 = t1->adjACD; tS2 = t2->adjACD; }
          if (tS1 != nullptr)
          {
            s_tetrahedron::conformSharedPoints(*tS1, *tS2);
            s_tetrahedron::swap_Pyramid(*tR1, *tR2, *tS1, *tS2); // Note: an edge will replace another, but the removed edge may belong to the origin geom. ?!?
            exporter.report_Step2({tR1, tR2, tS1, tS2}, *pt1, *pt2, "EdgeCREATEDSwap");
            edgeConstructed = true;
            break; // loop on tetra
          }
        }
        if (onBC + onCD + onDB >= 2)
        {
          exporter.report_Step2({t1, t2}, *pt1, *pt2, "EdgeFAILEDVertex");
          // TODO ...
        }
      }
      if (edgeConstructed) continue; // loop on edges
      // Fall-back case: create (temporary) points living on the edge.
      // TODO ...
      //exporter.report_Step2(listTetraToProcess, *pt1, *pt2, "EdgeFAILEDInsert"); // TMP
    } // loop on edges
  }

  for (std::size_t iS = 0; iS < listTetra.size(); ++iS)
  {
    TRE_ASSERT(listTetra[iS].valid());
  }

  TRE_LOG("tetrahedralize: main-step 2: there are " << Ntriangles - NtrianglesDirectMatch << " triangles that do not exist from the raw tetrahedrization, over " << Ntriangles << " triangles.");

  // main-step 3: compute the interior and exterior tetrahedrons

  std::size_t NtrianglesNoSide = 0;

  for (std::size_t jT = 0; jT < Ntriangles; ++jT)
  {
    if (progressNotifier != nullptr) (*progressNotifier)(0.7f + 0.1f * float(jT)/float(Ntriangles-1));

    s_meshTriangle &tri = inTriangles[jT];

    if (tri.tetraNearby == nullptr) continue; // Ignore because degenerated or invalid.

    s_tetrahedron &t = *tri.tetraNearby;

    // esay case, the triangle exists in the tetrahedralization
    {
      const bool    hasA = tri.hasPoint(t.ptA);
      const bool    hasB = tri.hasPoint(t.ptB);
      const bool    hasC = tri.hasPoint(t.ptC);
      const bool    hasD = tri.hasPoint(t.ptD);
      if (hasA + hasB + hasC + hasD == 3)
      {
        bool           tInterior = true;
        s_tetrahedron *tSide = nullptr;
        if (!hasA)
        {
          tInterior = glm::dot(s_surface(t.ptB, t.ptC, t.ptD, nullptr, *t.ptA).normalOut, tri.normalOut) > 0.f;
          tSide = t.adjBCD;
        }
        else if (!hasB)
        {
          tInterior = glm::dot(s_surface(t.ptA, t.ptC, t.ptD, nullptr, *t.ptB).normalOut, tri.normalOut) > 0.f;
          tSide = t.adjACD;
        }
        else if (!hasC)
        {
          tInterior = glm::dot(s_surface(t.ptA, t.ptB, t.ptD, nullptr, *t.ptC).normalOut, tri.normalOut) > 0.f;
          tSide =t.adjABD;
        }
        else //(!hasD)
        {
          tInterior = glm::dot(s_surface(t.ptA, t.ptB, t.ptC, nullptr, *t.ptD).normalOut, tri.normalOut) > 0.f;
          tSide = t.adjABC;
        }
        if (tInterior)
        {
          t.metaSetSideInterior();
          if (tSide != nullptr) tSide->metaSetSideExterior();
        }
        else
        {
          t.metaSetSideExterior();
          if (tSide != nullptr) tSide->metaSetSideInterior();
        }
        continue; // we're good. Go to the next triangle.
      }
    }

    // try to fit the triangle with one of the tetrahedron's surface.
    // "t" is already the best candidate tetrahedron.
    {
      const float     triArea2 = glm::length(tri.normalOut);
      const glm::vec3 triOutNormal = glm::normalize(tri.normalOut);
      TRE_ASSERT(!glm::any(glm::isnan(triOutNormal)));
      const float distC = 6.f * tetraMinVolume / triArea2;
      const float ptAn = glm::dot(*t.ptA - *tri.ptF, triOutNormal);
      const float ptBn = glm::dot(*t.ptB - *tri.ptF, triOutNormal);
      const float ptCn = glm::dot(*t.ptC - *tri.ptF, triOutNormal);
      const float ptDn = glm::dot(*t.ptD - *tri.ptF, triOutNormal);
      const bool  ptAontri = std::abs(ptAn) < distC;
      const bool  ptBontri = std::abs(ptBn) < distC;
      const bool  ptContri = std::abs(ptCn) < distC;
      const bool  ptDontri = std::abs(ptDn) < distC;
      const int   ptsontri = ptAontri + ptBontri + ptContri + ptDontri;
      if (ptsontri == 4)
      {
        exporter.report_Step2({&t}, tri, "FALIED_4ptsOnTriangle");
      }
      else if (ptsontri == 3)
      {
        bool side = false;
        s_tetrahedron *tSide = nullptr;
        if (!ptAontri) { side = ptAn > 0.f; tSide = t.adjBCD; }
        if (!ptBontri) { side = ptBn > 0.f; tSide = t.adjACD; }
        if (!ptContri) { side = ptCn > 0.f; tSide = t.adjABD; }
        if (!ptDontri) { side = ptDn > 0.f; tSide = t.adjABC; }
        if (!side)
        {
          t.metaSetSideInterior();
          if (tSide != nullptr) tSide->metaSetSideExterior();
        }
        else
        {
          t.metaSetSideExterior();
          if (tSide != nullptr) tSide->metaSetSideInterior();
        }
        continue; // we're good. Go to the next triangle.
      }
      else
      {
        static const char* kNPt[3] = { "FALIED_0ptsOnTriangle", "FALIED_1ptsOnTriangle", "FALIED_2ptsOnTriangle" };
        exporter.report_Step2({&t}, tri, kNPt[ptsontri]);
      }
    }

    ++NtrianglesNoSide;
  }

  exporter.report_Step3_Interior_Exterior(listTetra, "Direct", true);

  TRE_LOG("tetrahedralize: main-step 3: there are " << NtrianglesNoSide << " triangles that do not define the interior/exterior volume of the mesh, over " << Ntriangles << " triangles.");

  if (progressNotifier != nullptr) (*progressNotifier)(0.8f);

  // -> flag remaining tetras (recursive walk from neighbors)
  while (true)
  {
    bool hasUpdate = false;
    for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
    {
      s_tetrahedron &t = listTetra[iT];
      if (!t.metaSideInterior() && !t.metaSideExterior())
      {
        uint nInterior = 0;
        uint nExterior = 0;
        if (t.adjABC != nullptr) { nInterior += t.adjABC->metaSideInterior(); nExterior += t.adjABC->metaSideExterior(); }
        if (t.adjABD != nullptr) { nInterior += t.adjABD->metaSideInterior(); nExterior += t.adjABD->metaSideExterior(); }
        if (t.adjACD != nullptr) { nInterior += t.adjACD->metaSideInterior(); nExterior += t.adjACD->metaSideExterior(); }
        if (t.adjBCD != nullptr) { nInterior += t.adjBCD->metaSideInterior(); nExterior += t.adjBCD->metaSideExterior(); }
        if (nInterior > nExterior) t.metaSetSideInterior();
        if (nInterior < nExterior) t.metaSetSideExterior();
        hasUpdate |= (nInterior != nExterior);
      }
    }
    if (!hasUpdate) break;
  }

  exporter.report_Step3_Interior_Exterior(listTetra, "AfterSpreading");

  // final-step: compute output tetrahedra-list

  std::size_t NtetrasRemaining  = 0;
  bool        hasDeletedInterior = false;

  listTetrahedrons.reserve(listTetrahedrons.size() + listTetra.size() * 4);
  for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
  {
    const s_tetrahedron &t = listTetra[iT];
    if (!t.metaSideInterior() && !t.metaSideExterior()) ++NtetrasRemaining;
    const bool hasValidVertices = _indP(t.ptA) < layout.m_vertexCount &&
                                  _indP(t.ptB) < layout.m_vertexCount &&
                                  _indP(t.ptC) < layout.m_vertexCount &&
                                  _indP(t.ptD) < layout.m_vertexCount;
    hasDeletedInterior |= !hasValidVertices && t.metaSideInterior();
    if (hasValidVertices && !t.metaSideExterior())
    {
      listTetrahedrons.push_back(_indP(t.ptA));
      listTetrahedrons.push_back(_indP(t.ptB));
      listTetrahedrons.push_back(_indP(t.ptC));
      listTetrahedrons.push_back(_indP(t.ptD));
    }
  }

#undef _indP

  if (NtetrasRemaining != 0)
  {
    TRE_LOG("tetrahedralize: final-step: there are " << NtetrasRemaining << " un-flagged tetrahedrons.");
  }
  if (hasDeletedInterior)
  {
    TRE_LOG("tetrahedralize: final-step: input mesh does not seem to be closed (the interior volume is not well defined).");
  }

  TRE_LOG("tetrahedralize: final-step: ouputs " << listTetrahedrons.size() / 4 << " tetrahedrons from mesh with " << Ntriangles << " triangles.");

  if (progressNotifier != nullptr) (*progressNotifier)(1.f);

  return true;
}

// ============================================================================

} // namespace modelTools

} // namespace tre
