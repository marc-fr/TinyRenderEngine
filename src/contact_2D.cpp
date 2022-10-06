#include "tre_contact_2D.h"

#include "tre_utils.h"

#include <math.h>

namespace tre {

// Helpers ================================================================

inline static float tri_spin(const glm::vec2 &ptA, const glm::vec2 &ptB, const glm::vec2 &ptCt)
{
    return (ptA.x - ptCt.x) * (ptB.y - ptCt.y) - (ptB.x - ptCt.x) * (ptA.y - ptCt.y);
}

// ----------------------------------------------------------------------------

/// Project the point P on line (AB) with the direction "direction".
/// It returns parametric coords (u,v) such as AQ = u * AB = AP + v * direction
inline static glm::vec2 line_project(const glm::vec2 &vAB, const glm::vec2 &vAP, const glm::vec2 &direction)
{
  const glm::vec2 nAB = { -vAB.y, vAB.x };
  const glm::vec2 nRa = { -direction.y, direction.x };

  const float det = glm::dot(vAB, nRa);
  if (fabsf(det) < 1.e-6f)
    return { -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };

  glm::vec2 ret = { glm::dot(vAP, nRa), glm::dot(vAP, nAB) };
  ret *= (1.f / det);

  return ret;
}

// ----------------------------------------------------------------------------

inline static float tri_area(const glm::vec2 &ptA, const glm::vec2 &ptB, const glm::vec2 &ptCt)
{
    return 0.5f * fabsf( (ptA.x - ptCt.x) * (ptB.y - ptCt.y) - (ptB.x - ptCt.x) * (ptA.y - ptCt.y) ) ;
}

// ----------------------------------------------------------------------------

/// Compute the barycenter of the convex polygon {pts...}. Also compute the aera.
static void poly_barycenter(const std::vector<glm::vec2> pts, glm::vec2 &center, float &aera)
{
  center = glm::vec2(0.f);
  aera = 0.f;
  for (std::size_t iQ = 2, Npts = pts.size(); iQ < Npts; ++iQ)
  {
    const float weightCurr = tri_area(pts[0], pts[iQ - 1], pts[iQ]);
    center += weightCurr * (pts[0] + pts[iQ - 1] + pts[iQ]);
    aera += weightCurr;
  }
  TRE_ASSERT(aera != 0.f);
  center *= 1.f / ( 3.f * aera);
}

// (0D in 2D) Simple penetration test of a point of a polygon ==========================

bool s_contact2D::point_ydown(s_contact2D &cntDown,
                              const glm::vec2 &point,
                              const float ylim)
{
  if (point.y > ylim)
    return false;
  // we have contact
  cntDown.pt = { point.x, ylim };
  cntDown.normal = { 0.f, 1.f };
  cntDown.penet = ylim - point.y;
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_tri(const glm::vec2 &point,
                            const glm::vec2 &pt0, const glm::vec2 &pt1, const glm::vec2 &pt2)
{
  const bool b1 = tri_spin(pt0, pt1, point) < 0.f;
  const bool b2 = tri_spin(pt1, pt2, point) < 0.f;
  const bool b3 = tri_spin(pt2, pt0, point) < 0.f;
  return ((b1 == b2) && (b2 == b3));
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_tri(s_contact2D &cntTri,
                            const glm::vec2 &point,
                            const glm::vec2 &pt0, const glm::vec2 &pt1, const glm::vec2 &pt2)
{
  const float distsqareP0 = (point.x-pt0.x)*(point.x-pt0.x) + (point.y-pt0.y)*(point.y-pt0.y);
  const float distsqareP1 = (point.x-pt1.x)*(point.x-pt1.x) + (point.y-pt1.y)*(point.y-pt1.y);
  const float distsqareP2 = (point.x-pt2.x)*(point.x-pt2.x) + (point.y-pt2.y)*(point.y-pt2.y);
  if (distsqareP0 < 1.e-6f)
  {
    cntTri.pt = pt0;
    cntTri.penet = 0.f;
    cntTri.normal = glm::normalize(pt0*0.667f - pt1*0.333f - pt2*0.333f);
    return true;
  }
  if (distsqareP1 < 1.e-6f)
  {
    cntTri.pt = pt1;
    cntTri.penet = 0.f;
    cntTri.normal = glm::normalize(pt1*0.667f - pt0*0.333f - pt2*0.333f);
    return true;
  }
  if (distsqareP2 < 1.e-6f)
  {
    cntTri.pt = pt2;
    cntTri.penet = 0.f;
    cntTri.normal = glm::normalize(pt2*0.667f - pt1*0.333f - pt0*0.333f);
    return true;
  }
  float projP01 = tri_spin(pt0, pt1, point) / sqrtf((pt1.x-pt0.x)*(pt1.x-pt0.x) + (pt1.y-pt0.y)*(pt1.y-pt0.y));
  float projP12 = tri_spin(pt1, pt2, point) / sqrtf((pt2.x-pt1.x)*(pt2.x-pt1.x) + (pt2.y-pt1.y)*(pt2.y-pt1.y));
  float projP02 = tri_spin(pt2, pt0, point) / sqrtf((pt2.x-pt0.x)*(pt2.x-pt0.x) + (pt2.y-pt0.y)*(pt2.y-pt0.y));
  const bool b1 = projP01 < 0.f;
  const bool b2 = projP02 < 0.f;
  const bool b3 = projP12 < 0.f;
  projP01 = fabsf(projP01);
  projP02 = fabsf(projP02);
  projP12 = fabsf(projP12);

  if ((b1 == b2) && (b2 == b3))
  {
    if ( projP01<projP02 && projP01<projP12 )
    {
      cntTri.normal = glm::normalize(pt1-pt0);
      const float swap = - cntTri.normal.x;
      cntTri.normal.x = cntTri.normal.y;
      cntTri.normal.y = swap;
      if (b1) cntTri.normal = - cntTri.normal;
      cntTri.penet = projP01;
      cntTri.pt = point + cntTri.penet * cntTri.normal;
    }
    else if ( projP02<projP01 && projP02<projP12 )
    {
      cntTri.normal = glm::normalize(pt0-pt2);
      const float swap = - cntTri.normal.x;
      cntTri.normal.x = cntTri.normal.y;
      cntTri.normal.y = swap;
      if (b1) cntTri.normal = - cntTri.normal;
      cntTri.penet = projP02;
      cntTri.pt = point + cntTri.penet * cntTri.normal;
    }
    else
    {
      cntTri.normal = glm::normalize(pt2-pt1);
      const float swap = - cntTri.normal.x;
      cntTri.normal.x = cntTri.normal.y;
      cntTri.normal.y = swap;
      if (b1) cntTri.normal = - cntTri.normal;
      cntTri.penet = projP12;
      cntTri.pt = point + cntTri.penet * cntTri.normal;
    }
    return true;
  }
  //else
  return false;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_box(const glm::vec2 &point,
                            const glm::vec4 & AABB)
{
  const bool inX = AABB.x <= point.x && point.x <= AABB.z;
  const bool inY = AABB.y <= point.y && point.y <= AABB.w;
  if (!inX || !inY) return false;
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_box(s_contact2D &cntBox,
                            const glm::vec2 &point,
                            const glm::vec4 & AABB)
{
  const glm::vec2 ptA(AABB.x, AABB.y);
  const glm::vec2 ptB(AABB.z, AABB.w);

  const bool inX = ptA.x < point.x && point.x < ptB.x;
  const bool inY = ptA.y < point.y && point.y < ptB.y;
  if (!inX || !inY) return false;
  cntBox.penet = (ptB.x - ptA.x) + (ptB.y - ptA.y); // any max value
  {
    const float dist = point.x - ptA.x;
    if (dist < cntBox.penet)
    {
      cntBox.penet = dist;
      cntBox.normal = {-1.f, 0.f};
      cntBox.pt = {ptA.x, point.y};
    }
  }
  {
    const float dist = ptB.x - point.x;
    if (dist < cntBox.penet)
    {
      cntBox.penet = dist;
      cntBox.normal = {1.f, 0.f};
      cntBox.pt = {ptB.x, point.y};
    }
  }
  {
    const float dist = point.y - ptA.y;
    if (dist < cntBox.penet)
    {
      cntBox.penet = dist;
      cntBox.normal = {0.f, -1.f};
      cntBox.pt = {point.x, ptA.y};
    }
  }
  {
    const float dist = ptB.y - point.y;
    if (dist < cntBox.penet)
    {
      cntBox.penet = dist;
      cntBox.normal = {0.f, 1.f};
      cntBox.pt = {point.x, ptB.y};
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_poly(const glm::vec2 &point,
                             const std::vector<glm::vec2> &pts)
{
  if (pts.size() < 3)
    return false;

  const bool polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f;
  const float SignSpin = polySpin ? 1.f : -1.f;

  glm::vec2 ptPrev = pts.back();
  for (const glm::vec2 &ptCurr : pts)
  {
    if (tri_spin(ptPrev, ptCurr, point) * SignSpin < 0.f)
      return false;
    ptPrev = ptCurr;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_poly(s_contact2D &cntPoly,
                             const glm::vec2 &point,
                             const std::vector<glm::vec2> &pts)
{
  if (pts.size() < 3)
    return false;

  const bool polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f;
  const float SignSpin = polySpin ? 1.f : -1.f;

  cntPoly.penet = std::numeric_limits<float>::infinity();

  glm::vec2 ptPrev = pts.back();
  for (const glm::vec2 &ptCurr : pts)
  {
    const glm::vec2 edge = ptCurr - ptPrev;
    const glm::vec2 eN = SignSpin * glm::vec2(-edge.y, edge.x);
    const float d = glm::dot(point - ptPrev, eN);
    if (d < 0.f) return false;
    const float eNlen = glm::length(eN);
    const float dR = d / eNlen;
    if (cntPoly.penet > dR)
    {
      cntPoly.penet = dR;
      cntPoly.normal = -eN / eNlen;
    }
    ptPrev = ptCurr;
  }
  cntPoly.pt = point + cntPoly.penet * cntPoly.normal;
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_circle(const glm::vec2 &point,
                               const glm::vec2 center, const float radius)
{
  const glm::vec2 CP = point - center;
  const float CPn2 = glm::dot(CP, CP);
  const float r2 = radius * radius;
  return (CPn2 <= r2);
}

// ----------------------------------------------------------------------------

bool s_contact2D::point_circle(s_contact2D &cntCircle,
                               const glm::vec2 &point,
                               const glm::vec2 center, const float radius)
{
  const glm::vec2 CP = point - center;
  const float CPn2 = glm::dot(CP, CP);
  const float r2 = radius * radius;
  if (CPn2 > r2)
    return false;
  if (CPn2 < 1.e-12f)
  {
    cntCircle.normal = {1.f, 0.f};
    cntCircle.penet = radius;
    cntCircle.pt = center + radius * cntCircle.normal;
    return true;
  }
  const float CPn = sqrtf(CPn2);
  const float invCPn = 1.f / CPn;
  cntCircle.pt = center + CP * (radius * invCPn);
  cntCircle.normal = CP * invCPn;
  cntCircle.penet = radius - CPn;
  return true;
}

// (1D <-> 1D) Edge cross ===============================================================

bool s_contact2D::cross_line_line(glm::vec2 &crossPt,
                                  const glm::vec2 &pt0, const glm::vec2 &pt1,
                                  const glm::vec2 &ptA, const glm::vec2 &ptB)
{
  const glm::vec2 vAB = ptB - ptA;
  const glm::vec2 v01 = pt1 - pt0;
  const glm::vec2 v0A = ptA - pt0;

  const glm::vec2 cUV = line_project(v01, v0A, vAB);
  const float     coordu = cUV.x;
  const float     coordv = cUV.y;

  if (coordu < 0.f || coordu > 1.f)
    return false;

  if (coordv < 0.f || coordv > 1.f)
    return false;

  crossPt = pt0 + coordu * v01;
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::cross_yline_line(glm::vec2 &crossPt,
                                   const float y0,
                                   const glm::vec2 &ptA, const glm::vec2 &ptB)
{
  if (ptA.y > y0 && ptB.y > y0) return false;
  if (ptA.y < y0 && ptB.y < y0) return false;

  const glm::vec2 vAB = ptB - ptA;
  const glm::vec2 vA0 = glm::vec2(0.f, y0 - ptA.y);

  crossPt = glm::vec2(ptA.x + vAB.x * vA0.y / vAB.y, y0);

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::cross_line_circle(std::vector<glm::vec2> &crossPts,
                                    const glm::vec2 &pt0, const glm::vec2 &pt1,
                                    const glm::vec2 &center, const float radius)
{
  crossPts.reserve(2);
  crossPts.clear();

  const glm::vec2 v01 = pt1 - pt0;
  const glm::vec2 v0C = center - pt0;

  const glm::vec2 n01 = glm::normalize(glm::vec2(-v01.y, v01.x));

  const glm::vec2 coordUV = line_project(v01, v0C, n01); // can be inlined and simplified !

  const float delta = coordUV.y * coordUV.y - radius * radius;
  if (delta > 0.f)
    return false;

  if (delta > -1.e-4f)
  {
    // single cross pt
    if (0 <= coordUV.x && coordUV.x <= 1.f)
    {
      crossPts.push_back(pt0 + coordUV.x * v01);
      return true;
    }
    return false;
  }

  const float cd = sqrtf(-delta / glm::dot(v01,v01));

  const float u0 = coordUV.x - cd;
  const bool  valid0 = 0.f <= u0 && u0 <= 1.f;
  if (valid0)
    crossPts.push_back(pt0 + u0 * v01);

  const float u1 = coordUV.x + cd;
  const bool  valid1 = 0.f <= u1 && u1 <= 1.f;
  if (valid1)
    crossPts.push_back(pt0 + u1 * v01);

  return valid0 || valid1;
}

// (2D in 2D) ===============================================================

bool s_contact2D::ydown_poly(const float limy,
                             const std::vector<glm::vec2> &pts)
{
  if (pts.size() < 3)
    return false;

  for (const glm::vec2 &pt : pts)
  {
    if (pt.y <= limy) return true;
  }

  return false;
}

// ----------------------------------------------------------------------------

bool s_contact2D::ydown_poly(s_contact2D &cntDown,
                             const float limy,
                             const std::vector<glm::vec2> &pts)
{
  const std::size_t Npts = pts.size();
  if (Npts < 3)
    return false;

  cntDown.normal = { 0.f, 1.f };

  // find first inside pt
  std::size_t iP = 0;
  while (pts[iP].y <= limy && ++iP < Npts) {}
  if (iP == Npts)
  {
    // all points are inside.
    float dummy;
    poly_barycenter(pts, cntDown.pt, dummy);
    cntDown.penet = limy - pts[0].y;
    for (uint iQ = 1; iQ < Npts; ++iQ)
    {
      // penetration
      const float penetCurr = limy - pts[iQ].y;
      if (penetCurr > cntDown.penet) cntDown.penet = penetCurr;
    }
    return true;
  }
  while (pts[iP].y > limy && ++iP < Npts) {}
  if (iP == Npts)
  {
    if (pts[0].y <= limy)
      iP = 0;
    else
      return false; // all points are outside.
  }

  cntDown.penet = limy - pts[iP].y;

  const glm::vec2 *ptBefore = (iP == 0) ? &pts[Npts - 1] : &pts[iP - 1];

  glm::vec2 ptBegin;
  cross_yline_line(ptBegin, limy, *ptBefore, pts[iP]);

  // advance
  ptBefore = &pts[iP];
  iP = (iP == Npts - 1) ? 0 : iP + 1;

  glm::vec2 cntCenter = glm::vec2(0.f);
  float weightTotal = 0.f;

  while (pts[iP].y <= limy)
  {
    const glm::vec2 &ptCurr = pts[iP];
    // penetration
    const float penetCurr = limy - pts[iP].y;
    if (penetCurr > cntDown.penet) cntDown.penet = penetCurr;
    // triangle (ptBegin, ptBefore, ptCurr)
    const float weightCurr = tri_area(ptBegin, *ptBefore, ptCurr);
    cntCenter += weightCurr * (ptBegin + *ptBefore + ptCurr);
    weightTotal += 3.f * weightCurr;
    // advance
    ptBefore = &ptCurr;
    iP = (iP == Npts - 1) ? 0 : iP + 1;
  }

  glm::vec2 ptEnd;
  cross_yline_line(ptEnd, limy, *ptBefore, pts[iP]);

  {
    // triangle (ptBegin, ptBefore, ptEnd)
    const float weightCurr = tri_area(ptBegin, *ptBefore, ptEnd);
    cntCenter += weightCurr * (ptBegin + *ptBefore + ptEnd);
    weightTotal += 3.f * weightCurr;
  }

  cntDown.pt = weightTotal < 1.e-8f ? ptBegin : cntCenter * (1.f / weightTotal);

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::ydown_circle(const float limy, const glm::vec2 &center, const float radius)
{
  return (center.y - radius <= limy);
}

// ----------------------------------------------------------------------------

bool s_contact2D::ydown_circle(s_contact2D &cntDown,
                             const float limy,
                             const glm::vec2 &center, const float radius)
{
  if (center.y - radius > limy) return false; // early return

  const float cntPtY = (center.y + radius < limy) ? center.y : 0.5f * limy + (center.y - radius) * 0.5f; // approxim.

  cntDown.pt = { center.x, cntPtY };
  cntDown.normal = { 0.f, 1.f };
  cntDown.penet = limy - center.y + radius;

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::box_box(const glm::vec4 &AABB_0, const glm::vec4 &AABB_1)
{
  if ( AABB_0.x > AABB_1.z ) return false;
  if ( AABB_0.z < AABB_1.x ) return false;
  if ( AABB_0.y > AABB_1.w ) return false;
  if ( AABB_0.w < AABB_1.y ) return false;
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::box_poly(const glm::vec4 &AABB,
                           const std::vector<glm::vec2> &pts)
{
  if (pts.size() < 3)
    return false;

  // using "shadow" technique

  // edges of box
  glm::vec2 bmin = pts[0], bmax = pts[0];
  for (const glm::vec2 &pt : pts)
  {
    bmin = glm::min(bmin, pt);
    bmax = glm::max(bmax, pt);
  }
  if (bmax.x < AABB.x) return false;
  if (bmax.y < AABB.y) return false;
  if (bmin.x > AABB.z) return false;
  if (bmin.y > AABB.w) return false;

  // edges of poly
  const bool polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f;
  const float SignSpin = polySpin ? 1.f : -1.f;
  glm::vec2 ptPrev = pts.back();
  for (const glm::vec2 &pt : pts)
  {
    const glm::vec2 e = pt - ptPrev;
    const glm::vec2 n = SignSpin * glm::vec2(-e.y, e.x);
    if (glm::dot(glm::vec2(AABB.x, AABB.y) - ptPrev, n) < 0.f &&
        glm::dot(glm::vec2(AABB.x, AABB.w) - ptPrev, n) < 0.f &&
        glm::dot(glm::vec2(AABB.z, AABB.y) - ptPrev, n) < 0.f &&
        glm::dot(glm::vec2(AABB.z, AABB.w) - ptPrev, n) < 0.f) return false;
    ptPrev = pt;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::box_poly(s_contact2D &cntBox,
                           const glm::vec4 &AABB,
                           const std::vector<glm::vec2> &pts)
{
  std::vector<glm::vec2> boxPoly(4);
  boxPoly[0] = glm::vec2(AABB.x, AABB.y);
  boxPoly[1] = glm::vec2(AABB.x, AABB.w);
  boxPoly[2] = glm::vec2(AABB.z, AABB.w);
  boxPoly[3] = glm::vec2(AABB.z, AABB.y);

  return poly_poly(cntBox, boxPoly, pts);
}

// ----------------------------------------------------------------------------

bool s_contact2D::box_circle(const glm::vec4 &AABB, const glm::vec2 &center, const float radius)
{
  const glm::vec2 ptA(AABB.x, AABB.y);
  const glm::vec2 ptB(AABB.z, AABB.w);

  const bool inX_large = ptA.x - radius < center.x && center.x < ptB.x + radius;
  const bool inY_large = ptA.y - radius < center.y && center.y < ptB.y + radius;
  if (!inX_large || !inY_large) return false;

  const bool inX_exact = ptA.x < center.x && center.x < ptB.x;
  const bool inY_exact = ptA.y < center.y && center.y < ptB.y;

  if (!inX_exact && !inY_exact)
  {
    float penet = (ptB.x - ptA.x) + (ptB.y - ptA.y); // any max value
    const glm::vec2 p[4] = { ptA, ptB, {ptA.x, ptB.y}, {ptB.x, ptA.y} };
    for (uint i = 0; i < 4; ++i)
    {
      const glm::vec2 v = center - p[i];
      const float dist = glm::length(v);
      if (dist < radius && radius - dist < penet)
      {
        penet = radius - dist;
      }
    }
    if (penet > radius)
      return false;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::box_circle(s_contact2D &cntBox,
                             const glm::vec4 &AABB,
                             const glm::vec2 &center, const float radius)
{
  const glm::vec2 ptA(AABB.x, AABB.y);
  const glm::vec2 ptB(AABB.z, AABB.w);

  const bool inX_large = ptA.x - radius < center.x && center.x < ptB.x + radius;
  const bool inY_large = ptA.y - radius < center.y && center.y < ptB.y + radius;
  if (!inX_large || !inY_large) return false;

  const bool inX_exact = ptA.x < center.x && center.x < ptB.x;
  const bool inY_exact = ptA.y < center.y && center.y < ptB.y;

  if (inY_exact)
  {
    const float dist0 = center.x - ptA.x + radius;
    const float dist1 = ptB.x - center.x + radius;

    if (dist0 < dist1)
    {
      cntBox.penet = dist0;
      cntBox.normal = {-1.f, 0.f};
      if (dist0 > 2.f * radius) cntBox.pt = center;
      else                      cntBox.pt = {0.5f * (center.x + radius) + 0.5f * ptA.x, center.y};
    }
    else
    {
      cntBox.penet = dist1;
      cntBox.normal = {1.f, 0.f};
      if (dist1 > 2.f * radius) cntBox.pt = center;
      else                      cntBox.pt = {0.5f * (center.x - radius) + 0.5f * ptB.x, center.y};
    }
  }
  if (inX_exact)
  {
    const float dist0 = center.y - ptA.y + radius;
    const float dist1 = ptB.y - center.y + radius;

    if (dist0 < dist1)
    {
      cntBox.penet = dist0;
      cntBox.normal = {0.f, -1.f};
      if (dist0 > 2.f * radius) cntBox.pt = center;
      else                      cntBox.pt = {center.x, 0.5f * (center.y + radius) + 0.5f * ptA.y};
    }
    else
    {
      cntBox.penet = dist1;
      cntBox.normal = {0.f, 1.f};
      if (dist1 > 2.f * radius) cntBox.pt = center;
      else                      cntBox.pt = {center.x, 0.5f * (center.y - radius) + 0.5f * ptB.y};
    }
  }
  if (!inX_exact && !inY_exact)
  {
    cntBox.penet = (ptB.x - ptA.x) + (ptB.y - ptA.y); // any max value
    const glm::vec2 p[4] = { ptA, ptB, {ptA.x, ptB.y}, {ptB.x, ptA.y} };
    for (uint i = 0; i < 4; ++i)
    {
      const glm::vec2 v = center - p[i];
      const float dist = glm::length(v);
      if (dist < radius && radius - dist < cntBox.penet)
      {
        cntBox.penet = radius - dist;
        cntBox.normal = v / dist;
      }
    }
    if (cntBox.penet > radius)
      return false;
    cntBox.pt = center - (radius - 0.5f * cntBox.penet) * cntBox.normal;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::circle_poly(const glm::vec2 &center, const float radius,
                              const std::vector<glm::vec2> &pts)
{
  if (pts.size() < 3)
    return false;

  if (point_poly(center, pts))
      return true;

  glm::vec2 ptPrev = pts.back();
  for (const glm::vec2 &ptCurr : pts)
  {
    std::vector<glm::vec2> cntPoints;
    if (cross_line_circle(cntPoints, ptPrev, ptCurr, center, radius))
      return true;
    ptPrev = ptCurr;
  }

  return false;
}

// ----------------------------------------------------------------------------

bool s_contact2D::circle_poly(s_contact2D &cntCircle,
                              const glm::vec2 &center, const float radius,
                              const std::vector<glm::vec2> &pts)
{
  const std::size_t Npts = pts.size();
  if (Npts < 3)
    return false;

  // pre-compute points inside the circle
  std::vector<bool> ptsInside(Npts);
  for (std::size_t i = 0; i < Npts; ++i)
    ptsInside[i] = point_circle(pts[i], center, radius); // can be inlined and improved.

  // get the first point inside
  std::size_t ptInside = 0;
  if (ptsInside[Npts - 1] == true)
  {
    ptInside = Npts;
    while (ptInside-- != 0 && ptsInside[ptInside] == true) {}
    ++ptInside;
  }
  else
  {
    while (ptInside < Npts && ptsInside[ptInside] == false) { ++ptInside; }
  }

  // no pt inside -> check all the poly's edge
  if (ptInside == Npts)
  {
    const float polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f ? 1.f : -1.f;
    std::vector<glm::vec2> crossPts;
    glm::vec2 ptPrev = pts.back();
    for (const glm::vec2 &ptCurr : pts)
    {
      if (cross_line_circle(crossPts, ptPrev, ptCurr, center, radius))
      {
        const glm::vec2 midPt = 0.5f * (crossPts.front() + crossPts.back());
        const glm::vec2 vPrevCurr = ptCurr - ptPrev;
        const glm::vec2 inNormal = glm::normalize(glm::vec2(-vPrevCurr.y, vPrevCurr.x)) * polySpin;
        const glm::vec2 vCP = midPt - center;
        const float distCP = glm::dot(inNormal, vCP);
        cntCircle.normal = inNormal;
        cntCircle.penet = radius - distCP;
        cntCircle.pt = center + 0.5f * (radius + distCP) * inNormal;
        return true; // it should has only 1 match (the contact is undefined if not)
      }
      ptPrev = ptCurr;
    }
    return false;
  }

  // get the first point outside
  std::size_t ptOutside = ptsInside[Npts - 1] == true ? 0 : ptInside;
  while (ptOutside < Npts && ptsInside[ptOutside] == true) { ++ptOutside;  }

  // no pt outside -> the poly is included in the circle
  if (ptOutside == Npts)
    return false;

  // compute the 2 cross-points
  const std::size_t ptOutsideLast = ptInside == 0 ? Npts - 1 : ptInside - 1;
  const std::size_t ptInsideFirst = ptInside;
  const std::size_t ptInsideLast = ptOutside == 0 ? Npts - 1 : ptOutside - 1;
  const std::size_t ptOutsideFirst = ptOutside;

  std::vector<glm::vec2> ptsCross;
  cross_line_circle(ptsCross, pts[ptOutsideLast], pts[ptInsideFirst], center, radius);
  if (ptsCross.size() != 1)
    return false;
  const glm::vec2 ptCrossBegin = ptsCross[0];

  cross_line_circle(ptsCross, pts[ptInsideLast], pts[ptOutsideFirst], center, radius);
  if (ptsCross.size() != 1)
    return false;
  const glm::vec2 ptCrossEnd = ptsCross[0];

  const glm::vec2 vCrossBeginEnd = ptCrossEnd - ptCrossBegin;
  const float vCrossSign = tri_spin(ptCrossBegin, ptCrossEnd, center) >= 0.f ? 1.f : -1.f;
  const float circleCurvatureDepth = radius + vCrossSign * sqrtf(radius * radius - 0.25f * glm::dot(vCrossBeginEnd, vCrossBeginEnd));

  // compute the normal
  if (glm::dot(vCrossBeginEnd, vCrossBeginEnd) > 1.e-12f)
  {
    const float polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f ? 1.f : -1.f;
    cntCircle.normal = polySpin * glm::normalize(glm::vec2(-vCrossBeginEnd.y, vCrossBeginEnd.x));
  }
  else
  {
    cntCircle.normal = glm::normalize(vCrossBeginEnd - center);
  }

  // compute the barycenter
  {
    glm::vec2 ptSum(0.f);
    float aera = 0.f;
    for (std::size_t iP = 0; iP < Npts; ++iP)
    {
      if (!ptsInside[iP])
        continue;
      const float weightCurr = tri_area(ptCrossBegin, ptCrossEnd, pts[iP]);
      ptSum += weightCurr * (ptCrossBegin + ptCrossEnd + pts[iP]);
      aera += weightCurr;
    }
    ptSum /= 3.f;
    const float aeraCircleCurve = circleCurvatureDepth * glm::length(vCrossBeginEnd);
    ptSum += aeraCircleCurve * (center + (radius - 0.5f * circleCurvatureDepth) * cntCircle.normal);
    aera  += aeraCircleCurve;

    cntCircle.pt = aera < 1.e-8f ? ptCrossBegin : ptSum / aera;
  }

  // compute the penetration
  {
    float maxCoord = glm::dot(ptCrossBegin, cntCircle.normal); // == glm::dot(ptCrossEnd, cntCircle.normal)
    float minCoord = maxCoord;

    for (std::size_t iP = 0; iP < Npts; ++iP)
    {
      if (!ptsInside[iP])
        continue;
      const float coord = glm::dot(pts[iP], cntCircle.normal);
      if (coord > maxCoord) maxCoord = coord;
      if (coord < minCoord) minCoord = coord;
    }

    cntCircle.penet = maxCoord - minCoord + circleCurvatureDepth;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::circle_circle(s_contact2D &cntCircleA,
                                const glm::vec2 &centerA, const float radiusA,
                                const glm::vec2 &centerB, const float radiusB)
{
  const float radiusApB = radiusA + radiusB;
  const float radiusApBsquared = radiusApB * radiusApB;

  const glm::vec2 vBA = centerA - centerB;
  const float distSquared = glm::dot(vBA, vBA);
  if (distSquared > radiusApBsquared)
    return false;

  cntCircleA.pt = (centerA / radiusA + centerB / radiusB) / (1.f/radiusA + 1.f/radiusB);
  cntCircleA.penet = sqrtf(distSquared) - radiusApB;
  cntCircleA.normal = glm::normalize(vBA);

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact2D::poly_poly(const std::vector<glm::vec2> ptsA,
                            const std::vector<glm::vec2> ptsB)
{
  const uint NptsA = ptsA.size();
  const uint NptsB = ptsB.size();

  if (NptsA < 3 || NptsB < 3)
    return false;

  // using shadow technique (with diagonals)

  const std::vector<glm::vec2> &ptsM = (NptsA < NptsB) ? ptsA : ptsB;
  const std::vector<glm::vec2> &ptsN = (NptsA < NptsB) ? ptsB : ptsA;

  glm::vec2 centerM = glm::vec2(0.f);
  for (const glm::vec2 &ptM : ptsM) centerM += ptM;
  centerM *= (1.f / ptsM.size());

  glm::vec2 ptNprev = ptsN.back();
  for (const glm::vec2 &ptNcurr : ptsN)
  {
    const glm::vec2 vNpNc = ptNcurr - ptNprev;
    const glm::vec2 vNpC = centerM - ptNprev;
    for (const glm::vec2 &ptM : ptsM)
    {
      const glm::vec2 cUV = line_project(vNpNc, vNpC, ptM - centerM);
      if (cUV.x >= 0.f && cUV.x <= 1.f && cUV.y >= 0.f && cUV.y <= 1.f)
        return true;
    }
    ptNprev = ptNcurr;
  }

  return false;
}

// ----------------------------------------------------------------------------

bool s_contact2D::poly_poly(s_contact2D &cntPolyA,
                            const std::vector<glm::vec2> ptsA,
                            const std::vector<glm::vec2> ptsB)
{
  const std::size_t NptsA = ptsA.size();
  const std::size_t NptsB = ptsB.size();

  if (NptsA < 3 || NptsB < 3)
    return false;

  // pre-compute points inside the geometry.
  std::vector<bool> ptsBInside(NptsB);
  std::vector<bool> ptsAInside(NptsA);
  for (std::size_t iB = 0; iB < NptsB; ++iB)
    ptsBInside[iB] = point_poly(ptsB[iB], ptsA);
  for (std::size_t iA = 0; iA < NptsA; ++iA)
    ptsAInside[iA] = point_poly(ptsA[iA], ptsB);

  // get points range for the polyB iP{being, end}
  std::size_t iBbegin = 0;
  if (ptsBInside[NptsB - 1] == true || ptsBInside[0] == false)
  {
    while (iBbegin < NptsB && ptsBInside[iBbegin] == true) { ++iBbegin; }
  }
  if (iBbegin == NptsB)
  {
    // the polyB is included in the polyA
    float dummy;
    glm::vec2 localCenter;
    poly_barycenter(ptsB, localCenter, dummy);
    point_poly(cntPolyA, localCenter, ptsA);
    cntPolyA.pt = localCenter;
    return true;
  }
  while (ptsBInside[iBbegin] == false && ++iBbegin < NptsB) {}

  std::size_t iBend = ptsBInside[NptsB - 1] == true ? 0 : iBbegin;
  while (iBend < NptsB && ptsBInside[iBend] == true) { ++iBend;  }

  // get points range for the polyA iB{begin, end}
  std::size_t iAbegin = 0;
  if (ptsAInside[NptsA - 1] == true || ptsAInside[0] == false)
  {
    while (iAbegin < NptsA && ptsAInside[iAbegin] == true) { ++iAbegin; }
  }
  if (iAbegin == NptsA)
  {
    // the polyA is included in the polyB
    float dummy;
    glm::vec2 localCenter;
    poly_barycenter(ptsA, localCenter, dummy);
    point_poly(cntPolyA, localCenter, ptsB);
    cntPolyA.normal = -cntPolyA.normal;
    cntPolyA.pt = localCenter;
    return true;
  }
  while (iAbegin < NptsA && ptsAInside[iAbegin] == false) { ++iAbegin; }

  std::size_t iAend = ptsAInside[NptsA - 1] == true ? 0 : iAbegin;
  while (iAend < NptsA && ptsAInside[iAend] == true) { ++iAend; }

  // check validity
  if (iBbegin == NptsB && iAbegin == NptsA)
  {
    // all points are outside.
    // note: it ignores some case where there could have a non-null intersection.
    return false;
  }

  // compute the 2 cross-points
  // (and compute the normal)

  const std::size_t iBbeginM1 = iBbegin == 0 ? NptsB - 1 : iBbegin - 1;
  const std::size_t iBendM1 = iBend == 0 ? NptsB - 1 : iBend - 1;

  const std::size_t iAbeginM1 = iAbegin == 0 ? NptsA - 1 : iAbegin - 1;
  const std::size_t iAendM1 = iAend == 0 ? NptsA - 1 : iAend - 1;

  glm::vec2 ptCrossBegin;
  glm::vec2 ptCrossEnd;

  if (iBbegin == NptsB)
  {
    glm::vec2 ptPrev = ptsB.back();
    std::size_t iB;
    for (iB = 0; iB < NptsB; ++iB)
    {
      if (cross_line_line(ptCrossEnd, ptsA[iAbeginM1], ptsA[iAbegin], ptPrev, ptsB[iB]))
        break;
      ptPrev = ptsB[iB];
    }
    const bool crossSameLine = cross_line_line(ptCrossBegin, ptsA[iAendM1], ptsA[iAend], ptPrev, ptsB[iB]);
    if (!crossSameLine)
      return false;

    const glm::vec2 vecSide = ptsB[iB] - ptPrev;
    const float polySpinB = (tri_spin(ptsB[0],ptsB[1],ptsB[2]) > 0.f) ? 1.f : -1.f;
    cntPolyA.normal = polySpinB * glm::normalize(glm::vec2(-vecSide.y, vecSide.x));

  }
  else if (iAbegin == NptsA)
  {
    glm::vec2 ptPrev = ptsA.back();
    std::size_t iA;
    for (iA = 0; iA < NptsA; ++iA)
    {
      if (cross_line_line(ptCrossBegin, ptsB[iBbeginM1], ptsB[iBbegin], ptPrev, ptsA[iA]))
        break;
      ptPrev = ptsA[iA];
    }
    const bool crossSameLine = cross_line_line(ptCrossEnd, ptsB[iBendM1], ptsB[iBend], ptPrev, ptsA[iA]);
    if (!crossSameLine)
      return false;

    const glm::vec2 vecSide = ptsA[iA] - ptPrev;
    const float polySpinA = (tri_spin(ptsA[0],ptsA[1],ptsA[2]) > 0.f) ? -1.f : 1.f;
    cntPolyA.normal = polySpinA * glm::normalize(glm::vec2(-vecSide.y, vecSide.x));
  }
  else
  {
    if (!cross_line_line(ptCrossBegin, ptsB[iBbeginM1], ptsB[iBbegin], ptsA[iAbeginM1], ptsA[iAbegin]))
      cross_line_line(ptCrossBegin,    ptsB[iBbeginM1], ptsB[iBbegin], ptsA[iAendM1],   ptsA[iAend]); // TODO : assert it is true.

    if (!cross_line_line(ptCrossEnd, ptsB[iBendM1], ptsB[iBend], ptsA[iAendM1],   ptsA[iAend]))
      cross_line_line(ptCrossEnd,    ptsB[iBendM1], ptsB[iBend], ptsA[iAbeginM1], ptsA[iAbegin]); // TODO : assert it is true.

    glm::vec2 vecSide = ptCrossEnd - ptCrossBegin;
    const float polySpin = (tri_spin(ptsB[0],ptsB[1],ptsB[2]) > 0.f) ? 1.f : -1.f;

    if (glm::dot(vecSide, vecSide) < 1.e-12f)
      cntPolyA.normal = glm::vec2(0.f, 0.f);
    else
      cntPolyA.normal = polySpin * glm::normalize(glm::vec2(-vecSide.y, vecSide.x));
  }

  // compute the barycenter
  {
    glm::vec2 ptSum(0.f);
    float aera = 0.f;
    for (std::size_t iB = 0; iB < NptsB; ++iB)
    {
      if (!ptsBInside[iB])
        continue;
      const float weightCurr = tri_area(ptCrossBegin, ptCrossEnd, ptsB[iB]);
      ptSum += weightCurr * (ptCrossBegin + ptCrossEnd + ptsB[iB]);
      aera += weightCurr;
    }
    for (std::size_t iA = 0; iA < NptsA; ++iA)
    {
      if (!ptsAInside[iA])
        continue;
      const float weightCurr = tri_area(ptCrossBegin, ptCrossEnd, ptsA[iA]);
      ptSum += weightCurr * (ptCrossBegin + ptCrossEnd + ptsA[iA]);
      aera += weightCurr;
    }
    cntPolyA.pt = aera < 1.e-8f ? ptCrossBegin : ptSum / ( 3.f * aera);
  }

  // compute the penetration
  {
    float maxCoord = glm::dot(ptCrossBegin, cntPolyA.normal); // == glm::dot(ptCrossEnd, cntPolyA.normal)
    float minCoord = maxCoord;

    for (std::size_t iB = 0; iB < NptsB; ++iB)
    {
      if (!ptsBInside[iB])
        continue;
      const float coord = glm::dot(ptsB[iB], cntPolyA.normal);
      if (coord > maxCoord) maxCoord = coord;
      if (coord < minCoord) minCoord = coord;
    }
    for (std::size_t iA = 0; iA < NptsA; ++iA)
    {
      if (!ptsAInside[iA])
        continue;
      const float coord = glm::dot(ptsA[iA], cntPolyA.normal);
      if (coord > maxCoord) maxCoord = coord;
      if (coord < minCoord) minCoord = coord;
    }

    cntPolyA.penet = maxCoord - minCoord;
  }

  return true;
}


// ============================================================================

bool s_contact2D::raytrace_ydown(s_contact2D &hitInfo,
                                 const glm::vec2 &origin, const glm::vec2 &direction,
                                 float limy)
{
  TRE_ASSERT(fabsf(glm::length(direction) - 1.f) <= 1.e-6f);

  if (direction.y >= 0)
    return false;

  hitInfo.penet = (limy - origin.y) / direction.y;
  hitInfo.pt = origin + hitInfo.penet * direction;
  hitInfo.normal = glm::vec2(0.f, 1.f);

  return true;
}

// -----------------------------------------------------------------

bool s_contact2D::s_contact2D::raytrace_box(s_contact2D &hitInfo,
                                            const glm::vec2 & origin, const glm::vec2 & direction,
                                            const glm::vec4 & AABB)
{
  TRE_ASSERT(fabsf(glm::length(direction) - 1.f) <= 1.e-6f);

  const glm::vec2 ptA(AABB.x, AABB.y);
  const glm::vec2 ptB(AABB.z, AABB.w);

  if (direction.x < 0.f) // can hit the border:Right
  {
    const glm::vec2 puv = line_project( {0.f, ptA.y - ptB.y}, origin - ptB, direction);
    if (puv.x >= 0.f && puv.x <= 1.f)
    {
      hitInfo.penet = puv.y;
      hitInfo.pt = origin + hitInfo.penet * direction;
      hitInfo.normal = glm::vec2(1.f, 0.f);
      return true;
    }
  }
  if (direction.x > 0.f) // can hit the border:Left
  {
    const glm::vec2 puv = line_project( {0.f, ptB.y - ptA.y}, origin - ptA, direction);
    if (puv.x >= 0.f && puv.x <= 1.f)
    {
      hitInfo.penet = puv.y;
      hitInfo.pt = origin + hitInfo.penet * direction;
      hitInfo.normal = glm::vec2(-1.f, 0.f);
      return true;
    }
  }

  if (direction.y < 0.f) // can hit the border:Top
  {
    const glm::vec2 puv = line_project( {ptA.x - ptB.x, 0.f}, origin - ptB, direction);
    if (puv.x >= 0.f && puv.x <= 1.f)
    {
      hitInfo.penet = puv.y;
      hitInfo.pt = origin + hitInfo.penet * direction;
      hitInfo.normal = glm::vec2(0.f, 1.f);
      return true;
    }
  }
  if (direction.y > 0.f) // can hit the border:Bottom
  {
    const glm::vec2 puv = line_project( {ptB.x - ptA.x, 0.f}, origin - ptA, direction);
    if (puv.x >= 0.f && puv.x <= 1.f)
    {
      hitInfo.penet = puv.y;
      hitInfo.pt = origin + hitInfo.penet * direction;
      hitInfo.normal = glm::vec2(0.f, -1.f);
      return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------

bool s_contact2D::raytrace_circle(s_contact2D &hitInfo,
                                  const glm::vec2 & origin, const glm::vec2 &direction,
                                  const glm::vec2 & center, const float radius)
{
    TRE_ASSERT(fabsf(glm::length(direction) - 1.f) <= 1.e-6f);

  const glm::vec2 v0C = center -  origin;

  const glm::vec2 nDireciton = glm::normalize(glm::vec2(-direction.y, direction.x));

  const glm::vec2 coordUV = line_project(direction, v0C, nDireciton); // can be inlined and simplified !

  const float delta = coordUV.y * coordUV.y - radius * radius;
  if (delta > 0.f)
    return false;

  const float cd = sqrtf(-delta);

  hitInfo.penet = coordUV.x - cd;
  hitInfo.pt =  origin + hitInfo.penet * direction;
  hitInfo.normal = glm::normalize(hitInfo.pt - center);

  if (glm::dot(hitInfo.normal, direction) > 0.f)
  {
    hitInfo.penet = coordUV.x + cd;
    hitInfo.pt =  origin + hitInfo.penet * direction;
    hitInfo.normal = glm::normalize(hitInfo.pt - center);
  }

  TRE_ASSERT(glm::dot(hitInfo.normal, direction) <= 0.f);
  return true;
}

// -----------------------------------------------------------------

bool s_contact2D::raytrace_poly(s_contact2D &hitInfo,
                                const glm::vec2 &origin, const glm::vec2 &direction,
                                const std::vector<glm::vec2> pts)
{
  TRE_ASSERT(fabsf(glm::length(direction) - 1.f) <= 1.e-6f);

  const std::size_t Npts = pts.size();
  TRE_ASSERT(Npts >= 3);

  const bool polySpin = tri_spin(pts[0],pts[1],pts[2]) > 0.f;
  const float signSpin = polySpin ? 1.f : -1.f;

  const glm::vec2 directTangent = signSpin * glm::vec2(-direction.y, direction.x);

  const glm::vec2 *prevPt = &pts[Npts - 1];
  for (const glm::vec2 &currPt : pts)
  {
    const glm::vec2 vAB = currPt - *prevPt;
    if (glm::dot(vAB, directTangent) < 0.f)
    {
      const glm::vec2 cUV = line_project(vAB, origin - *prevPt, direction);
      if (cUV.x >= 0.f && cUV.x <= 1.f)
      {
        hitInfo.penet = cUV.y;
        hitInfo.pt = origin + hitInfo.penet * direction;
        hitInfo.normal = signSpin * glm::normalize(glm::vec2(vAB.y, -vAB.x));
        return true;
      }
    }
    prevPt = &currPt;
  }

  return false;
}

} // namespace
