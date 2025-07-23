#include "tre_ui.h"

#include "tre_textgenerator.h"
#include "tre_texture.h"

namespace tre {

namespace ui {

static const glm::vec4 VEC4_ZERO = glm::vec4(0.f);

// widget =====================================================================

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
  const bool isPressed  = event.mouseButtonIsPressed != 0;
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
  event.accepted |= isFocused; // note: discard events for other widgets (for ex, another window behind). TODO: acceptedWeak / acceptedStrong
}

void widget::acceptEventBase_click(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool isClickedL = (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0;
  const bool isClickedR = (event.mouseButtonIsPressed & SDL_BUTTON_RMASK) != 0 && (event.mouseButtonPrev & SDL_BUTTON_RMASK) == 0;;

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

void widget::setUpdateNeededAddress() const
{
  if (m_parentWindow) m_parentWindow->setUpdateNeededAddress();
}

void widget::setUpdateNeededLayout() const
{
  if (m_parentWindow) m_parentWindow->setUpdateNeededLayout();
}

void widget::setUpdateNeededData() const
{
  if (m_parentWindow) m_parentWindow->setUpdateNeededData();
}

glm::vec4 * __restrict widget::getDrawBuffer_Solid() const
{
  return m_parentWindow->get_parentUI()->getDrawBuffer(m_parentWindow->get_parentUI()->getDrawModel().partInfo(m_adSolid.part).m_offset + m_adSolid.offset);
}

glm::vec4 * __restrict widget::getDrawBuffer_Line() const
{
  return m_parentWindow->get_parentUI()->getDrawBuffer(m_parentWindow->get_parentUI()->getDrawModel().partInfo(m_adrLine.part).m_offset + m_adrLine.offset);
}

glm::vec4 * __restrict widget::getDrawBuffer_Pict() const
{
  return m_parentWindow->get_parentUI()->getDrawBuffer(m_parentWindow->get_parentUI()->getDrawModel().partInfo(m_adrPict.part).m_offset + m_adrPict.offset);
}

glm::vec4 * __restrict widget::getDrawBuffer_Text() const
{
  return m_parentWindow->get_parentUI()->getDrawBuffer(m_parentWindow->get_parentUI()->getDrawModel().partInfo(m_adrText.part).m_offset + m_adrText.offset);
}

// widgetText =================================================================

widget::s_drawElementCount widgetText::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6;
  res.m_vcountLine = (wwithborder==true)*4 * 2;
  res.m_vcountText = textgenerator::geometry_VertexCount(wtext.c_str());
  return res;
}
glm::vec2 widgetText::get_zoneSizeDefault() const
{
  const float fsize = wfontsizeModifier * m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, wtext.c_str());
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);
  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  return tOut.m_maxboxsize;
}
void widgetText::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();

  const glm::vec4 colorFront = resolveColorFront();
  const glm::vec4 colorBack = resolveColorBack();

  fillRect(bufferSolid, m_zone, colorBack);

  if (wwithborder)
  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);

    glm::vec4 colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, (!wisactive || wishighlighted) ? -0.2f : 0.2f);

    fillLine(bufferLine, pt00, pt10, colLine);
    fillLine(bufferLine, pt10, pt11, colLine);

    colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);

    fillLine(bufferLine, pt11, pt01, colLine);
    fillLine(bufferLine, pt01, pt00, colLine);
  }

  const float fsize = wfontsizeModifier * m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());

  textgenerator::s_textInfo tInfo;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, wtext.c_str());
  tInfo.m_zone = m_zone;
  tInfo.m_color = colorFront;
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);

  textgenerator::generate(tInfo, & m_parentWindow->get_parentUI()->getDrawModel(), m_adrText.part, m_adrText.offset, nullptr);
}
void widgetText::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}
glm::vec4 widgetText::resolveColorFront() const
{
  const auto &colorMask = m_parentWindow->get_colormask();
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : (wisactive ? colorTheme.m_colorOnObject : colorTheme.m_colorOnSurface);
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}
glm::vec4 widgetText::resolveColorBack() const
{
  const auto &colorMask = m_parentWindow->get_colormask();
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorBase = (wcolorBackground.a >= 0.f) ? wcolorBackground : (wisactive ? colorTheme.m_colorPrimary : glm::vec4(0.f));
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}

// widgetTextTranslatate ======================================================

widget::s_drawElementCount widgetTextTranslatate::get_drawElementCount() const
{
  s_drawElementCount res = widgetText::get_drawElementCount();
  for (const auto &txt : wtexts)
    res.m_vcountText = std::max(res.m_vcountText, unsigned(textgenerator::geometry_VertexCount(txt.c_str())));
  return res;
}
glm::vec2 widgetTextTranslatate::get_zoneSizeDefault() const
{
  const float fsize = wfontsizeModifier * m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, nullptr);
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);
  glm::vec2 ret = glm::vec2(0.f);
  for (const auto &txt : wtexts)
  {
    tInfo.m_text = txt.c_str();
    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    ret = glm::max(ret, tOut.m_maxboxsize);
  }
  return ret;
}
void widgetTextTranslatate::compute_data()
{
  // we need to clear the text data because the trailing data is not cleared
  glm::vec4 * __restrict bufferText = getDrawBuffer_Text();
  fillNull(bufferText, get_drawElementCount().m_vcountText);

  wtext = wtexts[m_parentWindow->get_parentUI()->get_language()];
  widgetText::compute_data();
}
widgetTextTranslatate* widgetTextTranslatate::set_texts(tre::span<std::string> values)
{
  TRE_ASSERT(values.size() == wtexts.size());
  for (std::size_t i = 0; i < wtexts.size(); ++i) wtexts[i] = values[i];
  setUpdateNeededAddress();
  setUpdateNeededData();
  return this;
}
widgetTextTranslatate* widgetTextTranslatate::set_texts(tre::span<const char*> values)
{
  TRE_ASSERT(values.size() == wtexts.size());
  for (std::size_t i = 0; i < wtexts.size(); ++i) wtexts[i] = values[i];
  setUpdateNeededAddress();
  setUpdateNeededData();
  return this;
}
widgetTextTranslatate* widgetTextTranslatate::set_text_LangIdx(const std::string &str, std::size_t lidx)
{
  TRE_ASSERT(lidx < wtexts.size());
  wtexts[lidx] = str;
  setUpdateNeededData();
  return this;
}

// widgetTextEdit =============================================================

widget::s_drawElementCount widgetTextEdit::get_drawElementCount() const
{
  s_drawElementCount res = widgetText::get_drawElementCount();
  res.m_vcountSolid += 6;
  return res;
}
glm::vec2 widgetTextEdit::get_zoneSizeDefault() const
{
  const float fsize = wfontsizeModifier * m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, wtext.c_str());
  if (wtext.empty()) tInfo.m_text = " ";
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);
  tInfo.m_boxExtendedToNextChar = true;

  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);

  return tOut.m_maxboxsize;
}
void widgetTextEdit::compute_data()
{
  widgetText::compute_data();

  auto & objsolid = m_parentWindow->get_parentUI()->getDrawModel();

  // dummy text to compute the cursor position

  const float fsize = wfontsizeModifier * m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  const glm::vec2 pxsize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);

  std::string txtDummy = wtext;
  if (wcursorPos != -1 && wcursorPos < int(wtext.size()))
    txtDummy = txtDummy.substr(0, wcursorPos);

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

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, txtDummy.c_str());
  tInfo.m_pixelSize = pxsize;
  tInfo.m_boxExtendedToNextChar = true;
  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  const glm::vec2 textDim = tOut.m_maxboxsize;

  // draw cursor

  const glm::vec4 cursorBox = glm::vec4(m_zone.x + textDim.x - pxsize.x, m_zone.w - textDim.y,
                                        m_zone.x + textDim.x + pxsize.x, m_zone.w - textDim.y + fsize);

  const float     cursorAlpha = (wisEditing && wcursorAnimTime <= 0.5f * wcursorAnimSpeed) ? 1.f : 0.f;
  const glm::vec4 cursorColor = resolveColorFront();
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, cursorBox, cursorColor, VEC4_ZERO);
}
void widgetTextEdit::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  if (wisEditing)
  {
    TRE_ASSERT(wishighlighted == true);
    TRE_ASSERT(SDL_IsTextInputActive() == SDL_TRUE);

    if ((event.mouseButtonIsPressed & (~event.mouseButtonPrev)) != 0) // a new button was pressed
    {
      acceptEventBase_focus(event);
      wisEditing &= wishighlighted;
    }
    else if (event.keyDown == SDLK_ESCAPE)
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

    wisEditing = wishighlighted && (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;

    if (wisEditing) // start editing
    {
      SDL_StartTextInput();
      event.accepted = true;
    }
  }

  // editing ...
  if (wisEditing)
  {
    if (wcursorPos >= int(wtext.size())) wcursorPos = -1; // can occur if the text is changed from the code.

    if (event.keyDown == SDLK_RETURN)
    {
      if (wcursorPos != -1)
        wtext.insert(wcursorPos++, "\n");
      else
        wtext += '\n';
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_BACKSPACE)
    {
      if (!wtext.empty() && wcursorPos != 0)
      {
        int posLast = int(wtext.size());
        int &posErase = (wcursorPos == -1) ? posLast : wcursorPos;
        char cErased;
        do
        {
          --posErase;
          cErased = wtext[posErase];
          wtext.erase(posErase, 1);
        }
        while (cErased < 0 && (cErased & 0x40) == 0); // non-ASCII char, handle UFT-8 encoding
      }
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_DELETE)
    {
      if (wcursorPos != -1)
      {
        wtext.erase(wcursorPos, 1);
        while (wcursorPos < int(wtext.size()) && wtext[wcursorPos] < 0 && (wtext[wcursorPos] & 0x40) == 0) // non-ASCII char, handle UFT-8 encoding
        {
          wtext.erase(wcursorPos, 1);
        }
        if (wcursorPos >= int(wtext.size())) wcursorPos = -1;
      }
      else if (!wtext.empty()) // same as SDLK_BACKSPACE
      {
        int posErase = int(wtext.size());
        char cErased;
        do
        {
          --posErase;
          cErased = wtext[posErase];
          wtext.erase(posErase, 1);
        }
        while (cErased < 0 && (cErased & 0x40) != 0x40); // non-ASCII char, handle UFT-8 encoding
      }
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_LEFT)
    {
      if (wcursorPos != 0 && !wtext.empty())
      {
        if (wcursorPos == -1) wcursorPos = int(wtext.size());
        --wcursorPos;
        while (wcursorPos != 0 && wtext[wcursorPos] < 0 && (wtext[wcursorPos] & 0x40) == 0) // non-ASCII char, handle UFT-8 encoding
          --wcursorPos;
      }
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_RIGHT)
    {
      if (wcursorPos != -1)
      {
        ++wcursorPos;
        while (wcursorPos != int(wtext.size()) && wtext[wcursorPos] < 0 && (wtext[wcursorPos] & 0x40) == 0) // non-ASCII char, handle UFT-8 encoding
          ++wcursorPos;
        if (wcursorPos == int(wtext.size())) wcursorPos = -1;
      }
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_HOME)
    {
      if (!wtext.empty()) wcursorPos = 0;
      event.accepted = true;
    }
    else if (event.keyDown == SDLK_END)
    {
      wcursorPos = -1;
      event.accepted = true;
    }
    else if (event.keyDown != 0)
    {
      event.accepted = true; // other keys are catched by SDL_TEXTINPUT
    }
    else if (event.textInput != nullptr)
    {
      // check encoding (ASCII, UFT-8)
      bool isInputValid = true;
      int i = 0;
      for (; i < 1024 ; ++i) // input limit
      {
        if (event.textInput[i] == 0)
          break;
        else if (event.textInput[i] > 0)
          continue;
        else if ((event.textInput[i] & 0x40) != 0)
        {
          isInputValid &= ((event.textInput[i] & 0x3C) == 0); // only the latin-1 extension is implemented in the font.
          ++i; // advance once only
          isInputValid = (event.textInput[i] < 0) && ((event.textInput[i] & 0x40) == 0);
          continue;
        }
        isInputValid = false;
        break;
      }
      if (event.textInput[i] != 0 || !isInputValid)
      {
        TRE_LOG("widgetTextEdit: Invalid or unknown encoding of text input from SDL");
        return;
      }

      if (wcursorPos != -1)
      {
        wtext.insert(wcursorPos, event.textInput);
        wcursorPos += int(strlen(event.textInput));
      }
      else
      {
        wtext += event.textInput;
      }
      event.accepted = true;
    }

    if (event.accepted)
    {
      wtext = wtext.c_str();
      if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
      setUpdateNeededAddress();
      setUpdateNeededLayout();
      setUpdateNeededData();
    }
  }
}
void widgetTextEdit::animate(float dt)
{
  widget::animate(dt);
  wcursorAnimTime = std::fmod(wcursorAnimTime + dt, wcursorAnimSpeed);
  setUpdateNeededData();
}

// widgetPicture ==============================================================

widget::s_drawElementCount widgetPicture::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountPict = 6;
  res.m_textureSlot = wtexId;
  return res;
}
glm::vec2 widgetPicture::get_zoneSizeDefault() const
{
  const texture *tex = m_parentWindow->get_parentUI()->getTexture(wtexId);
  if (tex == nullptr)
    return m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);

  const float ratioWoH = ( tex->m_w * fabsf(wtexUV[2]-wtexUV[0]) ) /
                         ( tex->m_h * fabsf(wtexUV[3]-wtexUV[1]) ) ;

  const float h = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize()) * wheightModifier;

  // TODO : 1-1 pixel or equivalent (see snapPixel)

  return glm::vec2(h * ratioWoH , h);
}
void widgetPicture::compute_data()
{
  glm::vec4 * __restrict bufferPict = getDrawBuffer_Pict();
  const glm::vec4 uvSwap = glm::vec4(wtexUV.x, wtexUV.w, wtexUV.z, wtexUV.y);
  const glm::vec4 colorMask = resolveColorFill();
  fillRect(bufferPict, m_zone, colorMask, uvSwap);
}
void widgetPicture::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}
glm::vec4 widgetPicture::resolveColorFill() const
{
  const auto &colorMask = m_parentWindow->get_colormask();
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorActive = (wisactive && colorTheme.factor > 0.f) ? glm::vec4(0.9f, 0.9f, 0.9f, 1.f) : glm::vec4(1.f);
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : colorActive;
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}

// widgetBar ==================================================================

widget::s_drawElementCount widgetBar::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 12;
  res.m_vcountLine = ((wwithborder==true)*4+(wwiththreshold==true)*1) * 2;
  res.m_vcountText = (wwithtext ? 16 * 6 : 0);
  return res;
}
glm::vec2 widgetBar::get_zoneSizeDefault() const
{
  const float h = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetBar::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();
  glm::vec4 * __restrict bufferText = getDrawBuffer_Text();

  const glm::vec4 colorPlain = resolveColorPlain();
  const glm::vec4 colorFill = resolveColorFill();
  const glm::vec4 colorLine = resolveColorOutLine();
  const glm::vec4 colorText = resolveColorText();

  const float valx2 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue-wvaluemin) / (wvaluemax-wvaluemin);
  const glm::vec4 zoneBar(m_zone.x,m_zone.y,valx2,m_zone.w);

  fillRect(bufferSolid, m_zone, colorPlain);
  fillRect(bufferSolid, zoneBar, colorFill);

  uint lineOffset = m_adrLine.offset;
  if (wwiththreshold)
  {
    const float valxt = m_zone.x + (m_zone.z-m_zone.x) * (wvaluethreshold-wvaluemin) / (wvaluemax-wvaluemin);

    fillLine(bufferLine, glm::vec2(valxt,m_zone.y), glm::vec2(valxt,m_zone.w), colorLine);
    lineOffset += 2;
  }
  if (wwithborder)
  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);

    glm::vec4 colorBorder = transformColor(colorLine, COLORTHEME_LIGHTNESS, (!wisactive || wishighlighted) ? -0.2f : 0.2f);
    fillLine(bufferLine, pt00, pt10, colorBorder);
    fillLine(bufferLine, pt10, pt11, colorBorder);

    colorBorder = transformColor(colorLine, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);
    fillLine(bufferLine, pt11, pt01, colorBorder);
    fillLine(bufferLine, pt01, pt00, colorBorder);

    lineOffset += 8;
  }
  if (wwithtext)
  {
    std::string curtxt;
    if (wcb_valuePrinter)
    {
      curtxt = wcb_valuePrinter(wvalue);
      if (!curtxt.empty()) curtxt = curtxt.substr(0, 16); // limit to 16 characters
    }
    else
    {
      curtxt.resize(16);
      std::snprintf(const_cast<char*>(curtxt.data()), 15, "%.3f", wvalue);
    }

    textgenerator::s_textInfo tInfo;
    textgenerator::s_textInfoOut tOut;
    tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), m_zone.w-m_zone.y, curtxt.data());
    tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);
    tInfo.m_zone = m_zone;
    tInfo.m_color = colorText;

    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    const glm::vec2 txtSize = tOut.m_maxboxsize;
    if (txtSize.x < m_zone.z - m_zone.x)
    {
      const float centerX = 0.5f * (m_zone.z + m_zone.x);
      tInfo.m_zone = glm::vec4(centerX - 0.5f * txtSize.x,m_zone.y, centerX + 0.6f * txtSize.x, m_zone.w);
    }

    fillNull(bufferText, 16 * 6);

    textgenerator::generate(tInfo, & m_parentWindow->get_parentUI()->getDrawModel(), m_adrText.part, m_adrText.offset, nullptr);
  }
}
void widgetBar::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool canBeEdited = wiseditable && getIsOverPosition(event.mousePosPrev) && !event.accepted;
  const bool isPressedLeft = canBeEdited && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isReleasedLeft = canBeEdited && (event.mouseButtonPrev & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) == 0;

  if (isPressedLeft)
    wishighlighted = true;
  else
    acceptEventBase_focus(event);

  if (isPressedLeft || isReleasedLeft)
  {
    float newvalue = wvaluemin + (event.mousePos.x - m_zone.x) / (m_zone.z - m_zone.x) * (wvaluemax - wvaluemin);
    if (newvalue < wvaluemin) newvalue = wvaluemin;
    if (newvalue > wvaluemax) newvalue = wvaluemax;
    if (wsnapInterval > 0.f) newvalue = std::roundf(newvalue / wsnapInterval) * wsnapInterval;
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
glm::vec4 widgetBar::resolveColorPlain() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  return colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.07f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorFill() const
{
  const auto &colorMask = m_parentWindow->get_colormask();
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorOutLine() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  return colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorText() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  return colorTheme.resolveColor(colorTheme.m_colorOnObject, wishighlighted ? 0.2f : 0.f) * colorMask;
}

// widgetBarZero ==============================================================

widget::s_drawElementCount widgetBarZero::get_drawElementCount() const
{
  s_drawElementCount res = widgetBar::get_drawElementCount();
  res.m_vcountLine += 1 * 2;
  return res;
}
void widgetBarZero::compute_data()
{
  widgetBar::compute_data();

  auto & objsolid = m_parentWindow->get_parentUI()->getDrawModel();

  const glm::vec4 colorFront = resolveColorFill();
  const glm::vec4 colorZero = resolveColorOutLine();

  const float valx1 = m_zone.x + (m_zone.z-m_zone.x)*(0.f-wvaluemin)/(wvaluemax-wvaluemin);
  const float valx2 = valx1 + (m_zone.z-m_zone.x)*(wvalue-0.f)/(wvaluemax-wvaluemin);
  const glm::vec4 bar(valx1,m_zone.y,valx2,m_zone.w);
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, bar, colorFront, VEC4_ZERO); // overwrite the widgetBar's fill-bar
  // zero line
  const uint lineOffset = m_adrLine.offset + widgetBar::get_drawElementCount().m_vcountLine;
  const glm::vec2 pt1(valx1,m_zone.y);
  const glm::vec2 pt2(valx1,m_zone.w);
  objsolid.fillDataLine(m_adrLine.part, lineOffset, pt1, pt2, colorZero);
}

// widgetSlider ===============================================================

widget::s_drawElementCount widgetSlider::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 10 * 3;
  res.m_vcountLine = 8 * 2;
  return res;
}
glm::vec2 widgetSlider::get_zoneSizeDefault() const
{
  const float h = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetSlider::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  const glm::vec4 colorBack = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 &colorBaseRaw = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  const glm::vec4 colorBase = colorTheme.resolveColor(colorBaseRaw, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorFront = colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;

  const float cursorRadius = 0.35f * (m_zone.w - m_zone.y);
  const float ycenter = 0.5f * (m_zone.y + m_zone.w);

  const glm::vec4 zoneBack = glm::vec4(m_zone.x + cursorRadius,
                                       ycenter - 0.15f * cursorRadius,
                                       m_zone.z - cursorRadius,
                                       ycenter + 0.15f * cursorRadius);
  fillRect(bufferSolid, zoneBack, colorBack);

  const float xBase = m_zone.x + cursorRadius;
  const float xVal  = m_zone.x + cursorRadius + (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvalue - wvaluemin) / (wvaluemax - wvaluemin);
  if (xBase < xVal - cursorRadius)
    fillRect(bufferSolid, glm::vec4(xBase, zoneBack.y, xVal - cursorRadius, zoneBack.w), colorFront);
  else
    fillRect(bufferSolid, glm::vec4(0.f), glm::vec4(0.f));

  static const float sqrtHalf = 0.707f;
  const float        cursorIn = cursorRadius * sqrtHalf;

  {
    fillRect(bufferSolid, glm::vec4(xVal - cursorIn, ycenter - cursorIn, xVal + cursorIn, ycenter + cursorIn), colorFront);

    *bufferSolid++ = glm::vec4(xVal - cursorIn, ycenter - cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn, ycenter - cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal           , ycenter - cursorRadius, 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal - cursorIn, ycenter + cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn, ycenter + cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal           , ycenter + cursorRadius, 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal - cursorIn    , ycenter - cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal - cursorIn    , ycenter + cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal - cursorRadius, ycenter           , 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal + cursorIn    , ycenter - cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn    , ycenter + cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorRadius, ycenter           , 0.f, 0.f); *bufferSolid++ = colorFront;
  }

  const glm::vec4 colorBorderTop = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ?  0.2f : -0.2f);
  const glm::vec4 colorBorderBot = transformColor(colorFront, COLORTHEME_LIGHTNESS, (!wisactive || wishighlighted) ? -0.2f :  0.2f);

  const float xVal1 = std::max(xVal - cursorRadius, zoneBack.x);
  const float xVal2 = std::min(xVal + cursorRadius, zoneBack.z);

  fillLine(bufferLine, glm::vec2(zoneBack.x, zoneBack.w), glm::vec2(xVal1, zoneBack.w), colorBorderTop);
  fillLine(bufferLine, glm::vec2(xVal2, zoneBack.w), glm::vec2(zoneBack.z, zoneBack.w), colorBorderTop);

  fillLine(bufferLine, glm::vec2(zoneBack.x, zoneBack.y), glm::vec2(xVal1, zoneBack.y), colorBorderBot);
  fillLine(bufferLine, glm::vec2(xVal2, zoneBack.y), glm::vec2(zoneBack.z, zoneBack.y), colorBorderBot);

  fillLine(bufferLine, glm::vec2(xVal - cursorIn, ycenter + cursorIn), glm::vec2(xVal, ycenter + cursorRadius), colorBorderTop);
  fillLine(bufferLine, glm::vec2(xVal + cursorIn, ycenter + cursorIn), glm::vec2(xVal, ycenter + cursorRadius), colorBorderTop);

  fillLine(bufferLine, glm::vec2(xVal - cursorIn, ycenter - cursorIn), glm::vec2(xVal, ycenter - cursorRadius), colorBorderBot);
  fillLine(bufferLine, glm::vec2(xVal + cursorIn, ycenter - cursorIn), glm::vec2(xVal, ycenter - cursorRadius), colorBorderBot);
}
void widgetSlider::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool canBeEdited = wiseditable && getIsOverPosition(event.mousePosPrev) && !event.accepted;
  const bool isPressedLeft = canBeEdited && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isReleasedLeft = canBeEdited && (event.mouseButtonPrev & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) == 0;

  if (isPressedLeft)
    wishighlighted = true;
  else
    acceptEventBase_focus(event);

  if (isPressedLeft || isReleasedLeft)
  {
    const float cursorRadius = 0.4f * (m_zone.w - m_zone.y);
    float newvalue = wvaluemin + (event.mousePos.x - m_zone.x - cursorRadius) / (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvaluemax - wvaluemin);
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

// widgetSliderInt =============================================================

widget::s_drawElementCount widgetSliderInt::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 10 * 3;
  res.m_vcountLine = 8 * 2;
  return res;
}
glm::vec2 widgetSliderInt::get_zoneSizeDefault() const
{
  const float h = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetSliderInt::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  const glm::vec4 colorBack = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 &colorBaseRaw = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  const glm::vec4 colorBase = colorTheme.resolveColor(colorBaseRaw, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorFront = colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;

  const float cursorRadius = 0.35f * (m_zone.w - m_zone.y);
  const float ycenter = 0.5f * (m_zone.y + m_zone.w);

  const glm::vec4 zoneBack = glm::vec4(m_zone.x + cursorRadius,
                                       ycenter - 0.15f * cursorRadius,
                                       m_zone.z - cursorRadius,
                                       ycenter + 0.15f * cursorRadius);
  fillRect(bufferSolid, zoneBack, colorBack);

  const float xBase = m_zone.x + cursorRadius;
  const float xVal  = m_zone.x + cursorRadius + (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvalue - wvaluemin) / (wvaluemax - wvaluemin);
  if (xBase < xVal - cursorRadius)
    fillRect(bufferSolid, glm::vec4(xBase, zoneBack.y, xVal - cursorRadius, zoneBack.w), colorFront);
  else
    fillRect(bufferSolid, glm::vec4(0.f), glm::vec4(0.f));

  static const float sqrtHalf = 0.707f;
  const float        cursorIn = cursorRadius * sqrtHalf;

  {
    fillRect(bufferSolid, glm::vec4(xVal - cursorIn, ycenter - cursorIn, xVal + cursorIn, ycenter + cursorIn), colorFront, glm::vec4(0.f));

    *bufferSolid++ = glm::vec4(xVal - cursorIn, ycenter - cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn, ycenter - cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal           , ycenter - cursorRadius, 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal - cursorIn, ycenter + cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn, ycenter + cursorIn    , 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal           , ycenter + cursorRadius, 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal - cursorIn    , ycenter - cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal - cursorIn    , ycenter + cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal - cursorRadius, ycenter           , 0.f, 0.f); *bufferSolid++ = colorFront;

    *bufferSolid++ = glm::vec4(xVal + cursorIn    , ycenter - cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorIn    , ycenter + cursorIn, 0.f, 0.f); *bufferSolid++ = colorFront;
    *bufferSolid++ = glm::vec4(xVal + cursorRadius, ycenter           , 0.f, 0.f); *bufferSolid++ = colorFront;
  }

  const glm::vec4 colorBorderTop = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ?  0.2f : -0.2f);
  const glm::vec4 colorBorderBot = transformColor(colorFront, COLORTHEME_LIGHTNESS, (!wisactive || wishighlighted) ? -0.2f :  0.2f);

  const float xVal1 = std::max(xVal - cursorRadius, zoneBack.x);
  const float xVal2 = std::min(xVal + cursorRadius, zoneBack.z);

  fillLine(bufferLine, glm::vec2(zoneBack.x, zoneBack.w), glm::vec2(xVal1, zoneBack.w), colorBorderTop);
  fillLine(bufferLine, glm::vec2(xVal2, zoneBack.w), glm::vec2(zoneBack.z, zoneBack.w), colorBorderTop);

  fillLine(bufferLine, glm::vec2(zoneBack.x, zoneBack.y), glm::vec2(xVal1, zoneBack.y), colorBorderBot);
  fillLine(bufferLine, glm::vec2(xVal2, zoneBack.y), glm::vec2(zoneBack.z, zoneBack.y), colorBorderBot);

  fillLine(bufferLine, glm::vec2(xVal - cursorIn, ycenter + cursorIn), glm::vec2(xVal, ycenter + cursorRadius), colorBorderTop);
  fillLine(bufferLine, glm::vec2(xVal + cursorIn, ycenter + cursorIn), glm::vec2(xVal, ycenter + cursorRadius), colorBorderTop);

  fillLine(bufferLine, glm::vec2(xVal - cursorIn, ycenter - cursorIn), glm::vec2(xVal, ycenter - cursorRadius), colorBorderBot);
  fillLine(bufferLine, glm::vec2(xVal + cursorIn, ycenter - cursorIn), glm::vec2(xVal, ycenter - cursorRadius), colorBorderBot);
}
void widgetSliderInt::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  const bool canBeEdited = wiseditable && getIsOverPosition(event.mousePosPrev) && !event.accepted;
  const bool isPressedLeft = canBeEdited && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isReleasedLeft = canBeEdited && (event.mouseButtonPrev & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) == 0;

  if (isPressedLeft)
    wishighlighted = true;
  else
    acceptEventBase_focus(event);

  if (isPressedLeft || isReleasedLeft)
  {
    const float cursorRadius = 0.4f * (m_zone.w - m_zone.y);
    int newvalue = wvaluemin + int((event.mousePos.x - m_zone.x - cursorRadius) / (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvaluemax - wvaluemin) + 0.5f);
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

// widgetBoxCheck =============================================================

widget::s_drawElementCount widgetBoxCheck::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6 + 12;
  res.m_vcountLine = 8;
  return res;
}
glm::vec2 widgetBoxCheck::get_zoneSizeDefault() const
{
  const float h = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());
  return glm::vec2(h, h);
}
void widgetBoxCheck::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  const glm::vec4 &colorBG_raw = wcolor.a >= 0.f ? wcolor : (wvalue ? colorTheme.m_colorPrimary : colorTheme.m_colorSurface);
  const glm::vec4 &colorBG = colorTheme.resolveColor(colorBG_raw, wishighlighted ? 0.1f : 0.f) * colorMask;
  const glm::vec4 colorBorder = colorTheme.m_colorOnSurface * colorMask;
  const glm::vec4 colorCross = colorTheme.resolveColor(colorTheme.m_colorOnObject, wishighlighted ? 0.2f : 0.f) * colorMask * (wvalue ? 1.f : 0.f);

  fillRect(bufferSolid, m_zone, colorBG);

  const glm::vec2 dXY = glm::vec2(m_zone.z - m_zone.x, m_zone.w - m_zone.y);
  static const float fExt = wmargin;
  static const float fInt = wmargin + wthin;

  *bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.z - fInt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;

  *bufferSolid++ = glm::vec4(m_zone.x + fInt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;

  *bufferSolid++ = glm::vec4(m_zone.z - fInt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;

  *bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.x + fInt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;
  *bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *bufferSolid++ = colorCross;

  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);
    glm::vec4 colorB = transformColor(colorBorder, COLORTHEME_LIGHTNESS, wishighlighted ? 0.2f : -0.2f);
    fillLine(bufferLine, pt11, pt01, colorB);
    fillLine(bufferLine, pt01, pt00, colorB);
    if (wisactive) colorB = transformColor(colorBorder, COLORTHEME_LIGHTNESS, wishighlighted ? -0.2f : 0.2f);
    fillLine(bufferLine, pt00, pt10, colorBorder);
    fillLine(bufferLine, pt10, pt11, colorBorder);
  }
}
void widgetBoxCheck::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);

  if (!wisactive || !wishighlighted) return;

  const bool isClickLeft = (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;

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

widget::s_drawElementCount widgetLineChoice::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6;
  for (const std::string &t : wvalues)
    res.m_vcountText =  std::max(res.m_vcountText, unsigned(textgenerator::geometry_VertexCount(t.c_str())));
  return res;
}
glm::vec2 widgetLineChoice::get_zoneSizeDefault() const
{
  const float fsize = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, nullptr);
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);

  float textMaxWidth = 0.f;

  for (const std::string &t : wvalues)
  {
    tInfo.m_text = t.c_str();
    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    if (tOut.m_maxboxsize.x > textMaxWidth) textMaxWidth = tOut.m_maxboxsize.x;
  }

  return glm::vec2(textMaxWidth + fsize * 3.f, fsize);
}
void widgetLineChoice::compute_data()
{
  auto & objsolid = m_parentWindow->get_parentUI()->getDrawModel();
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();
  glm::vec4 * __restrict bufferLine = getDrawBuffer_Line();
  glm::vec4 * __restrict bufferText = getDrawBuffer_Text();

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();

  const glm::vec4 colorFront = colorTheme.resolveColor(wcolor.a >= 0.f ? wcolor : colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorSelectors = (wcolor.a >= 0.f ? wcolor : colorTheme.m_colorOnObject) * colorMask;

  const float yCenter = 0.5f * (m_zone.y + m_zone.w);
  const float ySize = (m_zone.w - m_zone.y);

  const glm::vec4 colorL = colorTheme.resolveColor(colorSelectors, (wiseditable && (wselectedIndex != 0 || wcyclic)) ? (wisHoveredLeft ? 0.2f : 0.f) : -0.2f);

  *bufferSolid++ = glm::vec4(m_zone.x, yCenter, 0.f, 0.f); *bufferSolid++ = colorL;
  *bufferSolid++ = glm::vec4(m_zone.x + 0.8f * ySize, yCenter - 0.4f * ySize, 0.f, 0.f); *bufferSolid++ = colorL;
  *bufferSolid++ = glm::vec4(m_zone.x + 0.8f * ySize, yCenter + 0.4f * ySize, 0.f, 0.f); *bufferSolid++ = colorL;

  const glm::vec4 colorR = colorTheme.resolveColor(colorSelectors, (wiseditable && (wselectedIndex != wvalues.size()-1 || wcyclic)) ? (wisHoveredRight ? 0.2f : 0.f) : -0.2f);

  *bufferSolid++ = glm::vec4(m_zone.z, yCenter, 0.f, 0.f); *bufferSolid++ = colorR;
  *bufferSolid++ = glm::vec4(m_zone.z - 0.8f * ySize, yCenter + 0.4f * ySize, 0.f, 0.f); *bufferSolid++ = colorR;
  *bufferSolid++ = glm::vec4(m_zone.z - 0.8f * ySize, yCenter - 0.4f * ySize, 0.f, 0.f); *bufferSolid++ = colorR;

  fillNull(bufferText, get_drawElementCount().m_vcountText);

  const float fsize = m_parentWindow->resolve_sizeH(m_parentWindow->get_fontSize());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), fsize, wvalues[wselectedIndex].c_str());
  tInfo.m_pixelSize = m_parentWindow->resolve_sizeWH(s_size::ONE_PIXEL);
  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  const float xCenter = 0.5f * (m_zone.x + m_zone.z);
  const glm::vec2 textSize = tOut.m_maxboxsize;
  tInfo.m_zone = glm::vec4(xCenter - 0.5f * textSize.x, m_zone.y, xCenter + 0.6f * textSize.x, m_zone.w);
  tInfo.m_color = colorFront;
  textgenerator::generate(tInfo, &objsolid, m_adrText.part, m_adrText.offset, nullptr);
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

  const bool isClickLeft = (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0 && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;

  const float yCenter = 0.5f * (m_zone.y + m_zone.w);
  const float ySize = (m_zone.w - m_zone.y);

  const glm::vec4 zoneL = glm::vec4(m_zone.x, yCenter - 0.4f * ySize, m_zone.x + ySize * 0.8f, yCenter + 0.4f * ySize);
  const bool hoveredLeft = (zoneL.x <= event.mousePos.x) && (zoneL.z >= event.mousePos.x) &&
                           (zoneL.y <= event.mousePos.y) && (zoneL.w >= event.mousePos.y) &&
                           wiseditable;
  if (wisHoveredLeft != hoveredLeft) setUpdateNeededData();
  wisHoveredLeft = hoveredLeft;

  const glm::vec4 zoneR = glm::vec4(m_zone.z - ySize * 0.8f, yCenter - 0.4f * ySize, m_zone.z, yCenter + 0.4f * ySize);
  const bool hoveredRight = (zoneR.x <= event.mousePos.x) && (zoneR.z >= event.mousePos.x) &&
                            (zoneR.y <= event.mousePos.y) && (zoneR.w >= event.mousePos.y) &&
                            wiseditable;
  if (wisHoveredRight != hoveredRight) setUpdateNeededData();
  wisHoveredRight = hoveredRight;

  if (isClickLeft)
  {
    if (wisHoveredLeft && (wselectedIndex != 0 || wcyclic))
    {
      event.accepted = true;
      set_selectedIndex((wselectedIndex == 0) ? uint(wvalues.size()) - 1 : wselectedIndex - 1);
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
widgetLineChoice* widgetLineChoice::set_values(tre::span<std::string> values)
{
  wvalues.clear();
  wvalues.reserve(values.size());
  for (const auto &v : values) wvalues.push_back(v);
  setUpdateNeededAddress();
  return this;
}
widgetLineChoice* widgetLineChoice::set_values(tre::span<const char *> values)
{
  wvalues.clear();
  wvalues.reserve(values.size());
  for (const auto &v : values) wvalues.push_back(v);
  setUpdateNeededAddress();
  return this;
}

} // namespace ui

} // namespace tre
