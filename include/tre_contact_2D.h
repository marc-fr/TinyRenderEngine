#ifndef CONTACT_2D_H
#define CONTACT_2D_H

#include "glm/glm.hpp"

#include <vector>

namespace tre {

//=============================================================================

/**
 * @brief The contact class
 * This handles 2D contacts.
 */
struct s_contact2D
{
public:
  glm::vec2 pt;     ///< contact central point
  glm::vec2 normal; ///< outside-normal of the obstacle
  float penet;      ///< penetration along the normal

  // ===============================================================
  /// @name (0D in 2D) Simple penetration test of a point in a surface
  /// note:
  /// - the "pt" is defined as the projection of the point on the surface's edge.
  /// - the "normal" is defined as the out-normal of the surface.
  /// @{

  /// Check penetration of a point and a demi-plane Y+ (y0=limy).
  static bool point_ydown(s_contact2D & cntDown,
                          const glm::vec2 & point,
                          const float ylim);

  /// Check penetration of a point and a triangle (pt0,pt1,pt2) => no contact info
  static bool point_tri(const glm::vec2 & point,
                        const glm::vec2 & pt0, const glm::vec2 & pt1, const glm::vec2 & pt2);

  /// Check penetration of a point and a triangle (pt0,pt1,pt2)
  static bool point_tri(s_contact2D & cntTri,
                        const glm::vec2 & point,
                        const glm::vec2 & pt0, const glm::vec2 & pt1, const glm::vec2 & pt2);

  /// Check penetration of a point and a box (ptA-ptB) => no contact info
  static bool point_box(const glm::vec2 & point,
                        const glm::vec4 & AABB);

  /// Check penetration of a point and a box (ptA-ptB)
  static bool point_box(s_contact2D & cntBox,
                        const glm::vec2 & point,
                        const glm::vec4 & AABB);

  /// Check penetration of a point and a convexe polygon [pts...] => no contact info
  static bool point_poly(const glm::vec2 & point,
                         const std::vector<glm::vec2> & pts);

  /// Check penetration of a point and a convexe polygon [pts...]
  static bool point_poly(s_contact2D & cntPoly,
                         const glm::vec2 & point,
                         const std::vector<glm::vec2> & pts);

  /// Check penetration of a point and a circle (center, radius) => no contact info
  static bool point_circle(const glm::vec2 & point,
                           const glm::vec2 center, const float radius);

  /// Check penetration of a point and a circle (center, radius)
  static bool point_circle(s_contact2D & cntCircle,
                           const glm::vec2 & point,
                           const glm::vec2 center, const float radius);

  /// @}

  // ===============================================================
  /// @name (1D <-> 1D) Edge cross
  /// note: it just gives the crossing point (or a list of the crossing points)
  /// @{

  /// Check intersection of a line (pt0,pt1) and a line (ptA,ptB)
  static bool cross_line_line(glm::vec2 & crossPt,
                              const glm::vec2 & pt0, const glm::vec2 & pt1,
                              const glm::vec2 & ptA, const glm::vec2 & ptB);

  /// Check intersection of a line (y=y0) and a line (pt0,pt1).
  static bool cross_yline_line(glm::vec2 & crossPt,
                               const float y0,
                               const glm::vec2 & ptA, const glm::vec2 & ptB);

  /// Check intersection of a line (pt0,pt1) and a circle (center, radius)
  static bool cross_line_circle(std::vector<glm::vec2> & crossPts,
                                const glm::vec2 & pt0, const glm::vec2 & pt1,
                                const glm::vec2 & center, const float radius);

  /// @}

  //  ===============================================================
  /// @name (2D in 2D)
  /// note:
  /// - the "pt" is defined as the weighted center of the penetration (barycenter of {surface1 && surface2})
  /// - the "normal" is defined as the out-normal surface1 -> surface2.
  /// - the "penet" is the maximal penetration along the normal axis (always positive)
  /// @{

  /// Check penetration of a demi-space (y<=limy) and a convexe polygon [pts...] => no contact info
  static bool ydown_poly(const float limy,
                         const std::vector<glm::vec2> &pts);

  /// Check penetration of a demi-space (y<=limy) and a convexe polygon [pts...]
  static bool ydown_poly(s_contact2D & cntDown,
                         const float limy,
                         const std::vector<glm::vec2> &pts);

  /// Check penetration of a demi-space (y<=limy) and a circle (center, radius) => no contact info
  static bool ydown_circle(const float limy,
                           const glm::vec2 & center, const float radius);

  /// Check penetration of a demi-space (y<=limy) and a circle (center, radius)
  static bool ydown_circle(s_contact2D & cntDown,
                           const float limy,
                           const glm::vec2 & center, const float radius);

  /// Check intersection of a box and a cbox => no contact info
  static bool box_box(const glm::vec4 & AABB_0,
                      const glm::vec4 & AABB_1);

  /// Check intersection of a box and a convexe polygon [pts...] => no contact info
  static bool box_poly(const glm::vec4 & AABB,
                       const std::vector<glm::vec2> &pts);

  /// Check intersection of a box and a convexe polygon [pts...]
  static bool box_poly(s_contact2D & cntBox,
                       const glm::vec4 & AABB,
                       const std::vector<glm::vec2> &pts);

  /// Check intersection of a box and a circle (center, radius) => no contact info
  static bool box_circle(const glm::vec4 & AABB,
                         const glm::vec2 & center, const float radius);

  /// Check intersection of a box and a circle (center, radius)
  static bool box_circle(s_contact2D & cntBox,
                         const glm::vec4 & AABB,
                         const glm::vec2 & center, const float radius);

  /// Check intersection of a circle and a convexe polygon [pts...]) => no contact info
  static bool circle_poly(const glm::vec2 & center, const float radius,
                          const std::vector<glm::vec2> &pts);

  /// Check intersection of a circle and a convexe polygon [pts...]
  static bool circle_poly(s_contact2D & cntCircle,
                          const glm::vec2 & center, const float radius,
                          const std::vector<glm::vec2> &pts);

  /// Check intersection of a circle A and a circle B
  static bool circle_circle(s_contact2D & cntCircleA,
                            const glm::vec2 & centerA, const float radiusA,
                            const glm::vec2 & centerB, const float radiusB);

  /// Check intersection of a convex polygon [ptsA...] and a convex polygon [ptsB...]  => no contact info
  static bool poly_poly(const std::vector<glm::vec2> ptsA,
                        const std::vector<glm::vec2> ptsB);

  /// Check intersection of a convex polygon [ptsA...] and a convex polygon [ptsB...]
  static bool poly_poly(s_contact2D & cntPolyA,
                        const std::vector<glm::vec2> ptsA,
                        const std::vector<glm::vec2> ptsB);

  /// @}

  //  ===============================================================
  /// @name Ray-cast on surface (hit on the point for which the ray goes out of the surface)
  /// note:
  /// - the "pt" is the point on the hit surface
  /// - the "normal" is the out-normal of the hit surface
  /// - the "penet" is the signed distance between the ray-center and the hit-point
  /// @{

  /// Check if the ray (origin, direction) hits the demi-plane Y+ (y0=limy). Returns true when the ray hits the surface.
  static bool raytrace_ydown(s_contact2D &hitInfo,
                             const glm::vec2 & origin, const glm::vec2 & direction,
                             float limy);

  /// Check if the ray (origin, direction) hits the box (ptA,ptB). Returns true when the ray hits the surface.
  static bool raytrace_box(s_contact2D &hitInfo,
                           const glm::vec2 & origin, const glm::vec2 & direction,
                           const glm::vec4 & AABB);

  /// Check if the ray (origin, direction) hits the circle (circleCenter,circleRadius). Returns true when the ray hits the surface.
  static bool raytrace_circle(s_contact2D &hitInfo,
                              const glm::vec2 & origin, const glm::vec2 & direction,
                              const glm::vec2 & center, const float radius);

  /// Check if the ray (origin, direction) hits the convexe polygon (pts...). Returns true when the ray hits the surface.
  static bool raytrace_poly(s_contact2D &hitInfo,
                            const glm::vec2 & origin, const glm::vec2 & direction,
                            const std::vector<glm::vec2> pts);

 /// @}
};

// ============================================================================

} // namespace

#endif // CONTACT_2D_H
