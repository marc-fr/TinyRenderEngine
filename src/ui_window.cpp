
#include "ui.h"

namespace tre {

namespace ui {

// widget specific to window --------------------------------------------------

class widgetTextTitle : public widgetText
{
public:
  widgetTextTitle() : widgetText() {}
  virtual ~widgetTextTitle() override {}

  virtual uint get_vcountSolid() const override { return 2 * 6; }
  virtual void compute_data() override;
  //virtual void acceptEvent(s_eventIntern &event) override;
};

void widgetTextTitle::compute_data()
{
  wwithbackground = false;
  widgetText::compute_data();
  // process
  auto & objsolid = get_parentUI()->getDrawObject_Box();
  const uint vertexOffsetLocal = m_adSolid.offset; // + widgetText::get_vcountSolid();

  const float sepY = 0.35f * wzone.y + 0.65f * wzone.w;

  const glm::vec4 colorFront = resolve_color();
  const glm::vec4 colorParent = get_parent()->resolve_color();
  const glm::vec4 colorBack1 = 0.5f * colorFront + 0.5f * colorParent; // TODO: color blend modes ?
  const glm::vec4 colorBack2 = 0.3f * colorFront + 0.7f * colorParent; // TODO: color blend modes ?

  objsolid.fillDataRectangle(m_adSolid.part, vertexOffsetLocal + 0, glm::vec4(wzone.x,wzone.y,wzone.z,sepY   ), colorBack2, glm::vec4(0.f));
  objsolid.fillDataRectangle(m_adSolid.part, vertexOffsetLocal + 6, glm::vec4(wzone.x,sepY   ,wzone.z,wzone.w), colorBack1, glm::vec4(0.f));
}

/*void widgetTextTitle::acceptEvent(s_eventIntern &event)
{
  acceptEventBase_focus(event);
}*/

// global properties ----------------------------------------------------------

void window::set_visible(bool a_visible)
{
  if (wvisible == a_visible) return;
  m_isUpdateNeededData |= true;
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
  if (wTopBar != nullptr) delete(wTopBar);

  widgetTextTitle *wTextTitle = new widgetTextTitle;
  wTextTitle->set_text(name)->set_isactive(canBeMoved);
  wTopBar = wTextTitle;
  wTopBar->m_parent = this;

  if (canBeClosed)
  {
    if (wCloseButton != nullptr) delete(wCloseButton);

    widgetBoxCheck *wCloseBox = new widgetBoxCheck;
    wCloseBox->set_value(true)->set_isactive(true);
    wCloseBox->wcb_clicked_left = [this](widget *)
    {
      this->set_visible(false);
    };

    wCloseButton = wCloseBox;
    wCloseButton->m_parent = this;
  }

  m_isUpdateNeededAdress = true;

  return this;
}

window* window::set_topbarName(const std::string &name)
{
  if (wTopBar != nullptr) wTopBar->set_text(name);
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
  for (uint row = 0; row < wlayout.m_dimension.y; ++row)
    wlayout.m_cells[wlayout.index(row, col)].m_alignMask = alignMask;
}

void window::set_rowAlignment(uint row, uint alignMask)
{
  TRE_ASSERT(row < wlayout.m_dimension.y);
  for (uint col = 0; col < wlayout.m_dimension.x; ++col)
    wlayout.m_cells[wlayout.index(row, col)].m_alignMask = alignMask;
}

float window::resolve_sizeW(s_size size) const
{
  return resolve_sizeWH(size).x;
}

float window::resolve_sizeH(s_size size) const
{
  return resolve_sizeWH(size).y;
}

glm::vec2 window::resolve_sizeWH(s_size size) const
{
  if (size.unit == SIZE_NATIVE)
    return glm::vec2(size.size, size.size); // aspect ratio ??
  TRE_ASSERT(size.unit == SIZE_PIXEL);
  if (get_parentUI()->get_dimension() == 2)
  {
    const baseUI2D *ui = static_cast<const baseUI2D*>(get_parentUI());
    return ui->projectWindowPointFromScreen(glm::ivec2(size.size, size.size), wmat3, false);
  }
  else
  {
    TRE_ASSERT(get_parentUI()->get_dimension() == 3);
    const baseUI3D *ui = static_cast<const baseUI3D*>(get_parentUI());

    const glm::vec4 ptWin = glm::vec4(0.f, 0.f, 0.f, 1.f);
    const glm::vec4 ptClip = ui->get_matPV() * (wmat4 * ptWin);

    const glm::vec2 sizeViewport = size.size * 2.f / ui->get_viewport();
    return ptClip.w * sizeViewport;
  }
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
  if (wTopBar != nullptr) delete(wTopBar);

  wTopBar = w;
  if (w != nullptr) wTopBar->m_parent = this;

  m_isUpdateNeededAdress = true;
}

void window::set_selfwidget_CloseBox(widget * w)
{
  if (wCloseButton != nullptr) delete(wCloseButton);

  wCloseButton = w;
  if (w != nullptr) wCloseButton->m_parent = this;

  m_isUpdateNeededAdress = true;
}

// intern process -------------------------------------------------------------

void window::clear()
{
  wlayout.clear();

  if (wTopBar) delete(wTopBar);
  wTopBar = nullptr;

  if (wCloseButton) delete(wCloseButton);
  wCloseButton = nullptr;
}

void window::compute_adressPlage()
{
  // compute count and adress-plage
  auto & model   = get_parentUI()->getDrawObject_Box();
  auto & textgen = get_parentUI()->getDrawObject_Text();

  // -> for solid
  {
    m_adSolid.part = model.createPart(0);
    m_adSolid.pcount = 1;
    uint offsetVertex = 6; // the window's box
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      cell.m_widget->m_adSolid.part   = m_adSolid.part;
      cell.m_widget->m_adSolid.offset = offsetVertex;
      offsetVertex += cell.m_widget->get_vcountSolid();
    }
    if (wTopBar != nullptr)
    {
      wTopBar->m_adSolid.part   = m_adSolid.part;
      wTopBar->m_adSolid.offset = offsetVertex;
      offsetVertex += wTopBar->get_vcountSolid();
    }
    if (wCloseButton != nullptr)
    {
      wCloseButton->m_adSolid.part   = m_adSolid.part;
      wCloseButton->m_adSolid.offset = offsetVertex;
      offsetVertex += wCloseButton->get_vcountSolid();
    }
    model.resizePart(m_adSolid.part, offsetVertex);
  }

  // -> for line
  {
    m_adrLine.part = model.createPart(0);
    m_adrLine.pcount = 1;
    uint offsetVertex = 0;
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      cell.m_widget->m_adrLine.part   = m_adrLine.part;
      cell.m_widget->m_adrLine.offset = offsetVertex;
      offsetVertex += cell.m_widget->get_vcountLine();
    }
    if (wTopBar != nullptr)
    {
      wTopBar->m_adrLine.part   = m_adrLine.part;
      wTopBar->m_adrLine.offset = offsetVertex;
      offsetVertex += wTopBar->get_vcountLine();
    }
    if (wCloseButton != nullptr)
    {
      wCloseButton->m_adrLine.part   = m_adrLine.part;
      wCloseButton->m_adrLine.offset = offsetVertex;
      offsetVertex += wCloseButton->get_vcountLine();
    }
    model.resizePart(m_adrLine.part, offsetVertex);
  }

  // -> for pict
  {
    uint widCount = 0;
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      const uint vertexCount = cell.m_widget->get_vcountPict();
      if (vertexCount == 0) continue;
      cell.m_widget->m_adrPict.part   = model.createPart(vertexCount);
      cell.m_widget->m_adrPict.offset = 0;
      if (widCount == 0) m_adrPict.part = cell.m_widget->m_adrPict.part;
      TRE_ASSERT(cell.m_widget->m_adrPict.part == m_adrPict.part + widCount); // the adress-plage must be contiguous
      widCount++;
    }
    if (wTopBar != nullptr && wTopBar->get_vcountPict() != 0)
    {
      TRE_FATAL("not implemented");
    }
    if (wCloseButton != nullptr && wCloseButton->get_vcountPict() != 0)
    {
      TRE_FATAL("not implemented");
    }
    m_adrPict.pcount = widCount;
  }

  // -> for text
  {
    uint txtCount = 0;
    for(const auto &cell : wlayout.m_cells)
    {
      if (cell.m_widget == nullptr) continue;
      const uint txtLocalCount = cell.m_widget->get_countText();
      if (txtLocalCount == 0) continue;
      cell.m_widget->m_adrText.part   = textgen.createTexts(txtLocalCount, &model);
      cell.m_widget->m_adrText.offset = 0;
      if (txtCount == 0) m_adrText.part = cell.m_widget->m_adrText.part;
      TRE_ASSERT(cell.m_widget->m_adrText.part == m_adrText.part + txtCount); // the adress-plage must be contiguous
      txtCount += txtLocalCount;
    }
    if (wTopBar != nullptr && wTopBar->get_countText() != 0)
    {
      const uint txtLocalCount = wTopBar->get_countText();
      wTopBar->m_adrText.part   = textgen.createTexts(txtLocalCount, &model);
      wTopBar->m_adrText.offset = 0;
      if (txtCount == 0) m_adrText.part = wTopBar->m_adrText.part;
      TRE_ASSERT(wTopBar->m_adrText.part == m_adrText.part + txtCount); // the adress-plage must be contiguous
      txtCount += txtLocalCount;
    }
    if (wCloseButton != nullptr && wCloseButton->get_countText() != 0)
    {
      const uint txtLocalCount = wCloseButton->get_countText();
      wCloseButton->m_adrText.part   = textgen.createTexts(txtLocalCount, &model);
      wCloseButton->m_adrText.offset = 0;
      if (txtCount == 0) m_adrText.part = wCloseButton->m_adrText.part;
      TRE_ASSERT(wCloseButton->m_adrText.part == m_adrText.part + txtCount); // the adress-plage must be contiguous
      txtCount += txtLocalCount;
    }
    m_adrText.pcount = txtCount;
  }

  // -> mark window's data to be computed.
  m_isUpdateNeededAdress = false;
  m_isUpdateNeededLayout = true;
}

void window::compute_layout()
{
  // needs update ?
  bool willUpdate = m_isUpdateNeededLayout;
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
  }
  if (!willUpdate) return;

  const glm::vec2 topBarZoneSize = (wTopBar != nullptr) ? wTopBar->get_zoneSizeDefault() : glm::vec2(0.f);
  const glm::vec2 topCloseButtonZoneSize = (wCloseButton != nullptr) ? wCloseButton->get_zoneSizeDefault() : glm::vec2(0.f);

  glm::vec4 innerZone = wlayout.computeWidgetZones(*this, glm::vec2(wzone.x, wzone.w - topBarZoneSize.y));

  // add zone for top-bar
  {
    innerZone.w += topBarZoneSize.y;

    if (innerZone.z - innerZone.x < topBarZoneSize.x + topCloseButtonZoneSize.x)
      innerZone.z = innerZone.x + topBarZoneSize.x + topCloseButtonZoneSize.x;
  }

  // window alignment

  if (walignMask & ALIGN_MASK_HORIZONTAL_CENTERED)
  {
    const float halfSize = 0.5f * (innerZone.z - innerZone.x);
    innerZone.x = -halfSize;
    innerZone.z =  halfSize;
  }
  else if (walignMask & ALIGN_MASK_HORIZONTAL_RIGHT)
  {
    innerZone.x -= innerZone.z;
    innerZone.z = 0.f;
  }
  else //if (walignMask & ALIGN_MASK_HORIZONTAL_LEFT)
  {
    innerZone.z -= innerZone.x;
    innerZone.x = 0.f;
  }

  if (walignMask & ALIGN_MASK_VERTICAL_CENTERED)
  {
    const float halfSize = 0.5f * (innerZone.w - innerZone.y);
    innerZone.y = -halfSize;
    innerZone.w =  halfSize;
  }
  else if (walignMask & ALIGN_MASK_VERTICAL_BOTTOM)
  {
    innerZone.w -= innerZone.y;
    innerZone.y = 0.f;
  }
  else //if (walignMask & ALIGN_MASK_VERTICAL_TOP)
  {
    innerZone.y -= innerZone.w;
    innerZone.w = 0.f;
  }

  // final set

  if (wzone != innerZone)
  {
    wzone = innerZone;

    wlayout.computeWidgetZones(*this, glm::vec2(wzone.x, wzone.w - topBarZoneSize.y));

    if (wTopBar != nullptr)
      wTopBar->set_zone(glm::vec4(wzone.x, wzone.w - topBarZoneSize.y, wzone.z, wzone.w));

    if (wCloseButton != nullptr)
      wCloseButton->set_zone(glm::vec4(wzone.z - topCloseButtonZoneSize.x, wzone.w - topCloseButtonZoneSize.y, wzone.z, wzone.w));

    // update flags
    m_isUpdateNeededData = true; // recompute all widgets
  }

  m_isUpdateNeededLayout = false;
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
    if (wTopBar != nullptr)
      willUpdate |= wTopBar->m_isUpdateNeededData;
    if (wCloseButton != nullptr)
      willUpdate |= wCloseButton->m_isUpdateNeededData;
  }
  if (!willUpdate) return;

  const glm::vec4 colorBase = wcolortheme.resolveColor(wcolor, resolve_colorModifier()) * wcolormask;

  auto & objsolid = get_parentUI()->getDrawObject_Box();

  objsolid.fillDataRectangle(m_adSolid.part, 0, wzone, colorBase, glm::vec4(0.f));

  // window's widgets
  for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
  {
    if (c.m_widget == nullptr) continue;
    if (!m_isUpdateNeededData && !c.m_widget->m_isUpdateNeededData) continue;
    c.m_widget->compute_data();
    c.m_widget->m_isUpdateNeededData = false;
  }
  if (wTopBar != nullptr)
  {
    wTopBar->compute_data();
    wTopBar->m_isUpdateNeededData = false;
  }
  if (wCloseButton != nullptr)
  {
    wCloseButton->compute_data();
    wCloseButton->m_isUpdateNeededData = false;
  }

  m_isUpdateNeededData = false;

  // hack for pict
  {
    m_widgetTextureSlot.clear();
    m_widgetTextureSlot.reserve(m_adrPict.pcount);
    for (const s_layoutGrid::s_cell &c : wlayout.m_cells)
    {
      if (c.m_widget != nullptr && c.m_widget->get_vcountPict() != 0)
        m_widgetTextureSlot.push_back(c.m_widget->get_textureSlot());
    }
    if (wTopBar != nullptr && wTopBar->get_vcountPict() != 0)
    {
      m_widgetTextureSlot.push_back(wTopBar->get_textureSlot());
    }
    if (wCloseButton != nullptr && wCloseButton->get_vcountPict() != 0)
    {
      m_widgetTextureSlot.push_back(wCloseButton->get_textureSlot());
    }
    TRE_ASSERT(m_adrPict.pcount == m_widgetTextureSlot.size());
  }

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
    if (wCloseButton != nullptr) wCloseButton->acceptEvent(event);
    if (wTopBar != nullptr) wTopBar->acceptEvent(event);

    acceptEventBase_click(event);
  }

  event.accepted |= eventAccepted_save;

  // move
  if (wTopBar != nullptr)
  {
    if (m_isMoved)
    {
      if (event.mouseLeftPressed)
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
      const bool isClickedL = (event.event.type == SDL_MOUSEBUTTONDOWN && event.event.button.button == SDL_BUTTON_LEFT);
      if (isClickedL && wTopBar->get_ishighlighted())
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

glm::vec2 window::snapToPixel(const glm::vec2 &windowCoord) const
{
  if (get_parentUI()->get_dimension() == 2 && wsnapPixels)
  {
    const baseUI2D *ui = static_cast<const baseUI2D*>(get_parentUI());
    const glm::ivec2 screenCoordSnaped = glm::ceil(ui->projectWindowPointToScreen(windowCoord, wmat3));
    return ui->projectWindowPointFromScreen(screenCoordSnaped, wmat3);
  }
  return windowCoord;
}

glm::vec2 window::snapToPixel(const glm::vec2 &windowSize, const glm::ivec2 &pixelMin) const
{
  if (get_parentUI()->get_dimension() == 2 && wsnapPixels)
  {
    const baseUI2D *ui = static_cast<const baseUI2D*>(get_parentUI());
    glm::ivec2 screenCoordSnaped = glm::ceil(ui->projectWindowPointToScreen(windowSize, wmat3, false));
    if (screenCoordSnaped.x < pixelMin.x) screenCoordSnaped.x = pixelMin.x;
    if (screenCoordSnaped.y < pixelMin.y) screenCoordSnaped.y = pixelMin.y;
    return ui->projectWindowPointFromScreen(screenCoordSnaped, wmat3, false);
  }
  return windowSize;
}

} // namespace ui

} // namespace tre
