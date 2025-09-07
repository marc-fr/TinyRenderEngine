#include "tre_textureSampler.h"

#include "tre_utils.h"

#ifdef TRE_WITH_LIBTIFF
#include "tiffio.h"
#endif

namespace tre {

//=============================================================================
// Type helpers

struct s_RBGA8;
struct s_RBGfloat;

// ----------------------------------------------------------------------------

struct s_RBGA8
{
  uint8_t r,g,b,a;
  inline s_RBGA8(uint8_t v = 0) : r(v), g(v), b(v), a(0xFF) {}
  inline s_RBGA8(const s_RBGfloat &from);
};

// ----------------------------------------------------------------------------

struct s_RBGfloat
{
  float r,g,b, _padding;
  inline s_RBGfloat(float v = 0.f) : r(v), g(v), b(v) {}
  inline s_RBGfloat(const s_RBGA8 &from);
  inline s_RBGfloat operator*(const float scale) const;
  inline s_RBGfloat &operator+=(const s_RBGfloat &other);
};

// ----------------------------------------------------------------------------

s_RBGA8::s_RBGA8(const s_RBGfloat &from)
{
  r = uint8_t(from.r * 0xFF);
  b = uint8_t(from.b * 0xFF);
  g = uint8_t(from.g * 0xFF);
  a = 0xFF;
}

// ----------------------------------------------------------------------------

s_RBGfloat::s_RBGfloat(const s_RBGA8 &from)
{
  r = float(from.r) / float(0xFF);
  b = float(from.b) / float(0xFF);
  g = float(from.g) / float(0xFF);
}

// ----------------------------------------------------------------------------

s_RBGfloat s_RBGfloat::operator*(const float scale) const
{
  s_RBGfloat ret = *this;
  ret.r *= scale;
  ret.g *= scale;
  ret.b *= scale;
  return ret;
}

// ----------------------------------------------------------------------------

s_RBGfloat &s_RBGfloat::operator+=(const s_RBGfloat &other)
{
  r += other.r;
  g += other.g;
  b += other.b;
  return *this;
}

//=============================================================================
// Sampler Interface

template <typename _T, class _S>
struct s_sampler
{
  s_sampler(_S *s);
  _T pixelGet(uint u, uint v) const;
  void pixelSet(uint u, uint v, _T value);
};

// ----------------------------------------------------------------------------

template<>
struct s_sampler<s_RBGA8, SDL_Surface>
{
  SDL_Surface *m_surf;

  s_sampler(SDL_Surface *s) : m_surf(s)
  {
  }

  uint8_t *pixelAdress(uint u, uint v) const
  {
    return reinterpret_cast<uint8_t*>(m_surf->pixels) + v * uint(m_surf->pitch) + u * m_surf->format->BytesPerPixel;
  }

  s_RBGA8 pixelGet(uint u, uint v) const
  {
    uint8_t* p = pixelAdress(u, v);

    uint32_t pixelcolor = 0;

    switch (m_surf->format->BytesPerPixel)
    {
    case 1:
      pixelcolor = *p & 0xFF;
      break;
    case 2:
      pixelcolor = *reinterpret_cast<uint16_t*>(p) & 0xFFFF;
      break;
    case 3:
      TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN);
      pixelcolor = (p[0]) | (p[1] << 8) | (p[2] << 16);
      break;
    case 4:
      pixelcolor = *reinterpret_cast<uint32_t*>(p);
      break;
    default:
      TRE_FATAL("bad format");
    }

    s_RBGA8 ret;
    SDL_GetRGBA(pixelcolor,
                m_surf->format,
                &ret.r, &ret.g, &ret.b, &ret.a);

    return ret;
  }

  void pixelSet(uint u, uint v, s_RBGA8 value)
  {
    uint8_t* p = pixelAdress(u, v);
    uint32_t pixelRaw = SDL_MapRGBA(m_surf->format, value.r, value.g, value.b, value.a);

    switch (m_surf->format->BytesPerPixel)
    {
    case 1:
      *p = uint8_t(pixelRaw);
      break;
    case 2:
      *reinterpret_cast<uint16_t*>(p) = uint16_t(pixelRaw);
      break;
    case 3:
      TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN);
      p[0] = uint8_t(pixelRaw);
      p[1] = uint8_t(pixelRaw >> 8);
      p[2] = uint8_t(pixelRaw >> 16);
      break;
    case 4:
      *reinterpret_cast<uint32_t*>(p) = pixelRaw;
      break;
    default:
      TRE_FATAL("bad format");
    }
  }
};

// ----------------------------------------------------------------------------

template<>
struct s_sampler<float, SDL_Surface>
{
  s_sampler<s_RBGA8, SDL_Surface> m_sampler;

  s_sampler(SDL_Surface *s) : m_sampler(s)
  {
  }

  float pixelGet(uint u, uint v) const
  {
    return float(m_sampler.pixelGet(u, v).r) / float(0xFF);
  }

  void pixelSet(uint u, uint v, float value)
  {
    s_RBGA8 valueExt = s_RBGA8(uint8_t(value * 0xFF));
    m_sampler.pixelSet(u, v, valueExt);
  }
};

// ----------------------------------------------------------------------------

#ifdef TRE_WITH_LIBTIFF

static const char * orientationNames[] = { "default", "top-left", "top-right", "bot-right", "bot-left", "top-left (COL-MAJOR)", "top-right (COL-MAJOR)", "bot-right (COL-MAJOR)", "bot-left (COL-MAJOR)" };
static const char * formatNames[] = { "Invalid", "R32F", "RGBA8", "RGB8" };

struct s_driverTIFF
{
  TIFF *m_tiff;
  uint m_w = 0, m_h = 0;
  bool m_flipV = false;

  enum s_pixelFormat { INVALID, R32F, RGBA8, RGB8 }; // list of supported format by the "s_driverTIFF"
  s_pixelFormat m_sourceFormat;

  uint m_bytesPerPixel = 0;
  tsize_t  m_stripSizeInBytes = 0;
  uint m_rowsPerStrip = 0;

  mutable tstrip_t m_decodedStripID;
  mutable char*    m_decodedStripData;

#ifdef TRE_DEBUG
  mutable uint m_perfCount_pixelReqCount = 0;
  mutable uint m_perfCount_decodeCount = 0;
#endif

  s_driverTIFF(TIFF *s) : m_tiff(s)
  {
    TIFFGetField(m_tiff, TIFFTAG_IMAGEWIDTH, &m_w);
    TIFFGetField(m_tiff, TIFFTAG_IMAGELENGTH, &m_h);
    TRE_ASSERT(m_w != 0 && m_h != 0);

    // Get the source format
    uint16_t bytesPerSample = 0;
    TIFFGetField(m_tiff, TIFFTAG_BITSPERSAMPLE, &bytesPerSample);
    bytesPerSample /= 8;
    uint16_t samplePerPixel = 0;
    TIFFGetField(m_tiff, TIFFTAG_SAMPLESPERPIXEL, &samplePerPixel);
    uint16_t sampleFormat = 0;
    TIFFGetField(m_tiff, TIFFTAG_SAMPLEFORMAT, &sampleFormat);
    TRE_ASSERT(bytesPerSample != 0 && samplePerPixel != 0);

    if (samplePerPixel == 1 && bytesPerSample == 4 && sampleFormat == SAMPLEFORMAT_IEEEFP)
    {
      m_sourceFormat = R32F;
    }
    else if (samplePerPixel == 4 && bytesPerSample == 1)
    {
      m_sourceFormat = RGBA8;
    }
    else if (samplePerPixel == 3 && bytesPerSample == 1)
    {
      m_sourceFormat = RGB8;
    }
    TRE_ASSERT(m_sourceFormat != INVALID);

    // Get buffer size

    const uint stripCount = TIFFNumberOfStrips(m_tiff);
    m_stripSizeInBytes = tsize_t(TIFFStripSize(m_tiff));
    TIFFGetField(m_tiff, TIFFTAG_ROWSPERSTRIP, &m_rowsPerStrip);

    m_bytesPerPixel = bytesPerSample * samplePerPixel;
    m_stripSizeInBytes = tsize_t(TIFFStripSize(m_tiff));

    TRE_ASSERT(stripCount != 0 && m_stripSizeInBytes != 0 && m_rowsPerStrip != 0);

    // Check orientation
    uint16_t orient = 0;
    int hasOrient = TIFFGetField(m_tiff, TIFFTAG_ORIENTATION, &orient);
    if      (hasOrient == 0) { m_flipV = false; /* default */ }
    else if (hasOrient != 0 && orient == ORIENTATION_TOPLEFT) { m_flipV = false; }
    else if (hasOrient != 0 && orient == ORIENTATION_BOTLEFT) { m_flipV = true; }
    else
    {
      TRE_LOG("TIFF: unsupported image orientation. Ignore this error.");
    }

    // Allocate data
    m_decodedStripID = tstrip_t(std::numeric_limits<int>::max());
    m_decodedStripData = reinterpret_cast<char*>(_TIFFmalloc(m_stripSizeInBytes));
    TRE_ASSERT(m_decodedStripData != nullptr);

    // Log
    TRE_LOG("TIFF Image sampler Info" << std::endl <<
            "- Source = " << formatNames[m_sourceFormat] << " format with dimension " << m_w << " * " << m_h << " with " << orientationNames[orient] << " orientation" << std::endl <<
            "- BytesPerPixel = " << bytesPerSample << " BytesPerSample * " << samplePerPixel << " SamplesPerPixel" << std::endl <<
            "- strips = " << stripCount << " strips of size " << m_stripSizeInBytes << " bytes (" << m_rowsPerStrip << " rows per strip)" << std::endl);
  }

  ~s_driverTIFF()
  {
    if (m_decodedStripData != nullptr) _TIFFfree(m_decodedStripData);
    m_decodedStripData = nullptr;
#ifdef TRE_DEBUG
    TRE_LOG("TIFF Driver perf-counter: NPixelRequests = " << m_perfCount_pixelReqCount << ", NDecodedStrip = " << m_perfCount_decodeCount << " ( " <<
            float(m_perfCount_decodeCount) / float(m_perfCount_pixelReqCount) * 100.f << " %)");
#endif
  }

  char *requestPixel(uint u, uint v) const
  {
#ifdef TRE_DEBUG
    ++m_perfCount_pixelReqCount;
#endif

    if (m_flipV) v = m_h - 1 - v;

    tstrip_t reqStrip = TIFFComputeStrip(m_tiff, v, 0);

    if (reqStrip != m_decodedStripID)
    {
      // load a new strip ...
      m_decodedStripID = reqStrip;
      tsize_t ret = TIFFReadEncodedStrip(m_tiff, reqStrip, m_decodedStripData, -1);
      TRE_ASSERT(ret != -1);
#ifdef TRE_DEBUG
      ++m_perfCount_decodeCount;
#endif
    }

    v = v % m_rowsPerStrip;

    TRE_ASSERT((v * m_w + u) * m_bytesPerPixel <= uint(m_stripSizeInBytes));
    return m_decodedStripData + (v * m_w + u) * m_bytesPerPixel;
  }
};

template<>
struct s_sampler<s_RBGA8, TIFF>
{
  s_driverTIFF m_driverTiff;

  s_sampler(TIFF *s) : m_driverTiff(s)
  {
  }

  s_RBGA8 pixelGet(uint u, uint v) const
  {
    if (m_driverTiff.m_sourceFormat == s_driverTIFF::R32F)
    {
      uint8_t valueR8 = uint8_t(*reinterpret_cast<const float*>(m_driverTiff.requestPixel(u, v)) * 0xFF);
      return valueR8;
    }
    else if (m_driverTiff.m_sourceFormat == s_driverTIFF::RGBA8)
    {
      return *reinterpret_cast<const s_RBGA8*>(m_driverTiff.requestPixel(u, v));
    }
    else if (m_driverTiff.m_sourceFormat == s_driverTIFF::RGB8)
    {
      const uint8_t *px = reinterpret_cast<const uint8_t*>(m_driverTiff.requestPixel(u, v));
      s_RBGA8 ret;
      ret.r = *px++;
      ret.g = *px++;
      ret.b = *px;
      return ret;
    }
    return 0; // should have asserted before
  }
};

// ----------------------------------------------------------------------------

template<>
struct s_sampler<float, TIFF>
{
  s_driverTIFF m_driverTiff;

  s_sampler(TIFF *s) : m_driverTiff(s)
  {
  }

  float pixelGet(uint u, uint v) const
  {
    if (m_driverTiff.m_sourceFormat == s_driverTIFF::R32F)
    {
      return *reinterpret_cast<const float *>(m_driverTiff.requestPixel(u, v));
    }
    else if (m_driverTiff.m_sourceFormat == s_driverTIFF::RGBA8)
    {
      s_RBGA8 value = *reinterpret_cast<const s_RBGA8*>(m_driverTiff.requestPixel(u, v));
      return float(value.r) / float(0xFF);
    }
    else if (m_driverTiff.m_sourceFormat == s_driverTIFF::RGB8)
    {
      const uint8_t px = *reinterpret_cast<const uint8_t*>(m_driverTiff.requestPixel(u, v)) & 0xFF;
      return float(px) / float(0xFF);
    }
    return 0.f; // should have asserted before
  }
};

#endif // TRE_WITH_LIBTIFF

// ----------------------------------------------------------------------------

template<>
struct s_sampler<float, textureSampler::s_ImageData_R32F>
{
  textureSampler::s_ImageData_R32F *m_sampler;

  s_sampler(textureSampler::s_ImageData_R32F *s) : m_sampler(s)
  {
    TRE_ASSERT(m_sampler->pixels != nullptr);
  }

  float pixelGet(uint u, uint v) const
  {
    return m_sampler->pixels[v * m_sampler->w + u];
  }

  void pixelSet(uint u, uint v, float value)
  {
    m_sampler->pixels[v * m_sampler->w + u] = value;
  }
};

//=============================================================================
// Sampling Implementation

// note: the kernel should verify: f(x) = 0 if |x| >= 1, f(x) = f(-x), integral(f,-1,1) = 1
// thus: Value_uuvv = sum( pixel_i * integral(kernel_for_pixel_i, uuvv_Kf) ) / sum_over_i( integral(kernel_for_pixel_i, uuvv_Kf) )

struct s_kernel_C0
{
  // C0 kernel: f(x) = { 1 - |x| if |x| <= 1, 0 elsewhere }

  static inline float kernel_intg_x1(const float &_s, const float &_e)
  {
    const float s = glm::clamp(_s, -1.f, 1.f);
    const float e = glm::clamp(_e, -1.f, 1.f);
    const float wL0 = - 0.5f * (2.f - glm::abs(s)) * s;
    const float w0R =   0.5f * (2.f - glm::abs(e)) * e;
    return wL0 + w0R;
  }

  template<typename _T> static _T sampleValue(_T * __restrict data, const uint dataW, const uint dataH, const glm::vec4 &uuvv)
  {
    const glm::vec4  uuvv_Kf = uuvv - 0.5f;
    const glm::uvec4 uuvv_Ki = glm::clamp(uuvv_Kf, glm::vec4(0.f), glm::vec4(dataW, dataW, dataH, dataH));

    _T     value  = _T(0);
    float  weight = 0.f;

    for (uint iv = uuvv_Ki.z, ivStop = glm::min(uuvv_Ki.w + 1, dataH - 1); iv <= ivStop; ++iv)
    {
      const float v_fStart = uuvv_Kf.z - iv;
      const float v_fEnd   = uuvv_Kf.w - iv;
      const float v_w      = kernel_intg_x1(v_fStart, v_fEnd);

      for (uint iu = uuvv_Ki.x, iuStop = glm::min(uuvv_Ki.y + 1, dataW - 1); iu <= iuStop; ++iu)
      {
        const float u_fStart = uuvv_Kf.x - iu;
        const float u_fEnd   = uuvv_Kf.y - iu;
        const float u_w      = kernel_intg_x1(u_fStart, u_fEnd);

        const float w_uv = u_w * v_w;
        weight += w_uv;
        value += data[iu * dataH + iv] * w_uv;
      }
    }

    TRE_ASSERT(weight > 0.f);

    return value * (1.f / weight);
  }
};

//=============================================================================
// Main entry-points: resample

template<class _Kernel, typename _Tin, typename _Tout, typename _Tcompute, class _Sin, class _Sout>
void resample_imp(_Sout *outTexture, const uint out_un, const uint out_vn,
                  _Sin *inTexture, const uint in_un, const uint in_vn,
                  const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
  s_sampler<_Tin, _Sin> samplerIn(inTexture);
  s_sampler<_Tout, _Sout> samplerOut(outTexture);

  const glm::vec2  inWH   = glm::vec2(in_un, in_vn);
  const glm::vec4  inWWHH = glm::vec4(in_un, in_un, in_vn, in_vn);

  const glm::vec2 du = in_coordU_incr / float(out_un);
  const glm::vec2 dv = in_coordV_incr / float(out_vn);

  const glm::vec2 px_uv = glm::abs(du) + glm::abs(dv); // sampling-extend in the in-texture for 1 out-pixel
  const glm::vec4 px_uuvv = 0.5f * glm::vec4(-px_uv.x, px_uv.x, -px_uv.y, px_uv.y); // sampling-zone in the in-texture for 1 out-pixel

  // the tile is 32*32 out-pixels
  const glm::uvec2 tile_nm_out = glm::uvec2(32u);
  const glm::vec2  tile_uv_in = glm::vec2(tile_nm_out) * px_uv;
  const glm::uvec2 tile_nm_in = tile_uv_in * inWH + 4.f; /* + margin */
  const glm::ivec4 tile_nnmm_in = glm::ivec4(-tile_nm_in.x, tile_nm_in.x, -tile_nm_in.y, tile_nm_in.y) / 2;

  std::vector<_Tcompute> px_data_in;
  px_data_in.resize(tile_nm_in.x * tile_nm_in.y);
  TRE_ASSERT(px_data_in.size() == tile_nm_in.x * tile_nm_in.y);

  for (uint iv = 0; iv < out_vn; iv += tile_nm_out.y)
  {
    const uint ivCount = glm::min(out_vn - iv, tile_nm_out.y);

    for (uint iu = 0; iu < out_un; iu += tile_nm_out.x)
    {
      const uint iuCount = glm::min(out_un - iu, tile_nm_out.x);

      // get tile bounds
      const glm::vec2 curtile_uv_C = in_coord0 + du * float(iu + 0.5f + 0.5f * iuCount) + dv * float(iv + 0.5f + 0.5f * ivCount);
      const glm::ivec2 curtile_nm_C = curtile_uv_C * inWH;
      const glm::ivec4 curtile_nnmm_in = glm::ivec4(curtile_nm_C.x, curtile_nm_C.x, curtile_nm_C.y, curtile_nm_C.y) + tile_nnmm_in;

      // get in-pixel data
      for (uint ivL = 0; ivL < tile_nm_in.y; ++ivL)
      {
        const uint ivIn = uint(glm::clamp(curtile_nnmm_in.z + int(ivL), 0, int(in_vn - 1)));
        for (uint iuL = 0; iuL < tile_nm_in.x; ++iuL)
        {
          const uint iuIn = uint(glm::clamp(curtile_nnmm_in.x + int(iuL), 0, int(in_un - 1)));
          px_data_in[iuL * tile_nm_in.y + ivL] = samplerIn.pixelGet(iuIn, ivIn);
        }
      }

      // sample into out-pixel data
      const glm::vec4 curtile_nnmm_offset = glm::vec4(curtile_nnmm_in.x, curtile_nnmm_in.x, curtile_nnmm_in.z, curtile_nnmm_in.z);
      for (uint ivL = 0; ivL < ivCount; ++ivL)
      {
        for (uint iuL = 0; iuL < iuCount; ++iuL)
        {
          const glm::vec2 uv_center = in_coord0 + du * float(iu + iuL + 0.5f) + dv * float(iv + ivL + 0.5f);
          const glm::vec4 nnmm_in = (glm::vec4(uv_center.x, uv_center.x, uv_center.y, uv_center.y) + px_uuvv) * inWWHH;
          const glm::vec4 nnmm_inLocal = nnmm_in - curtile_nnmm_offset;
          const _Tcompute in_value = _Kernel::sampleValue(px_data_in.data(), tile_nm_in.x, tile_nm_in.y, nnmm_inLocal);
          samplerOut.pixelSet(iu + iuL, iv + ivL, in_value);
        }
      }

      // end of tile sampling
    }
  }
}

//-----------------------------------------------------------------------------

void textureSampler::resample(SDL_Surface *outTexture, SDL_Surface *inTexture, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
  const uint in_un = uint(inTexture->w);
  const uint in_vn = uint(inTexture->h);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_imp<s_kernel_C0, s_RBGA8, s_RBGA8, s_RBGfloat>(outTexture, out_un, out_vn,
                                                          inTexture, in_un, in_vn,
                                                          in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::resample(SDL_Surface *outTexture, TIFF *inTexture, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTexture, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTexture, TIFFTAG_IMAGELENGTH, &in_vn);

  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_imp<s_kernel_C0, s_RBGA8, s_RBGA8, s_RBGfloat>(outTexture, out_un, out_vn,
                                                          inTexture, in_un, in_vn,
                                                          in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTexture;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}

//-----------------------------------------------------------------------------

void textureSampler::resample(s_ImageData_R32F *outTexture, SDL_Surface *inTexture, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
  const uint in_un = uint(inTexture->w);
  const uint in_vn = uint(inTexture->h);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_imp<s_kernel_C0, float, float, float>(outTexture, out_un, out_vn,
                                                 inTexture, in_un, in_vn,
                                                 in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::resample(s_ImageData_R32F *outTexture, TIFF *inTexture, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTexture, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTexture, TIFFTAG_IMAGELENGTH, &in_vn);

  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_imp<s_kernel_C0, float, float, float>(outTexture, out_un, out_vn,
                                                 inTexture, in_un, in_vn,
                                                 in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTexture;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}

//=============================================================================
// Main entry-points: mapNormals

template<class _S> void mapNormals_imp(SDL_Surface *outTexture,
                                       _S *inTexture, const float factor, const uint in_un, const uint in_vn,
                                       const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
  s_sampler<s_RBGA8, SDL_Surface> samplerOut(outTexture);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  // pre-sample

  textureSampler::s_ImageData_R32F tmpTexture;
  std::vector<float> tmpValues;
  tmpValues.resize(out_un * out_vn);
  tmpTexture.pixels = tmpValues.data();
  tmpTexture.w = out_un;
  tmpTexture.h = out_vn;

  resample_imp<s_kernel_C0, float, float, float>(&tmpTexture, out_un, out_vn,
                                                 inTexture, in_un, in_vn,
                                                 in_coord0, in_coordU_incr, in_coordV_incr);

  // compute the normals

  const glm::vec2 du = in_coordU_incr / float(out_un);
  const glm::vec2 dv = in_coordV_incr / float(out_vn);
  const glm::vec2 dudv = 0.5f * (glm::abs(du) + glm::abs(dv));

  const float inv_dx = factor / (2.f * dudv.x);
  const float inv_dy = factor / (2.f * dudv.y);

  for (uint iv = 0; iv < out_vn; ++iv)
  {
    for (uint iu = 0; iu < out_un; ++iu)
    {
      const uint iuv = out_un * iv + iu;
      float dval_x, dval_y;

      if      (iu == 0         ) dval_x = (tmpValues[iuv + 1] - tmpValues[iuv]) * inv_dx;
      else if (iu == out_un - 1) dval_x = (tmpValues[iuv] - tmpValues[iuv - 1]) * inv_dx;
      else                       dval_x = (tmpValues[iuv + 1] - tmpValues[iuv - 1]) * 0.5f * inv_dx;

      if      (iv == 0         ) dval_y = (tmpValues[iuv + out_un] - tmpValues[iuv]) * inv_dy;
      else if (iv == out_vn - 1) dval_y = (tmpValues[iuv] - tmpValues[iuv - out_un]) * inv_dy;
      else                       dval_y = (tmpValues[iuv + out_un] - tmpValues[iuv - out_un]) * 0.5f * inv_dy;

      glm::vec3 outNormal;
      outNormal.x = -dval_x;
      outNormal.y = dval_y;
      outNormal.z = 1.f;

      outNormal = 0.5f + 0.5f * glm::normalize(outNormal);

      s_RBGfloat px_float;
      px_float.r = outNormal.r;
      px_float.g = outNormal.g;
      px_float.b = outNormal.b;

      samplerOut.pixelSet(iu, iv, s_RBGA8(px_float));
    }
  }
}

//-----------------------------------------------------------------------------

void textureSampler::mapNormals(SDL_Surface *outTexture, SDL_Surface *inTexture, const float factor, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
  const uint in_un = uint(inTexture->w);
  const uint in_vn = uint(inTexture->h);

  mapNormals_imp(outTexture, inTexture, factor, in_un, in_vn, in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::mapNormals(SDL_Surface *outTexture, TIFF *inTexture, const float factor, const glm::vec2 &in_coord0, const glm::vec2 &in_coordU_incr, const glm::vec2 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTexture, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTexture, TIFFTAG_IMAGELENGTH, &in_vn);

  mapNormals_imp(outTexture, inTexture, factor, in_un, in_vn, in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTexture;
  (void)factor;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}

//=============================================================================
// Main entry-points: resample_toCubeMap

static glm::vec2 coord_3D_To_2DMap(const glm::vec3 &coordUVW)
{
  const glm::vec3 coordUVW_n = glm::normalize(coordUVW);
  if (coordUVW_n.y >=  1.f) return glm::vec2(0.f, 0.f);
  if (coordUVW_n.y <= -1.f) return glm::vec2(0.f, 1.f);
  const float u = atan2f(-coordUVW_n.x, -coordUVW_n.z) * 0.3183098861837907f;
  const float v = asinf(coordUVW_n.y) * 0.6366197723675814f;
  return 0.5f + 0.5f * glm::vec2(u, -v);
}

static glm::vec3 coord_3D_To_2DMap_Deriv(const glm::vec3 &coordUVW) // packed d(u,v)/d(x,y,z)
{
  const glm::vec3 in_P = glm::normalize(coordUVW);
  const float c1myy = glm::max(1.e-3f, 1.f - in_P.y * in_P.y);
  const float inv1myy = 1.f / c1myy;
  const float map_dudx = 0.3183098861837907f * in_P.z * inv1myy;
  const float map_dudz = 0.3183098861837907f * (-in_P.x) * inv1myy;
  const float map_dvdy = 0.6366197723675814f * sqrtf(inv1myy);
  return 0.5f * glm::vec3(map_dudx, map_dudz, -map_dvdy);
}

static glm::vec2 coord_3D_To_2DMap_Repeat(const glm::vec3 &coordUVW, const glm::vec2 &coordRef)
{
  glm::vec2 ret = coord_3D_To_2DMap(coordUVW);
  ret.x += std::round(coordRef.x - ret.x); // repeat-x
  // TODO ... repeat-pole
  return ret;
}

//-----------------------------------------------------------------------------

template<class _Kernel, typename _Tin, typename _Tout, typename _Tcompute, class _Sin, class _Sout>
void resample_toCubeMap_imp(_Sout *outTexture, const uint out_un, const uint out_vn,
                            _Sin *inTextureEquirectangular, const uint in_un, const uint in_vn,
                            const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
  s_sampler<_Tin, _Sin> samplerIn(inTextureEquirectangular);
  s_sampler<_Tout, _Sout> samplerOut(outTexture);

  const glm::vec4 inWWHH = glm::vec4(in_un, in_un, in_vn, in_vn);

  const glm::vec3 du = in_coordU_incr / float(out_un);
  const glm::vec3 dv = in_coordV_incr / float(out_vn);

  // get the bounding-box of the sampling zone

  glm::vec4 global_uuvv = glm::vec4(std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                    std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());

  const glm::vec2 in_coordMap_Center = coord_3D_To_2DMap(in_coord0 + du * float(out_un / 2 + 0.5f) + dv * float(out_vn / 2 + 0.5f)); // to handle the latitude-repeat.

  for (uint iv = 0; iv < out_vn; ++iv)
  {
    for (uint iu = 0; iu < out_un; ++iu)
    {
      const glm::vec3 in_coord3D = in_coord0 + du * float(iu + 0.5f) + dv * float(iv + 0.5f);
      const glm::vec2 in_coordMap = coord_3D_To_2DMap_Repeat(in_coord3D, in_coordMap_Center);

      const glm::vec3 in_P = glm::normalize(in_coord3D);
      const glm::vec3 in_dPu = glm::normalize(in_coord3D + 0.5f * du) - in_P;
      const glm::vec3 in_dPv = glm::normalize(in_coord3D + 0.5f * dv) - in_P;
      const glm::vec3 map_derr = coord_3D_To_2DMap_Deriv(in_coord3D);

      const glm::vec2 dudv_onU = glm::abs(glm::vec2(map_derr.x * in_dPu.x + map_derr.y * in_dPu.z, map_derr.z * in_dPu.y));
      const glm::vec2 dudv_onP = glm::abs(glm::vec2(map_derr.x * in_dPv.x + map_derr.y * in_dPv.z, map_derr.z * in_dPv.y));

      const glm::vec2 dudv = glm::max(dudv_onU + dudv_onP, glm::vec2(1.e-6f));

      global_uuvv.x = glm::min(global_uuvv.x, in_coordMap.x - dudv.x);
      global_uuvv.y = glm::max(global_uuvv.y, in_coordMap.x + dudv.x);
      global_uuvv.z = glm::min(global_uuvv.z, in_coordMap.y - dudv.y);
      global_uuvv.w = glm::max(global_uuvv.w, in_coordMap.y + dudv.y);
    }
  }

  // cache the whoe sampling-zone (TODO ? split per tiles)

  const glm::ivec4 global_nnmm = glm::ivec4(global_uuvv * inWWHH) + glm::ivec4(0, 1, 0, 1); // sampling-zone (in in-pixel unit)
  TRE_ASSERT(global_nnmm.y > global_nnmm.x && global_nnmm.w > global_nnmm.z);
  TRE_ASSERT(global_nnmm.x > -int(in_un) && global_nnmm.y < 2 * int(in_un));
  TRE_ASSERT(global_nnmm.y > -int(in_vn) && global_nnmm.w < 2 * int(in_vn));
  const glm::uvec2 global_nm = glm::uvec2(global_nnmm.y - global_nnmm.x, global_nnmm.w - global_nnmm.z); // sampling-extend (in in-pixel unit)

  std::vector<_Tcompute> px_data_in;
  px_data_in.resize(global_nm.x * global_nm.y);
  TRE_ASSERT(px_data_in.size() == global_nm.x * global_nm.y);

  for (uint ivCache = 0; ivCache < global_nm.y; ++ivCache)
  {
    const int ivRaw = int(ivCache) + global_nnmm.z;
    if (ivRaw >= 0 && ivRaw < int(in_vn))
    {
      const uint iv = uint(ivRaw);
      for (uint iuCache = 0; iuCache < global_nm.x; ++iuCache)
      {
        const uint iu = (iuCache + uint(global_nnmm.x) + 2 * in_un) % in_un; // latitude-repeat
        px_data_in[iuCache * global_nm.y + ivCache] = samplerIn.pixelGet(iu, iv);
      }
    }
    else
    {
      const uint iv = (ivRaw < 0) ? uint(-ivRaw) : uint(in_vn * 2 - 1 - ivRaw);
      for (uint iuCache = 0; iuCache < global_nm.x; ++iuCache)
      {
        const uint iu = (-iuCache - uint(global_nnmm.x) + 3 * in_un) % in_un; // latitude-repeat
        px_data_in[iuCache * global_nm.y + ivCache] = samplerIn.pixelGet(iu, iv);
      }
    }
  }

  // sample into out-pixel data

  const glm::vec4 nnmm_offset = glm::vec4(global_nnmm.x, global_nnmm.x, global_nnmm.z, global_nnmm.z);

  for (uint iv = 0; iv < out_vn; ++iv)
  {
    for (uint iu = 0; iu < out_un; ++iu)
    {
      const glm::vec3 in_coord3D = in_coord0 + du * float(iu + 0.5f) + dv * float(iv + 0.5f);
      const glm::vec2 in_coordMap = coord_3D_To_2DMap_Repeat(in_coord3D, in_coordMap_Center);

      const glm::vec3 in_P = glm::normalize(in_coord3D);
      const glm::vec3 in_dPu = glm::normalize(in_coord3D + 0.5f * du) - in_P;
      const glm::vec3 in_dPv = glm::normalize(in_coord3D + 0.5f * dv) - in_P;
      const glm::vec3 map_derr = coord_3D_To_2DMap_Deriv(in_coord3D);

      const glm::vec2 dudv_onU = glm::abs(glm::vec2(map_derr.x * in_dPu.x + map_derr.y * in_dPu.z, map_derr.z * in_dPu.y));
      const glm::vec2 dudv_onP = glm::abs(glm::vec2(map_derr.x * in_dPv.x + map_derr.y * in_dPv.z, map_derr.z * in_dPv.y));

      const glm::vec2 dudv = glm::max(dudv_onU + dudv_onP, glm::vec2(1.e-6f));

      const glm::vec4 nnmm_in = glm::vec4(in_coordMap.x - dudv.x, in_coordMap.x + dudv.x, in_coordMap.y - dudv.y, in_coordMap.y + dudv.y) * inWWHH;
      const glm::vec4 nnmm_inLocal = nnmm_in - nnmm_offset;
      const _Tcompute in_value = _Kernel::sampleValue(px_data_in.data(), global_nm.x, global_nm.y, nnmm_inLocal);

      samplerOut.pixelSet(iu, iv, in_value);
    }
  }
}

//-----------------------------------------------------------------------------

void textureSampler::resample_toCubeMap(SDL_Surface *outTexture, SDL_Surface *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
  const uint in_un = uint(inTextureEquirectangular->w);
  const uint in_vn = uint(inTextureEquirectangular->h);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_toCubeMap_imp<s_kernel_C0, s_RBGA8, s_RBGA8, s_RBGfloat>(outTexture, out_un, out_vn,
                                                                    inTextureEquirectangular, in_un, in_vn,
                                                                    in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::resample_toCubeMap(SDL_Surface *outTexture, TIFF *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGELENGTH, &in_vn);

  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_toCubeMap_imp<s_kernel_C0,s_RBGA8, s_RBGA8, s_RBGfloat>(outTexture, out_un, out_vn,
                                                                   inTextureEquirectangular, in_un, in_vn,
                                                                   in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTextureEquirectangular;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}

//-----------------------------------------------------------------------------

void textureSampler::resample_toCubeMap(s_ImageData_R32F *outTexture, SDL_Surface *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
  const uint in_un = uint(inTextureEquirectangular->w);
  const uint in_vn = uint(inTextureEquirectangular->h);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_toCubeMap_imp<s_kernel_C0, float, float, float>(outTexture, out_un, out_vn,
                                                           inTextureEquirectangular, in_un, in_vn,
                                                           in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::resample_toCubeMap(s_ImageData_R32F *outTexture, TIFF *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGELENGTH, &in_vn);

  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  resample_toCubeMap_imp<s_kernel_C0, float, float, float>(outTexture, out_un, out_vn,
                                                           inTextureEquirectangular, in_un, in_vn,
                                                           in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTextureEquirectangular;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}

//=============================================================================
// Main entry-points: mapNormals_toCubeMap

template<class _S> void mapNormals_toCubeMap_imp(SDL_Surface *outTexture,
                                                 _S *inTexture, const float factor, const uint in_un, const uint in_vn,
                                                 const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
  s_sampler<s_RBGA8, SDL_Surface> samplerOut(outTexture);
  const uint out_un = uint(outTexture->w);
  const uint out_vn = uint(outTexture->h);

  // pre-sample

  textureSampler::s_ImageData_R32F tmpTexture;
  std::vector<float> tmpValues;
  tmpValues.resize(out_un * out_vn);
  tmpTexture.pixels = tmpValues.data();
  tmpTexture.w = out_un;
  tmpTexture.h = out_vn;

  resample_toCubeMap_imp<s_kernel_C0, float, float, float>(&tmpTexture, out_un, out_vn,
                                                           inTexture, in_un, in_vn,
                                                           in_coord0, in_coordU_incr, in_coordV_incr);

  // compute the normals

  const glm::vec3 du = in_coordU_incr / float(out_un);
  const glm::vec3 dv = in_coordV_incr / float(out_vn);

  for (uint iv = 0; iv < out_vn; ++iv)
  {
    for (uint iu = 0; iu < out_un; ++iu)
    {
      const glm::vec3 in_coord3D = glm::normalize(in_coord0 + du * float(iu + 0.5f) + dv * float(iv + 0.5f));

      const glm::vec3 in_coord3D_u1L = glm::normalize(in_coord0 + du * float(iu - 0.5f) + dv * float(iv + 0.5f));
      const glm::vec3 in_coord3D_u1R = glm::normalize(in_coord0 + du * float(iu + 1.5f) + dv * float(iv + 0.5f));

      const glm::vec3 in_coord3D_v1L = glm::normalize(in_coord0 + du * float(iu + 0.5f) + dv * float(iv - 0.5f));
      const glm::vec3 in_coord3D_v1R = glm::normalize(in_coord0 + du * float(iu + 0.5f) + dv * float(iv + 1.5f));

      float inv_dx, inv_dy;

      if      (iu == 0         ) inv_dx = 1.f / glm::length(in_coord3D_u1R - in_coord3D);
      else if (iu == out_un - 1) inv_dx = 1.f / glm::length(in_coord3D - in_coord3D_u1L);
      else                       inv_dx = 1.f / glm::length(in_coord3D_u1R - in_coord3D_u1L);

      if      (iv == 0         ) inv_dy = 1.f / glm::length(in_coord3D_v1R - in_coord3D);
      else if (iv == out_vn - 1) inv_dy = 1.f / glm::length(in_coord3D - in_coord3D_v1L);
      else                       inv_dy = 1.f / glm::length(in_coord3D_v1R - in_coord3D_v1L);

      inv_dx *= factor;
      inv_dy *= factor;

      const uint iuv = out_un * iv + iu;
      float dval_x, dval_y;

      if      (iu == 0         ) dval_x = tmpValues[iuv + 1] - tmpValues[iuv];
      else if (iu == out_un - 1) dval_x = tmpValues[iuv]     - tmpValues[iuv - 1];
      else                       dval_x = tmpValues[iuv + 1] - tmpValues[iuv - 1];

      if      (iv == 0         ) dval_y = tmpValues[iuv + out_un] - tmpValues[iuv];
      else if (iv == out_vn - 1) dval_y = tmpValues[iuv]          - tmpValues[iuv - out_un];
      else                       dval_y = tmpValues[iuv + out_un] - tmpValues[iuv - out_un];

      dval_x *= inv_dx;
      dval_y *= inv_dy;

      glm::vec3 outNormal;
      outNormal.x = -dval_x;
      outNormal.y = dval_y;
      outNormal.z = 1.f;

      outNormal = 0.5f + 0.5f * glm::normalize(outNormal);

      s_RBGfloat px_float;
      px_float.r = outNormal.r;
      px_float.g = outNormal.g;
      px_float.b = outNormal.b;

      samplerOut.pixelSet(iu, iv, s_RBGA8(px_float));
    }
  }
}

//-----------------------------------------------------------------------------

void textureSampler::mapNormals_toCubeMap(SDL_Surface *outTexture, SDL_Surface *inTextureEquirectangular, const float factor, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
  const uint in_un = uint(inTextureEquirectangular->w);
  const uint in_vn = uint(inTextureEquirectangular->h);

  mapNormals_toCubeMap_imp(outTexture,
                           inTextureEquirectangular, factor, in_un, in_vn,
                           in_coord0, in_coordU_incr, in_coordV_incr);
}

//-----------------------------------------------------------------------------

void textureSampler::mapNormals_toCubeMap(SDL_Surface *outTexture, TIFF *inTextureEquirectangular, const float factor, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr)
{
#ifdef TRE_WITH_LIBTIFF
  uint in_un, in_vn;
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGEWIDTH, &in_un);
  TIFFGetField(inTextureEquirectangular, TIFFTAG_IMAGELENGTH, &in_vn);

  mapNormals_toCubeMap_imp(outTexture,
                           inTextureEquirectangular, factor, in_un, in_vn,
                           in_coord0, in_coordU_incr, in_coordV_incr);
#else
  (void)outTexture;
  (void)inTextureEquirectangular;
  (void)factor;
  (void)in_coord0;
  (void)in_coordU_incr;
  (void)in_coordV_incr;
#endif
}


//=============================================================================

} // namespace
