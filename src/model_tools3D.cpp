#include "tre_model_tools.h"

#include "tre_contact_3D.h"

#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/norm.hpp> // for glm::length2()

#include <fstream> // TODO: remove
#include <string>  // TODO: remove
#include <algorithm> // TODO: remove (for the std::find)

namespace tre {

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

void computeConvexeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, const float threshold, std::vector<glm::vec3> &outSkinTri)
{
  TRE_ASSERT(layout.m_vertexCount > 0);
  TRE_ASSERT(layout.m_positions.m_size == 3);

  outSkinTri.clear();

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  const std::size_t end = offset + count;

  const glm::vec3 boxExtend = part.m_bbox.transform(transform).extend();
  const float radiusThreshold = threshold * fmaxf(boxExtend.x, boxExtend.y);
  const float thresholdSquared = radiusThreshold * radiusThreshold;

  // extract points
  std::vector<glm::vec3> points;
  points.resize(count);
  if (layout.m_indexCount == 0)
  {
    for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
    {
      const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec3>(ipt), 1.f); // TODO : naive, not optimized ...
      points[j] = glm::vec3(transform * pt);
    }
  }
  else
  {
    for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
    {
      const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec3>(layout.m_index[ipt]), 1.f); // TODO : naive, not optimized ...
      points[j] = glm::vec3(transform * pt);
    }
  }

  // divide and conquere ...

  TRE_FATAL("not implemented");
}

// ============================================================================

void computeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, std::vector<glm::vec3> &outSkinTri)
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

static void _computeConnectivity_tri2tri(const s_modelDataLayout &data, std::size_t offset, std::size_t count, std::vector<std::vector<uint> > &triNeighbors)
{
  TRE_ASSERT(data.m_indexCount > 0);

  std::vector<std::vector<uint> > vertexToTriangles;
  _computeConnectivity_vert2tri(data, offset, count, vertexToTriangles);

  const std::size_t nTri = count / 3;
  triNeighbors.resize(nTri);

  const std::size_t iend = offset + count;

  for (std::size_t i = offset, iT = 0; i < iend; i += 3, ++iT)
  {
    std::vector<uint>	&triangleNeighbors = triNeighbors[iT];
    triangleNeighbors.clear();
    triangleNeighbors.reserve(3);

    const uint vertexA = data.m_index[i + 0];
    const uint vertexB = data.m_index[i + 1];
    const uint vertexC = data.m_index[i + 2];

    std::vector<uint>	allNeighbors;
    allNeighbors.reserve(vertexToTriangles[vertexA].size() + vertexToTriangles[vertexB].size() + vertexToTriangles[vertexC].size());

    for(uint tri : vertexToTriangles[vertexA])
    {
      if (tri != iT)
        allNeighbors.push_back(tri);
    }
    for(uint tri : vertexToTriangles[vertexB])
    {
      if (tri != iT)
        allNeighbors.push_back(tri);
    }
    for(uint tri : vertexToTriangles[vertexC])
    {
      if (tri != iT)
        allNeighbors.push_back(tri);
    }

    if (allNeighbors.empty())
      continue;

    sortInsertion<uint>(allNeighbors);

    for (std::size_t iN = 0, stop = allNeighbors.size() - 1; iN < stop; ++iN)
    {
      if (allNeighbors[iN] == allNeighbors[iN + 1])
      {
        const uint	neighbor = allNeighbors[iN++];
        triangleNeighbors.push_back(neighbor);
        while (iN < stop && allNeighbors[iN] == neighbor) { ++iN; }
        --iN;
      }
    }
  }
}

// ----------------------------------------------------------------------------

void computeOutNormal(const s_modelDataLayout &layout, const s_partInfo &part)
{
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  TRE_ASSERT(count % 3 == 0);

  // compute data per triangle
  std::vector<glm::vec3> normalPerTri;
  std::vector<glm::vec3> centerPerTri;

  const std::size_t nTriangles = count / 3;
  normalPerTri.resize(nTriangles);
  centerPerTri.resize(nTriangles);

  {
    const std::size_t Sind = offset;
    const std::size_t Eind = Sind + count;

    if (layout.m_indexCount > 0)
    {
      for (std::size_t Iind = Sind, iT = 0; Iind < Eind; Iind += 3, ++iT)
      {
        const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(layout.m_index[Iind    ]);
        const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(layout.m_index[Iind + 1]);
        const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(layout.m_index[Iind + 2]);
        centerPerTri[iT] = (pt0 + pt1 + pt2) / 3.f;
        normalPerTri[iT] = glm::normalize(glm::cross(pt1 - pt0, pt2 - pt0));
      }
    }
    else
    {
      for (std::size_t Iind = Sind, iT = 0; Iind < Eind; Iind += 3, ++iT)
      {
        const glm::vec3 pt0 = layout.m_positions.get<glm::vec3>(Iind    );
        const glm::vec3 pt1 = layout.m_positions.get<glm::vec3>(Iind + 1);
        const glm::vec3 pt2 = layout.m_positions.get<glm::vec3>(Iind + 2);
        centerPerTri[iT] = (pt0 + pt1 + pt2) / 3.f;
        normalPerTri[iT] = glm::normalize(glm::cross(pt1 - pt0, pt2 - pt0));
      }
    }
  }

  {
    // oriente normals (per triangle)
    TRE_FATAL("TODO computeOutNormal");

    // if indexed, compute 1 ray and deduct from connectivity (when posible)
    // if not, compute a 1 of each triange (HEAVY !!)
  }

  // finally, synthetize normals
  if (layout.m_indexCount == 0)
  {
    TRE_ASSERT(offset + count <= layout.m_vertexCount);

    s_modelDataLayout::s_vertexData::iterator<glm::vec3> normalIt = layout.m_normals.begin<glm::vec3>(offset);

    for (std::size_t i = 0, iT = 0; i < count; i += 3, ++iT)
    {
      *normalIt++ = normalPerTri[iT];
      *normalIt++ = normalPerTri[iT];
      *normalIt++ = normalPerTri[iT];
    }
  }
  else
  {
    TRE_ASSERT(offset + count <= layout.m_indexCount);

    std::vector<std::vector<uint> > connectivity;
    _computeConnectivity_vert2tri(layout, offset, count, connectivity);

    s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = layout.m_normals.begin<glm::vec3>(offset);
    s_modelDataLayout::s_vertexData::iterator<glm::vec3> normalIt = layout.m_normals.begin<glm::vec3>(offset);

    for (std::size_t iV=0;iV<layout.m_vertexCount;++iV)
    {
      if (connectivity[iV].empty()) { ++posIt; ++normalIt; continue; }
      // for each vertice, the normal is the weigthed normal of all neighbor triangles
      const glm::vec3 vposition = *posIt++;
      glm::vec3 normal;
      // loop on the triangles
      for (const uint triInd : connectivity[iV])
      {
        const glm::vec3 vVP = centerPerTri[triInd] - vposition;
        const float w = 1.f / (glm::length(vVP) + 1.e-10f);
        normal += w * normalPerTri[triInd];
      }
      *normalIt++ = glm::normalize(normal);
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

std::size_t decimateKeepVertex(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold)
{
  TRE_ASSERT(layout.m_indexCount > 0); // needed for connectivity
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t count = part.m_size;
  const std::size_t offset = part.m_offset;

  TRE_ASSERT(count % 3 == 0);

  if (count == 0 || threshold == 0.f) return count;

  // decimate patterm: star-collapsing
  // collapse the vertex (i) taking account of all triangles {A,B,...}
  // thus, reconstruct the surface {A,B,...} without the vertex (i)

  TRE_LOG("decimate: it may take lot of time ... (Ntri = " << count / 3 << ")");

  // 1. prepare data

  //...

  // 2. initialize loop

  bool continueProcess = true;
  std::size_t Ntri = count / 3;

  std::vector<std::vector<uint> > vertexToTri;
  std::vector<bool>                   triangleProcessed;
  std::vector<uint>               triangleToRemove;

  while(continueProcess)
  {
    TRE_LOG("New step with Ntriangles = " << Ntri);

    // 2.1 prepare data
    triangleProcessed.resize(Ntri);
    std::fill(triangleProcessed.begin(), triangleProcessed.end(), false);

    triangleToRemove.clear();

    uint nvertMoreThan3Triangles = 0; // for debug and stats only
    uint nvertUntouched          = 0; // for debug and stats only
    uint nvertFlatCurvature      = 0; // for debug and stats only
    uint nvertValidStar          = 0; // for debug and stats only

    // 2.2 compute connectivity (from scratch)
    _computeConnectivity_vert2tri(layout, offset, count, vertexToTri);

    // 2.3 loop over vertices
    for (std::size_t ivert = 0; ivert < layout.m_vertexCount; ++ivert)
    {
      const std::vector<uint> &triangles = vertexToTri[ivert];

      // -> ignore stars with less than 3 triangles
      if (triangles.size() < 3)
        continue;
      ++nvertMoreThan3Triangles;

      // -> ignore stars with processed triangle(s)
      bool hasProcessedTri = false;
      for (uint tri : triangles)
        hasProcessedTri |= triangleProcessed[tri];
      if (hasProcessedTri)
        continue;
      ++nvertUntouched;

      // -> Compute star-couples
      std::vector<uint> couples;
      couples.reserve(triangles.size() * 2);
      for (uint tri : triangles)
      {
        const uint v0 = layout.m_index[offset + tri * 3 + 0];
        if (v0 != ivert) couples.push_back(v0);
        const uint v1 = layout.m_index[offset + tri * 3 + 1];
        if (v1 != ivert) couples.push_back(v1);
        const uint v2 = layout.m_index[offset + tri * 3 + 2];
        if (v2 != ivert) couples.push_back(v2);
      }

      if (couples.size() != triangles.size() * 2)
        continue; // ill-formed stars

      // -> Compute the "belt" (points around the star)
      std::vector<uint> belt;
      belt.reserve(triangles.size());

      { // first
        const GLuint indA = layout.m_index[offset + triangles[0] * 3 + 0];
        const GLuint indB = layout.m_index[offset + triangles[0] * 3 + 1];
        const GLuint indC = layout.m_index[offset + triangles[0] * 3 + 2];
        if      (indA == ivert) { belt.push_back(indB); belt.push_back(indC); }
        else if (indB == ivert) { belt.push_back(indC); belt.push_back(indA); }
        else                    { belt.push_back(indA); belt.push_back(indB); }
        couples[1] = couples.back(); couples.pop_back();
        couples[0] = couples.back(); couples.pop_back();
      }
      for (std::size_t is = 1; is < triangles.size(); ++is) // other
      {
        for (std::size_t ic = 0; ic < couples.size() / 2; ++ic)
        {
          const uint indB1 = couples[ic * 2 + 0];
          const uint indB2 = couples[ic * 2 + 1];
          if (indB1 == belt.back())
          {
            belt.push_back(indB2);
            couples[ic * 2 + 1] = couples.back(); couples.pop_back();
            couples[ic * 2 + 0] = couples.back(); couples.pop_back();
            break;
          }
          else if (indB2 == belt.back())
          {
            belt.push_back(indB1);
            couples[ic * 2 + 1] = couples.back(); couples.pop_back();
            couples[ic * 2 + 0] = couples.back(); couples.pop_back();
            break;
          }
        }
      }
      if (belt.size() != triangles.size() + 1) // ill-formed star ... sure ??
        continue;
      TRE_ASSERT(couples.empty());
      if (belt.front() != belt.back()) // ill-formed star ... sure ??
        continue;
      belt.pop_back();

      ++nvertValidStar;

      // -> compute curvature on the vertex "ivert"
      std::vector<glm::vec3> beltPts;
      beltPts.reserve(belt.size());
      for (uint ibelt : belt)
        beltPts.push_back(layout.m_positions.get<glm::vec3>(ibelt));

      glm::vec3 curve1, curve2;
      surfaceCurvature(layout.m_positions.get<glm::vec3>(ivert), layout.m_normals.get<glm::vec3>(ivert), beltPts, curve1, curve2);

      if (glm::length(curve1) + glm::length(curve2) >= 0.5f) // TODO : hqve the chqrqcteristic lenght of the mesh. then compare to 1/cLenght
        continue;

      ++nvertFlatCurvature;

      // 2.2 Reconstruct triangles
      std::size_t NtriLocal = 0;
      while(belt.size() > 2)
      {
        std::size_t ibroot = 0; // choosen triangle
        if (belt.size() > 3)
        {
          // get all triangle quality
          std::vector<float> triQual(belt.size());
          for (std::size_t ib = 0; ib < belt.size(); ++ib)
          {
            uint ibp1 = (ib + 1) % belt.size();
            uint ibp2 = (ib + 2) % belt.size();
            triangleQuality(layout.m_positions.get<glm::vec3>(belt[ib]), layout.m_positions.get<glm::vec3>(belt[ibp1]), layout.m_positions.get<glm::vec3>(belt[ibp2]), nullptr, & triQual[ib]);
          }
          // get max
          float    maxQual = triQual[0];
          for (std::size_t ib = 1; ib < belt.size(); ++ib)
          {
            if (maxQual > triQual[ib])
            {
              maxQual = triQual[ib];
              ibroot = ib;
            }
          }
        }
        // get the spin
        const GLuint indA = belt[ibroot];
        const GLuint indB = belt[(ibroot + 1) % belt.size()];
        const GLuint indC = belt[(ibroot + 2) % belt.size()];
        // add the triangle + remove the vertex
        const uint Itri = triangles[NtriLocal++];
        layout.m_index[offset + Itri * 3 + 0] = indA;
        layout.m_index[offset + Itri * 3 + 1] = indB;
        layout.m_index[offset + Itri * 3 + 2] = indC;
        belt.erase(belt.begin() + (ibroot + 1) % belt.size());
      }
      TRE_ASSERT(NtriLocal + 2 == triangles.size());
      triangleToRemove.push_back(triangles[NtriLocal]);
      triangleToRemove.push_back(triangles[NtriLocal + 1]);
      for (uint tri : triangles)
        triangleProcessed[tri] = true;
    }
    TRE_LOG(" - Vertices with 3-triangles  = " << nvertMoreThan3Triangles);
    TRE_LOG(" - Vertices untouched         = " << nvertUntouched);
    TRE_LOG(" - Vertices processed         = " << nvertValidStar);
    TRE_LOG(" - Vertices with flat surface = " << nvertFlatCurvature);

    // 3. Clear triangles
    sortQuick<uint>(triangleToRemove);
    for (std::size_t k = 0; k < triangleToRemove.size(); ++k)
    {
      const uint Itri = triangleToRemove[triangleToRemove.size() - k - 1];
      // copy the last into the triangle "Itri"
      Ntri--;
      layout.m_index[offset + Itri * 3 + 0] = layout.m_index[offset + Ntri * 3 + 0];
      layout.m_index[offset + Itri * 3 + 1] = layout.m_index[offset + Ntri * 3 + 1];
      layout.m_index[offset + Itri * 3 + 2] = layout.m_index[offset + Ntri * 3 + 2];
    }
    TRE_LOG(" - Triangles removed          = " << triangleToRemove.size());

    // End condition
    //TODO : for star, add condition for convexe-form
    //continueProcess = indTriangleRemove.size() > 0;
    continueProcess = false;
  }

  return Ntri * 3;
}

// ============================================================================

std::size_t decimateChangeVertex(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold)
{
  TRE_ASSERT(layout.m_indexCount > 0); // needed for connectivity
  TRE_ASSERT(layout.m_vertexCount > 0);

  const std::size_t count = part.m_size;
  const std::size_t offset = part.m_offset;

  if (count == 0 || threshold == 0.f) return count;

  // decimate patterm: edge-collapsing
  // collapse the edge (i,j) into the vertice (k),
  // thus, foreach triangles, replace (i) or (j) by (k)

  TRE_ASSERT(count % 3 == 0);

  TRE_LOG("decimate: it may take lot of time ... (Ntri = " << count / 3 << ")");

  TRE_ASSERT(layout.m_vertexCount < UINT_MAX);

  struct s_edge
  {
    GLuint indA;
    GLuint indB;
    uint triA;
    uint triB;
    std::vector<uint> tri;
    s_edge(GLuint iA, GLuint iB) : indA(iA), indB(iB) {}
  };

  std::vector<float> surfaceDiff(layout.m_vertexCount, 0.f); // store mesh-modification on each vertices (surface-diff ~= square of displacement)

  bool continueProcess = true;
  std::size_t Ntri = count / 3;

  while(continueProcess)
  {
    TRE_LOG("New step with Ntriangles = " << Ntri);

    // 1. get the edge-list (from scratch)
    std::vector<s_edge> listEdges;
    { // -> get "root"
      std::vector<uint> allcodes;
      allcodes.resize(Ntri * 3);
      for (std::size_t Itri = 0; Itri < Ntri; ++Itri)
      {
        const GLuint indA = layout.m_index[offset + Itri * 3 + 0];
        const GLuint indB = layout.m_index[offset + Itri * 3 + 1];
        const GLuint indC = layout.m_index[offset + Itri * 3 + 2];
        const uint codeAB = (indA < indB) ? indA * layout.m_vertexCount + indB : indB * layout.m_vertexCount + indA;
        const uint codeBC = (indB < indC) ? indB * layout.m_vertexCount + indC : indC * layout.m_vertexCount + indB;
        const uint codeAC = (indA < indC) ? indA * layout.m_vertexCount + indC : indC * layout.m_vertexCount + indA;
        allcodes[Itri * 3 + 0] = codeAB;
        allcodes[Itri * 3 + 0] = codeBC;
        allcodes[Itri * 3 + 0] = codeAC;
      }
      sortAndUniqueCounting(allcodes);

      listEdges.reserve(allcodes.size());
      for (uint code : allcodes) listEdges.push_back(s_edge(code / layout.m_vertexCount, code % layout.m_vertexCount));
    }
    // get triA, triB and triAll ...

    // remove not valid edges ....

    TRE_FATAL("TODO");

    TRE_LOG(" - Nedges = " << listEdges.size());

    uint processedEdge = 0; // just for stats.

    std::vector<uint> indTriangleRemove; // triangles to remove
    indTriangleRemove.reserve(Ntri / 2);

    for (s_edge & edge : listEdges)
    {

      const glm::vec3 midpoint = (layout.m_positions.get<glm::vec3>(edge.indA) + layout.m_positions.get<glm::vec3>(edge.indB)) * 0.5f;
      // compute total surface and mesh quality
      float surfaceBefore = 0.f;
      float surfaceAfter = 0.f;
      float minMeshQBefore = 1.f;
      float minMeshQAfter = 1.f;
      for (uint etri : edge.tri)
      {
        uint i0 = layout.m_index[etri*3+0];
        uint i1 = layout.m_index[etri*3+1];
        uint i2 = layout.m_index[etri*3+2];
        if (i0 == i1 || i0 == i2 || i1 == i2) continue; // degenerated triangle - will be cleaned after
        float area;
        float meshQuality;
        // triangle before edge collapsing
        glm::vec3 v0 = layout.m_positions.get<glm::vec3>(i0);
        glm::vec3 v1 = layout.m_positions.get<glm::vec3>(i1);
        glm::vec3 v2 = layout.m_positions.get<glm::vec3>(i2);
        triangleQuality(v0, v1, v2, &meshQuality, &area);
        TRE_ASSERT(area > 0.f);
        surfaceBefore += area;
        if (meshQuality < minMeshQBefore) minMeshQBefore = meshQuality;
        // triangle after edge collpasing
        int nReplace = 0;
        if (i0 == edge.indA || i0 == edge.indB)
        {
          v0 = midpoint;
          ++nReplace;
        }
        if (i1 == edge.indA || i1 == edge.indB)
        {
          v1 = midpoint;
          ++nReplace;
        }
        if (i2 == edge.indA || i2 == edge.indB)
        {
          v2 = midpoint;
          ++nReplace;
        }
        TRE_ASSERT(nReplace>0);
        if (nReplace==2) continue; // new triangle is degenerated (2 vertices replaced by the same vertex)
        triangleQuality(v0, v1, v2, &meshQuality, &area);
        TRE_ASSERT(area > 0.f);
        surfaceAfter += area;
        if (meshQuality < minMeshQAfter) minMeshQAfter = meshQuality;
      }
      // test if the edge will be collapsed
      const float surfaceRelativeDiff = fabs(surfaceBefore-surfaceAfter)/surfaceBefore +
                                        surfaceDiff[edge.indA] + surfaceDiff[edge.indB];
      if (surfaceRelativeDiff > (threshold*threshold))
      {
        //TRE_LOG("decimate: skip edge because of mesh conservation; surfaceRelativeDiff= " << surfaceRelativeDiff << " > " << (threshold*threshold) << " diff_A=" << surfaceDiff[indices[edge.indAidx]] << " diff_B=" << surfaceDiff[indices[edge.indBidx]]) ;
        continue;
      }
      if (minMeshQAfter < 0.33f && minMeshQAfter < minMeshQBefore)
      {
        //TRE_LOG("decimate: skip edge because it degrades too much the mesh quatily; before=" << minMeshQBefore << " after=" << minMeshQAfter);
        //do edge flip ? (if (minMeshQAfter < minMeshQBefore)
        continue;
      }
      // now, collapse the edge
      //TRE_LOG("decimate: process edge : surfaceRelativeDiff="<< surfaceRelativeDiff << " meshQuatily=(before=" << minMeshQBefore << ",after=" << minMeshQAfter << ")");
      const GLuint indC = edge.indA;
      // TODO collapseVertex(edge.indA, edge.indB);
      TRE_ASSERT(indC == edge.indA);
      ++processedEdge;
      if (indC>=surfaceDiff.size()) surfaceDiff.resize(indC+1, 0.f);
      surfaceDiff[indC] = surfaceRelativeDiff;

      // patch triangles
      for (uint Itri : edge.tri)
      {
        const GLuint indA = layout.m_index[offset + Itri * 3 + 0];
        const GLuint indB = layout.m_index[offset + Itri * 3 + 1];
        const GLuint indC = layout.m_index[offset + Itri * 3 + 2];

        const bool hasOneA = (indA == edge.indA) || (indB == edge.indA) || (indC == edge.indA);
        bool hasOneB = false;
        if (indA == edge.indB)
        {
          hasOneB = true;
          layout.m_index[offset + Itri * 3 + 0] = edge.indA;
        }
        if (indB == edge.indB)
        {
          hasOneB = true;
          layout.m_index[offset + Itri * 3 + 1] = edge.indA;
        }
        if (indC == edge.indB)
        {
          hasOneB = true;
          layout.m_index[offset + Itri * 3 + 2] = edge.indA;
        }
        if (hasOneA && hasOneB) indTriangleRemove.push_back(Itri); // degenerated
      }
    }

    TRE_LOG(" - Edge processed = " << processedEdge);

    // 3. Clear triangles
    sortQuick<uint>(indTriangleRemove);
    for (std::size_t k = 0; k < indTriangleRemove.size(); ++k)
    {
      const std::size_t Itri = indTriangleRemove[indTriangleRemove.size() - k - 1];
      // copy the last into the triangle "Itri"
      Ntri--;
      layout.m_index[offset + Itri * 3 + 0] = layout.m_index[offset + Ntri * 3 + 0];
      layout.m_index[offset + Itri * 3 + 1] = layout.m_index[offset + Ntri * 3 + 1];
      layout.m_index[offset + Itri * 3 + 2] = layout.m_index[offset + Ntri * 3 + 2];
    }
    TRE_LOG(" - Triangle removed = " << indTriangleRemove.size());

    // End condition
    //continueProcess = indTriangleRemove.size() > 0;
    continueProcess = false;
  }

  return 3 * Ntri;
}

// ============================================================================

struct s_tetrahedron
{
  const glm::vec3 *ptA, *ptB, *ptC, *ptD;
  s_tetrahedron *adjBCD, *adjACD, *adjABD, *adjABC;
  uint flags;
  float volume;

  static const uint FLAG_CONTAINING_POINTS = 1 << 0;
  static const uint FLAG_INTERIOR = 1 << 2;
  static const uint FLAG_EXTERIOR = 1 << 3;
  static const uint FLAG_SIDEMASK = FLAG_INTERIOR | FLAG_EXTERIOR;

  s_tetrahedron() : ptA(nullptr), ptB(nullptr), ptC(nullptr), ptD(nullptr)
  {
  }

  s_tetrahedron(const glm::vec3 *pa, const glm::vec3 *pb, const glm::vec3 *pc, const glm::vec3 *pd,
               s_tetrahedron *tBCD, s_tetrahedron *tACD, s_tetrahedron *tABD, s_tetrahedron *tABC,
               uint flag = FLAG_CONTAINING_POINTS)
  : ptA(pa), ptB(pb), ptC(pc), ptD(pd) , adjBCD(tBCD), adjACD(tACD), adjABD(tABD), adjABC(tABC), flags(flag)
  {
    const glm::vec3 eAB = *pb - *pa;
    const glm::vec3 eAC = *pc - *pa;
    const glm::vec3 eAD = *pd - *pa;
    volume = glm::abs(glm::dot(eAB, glm::cross(eAC, eAD))) / 6.f;
    TRE_ASSERT(volume != 0.f);
  }

  bool valid() const
  {
    const bool pV = (ptA != nullptr && ptB != nullptr && ptC != nullptr && ptD != nullptr);
    TRE_ASSERT((!pV) || (ptA != ptB && ptA != ptC && ptA != ptD && ptB != ptC && ptB != ptD && ptC != ptD));
    TRE_ASSERT((!pV) || (adjABC == nullptr) || (adjABC->hasPoint(ptA) && adjABC->hasPoint(ptB) && adjABC->hasPoint(ptC)));
    TRE_ASSERT((!pV) || (adjABD == nullptr) || (adjABD->hasPoint(ptA) && adjABD->hasPoint(ptB) && adjABD->hasPoint(ptD)));
    TRE_ASSERT((!pV) || (adjACD == nullptr) || (adjACD->hasPoint(ptA) && adjACD->hasPoint(ptC) && adjACD->hasPoint(ptD)));
    TRE_ASSERT((!pV) || (adjBCD == nullptr) || (adjBCD->hasPoint(ptB) && adjBCD->hasPoint(ptC) && adjBCD->hasPoint(ptD)));
    return pV;
  }

  bool hasPoint(const glm::vec3 *pt) const
  {
    return (ptA == pt) | (ptB == pt) | (ptC == pt) | (ptD == pt);
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

  static void flag_fromSurface(s_tetrahedron *tFrom, s_tetrahedron *tTo, bool isFromInterior)
  {
    tFrom->flags |=  (isFromInterior ? FLAG_INTERIOR : FLAG_EXTERIOR);
    if (tTo != nullptr) tTo->flags |=  (isFromInterior ? FLAG_EXTERIOR : FLAG_INTERIOR);
  }

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
    TRE_ASSERT(t1.valid() && t2.valid() && !t3.valid());
    conformSharedPoints(t1, t2);
    // replace neighbors
    if (t1.adjABC != nullptr) t1.adjABC->replaceNeigbourTetra(&t1, &t1);
    if (t2.adjABC != nullptr) t2.adjABC->replaceNeigbourTetra(&t2, &t1);
    if (t1.adjABD != nullptr) t1.adjABD->replaceNeigbourTetra(&t1, &t3);
    if (t2.adjABD != nullptr) t2.adjABD->replaceNeigbourTetra(&t2, &t3);
    if (t1.adjACD != nullptr) t1.adjACD->replaceNeigbourTetra(&t1, &t2);
    if (t2.adjACD != nullptr) t2.adjACD->replaceNeigbourTetra(&t2, &t2);
    // split
    TRE_FATAL("TODO: flags");
    const glm::vec3 *t2ptA = t2.ptA;
    s_tetrahedron   *t2adjABD = t2.adjABD;
    t3 = s_tetrahedron(t1.ptA, t2.ptA, t1.ptB, t1.ptC, t2.adjABC, t1.adjABC, &t2, &t1);
    t2 = s_tetrahedron(t1.ptA, t2.ptA, t1.ptC, t1.ptD, t2.adjACD, t1.adjACD, &t1, &t3);
    t1 = s_tetrahedron(t1.ptA, t2ptA , t1.ptD, t1.ptB, t2adjABD , t1.adjABD, &t3, &t2);
    TRE_ASSERT(t1.valid() && t2.valid() && t3.valid());
  }

  static void swapEdge_4tetras(s_tetrahedron &t1, s_tetrahedron &t2, s_tetrahedron &t3, s_tetrahedron &t4,
                               const glm::vec3 *ptEdgeC, const glm::vec3 *ptEdgeD,
                               const glm::vec3 *ptNewEdge1, const glm::vec3 *ptNewEdge2)
  {
    TRE_ASSERT(t1.valid() && t2.valid() && t3.valid() && t4.valid());
    // on each tetra, swap point that such the labeled points "ptC" and "ptD" are respectively "ptEdgeC" and "ptEdgeD"
    // -> t1
    if      (t1.ptA == ptEdgeC) t1.swapPoints_A_C();
    else if (t1.ptB == ptEdgeC) t1.swapPoints_B_C();
    else if (t1.ptD == ptEdgeC) t1.swapPoints_C_D();
    TRE_ASSERT(t1.ptC == ptEdgeC);
    if      (t1.ptA == ptEdgeD) t1.swapPoints_A_D();
    else if (t1.ptB == ptEdgeD) t1.swapPoints_B_D();
    TRE_ASSERT(t1.ptD == ptEdgeD);
    // -> t2
    if      (t2.ptA == ptEdgeC) t2.swapPoints_A_C();
    else if (t2.ptB == ptEdgeC) t2.swapPoints_B_C();
    else if (t2.ptD == ptEdgeC) t2.swapPoints_C_D();
    TRE_ASSERT(t2.ptC == ptEdgeC);
    if      (t2.ptA == ptEdgeD) t2.swapPoints_A_D();
    else if (t2.ptB == ptEdgeD) t2.swapPoints_B_D();
    TRE_ASSERT(t2.ptD == ptEdgeD);
    // -> t3
    if      (t3.ptA == ptEdgeC) t3.swapPoints_A_C();
    else if (t3.ptB == ptEdgeC) t3.swapPoints_B_C();
    else if (t3.ptD == ptEdgeC) t3.swapPoints_C_D();
    TRE_ASSERT(t3.ptC == ptEdgeC);
    if      (t3.ptA == ptEdgeD) t3.swapPoints_A_D();
    else if (t3.ptB == ptEdgeD) t3.swapPoints_B_D();
    TRE_ASSERT(t3.ptD == ptEdgeD);
    // -> t4
    if      (t4.ptA == ptEdgeC) t4.swapPoints_A_C();
    else if (t4.ptB == ptEdgeC) t4.swapPoints_B_C();
    else if (t4.ptD == ptEdgeC) t4.swapPoints_C_D();
    TRE_ASSERT(t4.ptC == ptEdgeC);
    if      (t4.ptA == ptEdgeD) t4.swapPoints_A_D();
    else if (t4.ptB == ptEdgeD) t4.swapPoints_B_D();
    TRE_ASSERT(t4.ptD == ptEdgeD);
    // on each tetra, swap point that such the new-edge points are labeled "ptA"
    // -> t1
    if (t1.ptB == ptNewEdge1 || t1.ptB == ptNewEdge2) t1.swapPoints_A_B();
    TRE_ASSERT(t1.ptA == ptNewEdge1 || t1.ptA == ptNewEdge2);
    // -> t2
    if (t2.ptB == ptNewEdge1 || t2.ptB == ptNewEdge2) t2.swapPoints_A_B();
    TRE_ASSERT(t2.ptA == ptNewEdge1 || t2.ptA == ptNewEdge2);
    // -> t3
    if (t3.ptB == ptNewEdge1 || t3.ptB == ptNewEdge2) t3.swapPoints_A_B();
    TRE_ASSERT(t3.ptA == ptNewEdge1 || t3.ptA == ptNewEdge2);
    // -> t4
    if (t4.ptB == ptNewEdge1 || t4.ptB == ptNewEdge2) t4.swapPoints_A_B();
    TRE_ASSERT(t4.ptA == ptNewEdge1 || t4.ptA == ptNewEdge2);
    // replace neighbors
    TRE_FATAL("TODO: neighbors");
    // flip
    TRE_FATAL("TODO: flags");
    s_tetrahedron *tStart = (t1.ptA == ptNewEdge1) ? &t1 : t1.adjBCD;
    TRE_ASSERT(tStart->ptA == ptNewEdge1);
    s_tetrahedron *tSide = t1.adjACD;
    s_tetrahedron *tOpp = t1.adjBCD;
    s_tetrahedron *tOppSide = tSide->adjBCD;
    TRE_ASSERT(tSide->adjBCD == tOpp->adjACD);
    *tStart   = s_tetrahedron(ptNewEdge1, ptNewEdge2, tStart->ptC  , tStart->ptB  , nullptr, nullptr, nullptr, nullptr);
    *tOpp     = s_tetrahedron(ptNewEdge1, ptNewEdge2, tOpp->ptD    , tOpp->ptB    , nullptr, nullptr, nullptr, nullptr);
    *tSide    = s_tetrahedron(ptNewEdge1, ptNewEdge2, tSide->ptC   , tSide->ptB   , nullptr, nullptr, nullptr, nullptr);
    *tOppSide = s_tetrahedron(ptNewEdge1, ptNewEdge2, tOppSide->ptC, tOppSide->ptB, nullptr, nullptr, nullptr, nullptr);

    TRE_ASSERT(t1.valid() && t2.valid() && t3.valid() && t4.valid());
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
  bool            resolved;

  void setup(const glm::vec3 *pF, const glm::vec3 *pG, const glm::vec3 *pH, const glm::vec3 &nOut)
  {
    ptF = pF;
    ptG = pG;
    ptH = pH;
    tetraNearby = nullptr;
    normalOut = nOut;
    resolved = false;
  }

  bool hasPoint(const glm::vec3 *pt)
  {
    return (pt == ptF) | (pt == ptG) | (pt == ptH);
  }
};

// ----------------------------------------------------------------------------

// TOTO: remove that !!

static std::ostream& operator<<(std::ostream& out, const glm::vec3 &pt)
{
  out << pt.x << ' ' <<  pt.y << ' ' << pt.z;
  return out;
}

#define osprinti3(_i) (_i < 100 ? "0" : "") << (_i < 10 ? "0" : "") << _i

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

  void report_Step1(const tre::chunkVector<s_tetrahedron, 128> &allTetras, const std::vector<s_tetrahedron *> &tetraToProcess, const glm::vec3 &ptToAdd, const std::vector<glm::vec3> &ptRemaining)
  {
    if (!rawOBJ.is_open()) return;

    rawOBJ << "o Step1_" << osprinti3(stepId) << "_AllTetra" << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && std::find(tetraToProcess.begin(), tetraToProcess.end(), &t) == tetraToProcess.end())
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t, 0.9f);
      }
    }
    for (std::size_t i = 0; i < tetraToProcess.size(); ++i)
    {
      rawOBJ  << "o Step1_" << osprinti3(stepId) << "_Tetra_" << osprinti3(i) << std::endl;
      writeTetra(*tetraToProcess[i]);
    }
    {
      rawOBJ << "o Step1_" << osprinti3(stepId) << "_Pt" << std::endl;
      writePoint(ptToAdd);
    }
    rawOBJ << "o Step1_" << osprinti3(stepId) << "_PtsRemaining" << std::endl;
    for (const glm::vec3 &pt : ptRemaining)
    {
      writePoint(pt);
    }
    ++stepId;
  }

  void report_Step1_Fail(const std::vector<s_tetrahedron *> &tetraToProcess, const s_surface *s, const glm::vec3 &ptToAdd, uint ptOnSurfCount)
  {
    for (std::size_t i = 0; i < tetraToProcess.size(); ++i)
    {
      rawOBJ  << "o Step1_" << osprinti3(stepId) << "_FAILED_Tetra_" << osprinti3(i) << std::endl;
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

  void report_Step2_SurfFail(const std::vector<s_tetrahedron *> &tetras, const s_meshTriangle &tri)
  {
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ  << "o Step2_" << osprinti3(stepId) << "_Surf_FAILED_Tetra_" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_Surf_FAILED_TriangleCurrent" << std::endl;
      writeTriangle(tri);
    }
    ++stepId;
  }

  void report_Step2_Edge(const std::vector<s_tetrahedron *> &tetras, const glm::vec3 &ptEdgeBegin, const glm::vec3 &ptEdgeEnd)
  {
    rawOBJ  << "o Step2_" << osprinti3(stepId) << "_Edge_Tetras"  << std::endl;
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ << "g " << "pgroup_Tetra_" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_Edge_EdgePtBegin" << std::endl;
      writePoint(ptEdgeBegin);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_Edge_EdgePtEnd" << std::endl;
      writePoint(ptEdgeEnd);
    }
    ++stepId;
  }

  void report_Step2_EdgeFail(const std::vector<s_tetrahedron *> &tetras, const glm::vec3 &ptEdgeBegin, const glm::vec3 &ptEdgeEnd, const char *label)
  {
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ  << "o Step2_" << osprinti3(stepId) << "_Edge_FAILED_" << label << "_Tetra_" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_Edge_FAILED_" << label << "_EdgePtBegin" << std::endl;
      writePoint(ptEdgeBegin);
    }
    {
      rawOBJ << "o Step2_" << osprinti3(stepId) << "_Edge_FAILED_" << label << "_EdgePtEnd" << std::endl;
      writePoint(ptEdgeEnd);
    }
    ++stepId;
  }

  void report_Step3_Interior_Exterior(const tre::chunkVector<s_tetrahedron, 128> &allTetras, const char *label)
  {
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Interior_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && (t.flags & s_tetrahedron::FLAG_INTERIOR) != 0 && (t.flags & s_tetrahedron::FLAG_EXTERIOR) == 0)
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Exterior_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && (t.flags & s_tetrahedron::FLAG_INTERIOR) == 0 && (t.flags & s_tetrahedron::FLAG_EXTERIOR) != 0)
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_Both_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && (t.flags & s_tetrahedron::FLAG_INTERIOR) != 0 && (t.flags & s_tetrahedron::FLAG_EXTERIOR) != 0)
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    rawOBJ  << "o Step3_" << osprinti3(stepId) << "_None_" << label << std::endl;
    for (std::size_t i = 0; i < allTetras.size(); ++i)
    {
      const s_tetrahedron &t = allTetras[i];
      if (t.valid() && (t.flags & s_tetrahedron::FLAG_INTERIOR) == 0 && (t.flags & s_tetrahedron::FLAG_EXTERIOR) == 0)
      {
        rawOBJ << "g " << "pgroup_TetraId_" << osprinti3(i) << std::endl;
        writeTetra(t);
      }
    }
    ++stepId;
  }

  void report_Step3_Triangle_Resolution(s_meshTriangle &tri, const std::vector<s_tetrahedron *> &tetras, const char *label)
  {
    for (std::size_t i = 0; i < tetras.size(); ++i)
    {
      rawOBJ  << "o Step3_" << osprinti3(stepId) << "_ResolutionTetra_" << label << "_Tetra_" << osprinti3(i) << std::endl;
      writeTetra(*tetras[i]);
    }
    {
      rawOBJ << "o Step3_" << osprinti3(stepId) << "_ResolutionTetra_" << label << "_Triangle" << std::endl;
      writeTriangle(tri);
    }
    ++stepId;
  }
};

// ----------------------------------------------------------------------------

bool tetrahedralize(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<uint> &listTetrahedrons,
                    std::function<void(float progress)> *progressNotifier)
{
  if (progressNotifier != nullptr) (*progressNotifier)(0.f);

  if (part.m_size < 4) return false;

  s_tetraReporter exporter("tetrahedralization.obj"); // TMP REMOVE

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
  inTriangles.resize(part.m_size / 3);

  float minEdgeLength = std::numeric_limits<float>::infinity();
  float minSurfaceAera = std::numeric_limits<float>::infinity();
  float minInscribedCercleRadius = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0, iT = 0; i < part.m_size; i += 3, ++iT)
  {
    const glm::vec3 &v0 = inPos.get<glm::vec3>(indicesData[i + 0]);
    const glm::vec3 &v1 = inPos.get<glm::vec3>(indicesData[i + 1]);
    const glm::vec3 &v2 = inPos.get<glm::vec3>(indicesData[i + 2]);
    const glm::vec3 e01 = v1 - v0;
    const glm::vec3 e02 = v2 - v0;
    const glm::vec3 e12 = v2 - v1;
    const glm::vec3 n = glm::cross(e01, e02);
    const float nSign = (inNormal.hasData() && glm::dot(n, inNormal.get<glm::vec3>(indicesData[i + 0]) + inNormal.get<glm::vec3>(indicesData[i + 1]) + inNormal.get<glm::vec3>(indicesData[i + 2])) < 0.f) ? -1.f : 1.f;
    s_meshTriangle &tri = inTriangles[iT];
    tri.setup(&v0, &v1, &v2, nSign * n);
    const float     a = 0.5f * glm::length(n);
    minSurfaceAera = std::min(minSurfaceAera, a);
    const float     l01 = glm::length(e01);
    const float     l02 = glm::length(e02);
    const float     l12 = glm::length(e12);
    const float     l = std::min(std::min(l01, l02), l12);
    minEdgeLength = std::min(minEdgeLength, l);
    minInscribedCercleRadius = std::min(minInscribedCercleRadius, 0.5f * a / (l01 + l02 + l12));
  }

  const float minVolumeAA = 0.3f * minSurfaceAera * minEdgeLength;
  const float minVolumeBB = 0.5f * minInscribedCercleRadius * minInscribedCercleRadius * minInscribedCercleRadius;

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
        if ((t.flags & s_tetrahedron::FLAG_CONTAINING_POINTS) == 0) continue;
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
        tBest->flags &= ~s_tetrahedron::FLAG_CONTAINING_POINTS;
        continue; // go to the next iteration
      }

      // ok
      TRE_ASSERT(std::isfinite(d2Best));
      listTetraToProcess.clear();
      listTetraToProcess.push_back(tBest);
      pt = ptBest;
      indices[iptBest] = indices.back(); indices.pop_back(); // remove the index from the list
    }

    if (progressNotifier != nullptr) (*progressNotifier)(0.1f + 0.6f * (1.f - float(indices.size())/float(indicesInitialSize)));

    TRE_ASSERT(listTetraToProcess.size() == 1);
    TRE_ASSERT(listTetraToProcess[0]->pointInCircumsphere(*pt));

    // -> prepare the insertion geometry
    s_tetrahedron &tRoot = *listTetraToProcess[0];
    // pt on vertex, edge or surface ?
    const bool onSurfABC = fabsf(s_surface(tRoot.ptA, tRoot.ptB, tRoot.ptC, nullptr, *tRoot.ptD).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfABD = fabsf(s_surface(tRoot.ptA, tRoot.ptB, tRoot.ptD, nullptr, *tRoot.ptC).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfACD = fabsf(s_surface(tRoot.ptA, tRoot.ptC, tRoot.ptD, nullptr, *tRoot.ptB).volumeSignedWithPoint(*pt)) <= tetraMinVolume;
    const bool onSurfBCD = fabsf(s_surface(tRoot.ptB, tRoot.ptC, tRoot.ptD, nullptr, *tRoot.ptA).volumeSignedWithPoint(*pt)) <= tetraMinVolume;

    const uint onSurfCount = onSurfABC + onSurfABD + onSurfACD + onSurfBCD;

    if (onSurfCount >= 3) // pt on vertex
    {
      if (onSurfABC & onSurfABD & onSurfACD) indexRemapper[_indP(pt) - indexRemapperOffset] = _indP(tRoot.ptA);
      if (onSurfABC & onSurfABD & onSurfBCD) indexRemapper[_indP(pt) - indexRemapperOffset] = _indP(tRoot.ptB);
      if (onSurfABC & onSurfACD & onSurfBCD) indexRemapper[_indP(pt) - indexRemapperOffset] = _indP(tRoot.ptC);
      if (onSurfABD & onSurfACD & onSurfBCD) indexRemapper[_indP(pt) - indexRemapperOffset] = _indP(tRoot.ptD);

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
      indexRemapper[_indP(pt) - indexRemapperOffset] = uint(-1); // TODO: improve that; we have the info about its location (isOnSurfXXX ...), so have a better remapper !!
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

    if (false)
    {
      std::vector<glm::vec3> ptsLeft;
      for (uint ind : indices) ptsLeft.push_back(inPos.get<glm::vec3>(ind));
      exporter.report_Step1(listTetra, listTetraToProcess, *pt, ptsLeft);
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
      *listTetraNew[iS] = s_tetrahedron(pt, surf.pt1, surf.pt2, surf.pt3,
                                        surf.tetraExterior, listTetraNew[surfInd_23], listTetraNew[surfInd_31], listTetraNew[surfInd_12]);

      if (surf.tetraExterior != nullptr)
        surf.tetraExterior->setNeigbourTetra(surf.pt1, surf.pt2, surf.pt3, listTetraNew[iS]);
    }

#ifdef TRE_DEBUG
    for (std::size_t iS = 0; iS < nSurf; ++iS)
    {
      TRE_ASSERT(listTetraNew[iS]->valid());
    }
#endif

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

  TRE_LOG("tetrahedralize: main-step 1: there are " << indicesFailedCount << " insertion-fails, over " << indicesInitialSize << " vertices");

  // main-step 2: apply the surface-constrain (swap surfaces to enforce there is existing surface in the tetrahedralization for each mesh's triangle)
  //              and flag tetrahedrons depending on their side (interior or exterior)

  const std::size_t Ntriangles = part.m_size / 3;
  std::size_t       NtrianglesResolved = 0;

  for (std::size_t jT = 0; jT < Ntriangles; ++jT)
  {
    s_meshTriangle &tri = inTriangles[jT];

    const uint indF_raw = indicesData[jT * 3 + 0];
    const uint indG_raw = indicesData[jT * 3 + 1];
    const uint indH_raw = indicesData[jT * 3 + 2];

    const uint indF = indexRemapper[indF_raw - indexRemapperOffset];
    const uint indG = indexRemapper[indG_raw - indexRemapperOffset];
    const uint indH = indexRemapper[indH_raw - indexRemapperOffset];

    if (indF != uint(-1)) tri.ptF = &inPos.get<glm::vec3>(indF);
    if (indG != uint(-1)) tri.ptG = &inPos.get<glm::vec3>(indG);
    if (indH != uint(-1)) tri.ptH = &inPos.get<glm::vec3>(indH);

    if (((indF | indG | indH) == uint(-1)) || ((indF == indG) | (indF == indH) | (indG == indH)))
      continue; // the mesh's triangle cannot be reconstructed. Skip it.

    uint            keyEdgesPrev = 0;
    bool            surfExists = false;

    while (true) // reconstruction step
    {
      uint           keyEdges = 0; // 0x1: edge FG exists, 0x2: edge FH exists, 0x4: edge GH exists
      s_tetrahedron *tOnPtF = nullptr;
      s_tetrahedron *tOnPtG = nullptr;

      for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
      {
        s_tetrahedron &t = listTetra[iT];
        TRE_ASSERT(t.valid());
        const uint key = t.hasPoint(tri.ptF) * 0x1 + t.hasPoint(tri.ptG) * 0x2 + t.hasPoint(tri.ptH) * 0x4;
        if (key == 0x7)
        {
          const bool onPtA = tri.hasPoint(t.ptA);
          const bool onPtB = tri.hasPoint(t.ptB);
          const bool onPtC = tri.hasPoint(t.ptC);
          const bool onPtD = tri.hasPoint(t.ptD);
          TRE_ASSERT(onPtA + onPtB + onPtC + onPtD == 3);
          if      (!onPtA) s_tetrahedron::flag_fromSurface(&t, t.adjBCD, glm::dot(s_surface(t.ptB, t.ptC, t.ptD, nullptr, *t.ptA).normalOut, tri.normalOut) > 0.f);
          else if (!onPtB) s_tetrahedron::flag_fromSurface(&t, t.adjACD, glm::dot(s_surface(t.ptA, t.ptC, t.ptD, nullptr, *t.ptB).normalOut, tri.normalOut) > 0.f);
          else if (!onPtC) s_tetrahedron::flag_fromSurface(&t, t.adjABD, glm::dot(s_surface(t.ptA, t.ptB, t.ptD, nullptr, *t.ptC).normalOut, tri.normalOut) > 0.f);
          else             s_tetrahedron::flag_fromSurface(&t, t.adjABC, glm::dot(s_surface(t.ptA, t.ptB, t.ptC, nullptr, *t.ptD).normalOut, tri.normalOut) > 0.f);
          surfExists = true;
          break;
        }
        if      (key == 0x3) keyEdges |= 0x1;
        else if (key == 0x5) keyEdges |= 0x2;
        else if (key == 0x6) keyEdges |= 0x4;
        if (key & 0x1) tOnPtF = &t;
        if (key & 0x2) tOnPtG = &t;
      }

      if (surfExists) break;

      TRE_ASSERT(tOnPtF != nullptr && tOnPtG != nullptr);
      tri.tetraNearby = tOnPtF;

      break; // TMP Disabled !

      if (((keyEdges     & 0x1) != 0) + ((keyEdges     & 0x2) != 0) + ((keyEdges     & 0x4) != 0) <=
          ((keyEdgesPrev & 0x1) != 0) + ((keyEdgesPrev & 0x2) != 0) + ((keyEdgesPrev & 0x4) != 0))
      {
        exporter.report_Step2_SurfFail({ tOnPtF, tOnPtG }, tri);
        break; // Failed (maybe the initial mesh has non-manfoiled geometry)
      }

      keyEdgesPrev = keyEdges;

      if (keyEdges == 0x7)
      {
        // All edges exist but without a shared tetra. TODO ... get 3 tetra adjacant each others
        listTetraToProcess.clear();
        for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
        {
          s_tetrahedron &t = listTetra[iT];
          TRE_ASSERT(t.valid());
          const uint key = t.hasPoint(tri.ptF) + t.hasPoint(tri.ptG) + t.hasPoint(tri.ptH);
          if (key == 2) listTetraToProcess.push_back(&t);
        }
        exporter.report_Step2_SurfFail(listTetraToProcess, tri);
        break; // the mesh's triangle is now reconstructed.
      }

      // get a non-existing edge (that will be labeled 'PQ')
      const glm::vec3 *ptEdgeP = tri.ptF;
      const glm::vec3 *ptEdgeQ = tri.ptG;
      s_tetrahedron   *tOnPtP = tOnPtF;
      if ((keyEdges & 0x2) == 0)
      {
        ptEdgeP = tri.ptF;
        ptEdgeQ = tri.ptH;
        tOnPtP = tOnPtF;
      }
      else if ((keyEdges & 0x4) == 0)
      {
        ptEdgeP = tri.ptG;
        ptEdgeQ = tri.ptH;
        tOnPtP = tOnPtG;
      }

      // process edge PQ
      // -> get all tetra around point P
      listTetraToProcess.clear();
      listTetraToProcess.push_back(tOnPtP);
      {
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
            if (tadjABC != nullptr && tadjABC->hasPoint(ptEdgeP)) listTetraToProcess.push_back(tadjABC);
            if (tadjABD != nullptr && tadjABD->hasPoint(ptEdgeP)) listTetraToProcess.push_back(tadjABD);
            if (tadjACD != nullptr && tadjACD->hasPoint(ptEdgeP)) listTetraToProcess.push_back(tadjACD);
            if (tadjBCD != nullptr && tadjBCD->hasPoint(ptEdgeP)) listTetraToProcess.push_back(tadjBCD);
          }
          queueStart = queueEnd;
          queueEnd = listTetraToProcess.size();
        }
      }
      // -> get a starting tetra (from the point P)
      s_tetrahedron *tStart = nullptr;
      float          tStartBestVol = 0.f;
      for (s_tetrahedron *t : listTetraToProcess)
      {
        // swap point such as ptEdgeP == t->ptA
        if      (ptEdgeP == t->ptB) t->swapPoints_A_B();
        else if (ptEdgeP == t->ptC) t->swapPoints_A_C();
        else if (ptEdgeP == t->ptD) t->swapPoints_A_D();
        TRE_ASSERT(ptEdgeP == t->ptA);
        const float volPBC_Q = s_surface(ptEdgeP, t->ptB, t->ptD, nullptr, *t->ptD).volumeSignedWithPoint(*ptEdgeQ);
        const float volPBD_Q = s_surface(ptEdgeP, t->ptB, t->ptD, nullptr, *t->ptC).volumeSignedWithPoint(*ptEdgeQ);
        const float volPCD_Q = s_surface(ptEdgeP, t->ptC, t->ptD, nullptr, *t->ptB).volumeSignedWithPoint(*ptEdgeQ);
        const float volMin = std::min(std::min(volPBC_Q, volPBD_Q), volPCD_Q);
        const float volMax = std::max(std::max(volPBC_Q, volPBD_Q), volPCD_Q);
        if (volMin < tStartBestVol && volMax < tetraMinVolume)
        {
          tStart = t;
          tStartBestVol = volMin;
        }
      }
      while (tStart != nullptr)
      {
        // -> walk one step toward the point Q
        s_tetrahedron *tNext = tStart->adjBCD;
        s_tetrahedron::conformSharedPoints(*tStart, *tNext);
        const float volPBC_NextPt = s_surface(ptEdgeP, tStart->ptB, tStart->ptD, nullptr, *tStart->ptD).volumeSignedWithPoint(*tNext->ptA);
        const float volPBD_NextPt = s_surface(ptEdgeP, tStart->ptB, tStart->ptD, nullptr, *tStart->ptC).volumeSignedWithPoint(*tNext->ptA);
        const float volPCD_NextPt = s_surface(ptEdgeP, tStart->ptC, tStart->ptD, nullptr, *tStart->ptB).volumeSignedWithPoint(*tNext->ptA);
        const bool isInterior_BC = volPBC_NextPt < -tetraMinVolume;
        const bool isInterior_BD = volPBD_NextPt < -tetraMinVolume;
        const bool isInterior_CD = volPCD_NextPt < -tetraMinVolume;
        const uint isInteriorCount = isInterior_BC + isInterior_BD + isInterior_CD;
        if (isInteriorCount == 3) // The ray from P towards the next-tetra exterme point hits a tetra's surface.
        {
          exporter.report_Step2_EdgeFail({tStart, tNext}, *ptEdgeP, *ptEdgeQ, "RayHitsSurface_BeforeSplit");

          listTetra.push_back(s_tetrahedron());
          s_tetrahedron *tNew = &listTetra[listTetra.size() - 1];
          s_tetrahedron::split_2tetras_to_3tetras(*tStart, *tNext, *tNew);

          exporter.report_Step2_EdgeFail({tStart, tNext, tNew}, *ptEdgeP, *ptEdgeQ, "RayHitsSurface_AfterSplit");

          break; // ?
        }
        else if (isInteriorCount == 2) // The ray from P towards the next-tetra exterme point hits an edge
        {
          const glm::vec3 *ptSharedEdge1 = isInterior_CD ? tStart->ptB : tStart->ptC;
          const glm::vec3 *ptSharededge2 = isInterior_BC ? tStart->ptD : tStart->ptC;

          listTetraToProcess.clear();
          listTetraToProcess.push_back(tStart);
          _walkTetrahedronOnEdge(listTetraToProcess, ptSharedEdge1, ptSharededge2);

          exporter.report_Step2_EdgeFail(listTetraToProcess, *ptEdgeP, *ptEdgeQ, "RayHitsEdge_BeforeSplit");

          // TODO: split all possible surface-adjacant-tetra (split 2 tetras into 3 tetras) in order to obtain only 4 tetras around the edge.
          // Then flip the edge.

          if (listTetraToProcess.size() > 4)
          {
            for (uint it = 0; it < listTetraToProcess.size(); )
            {
              s_tetrahedron *t1 = listTetraToProcess[it];
              s_tetrahedron *t2 = listTetraToProcess[(it + 1) % listTetraToProcess.size()];
              // check if t1 and t2 can be 'merged' (in regards with the shared edge)
              s_tetrahedron::conformSharedPoints(*t1, *t2);

              const float volABC = s_surface(t1->ptA, t1->ptB, t1->ptC, nullptr, *t1->ptD).volumeSignedWithPoint(*t2->ptA);
              const float volABD = s_surface(t1->ptA, t1->ptB, t1->ptD, nullptr, *t1->ptC).volumeSignedWithPoint(*t2->ptA);
              const float volACD = s_surface(t1->ptA, t1->ptC, t1->ptD, nullptr, *t1->ptB).volumeSignedWithPoint(*t2->ptA);

              if (volABC < -tetraMinVolume && volABD < -tetraMinVolume && volACD < -tetraMinVolume)
              {
                listTetra.push_back(s_tetrahedron());
                s_tetrahedron *tNew = &listTetra[listTetra.size() - 1];
                s_tetrahedron::split_2tetras_to_3tetras(*t1, *t2, *tNew);
                listTetraToProcess.erase(listTetraToProcess.begin() + it);
                if       (t1->hasPoint(ptSharedEdge1) && t1->hasPoint(ptSharededge2)) listTetraToProcess[it] = t1;
                else if  (t2->hasPoint(ptSharedEdge1) && t2->hasPoint(ptSharededge2)) listTetraToProcess[it] = t2;
                else                                                                  listTetraToProcess[it] = tNew;
                it = 0;
              }
              else
              {
                ++it;
              }
            }
          }

          if (listTetraToProcess.size() != 4)
          {
            exporter.report_Step2_EdgeFail(listTetraToProcess, *ptEdgeP, *ptEdgeQ, "RayHitsEdge_AfterSplit_FAILED");
            break; // failed to reduce the list. Stop this edge reconstruction.
          }

          s_tetrahedron::swapEdge_4tetras(*listTetraToProcess[0], *listTetraToProcess[1], *listTetraToProcess[2], *listTetraToProcess[3],
                                          ptSharedEdge1, ptSharededge2,
                                          tStart->ptA, tStart->ptD);

          exporter.report_Step2_EdgeFail(listTetraToProcess, *ptEdgeP, *ptEdgeQ, "RayHitsEdge_AfterSplit");
          break; // ?
        }
        else // The ray from P towards the next-tetra exterme point hits a vertex
        {
          // Aligned points (degenerated geometry). Ignore this edge.
          exporter.report_Step2_EdgeFail({tStart}, *ptEdgeP, *ptEdgeQ, "RayHitsVertex_AlignedPoints");
          break;
        }
      }

#ifdef TRE_DEBUG // maybe remove that once it's tested !!
      for (std::size_t iS = 0; iS < listTetra.size(); ++iS)
      {
        TRE_ASSERT(listTetra[iS].valid());
      }
#endif

    } // while reconstruct edge

    if (surfExists)
    {
      tri.resolved = true;
      ++NtrianglesResolved;
      if (progressNotifier != nullptr) (*progressNotifier)(0.7f + 0.2f * float(NtrianglesResolved) / float(Ntriangles));
    }
  }

  TRE_LOG("tetrahedralize: main-step 2: there are " << Ntriangles - NtrianglesResolved << " triangles that do not match with the tetrahedrization, over " << Ntriangles << " triangles.");

  exporter.report_Step3_Interior_Exterior(listTetra, "AfterBasicDetection");

  // main-step 3: compute the interior and exterior volumes

  // --> flag remaining tetrahedrons that does not fit with the raw mesh's triangles
  for (std::size_t jT = 0; jT < Ntriangles; ++jT)
  {
    s_meshTriangle &tri = inTriangles[jT];

    if (tri.resolved) continue;

    const glm::vec3 edgeFG = *tri.ptG - *tri.ptF;
    const glm::vec3 edgeFH = *tri.ptH - *tri.ptF;
    const glm::vec3 directNormal = glm::normalize(glm::cross(edgeFG, edgeFH));
    glm::mat3       matUnproject = glm::inverse(glm::mat3(edgeFG, edgeFH, directNormal)); // det(m) == 1.f

    // get nearby tetrahedrons
    if (tri.tetraNearby == nullptr)
    {
      for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
      {
        s_tetrahedron &t = listTetra[iT];
        if (t.hasPoint(tri.ptF) || t.hasPoint(tri.ptG) || t.hasPoint(tri.ptH))
        {
          tri.tetraNearby = &t;
          break;
        }
      }
    }
    if (tri.tetraNearby == nullptr)
      continue; // invalid triangle !!

    listTetraToProcess.clear();
    listTetraToProcess.push_back(tri.tetraNearby);
    {
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
          if (tadjABC != nullptr && (tadjABC->pointInCircumsphere(*tri.ptF) || tadjABC->pointInCircumsphere(*tri.ptG) || tadjABC->pointInCircumsphere(*tri.ptH))) listTetraToProcess.push_back(tadjABC);
          if (tadjABD != nullptr && (tadjABD->pointInCircumsphere(*tri.ptF) || tadjABD->pointInCircumsphere(*tri.ptG) || tadjABD->pointInCircumsphere(*tri.ptH))) listTetraToProcess.push_back(tadjABD);
          if (tadjACD != nullptr && (tadjACD->pointInCircumsphere(*tri.ptF) || tadjACD->pointInCircumsphere(*tri.ptG) || tadjACD->pointInCircumsphere(*tri.ptH))) listTetraToProcess.push_back(tadjACD);
          if (tadjBCD != nullptr && (tadjBCD->pointInCircumsphere(*tri.ptF) || tadjBCD->pointInCircumsphere(*tri.ptG) || tadjBCD->pointInCircumsphere(*tri.ptH))) listTetraToProcess.push_back(tadjBCD);
        }
        queueStart = queueEnd;
        queueEnd = listTetraToProcess.size();
      }
    }

    // TODO: will be improved, by looping on nearby tetras only -> use the "listTetraToProcess"

    for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
    {
      s_tetrahedron &t = listTetra[iT];

      // Project the 4 tetra's points on the mesh's triangle

      const glm::vec3 coordsA = matUnproject * (*t.ptA - *tri.ptF);
      const glm::vec3 coordsB = matUnproject * (*t.ptB - *tri.ptF);
      const glm::vec3 coordsC = matUnproject * (*t.ptC - *tri.ptF);
      const glm::vec3 coordsD = matUnproject * (*t.ptD - *tri.ptF);

      const bool isOnTriA = (std::abs(coordsA.z) < 1.e-4f);
      const bool isOnTriB = (std::abs(coordsB.z) < 1.e-4f);
      const bool isOnTriC = (std::abs(coordsC.z) < 1.e-4f);
      const bool isOnTriD = (std::abs(coordsD.z) < 1.e-4f);
      const unsigned nOnTri = isOnTriA + isOnTriB + isOnTriC + isOnTriD;

      const bool isOnExtA = (!isOnTriA) & (coordsA.z > 0.f);
      const bool isOnExtB = (!isOnTriB) & (coordsB.z > 0.f);
      const bool isOnExtC = (!isOnTriC) & (coordsC.z > 0.f);
      const bool isOnExtD = (!isOnTriD) & (coordsD.z > 0.f);
      const unsigned nOnExt = isOnExtA + isOnExtB + isOnExtC + isOnExtD;

      if ((nOnTri == 0) && (nOnExt == 0 || nOnExt == 4)) continue; // all points are on a side (interior or exterior) and not to close

      // TRE_ASSERT(t in listTetraToProcess)

      // if 1 pt is on a side, 3 on the other side, compute the tetra's slice on the plane (it's a triangle). And intersect the triangle with the mesh's triangle
      // if 2 pt is on a side, 2 on the other side, compute the tetra's slice on the plane (it's a quad). And intersect the quad with the mesh's triangle

      // Then compute both the volume of interior and the volume of exterior.
      TRE_ASSERT("TODO"); // HERE TODO !
      /*{

        //listTetraToProcess.push_back(&t);

        if (!isOnTriA && (isInTriB + isInTriC + isInTriD) >= 2)
        {
          s_tetrahedron::flag_fromSurface(&t, t.adjBCD, glm::dot(s_surface(t.ptB, t.ptC, t.ptD, nullptr, *t.ptA).normalOut, tri.normalOut) > 0.f);
          tri.resolved = true;
        }
        if (!isOnTriB && (isInTriA + isInTriC + isInTriD) >= 2)
        {
          s_tetrahedron::flag_fromSurface(&t, t.adjACD, glm::dot(s_surface(t.ptA, t.ptC, t.ptD, nullptr, *t.ptB).normalOut, tri.normalOut) > 0.f);
          tri.resolved = true;
        }
        if (!isOnTriC && (isInTriA + isInTriB + isInTriD) >= 2)
        {
          s_tetrahedron::flag_fromSurface(&t, t.adjABD, glm::dot(s_surface(t.ptA, t.ptB, t.ptD, nullptr, *t.ptC).normalOut, tri.normalOut) > 0.f);
          tri.resolved = true;
        }
        if (!isOnTriD && (isInTriA + isInTriB + isInTriC) >= 2)
        {
          s_tetrahedron::flag_fromSurface(&t, t.adjABC, glm::dot(s_surface(t.ptA, t.ptB, t.ptC, nullptr, *t.ptD).normalOut, tri.normalOut) > 0.f);
          tri.resolved = true;
        }
      }*/

    }

    if (!tri.resolved && exporter.stepId < 100)
      exporter.report_Step3_Triangle_Resolution(tri, listTetraToProcess, "FAILED");

    if (tri.resolved)
    {
      ++NtrianglesResolved;
      if (progressNotifier != nullptr) (*progressNotifier)(0.7f + 0.2f * float(NtrianglesResolved) / float(Ntriangles));
    }
  }

  exporter.report_Step3_Interior_Exterior(listTetra, "AfterProjectedDetection");

  TRE_LOG("tetrahedralize: main-step 3: there are " << Ntriangles - NtrianglesResolved << " triangles that are ignored to define the interior volume of the mesh, over " << Ntriangles << " triangles.");

  if (progressNotifier != nullptr) (*progressNotifier)(0.9f);

  // -> flag remaining tetras (recursive walk from neighbors)
  while (true)
  {
    bool hasUpdate = false;

    for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
    {
      s_tetrahedron &t = listTetra[iT];

      if ((t.flags & s_tetrahedron::FLAG_SIDEMASK) == 0)
      {
        uint nInterior = 0;
        uint nExterior = 0;

        if (t.adjABC != nullptr) { nInterior += ((t.adjABC->flags & s_tetrahedron::FLAG_INTERIOR) != 0); nExterior += ((t.adjABC->flags & s_tetrahedron::FLAG_EXTERIOR) != 0); }
        if (t.adjABD != nullptr) { nInterior += ((t.adjABD->flags & s_tetrahedron::FLAG_INTERIOR) != 0); nExterior += ((t.adjABD->flags & s_tetrahedron::FLAG_EXTERIOR) != 0); }
        if (t.adjACD != nullptr) { nInterior += ((t.adjACD->flags & s_tetrahedron::FLAG_INTERIOR) != 0); nExterior += ((t.adjACD->flags & s_tetrahedron::FLAG_EXTERIOR) != 0); }
        if (t.adjBCD != nullptr) { nInterior += ((t.adjBCD->flags & s_tetrahedron::FLAG_INTERIOR) != 0); nExterior += ((t.adjBCD->flags & s_tetrahedron::FLAG_EXTERIOR) != 0); }

        t.flags = ((nInterior > nExterior) * s_tetrahedron::FLAG_INTERIOR) | ((nInterior < nExterior) * s_tetrahedron::FLAG_EXTERIOR);

        hasUpdate |= (nInterior != nExterior);
      }
    }

    if (!hasUpdate) break;
  }

  exporter.report_Step3_Interior_Exterior(listTetra, "AfterSpreading");

  // -> get the side (interior or exterior) to delete
  uint        flagDelete = 0;
  std::size_t NtetrasRemaining  = 0;
  for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
  {
    const s_tetrahedron &t = listTetra[iT];
    if ((t.flags & s_tetrahedron::FLAG_SIDEMASK) == 0) ++NtetrasRemaining;
    const uint iA = _indP(t.ptA);
    const uint iB = _indP(t.ptB);
    const uint iC = _indP(t.ptC);
    const uint iD = _indP(t.ptD);
    if (!(iA < layout.m_vertexCount && iB < layout.m_vertexCount && iC < layout.m_vertexCount && iD < layout.m_vertexCount))
      flagDelete |= (t.flags & s_tetrahedron::FLAG_SIDEMASK);
  }
  if (NtetrasRemaining != 0 || flagDelete == s_tetrahedron::FLAG_SIDEMASK)
  {
    if (NtetrasRemaining != 0)
    {
      TRE_LOG("tetrahedralize: main-step 3: there are " << NtetrasRemaining << " un-flaged tetrahedrons. Algo failed.");
    }
    if (flagDelete == s_tetrahedron::FLAG_SIDEMASK)
    {
      TRE_LOG("tetrahedralize: main-step 3: input mesh does not seem to be closed (the interior volume is not well defined). Algo failed.");
    }
    listTetra.clear(); // clear all :(
  }

  // final-step: compute output tetrahedra-list

  listTetrahedrons.reserve(listTetrahedrons.size() + listTetra.size() * 4);
  for (std::size_t iT = 0; iT < listTetra.size(); ++iT)
  {
    const s_tetrahedron &t = listTetra[iT];
    if ((t.flags & flagDelete) == 0)
    {
      listTetrahedrons.push_back(_indP(t.ptA));
      listTetrahedrons.push_back(_indP(t.ptB));
      listTetrahedrons.push_back(_indP(t.ptC));
      listTetrahedrons.push_back(_indP(t.ptD));
    }
  }

#undef _indP

  if (progressNotifier != nullptr) (*progressNotifier)(1.f);

  return true;
}

// ============================================================================

} // namespace

} // namespace
