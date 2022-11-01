#include "tre_texture.h"

namespace tre {

static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN  , "Only implemented with little-endian system.");

// ============================================================================

static inline float _errorRGB8(const glm::ivec3 &d_rgb) // perceptual error [0,1]
{
  return (d_rgb.r * d_rgb.r * 0.299f / 65025.f) + (d_rgb.g * d_rgb.g * 0.587f / 65025.f) + (d_rgb.b * d_rgb.b * 0.114f / 65025.f);
}

static inline float _errorA8(const int d_alpha)
{
  return d_alpha * d_alpha / 65025.f;
}

// ============================================================================
/**
 * Ericsson Texture Compression (ETC) is a compression scheme
 * There is an implementation available in https://github.com/Ericsson/ETCPACK
 * ref: OpenGL ES documentation
 */
namespace formatETC {

struct s_compressionReport
{
  uint  m_modeINDIVcount = 0;
  uint  m_modeDIFFcount = 0;
  float m_errorL2 = 0.f;
  float m_errorInf = 0.f;

  void print() const
  {
#ifdef TRE_PRINTS
    const uint countTotal = m_modeDIFFcount + m_modeINDIVcount;
    const float errL2 = sqrtf(m_errorL2 / countTotal);
    TRE_LOG("report of conversion to ETC2: " <<
            "modeINDI = " << m_modeINDIVcount << " (" << int(m_modeINDIVcount * 100.f / countTotal) << " %), " <<
            "modeDIFF = " << m_modeDIFFcount << " (" << int(m_modeDIFFcount * 100.f / countTotal) << " %), " <<
            "maxError = " << m_errorInf << ", L2error = " << errL2);
#endif
  }
};

static void _encodeColorZone_ETC(glm::ivec3 &colorZone0, glm::ivec3 &colorZone1, uint &bufferEncoded_HIGH)
{
  // note: RGB8 (8bits, [0-255])
  static const glm::ivec3 _diffModeMin = glm::ivec3(-4*8, -4*8, -4*8);
  static const glm::ivec3 _diffModeMax = glm::ivec3( 3*8,  3*8,  3*8);
  const glm::ivec3        rgbDiff = colorZone1 - colorZone0;
  const bool              modeDiff = glm::all(glm::lessThanEqual(_diffModeMin, rgbDiff) & glm::lessThanEqual(rgbDiff, _diffModeMax));
  if (modeDiff)
  {
    colorZone0 = (colorZone0 * 8 + 33) / 66; // round from [0-255] to {8*a + a/4, a in [0,31]} ~ {33*a/4, a in [0,31]}
    TRE_ASSERT(colorZone0.r < 0x20 && colorZone0.g < 0x20 && colorZone0.b < 0x20);
    colorZone1 = (rgbDiff / 8) & 0x07; // note: the sign bit is already set (bit3)
    bufferEncoded_HIGH = (colorZone0.r << 3) | (colorZone1.r) | (colorZone0.g << 11) | (colorZone1.g << 8) | (colorZone0.b << 19) | (colorZone1.b << 16) | 0x02000000;  // warning: little-endian
    // decode the color back to RGB8
    colorZone1 = colorZone0 + (rgbDiff / 8);
    colorZone0 = colorZone0 * 8 | colorZone0 / 4;
    colorZone1 = colorZone1 * 8 | colorZone1 / 4;
  }
  else
  {
    colorZone0 = (colorZone0 + 8) / 17; // round from [0-255] to {17*a, a in [0,15]}
    colorZone1 = (colorZone1 + 8) / 17;
    TRE_ASSERT(colorZone0.r < 0x10 && colorZone0.g < 0x10 && colorZone0.b < 0x10);
    TRE_ASSERT(colorZone1.r < 0x10 && colorZone1.g < 0x10 && colorZone1.b < 0x10);
    bufferEncoded_HIGH = (colorZone0.r << 4) | (colorZone1.r) | (colorZone0.g << 12) | (colorZone1.g << 8) | (colorZone0.b << 20) | (colorZone1.b << 16); // warning: {R4.R4|G4.G4|B4.B4|..} -little-endian-> 0x.BGR
    // decode the color back to RGB8
    colorZone0 *= 0x11;
    colorZone1 *= 0x11;
  }
}

static glm::ivec3 _encodeTable_ETC(const uint pxid, const uint tid, const int t3, const glm::ivec3 &colorBase, uint &bufferEncoded_LOW)
{
  static const int _tableETC1_val[8][2] = { {2, 8},  {5, 17},  {9, 29}, {13, 42}, {18, 60}, {24, 80},  {33, 106},  {47, 183} };
  static const int _tableETC1_sep3[8] = { 5*3, 11*3, 19*3, 27*3, 39*3, 52*3, 69*3, 105*3 };
  // encode
  TRE_ASSERT(tid < 8);
  const uint pixModSign = t3 < 0 ? 1 : 0;
  const uint pixModLarge = fabs(t3) > _tableETC1_sep3[tid] ? 1 : 0;
  TRE_ASSERT(pxid < 16);
  const uint n = pxid ^ 0x08;
  bufferEncoded_LOW |= (pixModSign << (0+n)) | (pixModLarge << (16+n)); // warning: little-endian on 4-bytes word
  // decode
  const int pxModifier = (1 - 2 * int(pixModSign)) * _tableETC1_val[tid][pixModLarge];
  return glm::clamp(colorBase + pxModifier, 0, 255);
}

static int _encodeTable_EAC(const uint pxid, const uint tid, const int alpha, const int alphaBase, const int multiplier, uint64_t &bufferEncoded_BigEndian)
{
  static const int _tableAEC_val[16][4] = { {2, 5, 9, 14}, {2, 6, 9, 12}, {1, 4, 7, 12}, {1, 3, 5, 12},
                                            {2, 5, 7, 11}, {2, 6, 8, 10}, {3, 6, 7, 10}, {2, 4, 7, 10},
                                            {1, 5, 7,  9}, {1, 4, 7,  9}, {1, 3, 7,  9}, {1, 4, 6,  9},
                                            {2, 3, 6,  9}, {0, 1, 2,  9}, {3, 5, 7,  8}, {2, 4, 6,  8} };
  // encode
  TRE_ASSERT(tid < 16);
  const int  t = alpha - alphaBase;
  const int pixModSign = t < 0 ? 0 : 1;
  const int tMag = (abs(t) + 1 - pixModSign) * 2;
  const int pixModMagn = (tMag < (_tableAEC_val[tid][0] + _tableAEC_val[tid][1]) * multiplier) ? 0 :
                         (tMag < (_tableAEC_val[tid][1] + _tableAEC_val[tid][2]) * multiplier) ? 1 :
                         (tMag < (_tableAEC_val[tid][2] + _tableAEC_val[tid][3]) * multiplier) ? 2 : 3;
  TRE_ASSERT(pxid < 16);
  bufferEncoded_BigEndian |= uint64_t(pixModSign * 4 + pixModMagn) << (45 - pxid * 3);
  // decode
  const int pxModifier = (- 1 + 2 * pixModSign) * (_tableAEC_val[tid][pixModMagn] + 1 - pixModSign);
  return glm::clamp(alphaBase + multiplier * pxModifier, 0, 255);
}

static void _rawCompress4x4_RGB8_ETC2(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t* __restrict compressed, s_compressionReport &report)
{
  TRE_ASSERT(pxByteSize >= 3);

  /* pixel layout: |  0 |  4 |  8 | 12 |
   *               |  1 |  5 |  9 | 13 |
   *               |  2 |  6 | 10 | 14 |
   *               |  3 |  7 | 11 | 15 | */

#define extR(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 0] & 0xFF)
#define extG(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 1] & 0xFF)
#define extB(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 2] & 0xFF)
#define extRGB(i, j) glm::ivec3(extR(i, j), extG(i, j), extB(i, j))

  const glm::ivec3 rgb[16] = { extRGB(0,0), extRGB(0,1), extRGB(0,2), extRGB(0,3),
                               extRGB(1,0), extRGB(1,1), extRGB(1,2), extRGB(1,3),
                               extRGB(2,0), extRGB(2,1), extRGB(2,2), extRGB(2,3),
                               extRGB(3,0), extRGB(3,1), extRGB(3,2), extRGB(3,3) };

#undef extR
#undef extG
#undef extB
#undef extRGB

  // mode ETC1 (Individual, Differential): split 4x4 by either 2x4 either 4x2.
  // Each zone has a base color, each pixel within a zone has a color clamp(base.r + modifier, base.g + modifier, base.b + modifier)
  /* zone layout: Flip0: Zone0: 0  1  2  3  4  5  6  7 <=> Z00 ( 0  1  4  5) + Z10 ( 2  3  6  7)
   *                     Zone1: 8  9 10 11 12 13 14 15 <=> Z01 ( 8  9 12 13) + Z11 (10 11 14 15)
   *              Flip1: Zone0: 0  4  8 12  1  5  9 13 <=> Z00 ( 0  1  4  5) + Z01 ( 8  9 12 13)
   *                     Zone1: 2  6 10 14  3  7 11 15 <=> Z10 ( 2  3  6  7) + Z11 (10 11 14 15) */

  // Compute the base colors

  uint       rgbEncodedFlip0;
  glm::ivec3 rgbBaseFlip0[2];

  uint       rgbEncodedFlip1;
  glm::ivec3 rgbBaseFlip1[2];

  {
    // note: the least-square method leads to simply compute the average color.
    const glm::ivec3 r00 = rgb[ 0] + rgb[ 1] + rgb[ 4] + rgb[ 5];
    const glm::ivec3 r10 = rgb[ 2] + rgb[ 3] + rgb[ 6] + rgb[ 7];
    const glm::ivec3 r01 = rgb[ 8] + rgb[ 9] + rgb[12] + rgb[13];
    const glm::ivec3 r11 = rgb[10] + rgb[11] + rgb[14] + rgb[15];

    rgbBaseFlip0[0] = (r00 + r10) / 8; // Average - Flip0.Zone0
    rgbBaseFlip0[1] = (r01 + r11) / 8; // Average - Flip0.Zone1

    rgbBaseFlip1[0] = (r00 + r01) / 8; // Average - Flip1.Zone0
    rgbBaseFlip1[1] = (r10 + r11) / 8; // Average - Flip1.Zone1

    // Encode the base colors: select mode individual (RGB4-RGB4) or mode differential (RGB5-deltaRGB3)

    _encodeColorZone_ETC(rgbBaseFlip0[0], rgbBaseFlip0[1], rgbEncodedFlip0);
    _encodeColorZone_ETC(rgbBaseFlip1[0], rgbBaseFlip1[1], rgbEncodedFlip1);
  }

  // Compute the pixel's modifiers
  // This is a mixed-integer optimisation problem :(
  // Thus, we use a brute-force method.

#define getT(px, base) (rgb[px].r - base.r) + (rgb[px].g - base.g) + (rgb[px].b - base.b)

  uint bestTablesFlip = 0; // (encoded)
  uint bestPixelModifiers = 0; // (encoded)
  float bestError = std::numeric_limits<float>::infinity();

  for (uint fb = 0; fb < 2; ++fb)
  {
    const glm::ivec3 rgbBase_00 = (fb == 0) ? rgbBaseFlip0[0] : rgbBaseFlip1[0];
    const glm::ivec3 rgbBase_10 = (fb == 0) ? rgbBaseFlip0[0] : rgbBaseFlip1[1];
    const glm::ivec3 rgbBase_01 = (fb == 0) ? rgbBaseFlip0[1] : rgbBaseFlip1[0];
    const glm::ivec3 rgbBase_11 = (fb == 0) ? rgbBaseFlip0[1] : rgbBaseFlip1[1];

    // Compute the raw pixel modifiers

    const int t3[16] = { getT( 0, rgbBase_00), getT( 1, rgbBase_00), getT( 2, rgbBase_10), getT( 3, rgbBase_10),
                         getT( 4, rgbBase_00), getT( 5, rgbBase_00), getT( 6, rgbBase_10), getT( 7, rgbBase_10),
                         getT( 8, rgbBase_01), getT( 9, rgbBase_01), getT(10, rgbBase_11), getT(11, rgbBase_11),
                         getT(12, rgbBase_01), getT(13, rgbBase_01), getT(14, rgbBase_11), getT(15, rgbBase_11) }; // in fact, we have 3*t

    // Reduce the nbr of tables to evaluate (?)

    // Loop over tables (for each zone)

    glm::ivec3 rgbDelta;
    uint bestTableId_fb[2] = {0, 0};
    uint bestPixelModifiers_fb[2] = {0, 0};
    float bestError_fp[2] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };

    for (uint tid = 0; tid < 8; ++tid)
    {
      uint pixelModifiers_00 = 0; float err_00;
      rgbDelta = _encodeTable_ETC( 0, tid, t3[ 0], rgbBase_00, pixelModifiers_00) - rgb[ 0]; err_00  = _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 1, tid, t3[ 1], rgbBase_00, pixelModifiers_00) - rgb[ 1]; err_00 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 4, tid, t3[ 4], rgbBase_00, pixelModifiers_00) - rgb[ 4]; err_00 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 5, tid, t3[ 5], rgbBase_00, pixelModifiers_00) - rgb[ 5]; err_00 += _errorRGB8(rgbDelta);

      uint pixelModifiers_10 = 0; float err_10;
      rgbDelta = _encodeTable_ETC( 2, tid, t3[ 2], rgbBase_01, pixelModifiers_10) - rgb[ 2]; err_10  = _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 3, tid, t3[ 3], rgbBase_01, pixelModifiers_10) - rgb[ 3]; err_10 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 6, tid, t3[ 6], rgbBase_01, pixelModifiers_10) - rgb[ 6]; err_10 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 7, tid, t3[ 7], rgbBase_01, pixelModifiers_10) - rgb[ 7]; err_10 += _errorRGB8(rgbDelta);

      uint pixelModifiers_01 = 0; float err_01;
      rgbDelta = _encodeTable_ETC( 8, tid, t3[ 8], rgbBase_10, pixelModifiers_01) - rgb[ 8]; err_01  = _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC( 9, tid, t3[ 9], rgbBase_10, pixelModifiers_01) - rgb[ 9]; err_01 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC(12, tid, t3[12], rgbBase_10, pixelModifiers_01) - rgb[12]; err_01 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC(13, tid, t3[13], rgbBase_10, pixelModifiers_01) - rgb[13]; err_01 += _errorRGB8(rgbDelta);

      uint pixelModifiers_11 = 0; float err_11;
      rgbDelta = _encodeTable_ETC(10, tid, t3[10], rgbBase_11, pixelModifiers_11) - rgb[10]; err_11  = _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC(11, tid, t3[11], rgbBase_11, pixelModifiers_11) - rgb[11]; err_11 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC(14, tid, t3[14], rgbBase_11, pixelModifiers_11) - rgb[14]; err_11 += _errorRGB8(rgbDelta);
      rgbDelta = _encodeTable_ETC(15, tid, t3[15], rgbBase_11, pixelModifiers_11) - rgb[15]; err_11 += _errorRGB8(rgbDelta);

      const float errZ0 = (fb == 0) ? err_00 + err_10 : err_00 + err_01;
      if (errZ0 < bestError_fp[0])
      {
        bestError_fp[0] = errZ0;
        bestTableId_fb[0] = tid;
        bestPixelModifiers_fb[0] = (fb == 0) ? pixelModifiers_00 | pixelModifiers_10 : pixelModifiers_00 | pixelModifiers_01;
      }

      const float errZ1 = (fb == 0) ? err_01 + err_11 : err_10 + err_11;
      if (errZ1 < bestError_fp[1])
      {
        bestError_fp[1] = errZ1;
        bestTableId_fb[1] = tid;
        bestPixelModifiers_fb[1] = (fb == 0) ? pixelModifiers_01 | pixelModifiers_11 : pixelModifiers_10 | pixelModifiers_11;
      }

      TRE_ASSERT((bestPixelModifiers_fb[0] & bestPixelModifiers_fb[1]) == 0);
    }

    const float bestError_tmp = glm::max(bestError_fp[0], bestError_fp[1]);
    if (bestError_tmp < bestError)
    {
      bestError = bestError_tmp;
      bestTablesFlip = (bestTableId_fb[0] << (24+5)) | (bestTableId_fb[1] << (24+2)) | (fb << (24+0));
      bestPixelModifiers = bestPixelModifiers_fb[0] | bestPixelModifiers_fb[1];
    }
  }

#undef getT

  TRE_ASSERT((rgbEncodedFlip0 & bestTablesFlip) == 0);
  TRE_ASSERT((rgbEncodedFlip1 & bestTablesFlip) == 0);

  *reinterpret_cast<uint*>(&compressed[0]) = (((bestTablesFlip & 0x01000000) == 0) ? rgbEncodedFlip0 : rgbEncodedFlip1) | bestTablesFlip;
  *reinterpret_cast<uint*>(&compressed[4]) = bestPixelModifiers;

  {
    if (compressed[3] & 0x02) ++report.m_modeDIFFcount;
    else                      ++report.m_modeINDIVcount;
    bestError /= 8.f;
    report.m_errorL2 += bestError;
    report.m_errorInf = glm::max(report.m_errorInf, sqrtf(bestError));
  }
}

static void _rawCompress4x4_ALPHA_EAC(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t* __restrict compressed, s_compressionReport &report)
{
  TRE_ASSERT(pxByteSize >= 4);

  /* pixel layout: |  0 |  4 |  8 | 12 |
   *               |  1 |  5 |  9 | 13 |
   *               |  2 |  6 | 10 | 14 |
   *               |  3 |  7 | 11 | 15 | */

#define extA(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 3] & 0xFF)

  const int alpha[16] = { extA(0,0), extA(0,1), extA(0,2), extA(0,3),
                          extA(1,0), extA(1,1), extA(1,2), extA(1,3),
                          extA(2,0), extA(2,1), extA(2,2), extA(2,3),
                          extA(3,0), extA(3,1), extA(3,2), extA(3,3) };

#undef extA

  // mode EAC: each pixel has an alpha clamp(base + multiplier * modifier), where the modifier is definied per pixel.

  const int average = (alpha[ 0] + alpha[ 1] + alpha[ 2] + alpha[ 3] + alpha[ 4] + alpha[ 5] + alpha[ 6] + alpha[ 7] +
                       alpha[ 8] + alpha[ 9] + alpha[10] + alpha[11] + alpha[12] + alpha[13] + alpha[14] + alpha[15] ) / 16;

  // Loop over tables

  uint64_t bestEncoded_BigEndian = 0;
  float    bestError = std::numeric_limits<float>::infinity();

  for (uint tid = 0; tid < 16; ++tid)
  {
    // Get the multiplier
    int multiplier = 1;
    {
      int diffMax = 0;
      for (std::size_t i = 0; i < 16; ++i)
      {
        const int diff_i = abs(alpha[i] - average);
        if (diff_i > diffMax) diffMax = diff_i;
      }
      multiplier = glm::clamp(diffMax / 12, 1, 15);
    }
    TRE_ASSERT(multiplier > 0 && multiplier < 16);

    // Shift the average
    int localAverage = average;

    TRE_ASSERT(localAverage >= 0 && localAverage <= 0xFF);

    // Compute the error

    uint64_t localEncoded_BigEndian = (uint64_t(localAverage & 0xFF) << 56) | (uint64_t(multiplier & 0xF) << 52) | (uint64_t(tid & 0xF) << 48);
    float    localError = 0.f;
    int      alphaDiff;

    alphaDiff = _encodeTable_EAC( 0, tid, alpha[ 0], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 0]; localError  = _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 1, tid, alpha[ 1], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 1]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 2, tid, alpha[ 2], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 2]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 3, tid, alpha[ 3], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 3]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 4, tid, alpha[ 4], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 4]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 5, tid, alpha[ 5], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 5]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 6, tid, alpha[ 6], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 6]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 7, tid, alpha[ 7], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 7]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 8, tid, alpha[ 8], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 8]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC( 9, tid, alpha[ 9], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 9]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(10, tid, alpha[10], localAverage, multiplier, localEncoded_BigEndian) - alpha[10]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(11, tid, alpha[11], localAverage, multiplier, localEncoded_BigEndian) - alpha[11]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(12, tid, alpha[12], localAverage, multiplier, localEncoded_BigEndian) - alpha[12]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(13, tid, alpha[13], localAverage, multiplier, localEncoded_BigEndian) - alpha[13]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(14, tid, alpha[14], localAverage, multiplier, localEncoded_BigEndian) - alpha[14]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodeTable_EAC(15, tid, alpha[15], localAverage, multiplier, localEncoded_BigEndian) - alpha[15]; localError += _errorA8(alphaDiff);

    if (localError < bestError)
    {
      bestEncoded_BigEndian = localEncoded_BigEndian;
      bestError = localError;
    }
  }

  compressed[ 0] = (bestEncoded_BigEndian >> 56) & 0xFF;
  compressed[ 1] = (bestEncoded_BigEndian >> 48) & 0xFF;
  compressed[ 2] = (bestEncoded_BigEndian >> 40) & 0xFF;
  compressed[ 3] = (bestEncoded_BigEndian >> 32) & 0xFF;
  compressed[ 4] = (bestEncoded_BigEndian >> 24) & 0xFF;
  compressed[ 5] = (bestEncoded_BigEndian >> 16) & 0xFF;
  compressed[ 6] = (bestEncoded_BigEndian >>  8) & 0xFF;
  compressed[ 7] = (bestEncoded_BigEndian      ) & 0xFF;
}

}

// ============================================================================
/**
 * S3 Texture Compression (S3TC) is a compression scheme for 3 or 4 color channel textures.
 * Called also DXT
 * ref: https://www.khronos.org/opengl/wiki/S3_Texture_Compression
 */
namespace formatS3TC {

struct s_compressionReport
{
  uint  m_modeHIGHERcount = 0;
  uint  m_modeLOWERcount = 0;
  float m_errorL2 = 0.f;
  float m_errorInf = 0.f;

  void print() const
  {
#ifdef TRE_PRINTS
    const uint countTotal = m_modeLOWERcount + m_modeHIGHERcount;
    const float errL2 = sqrtf(m_errorL2 / countTotal);
    TRE_LOG("report of conversion to S3TC: " <<
            "modeLOWER = " << m_modeLOWERcount << " (" << int(m_modeLOWERcount * 100.f / countTotal) << " %), " <<
            "modeHIGHER = " << m_modeHIGHERcount << " (" << int(m_modeHIGHERcount * 100.f / countTotal) << " %), " <<
            "maxError = " << m_errorInf << ", L2error = " << errL2);
#endif
  }
};

static bool _compareColor_S3TC_0greaterthan1(const glm::ivec3 &color0, const glm::ivec3 &color1)
{
  const int r0 = (color0.r >> 3);
  const int g0 = (color0.g >> 2);
  const int b0 = (color0.b >> 3);
  const int r1 = (color1.r >> 3);
  const int g1 = (color1.g >> 2);
  const int b1 = (color1.b >> 3);
  return (r0 > r1) || (r0 == r1 && g0 > g1) || (r0 == r1 && g0 == g1 && b0 > b1);
}

static void _encodeColorBase_S3TC(glm::ivec3 &color0, glm::ivec3 &color1, uint &bufferEncoded_HIGH)
{
  // from RGB8 (8bits, [0-255]) to R5-G6-B5

  color0.r = (color0.r >> 3);
  color0.g = (color0.g >> 2);
  color0.b = (color0.b >> 3);

  color1.r = (color1.r >> 3);
  color1.g = (color1.g >> 2);
  color1.b = (color1.b >> 3);

  bufferEncoded_HIGH = (color0.b) | (color0.g << 5) | (color0.r << 11) | (color1.b << 16) | (color1.g << 21) | (color1.r << 27);

  color0.r = (color0.r << 3);
  color0.g = (color0.g << 2);
  color0.b = (color0.b << 3);

  color1.r = (color1.r << 3);
  color1.g = (color1.g << 2);
  color1.b = (color1.b << 3);
}

static void _rawCompress4x4_RGB8_S3TC(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t* __restrict compressed, s_compressionReport &report)
{
  TRE_ASSERT(pxByteSize >= 3);

#define extR(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 0] & 0xFF)
#define extG(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 1] & 0xFF)
#define extB(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 2] & 0xFF)
#define extRGB(i, j) glm::ivec3(extR(i, j), extG(i, j), extB(i, j))

  const glm::ivec3 rgb[16] = { extRGB(0,0), extRGB(1,0), extRGB(2,0), extRGB(3,0),
                               extRGB(0,1), extRGB(1,1), extRGB(2,1), extRGB(3,1),
                               extRGB(0,2), extRGB(1,2), extRGB(2,2), extRGB(3,2),
                               extRGB(0,3), extRGB(1,3), extRGB(2,3), extRGB(3,3) };

#undef extR
#undef extG
#undef extB
#undef extRGB

  // mode BC1: 2 blocks: 2 colors (RGB0,RGB1) + code table with 2 bits per pixel
  /* each pixel color is either:
   *   RGB0,              if color0 > color1 and code == 0
   *   RGB1,              if color0 > color1 and code == 1
   *   (2*RGB0+RGB1)/3,   if color0 > color1 and code == 2
   *   (RGB0+2*RGB1)/3,   if color0 > color1 and code == 3
   *
   *   RGB0,              if color0 <= color1 and code == 0
   *   RGB1,              if color0 <= color1 and code == 1
   *   (RGB0+RGB1)/2,     if color0 <= color1 and code == 2
   *   BLACK,             if color0 <= color1 and code == 3 */

  // Note1: all arthimetics are per-component. But if a component is 'BLACK', then the pixel is entirely black.
  // Note2: with DXT1 with alpha, all pixels have alpha = 1.f, except with 'BLACK'.
  // Note3: reinterpret color0 and color1 as raw unsigned-interger to compare both colors

  {
    // step: 3d-linear-regression: find colorOffset, colorSlope, {params} such as normL2( colorOffset + colorSlope * {params} - {colors} ) is minimal
    // -> find the best component (compute the 3 matrix by components, and select the best one
    glm::vec3 colorOffset;
    glm::vec3 colorSlope;
    {
      float Argb[3 * 3];
      memset(&Argb[0], 0, sizeof(float) * 3 * 3);
      for (std::size_t i = 0; i < 16; ++i)
      {
        // if black, exclude the pixel !
        Argb[0 * 3 + 0] += 1.f;
        Argb[0 * 3 + 1] += rgb[i].r;
        Argb[0 * 3 + 2] += rgb[i].r * rgb[i].r;
        Argb[1 * 3 + 0] += 1.f;
        Argb[1 * 3 + 1] += rgb[i].g;
        Argb[1 * 3 + 2] += rgb[i].g * rgb[i].g;
        Argb[2 * 3 + 0] += 1.f;
        Argb[2 * 3 + 1] += rgb[i].b;
        Argb[2 * 3 + 2] += rgb[i].b * rgb[i].b;
      }
      const float Ar_Det = Argb[3 * 0 + 0] * Argb[3 * 0 + 2] - Argb[3 * 0 + 1] * Argb[3 * 0 + 1];
      const float Ag_Det = Argb[3 * 1 + 0] * Argb[3 * 1 + 2] - Argb[3 * 1 + 1] * Argb[3 * 1 + 1];
      const float Ab_Det = Argb[3 * 2 + 0] * Argb[3 * 2 + 2] - Argb[3 * 2 + 1] * Argb[3 * 2 + 1];
      glm::length_t compRX = 0;
      glm::length_t compRY = 0;
      glm::length_t compRZ = 0;
      {
        float detMax = 0.f;
        if (fabsf(Ar_Det) > detMax) { detMax = fabsf(Ar_Det); compRX = 0; compRY = 1; compRZ = 2; }
        if (fabsf(Ag_Det) > detMax) { detMax = fabsf(Ag_Det); compRX = 1; compRY = 2; compRZ = 0; }
        if (fabsf(Ab_Det) > detMax) { detMax = fabsf(Ab_Det); compRX = 2; compRY = 0; compRZ = 1; }
        if (detMax < 1.e-3f) // uni-color
        {
          glm::ivec3 color0 = rgb[0];
          glm::ivec3 color1 = rgb[0];
          _encodeColorBase_S3TC(color0, color1, *reinterpret_cast<uint*>(compressed));
          *reinterpret_cast<uint*>(&compressed[4]) = 0u;
          // (report)
          report.m_modeLOWERcount += 1;
          return;
        }
      }
      // -> perform the two linear-regression
      float BY[2] = {0.f, 0.f};
      float BZ[2] = {0.f, 0.f};
      for (std::size_t i = 0; i < 16; ++i)
      {
        // if black, exclude the pixel !
        BY[0] += rgb[i][compRY];
        BY[1] += rgb[i][compRY] * rgb[i][compRX];
        BZ[0] += rgb[i][compRZ];
        BZ[1] += rgb[i][compRZ] * rgb[i][compRX];
      }
      const float invDetA = 1.f / (Argb[3 * compRX + 0] * Argb[3 * compRX + 2] - Argb[3 * compRX + 1] * Argb[3 * compRX + 1]);
      colorOffset[compRX] = 0.f;
      colorOffset[compRY] = (BY[0] * Argb[3 * compRX + 2] - BY[1] * Argb[3 * compRX + 1]) * invDetA;
      colorOffset[compRZ] = (BZ[0] * Argb[3 * compRX + 2] - BZ[1] * Argb[3 * compRX + 1]) * invDetA;
      colorSlope[compRX] = 1.f;
      colorSlope[compRY] = (BY[1] * Argb[3 * compRX + 0] - BY[0] * Argb[3 * compRX + 1]) * invDetA;
      colorSlope[compRZ] = (BZ[1] * Argb[3 * compRX + 0] - BZ[0] * Argb[3 * compRX + 1]) * invDetA;
      colorSlope = glm::normalize(colorSlope);
    }
    // -> compute the {params}
    float params[16];
    for (std::size_t i = 0; i < 16; ++i)
      params[i] = glm::dot(glm::vec3(rgb[i]) - colorOffset, colorSlope);

    // step: find the param0 (color0) and param1 (color1) that minimizes the global error
    // For now, we only consider the mode 'color0 > color1' without BLACK. TODO !
    // There are 4 zones: - zone0: close to 'param0'
    //                    - zone1: close to '2/3 param0 + 1/3 param1'
    //                    - zone2: close to '1/3 param0 + 2/3 param1'
    //                    - zone3: close to 'param1'
    // -> first guess of param0 and param1
    float param0 = params[0], param1 = params[0];
    for (std::size_t i = 1; i < 16; ++i)
    {
      if (param0 > params[i]) param0 = params[i];
      if (param1 < params[i]) param1 = params[i];
    }
    // -> iteration: resolve derr/{dparam0,dparam1} = 0
    {
      const float pZone_01 = param0 * 5.f/6.f + param1 * 1.f/6.f;
      const float pZone_12 = param0 * 3.f/6.f + param1 * 3.f/6.f;
      const float pZone_23 = param0 * 1.f/6.f + param1 * 5.f/6.f;
      unsigned countPerZone[4] = {0, 0, 0, 0};
      float    sumPerZone[4] = {0.f, 0.f, 0.f, 0.f};
      for (std::size_t i = 0; i < 16; ++i)
      {
        if      (params[i] < pZone_01) { ++countPerZone[0]; sumPerZone[0] += params[i]; }
        else if (params[i] < pZone_12) { ++countPerZone[1]; sumPerZone[1] += params[i]; }
        else if (params[i] < pZone_23) { ++countPerZone[2]; sumPerZone[2] += params[i]; }
        else                           { ++countPerZone[3]; sumPerZone[3] += params[i]; }
      }
      const float A2[2 * 2] = { (9.f * countPerZone[0] + 4.f * countPerZone[1] + countPerZone[2]), (2.f * countPerZone[1] + 2.f * countPerZone[2]),
                                (2.f * countPerZone[2] + 2.f * countPerZone[1]), (9.f * countPerZone[3] + 4.f * countPerZone[2] + countPerZone[1]) };
      const float B2[2] = { 9.f * sumPerZone[0] + 6.f * sumPerZone[1] + 3.f * sumPerZone[2],
                            9.f * sumPerZone[3] + 6.f * sumPerZone[2] + 3.f * sumPerZone[1] };

      const float detA = A2[0 * 2 + 0] * A2[1 * 2 + 1] - A2[1 * 2 + 0] * A2[0 * 2 + 1];
      const float invDetA = fabs(detA) > 1.e-3f ? 1.f / detA : 0.f;

      const float param0_new = (B2[0] * A2[1 * 2 + 1] - B2[1] * A2[0 * 2 + 1]) * invDetA;
      const float param1_new = (B2[1] * A2[0 * 2 + 0] - B2[0] * A2[1 * 2 + 0]) * invDetA;
      // -> compute the error with the raw params and the new params
      float errParamGuess = 0.f;
      {
        const float p0 = param0;
        const float p1 = param1;
        const float pZ_01 = p0 * 5.f/6.f + p1 * 1.f/6.f;
        const float pZ_12 = p0 * 3.f/6.f + p1 * 3.f/6.f;
        const float pZ_23 = p0 * 1.f/6.f + p1 * 5.f/6.f;
        const float pM_0 = p0 * 3.f/3.f + p1 * 0.f/3.f;
        const float pM_1 = p0 * 2.f/3.f + p1 * 1.f/3.f;
        const float pM_2 = p0 * 1.f/3.f + p1 * 2.f/3.f;
        const float pM_3 = p0 * 0.f/3.f + p1 * 3.f/3.f;
        for (std::size_t i = 0; i < 16; ++i)
        {
          if      (params[i] < pZ_01) { errParamGuess += (pM_0 - params[i])*(pM_0 - params[i]); }
          else if (params[i] < pZ_12) { errParamGuess += (pM_1 - params[i])*(pM_1 - params[i]); }
          else if (params[i] < pZ_23) { errParamGuess += (pM_2 - params[i])*(pM_2 - params[i]); }
          else                        { errParamGuess += (pM_3 - params[i])*(pM_3 - params[i]); }
        }
      }
      float errParamNew_NZ = 0.f;
      {
        const float p0 = param0_new;
        const float p1 = param1_new;
        const float pZ_01 = p0 * 5.f/6.f + p1 * 1.f/6.f;
        const float pZ_12 = p0 * 3.f/6.f + p1 * 3.f/6.f;
        const float pZ_23 = p0 * 1.f/6.f + p1 * 5.f/6.f;
        const float pM_0 = p0 * 3.f/3.f + p1 * 0.f/3.f;
        const float pM_1 = p0 * 2.f/3.f + p1 * 1.f/3.f;
        const float pM_2 = p0 * 1.f/3.f + p1 * 2.f/3.f;
        const float pM_3 = p0 * 0.f/3.f + p1 * 3.f/3.f;
        for (std::size_t i = 0; i < 16; ++i)
        {
          if      (params[i] < pZ_01) { errParamNew_NZ += (pM_0 - params[i])*(pM_0 - params[i]); }
          else if (params[i] < pZ_12) { errParamNew_NZ += (pM_1 - params[i])*(pM_1 - params[i]); }
          else if (params[i] < pZ_23) { errParamNew_NZ += (pM_2 - params[i])*(pM_2 - params[i]); }
          else                        { errParamNew_NZ += (pM_3 - params[i])*(pM_3 - params[i]); }
        }
      }
      // -> choose the best params
      if (errParamNew_NZ < errParamGuess)
      {
        param0 = param0_new;
        param1 = param1_new;
      }
    }
    // -> compute the color0 and color1
    glm::ivec3 color0 = glm::clamp(glm::ivec3(colorOffset + param0 * colorSlope), glm::ivec3(0), glm::ivec3(255));
    glm::ivec3 color1 = glm::clamp(glm::ivec3(colorOffset + param1 * colorSlope), glm::ivec3(0), glm::ivec3(255));
    if (!_compareColor_S3TC_0greaterthan1(color0, color1)) std::swap(color0, color1);
    _encodeColorBase_S3TC(color0, color1, *reinterpret_cast<uint*>(compressed));
    // -> compute table
    const glm::ivec3 colorT2 = (2 * color0 + color1) / 3;
    const glm::ivec3 colorT3 = (color0 + 2 * color1) / 3;
    uint table = 0;
    for (std::size_t i = 0; i < 16; ++i)
    {
      const glm::ivec3 errV0 = rgb[i] - color0;
      const glm::ivec3 errV1 = rgb[i] - color1;
      const glm::ivec3 errV2 = rgb[i] - colorT2;
      const glm::ivec3 errV3 = rgb[i] - colorT3;
      const int errT0 = errV0.x * errV0.x + errV0.y * errV0.y + errV0.z * errV0.z; // glm::dot only accepts floats
      const int errT1 = errV1.x * errV1.x + errV1.y * errV1.y + errV1.z * errV1.z;
      const int errT2 = errV2.x * errV2.x + errV2.y * errV2.y + errV2.z * errV2.z;
      const int errT3 = errV3.x * errV3.x + errV3.y * errV3.y + errV3.z * errV3.z;
      const int errMin = std::min(std::min(errT0, errT1), std::min(errT2, errT3));
      if      (errMin == errT1) table |= (0x1 << (i * 2));
      else if (errMin == errT2) table |= (0x2 << (i * 2));
      else if (errMin == errT3) table |= (0x3 << (i * 2));
    }
    // -> END
    *reinterpret_cast<uint*>(&compressed[4]) = table;
    // (report)
    report.m_modeHIGHERcount += 1;
  }
}

static void _rawCompress4x4_ALPHA_DXT3(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t* __restrict compressed, s_compressionReport &report)
{
  TRE_ASSERT(pxByteSize >= 4);
  (void)report;

  // Each pixel has an alpha encoded on 4 bits.

#define extA(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 3] & 0xFF)

  const int alpha[16] = { extA(0,0), extA(1,0), extA(2,0), extA(3,0),
                          extA(0,1), extA(1,1), extA(2,1), extA(3,1),
                          extA(0,2), extA(1,2), extA(2,2), extA(3,2),
                          extA(0,3), extA(1,3), extA(2,3), extA(3,3) };

#undef extA

  const int alpha4[16] = { alpha[ 0] >> 4, alpha[ 1] >> 4, alpha[ 2] >> 4, alpha[ 3] >> 4,
                           alpha[ 4] >> 4, alpha[ 5] >> 4, alpha[ 6] >> 4, alpha[ 7] >> 4,
                           alpha[ 8] >> 4, alpha[ 9] >> 4, alpha[10] >> 4, alpha[11] >> 4,
                           alpha[12] >> 4, alpha[13] >> 4, alpha[14] >> 4, alpha[15] >> 4 };

  compressed[0] = uint8_t((alpha4[ 0]) | (alpha4[ 1] << 4));
  compressed[1] = uint8_t((alpha4[ 2]) | (alpha4[ 3] << 4));
  compressed[2] = uint8_t((alpha4[ 4]) | (alpha4[ 5] << 4));
  compressed[3] = uint8_t((alpha4[ 6]) | (alpha4[ 7] << 4));
  compressed[4] = uint8_t((alpha4[ 8]) | (alpha4[ 9] << 4));
  compressed[5] = uint8_t((alpha4[10]) | (alpha4[11] << 4));
  compressed[6] = uint8_t((alpha4[12]) | (alpha4[13] << 4));
  compressed[7] = uint8_t((alpha4[14]) | (alpha4[15] << 4));
}

}

// ============================================================================

uint texture::_rawCompress(const s_SurfaceTemp &surf, GLenum targetFormat)
{
  const uint8_t *inPixels = const_cast<const uint8_t*>(surf.pixels);
  uint8_t       *outBuffer = surf.pixels;
  static_assert (sizeof(uint64_t) == 8, "bad size of uint64_t");

  if (targetFormat == GL_COMPRESSED_RGB_S3TC_DXT1_EXT)
  {
    formatS3TC::s_compressionReport report;
    for (uint ih = 0; ih < surf.h; ih += 4)
    {
      for (uint iw = 0; iw < surf.w; iw += 4)
      {
        formatS3TC::_rawCompress4x4_RGB8_S3TC(inPixels + surf.pitch * ih + surf.pxByteSize * iw, surf.pxByteSize, surf.pitch, outBuffer, report);
        outBuffer += 8;
      }
    }
    report.print();
  }
  else if (targetFormat == GL_COMPRESSED_RGBA_S3TC_DXT3_EXT)
  {
    formatS3TC::s_compressionReport report;
    TRE_ASSERT(surf.pxByteSize == 4);
    for (uint ih = 0; ih < surf.h; ih += 4)
    {
      for (uint iw = 0; iw < surf.w; iw += 4)
      {
        uint64_t out1 = 0;
        formatS3TC::_rawCompress4x4_ALPHA_DXT3(inPixels + surf.pitch * ih + 4 * iw, 4, surf.pitch, reinterpret_cast<uint8_t*>(&out1), report);
        uint64_t out2 = 0;
        formatS3TC::_rawCompress4x4_RGB8_S3TC(inPixels + surf.pitch * ih + 4 * iw, 4, surf.pitch, reinterpret_cast<uint8_t*>(&out2), report);
        memcpy(outBuffer, &out1, 8);
        outBuffer += 8;
        memcpy(outBuffer, &out2, 8);
        outBuffer += 8;
      }
    }
    report.print();
  }

#ifdef TRE_OPENGL_ES

  else if (targetFormat == GL_COMPRESSED_RGB8_ETC2)
  {
    formatETC::s_compressionReport report;
    for (uint ih = 0; ih < surf.h; ih += 4)
    {
      for (uint iw = 0; iw < surf.w; iw += 4)
      {
        formatETC::_rawCompress4x4_RGB8_ETC2(inPixels + surf.pitch * ih + surf.pxByteSize * iw, surf.pxByteSize, surf.pitch, outBuffer, report);
        outBuffer += 8;
      }
    }
    report.print();
  }
  else if (targetFormat == GL_COMPRESSED_RGBA8_ETC2_EAC)
  {
    formatETC::s_compressionReport report;
    TRE_ASSERT(surf.pxByteSize == 4);
    for (uint ih = 0; ih < surf.h; ih += 4)
    {
      for (uint iw = 0; iw < surf.w; iw += 4)
      {
        uint64_t out1 = 0;
        formatETC::_rawCompress4x4_ALPHA_EAC(inPixels + surf.pitch * ih + 4 * iw, 4, surf.pitch, reinterpret_cast<uint8_t*>(&out1), report);
        uint64_t out2 = 0;
        formatETC::_rawCompress4x4_RGB8_ETC2(inPixels + surf.pitch * ih + 4 * iw, 4, surf.pitch, reinterpret_cast<uint8_t*>(&out2), report);
        memcpy(outBuffer, &out1, 8);
        outBuffer += 8;
        memcpy(outBuffer, &out2, 8);
        outBuffer += 8;
      }
    }
    report.print();
  }

#endif // TRE_OPENGL_ES

  else
  {
    TRE_FATAL("_rawCompress: format not supported");
  }

  return static_cast<uint>(outBuffer - inPixels);
}

//-----------------------------------------------------------------------------

} // namespace
