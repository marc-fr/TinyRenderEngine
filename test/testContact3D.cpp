#include "testContact3D.h"

#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_shader.h"
#include "tre_contact_3D.h"
#include "tre_gizmo.h"
#include "tre_baker.h"
#include "tre_windowContext.h"

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// Constants ==================================================================

#define CSIZE    0.04f
#define CSIZECNT 0.01f

static const glm::vec4 white     = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
static const glm::vec4 grey      = glm::vec4(0.3f, 0.3f, 0.3f, 1.f);
static const glm::vec4 yellow    = glm::vec4(1.f, 1.f, 0.f, 1.f);
static const glm::vec4 green     = glm::vec4(0.f, 1.f, 0.f, 1.f);
static const glm::vec4 darkgreen = glm::vec4(0.1f, 0.4f, 0.1f, 1.f);
static const glm::vec4 magenta   = glm::vec4(1.f, 0.f, 1.f, 1.f);

// Scene-Object implementation ================================================

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

sceneObjectPoint::sceneObjectPoint(tre::modelIndexed *model) : sceneObjectBase(model)
{
  m_part = model->createPartFromPrimitive_box(glm::mat4(1.f), 0.05f);
}

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

sceneObjectTetra::sceneObjectTetra(tre::modelIndexed *model, const glm::vec3 &pt0, const glm::vec3 &pt1, const glm::vec3 &pt2, const glm::vec3 &pt3) : sceneObjectBase(model)
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

sceneObjectBox::sceneObjectBox(tre::modelIndexed *model, glm::vec3 boxHalfExtend) : sceneObjectBase(model)
{
  glm::mat4 tr(1.f);
  tr[0][0] = boxHalfExtend.x;
  tr[1][1] = boxHalfExtend.y;
  tr[2][2] = boxHalfExtend.z;

  m_part = model->createPartFromPrimitive_box(tr, 2.f);

  m_box = tre::s_boundbox(boxHalfExtend.x, boxHalfExtend.y, boxHalfExtend.z);
}

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

sceneObjectSkin::sceneObjectSkin(tre::modelIndexed *model) : sceneObjectBase(model)
{
}

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

sceneObjectSphere::sceneObjectSphere(tre::modelIndexed *model, float radius) : sceneObjectBase(model)
{
  m_part = model->createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), radius, 20, 20);
  m_radius = radius;
}

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

int main(int argc, char **argv)
{
  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;
  tre::windowContext::s_view3D myView3D(&myWindow);

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

  tre::shader shaderSolid;
  shaderSolid.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR);

  tre::shader shaderLigthed;
  shaderLigthed.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR | tre::shader::PRGM_LIGHT_SUN);

  tre::shader::s_UBOdata_sunLight sunLight;
  sunLight.direction    = glm::vec3(0.f, -1.f, 0.f);
  sunLight.color        = glm::vec3(0.7f, 0.7f, 0.7f);
  sunLight.colorAmbiant = glm::vec3(0.2f, 0.4f, 0.3f);

  tre::shader::updateUBO_sunLight(sunLight);

  // Scene objects

  tre::modelSemiDynamic3D       meshDraw(tre::modelSemiDynamic3D::VB_POSITION | tre::modelSemiDynamic3D::VB_NORMAL, tre::modelSemiDynamic3D::VB_COLOR);
  std::vector<sceneObjectBase*> sceneObjects;

  sceneObjectPoint objectPoint(&meshDraw);
  objectPoint.set_position(glm::vec3(2.f, 2.f, 0.f));
  sceneObjects.push_back(&objectPoint);

  sceneObjectBox objectBox(&meshDraw, glm::vec3(0.3f, 0.7f, 1.f));
  objectBox.set_position(glm::vec3(2.5f, 0.f, 0.f));
  sceneObjects.push_back(&objectBox);

  sceneObjectBox objectBox2(&meshDraw, glm::vec3(0.5f, 0.2f, 1.f));
  objectBox2.set_position(glm::vec3(4.f, 0.f, 0.f));
  sceneObjects.push_back(&objectBox2);

  sceneObjectTetra objectTetra(&meshDraw, glm::vec3(0.2f, 0.1f, 0.7f), glm::vec3(0.7f, 0.1f, -0.2f), glm::vec3(-0.8f, 0.f, 0.1f), glm::vec3(0.f, 0.9f, 0.f));
  objectTetra.set_position(glm::vec3(0.f, 2.f, 0.f));
  sceneObjects.push_back(&objectTetra);

  sceneObjectSphere objectSphere(&meshDraw, 0.5f);
  objectSphere.set_position(glm::vec3(-2.5f, 0.f, 0.f));
  sceneObjects.push_back(&objectSphere);

  sceneObjectSphere objectSphere2(&meshDraw, 0.3f);
  objectSphere2.set_position(glm::vec3(-4.f, 0.f, 0.f));
  sceneObjects.push_back(&objectSphere2);

  // Import mesh (optionnal)
  sceneObjectSkin objectPolyFromMesh(&meshDraw);
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

  tre::modelSemiDynamic3D meshDebug(0, tre::modelSemiDynamic3D::VB_POSITION | tre::modelStaticIndexed3D::VB_COLOR);

  const std::size_t meshDebug_partRayCast = meshDebug.createRawPart(6);
  const std::size_t meshDebug_partLine = meshDebug.createRawPart(0);

  meshDebug.loadIntoGPU();

  // Gizmo

  tre::gizmo gizmo;

  gizmo.GizmoSelfScale() = 0.06f;
  gizmo.loadIntoGPU();
  gizmo.loadShader();

  // Main loop
  SDL_Event rawEvent;

  myView3D.m_matView[3] = glm::vec4(0.f, 0.f, -5.f, 1.f);
  myView3D.setScreenBoundsMotion(true);
  myView3D.setKeyBinding(true);
  myView3D.m_keySensitivity = glm::vec3(0.5f);
  myView3D.m_mouseSensitivity = glm::vec4(1.f, 1.f, 1.f, 3.f);

  sceneObjectBase *objectHovered = nullptr;

  TRE_LOG("Start main loop ...");

  while(!myWindow.m_quit && !myControls.m_quit)
  {
    myWindow.SDLEvent_newFrame();
    myControls.newFrame();

    // Event
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

    // End of draw --------

    SDL_GL_SwapWindow(myWindow.m_window);

    SDL_Delay(10); // about 60 fps, let the v-sync do the job.
  }

  meshDraw.clearGPU();
  meshDebug.clearGPU();

  shaderSolid.clearShader();;
  shaderLigthed.clearShader();
  tre::shader::clearUBO();

  gizmo.clearGPU();
  gizmo.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("SDL finalized with success");

  return 0;
}
