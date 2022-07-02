
#include "ui.h"

#include "texture.h"

namespace tre {

namespace ui {

static const glm::vec4 VEC4_ZERO = glm::vec4(0.f);

// widget =====================================================================

bool widget::getIsOverPosition(const glm::vec3 &position) const
{
  return (wzone.x <= position.x) && (wzone.z >= position.x) &&
         (wzone.y <= position.y) && (wzone.w >= position.y);

}

void widget::acceptEventBase_focus(s_eventIntern &event)
{
  if (!wisactive || event.accepted)
  {
    if (wisactive && wishighlighted == true)
    {
      if (wcb_loss_focus != nullptr) wcb_loss_focus(this);
    }
    set_ishighlighted(false);
    return;
  }

  const bool isOverCurr = getIsOverPosition(event.mousePos);
  const bool isOverPrev = getIsOverPosition(event.mousePosPrev);
  const bool isPressed  = event.mouseLeftPressed | event.mouseRightPressed | (event.event.type == SDL_MOUSEBUTTONUP /*catch the release event too*/);
  const bool isFocused = (isOverCurr && !isPressed) || (isOverPrev && isPressed);

  if (wishighlighted == true && isFocused == false)
  {
    if (wcb_loss_focus != nullptr) wcb_loss_focus(this);
  }
  if (wishighlighted == false && isFocused == true)
  {
    if (wcb_gain_focus != nullptr) wcb_gain_focus(this);
  }
  set_ishighlighted(isFocused);
  event.accepted |= isFocused;
}

void widget::acceptEventBase_click(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool isClickedL = (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_LEFT);
  const bool isClickedR = (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_RIGHT);

  if (wishighlighted && isClickedL)
  {
    event.accepted = true;
    if (wcb_clicked_left != nullptr) wcb_clicked_left(this);
  }
  if (wishighlighted && isClickedR)
  {
    event.accepted = true;
    if (wcb_clicked_right != nullptr) wcb_clicked_right(this);
  }
}

glm::vec4 widget::resolve_color() const
{
  return get_parentWindow()->get_colortheme().resolveColor(wcolor, resolve_colorModifier()) * get_parentWindow()->get_colormask();
}

// widgetText =================================================================

uint widgetText::get_vcountSolid() const { return (wwithbackground==true) * 6; }
uint widgetText::get_vcountLine() const { return (wwithborder==true)*4 * 2; }
uint widgetText::get_vcountPict() const { return 0; }
uint widgetText::get_countText() const { return 1; }
const texture* widgetText::get_textureSlot() const { return nullptr; }
glm::vec2 widgetText::get_zoneSizeDefault() const
{
  auto & objtext  = get_parentUI()->getDrawObject_Text();

  const float fsize = wfontsizeModifier * get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());

  // check if "m_adrText.part" is valid. Else use a dummy adress.
  objtext.updateText_font(m_adrText.part, get_parentUI()->get_defaultFont());
  objtext.updateText_fontsize(m_adrText.part, fsize);
  objtext.updateText_txt(m_adrText.part, wtext);
  if (get_parentUI()->get_dimension() == 2)
    objtext.updateText_pixelSize(m_adrText.part, get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL));

  objtext.computeModelData(m_adrText.part); // tmp scafolding

  return objtext.get_maxboxsize(m_adrText.part);
}
void widgetText::compute_data()
{
  auto & objsolid = get_parentUI()->getDrawObject_Box();
  auto & objtext  = get_parentUI()->getDrawObject_Text();

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorParent = get_parent()->resolve_color();
  const glm::vec4 colorBack = blendColor(colorFront, colorParent, 0.5f);

  if (wwithbackground)
  {
    objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset, wzone, colorBack, VEC4_ZERO);
  }
  if (wwithborder)
  {
    const glm::vec2 pt00(wzone.x, wzone.y);
    const glm::vec2 pt01(wzone.x, wzone.w);
    const glm::vec2 pt11(wzone.z, wzone.w);
    const glm::vec2 pt10(wzone.z, wzone.y);

    glm::vec4 colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? -0.2f : 0.2f);

    objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 0, pt00, pt10, colLine);
    objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 2, pt10, pt11, colLine);

    colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);

    objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 4, pt11, pt01, colLine);
    objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 6, pt01, pt00, colLine);
  }

  const float fsize = wfontsizeModifier * get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());

  objtext.updateText_box(m_adrText.part,wzone.x,wzone.y,wzone.z,wzone.w);

  objtext.updateText_font(m_adrText.part, get_parentUI()->get_defaultFont());
  objtext.updateText_fontsize(m_adrText.part, fsize);
  objtext.updateText_txt(m_adrText.part, wtext);
  if (get_parentUI()->get_dimension() == 2)
    objtext.updateText_pixelSize(m_adrText.part, get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL));

  objtext.updateText_color(m_adrText.part,colorFront);
}
void widgetText::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}

// widgetTextEdit =============================================================

uint widgetTextEdit::get_vcountSolid() const { return widgetText::get_vcountSolid() + 6; }
uint widgetTextEdit::get_countText() const { return 2; }
void widgetTextEdit::compute_data()
{
  widgetText::compute_data();

  auto & objsolid = get_parentUI()->getDrawObject_Box();
  auto & objtext  = get_parentUI()->getDrawObject_Text();

  // dummy text to compute the cursor position

  const float fsize = wfontsizeModifier * get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());
  const glm::vec2 pxsize = get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL);

  std::string txtDummy = wtext;
  if (wcursorPos != -1) txtDummy = txtDummy.substr(0, wcursorPos);
  {
    std::size_t iBEG = 0;
    std::size_t iCR = txtDummy.find('\n');
    while (iCR != std::string::npos)
    {
      if (iBEG != iCR)
        txtDummy.erase(iBEG, iCR - iBEG);
      ++iBEG;
      iCR = txtDummy.find('\n', iBEG);
    }
  }

  objtext.updateText_font(m_adrText.part + 1, get_parentUI()->get_defaultFont());
  objtext.updateText_fontsize(m_adrText.part + 1, fsize);
  objtext.updateText_txt(m_adrText.part + 1, txtDummy);
  if (get_parentUI()->get_dimension() == 2)
    objtext.updateText_pixelSize(m_adrText.part + 1, pxsize);
  objtext.updateText_color(m_adrText.part + 1, glm::vec4(0.f));

  objtext.computeModelData(m_adrText.part + 1); // tmp scafolding

  const glm::vec2 textDim = objtext.get_maxboxsize(m_adrText.part + 1);

  // draw cursor

  const glm::vec4 cursorBox = glm::vec4(wzone.x + textDim.x - pxsize.x, wzone.w - textDim.y,
                                        wzone.x + textDim.x + pxsize.x, wzone.w - textDim.y + fsize);

  const float     cursorAlpha = (wisEditing && wcursorAnimTime <= 0.5f * wcursorAnimSpeed) ? 1.f : 0.f;
  const glm::vec4 cursorColor = transformColor(resolve_color(), COLORTHEME_LIGHTNESS, 0.2f) * glm::vec4(1.f, 1.f, 1.f, cursorAlpha);

  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + widgetText::get_vcountSolid(), cursorBox, cursorColor, VEC4_ZERO);
}
void widgetTextEdit::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  if (wisEditing)
  {
    TRE_ASSERT(wishighlighted == true);
    TRE_ASSERT(SDL_IsTextInputActive() == SDL_TRUE);

    if (event.event.type == SDL_MOUSEBUTTONDOWN)
    {
      acceptEventBase_focus(event);
      wisEditing &= wishighlighted;
    }
    else if (event.event.type == SDL_KEYDOWN &&
             (event.event.key.keysym.sym == SDLK_ESCAPE || (event.event.key.keysym.sym == SDLK_BACKSPACE && !wallowMultiLines)))
    {
      wisEditing = false;
      event.accepted = true;
    }

    if (!wisEditing) // stop editing
    {
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      SDL_StopTextInput();
    }
  }
  else
  {
    acceptEventBase_focus(event);

    wisEditing = wishighlighted && (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_LEFT);

    if (wisEditing) // start editing
    {
      SDL_StartTextInput();
      event.accepted = true;
    }
  }

  // editing ...
  if (wisEditing)
  {
    if (event.event.type == SDL_KEYDOWN)
    {
      if (event.event.key.keysym.sym == SDLK_RETURN)
      {
        if (wcursorPos != -1)
          wtext.insert(wcursorPos++, "\n");
        else
          wtext += "\n";
        event.accepted = true;
      }
      else if (event.event.key.keysym.sym == SDLK_BACKSPACE)
      {
        if (!wtext.empty() && wcursorPos != 0)
        {
          if (wcursorPos != -1)
            wtext.erase(--wcursorPos, 1);
          else
            wtext.pop_back();
          event.accepted = true;
        }
      }
      else if (event.event.key.keysym.sym == SDLK_DELETE)
      {
        if (!wtext.empty())
        {
          if (wcursorPos != -1)
            wtext.erase(wcursorPos, 1);
          else
            wtext.pop_back();
          if (wcursorPos >= int(wtext.size())) wcursorPos = -1;
          event.accepted = true;
        }
      }
      else if (event.event.key.keysym.sym == SDLK_LEFT)
      {
        if (wcursorPos != 0)
          wcursorPos = (wcursorPos == -1) ? wtext.size() - 1 : wcursorPos - 1;
        event.accepted = true;
      }
      else if (event.event.key.keysym.sym == SDLK_RIGHT)
      {
        if (wcursorPos != -1)
          wcursorPos = (wcursorPos + 1 == int(wtext.size())) ? -1 : wcursorPos + 1;
        event.accepted = true;
      }
      else if (event.event.key.keysym.sym == SDLK_HOME)
      {
        if (!wtext.empty())
        {
          wcursorPos = 0;
          event.accepted = true;
        }
      }
      else if (event.event.key.keysym.sym == SDLK_END)
      {
        wcursorPos = -1;
        event.accepted = true;
      }
      else
      {
        event.accepted = true; // keys are catched by SDL_TEXTINPUT
      }
    }
    else if (event.event.type == SDL_TEXTINPUT)
    {
      if (wcursorPos != -1)
        wtext.insert(wcursorPos++, event.event.text.text);
      else
        wtext += event.event.text.text;
      event.accepted = true;
    }

    if (event.accepted)
    {
      if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
      m_isUpdateNeededData = true;
    }
  }
}
void widgetTextEdit::animate(float dt)
{
  widget::animate(dt);
  wcursorAnimTime = std::fmod(wcursorAnimTime + dt, wcursorAnimSpeed);
  m_isUpdateNeededData = true;
}

// widgetPicture ==============================================================

uint widgetPicture::get_vcountSolid() const { return 0; }
uint widgetPicture::get_vcountLine() const { return 0; }
uint widgetPicture::get_vcountPict() const { return 1 * 6; }
uint widgetPicture::get_countText() const { return 0; }
const texture* widgetPicture::get_textureSlot() const { return wtexId; }
glm::vec2 widgetPicture::get_zoneSizeDefault() const
{
  if (wtexId == nullptr)
    return get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL);

  const float ratioWoH = ( wtexId->m_w * fabsf(wtexUV[2]-wtexUV[0]) ) /
                         ( wtexId->m_h * fabsf(wtexUV[3]-wtexUV[1]) ) ;

  const float h = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize()) * wheightModifier;

  // TODO : 1-1 pixel (add an option ??)

  return glm::vec2(h * ratioWoH , h);
}
void widgetPicture::compute_data()
{
  auto & objsolid = get_parentUI()->getDrawObject_Box();

  const glm::vec4 uvSwap = glm::vec4(wtexUV.x, wtexUV.w, wtexUV.z, wtexUV.y);

  objsolid.fillDataRectangle(m_adrPict.part, m_adrPict.offset, wzone, wmultiply ? resolve_color() : glm::vec4(1.f), uvSwap);
}
void widgetPicture::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}

// widgetBar ==================================================================

uint widgetBar::get_vcountSolid() const { return 2 * 6; }
uint widgetBar::get_vcountLine() const { return ((wwithborder==true)*4+(wwiththreshold==true)*1) * 2; }
uint widgetBar::get_vcountPict() const { return 0; }
uint widgetBar::get_countText() const { return (wwithtext==true)*1; }
const texture* widgetBar::get_textureSlot() const { return nullptr; }
glm::vec2 widgetBar::get_zoneSizeDefault() const
{
  const float h = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());
  return glm::vec2(5.f * h, h);
}
void widgetBar::compute_data()
{
  auto & objsolid = get_parentUI()->getDrawObject_Box();

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorParent = get_parent()->resolve_color();
  const glm::vec4 colorBack = blendColor(colorFront, colorParent, 0.5f);
  const glm::vec4 colorInv = inverseColor(0.5f * colorFront + 0.5f * colorBack, COLORTHEME_LIGHTNESS);

  const float valx2 = wzone.x + (wzone.z-wzone.x) * (wvalue-wvaluemin) / (wvaluemax-wvaluemin);
  const glm::vec4 zoneBar(wzone.x,wzone.y,valx2,wzone.w);

  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 0, wzone, colorBack, VEC4_ZERO);
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, zoneBar, colorFront, VEC4_ZERO);

  uint lineOffset = m_adrLine.offset;
  if (wwiththreshold)
  {
    const float valxt = wzone.x + (wzone.z-wzone.x) * (wvaluethreshold-wvaluemin) / (wvaluemax-wvaluemin);

    objsolid.fillDataLine(m_adrLine.part, lineOffset, glm::vec2(valxt,wzone.y), glm::vec2(valxt,wzone.w), colorInv);
    lineOffset += 2;
  }
  if (wwithborder)
  {
    const glm::vec2 pt00(wzone.x, wzone.y);
    const glm::vec2 pt01(wzone.x, wzone.w);
    const glm::vec2 pt11(wzone.z, wzone.w);
    const glm::vec2 pt10(wzone.z, wzone.y);

    glm::vec4 colorBorder = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? -0.2f : 0.2f);

    objsolid.fillDataLine(m_adrLine.part, lineOffset + 0, pt00, pt10, colorBorder);
    objsolid.fillDataLine(m_adrLine.part, lineOffset + 2, pt10, pt11, colorBorder);

    colorBorder = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);

    objsolid.fillDataLine(m_adrLine.part, lineOffset + 4, pt11, pt01, colorBorder);
    objsolid.fillDataLine(m_adrLine.part, lineOffset + 6, pt01, pt00, colorBorder);

    lineOffset += 8;
  }
  if (wwithtext)
  {
    auto & objtext = get_parentUI()->getDrawObject_Text();

    char curtxt[16];
    std::snprintf(curtxt, 15, "%.3f", wvalue);
    curtxt[15] = 0;

    objtext.updateText_box(m_adrText.part,wzone.x,wzone.y,wzone.z,wzone.w);
    objtext.updateText_fontsize(m_adrText.part, wzone.w-wzone.y);
    objtext.updateText_font(m_adrText.part, get_parentUI()->get_defaultFont());
    objtext.updateText_txt(m_adrText.part, curtxt);

    const glm::vec2 txtSize = objtext.get_maxboxsize(m_adrText.part);
    if (txtSize.x < wzone.z - wzone.x)
    {
      const float centerX = 0.5f * (wzone.z + wzone.x);
      objtext.updateText_box(m_adrText.part, centerX - 0.5f * txtSize.x,wzone.y, centerX + 0.6f * txtSize.x, wzone.w);
    }

    objtext.updateText_color(m_adrText.part, colorInv);
  }
}
void widgetBar::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool canBeEdited = wiseditable && getIsOverPosition(event.mousePosPrev) && !event.accepted;
  const bool isPressedLeft = canBeEdited && event.mouseLeftPressed;
  const bool isReleasedLeft = canBeEdited && (event.event.type == SDL_MOUSEBUTTONUP && event.event.button.button == SDL_BUTTON_LEFT);

  if (isPressedLeft)
    wishighlighted = true;
  else
    acceptEventBase_focus(event);

  if (isPressedLeft || isReleasedLeft)
  {
    float newvalue = wvaluemin + (event.mousePos.x - wzone.x) / (wzone.z - wzone.x) * (wvaluemax - wvaluemin);
    if (newvalue < wvaluemin) newvalue = wvaluemin;
    if (newvalue > wvaluemax) newvalue = wvaluemax;
    set_value(newvalue);
    event.accepted = true;
  }

  if (isPressedLeft)
  {
    if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
  }
  if (isReleasedLeft)
  {
    TRE_ASSERT(!isPressedLeft);
    if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
    else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
  }
}

// widgetBarZero ==============================================================

uint widgetBarZero::get_vcountLine() const { return widgetBar::get_vcountLine() + 1 * 2; }
void widgetBarZero::compute_data()
{
  widgetBar::compute_data();

  auto & objsolid = get_parentUI()->getDrawObject_Box();

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorZero = inverseColor(colorFront, COLORTHEME_SATURATION);

  const float valx1 = wzone.x + (wzone.z-wzone.x)*(0.f-wvaluemin)/(wvaluemax-wvaluemin);
  const float valx2 = valx1 + (wzone.z-wzone.x)*(wvalue-0.f)/(wvaluemax-wvaluemin);
  const glm::vec4 bar(valx1,wzone.y,valx2,wzone.w);
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, bar, colorFront, VEC4_ZERO);
  // zero line
  const uint lineOffset = m_adrLine.offset + widgetBar::get_vcountLine();
  const glm::vec2 pt1(valx1,wzone.y);
  const glm::vec2 pt2(valx1,wzone.w);
  objsolid.fillDataLine(m_adrLine.part, lineOffset, pt1, pt2, colorZero);
}

// widgetBoxCheck =============================================================

uint widgetBoxCheck::get_vcountSolid() const { return 1 * 6; }
uint widgetBoxCheck::get_vcountLine() const { return 6 * 2; }
uint widgetBoxCheck::get_vcountPict() const { return 0; }
uint widgetBoxCheck::get_countText() const { return 0; }
const texture* widgetBoxCheck::get_textureSlot() const { return nullptr; }
glm::vec2 widgetBoxCheck::get_zoneSizeDefault() const
{
  const float h = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());
  return glm::vec2(h, h);
}
void widgetBoxCheck::compute_data()
{
  auto & objsolid = get_parentUI()->getDrawObject_Box();

  const glm::vec2 center( 0.5f*(wzone.x+wzone.z),
                          0.5f*(wzone.y+wzone.w));
  const glm::vec2 length( 0.5f*(wzone.z-wzone.x),
                          0.5f*(wzone.w-wzone.y));

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorParent = get_parent()->resolve_color();
  const glm::vec4 colorBack = blendColor(colorFront, colorParent, 0.5f);

  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset, wzone, colorBack, VEC4_ZERO);

  const glm::vec2 pt00(wzone.x, wzone.y);
  const glm::vec2 pt01(wzone.x, wzone.w);
  const glm::vec2 pt11(wzone.z, wzone.w);
  const glm::vec2 pt10(wzone.z, wzone.y);

  glm::vec4 colorBorder = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? -0.2f : 0.2f);

  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 0, pt00, pt10, colorBorder);
  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 2, pt10, pt11, colorBorder);

  colorBorder = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);

  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 4, pt11, pt01, colorBorder);
  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 6, pt01, pt00, colorBorder);

  const glm::vec4 crossColor = wvalue == true ? colorFront : colorParent;

  const glm::vec2 ptLL(center.x - length.x * 0.7f, center.y - length.y * 0.7f);
  const glm::vec2 ptRR(center.x + length.x * 0.7f, center.y + length.y * 0.7f);
  const glm::vec2 ptRL(center.x + length.x * 0.7f, center.y - length.y * 0.7f);
  const glm::vec2 ptLR(center.x - length.x * 0.7f, center.y + length.y * 0.7f);

  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset +  8, ptLL, ptRR, crossColor);
  objsolid.fillDataLine(m_adrLine.part, m_adrLine.offset + 10, ptRL, ptLR, crossColor);
}
void widgetBoxCheck::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);

  if (!wisactive || !wishighlighted) return;

  const bool isClickLeft = (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_LEFT);

  if (isClickLeft)
  {
    event.accepted = true;
    if (wcb_clicked_left != nullptr) wcb_clicked_left(this);
    if (wiseditable)
    {
      set_value(!wvalue);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
}

// widgetLineChoice ===========================================================

uint widgetLineChoice::get_vcountSolid() const { return 2 * 3; }
uint widgetLineChoice::get_vcountLine() const { return 0; }
uint widgetLineChoice::get_vcountPict() const { return 0; }
uint widgetLineChoice::get_countText() const { return 1; }
const texture* widgetLineChoice::get_textureSlot() const { return nullptr; }
glm::vec2 widgetLineChoice::get_zoneSizeDefault() const
{
  const float fsize = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());

  uint textMaxId = 0;
  for (uint iT = 1; iT < wvalues.size(); ++iT)
  {
    if (wvalues[iT].size() > wvalues[textMaxId].size())
      textMaxId = iT;
  }

  // check if "m_adrText.part" is valid. Else use a dummy adress.
  auto & objtext  = get_parentUI()->getDrawObject_Text();

  objtext.updateText_font(m_adrText.part, get_parentUI()->get_defaultFont());
  objtext.updateText_fontsize(m_adrText.part, fsize);
  objtext.updateText_txt(m_adrText.part, wvalues[textMaxId]);
  if (get_parentUI()->get_dimension() == 2)
    objtext.updateText_pixelSize(m_adrText.part, get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL));

  const glm::vec2 textSize = objtext.get_maxboxsize(m_adrText.part);

  objtext.updateText_txt(m_adrText.part, wvalues[wselectedIndex]);

  return glm::vec2(textSize.x + fsize * 3.f, fsize);
}
void widgetLineChoice::compute_data()
{
  auto & objsolid = get_parentUI()->getDrawObject_Box();
  auto & objtext  = get_parentUI()->getDrawObject_Text();

  const glm::vec4 colorFront = resolve_color();

  const float yCenter = 0.5f * (wzone.y + wzone.w);
  const float ySize = (wzone.w - wzone.y);

  const uint objsolid_vertex_first = objsolid.partInfo(m_adSolid.part).m_offset + m_adSolid.offset;
  auto posPtr = objsolid.layout().m_positions.begin<glm::vec2>(objsolid_vertex_first);
  auto colPtr = objsolid.layout().m_colors.begin<glm::vec4>(objsolid_vertex_first);

  *posPtr++ = glm::vec2(wzone.x, yCenter);
  *posPtr++ = glm::vec2(wzone.x + 0.8f * ySize, yCenter - 0.4f * ySize);
  *posPtr++ = glm::vec2(wzone.x + 0.8f * ySize, yCenter + 0.4f * ySize);

  const glm::vec4 colorL = get_parentWindow()->get_colortheme().resolveColor(colorFront, (wselectedIndex != 0 || wcyclic) ? (wisHoveredLeft ? 1.f : 0.f) : -2.f);

  *colPtr++ = colorL;
  *colPtr++ = colorL;
  *colPtr++ = colorL;

  *posPtr++ = glm::vec2(wzone.z, yCenter);
  *posPtr++ = glm::vec2(wzone.z - 0.8f * ySize, yCenter + 0.4f * ySize);
  *posPtr++ = glm::vec2(wzone.z - 0.8f * ySize, yCenter - 0.4f * ySize);

  const glm::vec4 colorR = get_parentWindow()->get_colortheme().resolveColor(colorFront, (wselectedIndex != wvalues.size()-1 || wcyclic) ? (wisHoveredRight ? 1.f : 0.f) : -2.f);

  *colPtr++ = colorR;
  *colPtr++ = colorR;
  *colPtr++ = colorR;

  const float fsize = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());

  objtext.updateText_font(m_adrText.part, get_parentUI()->get_defaultFont());
  objtext.updateText_fontsize(m_adrText.part, fsize);
  objtext.updateText_txt(m_adrText.part, wvalues[wselectedIndex]);
  if (get_parentUI()->get_dimension() == 2)
    objtext.updateText_pixelSize(m_adrText.part, get_parentWindow()->resolve_sizeWH(s_size::ONE_PIXEL));

  const float xCenter = 0.5f * (wzone.x + wzone.z);
  const glm::vec2 textSize = objtext.get_maxboxsize(m_adrText.part);

  objtext.updateText_box(m_adrText.part,xCenter - 0.5f * textSize.x, wzone.y, xCenter + 0.6f * textSize.x, wzone.w);

  objtext.updateText_color(m_adrText.part,colorFront);
}
void widgetLineChoice::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);

  if (!wisactive || !wishighlighted)
  {
    wisHoveredLeft = false;
    wisHoveredRight = false;
    return;
  }

  const bool isClickLeft = (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_LEFT);

  const float yCenter = 0.5f * (wzone.y + wzone.w);
  const float ySize = (wzone.w - wzone.y);

  const glm::vec4 zoneL = glm::vec4(wzone.x, yCenter - 0.4f * ySize, wzone.x + ySize * 0.8f, yCenter + 0.4f * ySize);
  const bool hoveredLeft = (zoneL.x <= event.mousePos.x) && (zoneL.z >= event.mousePos.x) &&
                           (zoneL.y <= event.mousePos.y) && (zoneL.w >= event.mousePos.y) &&
                           wiseditable;
  m_isUpdateNeededData |= (wisHoveredLeft != hoveredLeft);
  wisHoveredLeft = hoveredLeft;

  const glm::vec4 zoneR = glm::vec4(wzone.z - ySize * 0.8f, yCenter - 0.4f * ySize, wzone.z, yCenter + 0.4f * ySize);
  const bool hoveredRight = (zoneR.x <= event.mousePos.x) && (zoneR.z >= event.mousePos.x) &&
                            (zoneR.y <= event.mousePos.y) && (zoneR.w >= event.mousePos.y) &&
                            wiseditable;
  m_isUpdateNeededData |= (wisHoveredRight != hoveredRight);
  wisHoveredRight = hoveredRight;

  if (isClickLeft)
  {
    if (wisHoveredLeft && (wselectedIndex != 0 || wcyclic))
    {
      event.accepted = true;
      set_selectedIndex((wselectedIndex == 0) ? wvalues.size() - 1 : wselectedIndex - 1);
      if (wcb_clicked_left != nullptr) wcb_clicked_left(this);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
    if (wisHoveredRight && (wselectedIndex != wvalues.size() - 1 || wcyclic))
    {
      event.accepted = true;
      set_selectedIndex((wselectedIndex == wvalues.size() - 1) ? 0 : wselectedIndex + 1);
      if (wcb_clicked_left != nullptr) wcb_clicked_left(this);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
}

} // namespace ui

} // namespace tre
