#include "tre_contact_3D.h"

#include "tre_utils.h"

#include <math.h>

namespace tre {

// Skin Kd-Tree ====================================================================

void s_contact3D::s_skinKdTree::computeTree()
{
  TRE_ASSERT(skin.size() % 3 == 0);
  const std::size_t nTri = skin.size() / 3;
  TRE_ASSERT(nTri != 0);
  TRE_ASSERT(nTri <= 0x7FFF); // 15bits used to encode triangle id

  std::vector<s_boundbox> treeBox;

  tree.clear();
  tree.reserve(1024);
  treeBox.reserve(1024);

  tree.push_back(s_node());
  treeBox.push_back(s_boundbox());

  std::vector<s_boundbox> triBox;
  triBox.resize(skin.size() / 3);
  for (std::size_t tri = 0, stop = nTri; tri < stop; ++tri)
  {
    triBox[tri].m_min = glm::min(glm::min(skin[3 * tri + 0], skin[3 * tri + 1]), skin[3 * tri + 2]);
    triBox[tri].m_max = glm::max(glm::max(skin[3 * tri + 0], skin[3 * tri + 1]), skin[3 * tri + 2]);
    treeBox[0].m_min = glm::min(treeBox[0].m_min, triBox[tri].m_min);
    treeBox[0].m_max = glm::max(treeBox[0].m_max, triBox[tri].m_max);
  }

  std::vector<float> triCP;
  triCP.reserve(nTri);

  std::vector<std::size_t> nodeStack, nodeStackNew;
  nodeStack.push_back(0);

  std::size_t idepth = 0;
  for (; idepth < kMaxDepth && !nodeStack.empty(); ++idepth)
  {
    nodeStackNew.clear();
    for (std::size_t nid : nodeStack)
    {
      s_node &node = tree[nid];
      const s_boundbox &nodeBox = treeBox[nid];
      // select the split direction
      const glm::vec3 bExtend = nodeBox.extend();
      const float     bExtendMax = std::max(std::max(bExtend.x, bExtend.y), bExtend.z);
      {
        // select split direction
        unsigned dir = 0;
        if (bExtendMax == bExtend.y) dir = 1;
        else if (bExtendMax == bExtend.z) dir = 2;
        // select the split point
        const float splitPoint = nodeBox.center()[dir];
        const std::size_t nidA = tree.size();
        const std::size_t nidB = nidA + 1;
        TRE_ASSERT(nidB <= 0x7FFF);
        // compute the child bounding-boxes
        s_boundbox bbA = nodeBox, bbB = nodeBox;
        if (dir == 0) { bbA.m_max.x = splitPoint; bbB.m_min.x = splitPoint; }
        else if (dir == 1) { bbA.m_max.y = splitPoint; bbB.m_min.y = splitPoint; }
        else  /*(dir == 2)*/ { bbA.m_max.z = splitPoint; bbB.m_min.z = splitPoint; }
        // check if the split is ok
        std::size_t ntriP = 0, ntriA = 0, ntriB = 0;
        for (std::size_t tri = 0, stop = nTri; tri < stop; ++tri)
        {
          if (box_box(nodeBox, triBox[tri])) ++ntriP;
          if (box_box(bbA, triBox[tri])) ++ntriA;
          if (box_box(bbB, triBox[tri])) ++ntriB;
        }
        ntriP = ntriP / 2 + ntriP / 4;
        if (ntriA > ntriP || ntriB > ntriP)
          continue;
        // ok, do the split
        tree.push_back(s_node()); treeBox.push_back(bbA);
        tree.push_back(s_node()); treeBox.push_back(bbB);
        tree[nid].flags = (unsigned(nidA) << 17) | (unsigned(nidB) << 2) | (dir + 1);
        tree[nid].split = splitPoint;
        nodeStackNew.push_back(nidA);
        nodeStackNew.push_back(nidB);
      }
    }
    nodeStack.swap(nodeStackNew);
  }

  // build the triangle list of each leaf
  std::size_t leafNodes = 0;
  std::size_t triCurr = 0;
  for (std::size_t nid = 0, stop = tree.size(); nid < stop; ++nid)
  {
    s_node &node = tree[nid];
    if (node.flags != 0) continue;
    const s_boundbox &nodeBox = treeBox[nid];
    const std::size_t rangeTriA = triCurr;
    for (std::size_t tri = 0; tri < rangeTriA; ++tri)
    {
      if (box_box(nodeBox, triBox[tri])) // duplicate the triangle
      {
        skin.insert(skin.begin() + triCurr * 3 + 0, skin[tri * 3 + 0]);
        skin.insert(skin.begin() + triCurr * 3 + 1, skin[tri * 3 + 1]);
        skin.insert(skin.begin() + triCurr * 3 + 2, skin[tri * 3 + 2]);
        triBox.insert(triBox.begin() + triCurr, triBox[tri]);
        ++triCurr;
      }
    }
    for (std::size_t tri = triCurr, triC = skin.size() / 3; tri < triC; ++tri)
    {
      if (box_box(nodeBox, triBox[tri])) // swap the triangle
      {
        std::swap(skin[triCurr * 3 + 0], skin[tri * 3 + 0]);
        std::swap(skin[triCurr * 3 + 1], skin[tri * 3 + 1]);
        std::swap(skin[triCurr * 3 + 2], skin[tri * 3 + 2]);
        std::swap(triBox[triCurr], triBox[tri]);
        ++triCurr;
      }
    }
    node.flags = uint32_t((rangeTriA << 17) | (triCurr << 2));
    ++leafNodes;
  }

  TRE_LOG("Kd-Tree built: " << skin.size() / 3 << " triangles (" << skin.size() / 3 - nTri << " duplicated), " << tree.size() << " nodes (" << leafNodes << " leaves), max-depth = " << idepth);
}

// ----------------------------------------------------------------------------

/// @brief Templated sample of a skin Kd-Tree. The functor is a struct with "operator()(pt0, pt1, pt2) -> bool" defined. Functor can be a lambda.
/// It stops processing as soon as a call returned true.
template <typename _fnct>
bool _skinKdTree_contains(const s_contact3D::s_skinKdTree &skinKdTree, const glm::vec3 &pos, _fnct &fnct)
{
  TRE_ASSERT(skinKdTree.skin.size() % 3 == 0);
  TRE_ASSERT(!skinKdTree.tree.empty());

  std::size_t nodeCurr = 0;
  while (true)
  {
    const s_contact3D::s_skinKdTree::s_node &cnode = skinKdTree.tree[nodeCurr];
    const unsigned dir = cnode.getDir();
    if (dir != 0)
    {
      nodeCurr = (pos[dir-1] <= cnode.split) ? cnode.getLeft() : cnode.getRight();
    }
    else
    {
      const unsigned triStart = cnode.getLeft();
      const unsigned triStop  = cnode.getRight();
      for (unsigned tri = triStart; tri < triStop; ++tri)
      {
        if (fnct(skinKdTree.skin[tri * 3 + 0], skinKdTree.skin[tri * 3 + 1], skinKdTree.skin[tri * 3 + 2])) return true;
      }
      return false;
    }
  }
  TRE_FATAL("not reached");
  return false;
}

// ----------------------------------------------------------------------------

/// @brief Templated ray-trace of a skin Kd-Tree. The functor is a struct with "operator()(pt0, pt1, pt2) -> bool" defined. Functor can be a lambda.
/// It stops processing after one or many calls returned true in a Kd-Tree leaf loop.
template <typename _fnct>
bool _skinKdTree_raytrace(const s_contact3D::s_skinKdTree &skinKdTree, const glm::vec3 &origin, const glm::vec3 &ray, float tStart, float tEnd, _fnct &fnct)
{
  TRE_ASSERT((std::abs(glm::length(ray) - 1.f)) < 1.e-6f);
  TRE_ASSERT(skinKdTree.skin.size() % 3 == 0);
  TRE_ASSERT(!skinKdTree.tree.empty());

  const glm::vec3 invDir = 1.f / ray;

  struct s_kdStack { unsigned nodeId, flags; float tMin, tMax; };
  std::array<s_kdStack, s_contact3D::s_skinKdTree::kMaxDepth> stack;

  std::size_t stackId = 0;
  stack[stackId].nodeId = 0;
  stack[stackId].flags = 0;
  stack[stackId].tMin = tStart;
  stack[stackId].tMax = tEnd;

  while (true)
  {
    const s_kdStack                         &cs    = stack[stackId];
    const s_contact3D::s_skinKdTree::s_node &cnode = skinKdTree.tree[cs.nodeId];
    const unsigned  dir = cnode.getDir();

    if (dir != 0)
    {
      const float tSplit = (cnode.split - origin[dir-1]) * invDir[dir-1];

      if (cs.tMin <= tSplit && cs.flags == 0) // stack: push first-node
      {
        stack[stackId].flags = 1;
        ++stackId;
        stack[stackId].nodeId = invDir[dir-1] >= 0.f ? cnode.getLeft() : cnode.getRight();
        stack[stackId].flags = 0;
        stack[stackId].tMin = cs.tMin;
        stack[stackId].tMax = std::min(cs.tMax, tSplit);
        continue;
      }
      if (cs.tMax >= tSplit && cs.flags <= 1) // stack: push second-node
      {
        stack[stackId].flags = 2;
        ++stackId;
        stack[stackId].nodeId = invDir[dir-1] >= 0.f ? cnode.getRight() : cnode.getLeft();
        stack[stackId].flags = 0;
        stack[stackId].tMin = std::max(cs.tMin, tSplit);
        stack[stackId].tMax = cs.tMax;
        continue;
      }
      // stack: pop
      if (stackId == 0) break;
      --stackId;
    }
    else
    {
      // leaf: process the triangles
      stack[stackId].flags = 4;
      bool hasHit = false;
      const unsigned triStart = cnode.getLeft();
      const unsigned triStop  = cnode.getRight();
      for (unsigned tri = triStart; tri < triStop; ++tri)
      {
        hasHit |= fnct(skinKdTree.skin[tri * 3 + 0], skinKdTree.skin[tri * 3 + 1], skinKdTree.skin[tri * 3 + 2]);
      }
      if (hasHit) return true;
      // stack: pop
      if (stackId == 0) break;
      --stackId;
    }
  }
  return false;
}

// Helpers ====================================================================

inline static float tetra_volume(const glm::vec3 &ptA, const glm::vec3 &ptB, const glm::vec3 &ptC, const glm::vec3 &ptD)
{
  return fabsf(glm::dot(glm::cross(ptB - ptA, ptC - ptA), ptD - ptA)) / 6.f;
}

/// Compute the barycenter of the convex mesh [mesh's skin: list of triangles]. Also compute the volume.
static void skin_barycenter(const s_contact3D::s_skin &pts, glm::vec3 &center, float &volume)
{
  TRE_ASSERT(pts.size() % 3 == 0);
  center = glm::vec3(0.f);
  volume = 0.f;
  for (std::size_t iQ = 3, Npts = pts.size(); iQ < Npts; iQ += 3)
  {
    const float weightCurr = tetra_volume(pts[0], pts[iQ], pts[iQ + 1], pts[iQ + 2]);
    center += weightCurr * (pts[0] + pts[iQ] + pts[iQ + 1] + pts[iQ + 2]);
    volume += weightCurr;
  }
  center *= 1.f / (4.f * volume);
}

static const glm::vec3 axes[3] = { glm::vec3(1.f, 0.f, 0.f), glm::vec3(0.f, 1.f, 0.f), glm::vec3(0.f, 0.f, 1.f) };

// (0D in 3D) Simple penetration test of a point in a volume ==================

bool s_contact3D::point_treta(const glm::vec3 & point,
                              const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2, const glm::vec3 & pt3)
{
  const glm::mat3 transf = glm::mat3(pt1 - pt0, pt2 - pt0, pt3 - pt0); // (no transpose - glm is column-major indexed)

  // fast but unsafe if the tetrahedron is degenerated. See other implementation of "point_treta" for the slow & safe method.
  TRE_ASSERT(fabsf(glm::determinant(transf)) > 1.e-12f);

  const glm::mat3 transfInv = glm::inverse(transf);
  const glm::vec3 coordUVW = transfInv * (point - pt0);

  if (glm::any(glm::lessThan(coordUVW, glm::vec3(0.f))) || (coordUVW.x + coordUVW.y + coordUVW.z) > 1.f)
      return false;

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_treta(s_contact3D & cntTetra,
                              const glm::vec3 & point,
                              const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2, const glm::vec3 & pt3)
{
  const glm::vec3 v0P = point - pt0;

  const glm::vec3 edge01 = pt1 - pt0;
  const glm::vec3 edge02 = pt2 - pt0;
  const glm::vec3 edge03 = pt3 - pt0;

  const glm::vec3 normal012 = glm::cross(edge01, edge02);
  if (glm::dot(normal012, v0P) * glm::dot(normal012, edge03) < 0.f) return false;

  const glm::vec3 normal013 = glm::cross(edge01, edge03);
  if (glm::dot(normal013, v0P) * glm::dot(normal013, edge02) < 0.f) return false;

  const glm::vec3 normal023 = glm::cross(edge02, edge03);
  if (glm::dot(normal023, v0P) * glm::dot(normal023, edge01) < 0.f) return false;

  const glm::vec3 v1P = point - pt1;
  const glm::vec3 edge12 = pt2 - pt1;
  const glm::vec3 edge13 = pt3 - pt1;

  const glm::vec3 normal123 = glm::cross(edge12, edge13);
  if (glm::dot(normal123, v1P) * glm::dot(normal123, edge01 /* = -edge10 */) > 0.f) return false;

  // the "point" is inside

  cntTetra.penet = std::numeric_limits<float>::infinity();

  TRE_ASSERT(glm::length(normal012) > 1.e-6f);
  const glm::vec3 normal012_unit = glm::normalize(normal012);
  const float dist012 = glm::dot(normal012_unit, v0P);
  const float dist012_abs = fabsf(dist012);
  if (dist012_abs < cntTetra.penet)
  {
    cntTetra.penet = dist012_abs;
    cntTetra.pt = point - dist012 * normal012_unit;
    cntTetra.normal = - normal012_unit;
    if (dist012 < 0.f)
      cntTetra.normal = - cntTetra.normal;
  }

  TRE_ASSERT(glm::length(normal013) > 1.e-6f);
  const glm::vec3 normal013_unit = glm::normalize(normal013);
  const float dist013 = glm::dot(normal013_unit, v0P);
  const float dist013_abs = fabsf(dist013);
  if (dist013_abs < cntTetra.penet)
  {
    cntTetra.penet = dist013_abs;
    cntTetra.pt = point - dist013 * normal013_unit;
    cntTetra.normal = - normal013_unit;
    if (dist013 < 0.f)
      cntTetra.normal = - cntTetra.normal;
  }

  TRE_ASSERT(glm::length(normal023) > 1.e-6f);
  const glm::vec3 normal023_unit = glm::normalize(normal023);
  const float dist023 = glm::dot(normal023_unit, v0P);
  const float dist023_abs = fabsf(dist023);
  if (dist023_abs < cntTetra.penet)
  {
    cntTetra.penet = dist023_abs;
    cntTetra.pt = point - dist023 * normal023_unit;
    cntTetra.normal = - normal023_unit;
    if (dist023 < 0.f)
      cntTetra.normal = - cntTetra.normal;
  }

  TRE_ASSERT(glm::length(normal123) > 1.e-6f);
  const glm::vec3 normal123_unit = glm::normalize(normal123);
  const float dist123 = glm::dot(normal123_unit, v1P);
  const float dist123_abs = fabsf(dist123);
  if (dist123_abs < cntTetra.penet)
  {
    cntTetra.penet = dist123_abs;
    cntTetra.pt = point - dist123 * normal123_unit;
    cntTetra.normal = - normal123_unit;
    if (dist123 < 0.f)
      cntTetra.normal = - cntTetra.normal;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_box(const glm::vec3 & point,
                            const s_boundbox & bbox)
{
  return glm::all(glm::lessThanEqual(bbox.m_min, point)) && glm::all(glm::lessThanEqual(point, bbox.m_max));
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_box(s_contact3D & cntBox,
                            const glm::vec3 & point,
                            const s_boundbox & bbox)
{
  const glm::vec3 vAP = point - bbox.m_min;
  const glm::vec3 vPB = bbox.m_max - point;

  float vAPmin = vAP.x; uint vAPminInd = 0;
  if (vAP.y < vAPmin) { vAPmin = vAP.y; vAPminInd = 1; }
  if (vAP.z < vAPmin) { vAPmin = vAP.z; vAPminInd = 2; }

  if (vAPmin < 0.f) return false;

  float vPBmin = vPB.x; uint vPBminInd = 0;
  if (vPB.y < vPBmin) { vPBmin = vPB.y; vPBminInd = 1; }
  if (vPB.z < vPBmin) { vPBmin = vPB.z; vPBminInd = 2; }

  if (vPBmin < 0.f) return false;

  if (vAPmin < vPBmin)
  {
    cntBox.penet = vAPmin;
    cntBox.normal = - axes[vAPminInd];
    cntBox.pt = point - vAP * axes[vAPminInd];
  }
  else
  {
    cntBox.penet = vPBmin;
    cntBox.normal = axes[vPBminInd];
    cntBox.pt = point + vPB * axes[vPBminInd];
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_skin(const glm::vec3 & point,
                             const s_skin & pts)
{
  TRE_ASSERT(pts.size() % 3 == 0);

  for (std::size_t iQ = 0, Npts = pts.size(); iQ < Npts; iQ += 3)
  {
    // check against the plane (iQ, iQ+1, iQ+2)
    const glm::vec3 v0P = point - pts[iQ+0];

    const glm::vec3 edge01 = pts[iQ+1] - pts[iQ+0];
    const glm::vec3 edge02 = pts[iQ+2] - pts[iQ+0];
    const glm::vec3 normal012 = glm::cross(edge01, edge02);
    TRE_ASSERT(glm::length(normal012) > 1.e-12f);

    if (glm::dot(normal012, v0P) > 0.f) return false;
  }
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_skin(s_contact3D & cntSkin,
                             const glm::vec3 & point,
                             const s_skin & pts)
{
  const std::size_t Npts = pts.size();
  TRE_ASSERT(Npts % 3 == 0);

  if (Npts < 12) // at least a tetrahedron
    return false;

  cntSkin.penet = std::numeric_limits<float>::infinity();

  for (std::size_t iQ = 0; iQ < Npts; iQ += 3)
  {
    // check against the plane (iQ, iQ+1, iQ+2)
    const glm::vec3 v0P = point - pts[iQ+0];

    const glm::vec3 edge01 = pts[iQ+1] - pts[iQ+0];
    const glm::vec3 edge02 = pts[iQ+2] - pts[iQ+0];
    const glm::vec3 normal012 = glm::normalize(glm::cross(edge01, edge02));
    TRE_ASSERT(!std::isnan(normal012.x));

    const float dist012 = glm::dot(normal012, v0P);

    if (dist012 > 0.f) return false;

    if (-dist012 < cntSkin.penet)
    {
      cntSkin.penet = -dist012;
      cntSkin.pt = point - dist012 * normal012;
      cntSkin.normal = normal012;
    }
  }
  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_sphere(const glm::vec3 &point,
                               const glm::vec3 &center, const float radius)
{
  return glm::length(point - center) <= radius;
}

// ----------------------------------------------------------------------------

bool s_contact3D::point_sphere(s_contact3D & cntSphere,
                               const glm::vec3 &point,
                               const glm::vec3 &center, const float radius)
{
  const glm::vec3 vCP = point - center;
  const float     dCP = glm::length(vCP);
  if (dCP > radius) return false;

  const glm::vec3 normalOut = dCP == 0.f ? glm::vec3(1.f, 0.f, 0.f) : vCP / dCP;
  cntSphere.pt = center + normalOut * radius;
  cntSphere.normal = normalOut;
  cntSphere.penet = radius - dCP;

  return true;
}

// (2D <-> 2D) Surface cross ==================================================

bool s_contact3D::cross_tri_sphere(glm::vec3 &ptOnTriangle,
                                   const glm::vec3 &pt0, const glm::vec3 &pt1, const glm::vec3 &pt2,
                                   const glm::vec3 &center, const float radius)
{
  // project center on plane (pt0,pt1,pt2)
  const glm::vec3 v01 = pt1 - pt0;
  const glm::vec3 v02 = pt2 - pt0;
  const glm::vec3 eN = glm::normalize(glm::cross(v01, v02));
  TRE_ASSERT(!std::isnan(eN.x));
  const glm::vec3 v0C = center - pt0;
  const float     coordN = glm::dot(v0C, eN);

  if (fabsf(coordN) > radius)
    return false;

  const float v01v01 = glm::dot(v01, v01);
  const float v02v02 = glm::dot(v02, v02);
  const float v01v02 = glm::dot(v01, v02);

  float cU = glm::dot(v01, v0C) / v01v01;
  float cV = glm::dot(v02, v0C) / v02v02;

  // projection on the tri's edge or tri's vertex.

  if (cV < 0.f) // project on edge 01
  {
    // find 0P + a t01 = (cU + a x) * v01 + (cV + a y) * v02 = cUnew * v01
    // with cVnew = (cV + a y) == 0
    // thus: cUnew = cU + a x = cU - cV * x / y

    // by definition, we have: t01 = x v01 + y v02
    // (t01.v01) = x v01.v01 + y v02.v01 = 0
    // thus: x / y = - v02.v01 / v01.v01

    // Finally, cUnew = cU + cV * v02.v01 / v01.v01
    //          cVnew = 0

    TRE_ASSERT(v01v01 > 1.e-6f);

    cU = cU + cV * v01v02 / v01v01;
    cV = 0.f;

    if (cU < 0.f)
       cU = 0.f;
    else if (cU > 1.f)
      cU = 1.f;
  }
  else if (cU < 0.f) // project on edge 02
  {
    // find 0P + a t02 = (cU + a x) * v01 + (cV + a y) * v02 = cVnew * v02
    // with cUnew = (cU + a x) == 0

    // in the same way: cUnew = 0
    //                  cVnew = cV + cU * v02.v01 / v02.v02

    cV = cV + cU * v01v02 / v02v02;
    cU = 0.f;

    if (cV < 0.f)
       cV = 0.f;
    else if (cV > 1.f)
      cV = 1.f;
  }
  else if ((cU + cV) > 1.f) // project on edge 12
  {
    // find 0P + a t12 = (cU + a x) * v01 + (cV + a y) * v02 = cUnew * v01 + cVnew * v02
    // with cUnew + cVnew = cU + cV + a (x + y) = 1.f

    // by definition, we have: t12 = x v01 + y v02
    // v12.v01 = t12.t01 = x v02.t01 = x (-v01.t02)
    // v12.v02 = t12.t02 = y v01.t02

    const glm::vec3 v12 = - v01 + v02;
    const glm::vec3 t02 = glm::cross(eN, v02);
    const float     v01t02 = glm::dot(v01, t02);

    const float x = glm::dot(v12, v01) / (-v01t02);
    const float y = glm::dot(v12, v02) / ( v01t02);

    const float a = (1.f - (cU + cV)) / (x + y);
    TRE_ASSERT(!std::isnan(a) && std::isfinite(a));

    cU += a * x;
    cV += a * y;

    if (cU > 1.f || cV < 0.f)
    {
      cU = 1.f;
      cV = 0.f;
    }
    else if (cU < 0.f || cV > 1.f)
    {
      cU = 0.f;
      cV = 1.f;
    }
  }

  ptOnTriangle = pt0 + cU * v01 + cV * v02;

  const glm::vec3 vCP = ptOnTriangle - center;
  return glm::dot(vCP,vCP) <= (radius * radius);
}

// (3D in 3D) =================================================================

bool s_contact3D::box_box(const s_boundbox &box_0, const s_boundbox &box_1)
{
  s_boundbox boxIntersect;
  boxIntersect.m_min = glm::max(box_0.m_min, box_1.m_min);
  boxIntersect.m_max = glm::min(box_0.m_max, box_1.m_max);
  return glm::all(glm::lessThanEqual(boxIntersect.m_min, boxIntersect.m_max));
}

// ----------------------------------------------------------------------------

bool s_contact3D::box_sphere(const s_boundbox &box, const glm::vec3 &center, const float radius)
{
  const glm::vec3 AC = center - box.m_min;
  const glm::vec3 BC = center - box.m_max;
  const glm::vec3 rrr = glm::vec3(radius);

  if (glm::any(glm::lessThan(AC, -rrr)) || glm::any(glm::greaterThan(BC, rrr)))
    return false;

  // check the edges and corners ...
  glm::vec3 PC = AC;
  if (AC.x > -BC.x) PC.x = BC.x; // TODO : find a "glm::select(vFalse, vTrue, condition)
  if (AC.y > -BC.y) PC.y = BC.y;
  if (AC.z > -BC.z) PC.z = BC.z;
  if (AC.x * BC.x <= 0.f) PC.x = 0.f; // TODO : find a "glm::select(vFalse, vTrue, condition)
  if (AC.y * BC.y <= 0.f) PC.y = 0.f;
  if (AC.z * BC.z <= 0.f) PC.z = 0.f;

  return glm::dot(PC, PC) <= radius * radius;
}

// ----------------------------------------------------------------------------

bool s_contact3D::sphere_skin(const glm::vec3 &center, const float radius, const s_skin &pts)
{
  TRE_ASSERT(pts.size() % 3 == 0);

  for (std::size_t iP = 0, iTstop = pts.size(); iP < iTstop; iP += 3)
  {
    glm::vec3 dummy;
    if (cross_tri_sphere(dummy, pts[iP], pts[iP + 1], pts[iP + 2], center, radius))
      return true;
  }

  return false;
}

// ----------------------------------------------------------------------------

bool s_contact3D::sphere_skin(s_contact3D &cntSphere, const glm::vec3 &center, const float radius, const s_skin &pts)
{
  TRE_ASSERT(pts.size() % 3 == 0);

  if (point_skin(cntSphere, center, pts))
  {
    cntSphere.penet = glm::length(cntSphere.pt - center) + radius;
    cntSphere.normal = -cntSphere.normal;
    cntSphere.pt += cntSphere.normal * cntSphere.penet * 0.5f;
    return true;
  }


  const float maxDist = 1.1f * radius;
  float dist = maxDist;

  // TODO ... (good enough approximation ?)

  for (std::size_t iP = 0, iTstop = pts.size(); iP < iTstop; iP += 3)
  {
    glm::vec3 ptOntri;
    if (cross_tri_sphere(ptOntri, pts[iP], pts[iP + 1], pts[iP + 2], center, radius))
    {
      const glm::vec3 vCP = ptOntri - center;
      const float     lCP = glm::length(vCP);
      if (lCP < dist)
      {
        dist = lCP;
        cntSphere.penet = radius - lCP;
        if (lCP > 1.e-6f)
          cntSphere.normal = vCP / lCP;
        else
          cntSphere.normal = - glm::normalize(glm::cross(pts[iP+1] - pts[iP], pts[iP+2] - pts[iP]));
        cntSphere.pt = center + 0.5f * (radius + lCP) * cntSphere.normal;
      }
    }
  }

  return (dist < maxDist);
}

// ----------------------------------------------------------------------------

bool s_contact3D::box_sphere(s_contact3D &cntBox, const s_boundbox &box, const glm::vec3 &center, const float radius)
{
  const glm::vec3 AC = center - box.m_min;
  const glm::vec3 BC = center - box.m_max;
  const glm::vec3 rrr = glm::vec3(radius);

  if (glm::any(glm::lessThan(AC, -rrr)) || glm::any(glm::greaterThan(BC, rrr)))
    return false;

  // check the edges and corners ...
  glm::vec3 PC = AC;
  if (AC.x > -BC.x) PC.x = BC.x; // TODO : find a "glm::select(vFalse, vTrue, condition)
  if (AC.y > -BC.y) PC.y = BC.y;
  if (AC.z > -BC.z) PC.z = BC.z;
  if (AC.x * BC.x < 0.f) PC.x = 0.f; // TODO : find a "glm::select(vFalse, vTrue, condition)
  if (AC.y * BC.y < 0.f) PC.y = 0.f;
  if (AC.z * BC.z < 0.f) PC.z = 0.f;

  const float distSquared = glm::dot(PC, PC);
  const float radiusSquared = radius * radius;
  if (distSquared > radiusSquared)
    return false;

  if (distSquared == 0.f) // center is inside
  {
    point_box(cntBox, center, box);
    cntBox.penet += radius;
    cntBox.pt -= cntBox.penet * 0.5f * cntBox.normal;
  }
  else
  {
    cntBox.normal = glm::normalize(PC);
    const float dist = sqrtf(distSquared);
    cntBox.penet = radius - dist;
    cntBox.pt = center - (radius + dist) * 0.5f * cntBox.normal;
  }

  return true;
}

// ----------------------------------------------------------------------------

bool s_contact3D::sphere_sphere(s_contact3D &cntSphereA, const glm::vec3 &centerA, const float radiusA, const glm::vec3 &centerB, const float radiusB)
{
  const glm::vec3 vBA = centerA - centerB;

  const float distSquared = glm::dot(vBA, vBA);
  const float radiusApB = radiusA + radiusB;

  if (distSquared > radiusApB * radiusApB)
    return false;

  cntSphereA.pt = (centerA / radiusA + centerB / radiusB) / (1.f/radiusA + 1.f/radiusB);
  cntSphereA.penet = sqrtf(distSquared) - radiusApB;
  cntSphereA.normal = glm::normalize(vBA);

  return true;
}

// ============================================================================

bool s_contact3D::raytrace_box(s_contact3D & hitInfo,
                               const glm::vec3 & origin, const glm::vec3 & direction,
                               const s_boundbox & box,
                               float rayStart)
{
  TRE_ASSERT((std::abs(glm::length(direction) - 1.f)) < 1.e-6f);

  const glm::vec3 extend = box.extend();

  const glm::vec3 vOA = box.m_min - origin;
  const glm::vec3 vOB = box.m_max - origin;

  for (uint iAxis = 0; iAxis < 3; ++iAxis)
  {
    const glm::vec3 planeNormal = axes[iAxis];
    const float     dotNormalDir = glm::dot(planeNormal, direction);
    if (std::abs(dotNormalDir) < 1.e-3f) continue;
    const bool      chooseA = dotNormalDir > 0.f;
    const float     dist = (chooseA ? glm::dot(planeNormal, vOA) : glm::dot(planeNormal, vOB)) / dotNormalDir;
    const glm::vec3 vCP = chooseA ? dist * direction - vOA : vOB - dist * direction; // if "B", take the opposite, so the "uv"-check still stay positive.

    // TODO: make it work when inside.

    uint condTrue = 0;

    if (iAxis != 0 && 0.f <= vCP.x && vCP.x <= extend.x)
      ++condTrue;
    if (iAxis != 1 && 0.f <= vCP.y && vCP.y <= extend.y)
      ++condTrue;
    if (iAxis != 2 && 0.f <= vCP.z && vCP.z <= extend.z)
      ++condTrue;

    if (dist >= rayStart && condTrue == 2)
    {
      hitInfo.penet = dist;
      hitInfo.pt = origin + hitInfo.penet * direction;
      hitInfo.normal = chooseA ? -planeNormal : planeNormal;
      return true;
    }
  }

  return false;
}

// -----------------------------------------------------------------

bool s_contact3D::raytrace_sphere(s_contact3D & hitInfo,
                                  const glm::vec3 & origin, const glm::vec3 & direction,
                                  const glm::vec3 & center, const float radius,
                                  float rayStart)
{
  TRE_ASSERT((std::abs(glm::length(direction) - 1.f)) < 1.e-6f);

  const glm::vec3 vOC = center - origin;
  const float     tOC = glm::dot(vOC, direction);
  const float     tCP_Squared = tOC * tOC - glm::dot(vOC, vOC) + radius * radius;

  if (tCP_Squared < 0.f)
    return false;

  const float tCP = std::sqrt(tCP_Squared);

  hitInfo.penet = tOC - tCP;
  hitInfo.pt = origin + hitInfo.penet * direction;
  hitInfo.normal = glm::normalize(hitInfo.pt - center);

  if (glm::dot(hitInfo.normal, direction) > 0.f) // inside
  {
    hitInfo.penet = tOC + tCP;
    hitInfo.pt = origin + hitInfo.penet * direction;
    hitInfo.normal = glm::normalize(center - hitInfo.pt);
  }

  TRE_ASSERT(glm::dot(hitInfo.normal, direction) <= 0.f);
  return (hitInfo.penet >= rayStart);
}

// -----------------------------------------------------------------

bool s_contact3D::raytrace_skin(s_contact3D & hitInfo,
                                const glm::vec3 &origin, const glm::vec3 &direction,
                                const s_skin &pts,
                                float rayStart)
{
  TRE_ASSERT((std::abs(glm::length(direction) - 1.f)) < 1.e-6f);
  TRE_ASSERT(pts.size() % 3 == 0);

  float     minDist = std::numeric_limits<float>::infinity();
  glm::vec3 cUVT;

  // loop on triangles
  for (std::size_t iP = 0, iPstop = pts.size(); iP < iPstop; iP += 3)
  {
    const glm::vec3 edge01 = pts[iP + 1] - pts[iP];
    const glm::vec3 edge02 = pts[iP + 2] - pts[iP];
    const glm::vec3 outNormal = glm::cross(edge01, edge02);
    if (glm::dot(outNormal, direction) > 0.f)
      continue;

    if (triangleRaytrace3D(pts[iP + 0], pts[iP + 1], pts[iP + 2], origin, direction, &cUVT))
    {
      if (cUVT.z >= rayStart && cUVT.z < minDist)
      {
        minDist = cUVT.z;
        hitInfo.normal = glm::normalize(outNormal);
        TRE_ASSERT(!std::isnan(hitInfo.normal.x));
      }
    }
  }

  hitInfo.penet = minDist;
  hitInfo.pt = origin + hitInfo.penet * direction;

  return (std::isfinite(minDist) ? true : false);
}

// -----------------------------------------------------------------

bool s_contact3D::raytrace_skin(s_contact3D & hitInfo,
                                const glm::vec3 & origin, const glm::vec3 & direction,
                                const s_skinKdTree & skinKdT,
                                float rayStart)
{
  TRE_ASSERT((std::abs(glm::length(direction) - 1.f)) < 1.e-6f);

  float minDist = std::numeric_limits<float>::infinity();

  const auto fnRT = [&origin, &direction, rayStart, &minDist, &hitInfo](const glm::vec3 &pt0, const glm::vec3 &pt1, const glm::vec3 &pt2) -> bool
    {
      const glm::vec3 edge01 = pt1 - pt0;
      const glm::vec3 edge02 = pt2 - pt0;
      const glm::vec3 outNormal = glm::cross(edge01, edge02);
      if (glm::dot(outNormal, direction) > 0.f) return false;

      glm::vec3 cUVT;
      if (triangleRaytrace3D(pt0, pt1, pt2, origin, direction, &cUVT))
      {
        if (cUVT.z >= rayStart && cUVT.z < minDist)
        {
          minDist = cUVT.z;
          hitInfo.normal = glm::normalize(outNormal);
          TRE_ASSERT(!std::isnan(hitInfo.normal.x));
          return true;
        }
      }
      return false;
    };

  _skinKdTree_raytrace(skinKdT, origin, direction, rayStart, std::numeric_limits<float>::infinity(), fnRT);

  hitInfo.penet = minDist;
  hitInfo.pt = origin + hitInfo.penet * direction;

  return (std::isfinite(minDist) ? true : false);
}

} // namespace
