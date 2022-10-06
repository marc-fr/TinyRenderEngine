#include "testContact2D.h"

#include <iostream> // std::cout std::endl

#include "tre_model.h"
#include "tre_model_tools.h"
#include "tre_shader.h"
#include "tre_contact_2D.h"
#include "tre_windowContext.h"

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// Scene implementation =======================================================

sceneObjectBase::sceneObjectBase(tre::modelRaw2D *model, unsigned partSize)
{
  m_model = model;
  m_part = model->createPart(partSize);
}

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

int main(int argc, char **argv)
{
  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;
  tre::windowContext::s_view2D myView2D(&myWindow);

  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "test Contact 2D", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

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
  tre::shader shaderSolid;
  shaderSolid.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR);

  // Scene
  tre::modelRaw2D               meshDraw;
  std::vector<sceneObjectBase*> sceneObjects;

  sceneObjectPoint objectPoint(&meshDraw, glm::vec2(1.f, 0.f));
  sceneObjects.push_back(&objectPoint);

  sceneObjectBox objectBox(&meshDraw, glm::vec4(0.4f, -0.7f, 1.0f, -0.5f));
  sceneObjects.push_back(&objectBox);

  sceneObjectLine objectLine1(&meshDraw, glm::vec2(-1.2f, -0.5f), glm::vec2(-1.2f, 0.f));
  sceneObjects.push_back(&objectLine1);

  sceneObjectLine objectLine2(&meshDraw, glm::vec2(-1.0f, -0.5f), glm::vec2(-1.0f, 0.f));
  sceneObjects.push_back(&objectLine2);

  sceneObjectYDown objectLineY(&meshDraw, -0.9f);
  sceneObjects.push_back(&objectLineY);

  sceneObjectCircle objectCircle(&meshDraw, glm::vec2(0.8f, 0.7f), 0.2f);
  sceneObjects.push_back(&objectCircle);

  std::vector<glm::vec2> tmpPoly;
  tmpPoly.resize(4);
  tmpPoly[0] = {-0.8f, 0.6f};
  tmpPoly[1] = {-0.5f, 0.6f};
  tmpPoly[2] = {-0.6f, 0.8f};
  tmpPoly[3] = {-0.7f, 0.8f};

  sceneObjectPoly objectPoly1(&meshDraw, tmpPoly);
  sceneObjects.push_back(&objectPoly1);

  sceneObjectRay objectRay(&meshDraw, glm::vec2(-1.1f, 0.5f), glm::vec2(0.f, 1.f));
  sceneObjects.push_back(&objectRay);

  // Import mesh (optionnal)
  std::vector<glm::vec2> tmpMeshEnvelop;
  tre::modelStaticIndexed3D importedMesh;
  bool importedMeshValid = importedMesh.loadfromWavefront(addmodel3D_path);
  if (importedMeshValid) importedMeshValid = importedMesh.reorganizeParts({ addmodel3D_pname });
  if (importedMeshValid)
  {
    tre::modelTools::computeConvexeEnvelop2D_XY(importedMesh.layout(), importedMesh.partInfo(0), glm::mat4(1.f), 1.e-2f, tmpMeshEnvelop);
    const tre::s_boundbox &bbox = importedMesh.partInfo(0).m_bbox;
    const glm::vec2 scale = 1.f / (glm::vec2(bbox.extend()) + 1.e-6f);
    const glm::vec2 add   = scale * glm::vec2(bbox.center());
    for (glm::vec2 &pt : tmpMeshEnvelop)
      pt = scale * pt + add;
  }
  sceneObjectPoly objectPolyFromMesh(&meshDraw, tmpMeshEnvelop);
  if (importedMeshValid)
    sceneObjects.push_back(&objectPolyFromMesh);

  // End scene creation

  const unsigned partCnt = meshDraw.createPart(128);

  meshDraw.loadIntoGPU();

  // Main loop controls
  SDL_Event rawEvent;
  bool hasSelection(false);

  myView2D.setKeyBinding(true);
  myView2D.setScreenBoundsMotion(true);

  std::cout << "Start main loop ..." << std::endl;

  while (!myWindow.m_quit && !myControls.m_quit)
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
    for (sceneObjectBase *oBase : sceneObjects)
      oBase->treatEvent(mouseWordSpace, myControls.m_mouseLEFT & tre::windowContext::s_controls::MASK_BUTTON_PRESSED, hasSelection);

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

    // Draw

    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderSolid.m_drawProgram);

    shaderSolid.setUniformMatrix(matPV);

    meshDraw.drawcallAll(true, GL_LINES);

    // End

    SDL_GL_SwapWindow(myWindow.m_window);

    SDL_Delay(10); // about 60 fps
  }

  meshDraw.clearGPU();

  shaderSolid.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  std::cout << "SDL finalized with success" << std::endl;

  return 0;
}
