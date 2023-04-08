#include "tre_ui.h"

namespace tre {

namespace ui {

// widget specific to window --------------------------------------------------

class widgetTextTitle : public widgetText
{
public:
  widgetTextTitle() : widgetText() {}
  virtual ~widgetTextTitle() override {}

  virtual uint get_vcountSolid() const override { return widgetText::get_vcountSolid() + 2 * 6; }
  virtual void compute_data() override;
};

void widgetTextTitle::compute_data()
{
  TRE_ASSERT(wwithbackground == false); // have a custom bg.
  widgetText::compute_data();
  // process
  auto & objsolid = get_parentUI()->getDrawModel();
  const uint vertexOffsetLocal = m_adSolid.offset + widgetText::get_vcountSolid();

  const float sepY = 0.35f * m_zone.y + 0.65f * m_zone.w;

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorParent = get_parent()->resolve_color();
  const glm::vec4 colorBack1 = 0.5f * colorFront + 0.5f * colorParent; // TODO: color blend modes ?
  const glm::vec4 colorBack2 = 0.3f * colorFront + 0.7f * colorParent; // TODO: color blend modes ?

  objsolid.fillDataRectangle(m_adSolid.part, vertexOffsetLocal + 0, glm::vec4(m_zone.x,m_zone.y,m_zone.z,sepY   ), colorBack2, glm::vec4(0.f));
  objsolid.fillDataRectangle(m_adSolid.part, vertexOffsetLocal + 6, glm::vec4(m_zone.x,sepY   ,m_zone.z,m_zone.w), colorBack1, glm::vec4(0.f));
}

// global properties ----------------------------------------------------------

void window::set_visible(bool a_visible)
{
  if (wvisible == a_visible) return;
  wvisible = a_visible;
  if (wvisible == false)
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
  wTextTitle->set_text(name)->set_isactive(canBeMoved);
  wOwnWidgets[0] = wTextTitle;
  wOwnWidgets[0]->m_parent = this;

  if (canBeClosed)
  {
    if (wOwnWidgets[1] != nullptr) delete(wOwnWidgets[1]);

    widgetBoxCheck *wCloseBox = new widgetBoxCheck;
    wCloseBox->set_withBorder(false);
    wCloseBox->set_value(true);
    wCloseBox->set_isactive(true);
    wCloseBox->wcb_clicked_left = [this](widget *)
    {
      this->set_visible(false);
    };
    wOwnWidgets[1] = wCloseBox;
    wOwnWidgets[1]->m_parent = this;
  }

  m_isUpdateNeededAdress = true;

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

  TRE_ASSERT(w->m_parent == nullptr); // widget is not already added in a window.

  cell.m_widget = w;
  cell.m_widget->m_parent = this;
  cell.m_span = glm::uvec2(span_col, span_row);

  m_isUpdateNeededAdress = true;
}

void window::set_selfwidget_Title(widgetText *w)
{
  if (wOwnWidgets[0] != nullptr) delete(wOwnWidgets[0]);

  wOwnWidgets[0] = w;
  if (w != nullptr) wOwnWidgets[0]->m_parent = this;

  m_isUpdateNeededAdress = true;
}

void window::set_selfwidget_CloseBox(widget * w)
{
  if (wOwnWidgets[1] != nullptr) delete(wOwnWidgets[1]);

  wOwnWidgets[1] = w;
  if (w != nullptr) wOwnWidgets[1]->m_parent = this;

  m_isUpdateNeededAdress = true;
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
  // needs update ?
  bool willUpdate = m_isUpdateNeededAdress;
  m_isUpdateNeededAdress = false;

  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget == nullptr) continue;
    willUpdate |= c.m_widget->m_isUpdateNeededAdress;
    c.m_widget->m_isUpdateNeededAdress = false;
  }
  for (auto *widPtr : wOwnWidgets)
  {
    if (widPtr == nullptr) continue;
    willUpdate |= widPtr->m_isUpdateNeededAdress;
    widPtr->m_isUpdateNeededAdress = false;
  }

  if (!willUpdate) return;

  // set flags
  m_isUpdateNeededData = true;

  // compute adress-plage
  auto & model   = get_parentUI()->getDrawModel();

  // -> for solid
  {
    uint offsetVertex = 6; // the window's box
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      cell.m_widget->m_adSolid.part   = m_adSolid.part;
      cell.m_widget->m_adSolid.offset = offsetVertex;
      offsetVertex += cell.m_widget->get_vcountSolid();
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      widPtr->m_adSolid.part   = m_adSolid.part;
      widPtr->m_adSolid.offset = offsetVertex;
      offsetVertex += widPtr->get_vcountSolid();
    }
    model.resizePart(m_adSolid.part, offsetVertex);
  }

  // -> for line
  {
    uint offsetVertex = 0;
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      cell.m_widget->m_adrLine.part   = m_adrLine.part;
      cell.m_widget->m_adrLine.offset = offsetVertex;
      offsetVertex += cell.m_widget->get_vcountLine();
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      widPtr->m_adrLine.part   = m_adrLine.part;
      widPtr->m_adrLine.offset = offsetVertex;
      offsetVertex += widPtr->get_vcountLine();
    }
    model.resizePart(m_adrLine.part, offsetVertex);
  }

  // -> for pict
  {
    std::array<uint, baseUI::s_textureSlotsCount> offsetVertexPerSlot;
    offsetVertexPerSlot.fill(0u);
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      const uint vertexCount = cell.m_widget->get_vcountPict();
      const uint textureSlot = cell.m_widget->get_textureSlot();
      if (textureSlot >= baseUI::s_textureSlotsCount) continue;
      cell.m_widget->m_adrPict.part   = m_adrPict.part + textureSlot;
      cell.m_widget->m_adrPict.offset = offsetVertexPerSlot[textureSlot];
      offsetVertexPerSlot[textureSlot] += vertexCount;
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      TRE_ASSERT(widPtr->get_vcountPict() == 0); // not implemented
    }
    for (std::size_t i = 0; i < baseUI::s_textureSlotsCount; ++i) model.resizePart(m_adrPict.part + i, offsetVertexPerSlot[i]);
  }

  // -> for text
  {
    uint offsetVertex = 0;
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      cell.m_widget->m_adrText.part   = m_adrText.part;
      cell.m_widget->m_adrText.offset = offsetVertex;
      offsetVertex += cell.m_widget->get_vcountText();
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      widPtr->m_adrText.part   = m_adrText.part;
      widPtr->m_adrText.offset = offsetVertex;
      offsetVertex += widPtr->get_vcountText();
    }
    model.resizePart(m_adrText.part, 0u); // hack, discard previous data.
    model.resizePart(m_adrText.part, offsetVertex);
  }
}

void window::compute_layout()
{
  // needs update ?
  bool willUpdate = m_isUpdateNeededLayout;
  m_isUpdateNeededLayout = false;

  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget == nullptr) continue;
    willUpdate |= c.m_widget->m_isUpdateNeededLayout;
    c.m_widget->m_isUpdateNeededLayout = false;
  }
  for (auto *widPtr : wOwnWidgets)
  {
    if (widPtr == nullptr) continue;
    willUpdate |= widPtr->m_isUpdateNeededLayout;
    widPtr->m_isUpdateNeededLayout = false;
  }

  if (!willUpdate) return;

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
  // needs update ?
  bool willUpdate = m_isUpdateNeededData;
  if (!willUpdate)
  {
    for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
    {
      if (c.m_widget == nullptr) continue;
      if (c.m_widget->m_isUpdateNeededData)
      {
        willUpdate = true;
        break;
      }
    }
    for (auto *widPtr : wOwnWidgets)
    {
      if (widPtr == nullptr) continue;
      willUpdate |= widPtr->m_isUpdateNeededData;
    }
  }
  if (!willUpdate) return;

  {
    const glm::vec4 colorBase = wcolortheme.resolveColor(wcolor, resolve_colorModifier()) * wcolormask;
    auto & objsolid = get_parentUI()->getDrawModel();
    objsolid.fillDataRectangle(m_adSolid.part, 0, glm::vec4(m_zone.x, m_zone.y, m_zone.x + m_zone.z, m_zone.y + m_zone.w), colorBase, glm::vec4(0.f));
  }

  // window's widgets
  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget == nullptr) continue;
    if (!m_isUpdateNeededData && !c.m_widget->m_isUpdateNeededData) continue;
    c.m_widget->compute_data();
    c.m_widget->m_isUpdateNeededData = false;
  }
  for (auto *widPtr : wOwnWidgets)
  {
    if (widPtr == nullptr) continue;
    if (!m_isUpdateNeededData && !widPtr->m_isUpdateNeededData) continue;
    widPtr->compute_data();
    widPtr->m_isUpdateNeededData = false;
  }

  m_isUpdateNeededData = false;
}

void window::acceptEvent(s_eventIntern &event)
{
  if (m_isMoved) event.accepted = true;

  const bool eventAccepted_save = event.accepted;

  if (!m_isMoved)
  {
    acceptEventBase_focus(event);
    event.accepted = eventAccepted_save;
  }

  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget != nullptr) c.m_widget->acceptEvent(event);
  }

  if (!m_isMoved)
  {
    for (std::size_t i = wOwnWidgets.size(); i-- != 0; ) // specific here, so "close"-widget is receiving the event first.
    {
      widget *widPtr = wOwnWidgets[i];
      if (widPtr != nullptr) widPtr->acceptEvent(event);
    }

    acceptEventBase_click(event);
  }

  event.accepted |= eventAccepted_save;

  // move

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
  widget::animate(dt);
  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget != nullptr) c.m_widget->animate(dt);
  }
}

} // namespace ui

} // namespace tre
