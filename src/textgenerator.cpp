#include "textgenerator.h"

#include "model.h"
#include "font.h"

namespace tre {

const s_partInfo &textgenerator::s_textInfo::modelPartInfo() const
{
  TRE_ASSERT(m_parent != nullptr);
  TRE_ASSERT(m_parentPartId < m_parent->m_partInfo.size());
  return m_parent->m_partInfo[m_parentPartId];
}

const s_modelDataLayout &textgenerator::s_textInfo::modelLayout() const
{
  TRE_ASSERT(m_parent != nullptr);
  return m_parent->m_layout;
}

// modeldyn2DText methods =====================================================

uint textgenerator::createTexts(uint tcount, modelRaw2D *parent)
{
  TRE_ASSERT(tcount > 0);
  TRE_ASSERT(parent != nullptr);

  const uint tcountOld = m_textInfo.size();
  m_textInfo.resize(tcountOld + tcount);

  for (uint tid = tcountOld; tid < tcountOld + tcount; ++tid)
  {
    s_textInfo & curText = m_textInfo[tid];
    curText.m_parent = parent;
    curText.m_parentPartId = curText.m_parent->createPart(0);
  }

  return tcountOld;
}

void textgenerator::updateText_pos(uint id, float xfirst, float yline)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  TRE_ASSERT(info.m_parent != nullptr);

  const glm::vec2 move = glm::vec2(xfirst - info.m_zone.x, yline - info.m_zone.w);
  info.m_zone += glm::vec4(move, move);

  if (info.m_font != nullptr && !info.m_text.empty()) internComputeModelData(id);
}

void textgenerator::updateText_box(uint id, float x1, float y1, float x2, float y2)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  info.m_zone = glm::vec4(x1,y1,x2,y2);
  if (info.m_font != nullptr && !info.m_text.empty()) internComputeModelData(id);
}

void textgenerator::updateText_fontsize(uint id, float fontsize)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  if (info.m_fontsize == fontsize) return;
  info.m_fontsize = fontsize;
  if (info.m_font != nullptr && !info.m_text.empty()) internComputeModelData(id);
}

void textgenerator::updateText_font(uint id, const tre::font *ffont)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  if (info.m_font == ffont) return;
  info.m_font = ffont;
  if (info.m_font != nullptr && !info.m_text.empty()) internComputeModelData(id);
}

void textgenerator::updateText_color(uint id, const glm::vec4 & color)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  TRE_ASSERT(info.m_parent != nullptr);
  if (info.m_color == color) return;
  info.m_color = color;
  info.m_parent->colorizePart(info.m_parentPartId, color);
}

void textgenerator::updateText_txt(uint id, const std::string &txt)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  TRE_ASSERT(info.m_font != nullptr);
  TRE_ASSERT(info.m_parent != nullptr);
  if (info.m_text == txt) return;
  info.m_text = txt;
  internComputeModelData(id);
}

void textgenerator::updateText_pixelSize(uint id,const glm::vec2 pixelSize)
{
  TRE_ASSERT(id < m_textInfo.size());
  s_textInfo & info = m_textInfo[id];
  if (info.m_pixelSize == pixelSize) return;
  info.m_pixelSize = pixelSize;
  internComputeModelData(id);
}

void textgenerator::computeModelData(uint first, uint count)
{
  TRE_ASSERT(first + count <= m_textInfo.size());
  for (uint id = first; id < first + count; ++id)
  {
    if (m_textInfo[id].m_font != nullptr && m_textInfo[id].m_parent != nullptr)
      internComputeModelData(id);
  }
}

void textgenerator::internComputeModelData(uint id)
{
  TRE_ASSERT(id < m_textInfo.size());
  const s_textInfo & info = m_textInfo[id];
  TRE_ASSERT(info.m_font != nullptr);
  TRE_ASSERT(info.m_parent != nullptr);
  TRE_ASSERT(std::strlen(info.m_text.c_str()) <= info.m_text.size());

  info.m_parent->resizePart(info.m_parentPartId, info.m_text.size() * 6);

  const s_modelDataLayout & layout = info.modelLayout();
  const s_partInfo & part = info.modelPartInfo();

  const bool isBoxValid = (info.m_zone.x < info.m_zone.z) && (info.m_zone.y < info.m_zone.w);
  const bool isPixelSnapValid = (info.m_pixelSize.x > 0.f) && (info.m_pixelSize.y > 0.f);

  const uint fontsizePixel = isPixelSnapValid ? uint(info.m_fontsize / info.m_pixelSize.y) : 512 /*big value*/;
  const font::s_fontMap &fontMap = info.m_font->get_bestFontMap(fontsizePixel);
  const float fontsize = isPixelSnapValid ? info.m_pixelSize.y * fontsizePixel : info.m_fontsize;

  info.m_choosenFontSizePixel = fontMap.m_fsize;

  const float scaleH = fontsize / fontMap.m_hline;
  const glm::vec2 scale = glm::vec2(info.m_font->get_texture().m_w * scaleH / info.m_font->get_texture().m_h, scaleH);

  // compute text
  uint iv0 = part.m_offset;
  info.m_maxboxsize.x = 0.f;
  info.m_maxboxsize.y = fontsize;
  float posx = info.m_zone.x;
  float posy = info.m_zone.w;
  for (std::size_t ich = 0, iLen = info.m_text.size(); ich < iLen; ++ich)
  {
    uint idchar( info.m_text[ich] );
    if (idchar >= fontMap.m_charMap.size() ) idchar = 0; // unknown character, log error in debug ?

    if (info.m_text[ich] == '\n')
    {
      posx = info.m_zone.x;
      posy -= fontsize;
      info.m_maxboxsize.y += fontsize;
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

    const font::s_charInfo & charMap = fontMap.get_charMap(idchar);

    glm::vec4 quadPos( posx + scale.x * charMap.xoffs,
                       posy - scale.y * charMap.yoffs - scale.y * (charMap.cby-charMap.cay),
                       posx + scale.x * charMap.xoffs + scale.x * (charMap.cbx-charMap.cax),
                       posy - scale.y * charMap.yoffs );
    glm::vec4 quadUV( charMap.cax, charMap.cay,
                      charMap.cbx, charMap.cby );

    const float sizex = quadPos.z - info.m_zone.x;
    if (sizex > info.m_maxboxsize.x) info.m_maxboxsize.x = sizex;

    const float posx_old = posx;

    posx += scale.x * charMap.xadvance;

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

    TRE_ASSERT(iv0 + 6 <= part.m_offset + part.m_size);
    // tri-A
    layout.m_positions[iv0 + 0][0] = quadPos.x;
    layout.m_positions[iv0 + 0][1] = quadPos.y;
    layout.m_positions[iv0 + 1][0] = quadPos.x;
    layout.m_positions[iv0 + 1][1] = quadPos.w;
    layout.m_positions[iv0 + 2][0] = quadPos.z;
    layout.m_positions[iv0 + 2][1] = quadPos.w;
    layout.m_uvs[iv0 + 0][0] = quadUV.x;
    layout.m_uvs[iv0 + 0][1] = quadUV.w;
    layout.m_uvs[iv0 + 1][0] = quadUV.x;
    layout.m_uvs[iv0 + 1][1] = quadUV.y;
    layout.m_uvs[iv0 + 2][0] = quadUV.z;
    layout.m_uvs[iv0 + 2][1] = quadUV.y;
    // tri-B
    layout.m_positions[iv0 + 3][0] = quadPos.z;
    layout.m_positions[iv0 + 3][1] = quadPos.w;
    layout.m_positions[iv0 + 4][0] = quadPos.z;
    layout.m_positions[iv0 + 4][1] = quadPos.y;
    layout.m_positions[iv0 + 5][0] = quadPos.x;
    layout.m_positions[iv0 + 5][1] = quadPos.y;
    layout.m_uvs[iv0 + 3][0] = quadUV.z;
    layout.m_uvs[iv0 + 3][1] = quadUV.y;
    layout.m_uvs[iv0 + 4][0] = quadUV.z;
    layout.m_uvs[iv0 + 4][1] = quadUV.w;
    layout.m_uvs[iv0 + 5][0] = quadUV.x;
    layout.m_uvs[iv0 + 5][1] = quadUV.w;
    // end
    iv0 += 6;
  }

  info.m_parent->resizePart(info.m_parentPartId, iv0 - part.m_offset);
  info.m_parent->colorizePart(info.m_parentPartId, info.m_color);
}

} // namespace
