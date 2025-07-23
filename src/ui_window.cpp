#include "tre_ui.h"

namespace tre {

namespace ui {

// widget specific to window --------------------------------------------------

class widgetTextTitle : public widgetText
{
public:
  widgetTextTitle() : widgetText() {}
  virtual ~widgetTextTitle() override {}

  virtual s_drawElementCount get_drawElementCount() const override;
  virtual void compute_data() override;
};
widget::s_drawElementCount widgetTextTitle::get_drawElementCount() const
{
  s_drawElementCount res = widgetText::get_drawElementCount();
  res.m_vcountSolid = 2 * 6; // overwrite.
  return res;
}
void widgetTextTitle::compute_data()
{
  widgetText::compute_data();
  // process
  auto & objsolid = m_parentWindow->get_parentUI()->getDrawModel();

  const float sepY = 0.35f * m_zone.y + 0.65f * m_zone.w;

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();

  const glm::vec4 colorPlain1 = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.1f : 0.f) * colorMask;
  const glm::vec4 colorPlain2 = colorTheme.resolveColor(colorTheme.m_colorSurface, wishighlighted ? 0.f : -0.1f) * colorMask;
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 0, glm::vec4(m_zone.x,m_zone.y,m_zone.z,sepY   ), colorPlain2, glm::vec4(0.f));
  objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, glm::vec4(m_zone.x,sepY   ,m_zone.z,m_zone.w), colorPlain1, glm::vec4(0.f));
}

class widgetCloseButton : public widgetBoxCheck
{
public:
  widgetCloseButton() : widgetBoxCheck() {}
  virtual ~widgetCloseButton() override {}

  virtual s_drawElementCount get_drawElementCount() const override;
  virtual void compute_data() override;
};
widget::s_drawElementCount widgetCloseButton::get_drawElementCount() const
{
  s_drawElementCount res;
  res.m_vcountSolid = 6 + 12;
  return res;
}
void widgetCloseButton::compute_data()
{
  glm::vec4 * __restrict bufferSolid = getDrawBuffer_Solid();

  const auto &colorTheme = m_parentWindow->get_colortheme();
  const auto &colorMask = m_parentWindow->get_colormask();
  const glm::vec4 colorBG = (wishighlighted ? colorTheme.m_colorPrimary : glm::vec4(0.f)) * colorMask;
  const glm::vec4 colorCross = (wishighlighted ? colorTheme.m_colorOnObject : colorTheme.m_colorOnSurface) * colorMask;

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
}

// global properties ----------------------------------------------------------

void window::set_isactiveWindow(bool a_active)
{
  if (a_active == wisactive) return;
  if (a_active == false)
  {
    s_eventIntern event;
    event.accepted = true;
    acceptEvent(event);
  }
  wisactive = a_active;
}

void window::set_isvisibleWindow(bool a_visible)
{
  if (wisvisible == a_visible) return;
  wisvisible = a_visible;
  if (wisvisible == false)
  {
    s_eventIntern event;
    event.accepted = true;
    acceptEvent(event);
  }
}

window *window::set_topbar(const std::string &name, bool canBeMoved, bool canBeClosed)
{
  if (wOwnWidgets[0] != nullptr) delete(wOwnWidgets[0]);

  widgetTextTitle *wTextTitle = new widgetTextTitle;
  wOwnWidgets[0] = wTextTitle;
  wOwnWidgets[0]->m_parentWindow = this;
  wTextTitle->set_text(name)->set_isactive(canBeMoved);

  if (canBeClosed)
  {
    if (wOwnWidgets[1] != nullptr) delete(wOwnWidgets[1]);

    widgetCloseButton *wCloseBox = new widgetCloseButton;
    wOwnWidgets[1] = wCloseBox;
    wOwnWidgets[1]->m_parentWindow = this;
    wCloseBox->set_value(true);
    wCloseBox->set_isactive(true);
    wCloseBox->wcb_clicked_left = [this](widget *)
    {
      this->set_isvisibleWindow(false);
    };
  }

  m_isUpdateNeededAddress = true;

  return this;
}

window* window::set_topbarName(const std::string &name)
{
  if (wOwnWidgets[0] != nullptr) get_selfwidget_Title()->set_text(name);
  return this;
}

void window::set_colWidth(uint col, const s_size relWidth)
{
  TRE_ASSERT(col < wlayout.m_dimension.x);
  m_isUpdateNeededLayout |= (wlayout.m_colsWidth_User[col] == relWidth);
  wlayout.m_colsWidth_User[col] = relWidth;
}

void window::set_rowHeight(uint row, const s_size relHeight)
{
  TRE_ASSERT(row < wlayout.m_dimension.y);
  m_isUpdateNeededLayout |= (wlayout.m_rowsHeight_User[row] == relHeight);
  wlayout.m_rowsHeight_User[row] = relHeight;
}

void window::set_colAlignment(uint col, uint alignMask)
{
  TRE_ASSERT(col < wlayout.m_dimension.x);
  m_isUpdateNeededLayout = true;
  for (uint row = 0; row < wlayout.m_dimension.y; ++row)
    wlayout.m_cells[wlayout.index(row, col)].m_alignMask = alignMask;
}

void window::set_rowAlignment(uint row, uint alignMask)
{
  TRE_ASSERT(row < wlayout.m_dimension.y);
  m_isUpdateNeededLayout = true;
  for (uint col = 0; col < wlayout.m_dimension.x; ++col)
    wlayout.m_cells[wlayout.index(row, col)].m_alignMask = alignMask;
}

void window::set_colSpacement(uint col, const s_size width, const bool atLeft)
{
  TRE_ASSERT(col < wlayout.m_dimension.x);
  m_isUpdateNeededLayout = true;
  wlayout.m_colsInbetweenSpace[col + (atLeft ? 0u : 1u)] = width;
}

void window::set_rowSpacement(uint row, const s_size height, const bool atTop)
{
  TRE_ASSERT(row < wlayout.m_dimension.y);
  m_isUpdateNeededLayout = true;
  wlayout.m_rowsInbetweenSpace[row + (atTop ? 0u : 1u)] = height;
}

glm::vec2 window::resolve_sizeWH(s_size size) const
{
  if (size.unit == SIZE_NATIVE) return glm::vec2(size.size, size.size);

  TRE_ASSERT(size.unit == SIZE_PIXEL);
  if (get_parentUI()->get_dimension() == 2)
  {
    const baseUI2D *ui = static_cast<const baseUI2D*>(get_parentUI());
    return ui->projectWindowPointFromScreen(glm::vec2(size.size, size.size), wmat3, false);
  }
  else if (get_parentUI()->get_dimension() == 3)
  {
    const baseUI3D *ui = static_cast<const baseUI3D*>(get_parentUI());

    const glm::vec4 ptWin = glm::vec4(0.f, 0.f, 0.f, 1.f);
    const glm::vec4 ptClip = ui->get_matPV() * (wmat4 * ptWin);

    const glm::vec2 sizeViewport = size.size * 2.f / ui->get_viewport();
    return ptClip.w * sizeViewport;
  }
  else
  {
    TRE_FATAL("invalid space-dimension of the parent-UI");
  }
}

glm::vec2 window::resolve_pixelOffset() const
{
  if (get_parentUI()->get_dimension() == 2)
  {
    const baseUI2D *ui = static_cast<const baseUI2D*>(get_parentUI());
    glm::vec2 screenCoordSnaped = glm::floor(ui->projectWindowPointToScreen(glm::vec2(0.f), wmat3)); // not mandatory, but allow to return an offset closer to zero.
    return ui->projectWindowPointFromScreen(screenCoordSnaped + glm::vec2(0.5f, 0.5f) /* offset by half a pixel*/, wmat3);
  }

  return glm::vec2(0.f); // fallback to zero if not implemented or supported
}


// interface with widgets -----------------------------------------------------

void window::set_widget(widget * w, uint row, uint col, const uint span_row, const uint span_col)
{
  s_layoutGrid::s_cell &cell = wlayout.m_cells[wlayout.index(row, col)];

  if (cell.m_widget != nullptr) delete(cell.m_widget);

  TRE_ASSERT(w->m_parentWindow == nullptr); // widget is not already added in a window.

  cell.m_widget = w;
  cell.m_widget->m_parentWindow = this;
  cell.m_span = glm::uvec2(span_col, span_row);

  m_isUpdateNeededAddress = true;
}

void window::set_selfwidget_Title(widgetText *w)
{
  if (wOwnWidgets[0] != nullptr) delete(wOwnWidgets[0]);

  wOwnWidgets[0] = w;
  if (w != nullptr) wOwnWidgets[0]->m_parentWindow = this;

  m_isUpdateNeededAddress = true;
}

void window::set_selfwidget_CloseBox(widget * w)
{
  if (wOwnWidgets[1] != nullptr) delete(wOwnWidgets[1]);

  wOwnWidgets[1] = w;
  if (w != nullptr) wOwnWidgets[1]->m_parentWindow = this;

  m_isUpdateNeededAddress = true;
}

// intern process -------------------------------------------------------------

void window::clear()
{
  wlayout.clear();

  for (auto *widPtr : wOwnWidgets)
  {
    if (widPtr != nullptr) delete(widPtr);
  }
  wOwnWidgets.fill(nullptr);
}

void window::compute_adressPlage()
{
  if (!m_isUpdateNeededAddress) return;

  // set flags
  m_isUpdateNeededAddress = false;
  m_isUpdateNeededLayout = true;
  m_isUpdateNeededData = true;

  // compute adress-plage
  auto & model = get_parentUI()->getDrawModel();

  // -> for solid
  {
    widget::s_drawElementCount ad;
    ad.m_vcountSolid = 6; // the window's box
    std::array<uint, baseUI::s_textureSlotsCount> offsetVertexPerSlot;
    offsetVertexPerSlot.fill(0u);
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      const auto adc = cell.m_widget->get_drawElementCount();
      cell.m_widget->m_adSolid.part   = m_adSolid.part;
      cell.m_widget->m_adSolid.offset = ad.m_vcountSolid;
      cell.m_widget->m_adrLine.part   = m_adrLine.part;
      cell.m_widget->m_adrLine.offset = ad.m_vcountLine;
      if (adc.m_textureSlot < baseUI::s_textureSlotsCount)
      {
        cell.m_widget->m_adrPict.part   = m_adrPict.part + adc.m_textureSlot;
        cell.m_widget->m_adrPict.offset = offsetVertexPerSlot[adc.m_textureSlot];
        offsetVertexPerSlot[adc.m_textureSlot] += adc.m_vcountPict;
      }
      else
      {
        cell.m_widget->m_adrPict.part   = -1; // no pict
        cell.m_widget->m_adrPict.offset = 0u; // no pict
      }
      cell.m_widget->m_adrText.part   = m_adrText.part;
      cell.m_widget->m_adrText.offset = ad.m_vcountText;
      ad += cell.m_widget->get_drawElementCount();
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      widPtr->m_adSolid.part   = m_adSolid.part;
      widPtr->m_adSolid.offset = ad.m_vcountSolid;
      widPtr->m_adrLine.part   = m_adrLine.part;
      widPtr->m_adrLine.offset = ad.m_vcountLine;
      TRE_ASSERT(ad.m_vcountPict == 0); // not implemented
      widPtr->m_adrText.part   = m_adrText.part;
      widPtr->m_adrText.offset = ad.m_vcountText;
      ad += widPtr->get_drawElementCount();
    }
    model.resizePart(m_adSolid.part, ad.m_vcountSolid);
    model.resizePart(m_adrLine.part, ad.m_vcountLine);
    model.resizePart(m_adrText.part, ad.m_vcountText);
    for (std::size_t i = 0; i < baseUI::s_textureSlotsCount; ++i) model.resizePart(m_adrPict.part + i, offsetVertexPerSlot[i]);
  }
}

void window::compute_layout()
{
  if (!m_isUpdateNeededLayout) return;

  m_isUpdateNeededLayout = false;
  m_isUpdateNeededData = true;

  widget *widTopBar = wOwnWidgets[0];
  widget *widCloseButton = wOwnWidgets[1];

  // note: no need to set "m_isUpdateNeededData" because "set_zone()" will flag widget(s) wih this flag.

  const glm::vec2 topBarZoneSize = (widTopBar != nullptr) ? widTopBar->get_zoneSizeDefault() : glm::vec2(0.f);
  const glm::vec2 topCloseButtonZoneSize = (widCloseButton != nullptr) ? widCloseButton->get_zoneSizeDefault() : glm::vec2(0.f);

  glm::vec2 offset = glm::vec2(m_zone.x, m_zone.y);
  glm::vec2 innerSize = wlayout.computeWidgetZones(*this, glm::vec2(m_zone.x, m_zone.y + m_zone.w - topBarZoneSize.y));

  // add zone for top-bar
  {
    innerSize.y += topBarZoneSize.y;
    innerSize.x = glm::max(innerSize.x, topBarZoneSize.x + topCloseButtonZoneSize.x);
  }

  // window alignment

  if (walignMask & ALIGN_MASK_HORIZONTAL_CENTERED)
  {
    offset.x = -0.5f * innerSize.x;
  }
  else if (walignMask & ALIGN_MASK_HORIZONTAL_RIGHT)
  {
    offset.x = -innerSize.x;
  }
  else //if (walignMask & ALIGN_MASK_HORIZONTAL_LEFT)
  {
    offset.x = 0.f;
  }

  if (walignMask & ALIGN_MASK_VERTICAL_CENTERED)
  {
    offset.y = -0.5f * innerSize.y;
  }
  else if (walignMask & ALIGN_MASK_VERTICAL_BOTTOM)
  {
    offset.y = 0.f;
  }
  else //if (walignMask & ALIGN_MASK_VERTICAL_TOP)
  {
    offset.y = -innerSize.y;
  }

  // final set

  if (m_zone.x != offset.x || m_zone.y != offset.y || m_zone.z != innerSize.x || m_zone.w != innerSize.y)
  {
    m_zone = glm::vec4(offset, innerSize);

    wlayout.computeWidgetZones(*this, glm::vec2(m_zone.x, m_zone.y + m_zone.w - topBarZoneSize.y));

    if (widTopBar != nullptr)
      widTopBar->set_zone(glm::vec4(m_zone.x, m_zone.y + m_zone.w - topBarZoneSize.y, m_zone.x + m_zone.z, m_zone.y + m_zone.w));

    if (widCloseButton != nullptr)
      widCloseButton->set_zone(glm::vec4(m_zone.x + m_zone.z - topCloseButtonZoneSize.x, m_zone.y +  m_zone.w - topCloseButtonZoneSize.y, m_zone.x + m_zone.z, m_zone.y + m_zone.w));
  }
}

void window::compute_data()
{
  if (!m_isUpdateNeededData) return;
  TRE_ASSERT(m_isUpdateNeededAddress == false);
  TRE_ASSERT(m_isUpdateNeededLayout == false);
  m_isUpdateNeededData = false;

  {
    const glm::vec4 colorBase = wcolortheme.resolveColor(wcolortheme.m_colorBackground, 0.f) * wcolormask;
    auto & objsolid = get_parentUI()->getDrawModel();
    objsolid.fillDataRectangle(m_adSolid.part, 0, glm::vec4(m_zone.x, m_zone.y, m_zone.x + m_zone.z, m_zone.y + m_zone.w), colorBase, glm::vec4(0.f));
  }

  // window's widgets
  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget == nullptr) continue;
    c.m_widget->compute_data();
  }
  for (auto *widPtr : wOwnWidgets)
  {
    if (widPtr == nullptr) continue;
    // TODO: (perf) update only if widget's data needs to be computed, and only the data has changed but not the address or layout.
    widPtr->compute_data();
  }
}

void window::acceptEvent(s_eventIntern &event)
{
  if (m_isMoved)
  {
    event.accepted = true;
  }
  else
  {
    acceptEventBase_focus(event);
  }

  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget != nullptr) c.m_widget->acceptEvent(event);
  }

  if (!wisactive)
  {
    m_isMoved = false;
    return;
  }

  // move

  if (!m_isMoved)
  {
    for (std::size_t i = wOwnWidgets.size(); i-- != 0; ) // specific here, so "close"-widget is receiving the event first.
    {
      widget *widPtr = wOwnWidgets[i];
      if (widPtr != nullptr) widPtr->acceptEvent(event);
    }
  }

  widget *widTopBar = wOwnWidgets[0];

  if (widTopBar != nullptr)
  {
    if (m_isMoved)
    {
      if ((event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0)
      {
        const glm::vec3 deltaMouse = event.mousePos - event.mousePosPrev;
        if (get_parentUI()->get_dimension() == 2)
          wmat3[2] = wmatPrev.m3 * glm::vec3(deltaMouse.x, deltaMouse.y, 1.f);
        else
          wmat4[3] = wmatPrev.m4 * glm::vec4(deltaMouse, 1.f);
        m_isUpdateNeededLayout = true;
      }
      else
      {
        m_isMoved = false;
      }
    }
    else
    {
      const bool isClickedL = (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0;
      if (isClickedL && widTopBar->get_ishighlighted())
      {
        m_isMoved = true;
        if (get_parentUI()->get_dimension() == 2) wmatPrev.m3 = wmat3;
        else                                      wmatPrev.m4 = wmat4;
        event.accepted = true;
      }
    }
  }
}

void window::animate(float dt)
{
  if (wcb_animate) wcb_animate(this, dt);
  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget != nullptr) c.m_widget->animate(dt);
  }
}

bool window::getIsOverPosition(const glm::vec3 &position) const
{
  return (m_zone.x <= position.x) && (m_zone.x + m_zone.z >= position.x) &&
         (m_zone.y <= position.y) && (m_zone.y + m_zone.w >= position.y);
}

void window::acceptEventBase_focus(s_eventIntern &event)
{
  if (!wisactive || event.accepted)
  {
    if (wisactive && whasFocus == true)
    {
      if (wcb_loss_focus != nullptr) wcb_loss_focus(this);
    }
    whasFocus = false;
    return;
  }

  const bool isOverCurr = getIsOverPosition(event.mousePos);
  const bool isOverPrev = getIsOverPosition(event.mousePosPrev);
  const bool isPressed  = event.mouseButtonIsPressed != 0;
  const bool isFocused = (isOverCurr && !isPressed) || (isOverPrev && isPressed);

  if (whasFocus == true && isFocused == false)
  {
    if (wcb_loss_focus != nullptr) wcb_loss_focus(this);
  }
  if (whasFocus == false && isFocused == true)
  {
    if (wcb_gain_focus != nullptr) wcb_gain_focus(this);
  }
  whasFocus = isFocused;
}

} // namespace ui

} // namespace tre
