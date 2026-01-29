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

#define TRE_LOG_N(uniqueName, count, msg) { static int uniqueName = 0; if (uniqueName++ % count == 0) TRE_LOG("(every " << count << ") " << msg); }

#else

#define TRE_LOG(msg) {}
#define TRE_FATAL(msg) { abort(); }

#define TRE_LOG_N(uniqueName, count, msg)

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
  chunkVector() { static_assert((chunkSize != 0) && (chunkSize & (chunkSize - 1)) == 0, "chunkVector requires a power-of-two chunk-size."); }
  ~chunkVector() { clean(); }

  _T &operator[](std::size_t i) const noexcept { return (*m_chunks[i / chunkSize])[i & (chunkSize - 1)]; }

  bool        empty() const noexcept { return m_size == 0; }
  std::size_t size() const noexcept { return m_size; }

  /// clear and free memory (capacity = 0)
  void        clean() noexcept
  {
    for (chunkElement *e : m_chunks) delete e;
    m_chunks.clear();
    m_size = 0;
  }

  /// clear but do not free memory
  void        clear() noexcept { m_size = 0; }

  void        reserve(std::size_t capacity)
  {
    for (std::size_t i = m_chunks.size(), stop = (capacity + chunkSize - 1) / chunkSize; i < stop; ++i)
      m_chunks.push_back(new chunkElement);
  }

  void        resize(std::size_t size)
  {
    reserve(size);
    m_size = size;
  }

  void        push_back(const _T &element)
  {
    resize(m_size + 1);
    this->operator[](m_size - 1) = element;
  }

  void        push_back(_T &&element)
  {
    resize(m_size + 1);
    this->operator[](m_size - 1) = std::move(element);
  }

  void        pop_back() { TRE_ASSERT(m_size != 0); --m_size; }

  class iterator
  {
  public:
    iterator(const chunkVector<_T, chunkSize> &self, std::size_t index) : m_self(self), m_index(index) {}
    inline iterator& operator++() noexcept { ++m_index; return *this; }
    inline iterator  operator++(int) noexcept { return iterator(m_self, m_index++); }
    inline bool      operator==(const iterator& other) const noexcept { return m_index == other.m_index; }
    inline bool      operator!=(const iterator& other) const noexcept { return m_index != other.m_index; }
    inline _T& operator*() { return m_self[m_index]; }
  private:
    const chunkVector<_T, chunkSize> &m_self;
    std::size_t                       m_index;
  };
  iterator begin() const noexcept { return iterator(*this, 0); }
  iterator end() const noexcept { return iterator(*this, m_size); }
};

/**
 * @brief class arrayCounted
 * This extends the std::array with a dynamic size.
 * The size cannot exceed the given capacity.
 */
template<typename _T, std::size_t capacity>
class arrayCounted : public std::array<_T, capacity>
{
private:
  std::size_t m_sizeCounted;

public:
  arrayCounted() : m_sizeCounted(0) {}

  bool        emptyCounted() const noexcept { return m_sizeCounted == 0; }
  std::size_t sizeCounted() const noexcept { return m_sizeCounted; }

  void        clear() noexcept { resize(0); } ///< clear but do not free memory

  void        resize(std::size_t size)
  {
    TRE_ASSERT(size <= capacity);
    m_sizeCounted = size;
  }

  void        push_back(const _T& element)
  {
    resize(m_sizeCounted + 1);
    this->operator[](m_sizeCounted - 1) = element;
  }

  void        push_back(_T&& element)
  {
    resize(m_sizeCounted + 1);
    this->operator[](m_sizeCounted - 1) = std::move(element);
  }

  const _T   &pop_back() noexcept
  {
    m_sizeCounted = (m_sizeCounted != 0) ? m_sizeCounted - 1 : 0;
    return this->operator[](m_sizeCounted);
  }

  const _T   &back() const noexcept { return (m_sizeCounted != 0) ? this->operator[](m_sizeCounted-1) : this->operator[](0); }
  _T         &back()       noexcept { return (m_sizeCounted != 0) ? this->operator[](m_sizeCounted-1) : this->operator[](0); }

  typename std::array<_T, capacity>::iterator end() noexcept { return std::array<_T, capacity>::begin() + m_sizeCounted; } // this overwrites the std::array<>::end()
  typename std::array<_T, capacity>::const_iterator end() const noexcept { return std::array<_T, capacity>::begin() + m_sizeCounted; } // this overwrites the std::array<>::end()
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
  s_boundbox & operator*=(const float scale) { m_min *= scale; m_max *= scale; return *this; }

  void addPointInBox(const glm::vec3 &pt) { m_min = glm::min(m_min, pt); m_max = glm::max(m_max, pt); }
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

/// @}
// Sort algorithm =====================================================================
/// @name Sort algorithms
/// @{

namespace details
{
  template<class _T> static void _sortFusion(tre::span<_T> &array, std::size_t istart, std::size_t iend, std::vector<_T> &buffer)
  {
    //- stop  condition
    if ((iend-istart)<=1) return;
    //- divide
    const std::size_t sizeA = (iend-istart) / 2;
    const std::size_t icut = istart + sizeA;
    _sortFusion(array, istart, icut, buffer);
    _sortFusion(array, icut  , iend, buffer);
    // - merge (fusion)
    memcpy(buffer.data(), &array[istart], sizeA * sizeof(_T));
    _T *ptrA = &buffer[0];
    _T *ptrAStop = &buffer[sizeA];
    _T *ptrB = &array[icut];
    _T *ptrBStop = &array[iend];
    _T *ptrMerge = &array[istart];

    while (ptrA != ptrAStop && ptrB != ptrBStop)
      *ptrMerge++ = (*ptrA < *ptrB) ? *ptrA++ : *ptrB++;

    while (ptrA != ptrAStop)
      *ptrMerge++ = *ptrA++;

    //while (ptrB != ptrBStop) => not needed as the arrayB is in-place.
    //  *ptrMerge++ = *ptrB++;
  }

  template<class _T> static void _sortQuick(tre::span<_T> &array, std::size_t istart, std::size_t iend)
  {
    //- stop  condition
    if ((iend-istart)<=1) return;
    //- find partition
    std::size_t ileft = istart, iright = iend-1;
    _T vpivot = array[istart];
    while (1)
    {
      while (ileft < iend     && array[ileft] <= vpivot) ++ileft;
      while (iright >= istart && array[iright] > vpivot) --iright;
      if (ileft<iright)
      {
        std::swap(array[ileft], array[iright]);
      }
      else
      {
        break;
      }
    }
    // pivot is on position "iright"
    {
      std::swap(array[istart], array[iright]);
    }
    //- divide recursively
    _sortQuick(array,istart,iright);
    _sortQuick(array,iright+1,iend);
  }

  template<class _T> static void _sortQuick_permutation(tre::span<_T> &array, tre::span<unsigned> &permut, std::size_t istart, std::size_t iend)
  {
    //- stop  condition
    if ((iend-istart)<=1) return;
    //- find partition
    std::size_t ileft = istart, iright = iend-1;
    _T vpivot = array[istart];
    while (1)
    {
      while (ileft < iend     && array[ileft] <= vpivot) ++ileft;
      while (iright >= istart && array[iright] > vpivot) --iright;
      if (ileft<iright)
      {
        std::swap(array[ileft], array[iright]);
        std::swap(permut[ileft], permut[iright]);
      }
      else
      {
        break;
      }
    }
    // pivot is on position "iright"
    {
      std::swap(array[istart], array[iright]);
      std::swap(permut[istart], permut[iright]);
    }
    //- divide recursively
    _sortQuick_permutation(array,permut,istart,iright);
    _sortQuick_permutation(array,permut,iright+1,iend);
  }

  template<class _T> struct sortRadixKey
  {
    static inline unsigned key(const _T &v);
  };

  template<> struct sortRadixKey<unsigned>
  {
    static inline unsigned key(const unsigned &v) { return v; }
  };

  template<> struct sortRadixKey<float>
  {
    static inline unsigned key(const float &v) { return *reinterpret_cast<const unsigned*>(&v) ^ ((*reinterpret_cast<const int*>(&v) >> 31) | 0x80000000); }
  };

}

/**
 * @brief sort with "Counting-sort" algorithm, and make values unique (remove duplicated values)
 */
void sortAndUniqueCounting(std::vector<unsigned> &array);

/**
 * @brief sort with "Bull-sort" algorithm, and make values unique (remove duplicated values)
 */
void sortAndUniqueBull(std::vector<unsigned> &array);

/**
 * @brief sort with "Insertion" algorithm
 */
template<class _T> void sortInsertion(tre::span<_T> array)
{
  for (size_t i = 1, iend = array.size(); i < iend; ++i)
  {
    size_t j = i;
    while (j != 0 && array[j] < array[j - 1])
    {
      std::swap(array[j], array[j - 1]);
      --j;
    }
  }
}

/**
 * @brief sort with "Fusion" algorithm
 */
template<class _T> void sortFusion(tre::span<_T> array)
{
  std::vector<_T> arrayBuffer(array.size() / 2);
  details::_sortFusion<_T>(array, 0, array.size(), arrayBuffer);
}

/**
 * @brief sort with "Quick-sort" algorithm
 */
template<class _T> void sortQuick(tre::span<_T> array)
{
  details::_sortQuick<_T>(array, 0, array.size());
}

/**
 * @brief sort with "Quick-sort" algorithm
 */
template<class _T> void sortQuick_permutation(tre::span<_T> array, tre::span<unsigned> permut)
{
  TRE_ASSERT(permut.size() == array.size());
  details::_sortQuick_permutation<_T>(array, permut, 0, array.size());
}

/**
 * @brief sort with "Radix-sort" algorithm
 */
template<class _T> void sortRadix(tre::span<_T> array)
{
  if (array.empty()) return;

  const std::size_t n = array.size();
  std::vector<_T> array2V(n);
  tre::span<_T> array2(array2V);

  unsigned counter[256];

  for (uint ishift = 0, s = 0; ishift < 4; ++ishift, s += 8)
  {
    memset(counter, 0, sizeof(unsigned) * 256); // reset counter

    for (std::size_t i = 0; i < n; ++i)
      ++counter[(details::sortRadixKey<_T>::key(array[i]) >> s) & 0xFF];

    for (std::size_t j = 1; j < 256; ++j)
      counter[j] += counter[j-1];

    for (std::size_t i = n; i-- != 0;)
    {
      unsigned j = (details::sortRadixKey<_T>::key(array[i]) >> s) & 0xFF;
      array2[--counter[j]] = array[i];
    }

    array.swap(array2); // result is in "array2", so swap it with "array". It runs in O(1) as std::vector::swap swaps the data pointers and the vector's intern data.
  }

}

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
// Noise ======================================================================
/// @name noise helpers
/// @{

/**
* @brief return integer given 3 integers
*/
static int hashI3(const glm::ivec3 &p)
{
  int n = p.x * 3 + p.y * 113 + p.z * 311; // 3D -> 1D
  n = (n << 13) ^ n;
  return n * (n * n * 15731 + 789221) + 1376312589;
}

/**
* @brief return float value in [0,1] given 2 integers
*/
static float hashF2(int p, int q)
{
  p = p * 3 + q * 11;
  p = (p << 13) ^ p;
  p = p * (p * p * 15731 + 789221) + 1376312589;
  return float(p & 0x0fffffff) / float(0x0fffffff);
}

/**
* @brief return float value in [0,1] given 3 integers
*/
static float hashF3(const glm::ivec3 &p)
{
  return float(hashI3(p) & 0x0fffffff) / float(0x0fffffff);
}

/**
* @brief return cubic interoplated value in [0,1], noise resolution is 1.
*/
float noise1(float t, int seed);

/**
* @brief return cubic interpolated value on 3d-grid values in [0,1]. The grid resolution is 1.
*/
float noise3(const glm::vec3 &x);

/**
* @brief return gradient in xyz and value in z, of a cubic interpolated 3d-grid values in [0,1]. The grid resolution is 1.
*/
glm::vec4 noise3GradAndValue(const glm::vec3 &x);

/// @}
// FFT ======================================================================
/// @name Fast Fourier Transform helpers
/// @{

/**
* @brief compute in-place the DFT of "data" (complex). The size must be a power of 2.
*/
void fft(glm::vec2 * __restrict data, const std::size_t n, const bool inverse);

/**
* @brief compute in-place the DFT of "data" (complex), describing a n x n values. The size must be a power of 2. "sideBuffer" must cover at least n elements.
*/
void fft2D(glm::vec2 * __restrict data, const std::size_t n, const bool inverse, glm::vec2 * __restrict sideBuffer);

/// @}

} // namespace

#endif
