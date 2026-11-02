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
    if (wisactive && wishighlighted)
    {
      if (wcb_loss_focus != nullptr) wcb_loss_focus(this);
      set_ishighlighted(false);
    }
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

bool widget::hasPointerCapture() const
{
  return m_parentWindow != nullptr && m_parentWindow->get_pointerCapture() == this;
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

// widgetText =================================================================

widget::s_drawElementCount widgetText::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6;
  res.m_vcountLine = (wwithborder==true)*4 * 2;
  res.m_vcountText = textgenerator::geometry_VertexCount(wtext.c_str());
  return res;
}
glm::vec2 widgetText::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());
  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), wtext.c_str());
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  return tOut.m_maxboxsize + glm::vec2(hLine - fsize) + 2.f * resolvePadding(dd);
}
glm::vec2 widgetText::resolvePadding(const s_drawData &dd) const
{
  if (!hasFrame()) return glm::vec2(0.f);
  return dd.resolve_sizeWH(m_parentWindow->get_colortheme().m_padding);
}
void widgetText::compute_data(s_drawData &dd)
{
  const glm::vec4 colorFront = resolveColorFront();
  const glm::vec4 colorBack = resolveColorBack();

  fillRect(dd.m_bufferSolid, m_zone, colorBack);

  if (wwithborder)
  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);

    glm::vec4 colLine = colorFront;
    if (wisactive) colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? -0.1f : 0.1f);
    fillLine(dd.m_bufferLine, pt00, pt10, colLine);
    fillLine(dd.m_bufferLine, pt10, pt11, colLine);
    if (wisactive) colLine = transformColor(colorFront, COLORTHEME_LIGHTNESS, wishighlighted ? 0.1f : -0.1f);
    fillLine(dd.m_bufferLine, pt11, pt01, colLine);
    fillLine(dd.m_bufferLine, pt01, pt00, colLine);
  }

  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());
  const float pad = 0.5f * (hLine - fsize);
  const glm::vec2 padding = resolvePadding(dd);

  textgenerator::s_textInfo tInfo;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), wtext.c_str());
  tInfo.m_zone = m_zone + glm::vec4(pad + padding.x, padding.y, -padding.x, -pad - padding.y);
  tInfo.m_color = colorFront;
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  textgenerator::generate(tInfo, & dd.m_model, m_adrText.part, m_adrText.offset, nullptr);
}
void widgetText::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}
glm::vec4 widgetText::resolveColorFront() const
{
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : (wisactive ? colorTheme.m_colorOnObject : colorTheme.m_colorOnSurface);
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}
glm::vec4 widgetText::resolveColorBack() const
{
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
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
glm::vec2 widgetTextTranslatate::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), nullptr);
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  glm::vec2 ret = glm::vec2(0.f);
  if (!wadjust)
  {
    for (const auto &txt : wtexts)
    {
      tInfo.m_text = txt.c_str();
      textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
      ret = glm::max(ret, tOut.m_maxboxsize);
    }
  }
  else
  {
      tInfo.m_text = wtexts[m_parentWindow->get_parentUI()->get_language()].c_str();
      textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
      ret = tOut.m_maxboxsize;
  }
  return ret + glm::vec2(hLine - fsize) + 2.f * resolvePadding(dd);
}
void widgetTextTranslatate::compute_data(s_drawData &dd)
{
  // we need to clear the text data because the trailing data is not cleared
  fillNull(dd.m_bufferText, get_drawElementCount().m_vcountText);

  wtext = wtexts[m_parentWindow->get_parentUI()->get_language()];
  widgetText::compute_data(dd);
}

// widgetTextEdit =============================================================

widget::s_drawElementCount widgetTextEdit::get_drawElementCount() const
{
  s_drawElementCount res = widgetText::get_drawElementCount();
  res.m_vcountSolid += 6;
  return res;
}
glm::vec2 widgetTextEdit::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;

  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), wtext.c_str());
  if (wtext.empty()) tInfo.m_text = " ";

  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  tInfo.m_boxExtendedToNextChar = true;

  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);

  return tOut.m_maxboxsize + 2.f * resolvePadding(dd);
}
void widgetTextEdit::compute_data(s_drawData &dd)
{
  widgetText::compute_data(dd);

  // dummy text to compute the cursor position

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

  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), txtDummy.c_str());
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  tInfo.m_boxExtendedToNextChar = true;
  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  const glm::vec2 textDim = tOut.m_maxboxsize;

  // draw cursor

  const glm::vec2 padding = resolvePadding(dd);
  const glm::vec4 cursorBox = glm::vec4(m_zone.x + padding.x + textDim.x - 0.05f * fsize, m_zone.w - padding.y - textDim.y,
                                        m_zone.x + padding.x + textDim.x + 0.05f * fsize, m_zone.w - padding.y - textDim.y + fsize);

  const float     cursorAlpha = (wisEditing && wcursorAnimTime <= 0.5f * wcursorAnimSpeed) ? 1.f : 0.f;
  const glm::vec4 cursorColor = resolveColorFront();
  fillRect(dd.m_bufferSolid, cursorBox, glm::vec4(cursorColor.r, cursorColor.g, cursorColor.b, cursorAlpha));
}
widgetTextEdit::~widgetTextEdit()
{
  if (wisEditing && m_parentWindow != nullptr) m_parentWindow->get_parentUI()->releaseTextInput(this);
}

void widgetTextEdit::startEditing()
{
  wisEditing = true;
  wcursorAnimTime = 0.f;
  m_parentWindow->get_parentUI()->acquireTextInput(this);
  setUpdateNeededData(); // show the cursor
}

void widgetTextEdit::stopEditing()
{
  wisEditing = false;
  m_parentWindow->get_parentUI()->releaseTextInput(this);
  setUpdateNeededData(); // hide the cursor
  if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
}

static bool _utf8IsContinuation(char c)
{
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

/// Only ASCII and the Latin-1 supplement (U+0080 - U+00FF, encoded with the lead-bytes 0xC2 or 0xC3) are implemented in the font.
static bool _textInputIsSupported(const char *txt)
{
  for (std::size_t i = 0; txt[i] != 0; ++i)
  {
    if (i >= 1024) return false; // input limit
    const unsigned char c = static_cast<unsigned char>(txt[i]);
    if (c < 0x80) continue;
    if ((c == 0xC2 || c == 0xC3) && _utf8IsContinuation(txt[i + 1]))
    {
      ++i;
      continue;
    }
    return false;
  }
  return true;
}

void widgetTextEdit::acceptEvent(s_eventIntern &event)
{
  if (!wisactive)
  {
    if (wisEditing) stopEditing();
    return;
  }

  if (!wisEditing)
  {
    acceptEventBase_focus(event);
    if (wishighlighted && event.isNewPress(SDL_BUTTON_LMASK) && !event.cancel)
    {
      startEditing();
      event.accepted = true;
    }
    return;
  }

  // stop editing ?

  if (event.cancel)
  {
    acceptEventBase_focus(event);
    stopEditing();
    return;
  }
  if (event.isNewPress())
  {
    acceptEventBase_focus(event);
    if (!wishighlighted) // clicked elsewhere
    {
      stopEditing();
      return;
    }
  }
  if (event.keyDown == SDLK_ESCAPE || ((event.keyDown == SDLK_RETURN || event.keyDown == SDLK_KP_ENTER) && !wallowMultiLines))
  {
    event.accepted = true;
    stopEditing();
    return;
  }

  // editing ...

  if (wcursorPos >= int(wtext.size())) wcursorPos = -1; // can occur if the text is changed from the code.

  bool isModified = false;

  if (event.keyDown == SDLK_RETURN || event.keyDown == SDLK_KP_ENTER)
  {
    if (wcursorPos != -1)
      wtext.insert(wcursorPos++, "\n");
    else
      wtext += '\n';
    isModified = true;
    event.accepted = true;
  }
  else if (event.keyDown == SDLK_BACKSPACE) // erase the character before the cursor
  {
    const int posEnd = (wcursorPos == -1) ? int(wtext.size()) : wcursorPos;
    if (posEnd > 0)
    {
      int posBegin = posEnd - 1;
      while (posBegin > 0 && _utf8IsContinuation(wtext[posBegin])) --posBegin;
      wtext.erase(posBegin, posEnd - posBegin);
      if (wcursorPos != -1) wcursorPos = posBegin;
      isModified = true;
    }
    event.accepted = true;
  }
  else if (event.keyDown == SDLK_DELETE) // erase the character after the cursor (nothing at the end of the text)
  {
    if (wcursorPos != -1)
    {
      int posEnd = wcursorPos + 1;
      while (posEnd < int(wtext.size()) && _utf8IsContinuation(wtext[posEnd])) ++posEnd;
      wtext.erase(wcursorPos, posEnd - wcursorPos);
      if (wcursorPos >= int(wtext.size())) wcursorPos = -1;
      isModified = true;
    }
    event.accepted = true;
  }
  else if (event.keyDown == SDLK_LEFT)
  {
    if (wcursorPos != 0 && !wtext.empty())
    {
      if (wcursorPos == -1) wcursorPos = int(wtext.size());
      --wcursorPos;
      while (wcursorPos != 0 && _utf8IsContinuation(wtext[wcursorPos])) --wcursorPos;
    }
    event.accepted = true;
  }
  else if (event.keyDown == SDLK_RIGHT)
  {
    if (wcursorPos != -1)
    {
      ++wcursorPos;
      while (wcursorPos < int(wtext.size()) && _utf8IsContinuation(wtext[wcursorPos])) ++wcursorPos;
      if (wcursorPos >= int(wtext.size())) wcursorPos = -1;
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
    if (!_textInputIsSupported(event.textInput))
    {
      TRE_LOG("widgetTextEdit: Invalid or unsupported encoding of text input from SDL");
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
    isModified = true;
    event.accepted = true;
  }

  if (isModified)
  {
    if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    setUpdateNeededAddress();
    setUpdateNeededLayout();
  }
  if (event.accepted)
  {
    wcursorAnimTime = 0.f; // show the cursor while typing
    setUpdateNeededData();
  }
}
void widgetTextEdit::animate(float dt)
{
  widget::animate(dt);
  if (!wisEditing) return;
  const bool isVisiblePrev = wcursorAnimTime <= 0.5f * wcursorAnimSpeed;
  wcursorAnimTime = std::fmod(wcursorAnimTime + dt, wcursorAnimSpeed);
  const bool isVisibleNext = wcursorAnimTime <= 0.5f * wcursorAnimSpeed;
  if (isVisiblePrev != isVisibleNext) setUpdateNeededData();
}

// widgetPicture ==============================================================

widget::s_drawElementCount widgetPicture::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountPict = 6;
  res.m_textureSlot = wtexId;
  return res;
}
glm::vec2 widgetPicture::get_zoneSizeDefault(const s_drawData &dd) const
{
  const auto &texRef = m_parentWindow->get_parentUI()->m_textures[wtexId];
  if (texRef.m_handle == 0) return glm::vec2(0.f);

  const float ratioWoH = ( texRef.m_w * std::abs(wtexUV[2]-wtexUV[0]) ) /
                         ( texRef.m_h * std::abs(wtexUV[3]-wtexUV[1]) ) ;

  const float h = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());

  // TODO : 1-1 pixel or equivalent (see snapPixel)

  return glm::vec2(h * ratioWoH , h);
}
void widgetPicture::compute_data(s_drawData &dd)
{
  const glm::vec4 uvSwap = glm::vec4(wtexUV.x, wtexUV.w, wtexUV.z, wtexUV.y);
  const glm::vec4 colorMask = resolveColorFill();
  fillRect(dd.m_bufferPict, m_zone, colorMask, uvSwap);
}
void widgetPicture::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
}
glm::vec4 widgetPicture::resolveColorFill() const
{
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : glm::vec4(1.f);
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.2f : 0.f) * colorMask;
}

// widgetBar ==================================================================

widget::s_drawElementCount widgetBar::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6 * 2;
  res.m_vcountLine = ((wwithborder==true ? 4 : 0) + 2) * 2;
  res.m_vcountText = (wwithtext ? 16 * 6 : 0);
  return res;
}
glm::vec2 widgetBar::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float h = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetBar::compute_data(s_drawData &dd)
{
  const glm::vec4 colorPlain = resolveColorPlain();
  const glm::vec4 colorFill = resolveColorFill();
  const glm::vec4 colorLine = resolveColorOutLine();
  const glm::vec4 colorText = resolveColorText();

  const float valx2 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue-wvaluemin) / (wvaluemax-wvaluemin);
  const glm::vec4 zoneBar(m_zone.x,m_zone.y,valx2,m_zone.w);

  fillRect(dd.m_bufferSolid, m_zone, colorPlain);
  fillRect(dd.m_bufferSolid, zoneBar, colorFill);

  if (wvaluethreshold >= wvaluemin && wvaluethreshold <= wvaluemax)
  {
    const float valxt = m_zone.x + (m_zone.z-m_zone.x) * (wvaluethreshold-wvaluemin) / (wvaluemax-wvaluemin);
    if (wwithtext) // 2 ticks, so the marker does not cross the text
    {
      const float tickH = 0.18f * (m_zone.w - m_zone.y);
      fillLine(dd.m_bufferLine, glm::vec2(valxt, m_zone.y), glm::vec2(valxt, m_zone.y + tickH), colorLine);
      fillLine(dd.m_bufferLine, glm::vec2(valxt, m_zone.w - tickH), glm::vec2(valxt, m_zone.w), colorLine);
    }
    else
    {
      fillLine(dd.m_bufferLine, glm::vec2(valxt,0.8f * m_zone.y + 0.2f * m_zone.w), glm::vec2(valxt,0.2f * m_zone.y + 0.8f * m_zone.w), colorLine);
      fillNull(dd.m_bufferLine, 2);
    }
  }
  else
  {
    fillNull(dd.m_bufferLine, 4);
  }
  if (wwithborder)
  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);

    glm::vec4 colorBorder = colorLine;
    if (wisactive) colorBorder = transformColor(colorLine, COLORTHEME_LIGHTNESS, wishighlighted ? -0.1f : 0.1f);
    fillLine(dd.m_bufferLine, pt00, pt10, colorBorder);
    fillLine(dd.m_bufferLine, pt10, pt11, colorBorder);
    if (wisactive) colorBorder = transformColor(colorLine, COLORTHEME_LIGHTNESS, wishighlighted ? 0.1f : -0.1f);
    fillLine(dd.m_bufferLine, pt11, pt01, colorBorder);
    fillLine(dd.m_bufferLine, pt01, pt00, colorBorder);
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
    tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), curtxt.data());

    const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
    const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());
    tInfo.setupSize(hLine, fsize);
    tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

    tInfo.m_zone = m_zone;
    tInfo.m_color = colorText;

    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    const glm::vec2 txtSize = tOut.m_maxboxsize;
    if (txtSize.x < m_zone.z - m_zone.x)
    {
      const float centerX = 0.5f * (m_zone.z + m_zone.x);
      tInfo.m_zone = glm::vec4(centerX - 0.5f * txtSize.x,m_zone.y, centerX + 0.6f * txtSize.x, m_zone.w);
    }

    fillNull(dd.m_bufferText, 16 * 6);
    textgenerator::generate(tInfo, & dd.m_model, m_adrText.part, m_adrText.offset, nullptr);
  }
}
void widgetBar::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  acceptEventBase_focus(event);

  if (!wiseditable) return;

  // drag: from the press on the widget (the window gives the pointer-capture after this call), until the release
  const bool isStarting = event.isNewPress(SDL_BUTTON_LMASK) && wishighlighted && !event.cancel;
  const bool isDragging = hasPointerCapture() && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isEnding   = hasPointerCapture() && event.isRelease(SDL_BUTTON_LMASK);
  const int  wheel     = wishighlighted ? wheelSteps(event) : 0;

  if (isStarting || isDragging || isEnding)
  {
    float newvalue = wvaluemin + (event.mousePos.x - m_zone.x) / (m_zone.z - m_zone.x) * (wvaluemax - wvaluemin);
    newvalue = glm::clamp(newvalue, wvaluemin, wvaluemax);
    if (wsnapInterval > 0.f) newvalue = std::roundf(newvalue / wsnapInterval) * wsnapInterval;
    set_value(newvalue);
    event.accepted = true;

    if (!isEnding)
    {
      if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
    else
    {
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
  else if (wheel != 0)
  {
    const float step = (wsnapInterval > 0.f) ? wsnapInterval : 0.05f * (wvaluemax - wvaluemin);
    float newvalue = glm::clamp(wvalue + wheel * step, wvaluemin, wvaluemax);
    if (wsnapInterval > 0.f) newvalue = std::roundf(newvalue / wsnapInterval) * wsnapInterval;
    event.accepted = true;
    if (newvalue != wvalue)
    {
      set_value(newvalue);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
}
glm::vec4 widgetBar::resolveColorPlain() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  return colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.07f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorFill() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const glm::vec4 &colorBase = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  return colorTheme.resolveColor(colorBase, wishighlighted ? 0.1f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorOutLine() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  return colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
}
glm::vec4 widgetBar::resolveColorText() const
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  return colorTheme.resolveColor(colorTheme.m_colorOnObject, wishighlighted ? 0.2f : 0.f) * colorMask;
}

// widgetBarZero ==============================================================

widget::s_drawElementCount widgetBarZero::get_drawElementCount() const
{
  s_drawElementCount res = widgetBar::get_drawElementCount();
  res.m_vcountLine += 2 * 2;
  return res;
}
void widgetBarZero::compute_data(s_drawData &dd)
{
  widgetBar::compute_data(dd);

  const glm::vec4 colorFront = resolveColorFill();
  const glm::vec4 colorZero = resolveColorOutLine();

  const float valx1 = m_zone.x + (m_zone.z-m_zone.x)*(0.f-wvaluemin)/(wvaluemax-wvaluemin);
  const float valx2 = valx1 + (m_zone.z-m_zone.x)*(wvalue-0.f)/(wvaluemax-wvaluemin);
  const glm::vec4 bar(valx1,m_zone.y,valx2,m_zone.w);
  dd.m_bufferSolid -= 6 * 2; // overwrite the widgetBar's fill-bar
  fillRect(dd.m_bufferSolid, bar, colorFront);
  // zero line
  if (wwithtext) // 2 ticks, so the line does not cross the text
  {
    const float tickH = 0.18f * (m_zone.w - m_zone.y);
    fillLine(dd.m_bufferLine, glm::vec2(valx1, m_zone.y), glm::vec2(valx1, m_zone.y + tickH), colorZero);
    fillLine(dd.m_bufferLine, glm::vec2(valx1, m_zone.w - tickH), glm::vec2(valx1, m_zone.w), colorZero);
  }
  else
  {
    fillLine(dd.m_bufferLine, glm::vec2(valx1, m_zone.y), glm::vec2(valx1, m_zone.w), colorZero);
    fillNull(dd.m_bufferLine, 2);
  }
}

// widgetSlider ===============================================================

static constexpr int kSliderCursorRaster = 12;
static constexpr float kSliderCursorRatio = 0.35f; ///< cursor radius, relative to the widget's height

static void _sliderGenCursor(const glm::vec2 &pos, const float radius,
                             const glm::vec4 &colorSolid, const glm::vec4 &colorLine,
                             glm::vec4 * __restrict &bufferSolid, glm::vec4 * __restrict &bufferLine)
{
  static_assert(kSliderCursorRaster % 2 == 0, "kSliderCursorRaster must be even");
  for (unsigned ip = 0; ip < kSliderCursorRaster / 2; ++ip)
  {
    const float aA = (float(ip      ) / float(kSliderCursorRaster)) * 2.f * 3.14159265f;
    const float aB = (float(ip + 1.f) / float(kSliderCursorRaster)) * 2.f * 3.14159265f;
    const float cA = radius * std::cos(aA);
    const float sA = radius * std::sin(aA);
    const float cB = radius * std::cos(aB);
    const float sB = radius * std::sin(aB);

    *bufferSolid++ = glm::vec4(pos.x     , pos.y     , 0.f, 0.f); *bufferSolid++ = colorSolid;
    *bufferSolid++ = glm::vec4(pos.x + cA, pos.y + sA, 0.f, 0.f); *bufferSolid++ = colorSolid;
    *bufferSolid++ = glm::vec4(pos.x + cB, pos.y + sB, 0.f, 0.f); *bufferSolid++ = colorSolid;

    *bufferSolid++ = glm::vec4(pos.x     , pos.y     , 0.f, 0.f); *bufferSolid++ = colorSolid;
    *bufferSolid++ = glm::vec4(pos.x - cA, pos.y - sA, 0.f, 0.f); *bufferSolid++ = colorSolid;
    *bufferSolid++ = glm::vec4(pos.x - cB, pos.y - sB, 0.f, 0.f); *bufferSolid++ = colorSolid;

    *bufferLine++ = glm::vec4(pos.x + cA, pos.y + sA, 0.f, 0.f); *bufferLine++ = colorLine;
    *bufferLine++ = glm::vec4(pos.x + cB, pos.y + sB, 0.f, 0.f); *bufferLine++ = colorLine;

    *bufferLine++ = glm::vec4(pos.x - cA, pos.y - sA, 0.f, 0.f); *bufferLine++ = colorLine;
    *bufferLine++ = glm::vec4(pos.x - cB, pos.y - sB, 0.f, 0.f); *bufferLine++ = colorLine;
  }
}

// ---------------------------------------------------------------------------

widget::s_drawElementCount widgetSlider::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 2 * 6 + kSliderCursorRaster * 3;
  res.m_vcountLine = 4 * 2 + kSliderCursorRaster * 2;
  res.m_vcountText = (wwithtext ? 16 * 6 : 0);
  return res;
}
glm::vec2 widgetSlider::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float h = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetSlider::compute_data(s_drawData &dd)
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const glm::vec4 colorBack = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.07f : 0.f) * colorMask;
  const glm::vec4 &colorBaseRaw = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  const glm::vec4 colorBase = colorTheme.resolveColor(colorBaseRaw, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorLine = colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;

  const float cursorRadius = kSliderCursorRatio * (m_zone.w - m_zone.y); // TODO: align 0.15*r with pixel-size
  const float xVal  = m_zone.x + cursorRadius + (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvalue - wvaluemin) / (wvaluemax - wvaluemin);
  const float ycenter = 0.5f * (m_zone.y + m_zone.w);

  const glm::vec4 zoneBack = glm::vec4(m_zone.x + cursorRadius,
                                       ycenter - 0.15f * cursorRadius,
                                       m_zone.z - cursorRadius,
                                       ycenter + 0.15f * cursorRadius);

  const float r15 = cursorRadius * (1.f - std::tan(0.15f));
  float xVal1 = std::max(xVal - r15, zoneBack.x);
  float xVal2 = std::min(xVal + r15, zoneBack.z);

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
      std::snprintf(const_cast<char*>(curtxt.data()), 15, "%.1f", wvalue);
    }

    textgenerator::s_textInfo tInfo;
    textgenerator::s_textInfoOut tOut;
    tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), curtxt.data());

    const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
    const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());
    tInfo.setupSize(hLine, fsize);
    tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

    tInfo.m_zone = m_zone;
    tInfo.m_color = colorLine;

    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    const glm::vec2 txtSize = tOut.m_maxboxsize;
    const float dxCT = 1.5f * cursorRadius;
    if (xVal + dxCT + txtSize.x < m_zone.z)
    {
      tInfo.m_zone = glm::vec4(xVal + dxCT, m_zone.y, m_zone.z, m_zone.w);
      xVal2 = std::min(xVal + dxCT + 1.1f * txtSize.x, zoneBack.z);
    }
    else if (m_zone.x + txtSize.x < xVal - dxCT)
    {
      tInfo.m_zone = glm::vec4(xVal - dxCT - txtSize.x, m_zone.y, xVal - dxCT + 1.1f * txtSize.x, m_zone.w);
      xVal1 = std::max(xVal - dxCT - 1.1f * txtSize.x, zoneBack.x);
    }
    else if (m_zone.x + txtSize.x < m_zone.z)
    {
      const float centerX = 0.5f * (m_zone.z + m_zone.x);
      tInfo.m_zone = glm::vec4(centerX - 0.5f * txtSize.x,m_zone.y, centerX + 0.6f * txtSize.x, m_zone.w);
      xVal1 = std::max(xVal - 0.55f * txtSize.x, zoneBack.x);
      xVal2 = std::min(xVal + 0.55f * txtSize.x, zoneBack.z);
    }

    fillNull(dd.m_bufferText, 16 * 6);
    textgenerator::generate(tInfo, & dd.m_model, m_adrText.part, m_adrText.offset, nullptr);
  }

  fillRect(dd.m_bufferSolid, glm::vec4(zoneBack.x, zoneBack.y, xVal1, zoneBack.w), wfillLeft ? colorBase : colorBack);
  fillRect(dd.m_bufferSolid, glm::vec4(xVal2, zoneBack.y, zoneBack.z, zoneBack.w), colorBack);

  _sliderGenCursor(glm::vec2(xVal, ycenter), cursorRadius, colorBase, colorLine, dd.m_bufferSolid, dd.m_bufferLine);

  glm::vec4 colorBorder = colorBase;
  if (wisactive) colorBorder = transformColor(colorBase, COLORTHEME_LIGHTNESS, wishighlighted ?  0.1f : -0.1f);
  fillLine(dd.m_bufferLine, glm::vec2(zoneBack.x, zoneBack.w), glm::vec2(xVal1, zoneBack.w), colorBorder);
  fillLine(dd.m_bufferLine, glm::vec2(xVal2, zoneBack.w), glm::vec2(zoneBack.z, zoneBack.w), colorBorder);
  if (wisactive) colorBorder = transformColor(colorBase, COLORTHEME_LIGHTNESS, wishighlighted ? -0.1f :  0.1f);
  fillLine(dd.m_bufferLine, glm::vec2(zoneBack.x, zoneBack.y), glm::vec2(xVal1, zoneBack.y), colorBorder);
  fillLine(dd.m_bufferLine, glm::vec2(xVal2, zoneBack.y), glm::vec2(zoneBack.z, zoneBack.y), colorBorder);
}
void widgetSlider::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  acceptEventBase_focus(event);

  if (!wiseditable) return;

  // drag: from the press on the widget (the window gives the pointer-capture after this call), until the release
  const bool isStarting = event.isNewPress(SDL_BUTTON_LMASK) && wishighlighted && !event.cancel;
  const bool isDragging = hasPointerCapture() && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isEnding   = hasPointerCapture() && event.isRelease(SDL_BUTTON_LMASK);
  const int  wheel = wishighlighted ? wheelSteps(event) : 0;

  if (isStarting || isDragging || isEnding)
  {
    const float cursorRadius = kSliderCursorRatio * (m_zone.w - m_zone.y);
    float newvalue = wvaluemin + (event.mousePos.x - m_zone.x - cursorRadius) / (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvaluemax - wvaluemin);
    newvalue = glm::clamp(newvalue, wvaluemin, wvaluemax);
    set_value(newvalue);
    event.accepted = true;

    if (!isEnding)
    {
      if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
    else
    {
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
  else if (wheel != 0)
  {
    const float newvalue = glm::clamp(wvalue + wheel * 0.05f * (wvaluemax - wvaluemin), wvaluemin, wvaluemax);
    event.accepted = true;
    if (newvalue != wvalue)
    {
      set_value(newvalue);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
}

// widgetSliderInt =============================================================

widget::s_drawElementCount widgetSliderInt::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 2 * 6 + kSliderCursorRaster * 3;
  res.m_vcountLine = 4 * 2 + kSliderCursorRaster * 2;
  res.m_vcountText = (wwithtext ? 16 * 6 : 0);
  if (wwithMarkers)
  {
    const int nMarkers = wvaluemax - wvaluemin + 1;
    res.m_vcountLine += nMarkers * 2;
  }
  return res;
}
glm::vec2 widgetSliderInt::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float h = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  return glm::vec2(wwidthFactor * h, h);
}
void widgetSliderInt::compute_data(s_drawData &dd)
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const glm::vec4 colorBack = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.07f : 0.f) * colorMask;
  const glm::vec4 &colorBaseRaw = (wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorPrimary;
  const glm::vec4 colorBase = colorTheme.resolveColor(colorBaseRaw, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorFront = colorTheme.resolveColor(colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorLine = colorTheme.m_colorOnSurface * colorMask;

  const float cursorRadius = kSliderCursorRatio * (m_zone.w - m_zone.y); // TODO: align 0.15*r with pixel-size
  const float xVal  = m_zone.x + cursorRadius + (m_zone.z - m_zone.x - 2.f * cursorRadius) * (wvalue - wvaluemin) / (wvaluemax - wvaluemin);
  const float ycenter = 0.5f * (m_zone.y + m_zone.w);

  const glm::vec4 zoneBack = glm::vec4(m_zone.x + cursorRadius,
                                       ycenter - 0.15f * cursorRadius,
                                       m_zone.z - cursorRadius,
                                       ycenter + 0.15f * cursorRadius);

  const float r15 = cursorRadius * (1.f - std::tan(0.15f));
  float xVal1 = std::max(xVal - r15, zoneBack.x);
  float xVal2 = std::min(xVal + r15, zoneBack.z);

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
      std::snprintf(const_cast<char*>(curtxt.data()), 15, "%d", wvalue);
    }

    textgenerator::s_textInfo tInfo;
    textgenerator::s_textInfoOut tOut;
    tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), curtxt.data());

    const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
    const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());
    tInfo.setupSize(hLine, fsize);
    tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

    tInfo.m_zone = m_zone;
    tInfo.m_color = colorLine;

    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    const glm::vec2 txtSize = tOut.m_maxboxsize;
    const float dxCT = 1.5f * cursorRadius;
    if (xVal + dxCT + txtSize.x < m_zone.z)
    {
      tInfo.m_zone = glm::vec4(xVal + dxCT, m_zone.y, m_zone.z, m_zone.w);
      xVal2 = std::min(xVal + dxCT + 1.1f * txtSize.x, zoneBack.z);
    }
    else if (m_zone.x + txtSize.x < xVal - dxCT)
    {
      tInfo.m_zone = glm::vec4(xVal - dxCT - txtSize.x, m_zone.y, xVal - dxCT + 1.1f * txtSize.x, m_zone.w);
      xVal1 = std::max(xVal - dxCT - 1.1f * txtSize.x, zoneBack.x);
    }
    else if (m_zone.x + txtSize.x < m_zone.z)
    {
      const float centerX = 0.5f * (m_zone.z + m_zone.x);
      tInfo.m_zone = glm::vec4(centerX - 0.5f * txtSize.x,m_zone.y, centerX + 0.6f * txtSize.x, m_zone.w);
      xVal1 = std::max(xVal - 0.55f * txtSize.x, zoneBack.x);
      xVal2 = std::min(xVal + 0.55f * txtSize.x, zoneBack.z);
    }

    fillNull(dd.m_bufferText, 16 * 6);
    textgenerator::generate(tInfo, & dd.m_model, m_adrText.part, m_adrText.offset, nullptr);
  }

  fillRect(dd.m_bufferSolid, glm::vec4(zoneBack.x, zoneBack.y, xVal1, zoneBack.w), wfillLeft ? colorFront : colorBack);
  fillRect(dd.m_bufferSolid, glm::vec4(xVal2, zoneBack.y, zoneBack.z, zoneBack.w), colorBack);

  _sliderGenCursor(glm::vec2(xVal, ycenter), cursorRadius, colorBase, colorLine, dd.m_bufferSolid, dd.m_bufferLine);

  if (wwithMarkers)
  {
    const int nMarkers = wvaluemax - wvaluemin + 1;
    for (int i = 0; i < nMarkers; ++i)
    {
      const float xMarker = m_zone.x + cursorRadius + (m_zone.z - m_zone.x - 2.f * cursorRadius) * float(i) / float(nMarkers - 1);
      const glm::vec4 cMarker = (wvaluemin + i == wvalue || (xVal1 <= xMarker && xMarker <= xVal2)) ? glm::vec4(0.f) : colorBack;
      fillLine(dd.m_bufferLine, glm::vec2(xMarker, ycenter - 0.8f * cursorRadius), glm::vec2(xMarker, ycenter + 0.8f * cursorRadius), cMarker);
    }
  }


  glm::vec4 colorBorder = colorBase;
  if (wisactive) colorBorder = transformColor(colorBase, COLORTHEME_LIGHTNESS, wishighlighted ?  0.1f : -0.1f);
  fillLine(dd.m_bufferLine, glm::vec2(zoneBack.x, zoneBack.w), glm::vec2(xVal1, zoneBack.w), colorBorder);
  fillLine(dd.m_bufferLine, glm::vec2(xVal2, zoneBack.w), glm::vec2(zoneBack.z, zoneBack.w), colorBorder);
  if (wisactive) colorBorder = transformColor(colorBase, COLORTHEME_LIGHTNESS, wishighlighted ? -0.1f :  0.1f);
  fillLine(dd.m_bufferLine, glm::vec2(zoneBack.x, zoneBack.y), glm::vec2(xVal1, zoneBack.y), colorBorder);
  fillLine(dd.m_bufferLine, glm::vec2(xVal2, zoneBack.y), glm::vec2(zoneBack.z, zoneBack.y), colorBorder);
}
void widgetSliderInt::acceptEvent(s_eventIntern &event)
{
  if (!wisactive) return;

  acceptEventBase_focus(event);

  if (!wiseditable) return;

  const bool isStarting = event.isNewPress(SDL_BUTTON_LMASK) && wishighlighted && !event.cancel;
  const bool isDragging = hasPointerCapture() && (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0;
  const bool isEnding   = hasPointerCapture() && event.isRelease(SDL_BUTTON_LMASK);
  const int wheel = wishighlighted ? wheelSteps(event) : 0;

  if (isStarting || isDragging || isEnding)
  {
    const float cursorRadius = kSliderCursorRatio * (m_zone.w - m_zone.y);
    int newvalue = wvaluemin + int((event.mousePos.x - m_zone.x - cursorRadius) / (m_zone.z - m_zone.x - 2.f * cursorRadius) * float(wvaluemax - wvaluemin) + 0.5f);
    newvalue = glm::clamp(newvalue, wvaluemin, wvaluemax);
    set_value(newvalue);
    event.accepted = true;

    if (!isEnding)
    {
      if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
    else
    {
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
  else if (wheel != 0)
  {
    const int newvalue = glm::clamp(wvalue + wheel, wvaluemin, wvaluemax);
    event.accepted = true;
    if (newvalue != wvalue)
    {
      set_value(newvalue);
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
  else if (wishighlighted && wheelSteps(event) != 0)
  {
    event.accepted = true; // fractional wheel motion (touch-pad): accumulated
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
glm::vec2 widgetBoxCheck::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float h = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  return glm::vec2(h, h);
}
void widgetBoxCheck::compute_data(s_drawData &dd)
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const glm::vec4 &colorBG_raw = wcolor.a >= 0.f ? wcolor : (wvalue ? colorTheme.m_colorPrimary : colorTheme.m_colorSurface);
  const glm::vec4 &colorBG = colorTheme.resolveColor(colorBG_raw, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorBorder = colorTheme.m_colorOnSurface * colorMask;
  const glm::vec4 colorCross = colorTheme.resolveColor(colorTheme.m_colorOnObject, wishighlighted ? 0.2f : 0.f) * colorMask * (wvalue ? 1.f : 0.f);

  fillRect(dd.m_bufferSolid, m_zone, colorBG);

  const glm::vec2 dXY = glm::vec2(m_zone.z - m_zone.x, m_zone.w - m_zone.y);
  const float fExt = wthin;
  const float fInt = fExt + wthin;

  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fInt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;

  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fInt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;

  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fInt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;

  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fExt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + fInt * dXY.x, m_zone.w - fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - fExt * dXY.x, m_zone.y + fExt * dXY.y, 0.f, 0.f); *dd.m_bufferSolid++ = colorCross;

  {
    const glm::vec2 pt00(m_zone.x, m_zone.y);
    const glm::vec2 pt01(m_zone.x, m_zone.w);
    const glm::vec2 pt11(m_zone.z, m_zone.w);
    const glm::vec2 pt10(m_zone.z, m_zone.y);
    glm::vec4 colorB = colorBorder;
    if (wisactive) colorB = transformColor(colorBorder, COLORTHEME_LIGHTNESS, wishighlighted ? 0.1f : -0.1f);
    fillLine(dd.m_bufferLine, pt11, pt01, colorB);
    fillLine(dd.m_bufferLine, pt01, pt00, colorB);
    if (wisactive) colorB = transformColor(colorBorder, COLORTHEME_LIGHTNESS, wishighlighted ? -0.1f : 0.1f);
    fillLine(dd.m_bufferLine, pt00, pt10, colorBorder);
    fillLine(dd.m_bufferLine, pt10, pt11, colorBorder);
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

// widgetSeparator ============================================================

widget::s_drawElementCount widgetSeparator::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6;
  return res;
}
glm::vec2 widgetSeparator::get_zoneSizeDefault(const s_drawData &dd) const
{
  const glm::vec2 thickness = dd.resolve_sizeWH(wthickness);
  if (wvertical)
    return glm::vec2(glm::max(thickness.x, wspacing * wheightModifier * dd.resolve_sizeW(m_parentWindow->get_lineHeight())), thickness.y);
  else
    return glm::vec2(thickness.x, glm::max(thickness.y, wspacing * wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight())));
}
void widgetSeparator::compute_data(s_drawData &dd)
{
  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);
  const glm::vec4 colorLine = ((wcolor.a >= 0.f) ? wcolor : colorTheme.m_colorOnSurface * glm::vec4(1.f, 1.f, 1.f, 0.4f)) * colorMask;

  const glm::vec2 thickness = dd.resolve_sizeWH(wthickness);
  const glm::vec2 center = 0.5f * glm::vec2(m_zone.x + m_zone.z, m_zone.y + m_zone.w);

  if (wvertical)
    fillRect(dd.m_bufferSolid, glm::vec4(center.x - 0.5f * thickness.x, m_zone.y, center.x + 0.5f * thickness.x, m_zone.w), colorLine);
  else
    fillRect(dd.m_bufferSolid, glm::vec4(m_zone.x, center.y - 0.5f * thickness.y, m_zone.z, center.y + 0.5f * thickness.y), colorLine);
}
void widgetSeparator::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
  acceptEventBase_click(event);
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
glm::vec2 widgetLineChoice::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), nullptr);
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  float textMaxWidth = 0.f;

  for (const std::string &t : wvalues)
  {
    tInfo.m_text = t.c_str();
    textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
    if (tOut.m_maxboxsize.x > textMaxWidth) textMaxWidth = tOut.m_maxboxsize.x;
  }

  return glm::vec2(textMaxWidth + fsize * 3.f, fsize) + glm::vec2(hLine - fsize);
}
void widgetLineChoice::compute_data(s_drawData &dd)
{
  if (wvalues.empty())
  {
    fillNull(dd.m_bufferSolid, 6);
    return;
  }
  if (wselectedIndex >= wvalues.size()) wselectedIndex = unsigned(wvalues.size()) - 1;

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto colorMask = m_parentWindow->get_colormask() * glm::vec4(1.f, 1.f, 1.f, wcolorAlpha);

  const glm::vec4 colorText = colorTheme.resolveColor(wcolor.a >= 0.f ? wcolor : colorTheme.m_colorOnSurface, wishighlighted ? 0.2f : 0.f) * colorMask;
  const glm::vec4 colorSelectors = (wcolor.a >= 0.f ? wcolor : colorTheme.m_colorOnObject) * colorMask;

  const float yCenter = 0.5f * (m_zone.y + m_zone.w);
  const float ySize = (m_zone.w - m_zone.y);

  const glm::vec4 colorL = colorTheme.resolveColor(colorSelectors, (wiseditable && (wselectedIndex != 0 || wcyclic)) ? (wisHoveredLeft ? 0.2f : 0.f) : -0.2f);

  *dd.m_bufferSolid++ = glm::vec4(m_zone.x, yCenter, 0.f, 0.f); *dd.m_bufferSolid++ = colorL;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + 0.8f * ySize, yCenter - 0.4f * ySize, 0.f, 0.f); *dd.m_bufferSolid++ = colorL;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.x + 0.8f * ySize, yCenter + 0.4f * ySize, 0.f, 0.f); *dd.m_bufferSolid++ = colorL;

  const glm::vec4 colorR = colorTheme.resolveColor(colorSelectors, (wiseditable && (wselectedIndex != wvalues.size()-1 || wcyclic)) ? (wisHoveredRight ? 0.2f : 0.f) : -0.2f);

  *dd.m_bufferSolid++ = glm::vec4(m_zone.z, yCenter, 0.f, 0.f); *dd.m_bufferSolid++ = colorR;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - 0.8f * ySize, yCenter + 0.4f * ySize, 0.f, 0.f); *dd.m_bufferSolid++ = colorR;
  *dd.m_bufferSolid++ = glm::vec4(m_zone.z - 0.8f * ySize, yCenter - 0.4f * ySize, 0.f, 0.f); *dd.m_bufferSolid++ = colorR;

  fillNull(dd.m_bufferText, get_drawElementCount().m_vcountText);

  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), wvalues[wselectedIndex].c_str());
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
  const float xCenter = 0.5f * (m_zone.x + m_zone.z);
  const glm::vec2 textSize = tOut.m_maxboxsize;
  tInfo.m_zone = glm::vec4(xCenter - 0.5f * textSize.x, m_zone.y, xCenter + 0.55f * textSize.x, m_zone.w);
  tInfo.m_color = colorText;
  textgenerator::generate(tInfo, & dd.m_model, m_adrText.part, m_adrText.offset, nullptr);
}
void widgetLineChoice::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);

  if (!wisactive || !wishighlighted || wvalues.empty())
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
      set_selectedIndex((wselectedIndex == 0) ? unsigned(wvalues.size()) - 1 : wselectedIndex - 1);
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

  if (wiseditable && wheelSteps(event) != 0) // wheel: up selects the next value, down the previous one
  {
    event.accepted = true;
    const int steps = wheelSteps(event);
    const int count = int(wvalues.size());
    int newIndex = int(wselectedIndex) + steps;
    if (wcyclic) newIndex = ((newIndex % count) + count) % count;
    else         newIndex = glm::clamp(newIndex, 0, count - 1);
    if (unsigned(newIndex) != wselectedIndex)
    {
      set_selectedIndex(unsigned(newIndex));
      if (wcb_modified_finished != nullptr) wcb_modified_finished(this);
      else if (wcb_modified_ongoing != nullptr) wcb_modified_ongoing(this);
    }
  }
}

// widgetLineChoiceTranslate ======================================================

widget::s_drawElementCount widgetLineChoiceTranslate::get_drawElementCount() const
{
  s_drawElementCount res = widgetLineChoice::get_drawElementCount();
  for (const auto &vs : wvaluesTr)
  {
    for (const std::string &txt : vs)
      res.m_vcountText = std::max(res.m_vcountText, unsigned(textgenerator::geometry_VertexCount(txt.c_str())));
  }
  return res;
}
glm::vec2 widgetLineChoiceTranslate::get_zoneSizeDefault(const s_drawData &dd) const
{
  const float hLine = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_lineHeight());
  const float fsize = wheightModifier * dd.resolve_sizeH(m_parentWindow->get_fontHeight());

  textgenerator::s_textInfo tInfo;
  textgenerator::s_textInfoOut tOut;
  tInfo.setupBasic(m_parentWindow->get_parentUI()->get_defaultFont(), nullptr);
  tInfo.setupSize(hLine, fsize);
  tInfo.m_fontPixelSize = unsigned(fsize / dd.m_pixelSize.y);

  float textMaxWidth = 0.f;
  for (const auto &vs : wvaluesTr)
  {
    for (const std::string &t : vs)
    {
      tInfo.m_text = t.c_str();
      textgenerator::generate(tInfo, nullptr, 0, 0, &tOut);
      if (tOut.m_maxboxsize.x > textMaxWidth) textMaxWidth = tOut.m_maxboxsize.x;
    }
  }
  return glm::vec2(textMaxWidth + fsize * 3.f, fsize) + glm::vec2(hLine - fsize);
}
void widgetLineChoiceTranslate::compute_data(s_drawData &dd)
{
  // we need to clear the text data because the trailing data is not cleared
  fillNull(dd.m_bufferText, get_drawElementCount().m_vcountText);

  wvalues = wvaluesTr[m_parentWindow->get_parentUI()->get_language()];
  if (wvalues.empty()) wvalues = wvaluesTr[0];
  widgetLineChoice::compute_data(dd);
}

} // namespace ui

} // namespace tre
