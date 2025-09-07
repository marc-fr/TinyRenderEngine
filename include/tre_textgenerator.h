#ifndef TEXTGENERATOR_H
#define TEXTGENERATOR_H

#include "tre_openglinclude.h"

#include <vector>
#include <string> // std::strlen

namespace tre {

// forward decl
struct s_modelDataLayout;
class modelRaw2D;
class font;

/**
 * @brief The modeldyn2DText class
 * It handles texts. Each text has an ID, and it can be modified after created.
 * Any modifications are effective in the GPU after updateIntoGPU call.
 * For drawing, "2D" shaders must be used.
 */

/**
 * Allow generation of text
 */
namespace textgenerator
{

  struct s_textInfo
  {
    glm::vec4    m_color = glm::vec4(1.f);
    glm::vec4    m_zone = glm::vec4(0.f); ///< zone in which the text is drawn (x1,y1,x2,y2). Use (x1,y1,x1,y1) if no out border.
    const char   *m_text = nullptr;
    const font   *m_font = nullptr;
    float        m_fontHeight = 1.f; ///< font-size
    float        m_lineHeight = 1.08f; ///< line-size
    unsigned     m_fontPixelSize = -1u; ///< hint on font-size in pixel units. Used to select best font maps according to pixel size.
    bool         m_boxExtendedToNextChar = false; ///< Extend the bounding box by the next character start position.

    void setupBasic(const font *font, const char *str, const glm::vec2 &pos = glm::vec2(0.f), const glm::vec4 &color = glm::vec4(1.f))
    {
      m_font = font;
      m_text = str;
      m_zone = glm::vec4(pos, pos);
      m_color = color;
    }
    void setupSize(const float lineHeight, const float fontHeight)
    {
      m_lineHeight = lineHeight;
      m_fontHeight = fontHeight;
    }
    void setupSize(const float lineHeight)
    {
      m_lineHeight = lineHeight;
      m_fontHeight = lineHeight / 1.08f;
    }
  };

  struct s_textInfoOut
  {
    glm::vec2 m_maxboxsize = glm::vec2(0.f); ///< native size (width, height) of the text (if no limit)
    unsigned m_choosenFontSizePixel = 0; ///< font-size from the font that has been used
  };

  inline std::size_t geometry_VertexCount(const char *txt) { return 6 * std::strlen(txt); }

  /**
   * @brief Generate the geometry to render the text (when outMesh is not null) and fill out gemetric info (when outInfo is not null).
   * Either outMesh or outInfo can be null.
   * It will write to the outMesh the vertex count given from "geometry_VertexCount". (Trailing vertices will have a full-transparent color.)
   */
  void generate(const s_textInfo &info, modelRaw2D *outMesh, unsigned outPartId, unsigned outOffset, s_textInfoOut *outInfo);
};

} // namespace

#endif // TEXTGENERATOR_H
