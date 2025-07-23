#include "tre_model_tools.h"

#include "tre_model.h"

//#define MESH_DEBUG // Mesh debugging
#ifdef MESH_DEBUG
#include <fstream>
#include <string>

#define osprinti3(_i) (_i < 100 ? "0" : "") << (_i < 10 ? "0" : "") << _i

#endif // MESH_DEBUG

namespace tre {

namespace modelTools {

// ============================================================================

glm::vec3 computeBarycenter2D(const std::vector<glm::vec2> &inEnvelop)
{
  TRE_ASSERT(inEnvelop.size() > 1);
  const std::size_t end = inEnvelop.size();

  glm::vec2 pt = glm::vec2(0.f, 0.f);
  float     surf = 0.f;

  // Sum of the signed surface of all triangles formed by each (oriented) edges and the origin: (Origin, P1_edge, P2_edge).

  glm::vec2 ptPrev = inEnvelop[end - 1];
  for (std::size_t ipt = 0; ipt < end; ++ipt)
  {
    const glm::vec2 ptCurr = inEnvelop[ipt];
    const float     s = 0.5f * (ptCurr.y * ptPrev.x - ptCurr.x * ptPrev.y);
    surf += s;
    pt += s * (ptPrev + ptCurr) / 3.f;
    ptPrev = ptCurr;
  }

  if (fabsf(surf) < 1.e-10f) return glm::vec3(0.f);

  pt /= surf;
  surf = fabsf(surf);

  return glm::vec3(pt, surf);
}

// ============================================================================

void computeConvexeEnvelop2D_XY(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, const float threshold, std::vector<glm::vec2> &outEnvelop)
{
  TRE_ASSERT(layout.m_vertexCount > 0);

  outEnvelop.clear();

  const std::size_t count = part.m_size;
  if (count == 0) return;
  const std::size_t offset = part.m_offset;
  const std::size_t end = offset + count;

  const glm::vec3 boxExtend = part.m_bbox.transform(transform).extend();
  const float radiusThreshold = threshold * fmaxf(boxExtend.x, boxExtend.y);
  const float thresholdSquared = radiusThreshold * radiusThreshold;

  // extract points
  std::vector<glm::vec2> points;
  points.resize(count);
  if (layout.m_indexCount == 0)
  {
    if (layout.m_positions.m_size == 2)
    {
      for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
      {
        const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec2>(ipt), 0.f, 1.f); // TODO : naive, not optimized ...
        points[j] = glm::vec2(transform * pt);
      }
    }
    else if (layout.m_positions.m_size == 3)
    {
      for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
      {
        const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec3>(ipt), 1.f); // TODO : naive, not optimized ...
        points[j] = glm::vec2(transform * pt);
      }
    }
    else
    {
      TRE_FATAL("not reached");
    }
  }
  else
  {
    if (layout.m_positions.m_size == 2)
    {
      for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
      {
        const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec2>(layout.m_index[ipt]), 0.f, 1.f); // TODO : naive, not optimized ...
        points[j] = glm::vec2(transform * pt);
      }
    }
    else if (layout.m_positions.m_size == 3)
    {
      for (std::size_t ipt = offset, j = 0; ipt < end; ++ipt, ++j)
      {
        const glm::vec4 pt = glm::vec4(layout.m_positions.get<glm::vec3>(layout.m_index[ipt]), 1.f); // TODO : naive, not optimized ...
        points[j] = glm::vec2(transform * pt);
      }
    }
    else
    {
      TRE_FATAL("not reached");
    }
  }

  // get the higher pt
  glm::vec2 pt_high = points[0];
  for (std::size_t i = 1; i < count; ++i)
  {
    const glm::vec2 &pt = points[i];
    if (pt.y > pt_high.y)
      pt_high = pt;
    else if (pt.y == pt_high.y && pt.x < pt_high.x)
      pt_high = pt;
  }

#define dot2D(v, w)        glm::dot(v, w)
#define cross2D(v, w)      (v.x * w.y - v.y * w.x)

  // compute alpha
  std::vector<float> alpha(count);
  for (std::size_t i = 0; i < count; ++i)
  {
    const glm::vec2 vHM = points[i] - pt_high;
    alpha[i] = fastAtan2(vHM.y, vHM.x);
  }

  // sort
  std::vector<uint> permut(count);
  for (uint i = 0; i < count; ++i)
    permut[i] = i;

  sortQuick_permutation<float>(alpha, permut);

  // generate envelop
  outEnvelop.reserve(count);

  outEnvelop.push_back(pt_high);
  outEnvelop.push_back(points[permut[0]]);

  glm::vec2 vMN = outEnvelop[1] - outEnvelop[0];
  for (uint i = 1; i < count; ++i)
  {
    const glm::vec2 P = points[permut[i]];
    const glm::vec2 vNP = P - outEnvelop.back();
    if (glm::dot(vNP, vNP) <= thresholdSquared)
      continue;
    const float cosA = dot2D(vMN, vNP);
    const float sinA = cross2D(vMN, vNP);
    if (sinA == 0.f && cosA == 0.f)
      continue;
    if (sinA <= 0.f)
    {
      if (sinA < 0.f || cosA >= 0.f)
      {
        outEnvelop.back() = P;
      }
      while(outEnvelop.size() > 2)
      {
        const glm::vec2 vQM = outEnvelop[outEnvelop.size() - 2] - outEnvelop[outEnvelop.size() - 3];
        const glm::vec2 vMP = P                                 - outEnvelop[outEnvelop.size() - 2];
        const float sinA2 = cross2D(vQM, vMP);
        if (sinA2 <= 0.f)
        {
          outEnvelop.pop_back();
          outEnvelop.back() = P;
        }
        else
          break;
      }
      vMN = outEnvelop.back() - outEnvelop[outEnvelop.size() - 2];
    }
    else
    {
      vMN = P - outEnvelop.back();
      outEnvelop.push_back(P);
    }
  }

  // the last pt might be a duplicate.
  if (outEnvelop.size() >= 3)
  {
    const glm::vec2 vAB = outEnvelop.back() - outEnvelop[outEnvelop.size() - 2];
    const glm::vec2 vBC = outEnvelop.front() - outEnvelop.back();
    const float sinABC = cross2D(vAB, vBC);
    if (sinABC <= 0.f)
      outEnvelop.pop_back();
  }

  // check
#ifdef TRE_DEBUG
  const std::size_t outEnvelopSize = outEnvelop.size();
  for (std::size_t i = 0; i < outEnvelopSize; ++i)
  {
    const std::size_t ip1 = (i + 1) % outEnvelopSize;
    const std::size_t ip2 = (i + 2) % outEnvelopSize;
    const glm::vec2 vAB = outEnvelop[ip1] - outEnvelop[i];
    const glm::vec2 vBC = outEnvelop[ip2] - outEnvelop[ip1];
    const float sinABC = cross2D(vAB, vBC);
    TRE_ASSERT(sinABC > 0.f);
  }
#endif

  #undef cross2D
  #undef dot2D
}

// ============================================================================

static float areaSigned(const glm::vec2 &ptA, const glm::vec2 &ptB, const glm::vec2 &ptC)
{
  const glm::vec2 vAB = ptB - ptA;
  const glm::vec2 vAC = ptC - ptA;
  return 0.5f * (vAB.x * vAC.y - vAB.y * vAC.x);
}

// ----------------------------------------------------------------------------

struct s_triangle
{
  const glm::vec2 *ptA, *ptB, *ptC;
  s_triangle *adjAB, *adjBC, *adjCA;
  long flag; // user-flag

  s_triangle()
    : ptA(nullptr), ptB(nullptr), ptC(nullptr), adjAB(nullptr), adjBC(nullptr), adjCA(nullptr), flag(0) {}
  s_triangle(const glm::vec2 *A, const glm::vec2 *B, const glm::vec2 *C, s_triangle *tab, s_triangle *tbc, s_triangle *tca)
    : ptA(A), ptB(B), ptC(C), adjAB(tab), adjBC(tbc), adjCA(tca), flag(0) {}

  bool valid()  const
  {
    TRE_ASSERT(ptA != ptB || (ptA == nullptr && ptB == nullptr));
    TRE_ASSERT(ptA != ptC || (ptA == nullptr && ptC == nullptr));
    TRE_ASSERT(ptB != ptC || (ptB == nullptr && ptC == nullptr));
    const float as = (ptA != nullptr) ? areaSigned(*ptA, *ptB, *ptC) : 1.f;
    return (ptA != nullptr) && (as > 0.f);
  }

  bool hasPoint(const glm::vec2 *pt) const
  {
    return (ptA == pt || ptB == pt || ptC == pt);
  }

  bool pointInside(const glm::vec2 &pt, const float minArea) const
  {
    const bool b1 = areaSigned(*ptA, *ptB, pt) >= -minArea;
    const bool b2 = areaSigned(*ptB, *ptC, pt) >= -minArea;
    const bool b3 = areaSigned(*ptC, *ptA, pt) >= -minArea;
    return (b1 && b2 && b3);
  }

  bool pointInCircumcircle(const glm::vec2 &pt) const
  {
    const glm::vec2 vPA = *ptA - pt;
    const glm::vec2 vPB = *ptB - pt;
    const glm::vec2 vPC = *ptC - pt;
    const float     lPA = glm::dot(vPA, vPA);
    const float     lPB = glm::dot(vPB, vPB);
    const float     lPC = glm::dot(vPC, vPC);
    const float det = lPC * (vPA.x * vPB.y - vPA.y * vPB.x) +
                      lPB * (vPC.x * vPA.y - vPC.y * vPA.x) +
                      lPA * (vPB.x * vPC.y - vPB.y * vPC.x);
    return det >= 0.f;
  }

  bool crossLine(const glm::vec2 *e1, const glm::vec2 *e2) const
  {
    const glm::vec2 v12 = *e2 - *e1;
    const glm::vec2 t12 = glm::vec2(-v12.y, v12.x);

    if (e1 != ptA && e1 != ptB && e2 != ptA && e2 != ptB)
    {
      const glm::vec2 vAB = *ptB - *ptA;
      const glm::vec2 tAB = glm::vec2(-vAB.y, vAB.x);
      const glm::vec2 vA1 = *e1 - *ptA;
      const glm::vec2 param = glm::vec2(glm::dot(vA1, t12), glm::dot(vA1, tAB)) / glm::dot(vAB, t12);
      if (param.x > 0.f && param.x < 1.f && param.y > 0.f && param.y < 1.f) // note: false if param == NaN
        return true;
    }

    if (e1 != ptA && e1 != ptC && e2 != ptA && e2 != ptC)
    {
      const glm::vec2 vAC = *ptC - *ptA;
      const glm::vec2 tAC = glm::vec2(-vAC.y, vAC.x);
      const glm::vec2 vA1 = *e1 - *ptA;
      const glm::vec2 param = glm::vec2(glm::dot(vA1, t12), glm::dot(vA1, tAC)) / glm::dot(vAC, t12);
      if (param.x > 0.f && param.x < 1.f && param.y > 0.f && param.y < 1.f) // note: false if param == NaN
        return true;
    }

    if (e1 != ptB && e1 != ptC && e2 != ptB && e2 != ptC)
    {
      const glm::vec2 vBC = *ptC - *ptB;
      const glm::vec2 tBC = glm::vec2(-vBC.y, vBC.x);
      const glm::vec2 vB1 = *e1 - *ptB;
      const glm::vec2 param = glm::vec2(glm::dot(vB1, t12), glm::dot(vB1, tBC)) / glm::dot(vBC, t12);
      if (param.x > 0.f && param.x < 1.f && param.y > 0.f && param.y < 1.f) // note: false if param == NaN
        return true;
    }

    return false;
  }

  void clear()
  {
    ptA = ptB = ptC = nullptr;
    if (adjAB != nullptr) adjAB->replaceNeigborTri(this, nullptr);
    if (adjBC != nullptr) adjBC->replaceNeigborTri(this, nullptr);
    if (adjCA != nullptr) adjCA->replaceNeigborTri(this, nullptr);
    adjAB = adjBC = adjCA = nullptr;
  }

  void replaceNeigborTri(s_triangle *oldtri, s_triangle *newtri)
  {
    if (adjAB == oldtri) adjAB = newtri;
    if (adjBC == oldtri) adjBC = newtri;
    if (adjCA == oldtri) adjCA = newtri;
  }

  static void swapEdge(s_triangle &tri1, s_triangle &tri2)
  {
    TRE_ASSERT(tri1.valid());
    TRE_ASSERT(tri2.valid());
    const glm::vec2 *ptSide1, *ptEdge1;
    const glm::vec2 *ptEdge2, *ptSide2;
    s_triangle *tri1_e1, *tri1_e2;
    s_triangle *tri2_e1, *tri2_e2;
    // fill
    if (!tri2.hasPoint(tri1.ptA)) // tri1: edge is B-C
    {
      TRE_ASSERT(tri2.hasPoint(tri1.ptB));
      TRE_ASSERT(tri2.hasPoint(tri1.ptC));
      ptSide1 = tri1.ptA; ptEdge1 = tri1.ptB; ptEdge2 = tri1.ptC;
      tri1_e1 = tri1.adjCA; tri1_e2 = tri1.adjAB;
    }
    else if (tri2.hasPoint(tri1.ptB)) // tri1: edge is A-B
    {
      TRE_ASSERT(!tri2.hasPoint(tri1.ptC));
      ptSide1 = tri1.ptC; ptEdge1 = tri1.ptA; ptEdge2 = tri1.ptB;
      tri1_e1 = tri1.adjBC; tri1_e2 = tri1.adjCA;
    }
    else
    {
      TRE_ASSERT(tri2.hasPoint(tri1.ptC)); //tri1: edge is C-A
      ptSide1 = tri1.ptB; ptEdge1 = tri1.ptC; ptEdge2 = tri1.ptA;
      tri1_e1 = tri1.adjAB; tri1_e2 = tri1.adjBC;
    }
    if (!tri1.hasPoint(tri2.ptA)) // tri2: edge is B-C
    {
      TRE_ASSERT(tri1.hasPoint(tri2.ptB));
      TRE_ASSERT(tri1.hasPoint(tri2.ptC));
      ptSide2 = tri2.ptA;
      TRE_ASSERT(ptEdge2 == tri2.ptB);
      TRE_ASSERT(ptEdge1 == tri2.ptC);
      tri2_e2 = tri2.adjCA;
      tri2_e1 = tri2.adjAB;
    }
    else if (tri1.hasPoint(tri2.ptB)) // tri2: edge is A-B
    {
      TRE_ASSERT(!tri1.hasPoint(tri2.ptC));
      ptSide2 = tri2.ptC;
      TRE_ASSERT(ptEdge2 == tri2.ptA);
      TRE_ASSERT(ptEdge1 == tri2.ptB);
      tri2_e2 = tri2.adjBC;
      tri2_e1 = tri2.adjCA;
    }
    else
    {
      TRE_ASSERT(tri1.hasPoint(tri2.ptC)); //tri2: edge is C-A
      ptSide2 = tri2.ptB;
      TRE_ASSERT(ptEdge2 == tri2.ptC);
      TRE_ASSERT(ptEdge1 == tri2.ptA);
      tri2_e2 = tri2.adjAB;
      tri2_e1 = tri2.adjBC;
    }
    // make the 2 new triangles
    tri1.ptA = ptSide1; tri1.adjBC = tri2_e2;
    tri1.ptB = ptEdge1; tri1.adjCA = &tri2;
    tri1.ptC = ptSide2; tri1.adjAB = tri1_e2;
    tri2.ptA = ptSide2; tri2.adjBC = tri1_e1;
    tri2.ptB = ptEdge2; tri2.adjCA = &tri1;
    tri2.ptC = ptSide1; tri2.adjAB = tri2_e1;
    // patch neighbour triangles
    if (tri1_e1 != nullptr) tri1_e1->replaceNeigborTri(&tri1, &tri2);
    if (tri2_e2 != nullptr) tri1_e2->replaceNeigborTri(&tri2, &tri1);
    // end
    TRE_ASSERT(tri1.valid());
    TRE_ASSERT(tri2.valid());
  }
};

// ----------------------------------------------------------------------------

static bool _checkInsertionValidity(const std::vector<s_triangle *> &listTetra, s_triangle &tAdd, const glm::vec2 &pt, const float minArea)
{
  // Check only surfaces that would be exterior if the tetrahedron is added.

  // get exterior surfaces
  s_triangle *tadjAB = tAdd.adjAB;
  s_triangle *tadjBC = tAdd.adjBC;
  s_triangle *tadjCA = tAdd.adjCA;
  for (s_triangle *tbis : listTetra)
  {
    if      (tbis == tadjAB) tadjAB = nullptr;
    else if (tbis == tadjBC) tadjBC = nullptr;
    else if (tbis == tadjCA) tadjCA = nullptr;
  }

  // check edge AB
  if (tadjAB != nullptr && areaSigned(*tAdd.ptA, *tAdd.ptB, pt) < minArea)
      return false;

  // check edge BC
  if (tadjBC != nullptr && areaSigned(*tAdd.ptB, *tAdd.ptC, pt) < minArea)
      return false;

  // check edge CA
  if (tadjCA != nullptr && areaSigned(*tAdd.ptC, *tAdd.ptA, pt) < minArea)
      return false;

  return true;
}

// ----------------------------------------------------------------------------

struct s_edge
{
  const glm::vec2 *pt1, *pt2;
  s_triangle *tri12, *tri21;

  s_edge(const glm::vec2 *p1, const glm::vec2 *p2, s_triangle *t12, s_triangle *t21)
    : pt1(p1), pt2(p2), tri12(t12), tri21(t21) {}

  static bool genContour(const std::vector<s_triangle*> &listTri, std::vector<s_edge> &listEdge)
  {
    listEdge.clear();
    // find edges
    for (s_triangle *t : listTri)
    {
      bool isEdge_AB = true;
      bool isEdge_BC = true;
      bool isEdge_CA = true;
      s_triangle *tAB = t->adjAB;
      s_triangle *tBC = t->adjBC;
      s_triangle *tCA = t->adjCA;
      for (const s_triangle *tt : listTri)
      {
        if (tAB == tt) isEdge_AB = false;
        if (tBC == tt) isEdge_BC = false;
        if (tCA == tt) isEdge_CA = false;
      }
      if (isEdge_AB) listEdge.push_back(s_edge(t->ptA, t->ptB, t, tAB));
      if (isEdge_BC) listEdge.push_back(s_edge(t->ptB, t->ptC, t, tBC));
      if (isEdge_CA) listEdge.push_back(s_edge(t->ptC, t->ptA, t, tCA));
    }
    // sort edges
    const std::size_t nEdge = listEdge.size();
    for (std::size_t iE = 1; iE < nEdge; ++iE)
    {
      const glm::vec2 *lastPt = listEdge[iE - 1].pt2;
      std::size_t jE = iE;
      for (; jE < nEdge; ++jE)
      {
        if (lastPt == listEdge[jE].pt1) break;
      }
      if (jE == nEdge) return false; // failed
      if (jE != iE) std::swap(listEdge[iE], listEdge[jE]);
    }
    return true;
  }
};

// ----------------------------------------------------------------------------

#ifdef MESH_DEBUG

struct s_triangulateExporter
{
  std::ofstream rawOBJ;
  uint          offsetVert = 1;
  uint          iter = 0;

  s_triangulateExporter(const std::string &filename) { rawOBJ.open(filename.c_str(), std::ofstream::out); if (rawOBJ.is_open()) rawOBJ << "# WAVEFRONT data - export skin" << std::endl; }
  ~s_triangulateExporter() { rawOBJ.close(); }

  void writePoint(const glm::vec2 &pt)
  {
    rawOBJ << "v " << pt.x         << ' ' <<  pt.y - 0.02f << ' ' << 0.f << std::endl;
    rawOBJ << "v " << pt.x         << ' ' <<  pt.y + 0.02f << ' ' << 0.f << std::endl;
    rawOBJ << "v " << pt.x - 0.02f << ' ' <<  pt.y         << ' ' << 0.f << std::endl;
    rawOBJ << "v " << pt.x + 0.02f << ' ' <<  pt.y         << ' ' << 0.f << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 2 << ' ' << offsetVert + 1 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 1 << ' ' << offsetVert + 3 << ' ' << offsetVert + 0 << ' ' << std::endl;
    offsetVert += 4;
  }

  void writeLine(const glm::vec2 &ptA, const glm::vec2 &ptB)
  {
    rawOBJ << "v " << ptA.x << ' ' <<  ptA.y << ' ' << 0.f - 0.02f << std::endl;
    rawOBJ << "v " << ptA.x << ' ' <<  ptA.y << ' ' << 0.f + 0.02f << std::endl;
    rawOBJ << "v " << ptB.x << ' ' <<  ptB.y << ' ' << 0.f - 0.02f << std::endl;
    rawOBJ << "v " << ptB.x << ' ' <<  ptB.y << ' ' << 0.f + 0.02f << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 3 << ' ' << std::endl;
    rawOBJ << "f " << offsetVert + 3 << ' ' << offsetVert + 2 << ' ' << offsetVert + 0 << ' ' << std::endl;
    offsetVert += 4;
  }

  void writeTriangle(const glm::vec2 &ptA, const glm::vec2 &ptB, const glm::vec2 &ptC)
  {
    rawOBJ << "v " << ptA.x << ' ' <<  ptA.y << ' ' << 0.f << std::endl;
    rawOBJ << "v " << ptB.x << ' ' <<  ptB.y << ' ' << 0.f << std::endl;
    rawOBJ << "v " << ptC.x << ' ' <<  ptC.y << ' ' << 0.f << std::endl;
    rawOBJ << "f " << offsetVert + 0 << ' ' << offsetVert + 1 << ' ' << offsetVert + 2 << ' ' << std::endl;
    offsetVert += 3;
  }

  void report_rawTriangulation(const std::vector<glm::vec2> &envelop, const std::vector<s_triangle> &listTrangles)
  {
    if (!rawOBJ.is_open()) return;
    rawOBJ << "o Step_" << osprinti3(iter) << "_0InEnvelop" << std::endl;
    for (std::size_t i = 0; i < envelop.size(); ++i)
    {
      writePoint(envelop[i]);
      writeLine(envelop[i], envelop[(i + 1) % envelop.size()]);
    }
    for (std::size_t iT = 0; iT < listTrangles.size(); ++iT)
    {
      const s_triangle &t = listTrangles[iT];
      if (t.valid())
      {
        rawOBJ << "o Step_" << osprinti3(iter) << "_Triangle" << osprinti3(iT) << std::endl;
        writeTriangle(*t.ptA, *t.ptB, *t.ptC);
      }
    }
    ++iter;
  }

  void report_edgeFlip(const std::vector<s_triangle*> localTriangles, const glm::vec2 *ptEdge0, const glm::vec2 *ptEdge1, const char *msg)
  {
    if (!rawOBJ.is_open()) return;
    rawOBJ << "o Step_" << osprinti3(iter) << "_" << msg << std::endl;
    for (const auto tptr : localTriangles)
    {
      writeTriangle(*tptr->ptA, *tptr->ptB, *tptr->ptC);
    }
    writeLine(*ptEdge0, *ptEdge1);
    ++iter;
  }
};

#else // MESH_DEBUG

struct s_triangulateExporter
{
  s_triangulateExporter(const char *) {}

  void report_rawTriangulation(const std::vector<glm::vec2> &, const std::vector<s_triangle> &) {}
  void report_edgeFlip(const std::vector<s_triangle*>, const glm::vec2 *, const glm::vec2 *, const char *) {}
};

#endif // MESH_DEBUG

// ----------------------------------------------------------------------------

void triangulate(const std::vector<glm::vec2> &envelop, std::vector<uint> &listTriangles)
{
  listTriangles.clear();

  if (envelop.size() < 3)
    return;

  const std::size_t Npts = envelop.size();

  // compute b-box

  glm::vec2 boxMin = envelop[0];
  glm::vec2 boxMax = envelop[0];
  for (const glm::vec2 &pt : envelop)
  {
    boxMin = glm::min(boxMin, pt);
    boxMax = glm::max(boxMax, pt);
  }

  {
    glm::vec2 boxExtend = (boxMax - boxMin) * 0.05f;
    boxMin -= boxExtend;
    boxMax += boxExtend;
  }

  // compute minimal area allowed for the triangles

  float minEdgeLength = std::numeric_limits<float>::infinity();
  float surfaceAera = 0.f;
  for (std::size_t i = 0; i < Npts; ++i)
  {
    const glm::vec2 &v0 = envelop[i != 0 ? i - 1 : Npts - 1];
    const glm::vec2 &v1 = envelop[i];
    const glm::vec2 e01 = v1 - v0;
    const float     l01 = glm::length(e01);
    surfaceAera += e01.x * (0.f - v0.y) - e01.y * (0.f - v0.x);
    minEdgeLength = std::min(minEdgeLength, l01);
  }

  const float triMinArea = glm::max(std::abs(surfaceAera) / Npts * 1.e-3f, 1.e-7f /* fp-precision limit */); // TODO !!!

  // generate the "master"-quad (2 triangles)

  const glm::vec2 boxAA = boxMin;
  const glm::vec2 boxAB = glm::vec2(boxMin.x, boxMax.y);
  const glm::vec2 boxBA = glm::vec2(boxMax.x, boxMin.y);
  const glm::vec2 boxBB = boxMax;

  const std::size_t  listTriSizeMax = (Npts + 4) * 2;
  std::vector<s_triangle> listTri(listTriSizeMax); // warning: don't resize because data must not be re-located
  std::size_t    listTriSize = 0;

  listTri[0] = s_triangle(&boxAA, &boxBA, &boxBB, nullptr, nullptr, &listTri[1]);
  listTri[1] = s_triangle(&boxBB, &boxAB, &boxAA, nullptr, nullptr, &listTri[0]);
  listTriSize = 2;

  s_triangulateExporter exporter("triangulate.obj");

  // process (Delaunay triangulation - Bowyer-Watson algorithm) without edge-constrain

  std::vector<s_triangle*> listTriToProcess;
  listTriToProcess.reserve(16);

  for (std::size_t iP = 0; iP < Npts; ++iP)
  {
    const glm::vec2 *pt = &envelop[iP];

    // -> get list of triangles that will be removed
    listTriToProcess.clear();
    {
      // --> find the triangle(s) that contains the point (the point can be on the edge)
      for (std::size_t i = 0; i < listTriSize; ++i)
      {
        s_triangle &t = listTri[i];
        TRE_ASSERT(t.valid());
        if (t.pointInside(*pt, triMinArea)) { listTriToProcess.push_back(&t); }
      }
      if (listTriToProcess.size() >= 3)
      {
        TRE_LOG("triangulate: a point belongs to more than 3 triangles, results may be wrong."); // TODO: implement the "index"-remapper to consider duplicated points.
      }
      // --> add other triangles by checking for neighbors recursively
      std::size_t queueStart = 0;
      std::size_t queueEnd = listTriToProcess.size();
      while (queueStart < queueEnd)
      {
        for (std::size_t i = queueStart; i < queueEnd; ++i)
        {
          s_triangle &t = *listTriToProcess[i];
          s_triangle *tadjAB = t.adjAB;
          s_triangle *tadjBC = t.adjBC;
          s_triangle *tadjCA = t.adjCA;
          for (s_triangle *tbis : listTriToProcess)
          {
            if      (tbis == tadjAB) tadjAB = nullptr;
            else if (tbis == tadjBC) tadjBC = nullptr;
            else if (tbis == tadjCA) tadjCA = nullptr;
          }
          if (tadjAB != nullptr && tadjAB->pointInCircumcircle(*pt) && _checkInsertionValidity(listTriToProcess, *tadjAB, *pt, triMinArea))
            listTriToProcess.push_back(tadjAB);
          if (tadjBC != nullptr && tadjBC->pointInCircumcircle(*pt) && _checkInsertionValidity(listTriToProcess, *tadjBC, *pt, triMinArea))
            listTriToProcess.push_back(tadjBC);
          if (tadjCA != nullptr && tadjCA->pointInCircumcircle(*pt) && _checkInsertionValidity(listTriToProcess, *tadjCA, *pt, triMinArea))
            listTriToProcess.push_back(tadjCA);
        }
        queueStart = queueEnd;
        queueEnd = listTriToProcess.size();
      }
    }
    TRE_ASSERT(!listTriToProcess.empty());

    // -> generate the contour
    std::vector<s_edge> listEdge;
    s_edge::genContour(listTriToProcess, listEdge);

    // -> compute pointers in which new triangles will be created
    const std::size_t nEdge = listEdge.size();
    std::vector<s_triangle*> listTriNew(nEdge, nullptr);
    {
      std::size_t iTnew = 0;
      for (std::size_t stop = glm::min(nEdge, listTriToProcess.size()); iTnew < stop; ++iTnew)
        listTriNew[iTnew] = listTriToProcess[iTnew];
      for (; iTnew < nEdge; ++iTnew)
      {
        listTriNew[iTnew] = &listTri[listTriSize++];
        TRE_ASSERT(listTriSize <= listTriSizeMax);
      }
    }

    // -> generate new triangles
    TRE_ASSERT(listEdge.size() == listTriNew.size());
    for (std::size_t iE = 0; iE < nEdge; ++iE)
    {
      const s_edge &edge = listEdge[iE];
      s_triangle *prevTri = iE == 0         ? listTriNew.back()  : listTriNew[iE - 1];
      s_triangle *nextTri = iE == nEdge - 1 ? listTriNew.front() : listTriNew[iE + 1];
      *listTriNew[iE] = s_triangle(pt, edge.pt1, edge.pt2, prevTri, edge.tri21, nextTri);
      if (edge.tri21 != nullptr)
        edge.tri21->replaceNeigborTri(edge.tri12, listTriNew[iE]);
      TRE_ASSERT(listTriNew[iE]->valid());
    }
  }

  exporter.report_rawTriangulation(envelop, listTri);

  // apply the edge-constrain: swap edges to enforce there is existing edge in the triangulation for each envelop's segment

  std::vector<bool> envelopEdgeFound(Npts, false);

  for (std::size_t iT = 0; iT < listTriSize; ++iT)
  {
    s_triangle &t = listTri[iT];
    TRE_ASSERT(t.valid());
    const uint iA = uint(t.ptA - envelop.data());
    const uint iB = uint(t.ptB - envelop.data());
    const uint iC = uint(t.ptC - envelop.data());

#define testEdge(i1, i2) \
    if (i1 < Npts && i2 < Npts) \
    { \
      const uint iMin = (i1 < i2) ? i1 : i2; \
      const uint iMax = (i1 < i2) ? i2 : i1; \
      if (iMin + 1 == iMax) \
        envelopEdgeFound[iMin] = true; \
      else if (iMin == 0 && iMax == Npts - 1) \
        envelopEdgeFound[Npts - 1] = true; \
    }

    testEdge(iA, iB);
    testEdge(iA, iC);
    testEdge(iB, iC);

#undef testEdge
  }

  for (std::size_t iE = 0; iE < Npts; ++iE)
  {
    if (envelopEdgeFound[iE]) continue;

    const glm::vec2 *ptM = &envelop[iE];
    const glm::vec2 *ptN = &envelop[iE == Npts -1 ? 0 : iE + 1];

    // edge (ptM, ptN) does not exist in the triangulation.
    std::vector<s_triangle*> listTriToProcess;
    for (std::size_t iT = 0; iT < listTriSize; ++iT)
    {
      s_triangle &t = listTri[iT];
      if (t.crossLine(ptM, ptN)) listTriToProcess.push_back(&t);
    }
    // like in previous step, generate a new triangulation of the area 'listTriToProcess'
    if (listTriToProcess.size() <= 1)
    {
      exporter.report_edgeFlip(listTriToProcess, ptM, ptN, "FAILED_NoEnoughTriangle");
      continue; // degenerated geometry. Ignore the missing edge :(
    }
    if (listTriToProcess.size() == 2)
    {
      // quick-process
      s_triangle::swapEdge(*listTriToProcess[0], *listTriToProcess[1]);
      TRE_ASSERT(listTriToProcess[0]->valid());
      TRE_ASSERT(listTriToProcess[1]->valid());
      continue;
    }
    // -> generate the contour
    std::vector<s_edge> listEdge;
    if (!s_edge::genContour(listTriToProcess, listEdge))
    {
      exporter.report_edgeFlip(listTriToProcess, ptM, ptN, "FAILED_BadContour");
      continue; // ignoring ...
    }
    TRE_ASSERT(listEdge.size() >= 5);

    // -> process edge per edge
    while (true)
    {
      // select the candidate
      const std::size_t nEdges = listEdge.size();
      std::size_t iEdge = 0;
      std::size_t iEdgeP1 = 1;
      for (; iEdge < nEdges; ++iEdge)
      {
        iEdgeP1 = (iEdge == nEdges -1) ? 0 : iEdge + 1;
        if (listEdge[iEdge].pt2 == ptM || listEdge[iEdge].pt2 == ptN)
          continue;
        const glm::vec2 v00 = *listEdge[iEdge  ].pt2 - *listEdge[iEdge].pt1;
        const glm::vec2 v02 = *listEdge[iEdgeP1].pt2 - *listEdge[iEdge].pt1;
        const float     spin = v00.x * v02.y - v00.y * v02.x;
        if (spin > 0.f)
          break; // got a candidate
      }
      TRE_ASSERT(iEdge != nEdges);
      // generate the triangle
      s_triangle *triNew = nullptr;
      if (listTriToProcess.empty())
      {
        triNew = &listTri[listTriSize++]; // "allocate" a new triangle
        TRE_ASSERT(listTriSize <= listTriSizeMax);
      }
      else
      {
        triNew = listTriToProcess.back();
        listTriToProcess.pop_back();
      }
      s_triangle *triInvalid = reinterpret_cast<s_triangle*>(long(nEdges * (nEdges + 1) / 2 + iEdge));
      s_edge &e1 = listEdge[iEdge];
      s_edge &e2 = listEdge[iEdgeP1];

      triNew->ptA   = e1.pt1;
      triNew->ptB   = e1.pt2;
      triNew->ptC   = e2.pt2;
      triNew->adjAB = e1.tri21;
      triNew->adjBC = e2.tri21;
      triNew->adjCA = triInvalid;

      if (e1.tri21 != nullptr)
        e1.tri21->replaceNeigborTri(e1.tri12, triNew);
      if (e2.tri21 != nullptr)
        e2.tri21->replaceNeigborTri(e2.tri12, triNew);

      TRE_ASSERT(triNew->valid());

      e1.pt2   = e2.pt2;
      e1.tri12 = triInvalid;
      e1.tri21 = triNew;

      listEdge.erase(listEdge.begin() + iEdgeP1);

      if (listEdge.size() == 2)
      {
        listEdge[0].tri21->replaceNeigborTri(listEdge[0].tri12, triNew);
        listEdge[1].tri21->replaceNeigborTri(listEdge[1].tri12, triNew);
        break;
      }
    }
    TRE_ASSERT(listTriToProcess.empty());
  }

  // clean exterior triangles

  // -> walk triangles : mark the direct interior triangle and clear the direct exterior triangle
  for (std::size_t iT = 0; iT < listTriSize; ++iT)
  {
    s_triangle &t = listTri[iT];
    TRE_ASSERT(t.valid());
    const uint iA = uint(t.ptA - envelop.data());
    const uint iB = uint(t.ptB - envelop.data());
    const uint iC = uint(t.ptC - envelop.data());

#define testEdge(iL, iR) \
    if (iL < Npts && iR < Npts) \
    { \
      if (iL + 1 == iR || (iL == Npts - 1 && iR == 0)) \
        t.flag = 1; \
      else if (iR + 1 == iL || (iR == Npts - 1 && iL == 0)) \
        t.clear(); \
    } \
    else \
    { \
      t.clear(); \
    }

    testEdge(iA, iB);
    testEdge(iB, iC);
    testEdge(iC, iA);

#undef testEdge
  }

  // -> colorize recursively interior triangles
  bool hasNewTriangle = true;
  while (hasNewTriangle)
  {
    hasNewTriangle = false;
    for (std::size_t iT = 0; iT < listTriSize; ++iT)
    {
       s_triangle &t = listTri[iT];
      if (!t.valid() || t.flag != 1)
        continue;
      if (t.adjAB != nullptr && t.adjAB->flag != 1) { hasNewTriangle = true; t.adjAB->flag = 1; }
      if (t.adjBC != nullptr && t.adjBC->flag != 1) { hasNewTriangle = true; t.adjBC->flag = 1; }
      if (t.adjCA != nullptr && t.adjCA->flag != 1) { hasNewTriangle = true; t.adjCA->flag = 1; }
    }
  }

  // -> delete (un-colorized) exterior triangles
  for (std::size_t iT = 0; iT < listTriSize; ++iT)
  {
    s_triangle &t = listTri[iT];
    if (t.valid() && t.flag != 1)
      t.clear();
  }

  // compute output triangle-list

  listTriangles.reserve(listTriSize * 3);
  for (std::size_t iT = 0; iT < listTriSize; ++iT)
  {
    s_triangle &t = listTri[iT];
    if (t.valid())
    {
      TRE_ASSERT(uint(t.ptA - envelop.data()) < listTriSize);
      TRE_ASSERT(uint(t.ptB - envelop.data()) < listTriSize);
      TRE_ASSERT(uint(t.ptC - envelop.data()) < listTriSize);

      listTriangles.push_back(uint(t.ptA - envelop.data()));
      listTriangles.push_back(uint(t.ptB - envelop.data()));
      listTriangles.push_back(uint(t.ptC - envelop.data()));
    }
  }
  listTriangles.shrink_to_fit();
}

// ============================================================================

} // namespace

} // namespace
