#ifndef TEXTGENERATOR_H
#define TEXTGENERATOR_H

#include "openglinclude.h"

#include "utils.h"

#include <vector>
#include <string>

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
    float        m_fontsize = 1.f; ///< font-size
    std::string  m_text;
    const font*  m_font = nullptr;
    glm::vec2    m_pixelSize = glm::vec2(0.f, 0.f); ///< snap to pixel. Size of a pixel. (Zero means no snapping.)

    void setupBasic(const font *font, const float fontSize, const std::string &str, const glm::vec2 &pos = glm::vec2(0.f), const glm::vec4 &color = glm::vec4(1.f))
    {
      m_font = font;
      m_fontsize = fontSize;
      m_text = str;
      m_zone = glm::vec4(pos, pos);
      m_color = color;
    }
  };

  struct s_textInfoOut
  {
    glm::vec2 m_maxboxsize = glm::vec2(0.f); ///< native size (width, height) of the text (if no limit)
    uint m_choosenFontSizePixel = 0; ///< font-size from the font that has been used
  };

  inline uint geometry_VertexCount(const std::string &txt) { return 6 * txt.size(); }

  void generate(const s_textInfo &info, modelRaw2D *outMesh, uint outPartId, uint outOffset, s_textInfoOut *outInfo); ///< Generate the geometry to render the text. outMesh or outInfo can be null.
};

} // namespace

#endif // TEXTGENERATOR_H
