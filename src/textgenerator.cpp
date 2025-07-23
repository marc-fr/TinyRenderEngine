#include "tre_textgenerator.h"

#include "tre_model.h"
#include "tre_font.h"

namespace tre {

namespace textgenerator {

// ============================================================================

void generate(const s_textInfo &info, modelRaw2D *outMesh, unsigned outPartId, unsigned outOffset, s_textInfoOut *outInfo)
{
  TRE_ASSERT(info.m_font != nullptr);
  TRE_ASSERT(info.m_text != nullptr);

  const uint maxVertexCount = geometry_VertexCount(info.m_text);
  TRE_ASSERT(outMesh == nullptr || outOffset + maxVertexCount <= outMesh->partInfo(outPartId).m_size)

  const bool isBoxValid = (info.m_zone.x < info.m_zone.z) && (info.m_zone.y < info.m_zone.w);
  const bool isPixelSizeValid = (info.m_pixelSize.x > 0.f) && (info.m_pixelSize.y > 0.f);

  const uint fontsizePixel = isPixelSizeValid ? uint(info.m_fontsize / info.m_pixelSize.y) : 512 /*big value*/;
  const font::s_fontMap &fontMap = info.m_font->get_bestFontMap(fontsizePixel);
  const float fontsize = info.m_fontsize; // pixel-snap ? info.m_pixelSize.y * fontsizePixel

  const float scaleH = fontsize / fontMap.m_hline;
  const glm::vec2 scale = glm::vec2(info.m_font->get_texture().m_w * scaleH / info.m_font->get_texture().m_h, scaleH);

  glm::vec2 maxboxsize = glm::vec2(0.f);

  maxboxsize.x = 0.f;
  maxboxsize.y = fontsize;
  float posx = info.m_zone.x;
  float posy = info.m_zone.w;
  uint vidx = outOffset;
  for (std::size_t ich = 0, iLen = std::strlen(info.m_text); ich < iLen; ++ich)
  {
    unsigned char idchar = info.m_text[ich];

    if (idchar == '\n')
    {
      posx = info.m_zone.x;
      posy -= fontsize;
      maxboxsize.y += fontsize;
      continue;
    }
    else if (idchar == '\t')
    {
      posx += fontsize * 3.f;
      continue;
    }
    else if (idchar == 0)
    {
      continue;
    }
    else if (idchar >= 128) // non-ASCII
    {
      TRE_ASSERT((idchar & 0x40) != 0 && (idchar & 0x20) == 0); // UFT-8 encoding, only from U+0080 to U+07FF
      TRE_ASSERT(ich + 1 < iLen); // truncated character when decoding UFT-8 string
      TRE_ASSERT((idchar & 0x3C) == 0); // only the latin-1 extension is implemented in the font.
      idchar = ((idchar & 0x03) << 6) | (info.m_text[++ich] & 0x3F);
      if (idchar < 128)
      {
        TRE_LOG("textgenerator: invalid character when decoding UFT-8 string (str=\"" << info.m_text << "\" buffer-index=" << ich - 1);
        continue;
      }
    }

    const font::s_charInfo & charMap = fontMap.m_charMap[idchar];

    glm::vec4 quadPos( posx + scale.x * charMap.xoffs,
                       posy - scale.y * charMap.yoffs - scale.y * (charMap.cby-charMap.cay),
                       posx + scale.x * charMap.xoffs + scale.x * (charMap.cbx-charMap.cax),
                       posy - scale.y * charMap.yoffs );
    glm::vec4 quadUV( charMap.cax, charMap.cay,
                      charMap.cbx, charMap.cby );

    const float sizex = quadPos.z - info.m_zone.x;
    if (sizex > maxboxsize.x) maxboxsize.x = sizex;

    const float posx_old = posx;

    posx += scale.x * charMap.xadvance;
    if (info.m_boxExtendedToNextChar && posx - info.m_zone.x > maxboxsize.x) maxboxsize.x = posx - info.m_zone.x;

    if (isBoxValid && (posx_old > info.m_zone.z || posy < info.m_zone.y)) continue; // out-of-bound

    if (isBoxValid && quadPos.y < info.m_zone.y)
    {
      quadUV.w = quadUV.y - (quadUV.w - quadUV.y) * (info.m_zone.y - quadPos.w) / (quadPos.w - quadPos.y);
      quadPos.y = info.m_zone.y;
    }
    if (isBoxValid && quadPos.z > info.m_zone.z)
    {
      quadUV.z = quadUV.x + (quadUV.z - quadUV.x) * (info.m_zone.z - quadPos.x) / (quadPos.z - quadPos.x);
      quadPos.z = info.m_zone.z;
    }
    if (outMesh != nullptr)
    {
      const glm::vec4 quadUVflip = glm::vec4(quadUV.x, quadUV.w, quadUV.z, quadUV.y); // flip Y
      outMesh->fillDataRectangle(outPartId, vidx, quadPos, info.m_color, quadUVflip);
    }
    // end
    vidx += 6;
  }

  if (outMesh != nullptr)
  {
    for (uint stop = outOffset + maxVertexCount; vidx < stop; ++vidx)
      outMesh->fillDataRectangle(outPartId, vidx, glm::vec4(0.f), glm::vec4(0.f), glm::vec4(0.f));
  }

  if (outInfo != nullptr)
  {
    outInfo->m_maxboxsize = maxboxsize;
    outInfo->m_choosenFontSizePixel = fontMap.m_fsize;
  }
}

// ============================================================================

} // namespace

} // namespace
