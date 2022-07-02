#ifndef TEXTGENERATOR_H
#define TEXTGENERATOR_H

#include "openglinclude.h"

#include "utils.h"

#include <vector>
#include <string>

namespace tre {

// forward decl
struct s_partInfo;
struct s_modelDataLayout;
class modelRaw2D;
class font;

/**
 * @brief The modeldyn2DText class
 * It handles texts. Each text has an ID, and it can be modified after created.
 * Any modifications are effective in the GPU after updateIntoGPU call.
 * For drawing, "2D" shaders must be used.
 */
class textgenerator
{
protected:
  struct s_textInfo
  {
    glm::vec4    m_color = glm::vec4(1.f);
    glm::vec4    m_zone = glm::vec4(0.f); ///< zone in which the text is drawn (x1,y1,x2,y2)
    float        m_fontsize = 1.f; ///< font-size
    std::string  m_text;
    const font*  m_font = nullptr;
    modelRaw2D*  m_parent = nullptr;
    uint         m_parentPartId = 0;
    glm::vec2    m_pixelSize = glm::vec2(-1.f, -1.f); ///< snap to pixel. Size of a pixel. (Negative value means no value.)

    mutable glm::vec2 m_maxboxsize = glm::vec2(0.f); ///< [output] native size (width, height) of the text (if no limit)
    mutable uint m_choosenFontSizePixel = 0; ///< [output] report which font-size has been used

    const s_partInfo &modelPartInfo() const;
    const s_modelDataLayout &modelLayout() const;
  };
public:
  // Default constructors/destructors

  uint createTexts(uint tcount, modelRaw2D * parent); ///< Create texts. Return the id of the first.
  void clearTexts() { m_textInfo.clear(); } ///< Clear texts. Leave the parent-model(s) untouched.
  void updateText_pos(uint id,float xfirst,float yline); ///< update the position of the first char. Warning: it applies an offset on the "box"
  void updateText_box(uint id, float x1, float y1, float x2, float y2); ///< update the bounding box. Warning: it applies an offset on the "pos"
  void updateText_fontsize(uint id, float fontsize); ///< update the font size.
  void updateText_font(uint id, const font * ffont); ///< update the font.
  void updateText_color(uint id, const glm::vec4 & color);
  void updateText_txt(uint id, const std::string & txt);
  void updateText_pixelSize(uint id, const glm::vec2 pixelSize);

  glm::vec2 get_maxboxsize(uint id) const { TRE_ASSERT(id < m_textInfo.size()); return m_textInfo[id].m_maxboxsize; }
  uint      get_pickedFontSizePixel(uint id) const { TRE_ASSERT(id < m_textInfo.size()); return m_textInfo[id].m_choosenFontSizePixel; }

  const font *get_font(uint id) const { return m_textInfo[id].m_font; } ///< Get the font used in text "id"
  uint        get_partId(uint id) const { return m_textInfo[id].m_parentPartId; } ///< Get the part-id in parent-model used in text "id"

  void computeModelData() { computeModelData(0, uint(m_textInfo.size())); } ///< compute all texts
  void computeModelData(uint first, uint count = 1);

protected:
  void internComputeModelData(uint id);
  std::vector<s_textInfo> m_textInfo; ///< info of the text "id" (with help of the partInfo also)
};

} // namespace

#endif // TEXTGENERATOR_H
