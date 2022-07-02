
namespace tre {

// ============================================================================

template<typename _T, std::size_t chunkSize>
void chunkVector<_T, chunkSize>::clean() noexcept
{
  for (chunkElement *e : m_chunks) delete e;
  m_chunks.clear();
  m_size = 0;
}

// ----------------------------------------------------------------------------

template<typename _T, std::size_t chunkSize>
void chunkVector<_T, chunkSize>::reserve(std::size_t capacity)
{
  for (std::size_t i = m_chunks.size(), stop = (capacity + chunkSize - 1) / chunkSize; i < stop; ++i)
    m_chunks.push_back(new chunkElement);
}

// ----------------------------------------------------------------------------

template<typename _T, std::size_t chunkSize>
void chunkVector<_T, chunkSize>::resize(std::size_t size)
{
  reserve(size);
  m_size = size;
}

// ----------------------------------------------------------------------------

template<typename _T, std::size_t chunkSize>
void chunkVector<_T, chunkSize>::push_back(const _T &element)
{
  resize(m_size + 1);
  this->operator[](m_size - 1) = element;
}

// ----------------------------------------------------------------------------

template<typename _T, std::size_t chunkSize>
void chunkVector<_T, chunkSize>::push_back(_T &&element)
{
  resize(m_size + 1);
  this->operator[](m_size - 1) = std::move(element);
}

// ============================================================================

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

// ----------------------------------------------------------------------------

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

template<class _T> void sortFusion(tre::span<_T> array)
{
  std::vector<_T> arrayBuffer(array.size() / 2);
  _sortFusion<_T>(array, 0, array.size(), arrayBuffer);
}

// ----------------------------------------------------------------------------

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

template<class _T> void sortQuick(tre::span<_T> array)
{
  _sortQuick<_T>(array, 0, array.size());
}

// ----------------------------------------------------------------------------

template<class _T> static void _sortQuick_permutation(tre::span<_T> &array, tre::span<uint> &permut, std::size_t istart, std::size_t iend)
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

template<class _T> void sortQuick_permutation(tre::span<_T> array, tre::span<uint> permut)
{
  const uint n = array.size();
  TRE_ASSERT(permut.size() == n);
  _sortQuick_permutation<_T>(array, permut, 0, n);
}

// ----------------------------------------------------------------------------

template<class _T> struct sortRadixKey
{
  static inline uint key(const _T &v);
};

template<> struct sortRadixKey<uint>
{
  static inline uint key(const uint &v) { return v; }
};

template<> struct sortRadixKey<float>
{
  static inline uint key(const float &v) { return *reinterpret_cast<const uint*>(&v) ^ ((*reinterpret_cast<const int*>(&v) >> 31) | 0x80000000); }
};

template<class _T> void sortRadix(tre::span<_T> array)
{
  if (array.empty()) return;

  const std::size_t n = array.size();
  std::vector<_T> array2V(n);
  tre::span<_T> array2(array2V);

  uint counter[256];

  for (uint ishift = 0, s = 0; ishift < 4; ++ishift, s += 8)
  {
    memset(counter, 0, sizeof(uint) * 256); // reset counter

    for (std::size_t i = 0; i < n; ++i)
      ++counter[(sortRadixKey<_T>::key(array[i]) >> s) & 0xFF];

    for (std::size_t j = 1; j < 256; ++j)
      counter[j] += counter[j-1];

    for (std::size_t i = n; i-- != 0;)
    {
      uint j = (sortRadixKey<_T>::key(array[i]) >> s) & 0xFF;
      array2[--counter[j]] = array[i];
    }

    array.swap(array2); // result is in "array2", so swap it with "array". It runs in O(1) as std::vector::swap swaps the data pointers and the vector's intern data.
  }
}

// ============================================================================

} // namespace
