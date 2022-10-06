
#include <vector>

#include "tre_utils.h"

namespace tre {
  class  shader;
  class  modelIndexed;
  struct s_contact3D;
}

// ----------------------------------------------------------------------------

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
  sceneObjectPoint(tre::modelIndexed *model);

  virtual eObjectType type() const { return POINT; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  glm::vec3 get_point() const { return glm::vec3(m_transform[3]); }
};

// ----------------------------------------------------------------------------

class sceneObjectTetra : public sceneObjectBase
{
public:
  sceneObjectTetra(tre::modelIndexed *model, const glm::vec3 &pt0, const glm::vec3 &pt1, const glm::vec3 &pt2, const glm::vec3 &pt3);

  virtual eObjectType type() const { return TETRA; }
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
  sceneObjectBox(tre::modelIndexed *model, glm::vec3 boxHalfExtend = glm::vec3(0.f));

  virtual eObjectType type() const { return BOX; }
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
  sceneObjectSkin(tre::modelIndexed *model);

  void loadFromMesh(const tre::modelIndexed &otherModel, std::size_t otherPart);

  virtual eObjectType type() const { return SKIN; }
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
  sceneObjectSphere(tre::modelIndexed *model, float radius = 0.1f);

  virtual eObjectType type() const { return SPHERE; }
  virtual bool isContactWith(const sceneObjectBase &other, tre::s_contact3D &cnt) const override;
  virtual bool rayTrace(const glm::vec3 &origin, const glm::vec3 &direction, tre::s_contact3D &cnt) const override;

  glm::vec3 get_center() const { return m_transform[3]; }
  float     get_radius() const { TRE_ASSERT(fabsf(glm::length(m_transform[0]) - glm::length(m_transform[1])) < 1.e-6f); return glm::length(m_transform[0]) * m_radius;}

protected:
  float m_radius;
};

// ----------------------------------------------------------------------------
