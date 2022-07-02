
#include "ui.h"

namespace tre {

namespace ui {

// ============================================================================

void s_layoutGrid::set_dimension(uint row, uint col)
{
  TRE_ASSERT(row>0);
  TRE_ASSERT(col>0);
  if (m_dimension.x == col && m_dimension.y == row) return;
  TRE_ASSERT(m_dimension.x == 0 && m_dimension.y == 0); // for now, set_dimension supports only one call

  m_cells.resize(col*row);

  m_rowsHeight_User.resize(row, -1.f);
  m_colsWidth_User.resize(col, -1.f);
  m_rowsHeight.resize(row, 0.f);
  m_colsWidth.resize(col, 0.f);

  m_dimension.x = col;
  m_dimension.y = row;
}

// ----------------------------------------------------------------------------

glm::vec4 s_layoutGrid::computeWidgetZones(const ui::window &win, const glm::vec2 &offset)
{
  if (m_dimension.x==0 || m_dimension.y==0)
    return glm::vec4(offset, offset); // should not occur.

  TRE_ASSERT(m_dimension.x == m_colsWidth_User.size() && m_dimension.x == m_colsWidth.size());
  TRE_ASSERT(m_dimension.y == m_rowsHeight_User.size() && m_dimension.y == m_rowsHeight.size());

   // 1. compute sizes of the rows and columns
  {
    // -> clear
    for (float &cw : m_colsWidth) cw = 0.f;
    for (float &rh : m_rowsHeight) rh = 0.f;

    // -> single-span
    for (uint ix = 0; ix < m_dimension.x; ++ix)
    {
      for (uint iy = 0; iy < m_dimension.y; ++iy)
      {
        const s_cell &cell = m_cells[index(iy,ix)];

        if (cell.m_widget == nullptr) continue;

        const glm::vec2 wdefaultSize = cell.m_widget->get_zoneSizeDefault();
        if (cell.m_span.x == 1 && wdefaultSize.x > m_colsWidth[ix])
          m_colsWidth[ix] = wdefaultSize.x;
        if (cell.m_span.y == 1 && wdefaultSize.y > m_rowsHeight[iy])
          m_rowsHeight[iy] = wdefaultSize.y;
      }
    }

    // -> apply user info
    for (uint ix = 0; ix < m_dimension.x; ++ix)
    {
      if (m_colsWidth_User[ix].size > 0.f)
        m_colsWidth[ix] = glm::max(m_colsWidth[ix], win.resolve_sizeW(m_colsWidth_User[ix]));
    }
    for (uint iy = 0; iy < m_dimension.y; ++iy)
    {
      if (m_rowsHeight_User[iy].size > 0.f)
        m_rowsHeight[iy] = glm::max(m_rowsHeight[iy], win.resolve_sizeH(m_rowsHeight_User[iy]));
    }

    // -> multi-span
    for (uint ix = 0; ix < m_dimension.x; ++ix)
    {
      for (uint iy = 0; iy < m_dimension.y; ++iy)
      {
        const s_cell &cell = m_cells[index(iy,ix)];

        if (cell.m_widget == nullptr) continue;

        const glm::vec2 wdefaultSize = cell.m_widget->get_zoneSizeDefault();

        if (cell.m_span.x > 1)
        {
          float totalWidth = 0.f;
          const uint jstop = glm::min(ix + cell.m_span.x, m_dimension.x);
          for (uint jx = ix; jx < jstop; ++jx)
            totalWidth += m_colsWidth[jx];
          if (totalWidth < wdefaultSize.x)
          {
            uint jx = ix;
            while (m_colsWidth_User[jx].size > 0.f && jx < jstop) { ++jx; }
            if (m_colsWidth_User[jx].size <= 0.f)
            {
              m_colsWidth[jx] += wdefaultSize.x - totalWidth;
            }
          }
        }

        if (cell.m_span.y > 1)
        {
          float totalHeight = 0.f;
          const uint jstop = glm::min(iy + cell.m_span.y, m_dimension.y);
          for (uint jy = iy; jy < jstop; ++jy)
            totalHeight += m_rowsHeight[jy];
          if (totalHeight < wdefaultSize.y)
          {
            uint jy = iy;
            while (m_rowsHeight_User[jy].size > 0.f && jy < jstop) { ++jy; }
            if (m_rowsHeight_User[jy].size <= 0.f)
            {
              m_rowsHeight[jy] += wdefaultSize.y - totalHeight;
            }
          }
        }
      }
    }

    // -> snap to pixel
    for (float &cw : m_colsWidth)
    {
      const glm::vec2 out = win.snapToPixel(glm::vec2(cw, 0.f), glm::ivec2(0));
      cw = out.x;
    }
    for (float &rh : m_rowsHeight)
    {
      const glm::vec2 out = win.snapToPixel(glm::vec2(0.f, rh), glm::ivec2(0));
      rh = out.y;
    }
  }

  // 2. compute offsets of rows and columns

  std::vector<float> colsPos(m_dimension.x + 1), rowsPos(m_dimension.y + 1);

  const glm::vec2 marginSnaped = win.snapToPixel(0.5f * win.resolve_sizeWH(m_cellMargin), glm::ivec2(2)) * 2.f;

  {
    colsPos[0] = 0.f;
    rowsPos[0] = 0.f;

    for (uint ix = 1; ix <= m_dimension.x; ++ix)
      colsPos[ix] = colsPos[ix-1] + m_colsWidth[ix-1] + marginSnaped.x;

    for (uint iy = 1; iy <= m_dimension.y; ++iy)
      rowsPos[iy] = rowsPos[iy-1] - m_rowsHeight[iy-1] - marginSnaped.y;
  }

  // 3. assign zone to widgets

  const glm::vec2 offsetSnaped = win.snapToPixel(offset);

  for (uint ix = 0; ix < m_dimension.x; ++ix)
  {
    for (uint iy = 0; iy < m_dimension.y; ++iy)
    {
      const s_cell &cell = m_cells[index(iy,ix)];

      if (cell.m_widget == nullptr) continue;

      const uint ixN = glm::min(ix + cell.m_span.x, m_dimension.x);
      const uint iyN = glm::min(iy + cell.m_span.y, m_dimension.y);

      glm::vec2 ptLB(colsPos[ix]  + 0.5f * marginSnaped.x, rowsPos[iyN] + 0.5f * marginSnaped.y);
      glm::vec2 ptRT(colsPos[ixN] - 0.5f * marginSnaped.x, rowsPos[iy]  - 0.5f * marginSnaped.y);

      const glm::vec2 wSize = glm::min(cell.m_widget->get_zoneSizeDefault(), ptRT - ptLB);

      if (ptRT.x - ptLB.x > wSize.x)
      {
        if (cell.m_alignMask & ALIGN_MASK_HORIZONTAL_CENTERED)
        {
          const float center = 0.5f * (ptRT.x + ptLB.x);
          ptLB.x = center - 0.5f * wSize.x;
          ptRT.x = center + 0.5f * wSize.x;
        }
        else if (cell.m_alignMask & ALIGN_MASK_HORIZONTAL_RIGHT)
          ptLB.x = ptRT.x - wSize.x;
        else //if (cell.m_alignMask & ALIGN_MASK_HORIZONTAL_LEFT)
          ptRT.x = ptLB.x + wSize.x;
      }

      if (ptRT.y - ptLB.y > wSize.y)
      {
        if (cell.m_alignMask & ALIGN_MASK_VERTICAL_CENTERED)
        {
          const float center = 0.5f * (ptRT.y + ptLB.y);
          ptLB.y = center - 0.5f * wSize.y;
          ptRT.y = center + 0.5f * wSize.y;
        }
        else if (cell.m_alignMask & ALIGN_MASK_VERTICAL_TOP)
          ptLB.y = ptRT.y - wSize.y;
        else //if (cell.m_alignMask & ALIGN_MASK_VERTICAL_BOTTOM)
          ptRT.y = ptLB.y + wSize.y;
      }

      cell.m_widget->set_zone(glm::vec4(offsetSnaped + ptLB, offsetSnaped + ptRT));
    }
  }

  return glm::vec4(offset, offset) + glm::vec4(0.f, rowsPos[m_dimension.y], colsPos[m_dimension.x], 0.f);
}

// ----------------------------------------------------------------------------

void s_layoutGrid::clear()
{
  for (s_cell &c : m_cells)
  {
    if (c.m_widget) delete(c.m_widget);
  }
  m_cells.clear();
  m_rowsHeight_User.clear();
  m_colsWidth_User.clear();
  m_rowsHeight.clear();
  m_colsWidth.clear();
  m_dimension = glm::uvec2(0);
}

// ============================================================================


} // namespace ui

} // namespace tre
