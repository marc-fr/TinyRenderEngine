#ifndef CONTACT_3D_H
#define CONTACT_3D_H

#include "glm/glm.hpp"

#include <vector>

namespace tre {

struct s_boundbox; // forward decl.
template <class _t> class span; // forward decl.

//=============================================================================

/**
 * @brief The contact class
 * This handles 3D contacts.
 */
struct s_contact3D
{
public:
  glm::vec3 pt;     ///< contact central point
  glm::vec3 normal; ///< outside-normal of the obstacle
  float penet;      ///< penetration along the normal

  // ===============================================================

  typedef std::vector<glm::vec3> s_skin;
  typedef span<glm::vec3>        s_skinArg;

  /// Kd-Tree for mesh's skin sampling
  struct s_skinKdTree
  {
    s_skin skin;

    struct s_node
    {
      uint32_t flags = 0; ///< encodes: nodeLeft or triangle-range [15bits], nodeRight or triangle-range [15bits], split-direction (1:x,2:y,3:z) [2bits]
      float    split = 0.f; ///< split coordinate

      unsigned getDir() const { return flags & 0x3; }
      unsigned getLeft() const { return flags >> 17; }
      unsigned getRight() const { return (flags >> 2) & 0x7FFF; }
    };
    std::vector<s_node> tree;

    static constexpr unsigned kMaxDepth = 14;

    void computeTree();
  };

  // ===============================================================
  /// @name (0D in 3D) Simple penetration test of a point in a volume
  /// note:
  /// - the "pt" is defined as the projection of the point on the volume's surface.
  /// - the "normal" is defined as the out-normal of the volume.
  /// @{

  /// Check penetration of a point and a tetrahedron (pt0,pt1,pt2,pt3) => no contact info
  static bool point_treta(const glm::vec3 & point,
                         const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2, const glm::vec3 & pt3);

  /// Check penetration of a point and a tetrahedron (pt0,pt1,pt2,pt3)
  static bool point_treta(s_contact3D & cntTetra,
                          const glm::vec3 & point,
                          const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2, const glm::vec3 & pt3);

  /// Check penetration of a point and a box => no contact info
  static bool point_box(const glm::vec3 & point,
                        const s_boundbox & bbox);

  /// Check penetration of a point and a box
  static bool point_box(s_contact3D & cntBox,
                        const glm::vec3 & point,
                        const s_boundbox & bbox);

  /// Check penetration of a point and a convexe mesh [mesh's skin: list of triangles (warning: in-face culling)] => no contact info
  static bool point_skin(const glm::vec3 & point,
                         const s_skin & pts);

  /// Check penetration of a point and a convexe mesh [mesh's skin: list of triangles (warning: in-face culling)]
  static bool point_skin(s_contact3D & cntSkin,
                         const glm::vec3 & point,
                         const s_skin & pts);

  /// Check penetration of a point and a sphere (center, radius) => no contact info
  static bool point_sphere(const glm::vec3 &point,
                           const glm::vec3 &center, const float radius);

  /// Check penetration of a point and a sphere (center, radius)
  static bool point_sphere(s_contact3D & cntSphere,
                           const glm::vec3 &point,
                           const glm::vec3 &center, const float radius);

  /// @}

  // ===============================================================
  /// @name (2D <-> 2D) Surface cross
  /// note: it just gives the crossing point(s)
  /// @{

  /// Check intersection of a triangle (pt0,pt1,pt2) and a triangle (ptA,ptB,ptC)
  static bool cross_tri_tri(glm::vec3 & crossPtM, glm::vec3 & crossPtN,
                            const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2,
                            const glm::vec3 & ptA, const glm::vec3 & ptB, const glm::vec3 & ptC);

  /// Check intersection of a triangle (pt0,pt1,pt2) and a sphere (center, radius)
  static bool cross_tri_sphere(glm::vec3 & ptOnTriangle,
                               const glm::vec3 & pt0, const glm::vec3 & pt1, const glm::vec3 & pt2,
                               const glm::vec3 & center, const float radius);

  /// @}

  //  ===============================================================
  /// @name (3D in 3D)
  /// note:
  /// - the "pt" is defined as the weighted center of the penetration (barycenter of {volume1 && volume2})
  /// - the "normal" is defined as the out-normal volume1 -> volume2.
  /// - the "penet" is the maximal penetration along the normal axis (always positive)
  /// @{

  /// Check intersection of a box and a cbox => no contact info
  static bool box_box(const s_boundbox & box_0,
                      const s_boundbox & box_1);

  /// Check intersection of a box and a convexe mesh [mesh's skin: list of triangles (warning: in-face culling)]
  static bool box_skin(s_contact3D & cntBox,
                       const s_boundbox & box,
                       const s_skin &pts);

  /// Check intersection of a box and a sphere (center, radius) => no contact info
  static bool box_sphere(const s_boundbox & box,
                         const glm::vec3 & center, const float radius);

  /// Check intersection of a box and a sphere (center, radius)
  static bool box_sphere(s_contact3D & cntBox,
                         const s_boundbox & box,
                         const glm::vec3 & center, const float radius);

  /// Check intersection of a circle and a convexe mesh [mesh's skin: list of triangles (warning: in-face culling)] => no contact info
  static bool sphere_skin(const glm::vec3 & center, const float radius,
                          const s_skin &pts);

  /// Check intersection of a circle and a convexe mesh [mesh's skin: list of triangles (warning: in-face culling)]
  static bool sphere_skin(s_contact3D & cntSphere,
                          const glm::vec3 & center, const float radius,
                          const s_skin &pts);

  /// Check intersection of a sphere A and a sphere B
  static bool sphere_sphere(s_contact3D & cntSphereA,
                            const glm::vec3 & centerA, const float radiusA,
                            const glm::vec3 & centerB, const float radiusB);

  /// @}

  //  ===============================================================
  /// @name Ray-cast on volume (hit on the point for which the ray goes out of the volume)
  /// note:
  /// - the "pt" is the point on the hit volume
  /// - the "normal" is the out-normal of the hit volume
  /// - the "penet" is the signed distance between the ray-center and the hit-point
  /// @{

  /// Check if the ray (origin, direction) hits the box (ptA,ptB). Returns true when the ray hits the box.
  static bool raytrace_box(s_contact3D & hitInfo,
                           const glm::vec3 & origin, const glm::vec3 & direction,
                           const s_boundbox & box,
                           float rayStart = -std::numeric_limits<float>::infinity());

  /// Check if the ray (origin, direction) hits the sphere (center, radius). Returns true when the ray hits the sphere.
  static bool raytrace_sphere(s_contact3D & hitInfo,
                              const glm::vec3 & origin, const glm::vec3 & direction,
                              const glm::vec3 & center, const float radius,
                              float rayStart = -std::numeric_limits<float>::infinity());

  /// Check if the ray (origin, direction) hits the mesh's skin. Warning: in-face culling. Returns true when the ray hits the skin.
  static bool raytrace_skin(s_contact3D & hitInfo,
                            const glm::vec3 & origin, const glm::vec3 & direction,
                            const s_skin & pts,
                            float rayStart = -std::numeric_limits<float>::infinity());

  /// Check if the ray (origin, direction) hits the mesh's skin. Warning: in-face culling. Returns true when the ray hits the skin.
  static bool raytrace_skin(s_contact3D & hitInfo,
                            const glm::vec3 & origin, const glm::vec3 & direction,
                            const s_skinKdTree & skinKdT,
                            float rayStart = -std::numeric_limits<float>::infinity());

  /// @}
};

// ============================================================================

} // namespace

#endif // CONTACT_3D_H
