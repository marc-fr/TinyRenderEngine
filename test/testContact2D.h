
#include <vector>
#include <array>

#include "openglinclude.h"
#include "model.h"

namespace tre {
  struct s_contact2D;
}

#define CSIZE    0.04f
#define CSIZECNT 0.01f

static const glm::vec4 white     = glm::vec4(1.f);
static const glm::vec4 grey      = glm::vec4(0.3f);
static const glm::vec4 yellow    = glm::vec4(1.f, 1.f, 0.f, 1.f);
static const glm::vec4 green     = glm::vec4(0.f, 1.f, 0.f, 1.f);
static const glm::vec4 darkgreen = glm::vec4(0.1f, 0.4f, 0.1f, 1.f);
static const glm::vec4 darkblue  = glm::vec4(0.1f, 0.1f, 0.4f, 1.f);
static const glm::vec4 magenta   = glm::vec4(1.f, 0.f, 1.f, 1.f);

// ----------------------------------------------------------------------------

class sceneObjectBase
{
public:
  sceneObjectBase(tre::modelRaw2D *model, unsigned partSize);
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection) = 0;
  virtual void updateForDraw() = 0;

  enum eObjectType {POINT, LINE, YLINE, BOX, POLY, CIRCLE, RAY};
  virtual eObjectType type() const = 0;

  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const { return false; }

protected:
  void treatEventPt(glm::vec2 mousePos, bool mouseLeft, glm::vec2 &pt, bool &isSelected, bool &isHovered, bool &hasSelection);
  tre::modelRaw2D *m_model;
  unsigned         m_part;
};

// ----------------------------------------------------------------------------

class sceneObjectPoint : public sceneObjectBase
{
public:
  sceneObjectPoint(tre::modelRaw2D *model, glm::vec2 posInitial = glm::vec2(0.f))
    : sceneObjectBase(model, 4), m_pt(posInitial)
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    treatEventPt(mousePos, mouseLeft, m_pt, m_selected, m_hovered, hasSelection);
  }
  virtual void updateForDraw()
  {
    const glm::vec4 &col = m_selected ? magenta : (m_hovered ? yellow : white);
    m_model->fillDataLine(m_part, 0, m_pt.x - CSIZE, m_pt.y, m_pt.x + CSIZE, m_pt.y, col);
    m_model->fillDataLine(m_part, 2, m_pt.x, m_pt.y - CSIZE, m_pt.x, m_pt.y + CSIZE, col);
  }
  virtual eObjectType type() const { return POINT; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  bool      m_selected;
  bool      m_hovered;
  glm::vec2 m_pt;
};

// ----------------------------------------------------------------------------

class sceneObjectBox : public sceneObjectBase
{
public:
  sceneObjectBox(tre::modelRaw2D *model, glm::vec4 boxInitial = glm::vec4(0.f))
    : sceneObjectBase(model, 16),m_box(boxInitial)
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    glm::vec2 c00 = glm::vec2(m_box.x, m_box.y);
    glm::vec2 c11 = glm::vec2(m_box.z, m_box.w);
    treatEventPt(mousePos, mouseLeft, c00, m_selected[0], m_hovered[0], hasSelection);
    treatEventPt(mousePos, mouseLeft, c11, m_selected[1], m_hovered[1], hasSelection);
    if (c00.x < c11.x)
    {
      m_box.x = c00.x;
      m_box.z = c11.x;
    }
    if (c00.y < c11.y)
    {
      m_box.y = c00.y;
      m_box.w = c11.y;
    }
  }
  virtual void updateForDraw()
  {
    const glm::vec2 c00 = glm::vec2(m_box.x, m_box.y);
    const glm::vec2 c11 = glm::vec2(m_box.z, m_box.w);
    const glm::vec2 c01 = glm::vec2(m_box.x, m_box.w);
    const glm::vec2 c10 = glm::vec2(m_box.z, m_box.y);
    m_model->fillDataLine(m_part, 0, c00, c01, grey);
    m_model->fillDataLine(m_part, 2, c01, c11, grey);
    m_model->fillDataLine(m_part, 4, c11, c10, grey);
    m_model->fillDataLine(m_part, 6, c10, c00, grey);
    const glm::vec4 &colA = m_selected[0] ? magenta : (m_hovered[0] ? yellow : white);
    m_model->fillDataLine(m_part, 8,  c00.x - CSIZE, c00.y, c00.x + CSIZE, c00.y, colA);
    m_model->fillDataLine(m_part, 10, c00.x, c00.y - CSIZE, c00.x, c00.y + CSIZE, colA);
    const glm::vec4 &colB = m_selected[1] ? magenta : (m_hovered[1] ? yellow : white);
    m_model->fillDataLine(m_part, 12, c11.x - CSIZE, c11.y, c11.x + CSIZE, c11.y, colB);
    m_model->fillDataLine(m_part, 14, c11.x, c11.y - CSIZE, c11.x, c11.y + CSIZE, colB);
  }
  virtual eObjectType type() const { return BOX; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  std::array<bool,2> m_selected;
  std::array<bool,2> m_hovered;
  glm::vec4          m_box;

};

// ----------------------------------------------------------------------------

class sceneObjectLine : public sceneObjectBase
{
public:
  sceneObjectLine(tre::modelRaw2D *model, glm::vec2 ptA = glm::vec2(0.f), glm::vec2 ptB = glm::vec2(0.f))
    : sceneObjectBase(model, 10),m_pts({ptA, ptB})
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    treatEventPt(mousePos, mouseLeft, m_pts[0], m_selected[0], m_hovered[0], hasSelection);
    treatEventPt(mousePos, mouseLeft, m_pts[1], m_selected[1], m_hovered[1], hasSelection);
  }
  virtual void updateForDraw()
  {
    const glm::vec4 &colA = m_selected[0] ? magenta : (m_hovered[0] ? yellow : white);
    m_model->fillDataLine(m_part, 0, m_pts[0].x - CSIZE, m_pts[0].y, m_pts[0].x + CSIZE, m_pts[0].y, colA);
    m_model->fillDataLine(m_part, 2, m_pts[0].x, m_pts[0].y - CSIZE, m_pts[0].x, m_pts[0].y + CSIZE, colA);
    const glm::vec4 &colB = m_selected[1] ? magenta : (m_hovered[1] ? yellow : white);
    m_model->fillDataLine(m_part, 4, m_pts[1].x - CSIZE, m_pts[1].y, m_pts[1].x + CSIZE, m_pts[1].y, colB);
    m_model->fillDataLine(m_part, 6, m_pts[1].x, m_pts[1].y - CSIZE, m_pts[1].x, m_pts[1].y + CSIZE, colB);

    m_model->fillDataLine(m_part, 8, m_pts[0], m_pts[1], grey);
  }
  virtual eObjectType type() const { return LINE; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  std::array<bool,2>      m_selected;
  std::array<bool,2>      m_hovered;
  std::array<glm::vec2,2> m_pts;
};

// ----------------------------------------------------------------------------

class sceneObjectYDown : public sceneObjectBase
{
public:
  sceneObjectYDown(tre::modelRaw2D *model, float limy = 0.f)
    : sceneObjectBase(model, 4),m_pt(0.f, limy)
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    treatEventPt(mousePos, mouseLeft, m_pt, m_selected, m_hovered, hasSelection);
  }
  virtual void updateForDraw()
  {
    const glm::vec4 &col = m_selected ? magenta : (m_hovered ? yellow : white);
    m_model->fillDataLine(m_part, 0, -100.f, m_pt.y, 100.f, m_pt.y, grey);
    m_model->fillDataLine(m_part, 2, m_pt.x, m_pt.y - CSIZE, m_pt.x, m_pt.y + CSIZE, col);
  }
  virtual eObjectType type() const { return YLINE; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  bool      m_selected;
  bool      m_hovered;
  glm::vec2 m_pt;
};

// ----------------------------------------------------------------------------

class sceneObjectPoly : public sceneObjectBase
{
public:
  sceneObjectPoly(tre::modelRaw2D *model, const std::vector<glm::vec2> &ptsInitial)
    : sceneObjectBase(model, 6 * ptsInitial.size() + 4),m_pts(ptsInitial),
      m_selected(ptsInitial.size() + 1, false),m_hovered(ptsInitial.size() + 1, false)
  {
    m_center = glm::vec2(0.f);
    for (const glm::vec2 &pt : ptsInitial)
      m_center += pt;
    m_center *= ptsInitial.empty() ? 0.f : 1.f/ptsInitial.size();
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    for (unsigned i = 0; i < m_pts.size(); ++i)
    {
      bool selected = m_selected[i];
      bool hovered = m_hovered[i];
      treatEventPt(mousePos, mouseLeft, m_pts[i], selected, hovered, hasSelection);
      m_selected[i] = selected;
      m_hovered[i] = hovered;
    }
    {
      bool selected = m_selected.back();
      bool hovered = m_hovered.back();
      const glm::vec2 prevCenter = m_center;
      treatEventPt(mousePos, mouseLeft, m_center, selected, hovered, hasSelection);
      m_selected.back() = selected;
      m_hovered.back() = hovered;
      if (selected)
      {
        const glm::vec2 move = m_center - prevCenter;
        for (glm::vec2 &pt : m_pts)
          pt += move;
      }
    }
  }
  virtual void updateForDraw()
  {
    glm::vec2 prevPt = m_pts.back();
    for (unsigned i = 0; i < m_pts.size(); ++i)
    {
      const glm::vec2 &currPt = m_pts[i];
      const glm::vec4 &col = m_selected[i] ? magenta : (m_hovered[i] ? yellow : white);
      m_model->fillDataLine(m_part, i * 6 + 0, currPt.x - CSIZE, currPt.y, currPt.x + CSIZE, currPt.y, col);
      m_model->fillDataLine(m_part, i * 6 + 2, currPt.x, currPt.y - CSIZE, currPt.x, currPt.y + CSIZE, col);
      m_model->fillDataLine(m_part, i * 6 + 4, prevPt.x, prevPt.y, currPt.x, currPt.y, grey);
      prevPt = currPt;
    }
    if (m_selected.back() || m_hovered.back())
    {
      const glm::vec4 &col = m_selected.back() ? magenta : (m_hovered.back() ? yellow : white);
      m_model->fillDataLine(m_part, m_pts.size() * 6 + 0, m_center.x - CSIZE, m_center.y, m_center.x + CSIZE, m_center.y, col);
      m_model->fillDataLine(m_part, m_pts.size() * 6 + 2, m_center.x, m_center.y - CSIZE, m_center.x, m_center.y + CSIZE, col);
    }
    else
    {
      m_model->fillDataLine(m_part, m_pts.size() * 6 + 0, m_center.x - CSIZE * 0.5f, m_center.y, m_center.x + CSIZE * 0.5f, m_center.y, glm::vec4(0.1f));
      m_model->fillDataLine(m_part, m_pts.size() * 6 + 2, m_center.x, m_center.y - CSIZE * 0.5f, m_center.x, m_center.y + CSIZE * 0.5f, glm::vec4(0.1f));
    }
  }
  virtual eObjectType type() const { return POLY; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  std::vector<bool>      m_selected;
  std::vector<bool>      m_hovered;
  std::vector<glm::vec2> m_pts;
  glm::vec2              m_center;
};

// ----------------------------------------------------------------------------

class sceneObjectCircle : public sceneObjectBase
{
public:
  sceneObjectCircle(tre::modelRaw2D *model, glm::vec2 centerInitial = glm::vec2(0.f), float radiusInitial = 0.1f)
    : sceneObjectBase(model, 52),m_center(centerInitial),m_radiusPt(centerInitial + glm::vec2(radiusInitial, 0.f))
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    glm::vec2 pt = m_center;
    treatEventPt(mousePos, mouseLeft, pt, m_selected[0], m_hovered[0], hasSelection);
    if (m_selected[0])
    {
      m_radiusPt += (pt - m_center);
      m_center = pt;
    }
    treatEventPt(mousePos, mouseLeft, m_radiusPt, m_selected[1], m_hovered[1], hasSelection);
  }
  virtual void updateForDraw()
  {
    const glm::vec4 &colA = m_selected[0] ? magenta : (m_hovered[0] ? yellow : white);
    m_model->fillDataLine(m_part, 0, m_center.x - CSIZE, m_center.y, m_center.x + CSIZE, m_center.y, colA);
    m_model->fillDataLine(m_part, 2, m_center.x, m_center.y - CSIZE, m_center.x, m_center.y + CSIZE, colA);
    const glm::vec4 &colB = m_selected[1] ? magenta : (m_hovered[1] ? yellow : white);
    m_model->fillDataLine(m_part, 4, m_radiusPt.x - CSIZE, m_radiusPt.y, m_radiusPt.x + CSIZE, m_radiusPt.y, colB);
    m_model->fillDataLine(m_part, 6, m_radiusPt.x, m_radiusPt.y - CSIZE, m_radiusPt.x, m_radiusPt.y + CSIZE, colB);

    const float radius = glm::length(m_radiusPt - m_center);
    glm::vec2 prevPt = m_center + glm::vec2(radius, 0.f);
    for (unsigned i = 1; i <= 22; ++i)
    {
      const glm::vec2 currPt = m_center + radius * glm::vec2(cosf(i * 3.14f / 11.f), sinf(i * 3.14f / 11.f));
      m_model->fillDataLine(m_part, 6 + i * 2, prevPt, currPt, grey);
      prevPt = currPt;
    }
  }
  virtual eObjectType type() const { return CIRCLE; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  std::array<bool,2> m_selected;
  std::array<bool,2> m_hovered;
  glm::vec2          m_center;
  glm::vec2          m_radiusPt;
};

// ----------------------------------------------------------------------------

class sceneObjectRay : public sceneObjectBase
{
public:
  sceneObjectRay(tre::modelRaw2D *model, glm::vec2 originInitial = glm::vec2(0.f), glm::vec2 directionInitial = glm::vec2(1.f, 0.f))
    : sceneObjectBase(model, 10),m_origin(originInitial),m_direction(directionInitial)
  {
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection)
  {
    treatEventPt(mousePos, mouseLeft, m_origin, m_selected[0], m_hovered[0], hasSelection);
    glm::vec2 ptDirection = m_origin + 0.1f * m_direction;
    treatEventPt(mousePos, mouseLeft, ptDirection, m_selected[1], m_hovered[1], hasSelection);
    if (m_selected[1])
      m_direction = (ptDirection != m_origin) ? glm::normalize(ptDirection - m_origin) : glm::vec2(1.f, 0.f);
  }
  virtual void updateForDraw()
  {
    const glm::vec4 &colA = m_selected[0] ? magenta : (m_hovered[0] ? yellow : white);
    m_model->fillDataLine(m_part, 0, m_origin.x - CSIZE, m_origin.y, m_origin.x + CSIZE, m_origin.y, colA);
    m_model->fillDataLine(m_part, 2, m_origin.x, m_origin.y - CSIZE, m_origin.x, m_origin.y + CSIZE, colA);
    const glm::vec4 &colB = m_selected[1] ? magenta : (m_hovered[1] ? yellow : white);
    const glm::vec2 ptDirection = m_origin + 0.1f * m_direction;
    m_model->fillDataLine(m_part, 4, ptDirection.x - CSIZE, ptDirection.y, ptDirection.x + CSIZE, ptDirection.y, colB);
    m_model->fillDataLine(m_part, 6, ptDirection.x, ptDirection.y - CSIZE, ptDirection.x, ptDirection.y + CSIZE, colB);

    m_model->fillDataLine(m_part, 8, m_origin, m_origin + 0.3f * m_direction, grey);
  }
  virtual eObjectType type() const { return RAY; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const;
public:
  std::array<bool,2> m_selected;
  std::array<bool,2> m_hovered;
  glm::vec2          m_origin;
  glm::vec2          m_direction;
};
