#ifndef UTILS_DEFINED
#define UTILS_DEFINED

#include "tre_openglinclude.h"

#include <array>
#include <vector>
#include <iostream>

// macros =====================================================================

#ifdef TRE_PRINTS

#ifdef _WIN32

#include <sstream>

namespace tre {
  void debugPrint_Win32(const char *msg);
}

#define TRE_LOG(msg) { std::stringstream ss; ss << "TRE: " << msg << std::endl; tre::debugPrint_Win32(ss.str().c_str()); }
#define TRE_FATAL(msg) { std::stringstream ss; ss << "TRE: error: " << msg << std::endl; tre::debugPrint_Win32(ss.str().c_str()); abort(); }

#else

#define TRE_LOG(msg) std::cout << "TRE: " << msg << std::endl
#define TRE_FATAL(msg) { std::cout << "TRE: error: " << msg << std::endl; abort(); }

#endif

#else

#define TRE_LOG(msg) {}
#define TRE_FATAL(msg) { abort(); }

#endif

#ifdef TRE_DEBUG

#define TRE_ASSERT(test) \
  if (!(test)) TRE_FATAL("assert failed at " << __FILE__ << ":" << __LINE__ << " (" << #test << ")" )

#else

#define TRE_ASSERT(test)

#endif

// types ======================================================================

typedef unsigned uint;

// ============================================================================

namespace tre {

// containers =================================================================
/// @name containers
/// @{

/**
 * @brief class span
 * This warps memory-views on c-style array, std::vector or std::array.
 * std::span is available in C++20 standard, but this code follows C++11 standard.
 */
template<typename _T>
class span
{
private:
   _T                *m_ptr;
   const std::size_t m_size;

public:
    span(_T* ptr, std::size_t len) noexcept : m_ptr(ptr), m_size(len) {}
    span(const std::vector<_T> &v, std::size_t begin, std::size_t len) noexcept : m_ptr(const_cast<_T*>(&v[begin])), m_size(len) {}
    span(const std::vector<_T> &v, std::size_t begin = 0u) noexcept : m_ptr(const_cast<_T*>(&v[begin])), m_size(v.size() <= begin ? 0 : v.size() - begin) {}
    template<std::size_t _N> span(const std::array<_T, _N> &a) noexcept : m_ptr(const_cast<_T*>(&a[0])), m_size(a.size()) {}

    _T       &operator[](std::size_t i) const noexcept { return m_ptr[i]; }

    bool        empty() const noexcept { return m_size == 0; }
    std::size_t size() const noexcept { return m_size; }

    _T* data() noexcept { return m_ptr; }
    _T* dataEnd() noexcept { return m_ptr + m_size; }

    const _T* data() const noexcept { return m_ptr; }
    const _T* dataEnd() const noexcept { return m_ptr + m_size; }

    class iterator
    {
    public:
      iterator(_T *data) : m_ptr(data) {}
      inline iterator& operator++() { ++m_ptr; return *this; }
      inline iterator  operator++(int ) { iterator retIt(*this); ++(*this); return retIt; }
      inline bool      operator==(const iterator &other) const { return m_ptr == other.m_ptr; }
      inline bool      operator!=(const iterator &other) const { return m_ptr != other.m_ptr; }
      inline _T&       operator*() { return *m_ptr; }
     private:
      _T *m_ptr;
    };

    iterator begin() noexcept { return iterator(data()); }
    iterator end() noexcept { return iterator(dataEnd()); }

    class const_iterator
    {
    public:
      const_iterator(const _T *data) : m_ptr(data) {}
      inline const_iterator& operator++() { ++m_ptr; return *this; }
      inline const_iterator  operator++(int ) { const_iterator retIt(*this); ++(*this); return retIt; }
      inline bool            operator==(const const_iterator &other) const { return m_ptr == other.m_ptr; }
      inline bool            operator!=(const const_iterator &other) const { return m_ptr != other.m_ptr; }
      inline const _T&       operator*() { return *m_ptr; }
     private:
      const _T *m_ptr;
    };

    const_iterator begin() const noexcept { return const_iterator(data()); }
    const_iterator end() const noexcept { return const_iterator(dataEnd()); }

    void swap(span<_T> &other)
    {
      TRE_ASSERT(m_size == other.m_size);
      std::swap(m_ptr, other.m_ptr);
    }
};

/**
* @brief class chunkVector
* The chunkVector never relocates the data when growing.
*/
template<typename _T, std::size_t chunkSize>
class chunkVector
{
private:
  typedef std::array<_T, chunkSize> chunkElement;
  std::vector<chunkElement *> m_chunks;
  std::size_t                 m_size; ///< nbr of elements

public:
  chunkVector() { TRE_ASSERT((chunkSize != 0) && (chunkSize & (chunkSize - 1)) == 0); }
  ~chunkVector() { clean(); }

  _T &operator[](std::size_t i) const noexcept { return (*m_chunks[i / chunkSize])[i & (chunkSize - 1)]; }

  bool        empty() const noexcept { return m_size == 0; }
  std::size_t size() const noexcept { return m_size; }
  void        clean() noexcept; ///< clear and free memory (capacity = 0)
  void        clear() noexcept { m_size = 0; } ///< clear but do not free memory

  void        reserve(std::size_t capacity);
  void        resize(std::size_t size);
  void        push_back(const _T &element);
  void        push_back(_T &&element);

  // TODO: iterator ?
};

/// @}
// Open-GL =====================================================================
/// @name OpenGL
/// @{

bool IsOpenGLok(const char * msg = nullptr);

/**
 * @brief compute3DFrustumProjection
 * @param[out] MP projection matrix, from Right-Handed Y-Up axis system to clip-space [-1,1]
 * @param[in] invratio dy/dx (<=1)
 * @param[in] fov vertical field-of-view (angle)
 * @param[in] near near cliping distance
 * @param[in] far far cliping distance
 *
 * Note on the OpenGL Projection matrix
 *  ProjM * (x,y,z,1) = (xe,ye,ze,we)
 *  The point is drawn if  -we < xe,yz,zc < we
 *  Finally, the point coord on the screen [-1,1]*[-1,1] will be:
 *    xc = xe/we
 *    yc = ye/we
 *    (zc = ze/we) is used for the depth comparision.
 *  On 3D, the cameca is along (-Z) axis, so we = -z and ze is interpolated such as near<-z<far
 */
void compute3DFrustumProjection(glm::mat4 & MP, const float invratio, const float fov, const float near, const float far);

/**
 * @brief compute3DOrthoProjection
 * @param[out] MP projection matrix, from Right-Handed Y-Up axis system to clip-space [-1,1]
 * @param[in] dx half of the screen width [-dx,dx]
 * @param[in] dy half of the screen height [-dy,dy]
 * @param[in] near near cliping distance
 * @param[in] far far cliping distance
 */
void compute3DOrthoProjection(glm::mat4 & MP, float dx, float dy, float near, float far);

/**
 * @brief compute2DOrthoProjection
 * @param[out] MP projection matrix
 * @param[in] invratio dy/dx (<=1)
 *
 * Note on the OpenGL Projection matrix:
 *   See explaination for the mProjection3D.
 *   On 2D, the matrix is simplified because the shader will set:
 *     ze = 0.1 (dummy value between -1. and 1.)
 *     we = 1.
 */
void compute2DOrthoProjection(glm::mat3 & MP, float invratio);

/// @}
// Math =====================================================================
/// @name Math
/// @{

/**
 * @brief fastAtan2 returns the arc tangent of y/x in the range [-pi,pi] radians
 */
float fastAtan2(const float y, const float x);

/// @}
// BoundBox =====================================================================
/// @name Bounding Box
/// @{

/**
 * @brief The viewbox struct
 *
 */
struct s_boundbox
{
  glm::vec3 m_min;
  glm::vec3 m_max;
  s_boundbox() : m_min(std::numeric_limits<float>::infinity()), m_max(-std::numeric_limits<float>::infinity()) {}
  s_boundbox(float dx, float dy, float dz = 0.f) : m_min(glm::vec3(-dx, -dy, -dz)), m_max(glm::vec3(dx, dy, dz)) {}
  s_boundbox(const glm::vec3 &pt) : m_min(pt), m_max(pt) {}
  s_boundbox(const glm::vec3 &box_min, const glm::vec3 &box_max) : m_min(box_min), m_max(box_max) {}
  s_boundbox transform(const glm::mat4 &transform) const; ///< transform by the transform matrix
  s_boundbox & operator+=(const s_boundbox &other);
  s_boundbox operator+(const s_boundbox &other) const;
  s_boundbox & operator*=(const float scale);
  void addPointInBox(const glm::vec3 &pt);
  void addPointInBox(float px, float py, float pz = 0.f) { addPointInBox(glm::vec3(px,py,pz)); }

  inline bool valid() const { return glm::all(glm::lessThanEqual(m_min, m_max)); }
  inline glm::vec3 center() const { return 0.5f * (m_min + m_max); }
  inline glm::vec3 extend() const { return m_max - m_min; }

  void read(std::istream & inbuffer); ///< load from buffer
  void write(std::ostream & outbuffer) const; ///< dump into buffer
};

/// @}
// Geometry =====================================================================
/// @name Geometry
/// @{

/**
 * @brief triangleRaytrace3D
 * Returns true if the ray hits a triangle (v00,v01,v10)
 * @param[in] v00 vertex (3D coordinates)
 * @param[in] v01 vertex (3D coordinates), (v00-v01) defines the parametric-coordinate "u"
 * @param[in] v10 vertex (3D coordinates), (v00-v10) defines the parametric-coordinate "v"
 * @param[in] origin origin of the ray
 * @param[in] direction direction of the ray
 * @param[out] coordUVT when the ray hits the triangle, the parametric-coordinates (u,v,t) are filled. t is the distance (> 0).
 */
bool triangleRaytrace3D(const glm::vec3 &v00, const glm::vec3 &v01, const glm::vec3 &v10, const glm::vec3 &origin, const glm::vec3 &direction, glm::vec3 * coordUVT = nullptr);

/**
 * @brief triangleProject3D
 * @param v00 (3D coordinates)
 * @param v01 (3D coordinates)
 * @param v10 (3D coordinates)
 * @param origin
 * @param direction
 * @return the projected-distance of a point with a direction on the triangle (v00,v01,v10). +inf if the line does not hit the triangle.
 */
float triangleProject3D(const glm::vec3 &v00, const glm::vec3 &v01, const glm::vec3 &v10, const glm::vec3 &origin, const glm::vec3 &direction);

/**
 * @brief triangleQuality
 * @param[in] vA vertex (3D coordinates)
 * @param[in] vB vertex (3D coordinates)
 * @param[in] vC vertex (3D coordinates)
 * @param[out] area the area of the triangle (positive). If nullptr is given, the value is not computed.
 * @param[out] quality the quality of the triangle in range [0,1], where 0 is reached for degenerated triangle and 1 for equilateral triangle. If nullptr is given, the value is not computed.
 */
void triangleQuality(const glm::vec3 &vA, const glm::vec3 &vB, const glm::vec3 &vC, float * area, float * quality);

/**
 * @brief tetrahedronQuality
 * @param[in] vA vertex (3D coordinates)
 * @param[in] vB vertex (3D coordinates)
 * @param[in] vC vertex (3D coordinates)
 * @param[in] vD vertex (3D coordinates)
 * @param[out] volume the volume of the tetrahedron (positive). If nullptr is given, the value is not computed.
 * @param[out] quality the quality of the tetrahedron in range [0,1], where 0 is reached for degenerated tetrahedron and 1 for regular tetrahedron. If nullptr is given, the value is not computed.
 */
void tetrahedronQuality(const glm::vec3 &vA, const glm::vec3 &vB, const glm::vec3 &vC, const glm::vec3 &vD, float * volume, float * quality);

/**
 * @brief surfaceCurvature
 * Returns curvature (k1, k2) on main curvature direction (e1, e2), in the point's local frame
 * @param pCenter
 * @param plocalFrame transform matrix global-frame -> local-space, such as local-frame = (tanU, tanV, normal)
 * @param pOthers
 * @param curve1 curvature vector in the local space
 * @param curve2 curvature vector in the local space
 */
void surfaceCurvature(const glm::vec3 &pCenter, const glm::mat3 &plocalFrame, const tre::span<glm::vec3> pOthers, glm::vec2 &curve1, glm::vec2 &curve2);

/**
 * @brief surfaceCurvature
 * Returns curvature (k1, k2) on main curvature direction (e1, e2)
 * @param[in] pCenter
 * @param[in] normal of a surface (an approximation can be used here, such as the projection of pOthers on the normal plane does not produce irregularities)
 * @param[in] pOthers
 * @param[out] curve1 curvature vector
 * @param[out] curve2 curvature vector
 */
void surfaceCurvature(const glm::vec3 &pCenter, const glm::vec3 &normal, const tre::span<glm::vec3> pOthers, glm::vec3 &curve1, glm::vec3 &curve2);

/// @}
// Sort algorithm =====================================================================
/// @name Sort algorithms
/// @{

/**
 * @brief sort with "Counting-sort" algorithm, and make values unique (remove duplicated values)
 */
void sortAndUniqueCounting(std::vector<uint> &array);

/**
 * @brief sort with "Bull-sort" algorithm, and make values unique (remove duplicated values)
 */
void sortAndUniqueBull(std::vector<uint> &array);

/**
 * @brief sort with "Insertion" algorithm
 */
template<class _T> void sortInsertion(tre::span<_T> array);

/**
 * @brief sort with "Fusion" algorithm
 */
template<class _T> void sortFusion(tre::span<_T> array);

/**
 * @brief sort with "Quick-sort" algorithm
 */
template<class _T> void sortQuick(tre::span<_T> array);

/**
 * @brief sort with "Quick-sort" algorithm
 */
template<class _T> void sortQuick_permutation(tre::span<_T> array, tre::span<uint> permut);

/**
 * @brief sort with "Radix-sort" algorithm
 */
template<class _T> void sortRadix(tre::span<_T> array);

/// @}
// Shader-Model ===============================================================
/// @name Shaders and Models
/// @{

class model;
class shader;

/**
 * @brief check layout of the model, in regards with the shader needed inputs
 */
bool checkLayoutMatch_Shader_Model(shader *pshader, model *pmodel);

/// @}

} // namespace

#include "tre_utils.hpp"

#endif
