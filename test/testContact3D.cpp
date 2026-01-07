
#include "tre_contact_3D.h"

#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_shader.h"
#include "tre_gizmo.h"
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

    static constexpr std::size_t res = 16;

    template <class _fnct> void runBatch(const _fnct &fnct)
    {
      constexpr std::size_t res3 = res * res * res;
      constexpr float oneOverRes3 = 1.f / float(res3);
      constexpr float twoOverRes  = 2.f / float(res);
      const glm::vec3 offsetPos = glm::vec3(-1.f + twoOverRes * kRand01(m_localRNG), -1.f + twoOverRes * kRand01(m_localRNG), -1.f + twoOverRes * kRand01(m_localRNG));
      std::size_t nHit = 0;
      const systemtick tStart = systemclock::now();
      for (std::size_t i = 0; i < res; ++i)
      {
        for (std::size_t j = 0; j < res; ++j)
        {
          for (std::size_t k = 0; k < res; ++k)
          {
            const glm::vec3 pos = offsetPos + twoOverRes * glm::vec3(float(i), float(j), float(k));
            const bool hit = fnct(pos);
            if (hit) ++nHit;
          }
        }
      }
      const systemtick tEnd = systemclock::now();
      const float      tpc = std::chrono::duration<float, std::milli>(tEnd - tStart).count() * (1.e-3f * oneOverRes3);
      m_coverage = m_coverage * m_count + float(nHit);
      m_count += float(res3);
      m_coverage /= m_count;
      m_elapsedTime = std::min(m_elapsedTime, tpc);
    }

    void printInfo(char *txt, std::size_t txtSize, const char *msg, const float volume) const
    {
      std::snprintf(txt, txtSize, "%s: %d k/ms, cov: %.02e (c:%.0f M)", msg, int(1.e-6f / m_elapsedTime), std::abs(m_coverage / (0.125f * volume) - 1.f), m_count * 1.e-6f);
    }
  };

  static const tre::s_boundbox box = tre::s_boundbox(glm::vec3(-0.8f, -0.7f, -0.4f), glm::vec3( 0.8f,  0.3f,  0.9f));
  static const glm::vec3 circleBase_c = glm::vec3(0.1f, 0.f, -0.03f);
  static const float     circleBase_r = 0.5f;
  static const glm::vec3 tetraA = glm::vec3(-0.8f, -0.7f, -0.5f);
  static const glm::vec3 tetraB = glm::vec3( 0.9f, -0.6f, -0.4f);
  static const glm::vec3 tetraC = glm::vec3( 0.7f,  0.5f, -0.6f);
  static const glm::vec3 tetraD = glm::vec3( 0.1f, -0.2f,  0.8f);

  static const float volumeTetra = glm::abs(glm::dot(tetraB - tetraA, glm::cross(tetraC - tetraA, tetraD - tetraA))) / 6.f;
  static const float volumeBox = (box.m_max.x - box.m_min.x) * (box.m_max.y - box.m_min.y) * (box.m_max.z - box.m_min.z);
  static const float volumeSphere = (4.f / 3.f) * 3.14159265358979323846f * circleBase_r * circleBase_r * circleBase_r;

  s_coverageTest cvrgPointTetra;
  auto           fnctPointTetra = [](const glm::vec3 &pt) -> bool  { return tre::s_contact3D::point_treta(pt, tetraA, tetraB, tetraC, tetraD); };

  s_coverageTest cvrgPointTetra_info;
  auto           fnctPointTetra_info = [](const glm::vec3 &pt) -> bool  { tre::s_contact3D hInfo;  return tre::s_contact3D::point_treta(hInfo, pt, tetraA, tetraB, tetraC, tetraD); };

  s_coverageTest cvrgPointBox;
  auto           fnctPointBox = [](const glm::vec3 &pt) -> bool  { return tre::s_contact3D::point_box(pt, box); };

  s_coverageTest cvrgPointBox_info;
  auto           fnctPointBox_info = [](const glm::vec3 &pt) -> bool  { tre::s_contact3D hInfo; return tre::s_contact3D::point_box(hInfo, pt, box); };

  s_coverageTest cvrgPointSphere;
  auto           fnctPointSphere = [](const glm::vec3 &pt) -> bool  { return tre::s_contact3D::point_sphere(pt, circleBase_c, circleBase_r); };

  s_coverageTest cvrgPointSphere_info;
  auto           fnctPointSphere_info = [](const glm::vec3 &pt) -> bool  { tre::s_contact3D hInfo; return tre::s_contact3D::point_sphere(hInfo, pt, circleBase_c, circleBase_r); };

#ifdef TEST_WITH_UNITTEST
  bool        ut_Continue = true;
  std::thread ut_thread;
  //SDL_Thread  *ut_thread = nullptr;
  static int processThread(void *)
  {
    while (ut_Continue)
    {
      cvrgPointTetra.runBatch(fnctPointTetra);
      cvrgPointTetra_info.runBatch(fnctPointTetra_info);
      cvrgPointBox.runBatch(fnctPointBox);
      cvrgPointBox_info.runBatch(fnctPointBox_info);
      cvrgPointSphere.runBatch(fnctPointSphere);
      cvrgPointSphere_info.runBatch(fnctPointSphere_info);

      if (cvrgPointTetra.m_count > 1.e8f) ut_Continue = false; // start loosing precision, stop here
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

// Constants ==================================================================

#define CSIZE    0.04f
#define CSIZECNT 0.01f

static const glm::vec4 white     = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
static const glm::vec4 grey      = glm::vec4(0.3f, 0.3f, 0.3f, 1.f);

// Scene-Object implementation ================================================

class sceneObjectBase
{
public:
  sceneObjectBase(tre::modelIndexed *model) : m_model(model) {}

  virtual void updateForDraw(const bool isHovered);
  virtual void draw(const tre::shader &usedShader, const glm::mat4 &mProj, const glm::mat4 &mView) const;

  enum eObjectType {POINT, TETRA, BOX, SKIN, SPHERE, RAY};
  virtual eObjectType type() const = 0;

  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const { return false; }
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const { return false; }

  void set_position(const glm::vec3 &newpos) { m_transform[3] = glm::vec4(newpos, 1.f); } // Helper
  glm::mat4 *transformPtr() { return &m_transform; }

protected:
  tre::modelIndexed *m_model = nullptr;
  std::size_t        m_part = std::size_t(-1);
  glm::mat4          m_transform = glm::mat4(1.f);
};

// ----------------------------------------------------------------------------

class sceneObjectPoint : public sceneObjectBase
{
public:
  sceneObjectPoint(tre::modelIndexed *model) : sceneObjectBase(model)
  {
    m_part = model->createPartFromPrimitive_box(glm::mat4(1.f), 0.05f);
  }

  virtual eObjectType type() const override { return POINT; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  glm::vec3 get_point() const { return glm::vec3(m_transform[3]); }
};

// ----------------------------------------------------------------------------

class sceneObjectTetra : public sceneObjectBase
{
public:
  sceneObjectTetra(tre::modelIndexed *model, const glm::vec3 &pt0, const glm::vec3 &pt1, const glm::vec3 &pt2, const glm::vec3 &pt3) : sceneObjectBase(model)
  {
    m_part = model->createRawPart(12);
    m_ptsTri_skin.resize(12);

    // fill vertex-data by hand
    const std::size_t offset = model->partInfo(m_part).m_offset;
    auto posIt = model->layout().m_positions.begin<glm::vec3>(model->layout().m_index[offset]);
    auto normalIt = model->layout().m_normals.begin<glm::vec3>(model->layout().m_index[offset]);

    // tri-012
    const glm::vec3 v01 = pt1 - pt0;
    const glm::vec3 v02 = pt2 - pt0;
    const glm::vec3 v03 = pt3 - pt0;
    {
      const glm::vec3 n012 = glm::cross(v01, v02);
      const float     spin = glm::dot(n012, v03);
      const glm::vec3 n = (spin > 0.f ? -1.f : 1.f) * glm::normalize(n012);
      *posIt++ = m_ptsTri_skin[0] = pt0;
      *posIt++ = m_ptsTri_skin[1] = spin > 0.f ? pt2 : pt1;
      *posIt++ = m_ptsTri_skin[2] = spin > 0.f ? pt1 : pt2;
      *normalIt++ = n; *normalIt++ = n; *normalIt++ = n;
    }
    // tri-013
    {
      const glm::vec3 n013 = glm::cross(v01, v03);
      const float     spin = glm::dot(n013, v02);
      const glm::vec3 n = (spin > 0.f ? -1.f : 1.f) * glm::normalize(n013);
      *posIt++ = m_ptsTri_skin[3] = pt0;
      *posIt++ = m_ptsTri_skin[4] = spin > 0.f ? pt3 : pt1;
      *posIt++ = m_ptsTri_skin[5] = spin > 0.f ? pt1 : pt3;
      *normalIt++ = n; *normalIt++ = n; *normalIt++ = n;
    }
    // tri-023
    {
      const glm::vec3 n023 = glm::cross(v02, v03);
      const float     spin = glm::dot(n023, v01);
      const glm::vec3 n = (spin > 0.f ? -1.f : 1.f) * glm::normalize(n023);
      *posIt++ = m_ptsTri_skin[6] = pt0;
      *posIt++ = m_ptsTri_skin[7] = spin > 0.f ? pt3 : pt2;
      *posIt++ = m_ptsTri_skin[8] = spin > 0.f ? pt2 : pt3;
      *normalIt++ = n; *normalIt++ = n; *normalIt++ = n;
    }
    // tri-123
    const glm::vec3 v12 = pt2 - pt1;
    const glm::vec3 v13 = pt3 - pt1;
    {
      const glm::vec3 n123 = glm::cross(v12, v13);
      const float     spin = - glm::dot(n123, v01);
      const glm::vec3 n = (spin > 0.f ? -1.f : 1.f) * glm::normalize(n123);
      *posIt++ = m_ptsTri_skin[ 9] = pt1;
      *posIt++ = m_ptsTri_skin[10] = spin > 0.f ? pt3 : pt2;
      *posIt++ = m_ptsTri_skin[11] = spin > 0.f ? pt2 : pt3;
      *normalIt++ = n; *normalIt++ = n; *normalIt++ = n;
    }

    m_pt0 = pt0;
    m_pt1 = pt1;
    m_pt2 = pt2;
    m_pt3 = pt3;

    m_ptsTri_skin_transformed.resize(m_ptsTri_skin.size());
}

  virtual eObjectType type() const override { return TETRA; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  glm::vec3 get_pt0() const { return m_transform * glm::vec4(m_pt0, 1.f); }
  glm::vec3 get_pt1() const { return m_transform * glm::vec4(m_pt1, 1.f); }
  glm::vec3 get_pt2() const { return m_transform * glm::vec4(m_pt2, 1.f); }
  glm::vec3 get_pt3() const { return m_transform * glm::vec4(m_pt3, 1.f); }
  const std::vector<glm::vec3> &get_skin_pts() const;

protected:
  glm::vec3 m_pt0, m_pt1, m_pt2, m_pt3;
  std::vector<glm::vec3> m_ptsTri_skin;
  mutable std::vector<glm::vec3> m_ptsTri_skin_transformed;
};

// ----------------------------------------------------------------------------

class sceneObjectBox : public sceneObjectBase
{
public:
  sceneObjectBox(tre::modelIndexed *model, glm::vec3 boxHalfExtend = glm::vec3(0.f)): sceneObjectBase(model)
  {
    glm::mat4 tr(1.f);
    tr[0][0] = boxHalfExtend.x;
    tr[1][1] = boxHalfExtend.y;
    tr[2][2] = boxHalfExtend.z;

    m_part = model->createPartFromPrimitive_box(tr, 2.f);

    m_box = tre::s_boundbox(boxHalfExtend.x, boxHalfExtend.y, boxHalfExtend.z);
}

  virtual eObjectType type() const override { return BOX; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  tre::s_boundbox get_box() const { TRE_ASSERT(m_transform[0][1] == 0.f && m_transform[0][2] == 0.f && m_transform[1][2] == 0.f); return m_box.transform(m_transform); }

protected:
  tre::s_boundbox m_box;
};

// ----------------------------------------------------------------------------

class sceneObjectSkin : public sceneObjectBase
{
public:
  sceneObjectSkin(tre::modelIndexed *model): sceneObjectBase(model)
  {
  }

  void loadFromMesh(const tre::modelIndexed &otherModel, std::size_t otherPart);

  virtual eObjectType type() const override { return SKIN; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  const std::vector<glm::vec3> &get_skin_pts() const;

protected:
  std::vector<glm::vec3> m_ptsTri;
  mutable std::vector<glm::vec3> m_ptsTri_transformed;
};

// ----------------------------------------------------------------------------

class sceneObjectSphere : public sceneObjectBase
{
public:
  sceneObjectSphere(tre::modelIndexed *model, float radius = 0.1f) : sceneObjectBase(model)
  {
    m_part = model->createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), radius, 20, 20);
    m_radius = radius;
  }

  virtual eObjectType type() const override { return SPHERE; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  glm::vec3 get_center() const { return m_transform[3]; }
  float     get_radius() const { TRE_ASSERT(fabsf(glm::length(m_transform[0]) - glm::length(m_transform[1])) < 1.e-6f); return glm::length(m_transform[0]) * m_radius;}

protected:
  float m_radius;
};

// ----------------------------------------------------------------------------

void sceneObjectBase::updateForDraw(const bool isHovered)
{
  if (isHovered)
    m_model->colorizePart(m_part, white);
  else
    m_model->colorizePart(m_part, grey);
}

void sceneObjectBase::draw(const tre::shader &usedShader, const glm::mat4 &mProj, const glm::mat4 &mView) const
{
  usedShader.setUniformMatrix(mProj * mView * m_transform, m_transform, mView);
  m_model->drawcall(m_part, 1, false);
}

// --------------------------------------

bool sceneObjectPoint::isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const
{
  switch(other.type())
  {
  case TETRA:
  {
    auto otherT = static_cast<const sceneObjectTetra *>(&other);
    const bool res = tre::s_contact3D::point_treta(cnt, get_point(), otherT->get_pt0(), otherT->get_pt1(), otherT->get_pt2(), otherT->get_pt3());
    TRE_ASSERT(res == tre::s_contact3D::point_treta(get_point(), otherT->get_pt0(), otherT->get_pt1(), otherT->get_pt2(), otherT->get_pt3()));
    return res;
  }
  case BOX:
  {
    auto otherB = static_cast<const sceneObjectBox *>(&other);
    return tre::s_contact3D::point_box(cnt, get_point(), otherB->get_box());
  }
  case SPHERE:
  {
    auto otherC = static_cast<const sceneObjectSphere *>(&other);
    return tre::s_contact3D::point_sphere(cnt, get_point(), otherC->get_center(), otherC->get_radius());
  }
  case SKIN:
  {
    auto otherP = static_cast<const sceneObjectSkin *>(&other);
    return tre::s_contact3D::point_skin(cnt, get_point(), otherP->get_skin_pts());
  }
  default:
    return false;
  }
}

bool sceneObjectPoint::rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const
{
  return tre::s_contact3D::raytrace_sphere(cnt, origin, direction, get_point(), 0.07f);
}

// --------------------------------------

bool sceneObjectTetra::isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const
{
  switch(other.type())
  {
  /* case TETRA:
   * case SKIN:
  {
    auto otherP = static_cast<const sceneObjectSkin *>(&other);
    return tre::s_contact3D::skin_skin(...);
  }*/
  default:
    return false;
  }
}

bool sceneObjectTetra::rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const
{
  return tre::s_contact3D::raytrace_skin(cnt, origin, direction, get_skin_pts());
}

const std::vector<glm::vec3> &sceneObjectTetra::get_skin_pts() const
{
  TRE_ASSERT(m_ptsTri_skin.size() == m_ptsTri_skin_transformed.size());
  for (std::size_t iP = 0, iPstop = m_ptsTri_skin.size(); iP < iPstop; ++iP)
    m_ptsTri_skin_transformed[iP] = m_transform * glm::vec4(m_ptsTri_skin[iP], 1.f);
  return m_ptsTri_skin_transformed;
}

// --------------------------------------

bool sceneObjectBox::isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const
{
  switch(other.type())
  {
  case BOX:
  {
    auto otherB = static_cast<const sceneObjectBox *>(&other);
    cnt.penet = 0.f;
    cnt.normal = glm::vec3(1.f, 0.f, 0.f);
    cnt.pt = (get_box() + otherB->get_box()).center(); // fallback, no contact info ...
    return tre::s_contact3D::box_box(get_box(), otherB->get_box());
  }
  case SPHERE:
  {
    auto otherC = static_cast<const sceneObjectSphere *>(&other);
    return tre::s_contact3D::box_sphere(cnt, get_box(), otherC->get_center(), otherC->get_radius());
  }
  /*case SKIN:
  {
    auto otherP = static_cast<const sceneObjectSkin *>(&other);
    std::vector<glm::vec3> skinPts;
    otherP->get_skin_pts(skinPts);
    return tre::s_contact3D::box_skin(cnt, get_box(), skinPts);
  }*/
  default:
    return false;
  }
}

bool sceneObjectBox::rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const
{
  return tre::s_contact3D::raytrace_box(cnt, origin, direction, get_box());
}

// --------------------------------------

void sceneObjectSkin::loadFromMesh(const tre::modelIndexed &otherModel, std::size_t otherPart)
{
  const tre::s_modelDataLayout &otherLayout = otherModel.layout();
  const tre::s_partInfo        &otherInfo = otherModel.partInfo(otherPart);

  // compute the skin

  tre::modelTools::computeSkin3D(otherLayout, otherInfo, m_ptsTri);

  m_ptsTri_transformed.resize(m_ptsTri.size());

  // re-create the mesh form the skin

  m_part = m_model->createRawPart(m_ptsTri.size());

  const tre::s_modelDataLayout &layout = m_model->layout();
  const tre::s_partInfo        &info = m_model->partInfo(m_part);

  auto posIt    = layout.m_positions.begin<glm::vec3>(layout.m_index[info.m_offset]);
  auto normalIt = layout.m_normals.begin<glm::vec3>(layout.m_index[info.m_offset]);

  for (std::size_t iV = 0; iV < info.m_size; iV += 3)
  {
    const glm::vec3 &pt0 = m_ptsTri[iV + 0];
    const glm::vec3 &pt1 = m_ptsTri[iV + 1];
    const glm::vec3 &pt2 = m_ptsTri[iV + 2];
    *posIt++ = pt0;
    *posIt++ = pt1;
    *posIt++ = pt2;
    const glm::vec3 n = glm::normalize(glm::cross(pt1 - pt0, pt2 - pt0));
    TRE_ASSERT(!std::isnan(n.x));
    *normalIt++ = n;
    *normalIt++ = n;
    *normalIt++ = n;
  }
}

bool sceneObjectSkin::isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const
{
  switch(other.type())
  {

  /* case TETRA:
   * case SKIN:
  {
    auto otherP = static_cast<const sceneObjectSkin *>(&other);
    return tre::s_contact3D::skin_skin(cnt, ...);
  }*/
  default:
    return false;
  }
}

bool sceneObjectSkin::rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const
{
  return tre::s_contact3D::raytrace_skin(cnt, origin, direction, get_skin_pts());
}

const std::vector<glm::vec3> &sceneObjectSkin::get_skin_pts() const
{
  TRE_ASSERT(m_ptsTri.size() == m_ptsTri_transformed.size());
  for (std::size_t iP = 0, iPstop = m_ptsTri.size(); iP < iPstop; ++iP)
    m_ptsTri_transformed[iP] = m_transform * glm::vec4(m_ptsTri[iP], 1.f);
  return m_ptsTri_transformed;
}

// ------------------------------

bool sceneObjectSphere::isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const
{
  switch(other.type())
  {
  case SPHERE:
  {
    auto otherC = static_cast<const sceneObjectSphere *>(&other);
    return tre::s_contact3D::sphere_sphere(cnt, get_center(), get_radius(), otherC->get_center(), otherC->get_radius());
  }
  case TETRA:
  {
    auto otherT = static_cast<const sceneObjectTetra *>(&other);
    return tre::s_contact3D::sphere_skin(cnt, get_center(), get_radius(), otherT->get_skin_pts());
  }
  case SKIN:
  {
    auto otherP = static_cast<const sceneObjectSkin *>(&other);
    return tre::s_contact3D::sphere_skin(cnt, get_center(), get_radius(), otherP->get_skin_pts());
  }
  default:
    return false;
  }
}

bool sceneObjectSphere::rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const
{
  return tre::s_contact3D::raytrace_sphere(cnt, origin, direction, get_center(), get_radius());
}

// ============================================================================

tre::windowContext myWindow;
tre::windowContext::s_controls myControls;
tre::windowContext::s_view3D myView3D(&myWindow);

tre::shader shaderSolid;
tre::shader shaderLigthed;
tre::shader::s_UBOdata_sunLight sunLight;

tre::modelSemiDynamic3D       meshDraw(tre::modelSemiDynamic3D::VB_POSITION | tre::modelSemiDynamic3D::VB_NORMAL, tre::modelSemiDynamic3D::VB_COLOR);
std::vector<sceneObjectBase*> sceneObjects;

sceneObjectPoint objectPoint(&meshDraw);
sceneObjectBox objectBox(&meshDraw, glm::vec3(0.3f, 0.7f, 1.f));
sceneObjectBox objectBox2(&meshDraw, glm::vec3(0.5f, 0.2f, 1.f));
sceneObjectTetra objectTetra(&meshDraw, glm::vec3(0.2f, 0.1f, 0.7f), glm::vec3(0.7f, 0.1f, -0.2f), glm::vec3(-0.8f, 0.f, 0.1f), glm::vec3(0.f, 0.9f, 0.f));
sceneObjectSphere objectSphere(&meshDraw, 0.5f);
sceneObjectSphere objectSphere2(&meshDraw, 0.3f);
sceneObjectSkin objectPolyFromMesh(&meshDraw);

tre::modelSemiDynamic3D meshDebug(0, tre::modelSemiDynamic3D::VB_POSITION | tre::modelStaticIndexed3D::VB_COLOR);
std::size_t meshDebug_partRayCast;
std::size_t meshDebug_partLine;

tre::gizmo gizmo;

// ---------------------------------------------------------

int app_init(int argc, char **argv)
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Contact 3D", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // Arguments

  std::string addmodel3D_path = TESTIMPORTPATH "resources/objects.obj";
  std::string addmodel3D_pname = "CubeSmoothed";

  if (argc > 1) addmodel3D_path = argv[1];
  if (argc > 2) addmodel3D_pname = argv[2];

  // set pipeline state
  glEnable(GL_BLEND);
  glClearColor(0.f,0.f,0.f,0.f);

  // Shaders

  shaderSolid.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR);
  shaderLigthed.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR | tre::shader::PRGM_LIGHT_SUN);

  sunLight.direction    = glm::vec3(0.f, -1.f, 0.f);
  sunLight.color        = glm::vec3(0.7f, 0.7f, 0.7f);
  sunLight.colorAmbiant = glm::vec3(0.2f, 0.4f, 0.3f);

  tre::shader::updateUBO_sunLight(sunLight);

  // Scene

  objectPoint.set_position(glm::vec3(2.f, 2.f, 0.f));
  sceneObjects.push_back(&objectPoint);

  objectBox.set_position(glm::vec3(2.5f, 0.f, 0.f));
  sceneObjects.push_back(&objectBox);

  objectBox2.set_position(glm::vec3(4.f, 0.f, 0.f));
  sceneObjects.push_back(&objectBox2);

  objectTetra.set_position(glm::vec3(0.f, 2.f, 0.f));
  sceneObjects.push_back(&objectTetra);

  objectSphere.set_position(glm::vec3(-2.5f, 0.f, 0.f));
  sceneObjects.push_back(&objectSphere);

  objectSphere2.set_position(glm::vec3(-4.f, 0.f, 0.f));
  sceneObjects.push_back(&objectSphere2);

  // Import mesh (optionnal)
  {
    tre::modelStaticIndexed3D importedMesh(tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
    bool importedMeshValid = tre::modelImporter::addFromWavefront(importedMesh, addmodel3D_path);
    if (importedMeshValid) importedMeshValid = importedMesh.reorganizeParts({ addmodel3D_pname });
    if (importedMeshValid)
    {
      objectPolyFromMesh.loadFromMesh(importedMesh, 0);
      sceneObjects.push_back(&objectPolyFromMesh);
    }
  }

  meshDraw.loadIntoGPU();

  // indicator mesh

  meshDebug_partRayCast = meshDebug.createRawPart(6);
  meshDebug_partLine = meshDebug.createRawPart(0);

  meshDebug.loadIntoGPU();

  // Gizmo

  gizmo.GizmoSelfScale() = 0.06f;
  gizmo.loadIntoGPU();
  gizmo.loadShader();

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

  // Misc.

  myView3D.m_matView[3] = glm::vec4(0.f, 0.f, -5.f, 1.f);
  myView3D.setScreenBoundsMotion(true);
  myView3D.setKeyBinding(true);
  myView3D.m_keySensitivity = glm::vec3(0.5f);
  myView3D.m_mouseSensitivity = glm::vec4(1.f, 1.f, 1.f, 3.f);

  return 0;
}

// ------------------------------------------------

sceneObjectBase *objectHovered = nullptr;

void app_update()
{

  {
    myWindow.SDLEvent_newFrame();
    myControls.newFrame();

    // Event
    SDL_Event rawEvent;
    while(SDL_PollEvent(&rawEvent) == 1)
    {
      myWindow.SDLEvent_onWindow(rawEvent);
      myControls.treatSDLEvent(rawEvent);

      if (!myView3D.m_mouseBound)
        gizmo.acceptEvent(rawEvent);

      if (rawEvent.type == SDL_KEYUP && rawEvent.key.keysym.sym == SDLK_HOME) // view-reset
      {
         myView3D.m_matView = glm::mat4(1.f);
         myView3D.m_matView[3] = glm::vec4(0.f, 0.f, -5.f, 1.f);
      }
    }

    // camera motion

    if (myWindow.m_hasFocus)
      myView3D.treatControlEvent(myControls, 0.17f /*about 60 fps*/);

    if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED)
      myView3D.setMouseBinding(!myView3D.m_mouseBound);

    if (myWindow.m_viewportResized)
    {
      // ...
    }

    gizmo.updateCameraInfo(myWindow.m_matProjection3D, myView3D.m_matView, myWindow.m_resolutioncurrent);

    // logic

    bool hasRayCast = false;

    if (!gizmo.isGrabbed() && !myView3D.m_mouseBound)
    {
      // ray-cast the mouse "direction" against the scene's objects
      const glm::vec4 mouseClipPos = glm::vec4( (-1.f + 2.f * float(myControls.m_mouse.x)  * myWindow.m_resolutioncurrentInv.x),
                                                ( 1.f - 2.f * float(myControls.m_mouse.y)  * myWindow.m_resolutioncurrentInv.y),
                                                1.f, 1.f);
      const glm::vec3 rayOrigin = glm::inverse(myView3D.m_matView) * glm::vec4(-glm::vec3(myView3D.m_matView[3]), 0.f);
      const glm::vec4 mousePtFar = glm::inverse(myWindow.m_matProjection3D * myView3D.m_matView) * mouseClipPos;
      const glm::vec3 rayDireciton = glm::normalize(glm::vec3(mousePtFar) / mousePtFar.w - rayOrigin);

      tre::s_contact3D hitInfo;
      hitInfo.penet = std::numeric_limits<float>::infinity();

      for (sceneObjectBase *object : sceneObjects)
      {
        tre::s_contact3D hitInfoLocal;
        if (object->rayTrace(rayOrigin, rayDireciton, hitInfoLocal))
        {
          if (hitInfoLocal.penet >= 0.f && hitInfoLocal.penet < hitInfo.penet)
          {
            hitInfo = hitInfoLocal;
            objectHovered = object;
          }
        }
      }

      if (objectHovered != nullptr)
      {
        {
          hasRayCast = true;

          // construct local TBN
          const glm::vec3 n = hitInfo.normal;
          const glm::vec3 t = glm::normalize(glm::cross(n, glm::vec3(0.f, 1.f, 0.f)) + n.y * n.y * glm::vec3(0.001f, 0.f, 0.f));
          const glm::vec3 b = glm::normalize(glm::cross(n, t));

          const glm::vec3 rPt = hitInfo.pt;
          const float     rSize = 0.03f * hitInfo.penet;
          //
          auto &indices = meshDebug.layout().m_index;
          auto &vPos = meshDebug.layout().m_positions;
          auto &vCol = meshDebug.layout().m_colors;
          std::size_t offset = meshDebug.partInfo(meshDebug_partRayCast).m_offset;
          TRE_ASSERT(meshDebug.partInfo(meshDebug_partRayCast).m_size == 6);
          vPos.get<glm::vec3>(indices[offset + 0]) = rPt;
          vPos.get<glm::vec3>(indices[offset + 1]) = rPt + rSize * 2.f * n;
          vPos.get<glm::vec3>(indices[offset + 2]) = rPt - rSize * t;
          vPos.get<glm::vec3>(indices[offset + 3]) = rPt + rSize * t;
          vPos.get<glm::vec3>(indices[offset + 4]) = rPt - rSize * b;
          vPos.get<glm::vec3>(indices[offset + 5]) = rPt + rSize * b;
          vCol.get<glm::vec4>(indices[offset + 0]) = glm::vec4(0.f, 1.f, 1.f, 1.f);
          vCol.get<glm::vec4>(indices[offset + 1]) = glm::vec4(0.f, 1.f, 1.f, 1.f);
          vCol.get<glm::vec4>(indices[offset + 2]) = glm::vec4(0.f, 0.f, 1.f, 1.f);
          vCol.get<glm::vec4>(indices[offset + 3]) = glm::vec4(0.f, 0.f, 1.f, 1.f);
          vCol.get<glm::vec4>(indices[offset + 4]) = glm::vec4(0.f, 0.f, 1.f, 1.f);
          vCol.get<glm::vec4>(indices[offset + 5]) = glm::vec4(0.f, 0.f, 1.f, 1.f);
        }

        gizmo.setTransfromToUpdate(objectHovered->transformPtr());
        if (gizmo.Mode() == tre::gizmo::GMODE_NONE)
          gizmo.SetMode(tre::gizmo::GMODE_TRANSLATING);
      }
      else
      {
        gizmo.SetMode(tre::gizmo::GMODE_NONE);
      }
    }

    // Compute contact

    std::vector<tre::s_contact3D> cntsList;
    cntsList.reserve(sceneObjects.size() * (sceneObjects.size() - 1));

    for (const sceneObjectBase *objectA : sceneObjects)
    {
      for (const sceneObjectBase *objectB : sceneObjects)
      {
        if (objectA == objectB)
          continue;
        tre::s_contact3D cnt;
        if (objectA->isContactWith(*objectB, cnt))
          cntsList.push_back(cnt);
      }
    }

    // Prepare draw

    {
      for (sceneObjectBase *oBase : sceneObjects)
        oBase->updateForDraw(objectHovered == oBase);

      const std::size_t meshDebug_partLineSize = cntsList.size() * 6;

      meshDebug.resizeRawPart(meshDebug_partLine, meshDebug_partLineSize);

      // fill
      auto &indices = meshDebug.layout().m_index;
      auto &vPos = meshDebug.layout().m_positions;
      auto &vCol = meshDebug.layout().m_colors;
      std::size_t offset = meshDebug.partInfo(meshDebug_partLine).m_offset;
      for (const tre::s_contact3D &cnt : cntsList)
      {
        // construct local TBN
        const glm::vec3 n = cnt.normal;
        const glm::vec3 t = glm::normalize(glm::cross(n, glm::vec3(0.f, 1.f, 0.f)) + n.y * n.y * glm::vec3(0.001f, 0.f, 0.f));
        const glm::vec3 b = glm::normalize(glm::cross(n, t));

        vPos.get<glm::vec3>(indices[offset + 0]) = cnt.pt - 0.3f * t;
        vPos.get<glm::vec3>(indices[offset + 1]) = cnt.pt + 0.3f * t;
        vPos.get<glm::vec3>(indices[offset + 2]) = cnt.pt - 0.3f * b;
        vPos.get<glm::vec3>(indices[offset + 3]) = cnt.pt + 0.3f * b;
        vCol.get<glm::vec4>(indices[offset + 0]) = glm::vec4(0.f, 1.f, 0.f, 1.f);
        vCol.get<glm::vec4>(indices[offset + 1]) = glm::vec4(0.f, 1.f, 0.f, 1.f);
        vCol.get<glm::vec4>(indices[offset + 2]) = glm::vec4(0.f, 1.f, 0.f, 1.f);
        vCol.get<glm::vec4>(indices[offset + 3]) = glm::vec4(0.f, 1.f, 0.f, 1.f);

        vPos.get<glm::vec3>(indices[offset + 4]) = cnt.pt;
        vPos.get<glm::vec3>(indices[offset + 5]) = cnt.pt + cnt.penet * cnt.normal;
        vCol.get<glm::vec4>(indices[offset + 4]) = glm::vec4(0.f, 1.f, 0.f, 1.f);
        vCol.get<glm::vec4>(indices[offset + 5]) = glm::vec4(1.f, 1.f, 0.f, 1.f);

        offset += 6;
      }
      TRE_ASSERT(offset == meshDebug.partInfo(meshDebug_partLine).m_offset + meshDebug_partLineSize);
    }

    meshDraw.updateIntoGPU(); // not needed every frame, but dont care.
    meshDebug.updateIntoGPU(); // not needed every frame, but dont care.

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

      unittest::cvrgPointTetra.printInfo(txt, 128, "UT Point-Tetra", unittest::volumeTetra);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointTetra_info.printInfo(txt, 128, "UT Point-Tetra (info)", unittest::volumeTetra);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointBox.printInfo(txt, 128, "UT Point-Box", unittest::volumeBox);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointBox_info.printInfo(txt, 128, "UT Point-Box (info)", unittest::volumeBox);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointSphere.printInfo(txt, 128, "UT Point-Sphere", unittest::volumeSphere);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      unittest::cvrgPointSphere_info.printInfo(txt, 128, "UT Point-Sphere (info)", unittest::volumeSphere);
      tre::textgenerator::generate(tInfo, &ui_mesh, ui_mesh.createPart(tre::textgenerator::geometry_VertexCount(tInfo.m_text)), 0, &tOut);
      pos.y -= tOut.m_maxboxsize.y + 0.01f;
      tInfo.m_zone = glm::vec4(pos, pos);

      ui_mesh.updateIntoGPU();
    }
#endif

    // Begin of draw --------

    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw objects

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glUseProgram(shaderLigthed.m_drawProgram);

    meshDraw.drawcall(0,0,true); // HACK, just bind the model's VAO

    for (sceneObjectBase *oBase : sceneObjects)
      oBase->draw(shaderLigthed, myWindow.m_matProjection3D, myView3D.m_matView);

    // Draw Gizmo

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    if (!myView3D.m_mouseBound)
    {
      gizmo.updateIntoGPU();
      gizmo.draw();
    }

    // Draw indicators

    glUseProgram(shaderSolid.m_drawProgram);

    shaderSolid.setUniformMatrix(myWindow.m_matProjection3D * myView3D.m_matView);

    meshDebug.drawcall(0,0,true); // HACK, just bind the model's VAO

    if (hasRayCast)
      meshDebug.drawcall(meshDebug_partRayCast, 1, false, GL_LINES);

    meshDebug.drawcall(meshDebug_partLine, 1, false, GL_LINES);

    // Draw Unit-Test

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

    // End of draw --------

    SDL_GL_SwapWindow(myWindow.m_window);
  }
}

// --------------------------------------------------

void app_quit()
{
  meshDraw.clearGPU();
  meshDebug.clearGPU();

  shaderSolid.clearShader();;
  shaderLigthed.clearShader();
  tre::shader::clearUBO();

  gizmo.clearGPU();
  gizmo.clearShader();

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
