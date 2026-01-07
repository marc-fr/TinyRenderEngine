
#include "tre_contact_2D.h"

#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_shader.h"
#include "tre_windowContext.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <random>
#include <chrono>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

#define TEST_WITH_UNITTEST

#ifdef TEST_WITH_UNITTEST
#include "tre_font.h"
#include "tre_textgenerator.h"
#include <thread>

tre::shader     ui_shader;
tre::font       ui_font;
tre::modelRaw2D ui_mesh;
#endif

typedef std::chrono::steady_clock systemclock;
typedef systemclock::time_point   systemtick;

std::uniform_real_distribution kRand01(0.f, 1.f);

// Benchmark and Unit-Test =============================================

namespace unittest
{
  struct s_coverageTest
  {
    float m_count = 0.f; ///< total contact queries done
    float m_coverage = 0.f; ///< ratio of contact hit over total surface
    float m_elapsedTime = std::numeric_limits<float>::infinity(); ///< faster elapsed time per contact query [s]

    std::mt19937 m_localRNG;

    static constexpr std::size_t res = 128;

    template <class _fnct> void runBatch(const _fnct &fnct)
    {
      constexpr std::size_t res2 = res * res;
      constexpr float oneOverRes2 = 1.f / float(res2);
      constexpr float twoOverRes  = 2.f / float(res);
      const glm::vec2 offsetPos = glm::vec2(-1.f + twoOverRes * kRand01(m_localRNG), -1.f + twoOverRes * kRand01(m_localRNG));
      std::size_t nHit = 0;
      const systemtick tStart = systemclock::now();
      for (std::size_t i = 0; i < res; ++i)
      {
        for (std::size_t j = 0; j < res; ++j)
        {
          const glm::vec2 pos = offsetPos + twoOverRes * glm::vec2(float(i), float(j));
          const bool hit = fnct(pos);
          if (hit) ++nHit;
        }
      }
      const systemtick tEnd = systemclock::now();
      const float      tpc = std::chrono::duration<float, std::milli>(tEnd - tStart).count() * (1.e-3f * oneOverRes2);
      m_coverage = m_coverage * m_count + float(nHit);
      m_count += float(res2);
      m_coverage /= m_count;
      m_elapsedTime = std::min(m_elapsedTime, tpc);
    }

    void printInfo(char *txt, std::size_t txtSize, const char *msg, const float area) const
    {
      std::snprintf(txt, txtSize, "%s: %d k/ms, cov: %.02e (c:%.0f M)", msg, int(1.e-6f / m_elapsedTime), std::abs(m_coverage / (0.25f * area) - 1.f), m_count * 1.e-6f);
    }
  };

  static const glm::vec4 box = glm::vec4(-0.8f, -0.8f, 0.4f, 0.9f);
  static const glm::vec2 circleBase_c = glm::vec2(0.1f, 0.f);
  static const float     circleBase_r = 0.5f;
  static const glm::vec2 triA = glm::vec2(-0.8f, -0.7f);
  static const glm::vec2 triB = glm::vec2( 0.9f, -0.6f);
  static const glm::vec2 triC = glm::vec2( 0.7f,  0.5f);
  static const std::vector<glm::vec2> poly5Base = { glm::vec2(-0.9f, -0.9f), glm::vec2(0.f, -0.5f), glm::vec2(0.8f, -0.1f), glm::vec2(0.2f, 0.7f), glm::vec2(-0.2f,  0.7f) };

  static const float areaTri = 0.5f * std::abs((triB.x - triA.x) * (triC.y - triA.y) - (triC.x - triA.x) * (triB.y - triA.y));
  static const float areaBox = (box.z - box.x) * (box.w - box.y);
  static const float areaCircle = 3.14159265358979323846f * circleBase_r * circleBase_r;
  static const float areaPoly = 0.5f * std::abs(poly5Base[0].x * poly5Base[1].y - poly5Base[1].x * poly5Base[0].y +
                                                poly5Base[1].x * poly5Base[2].y - poly5Base[2].x * poly5Base[1].y +
                                                poly5Base[2].x * poly5Base[3].y - poly5Base[3].x * poly5Base[2].y +
                                                poly5Base[3].x * poly5Base[4].y - poly5Base[4].x * poly5Base[3].y +
                                                poly5Base[4].x * poly5Base[0].y - poly5Base[0].x * poly5Base[4].y);

  s_coverageTest cvrgPointTri;
  auto           fnctPointTri = [](const glm::vec2 &pt) -> bool  { return tre::s_contact2D::point_tri(pt, triA, triB, triC); };

  s_coverageTest cvrgPointTri_info;
  auto           fnctPointTri_info = [](const glm::vec2 &pt) -> bool  { tre::s_contact2D hInfo;  return tre::s_contact2D::point_tri(hInfo, pt, triA, triB, triC); };

  s_coverageTest cvrgPointBox;
  auto           fnctPointBox = [](const glm::vec2 &pt) -> bool  { return tre::s_contact2D::point_box(pt, box); };

  s_coverageTest cvrgPointBox_info;
  auto           fnctPointBox_info = [](const glm::vec2 &pt) -> bool  { tre::s_contact2D hInfo; return tre::s_contact2D::point_box(hInfo, pt, box); };

  s_coverageTest cvrgPointCircle;
  auto           fnctPointCircle = [](const glm::vec2 &pt) -> bool  { return tre::s_contact2D::point_circle(pt, circleBase_c, circleBase_r); };

  s_coverageTest cvrgPointCircle_info;
  auto           fnctPointCircle_info = [](const glm::vec2 &pt) -> bool  { tre::s_contact2D hInfo; return tre::s_contact2D::point_circle(hInfo, pt, circleBase_c, circleBase_r); };

  s_coverageTest cvrgPointPoly;
  auto           fnctPointPoly = [](const glm::vec2 &pt) -> bool  { return tre::s_contact2D::point_poly(pt, poly5Base); };

  s_coverageTest cvrgPointPoly_info;
  auto           fnctPointPoly_info = [](const glm::vec2 &pt) -> bool  { tre::s_contact2D hInfo; return tre::s_contact2D::point_poly(hInfo, pt, poly5Base); };



#ifdef TEST_WITH_UNITTEST
  bool        ut_Continue = true;
  std::thread ut_thread;
  //SDL_Thread  *ut_thread = nullptr;
  static int processThread(void *)
  {
    while (ut_Continue)
    {
      cvrgPointTri.runBatch(fnctPointTri);
      cvrgPointTri_info.runBatch(fnctPointTri_info);
      cvrgPointBox.runBatch(fnctPointBox);
      cvrgPointBox_info.runBatch(fnctPointBox_info);
      cvrgPointCircle.runBatch(fnctPointCircle);
      cvrgPointCircle_info.runBatch(fnctPointCircle_info);
      cvrgPointPoly.runBatch(fnctPointPoly);
      cvrgPointPoly_info.runBatch(fnctPointPoly_info);

      if (cvrgPointTri.m_count > 1.e8f) ut_Continue = false; // start loosing precision, stop here
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return 0;
  }
  void launchThread()
  {
    TRE_LOG("unit-test thread will be launched ...");
    ut_thread = std::thread(&processThread, nullptr);
    //ut_thread = SDL_CreateThread(&processThread, "ut process", nullptr);
  }
  void stopThread()
  {
    ut_Continue = false;
    ut_thread.join();
    //int st = 0; SDL_WaitThread(ut_thread, &st);
  }
#endif
}

// Scene implementation =======================================================

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
  sceneObjectBase(tre::modelRaw2D *model, std::size_t partSize) : m_model(model)
  {
    m_part = model->createPart(partSize);
  }
  virtual void treatEvent(glm::vec2 mousePos, bool mouseLeft, bool &hasSelection) = 0;
  virtual void updateForDraw() = 0;

  enum eObjectType {POINT, LINE, YLINE, BOX, POLY, CIRCLE, RAY};
  virtual eObjectType type() const = 0;

  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const { return false; }

protected:
  void treatEventPt(glm::vec2 mousePos, bool mouseLeft, glm::vec2 &pt, bool &isSelected, bool &isHovered, bool &hasSelection);
  tre::modelRaw2D * const m_model;
  std::size_t             m_part;
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
  sceneObjectPoly(tre::modelRaw2D *model)
    : sceneObjectBase(model, 0) {}

  void setPts(const std::vector<glm::vec2> &pts)
  {
    m_pts = pts; // copy
    m_center = glm::vec2(0.f);
    for (const glm::vec2 &pt : pts) m_center += pt;
    m_center *= pts.empty() ? 0.f : 1.f/m_pts.size();
    m_selected.resize(pts.size() + 1, false);
    m_hovered.resize(pts.size() + 1, false);
    m_model->resizePart(m_part, 6 * pts.size() + 4);
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

// --------------------------------------------------------------------------

void sceneObjectBase::treatEventPt(glm::vec2 mousePos, bool mouseLeft, glm::vec2 &pt, bool &isSelected, bool &isHovered, bool &hasSelection)
{
  if (mouseLeft == false)
  {
    isSelected = false;
    hasSelection = false;
  }
  if (isSelected)
  {
    pt = mousePos;
  }
  else if (!hasSelection)
  {
    isHovered = false;

    if (mousePos.x - CSIZE <= pt.x && pt.x <= mousePos.x + CSIZE &&
        mousePos.y - CSIZE <= pt.y && pt.y <= mousePos.y + CSIZE)
    {
        if (mouseLeft == true)
        {
          isSelected = true;
          hasSelection = true;
        }
        else
        {
          isHovered = true;
        }
    }
  }
}

// ------------------------------

bool sceneObjectPoint::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case YLINE:
  {
    auto otherY = static_cast<const sceneObjectYDown *>(&other);
    return tre::s_contact2D::point_ydown(cnt, m_pt, otherY->m_pt.y);
  }
  case BOX:
  {
    auto otherB = static_cast<const sceneObjectBox *>(&other);
    const bool retB = tre::s_contact2D::point_box(cnt, m_pt, otherB->m_box);
    TRE_ASSERT(retB == tre::s_contact2D::point_box(m_pt, otherB->m_box))
    return retB;
  }
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    const bool retC = tre::s_contact2D::point_circle(cnt, m_pt, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
    TRE_ASSERT(retC == tre::s_contact2D::point_circle(m_pt, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center)))
    return retC;
  }
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    const bool retP = tre::s_contact2D::point_poly(cnt, m_pt, otherP->m_pts);
    TRE_ASSERT(retP == tre::s_contact2D::point_poly(m_pt, otherP->m_pts));
    return retP;
  }
  default:
    return false;
  }
}

// --------------------------------------

bool sceneObjectBox::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case BOX:
  {
    auto otherB = static_cast<const sceneObjectBox *>(&other);
    cnt.pt = glm::vec2(-100.f, -100.f);
    cnt.penet = 0.f;
    return tre::s_contact2D::box_box(m_box, otherB->m_box);
  }
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    const bool retC = tre::s_contact2D::box_circle(cnt, m_box, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
    TRE_ASSERT(retC == tre::s_contact2D::box_circle(m_box, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center)));
    return retC;
  }
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    const bool retP = tre::s_contact2D::box_poly(cnt, m_box, otherP->m_pts);
    TRE_ASSERT(retP == tre::s_contact2D::box_poly(m_box, otherP->m_pts));
    return retP;
  }
  default:
    return false;
  }
}

// --------------------------------------

bool sceneObjectLine::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  cnt.penet = 0.f;
  cnt.normal = glm::vec2(0.f, 0.f);
  switch(other.type())
  {
  case LINE:
  {
    auto otherL = static_cast<const sceneObjectLine *>(&other);
    return tre::s_contact2D::cross_line_line(cnt.pt, m_pts[0], m_pts[1], otherL->m_pts[0], otherL->m_pts[1]);
  }
  case YLINE:
  {
    auto otherY = static_cast<const sceneObjectYDown *>(&other);
    return tre::s_contact2D::cross_yline_line(cnt.pt, otherY->m_pt.y, m_pts[0], m_pts[1]);
  }
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    std::vector<glm::vec2> cnts;
    const bool b = tre::s_contact2D::cross_line_circle(cnts, m_pts[0], m_pts[1], otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
    if (b) cnt.pt = cnts[0];
    return b;
  }
  default:
    return false;

  }
}

// --------------------------------------

bool sceneObjectYDown::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    return tre::s_contact2D::ydown_circle(cnt, m_pt.y, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
  }
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    return tre::s_contact2D::ydown_poly(cnt, m_pt.y, otherP->m_pts);
  }
  default:
    return false;
  }
}

// --------------------------------------

bool sceneObjectPoly::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    const bool retP = tre::s_contact2D::poly_poly(cnt, m_pts, otherP->m_pts);
    TRE_ASSERT(retP == tre::s_contact2D::poly_poly(m_pts, otherP->m_pts));
    return retP;
  }
  default:
    return false;
  }
}

// ------------------------------

bool sceneObjectCircle::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    return tre::s_contact2D::circle_circle(cnt, m_center, glm::length(m_radiusPt - m_center), otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
  }
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    const bool retP = tre::s_contact2D::circle_poly(cnt, m_center, glm::length(m_radiusPt - m_center), otherP->m_pts);
    TRE_ASSERT(retP == tre::s_contact2D::circle_poly(m_center, glm::length(m_radiusPt - m_center), otherP->m_pts));
    return retP;
  }
  default:
    return false;
  }
}

// ------------------------------

bool sceneObjectRay::isContactWith(const sceneObjectBase &other, tre::s_contact2D &cnt) const
{
  switch(other.type())
  {
  case YLINE:
  {
    auto otherY = static_cast<const sceneObjectYDown *>(&other);
    return tre::s_contact2D::raytrace_ydown(cnt, m_origin, m_direction, otherY->m_pt.y);
  }
  case BOX:
  {
    auto otherB = static_cast<const sceneObjectBox *>(&other);
    return tre::s_contact2D::raytrace_box(cnt, m_origin, m_direction, otherB->m_box);
  }
  case CIRCLE:
  {
    auto otherC = static_cast<const sceneObjectCircle *>(&other);
    return tre::s_contact2D::raytrace_circle(cnt, m_origin, m_direction, otherC->m_center, glm::length(otherC->m_radiusPt - otherC->m_center));
  }
  case POLY:
  {
    auto otherP = static_cast<const sceneObjectPoly *>(&other);
    return tre::s_contact2D::raytrace_poly(cnt, m_origin, m_direction, otherP->m_pts);
  }
  default:
    return false;
  }
}

// ============================================================================

tre::windowContext myWindow;
tre::windowContext::s_controls myControls;
tre::windowContext::s_view2D myView2D(&myWindow);

tre::shader shaderSolid;

tre::modelRaw2D                         meshDraw;
tre::arrayCounted<sceneObjectBase*, 64> sceneObjects;
std::size_t                             partCnt;

sceneObjectPoint  objectPoint (&meshDraw, glm::vec2(1.f, 0.f));
sceneObjectBox    objectBox   (&meshDraw, glm::vec4(0.4f, -0.7f, 1.0f, -0.5f));
sceneObjectLine   objectLine1 (&meshDraw, glm::vec2(-1.2f, -0.5f), glm::vec2(-1.2f, 0.f));
sceneObjectLine   objectLine2 (&meshDraw, glm::vec2(-1.0f, -0.5f), glm::vec2(-1.0f, 0.f));
sceneObjectYDown  objectLineY (&meshDraw, -0.9f);
sceneObjectCircle objectCircle(&meshDraw, glm::vec2(0.8f, 0.7f), 0.2f);
sceneObjectPoly   objectPoly1 (&meshDraw);
sceneObjectPoly   objectPolyM (&meshDraw);
sceneObjectRay    objectRay   (&meshDraw, glm::vec2(-1.1f, 0.5f), glm::vec2(0.f, 1.f));

// ----------------------------------------------------------------------------

int app_init(int argc, char **argv)
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Contact 2D", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // arguments
  std::string addmodel3D_path = TESTIMPORTPATH "resources/objects.obj";
  std::string addmodel3D_pname = "CubeSmoothed";

  if (argc > 1) addmodel3D_path = argv[1];
  if (argc > 2) addmodel3D_pname = argv[2];

  // set pipeline state
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.f,0.f,0.f,0.f);

  // Shader
  shaderSolid.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR);

  // Scene
  sceneObjects.push_back(&objectPoint);
  sceneObjects.push_back(&objectBox);
  sceneObjects.push_back(&objectLine1);
  sceneObjects.push_back(&objectLine2);
  sceneObjects.push_back(&objectLineY);
  sceneObjects.push_back(&objectCircle);
  sceneObjects.push_back(&objectPoly1);
  sceneObjects.push_back(&objectRay);

  {
    std::vector<glm::vec2> tmpPts(4);
    tmpPts[0] = {-0.8f, 0.6f};
    tmpPts[1] = {-0.5f, 0.6f};
    tmpPts[2] = {-0.6f, 0.8f};
    tmpPts[3] = {-0.7f, 0.8f};
    objectPoly1.setPts(tmpPts);
  }


  // Import mesh (optionnal)
  std::vector<glm::vec2> tmpMeshEnvelop;
  tre::modelStaticIndexed3D importedMesh;
  bool importedMeshValid = tre::modelImporter::addFromWavefront(importedMesh, addmodel3D_path);
  if (importedMeshValid) importedMeshValid = importedMesh.reorganizeParts({ addmodel3D_pname });
  if (importedMeshValid)
  {
    tre::modelTools::computeConvexeEnvelop2D_XY(importedMesh.layout(), importedMesh.partInfo(0), glm::mat4(1.f), 1.e-2f, tmpMeshEnvelop);
    const tre::s_boundbox &bbox = importedMesh.partInfo(0).m_bbox;
    const glm::vec2 scale = 1.f / (glm::vec2(bbox.extend()) + 1.e-6f);
    const glm::vec2 add   = scale * glm::vec2(bbox.center());
    for (glm::vec2 &pt : tmpMeshEnvelop)
      pt = scale * pt + add;
    objectPolyM.setPts(tmpMeshEnvelop);
    sceneObjects.push_back(&objectPolyM);
  }

  // End scene creation

  partCnt = meshDraw.createPart(128);

  meshDraw.loadIntoGPU();

  myView2D.setKeyBinding(true);
  myView2D.setScreenBoundsMotion(true);

  // UnitTest

#ifdef TEST_WITH_UNITTEST
  {
    ui_shader.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_TEXTURED);
    if (!ui_font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true))
      ui_font.load({ tre::font::loadProceduralLed(2,1) }, true);
    ui_mesh.loadIntoGPU();

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, ui_font.get_texture().m_handle);
  }
  unittest::launchThread();
#endif

  return 0;
}

// ----------------------------------------------------------------------------

bool hasSelection = false;

void app_update()
{
  SDL_Event rawEvent;

  {
    myWindow.SDLEvent_newFrame();
    myControls.newFrame();
    // Event
    while(SDL_PollEvent(&rawEvent) == 1)
    {
      myWindow.SDLEvent_onWindow(rawEvent); // window resize ...
      myControls.treatSDLEvent(rawEvent); // record mouse and keyboard events
    }

    if (myWindow.m_hasFocus)
      myView2D.treatControlEvent(myControls, 0.17f /* about 60 fps*/);

    if (myControls.m_mouseRIGHT & tre::windowContext::s_controls::MASK_BUTTON_RELEASED)
      myView2D.setMouseBinding(!myView2D.m_mouseBound);

    const glm::mat3 matPV = myWindow.m_matProjection2D * myView2D.m_matView;
    const glm::vec2 mouseClipSpace = glm::vec2(myControls.m_mouse) * myWindow.m_resolutioncurrentInv * glm::vec2(2.f, -2.f) + glm::vec2(-1.f, 1.f);
    const glm::vec2 mouseWordSpace = glm::inverse(matPV) * glm::vec3(mouseClipSpace, 1.f);

    // Event treatment
    if (!myView2D.m_mouseBound)
    {
      for (sceneObjectBase *oBase : sceneObjects)
        oBase->treatEvent(mouseWordSpace, myControls.m_mouseLEFT & tre::windowContext::s_controls::MASK_BUTTON_PRESSED, hasSelection);
    }

    // Compute contact ...
    std::vector<tre::s_contact2D> cntsList;
    cntsList.reserve(sceneObjects.size() * sceneObjects.size());

    for (const sceneObjectBase *objectA : sceneObjects)
    {
      for (const sceneObjectBase *objectB : sceneObjects)
      {
        if (objectA == objectB)
          continue;
        tre::s_contact2D cnt;
        if (objectA->isContactWith(*objectB, cnt))
          cntsList.push_back(cnt);
      }
    }

    // Updates of draw

    for (sceneObjectBase *oBase : sceneObjects)
      oBase->updateForDraw();

    // -> all contacts
    meshDraw.resizePart(partCnt, 8 * cntsList.size());
    unsigned iC = 0;
    for (const tre::s_contact2D &cnt : cntsList)
    {
      meshDraw.fillDataLine(partCnt, iC + 0, cnt.pt.x - CSIZECNT, cnt.pt.y - CSIZECNT, cnt.pt.x + CSIZECNT, cnt.pt.y + CSIZECNT, green);
      meshDraw.fillDataLine(partCnt, iC + 2, cnt.pt.x - CSIZECNT, cnt.pt.y + CSIZECNT, cnt.pt.x + CSIZECNT, cnt.pt.y - CSIZECNT, green);
      meshDraw.fillDataLine(partCnt, iC + 4, cnt.pt, cnt.pt + cnt.normal * 0.2f, darkblue);
      meshDraw.fillDataLine(partCnt, iC + 6, cnt.pt, cnt.pt + cnt.normal * cnt.penet, darkgreen);
      iC += 8;
    }

    meshDraw.updateIntoGPU();

#ifdef TEST_WITH_UNITTEST
    {
      ui_mesh.clearParts();

      glm::vec2 pos = glm::vec2(0.f, 0.f);
      char txt[128];
      tre::textgenerator::s_textInfo tInfo;
      tre::textgenerator::s_textInfoOut tOut;
      tInfo.setupBasic(&ui_font, txt, pos);
      tInfo.setupSize(0.04f);
      tInfo.m_fontPixelSize = 16;

      unittest::cvrgPointTri.printInfo(txt, 128, "UT Point-Tri", unittest::areaTri);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointTri_info.printInfo(txt, 128, "UT Point-Tri (info)", unittest::areaTri);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointBox.printInfo(txt, 128, "UT Point-Box", unittest::areaBox);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointBox_info.printInfo(txt, 128, "UT Point-Box (info)", unittest::areaBox);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointCircle.printInfo(txt, 128, "UT Point-Circle", unittest::areaCircle);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointCircle_info.printInfo(txt, 128, "UT Point-Circle (info)", unittest::areaCircle);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointPoly.printInfo(txt, 128, "UT Point-Poly", unittest::areaPoly);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointPoly_info.printInfo(txt, 128, "UT Point-Poly (info)", unittest::areaPoly);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      ui_mesh.updateIntoGPU();
    }
#endif

    // Draw

    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderSolid.m_drawProgram);
    shaderSolid.setUniformMatrix(matPV);
    meshDraw.drawcallAll(true, GL_LINES);

#ifdef TEST_WITH_UNITTEST
    glUseProgram(ui_shader.m_drawProgram);
    {
      glm::mat3 mViewModel_hud = glm::mat3(1.f);
      mViewModel_hud[0][0] = 1.f;
      mViewModel_hud[1][1] = 1.f;
      mViewModel_hud[2][0] =  -0.98f / myWindow.m_matProjection2D[0][0];
      mViewModel_hud[2][1] =   0.98f;
      ui_shader.setUniformMatrix(myWindow.m_matProjection2D * mViewModel_hud);
    }
    glUniform1i(ui_shader.getUniformLocation(tre::shader::TexDiffuse), 2);
    ui_mesh.drawcallAll();
#endif

    // End

    SDL_GL_SwapWindow(myWindow.m_window);
  }
}

// -----------------------------------------------------------------------------

void app_quit()
{
  meshDraw.clearGPU();

  shaderSolid.clearShader();

#ifdef TEST_WITH_UNITTEST
  ui_shader.clearShader();
  ui_font.clear();
  ui_mesh.clearGPU();
  unittest::stopThread();
#endif

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();
}

// ============================================================================

int main(int argc, char **argv)
{
  if (app_init(argc, argv) != 0)
    return -1;

#ifdef TRE_EMSCRIPTEN
  emscripten_set_main_loop(app_update, 0, true);
#else
  while(!myWindow.m_quit && !myControls.m_quit)
  {
    app_update();
  }

  app_quit();

#endif

  return 0;
}
