#include "tre_gizmo.h"

#include "tre_model.h"
#include "tre_shader.h"

namespace tre {

//----------------------------------------------------------------------------

static const glm::vec4 AXISX = glm::vec4(1.f, 0.f, 0.f, 0.f);
static const glm::vec4 AXISY = glm::vec4(0.f, 1.f, 0.f, 0.f);
static const glm::vec4 AXISZ = glm::vec4(0.f, 0.f, 1.f, 0.f);
static const glm::vec4 AXISW = glm::vec4(0.f, 0.f, 0.f, 1.f);

static const glm::mat4 IDENTITY = glm::mat4(AXISX, AXISY, AXISZ, AXISW);

static const glm::ivec4 ivec4ZERO = glm::ivec4(0);

#define AXISINDEX_RIGHT    0
#define AXISINDEX_TOP      1
#define AXISINDEX_DEPTH    2

//----------------------------------------------------------------------------

gizmo::gizmo() : m_model(modelSemiDynamic3D::VB_POSITION, modelSemiDynamic3D::VB_COLOR)
{
  m_parentPureRotation = IDENTITY;
  m_parentTransform = IDENTITY;
  m_transform = IDENTITY;
}

//----------------------------------------------------------------------------

gizmo::~gizmo()
{
  TRE_ASSERT(m_shader == nullptr);
}

//----------------------------------------------------------------------------

void gizmo::setParentTransform(const glm::mat4 &transform)
{
  if (!isGrabbed()) // if the user is manipulating the Gizmo, then we ignore concurrent update.
  {
    m_parentTransform = transform;

    m_parentPureRotation[0] = glm::normalize(transform[0]);
    m_parentPureRotation[1] = glm::normalize(transform[1]);
    m_parentPureRotation[2] = glm::normalize(transform[2]);
    m_parentPureRotation[3] = AXISW;

    _transformFromOutput();
  }
}

//----------------------------------------------------------------------------

void gizmo::setTransfromToUpdate(glm::mat4 *transform)
{
  m_transformToUpdate = transform;

  if (!isGrabbed())
  {
    m_transformToUpdateChanged = false;
    _transformFromOutput();
  }
}

//----------------------------------------------------------------------------

void gizmo::updateCameraInfo(const glm::mat4 &mProj, const glm::mat4 &mView, const glm::ivec2 &screenSize)
{
  const glm::mat4 mPV = mProj * mView;

  m_viewportSize = screenSize;

  m_viewTransposed = glm::transpose(mView);
  m_viewTransposed[0].w = 0.f;
  m_viewTransposed[1].w = 0.f;
  m_viewTransposed[2].w = 0.f;
  m_viewTransposed[3] = mView[3];

  // zero is w component
  m_PV = mPV;
  m_cameraPosition = m_viewTransposed * glm::vec4(-glm::vec3(mView[3]), 0.f);
  m_PVinv = glm::inverse(mPV);

  if (!isGrabbed())
    _transformReset();
}

//----------------------------------------------------------------------------

bool gizmo::acceptEvent(const SDL_Event &event)
{
  bool eventAccepted = false;

  const glm::ivec2 mouseCoord(event.button.x, event.button.y);

  switch(event.type)
  {
  case SDL_KEYDOWN:
    if      (event.key.keysym.scancode == SDL_SCANCODE_Q) // scancode: physical location of the keyboard key
    {
      SetMode(GMODE_NONE);
      eventAccepted = true;
    }
    else if (event.key.keysym.scancode == SDL_SCANCODE_W) // scancode: physical location of the keyboard key
    {
      SetMode(GMODE_TRANSLATING);
      eventAccepted = true;
    }
    else if (event.key.keysym.scancode == SDL_SCANCODE_E) // scancode: physical location of the keyboard key
    {
      SetMode(GMODE_ROTATING);
      eventAccepted = true;
    }
    else if (event.key.keysym.scancode == SDL_SCANCODE_R) // scancode: physical location of the keyboard key
    {
      SetMode(GMODE_SCALING);
      eventAccepted = true;
    }
  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT)
    {
      eventAccepted = acceptEvent(mouseCoord, true);
    }
    break;
  case SDL_MOUSEBUTTONUP:
    if (event.button.button == SDL_BUTTON_LEFT)
    {
      eventAccepted = acceptEvent(mouseCoord, false);
    }
    break;
  case SDL_MOUSEMOTION:
    eventAccepted = acceptEvent(mouseCoord, m_clicked);
    break;
  default:
    break;
  }
  return eventAccepted;
}

//----------------------------------------------------------------------------

bool gizmo::acceptEvent(const glm::ivec2 &mousePosition, bool clicked)
{
  if (m_clicked && clicked)
  {
    // mouse-drag
    _applyMouseMouve(mousePosition);
    return true;
  }

  if (!m_clicked && !clicked)
  {
    // check mouse hover
    _checkSelection(m_hoveredAxis, mousePosition);
    return m_hoveredAxis != ivec4ZERO;
  }

  if (clicked)
  {
    // check if something has been selected
    _checkSelection(m_grabbedAxis, mousePosition, &m_mouseWorldPositionPrev);
    m_clicked = m_grabbedAxis != ivec4ZERO;
    m_hoveredAxis = ivec4ZERO;
    if (m_clicked) // set the "prev"-transforms
    {
      m_transformPrev = m_transform;
      if (m_transformToUpdate != nullptr)
      {
        m_transformToUpdatePrev = *m_transformToUpdate;
      }
      else
      {
        m_transformToUpdatePrev = IDENTITY;
        m_transformToUpdatePrev[3] = m_transform[3];
      }
    }
    return m_clicked;
  }
  else
  {
    m_grabbedAxis = ivec4ZERO;
    m_clicked = false;
    _transformReset();
    return false;
  }
}

//----------------------------------------------------------------------------

glm::vec2 gizmo::projectPointOnScreen(const glm::vec3 &point, const glm::mat4 &mvp, const glm::ivec2 &screenSize)
{
  glm::vec4 projected = mvp * glm::vec4(point, 1.f);
  glm::vec2 projectedScreen = projected / projected.w;
  projectedScreen.x =   projectedScreen.x * 0.5f + 0.5f;
  projectedScreen.y = - projectedScreen.y * 0.5f + 0.5f;
  projectedScreen *= screenSize;
  return projectedScreen;
}

//----------------------------------------------------------------------------

glm::vec3 gizmo::unprojectPointToWorld(const glm::vec2 &pixelCoords, const glm::mat4 &inverseMVP, const glm::ivec2 &screenSize)
{
  glm::vec4 unproj(pixelCoords.x / screenSize.x * 2.f - 1.f,
                   pixelCoords.y / screenSize.y * -2.f + 1.f,
                   1.f,
                   1.f);

  unproj = inverseMVP * unproj;
  unproj /= unproj.w;
  return unproj;
}

//----------------------------------------------------------------------------

float gizmo::_getViewScale() const
{
  const glm::vec3 viewBackward = m_viewTransposed[AXISINDEX_DEPTH];
  const glm::vec3 camToPos = isGrabbed() ? glm::vec3(m_transformPrev[3]) - m_cameraPosition :
                                           glm::vec3(m_transform[3]) - m_cameraPosition;
  return fabs(m_SelfScale * glm::dot(viewBackward, camToPos));
}

//----------------------------------------------------------------------------

float gizmo::_getViewScalePre() const
{
  const glm::vec3 viewBackward = m_viewTransposed[AXISINDEX_DEPTH];
  const glm::vec3 camToPos = glm::vec3(m_transformPrev[3]) - m_cameraPosition;
  return fabs(m_SelfScale * glm::dot(viewBackward, camToPos));
}

//----------------------------------------------------------------------------

void gizmo::_transformReset()
{
  if (m_isFrameLocal)
  {
    const float sX = glm::length(m_transform[0]);
    const float invSX = sX < 1.e-12f ? 1.e12f : 1.f / sX;
    m_transform[0] *= invSX;

    const float sY = glm::length(m_transform[1]);
    const float invSY = sY < 1.e-12f ? 1.e12f : 1.f / sY;
    m_transform[1] *= invSY;

    const float sZ = glm::length(m_transform[2]);
    const float invSZ = sZ < 1.e-12f ? 1.e12f : 1.f / sZ;
    m_transform[2] *= invSZ;
  }
  else
  {
    m_transform[0] = AXISX;
    m_transform[1] = AXISY;
    m_transform[2] = AXISZ;
  }

  // also compute the sign to have the Gizmo facing the camera ...

  const glm::vec4 camToPos = m_parentTransform * m_transform[3] - glm::vec4(m_cameraPosition, 1.f);

  if (glm::dot(camToPos, m_transform[0]) > 0.f)
      m_transform[0] = -m_transform[0];

  if (glm::dot(camToPos, m_transform[1]) > 0.f)
      m_transform[1] = -m_transform[1];

  if (glm::dot(camToPos, m_transform[2]) > 0.f)
      m_transform[2] = -m_transform[2];


  // Build View-space transform for rotation handles
  if (m_Type == GMODE_ROTATING)
    _transformComputeRotating();
}

//----------------------------------------------------------------------------

void gizmo::_transformFromOutput()
{
  if (isGrabbed())
  {
    // if grabbed, cancel the move
    m_transform = m_transformPrev;

    // set value of "m_TransformToUpdate"
    if (m_transformToUpdate != nullptr)
    {
      *m_transformToUpdate = m_transformToUpdatePrev;
    }

    m_clicked = false;
  }
  else
  {
    m_transform = m_parentTransform;

    // get value from "m_TransformToUpdate"
    if (m_transformToUpdate != nullptr)
    {
      if (m_isFrameLocal)
      {
        m_transform = m_parentTransform * (*m_transformToUpdate);
      }
      else
      {
        m_transform = IDENTITY;
        m_transform[3] = m_parentTransform * (*m_transformToUpdate)[3];
        }
    }
  }
  _transformReset();
}

//----------------------------------------------------------------------------

static inline glm::vec4 cross4(const glm::vec4 &a, const glm::vec4 &b)
{
  return glm::vec4(glm::cross(glm::vec3(a), glm::vec3(b)), 0.f);
}

void gizmo::_transformComputeRotating()
{
  // TRE_ASSERT(m_transform.isOrthonormal());

  m_transformRotating[0] = m_transformRotating[1] = m_transformRotating[2] = m_transform;

  const glm::vec4 viewDepth = glm::vec4(glm::normalize(m_cameraPosition - glm::vec3(m_transform[3])), 0.f);

  // m_TransformRotating[0] => axisX -> transformX, axisY -> viewDepth
  glm::vec4 new0_Y = viewDepth - glm::dot(viewDepth, m_transformRotating[0][0]) * m_transformRotating[0][0];
  TRE_ASSERT(new0_Y.w == 0.f);
  if (glm::dot(new0_Y, new0_Y) > 1.e-12f)
  {
    m_transformRotating[0][1] = glm::normalize(new0_Y);
    m_transformRotating[0][2] = glm::normalize(cross4(m_transformRotating[0][0], m_transformRotating[0][1]));
  }

  // m_TransformRotating[1] => axisY -> transformY, axisZ -> viewDepth
  glm::vec4 new1_Z = viewDepth - glm::dot(viewDepth, m_transformRotating[1][1]) * m_transformRotating[1][1];
  TRE_ASSERT(new1_Z.w == 0.f);
  if (glm::dot(new1_Z, new1_Z) > 1.e-12f)
  {
      m_transformRotating[1][2] = glm::normalize(new1_Z);
      m_transformRotating[1][0] = glm::normalize(cross4(m_transformRotating[1][1], m_transformRotating[1][2]));
  }

  // m_TransformRotating[2] => axisZ -> transformZ, axisX -> viewDepth
  glm::vec4 new2_X = viewDepth - glm::dot(viewDepth, m_transformRotating[2][2]) * m_transformRotating[2][2];
  TRE_ASSERT(new2_X.w == 0.f);
  if (glm::dot(new2_X, new2_X) > 1.e-12f)
  {
      m_transformRotating[2][0] = glm::normalize(new2_X);
      m_transformRotating[2][1] = glm::normalize(cross4(m_transformRotating[2][2], m_transformRotating[2][0]));
  }

  // m_transformRotating[3] => axisX -> viewDepth
  glm::vec4 new3_Y = cross4(viewDepth, m_transformRotating[3][0]);
  if (glm::dot(new3_Y, new3_Y) > 1.e-12f)
  {
    m_transformRotating[3][0] = glm::normalize(cross4(new3_Y, viewDepth));
    m_transformRotating[3][1] = glm::normalize(new3_Y);
    m_transformRotating[3][2] = viewDepth;
  }
  else
  {
    m_transformRotating[3] = m_viewTransposed;
  }
  m_transformRotating[3][3] = m_transform[3];
}

//----------------------------------------------------------------------------

void gizmo::_applyMouseMouve(const glm::ivec2 &mouseScreenPosition)
{
  const glm::mat4 frame = m_transform;
  const glm::vec3 position = frame[3];

  TRE_ASSERT(m_clicked && m_grabbedAxis != ivec4ZERO);

  glm::vec3 viewDepth;
  if (m_Type == GMODE_ROTATING)
    viewDepth = glm::vec4(glm::normalize(m_cameraPosition - glm::vec3(m_transform[3])), 0.f);
  else
    viewDepth = m_viewTransposed[AXISINDEX_DEPTH];

  // Construct the "mouseWorldPos" depending on projection
  glm::vec3   mouseWorldPos;

  const bool projAxisAll = m_grabbedAxis[0] != 0 && m_grabbedAxis[1] != 0 && m_grabbedAxis[2] != 0;

  {
    const glm::vec3 mouseWorldPosFar = unprojectPointToWorld(mouseScreenPosition, m_PVinv, m_viewportSize);
    const glm::vec3 mouseDirection = glm::normalize(mouseWorldPosFar - m_cameraPosition);

    const glm::vec3 cameraToPos = position - m_cameraPosition;

    const bool projAxisX = m_Type != GMODE_ROTATING ? m_grabbedAxis[0] != 0 : m_grabbedAxis[0] == 0;
    const bool projAxisY = m_Type != GMODE_ROTATING ? m_grabbedAxis[1] != 0 : m_grabbedAxis[1] == 0;
    const bool projAxisZ = m_Type != GMODE_ROTATING ? m_grabbedAxis[2] != 0 : m_grabbedAxis[2] == 0;
    const bool projSphere = m_Type == GMODE_ROTATING && projAxisAll;
    const bool projScreenP = (m_Type != GMODE_ROTATING && projAxisAll) || m_grabbedAxis.w != 0;

    if (projSphere)
    {
      // project on sphere
      const float  radius = _getViewScale() * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight);
      const float  tRayCenter = glm::dot(cameraToPos, mouseDirection);
      const float  tOffsetSquared = tRayCenter * tRayCenter - glm::dot(cameraToPos, cameraToPos) + radius * radius;

      if (tOffsetSquared < 0.f) // the ray does not hit the sphere
      {
        const glm::vec3 PosToShpere = radius * glm::normalize(tRayCenter * mouseDirection - cameraToPos);
        mouseWorldPos = position + PosToShpere;
      }
      else // the ray hits the sphere
      {
        const float  tOffset = sqrtf(tOffsetSquared);
        const float  tRayClosestPoint = tRayCenter - tOffset;
        mouseWorldPos = m_cameraPosition + tRayClosestPoint * mouseDirection;
      }
    }
    else if (projScreenP)
    {
      // project on screen-plane with direction = mouseDirection
      const glm::vec3 planeNormal = viewDepth;
      mouseWorldPos = m_cameraPosition + mouseDirection * (glm::dot(planeNormal, cameraToPos) / glm::dot(planeNormal, mouseDirection));
    }
    else if (projAxisX && projAxisY)
    {
      // project on XY-plane with direction = mouseDirection
      const glm::vec3 planeNormal = frame[2];
      mouseWorldPos = m_cameraPosition + mouseDirection * (glm::dot(planeNormal, cameraToPos) / glm::dot(planeNormal, mouseDirection));
    }
    else if (projAxisX && projAxisZ)
    {
      // project on XZ-plane with direction = mouseDirection
      const glm::vec3 planeNormal = frame[1];
      mouseWorldPos = m_cameraPosition + mouseDirection * (glm::dot(planeNormal, cameraToPos) / glm::dot(planeNormal, mouseDirection));
    }
    else if (projAxisY && projAxisZ)
    {
      // project on YZ-plane with direction = mouseDirection
      const glm::vec3 planeNormal = frame[0];
      mouseWorldPos = m_cameraPosition + mouseDirection * (glm::dot(planeNormal, cameraToPos) / glm::dot(planeNormal, mouseDirection));
    }
    else if (projAxisX || projAxisY || projAxisZ)
    {
      const std::size_t  axisIdx = projAxisZ ? 2 : (projAxisY ? 1 : 0);
      const glm::vec3 axis = frame[axisIdx];
#if 1 // pre-2D-Projection (not perfect but better than nothing)
      glm::vec3   newMouseDirection = mouseDirection;
      const glm::vec2 gizmoScreenPosition = projectPointOnScreen(m_transformPrev[3], m_PV, m_viewportSize);
      const glm::vec2 gizmoScreenPositionDx = projectPointOnScreen(glm::vec3(m_transformPrev[3]) + axis * _getViewScale(), m_PV, m_viewportSize);
      glm::vec2   axis2D = gizmoScreenPositionDx - gizmoScreenPosition;
      if (glm::dot(axis2D, axis2D) > 1.e-12f)
      {
        axis2D = glm::normalize(axis2D);
        const glm::vec2 mouseScreenProjected = gizmoScreenPosition + glm::dot(axis2D, glm::vec2(mouseScreenPosition) - gizmoScreenPosition) * axis2D;
        // mouseScreenProjected : we can do even better, with a 2D-projection using the on-screen-viewSide direction along the (gizmoScreenPosition, axis2D).
        // get a new 3D-ray
        const glm::vec3 mouseWorldProjected = unprojectPointToWorld(mouseScreenProjected, m_PVinv, m_viewportSize);
        newMouseDirection = mouseWorldProjected - m_cameraPosition;
      }
#else
      const glm::vec3 newMouseDirection = mouseDirection;
#endif
      // project on X-axis with direction = mouseDirection
      const glm::vec3 cross = glm::cross(axis, newMouseDirection);
      // -> project on (rayDir,cross)-plane with direction = mouseDirection
      const glm::vec3 planeNormal1 = glm::cross(cross, axis);
      const glm::vec3 posToProj = newMouseDirection * (glm::dot(planeNormal1, cameraToPos) / glm::dot(planeNormal1, newMouseDirection)) - cameraToPos;
      // -> distance along the axis
      const float  tAxis = glm::dot(posToProj, axis) / glm::dot(axis, axis);
      mouseWorldPos = position + tAxis * axis;
    }
    else
    {
      TRE_FATAL("not reached");
    }
  }

  // Now, apply the new transform on the Gizmo

  if (m_Type == GMODE_TRANSLATING)
  {
    if (glm::dot(viewDepth, mouseWorldPos - m_cameraPosition) > 0.f)
    {
      // cancel translating
      m_transform = m_transformPrev;
      if (m_transformToUpdate != nullptr)
      {
        (*m_transformToUpdate)[3] = m_transform[3];
        m_transformToUpdateChanged = true;
      }
      return;
    }

    glm::mat3 parentTransform3x3Inverse = glm::mat3(m_parentTransform);
    parentTransform3x3Inverse = glm::inverse(parentTransform3x3Inverse);
    const glm::vec3 outTranslation = parentTransform3x3Inverse * (mouseWorldPos - m_mouseWorldPositionPrev);
    m_transform[3] = m_transformPrev[3] + glm::vec4(outTranslation, 0.f);

    if (m_transformToUpdate != nullptr)
    {
      (*m_transformToUpdate)[3] = m_transform[3];
      m_transformToUpdateChanged = true;
    }
  }
  else if (m_Type == GMODE_SCALING)
  {
    if (glm::dot(viewDepth, mouseWorldPos - m_cameraPosition) > 0.f)
    {
      // cancel scaling
      m_transform = m_transformPrev;
      if (m_transformToUpdate != nullptr)
      {
        *m_transformToUpdate = m_transform;
        m_transformToUpdateChanged = true;
      }
      return;
    }

    const glm::vec3 position = frame[3];

    if (projAxisAll) // specific case : global scaling
    {
      const float  projParam = glm::dot(mouseWorldPos - position, glm::vec3(m_viewTransposed[AXISINDEX_RIGHT]));
      const float  scaleFactor = projParam + 1.f;

      m_transform[0] = scaleFactor * m_transformPrev[0];
      m_transform[1] = scaleFactor * m_transformPrev[1];
      m_transform[2] = scaleFactor * m_transformPrev[2];

      if (m_transformToUpdate != nullptr)
      {
        (*m_transformToUpdate)[0] = scaleFactor * m_transformToUpdatePrev[0];
        (*m_transformToUpdate)[1] = scaleFactor * m_transformToUpdatePrev[1];
        (*m_transformToUpdate)[2] = scaleFactor * m_transformToUpdatePrev[2];
        m_transformToUpdateChanged = true;
      }

    }
    else
    {
        const glm::vec3 vectorPrev = m_mouseWorldPositionPrev - position;
        const float  scaleNewOld = glm::dot(vectorPrev, mouseWorldPos - position);
        const float  scaleSquaredPrev = glm::dot(vectorPrev, vectorPrev);
        const float  scaleFactor = scaleSquaredPrev < 1.e-6f ? 0.f : scaleNewOld / scaleSquaredPrev; // = scaleNew * scaleOld / (scaleOld * scaleOld) = scaleNew / scaleOld

        {
            glm::mat4 scaleMatrix = IDENTITY;
            if (m_grabbedAxis[0] != 0)
                scaleMatrix[0] *= scaleFactor;
            if (m_grabbedAxis[1] != 0)
                scaleMatrix[1] *= scaleFactor;
            if (m_grabbedAxis[2] != 0)
                scaleMatrix[2] *= scaleFactor;

            m_transform = m_transformPrev * scaleMatrix;

            if (m_transformToUpdate != nullptr)
            {
              if (m_isFrameLocal)
              {
                *m_transformToUpdate = m_transformToUpdatePrev * scaleMatrix;
              }
              else
              {
                // this case will make the local-transform matrix non-orthogonal anymore, but we must avoid this !!
                // So the resulting transform will not match exactly with the gizmo transform.

                const glm::mat4 newTransform = glm::transpose(m_parentPureRotation) * scaleMatrix * m_parentPureRotation * m_transformToUpdatePrev;
                const glm::vec3 scaleFactorOld = glm::vec3(glm::length(m_transformToUpdatePrev[0]),
                                                           glm::length(m_transformToUpdatePrev[1]),
                                                           glm::length(m_transformToUpdatePrev[2]));
                const glm::vec3 scaleFactorNew = glm::vec3(glm::length(newTransform[0]),
                                                           glm::length(newTransform[1]),
                                                           glm::length(newTransform[2]));

                scaleMatrix[0] = AXISX * scaleFactorNew.x / scaleFactorOld.x;
                scaleMatrix[1] = AXISY * scaleFactorNew.y / scaleFactorOld.y;
                scaleMatrix[2] = AXISZ * scaleFactorNew.z / scaleFactorOld.z;

                const glm::vec4 center = (*m_transformToUpdate)[3];
                *m_transformToUpdate = m_transformToUpdatePrev * scaleMatrix;
                (*m_transformToUpdate)[3] = center;
              }
            }

            m_transformToUpdateChanged = true;
        }
    }
  }
  else if (m_Type == GMODE_ROTATING)
  {
    const glm::vec3 position = frame[3];

    const glm::vec3 vectOld = glm::normalize(m_mouseWorldPositionPrev - position);
    const glm::vec3 vectNew = glm::normalize(mouseWorldPos - position);

    glm::mat4  rotateMatrix = IDENTITY;

    if (projAxisAll) // specific case : global rotation
    {
      const glm::vec3  rotationVector = glm::cross(vectOld, vectNew);
      const float   sinTheta = glm::length(rotationVector);

      if (sinTheta > 1.e-6f)
      {
        const glm::vec3  rv = glm::normalize(rotationVector);
        const float   cosTheta = glm::dot(vectOld, vectNew);
        const float   OneMcosTheta = 1.f - cosTheta;

        rotateMatrix[0] = glm::vec4(cosTheta + OneMcosTheta * rv.x * rv.x, sinTheta * rv.z + OneMcosTheta * rv.x * rv.y, -sinTheta * rv.y + OneMcosTheta * rv.x * rv.z, 0.f);
        rotateMatrix[1] = glm::vec4(-sinTheta * rv.z + OneMcosTheta * rv.y * rv.x, cosTheta + OneMcosTheta * rv.y * rv.y, sinTheta * rv.x + OneMcosTheta * rv.y * rv.z, 0.f);
        rotateMatrix[2] = glm::vec4(sinTheta * rv.y + OneMcosTheta * rv.z * rv.x, -sinTheta * rv.x + OneMcosTheta * rv.z * rv.y, cosTheta + OneMcosTheta * rv.z * rv.z, 0.f);
      }
    }
    else
    {
      glm::vec3  rv;
      if (m_grabbedAxis[0] != 0)
        rv = m_transformPrev[0];
      else if (m_grabbedAxis[1] != 0)
        rv = m_transformPrev[1];
      else if (m_grabbedAxis[2] != 0)
        rv = m_transformPrev[2];
      else // (m_grabbedAxis[3] != 0)
        rv = viewDepth;

#if 1 // Rotate by projection
      const float  sinTheta = glm::dot(glm::cross(vectOld, vectNew), rv);
      const float  cosTheta = glm::dot(vectOld, vectNew);
#else // Rotate by screen-space move
      const glm::vec3 mouveTangent3D = glm::cross(rv, vectOld);
      glm::vec2   mouveDirection2D = glm::vec2(m_PV * glm::vec4(mouveTangent3D, 0.f));
      //mouveDirection2D.y *= 1.f; // ???
      if (glm::dot(mouveDirection2D, mouveDirection2D) < 1.e-12f)
        mouveDirection2D = glm::vec2(1.f, -1.f);
      mouveDirection2D = glm::normalize(mouveDirection2D);
      const glm::ivec2  mouseScreenPositionPrev = projectPointOnScreen(m_mouseWorldPositionPrev, m_PV, m_viewportSize);
      const glm::vec2 deltaXY = mouseScreenPosition - mouseScreenPositionPrev;
      const float  theta = glm::dot(mouveDirection2D, deltaXY) * 0.006f; // rotation-angle by pixels
      const float  sinTheta = sin(theta);
      const float  cosTheta = cos(theta);
#endif

      if (fabsf(sinTheta) > 1.e-6f)
      {
        const float   OneMcosTheta = 1.f - cosTheta;

        rotateMatrix[0] = glm::vec4(cosTheta + OneMcosTheta * rv.x * rv.x, sinTheta * rv.z + OneMcosTheta * rv.x * rv.y, -sinTheta * rv.y + OneMcosTheta * rv.x * rv.z, 0.f);
        rotateMatrix[1] = glm::vec4(-sinTheta * rv.z + OneMcosTheta * rv.y * rv.x, cosTheta + OneMcosTheta * rv.y * rv.y, sinTheta * rv.x + OneMcosTheta * rv.y * rv.z, 0.f);
        rotateMatrix[2] = glm::vec4(sinTheta * rv.y + OneMcosTheta * rv.z * rv.x, -sinTheta * rv.x + OneMcosTheta * rv.z * rv.y, cosTheta + OneMcosTheta * rv.z * rv.z, 0.f);
      }
    }

    rotateMatrix = glm::transpose(m_parentPureRotation) * rotateMatrix * m_parentPureRotation;

    m_transform = rotateMatrix * m_transformPrev;
    m_transform[3] = m_transformPrev[3];

    _transformComputeRotating();

    if (m_transformToUpdate != nullptr)
    {
      glm::mat4 transformObject = rotateMatrix * m_transformToUpdatePrev;
      transformObject[3] = m_transformToUpdatePrev[3];

      *m_transformToUpdate = transformObject;
      m_transformToUpdateChanged = true;
    }

  }
  else
  {
    TRE_FATAL("not reached")
  }
}

//----------------------------------------------------------------------------

void gizmo::_checkSelection(glm::ivec4 &outSelection, const glm::ivec2 &mouseScreenPosition, glm::vec3 *outPosition)
{
  outSelection = ivec4ZERO;

  if (m_Type == GMODE_NONE || m_Type == GMODE_SHOWAXIS)
    return;

  const glm::vec3 mouseWorldPosFar = unprojectPointToWorld(mouseScreenPosition, m_PVinv, m_viewportSize);
  const glm::vec3 mouseDirection = glm::normalize(mouseWorldPosFar - m_cameraPosition);
  const CRay  mouseToWorld = CRay(m_cameraPosition, mouseDirection);

  if (m_Type == GMODE_ROTATING)
  {
    _checkSelectionSphere(mouseToWorld, outSelection, outPosition);
  }
  else
  {
    _checkSelectionQuads(mouseToWorld, outSelection, outPosition);

    if (outSelection == ivec4ZERO)
      _checkSelectionRoot(mouseToWorld, outSelection, outPosition);
    if (outSelection == ivec4ZERO)
      _checkSelectionAxis(mouseToWorld, outSelection, outPosition);
  }
}

//----------------------------------------------------------------------------

void gizmo::_checkSelectionQuads(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition) const
{
  const glm::mat4 frame = m_transform;

  const glm::vec3 position = frame[3];
  const glm::vec3 camToPos = position - ray.m_origin;

  const float  scale = _getViewScale();

  const float  squareSizeIn = scale * (m_GeomDesc.m_SquareDist - m_GeomDesc.m_SquareSize * 0.5f);
  const float  squareSizeOu = scale * (m_GeomDesc.m_SquareSize + m_GeomDesc.m_SquareSize * 0.5f);

  float distMin = std::numeric_limits<float>::infinity();
  int  hintMin = -1;

  // Check plane
  for (uint iAxis = 0; iAxis < 3; ++iAxis)
  {
    const glm::vec3 planeNormal = frame[iAxis];
    const glm::vec3 planeTangU = frame[(iAxis + 1)%3];
    const glm::vec3 planeTangV = frame[(iAxis + 2)%3];
    const float dist = glm::dot(planeNormal, camToPos) / glm::dot(planeNormal, ray.m_direction);
    const glm::vec3 posToProj = ray.m_direction * dist - camToPos;
    // -> distance along the axis
    const float  uAxis = glm::dot(posToProj, planeTangU) / glm::dot(planeTangU, planeTangU);
    const float  vAxis = glm::dot(posToProj, planeTangV) / glm::dot(planeTangV, planeTangV);
    if (dist >= 0.f && dist < distMin &&
        squareSizeIn <= uAxis && uAxis <= squareSizeOu &&
        squareSizeIn <= vAxis && vAxis <= squareSizeOu)
    {
      distMin = dist;
      hintMin = iAxis;
    }
  }

  outSelectionAxis = ((hintMin == 0) ? 1 : 0) * glm::ivec4(0, 1, 1, 0) +
                     ((hintMin == 1) ? 1 : 0) * glm::ivec4(1, 0, 1, 0) +
                     ((hintMin == 2) ? 1 : 0) * glm::ivec4(1, 1, 0, 0);

  if (outPosition != nullptr && hintMin >= 0)
    *outPosition = ray.m_origin + distMin * ray.m_direction;
}

//----------------------------------------------------------------------------

void gizmo::_checkSelectionRoot(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition) const
{
  const glm::mat4 frame = m_transform;

  const glm::vec3 position = frame[3];
  const glm::vec3 camToPos = position - ray.m_origin;

  const float  sphereSize = _getViewScale() * m_GeomDesc.m_RootSize;

  const glm::vec3 tang = glm::cross(camToPos, ray.m_direction);
  const float  distSquared = glm::dot(tang, tang);

  //Note: we assume that ray.m_origin == m_CameraPosition

  if (distSquared <= sphereSize * sphereSize)
  {
    outSelectionAxis = glm::ivec4(1, 1, 1, 0);

    if (outPosition != nullptr)
    {
      TRE_ASSERT(m_Type != GMODE_ROTATING);
      // project on screen-plane with direction = ray.m_direction
      const glm::vec3 planeNormal = m_viewTransposed[AXISINDEX_DEPTH];
      *outPosition = m_cameraPosition + ray.m_direction * (glm::dot(planeNormal, camToPos) / glm::dot(planeNormal, ray.m_direction));
    }
  }
}

//----------------------------------------------------------------------------

void gizmo::_checkSelectionAxis(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition) const
{
  const glm::mat4 frame = m_transform;

  const glm::vec3 position = frame[3];
  const float  scale = _getViewScale();
  const glm::vec3 camToPos = position - ray.m_origin;

  const float  axisSize = scale * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight);
  const float  axisWidth = scale * m_GeomDesc.m_AxisRadius;

  float distMin = std::numeric_limits<float>::infinity();
  int   hintMin = -1;
  glm::vec3 pointOnAxis;

  //Note: we assume that ray.m_origin == m_CameraPosition

  // Check axis
  for (uint iAxis = 0; iAxis < 3; ++iAxis)
  {
    const glm::vec3 axis = frame[iAxis];
    const glm::vec3 cross = glm::cross(axis, ray.m_direction);
    // -> project on (rayDir,cross)-plane with direction = ray.m_direction
    const glm::vec3 planeNormal1 = glm::cross(cross, axis);
    const glm::vec3 posToProj = ray.m_direction * (glm::dot(planeNormal1, camToPos) / glm::dot(planeNormal1, ray.m_direction)) - camToPos;
    // -> distance along the axis
    const float  tAxis = glm::dot(posToProj, axis) / glm::dot(axis, axis);
    if (tAxis >= 0.f && tAxis <= axisSize)
    {
      const glm::vec3 crossDir = glm::cross(ray.m_direction, axis);
      const float  denom = glm::length(crossDir);
      if (denom > 1.e-6f)
      {
        const float dist = fabsf(glm::dot(camToPos, crossDir)) / denom;
        if (dist < distMin && dist < axisWidth * 7.f)
        {
          distMin = dist;
          hintMin = iAxis;
          pointOnAxis = position + tAxis * axis;
        }
      }
    }
  }

  outSelectionAxis = ((hintMin == 0) ? 1 : 0) * glm::ivec4(1, 0, 0, 0) +
                     ((hintMin == 1) ? 1 : 0) * glm::ivec4(0, 1, 0, 0) +
                     ((hintMin == 2) ? 1 : 0) * glm::ivec4(0, 0, 1, 0);

  if (outPosition != nullptr && hintMin >= 0)
      *outPosition = pointOnAxis;
}

//----------------------------------------------------------------------------

void gizmo::_checkSelectionSphere(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition) const
{
  const glm::mat4 frame = m_transform;

  const glm::vec3 position = frame[3];
  const glm::vec3 camToPos = position - ray.m_origin;

  const float  radius = _getViewScale() * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight);
  const float  radius2Sup = radius * radius;
  const float  radius2Inf = radius2Sup * 0.96f * 0.96f;

  // in fact, it should check the intersection with a torus, but this implies to get the roots of a quartic-equation ...

  TRE_ASSERT(m_Type == GMODE_ROTATING);
  const glm::vec3 viewDepth = glm::vec4(glm::normalize(m_cameraPosition - glm::vec3(m_transform[3])), 0.f);

  // Project on the sphere

  const float  tRayCenter = glm::dot(camToPos, ray.m_direction);
  const float  tOffsetSquared = tRayCenter * tRayCenter - glm::dot(camToPos, camToPos) + radius * radius;
  const bool   hitSphere = tOffsetSquared >= 0.f;

  const float  tOffset = hitSphere ? sqrtf(tOffsetSquared) : 0.f;
  const float  tRayClosestPoint = tRayCenter - tOffset;

  const glm::vec3 camToShpere = tRayClosestPoint * ray.m_direction;

  glm::vec3   pointOnSpin = ray.m_origin + camToShpere;

  if (hitSphere && tRayClosestPoint > 0.f) // not behind the camera
  {
    outSelectionAxis = glm::ivec4(1, 1, 1, 0); // full rotation
  }

  // Check if it is close of an axis, for rotation on specific axis (x, y, z or screen-space)

  const glm::vec3 posToShpere = camToShpere - camToPos; // we assume that ray.m_origin == m_cameraPosition

  float   distMin = 1.e30f;

  // Check ring X-Y-Z
  for (uint iAxis = 0; iAxis < 3; ++iAxis)
  {
      const glm::vec3 planeNormal = frame[iAxis];
      const float  tAxisPlane = glm::dot(planeNormal, camToPos) / glm::dot(planeNormal, ray.m_direction);
      if (tAxisPlane > 0.f)
      {
        // Try to project directly on the plane
        const glm::vec3 posToProj = ray.m_direction * tAxisPlane - camToPos; // we assume that ray.m_origin == m_CameraPosition
        const float  dist2 = glm::dot(posToProj, posToProj);
        if (glm::dot(posToProj, viewDepth) > 0.f && dist2 >= radius2Inf && dist2 <= radius2Sup && dist2 && (dist2 - radius2Inf) < distMin)
        {
          distMin = dist2 - radius2Inf;
          outSelectionAxis = ivec4ZERO;
          outSelectionAxis[iAxis] = 1;
          pointOnSpin = ray.m_origin + ray.m_direction * tAxisPlane;
        }
        // Try to project on the shpere, then on the plane
        else if (hitSphere)
        {
          const float  tRayAbs = fabsf(glm::dot(planeNormal, posToShpere));
          if (tRayAbs < 0.05f * radius && tRayAbs < distMin)
          {
            distMin = tRayAbs;
            outSelectionAxis = ivec4ZERO;
            outSelectionAxis[iAxis] = 1;
            pointOnSpin = ray.m_origin + ray.m_direction * tAxisPlane;
          }
      }
    }
  }

  // Check ring Screen-space
  {
    const float  radiusSP = _getViewScale() * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight * 2.f + m_GeomDesc.m_AxisRadius * 2.f);
    const float  radiusSP2Sup = radiusSP * radiusSP;
    const float  radiusSP2Inf = radiusSP2Sup * 0.96f * 0.96f;

    // project on screen-plane with direction = mouseDirection
    const glm::vec3 planeNormal = viewDepth;
    const glm::vec3 camToProj = ray.m_direction * (glm::dot(planeNormal, camToPos) / glm::dot(planeNormal, ray.m_direction));
    const glm::vec3 posToProj = camToProj - camToPos;
    const float localDist2 = glm::dot(posToProj, posToProj);
    if (radiusSP2Inf <= localDist2 && localDist2 <= radiusSP2Sup)
    {
      outSelectionAxis = glm::ivec4(0, 0, 0, 1);
      pointOnSpin = ray.m_origin + camToProj;
    }
  }

  if (outPosition != nullptr)
      *outPosition = pointOnSpin;
}

//----------------------------------------------------------------------------

void gizmo::draw() const
{
  TRE_ASSERT(m_model.isLoadedGPU());
  TRE_ASSERT(m_shader != nullptr);

  if (m_Type == gizmo::GMODE_NONE)
    return;

  glm::mat4 scaleMat = _getViewScale() * IDENTITY;
  scaleMat[3] = AXISW;
  glm::mat4  matPVM = m_PV * m_transform * scaleMat;

  glUseProgram(m_shader->m_drawProgram);

  if (m_Type == gizmo::GMODE_ROTATING)
  {
    m_shader->setUniformMatrix(matPVM);
    m_model.drawcall(m_PartSphere, 1, true);  // rotation sphere
    m_model.drawcall(m_PartGhostT, 1, false); // axis ghost
  }
  if (m_Type != gizmo::GMODE_ROTATING && isGrabbed())
  {
    glm::mat4 scaleMatPre = _getViewScalePre() * IDENTITY;
    scaleMatPre[3] = AXISW;
    glm::mat4  matPVMpre = m_PV * m_transformPrev * scaleMatPre;
    m_shader->setUniformMatrix(matPVMpre);

    const std::size_t partGhost = m_Type == gizmo::GMODE_SCALING ? m_PartGhostS : m_PartGhostT;

    m_model.drawcall(partGhost, 1, true);
  }

  if (m_Type == gizmo::GMODE_SHOWAXIS)
  {
    m_shader->setUniformMatrix(matPVM);
    m_model.drawcall(m_PartAxisTX, 3, true);
  }
  else if (m_Type != gizmo::GMODE_ROTATING)
  {
    m_shader->setUniformMatrix(matPVM);

    // Draw Axis
    const std::size_t partAxis = m_Type == gizmo::GMODE_SCALING ? m_PartAxisSX : m_PartAxisTX;
    m_model.drawcall(partAxis, 3, true);

    // Draw Root
    m_model.drawcall(m_PartRoot, 1, false);

    // Draw Square handles
    if (!isGrabbed() || ((m_grabbedAxis.x + m_grabbedAxis.y + m_grabbedAxis.z) == 2))
      m_model.drawcall(m_PartSquareX, 3, false);
  }
  else if (m_Type == gizmo::GMODE_ROTATING)
  {
    glm::mat4 matPVMrotating;

    matPVMrotating = m_PV * m_transformRotating[0] * scaleMat;
    m_shader->setUniformMatrix(matPVMrotating);
    m_model.drawcall(m_PartRotationX, 1, false);

    matPVMrotating = m_PV * m_transformRotating[1] * scaleMat;
    m_shader->setUniformMatrix(matPVMrotating);
    m_model.drawcall(m_PartRotationX + 1, 1, false);

    matPVMrotating = m_PV * m_transformRotating[2] * scaleMat;
    m_shader->setUniformMatrix(matPVMrotating);
    m_model.drawcall(m_PartRotationX + 2, 1, false);

    matPVMrotating = m_PV * m_transformRotating[3] * scaleMat;
    m_shader->setUniformMatrix(matPVMrotating);
    m_model.drawcall(m_PartRotationX + 3, 1, false);
  }
  else
  {
    TRE_FATAL("not reachable code")
  }

  return;
}

//----------------------------------------------------------------------------

bool gizmo::loadIntoGPU()
{
  TRE_ASSERT(!m_model.isLoadedGPU());

  // Create the model part:

  glm::mat4 xMatrix;
  xMatrix[0] = AXISZ;
  xMatrix[1] = AXISX;
  xMatrix[2] = AXISY;
  xMatrix[3] = AXISW;

  glm::mat4 yMatrix;
  yMatrix[0] = AXISX;
  yMatrix[1] = AXISY;
  yMatrix[2] = AXISZ;
  yMatrix[3] = AXISW;

  glm::mat4 zMatrix;
  zMatrix[0] = AXISY;
  zMatrix[1] = AXISZ;
  zMatrix[2] = AXISX;
  zMatrix[3] = AXISW;

  // -> Root
  {
    m_PartRoot = m_model.createPartFromPrimitive_uvtrisphere(IDENTITY, m_GeomDesc.m_RootSize, 9, 18);
  }

  // -> Rotation sphere
  {
    m_PartSphere = m_model.createPartFromPrimitive_uvtrisphere(IDENTITY, m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight, 21, 42);
  }

  // -> Square handles
  {
    const float  squareSizeMid = m_GeomDesc.m_SquareDist + m_GeomDesc.m_SquareSize * 0.5f;
    const float  squareSizeSize = m_GeomDesc.m_SquareSize;

    glm::mat4 transf;

    transf = xMatrix;
    transf[3] = (transf[0]+transf[2]) * squareSizeMid;
    m_PartSquareX = m_model.createPartFromPrimitive_square(transf, squareSizeSize);

    transf = yMatrix;
    transf[3] = (transf[0]+transf[2]) * squareSizeMid;
    const std::size_t py = m_model.createPartFromPrimitive_square(transf, squareSizeSize);
    TRE_ASSERT(py == m_PartSquareX + 1);

    transf = zMatrix;
    transf[3] = (transf[0]+transf[2]) * squareSizeMid;
    const std::size_t pz = m_model.createPartFromPrimitive_square(transf, squareSizeSize);
    TRE_ASSERT(pz == m_PartSquareX + 2);
  }

  // -> Axis (for translation)
  {
    glm::mat4 transf;

    transf = xMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    m_PartAxisTX = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength;
    const std::size_t px2 = m_model.createPartFromPrimitive_cone(transf, m_GeomDesc.m_ArrowRadius, m_GeomDesc.m_ArrowHeight, 8);
    m_model.mergeParts(m_PartAxisTX, px2);

    transf = yMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    const std::size_t py1 = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength;
    const std::size_t py2 = m_model.createPartFromPrimitive_cone(transf, m_GeomDesc.m_ArrowRadius, m_GeomDesc.m_ArrowHeight, 8);
    m_model.mergeParts(py1, py2);
    TRE_ASSERT(py1 == m_PartAxisTX + 1);

    transf = zMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    const std::size_t pz1 = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength;
    const std::size_t pz2 = m_model.createPartFromPrimitive_cone(transf, m_GeomDesc.m_ArrowRadius, m_GeomDesc.m_ArrowHeight, 8);
    m_model.mergeParts(pz1, pz2);
    TRE_ASSERT(pz1 == m_PartAxisTX + 2);
  }

  // -> Axis for Scale

  {
    glm::mat4 transf;

    transf = xMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    m_PartAxisSX = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowRadius * 0.5f);
    const std::size_t px2 = m_model.createPartFromPrimitive_box(transf, m_GeomDesc.m_ArrowRadius * 2.f);
    m_model.mergeParts(m_PartAxisSX, px2);

    transf = yMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    const std::size_t py1 = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowRadius * 0.5f);
    const std::size_t py2 = m_model.createPartFromPrimitive_box(transf, m_GeomDesc.m_ArrowRadius * 2.f);
    m_model.mergeParts(py1, py2);
    TRE_ASSERT(py1 == m_PartAxisSX + 1);

    transf = zMatrix;
    transf[3] = transf[1] * m_GeomDesc.m_AxisLength * 0.5f;
    const std::size_t pz1 = m_model.createPartFromPrimitive_tube(transf, m_GeomDesc.m_AxisRadius, m_GeomDesc.m_AxisLength, true, 8);
    transf[3] = transf[1] * (m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowRadius * 0.5f);
    const std::size_t pz2 = m_model.createPartFromPrimitive_box(transf, m_GeomDesc.m_ArrowRadius * 2.f);
    m_model.mergeParts(pz1, pz2);
    TRE_ASSERT(pz1 == m_PartAxisSX + 2);
  }

  // -> Rotation handles
  {
    const float torusRadius = m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight;
    const float innerRadius = m_GeomDesc.m_AxisRadius;
    const float diskInnerRadius = m_GeomDesc.m_AxisLength + m_GeomDesc.m_ArrowHeight * 2;
    const float diskOuterRadius = diskInnerRadius + m_GeomDesc.m_AxisRadius * 2.f;

    m_PartRotationX = m_model.createPartFromPrimitive_halftorus(xMatrix, torusRadius, innerRadius, true, 32, 8);

    const std::size_t py = m_model.createPartFromPrimitive_halftorus(yMatrix, torusRadius, innerRadius, true, 32, 8);
    TRE_ASSERT(py == m_PartRotationX + 1);

    const std::size_t pz = m_model.createPartFromPrimitive_halftorus(zMatrix, torusRadius, innerRadius, true, 32, 8);
    TRE_ASSERT(pz == m_PartRotationX + 2);

    const std::size_t ps = m_model.createPartFromPrimitive_disk(zMatrix, diskOuterRadius, diskInnerRadius, 64);
    TRE_ASSERT(ps == m_PartRotationX + 3);
  }

  // gizmo ghost
  {
    m_PartGhostT = m_model.copyPart(m_PartRoot);
    const std::size_t partGT2 = m_model.copyPart(m_PartAxisTX, 3);
    m_model.mergeParts(m_PartGhostT, partGT2);

    m_PartGhostS = m_model.copyPart(m_PartRoot);
    const std::size_t partGS2 = m_model.copyPart(m_PartAxisSX, 3);
    m_model.mergeParts(m_PartGhostS, partGS2);

    // color does not change - do it here.
    // hmm, not sure - then the gizmo-transparency could be managed.
    m_model.colorizePart(m_PartGhostT, glm::vec4(0.7f, 0.7f, 0.7f, 0.6f));
    m_model.colorizePart(m_PartGhostS, glm::vec4(0.7f, 0.7f, 0.7f, 0.6f));
  }

  // End - load
  if (!m_model.loadIntoGPU())
    return false;

  updateIntoGPU(); // at least compute and upload once the color buffer

  return true;
}

//----------------------------------------------------------------------------

void gizmo::updateIntoGPU()
{
  TRE_ASSERT(m_model.isLoadedGPU());

  // Fill buffers

  const bool grabbedAxisFull = m_grabbedAxis.x != 0 && m_grabbedAxis.y != 0 && m_grabbedAxis.z != 0;
  const bool hoveredAxisFull = m_hoveredAxis.x != 0 && m_hoveredAxis.y != 0 && m_hoveredAxis.z != 0;

  #define GET_COLOR(cond1, cond2, color1true, color1false2true, color1false2false) \
    (cond1) ? color1true : ((cond2) ? color1false2true : color1false2false)

  // root
  m_model.colorizePart(m_PartRoot, GET_COLOR(grabbedAxisFull, hoveredAxisFull,
                                              glm::vec4(1, 0, 1, 0.7f),
                                              glm::vec4(1, 1, 0, 0.7f),
                                              glm::vec4(1, 1, 1, 0.7f)));

  // rotation sphere
  m_model.colorizePart(m_PartSphere, GET_COLOR(grabbedAxisFull, hoveredAxisFull,
                                                glm::vec4(1.0f, 0.5f, 1.0f, 0.4f),
                                                glm::vec4(1.0f, 1.0f, 0.5f, 0.2f),
                                                glm::vec4(0.8f, 0.8f, 0.8f, 0.2f)));

  // axis translate + scale
  const glm::vec4 cAxisX = GET_COLOR(m_grabbedAxis.x != 0, m_hoveredAxis.x != 0,
                                     glm::vec4(1, 0, 1, 1.0f),
                                     glm::vec4(1, 1, 0, 1.0f),
                                     glm::vec4(1, 0, 0, 1.0f));

  m_model.colorizePart(m_PartAxisTX, cAxisX);
  m_model.colorizePart(m_PartAxisSX, cAxisX);

  const glm::vec4 cAxisY = GET_COLOR(m_grabbedAxis.y != 0, m_hoveredAxis.y != 0,
                                     glm::vec4(1, 0, 1, 1.0f),
                                     glm::vec4(1, 1, 0, 1.0f),
                                     glm::vec4(0, 1, 0, 1.0f));

  m_model.colorizePart(m_PartAxisTX + 1, cAxisY);
  m_model.colorizePart(m_PartAxisSX + 1, cAxisY);

  const glm::vec4 cAxisZ = GET_COLOR(m_grabbedAxis.z != 0, m_hoveredAxis.z != 0,
                                     glm::vec4(1, 0, 1, 1.0f),
                                     glm::vec4(1, 1, 0, 1.0f),
                                     glm::vec4(0, 0, 1, 1.0f));

  m_model.colorizePart(m_PartAxisTX + 2, cAxisZ);
  m_model.colorizePart(m_PartAxisSX + 2, cAxisZ);

  // squares
  const glm::vec4 cSquareX = GET_COLOR(m_grabbedAxis.y != 0 && m_grabbedAxis.z != 0, m_hoveredAxis.y != 0 && m_hoveredAxis.z != 0,
                                       glm::vec4(1.0f, 0.5f, 1.0f, 0.7f),
                                       glm::vec4(1.0f, 1.0f, 0.5f, 0.5f),
                                       glm::vec4(1.0f, 0.5f, 0.5f, 0.5f));

  m_model.colorizePart(m_PartSquareX, cSquareX);

  const glm::vec4 cSquareY = GET_COLOR(m_grabbedAxis.x != 0 && m_grabbedAxis.z != 0, m_hoveredAxis.x != 0 && m_hoveredAxis.z != 0,
                                       glm::vec4(1.0f, 0.5f, 1.0f, 0.7f),
                                       glm::vec4(1.0f, 1.0f, 0.5f, 0.5f),
                                       glm::vec4(0.5f, 1.0f, 0.5f, 0.5f));

  m_model.colorizePart(m_PartSquareX + 1, cSquareY);

  const glm::vec4 cSquareZ = GET_COLOR(m_grabbedAxis.x != 0 && m_grabbedAxis.y != 0, m_hoveredAxis.x != 0 && m_hoveredAxis.y != 0,
                                       glm::vec4(1.0f, 0.5f, 1.0f, 0.7f),
                                       glm::vec4(1.0f, 1.0f, 0.5f, 0.5f),
                                       glm::vec4(0.5f, 0.5f, 1.0f, 0.5f));

  m_model.colorizePart(m_PartSquareX + 2, cSquareZ);

  // rotation along axis

  const glm::vec4 cRotationX = GET_COLOR(grabbedAxisFull ? 0 : (m_grabbedAxis.x != 0),
                                         hoveredAxisFull ? 0 : (m_hoveredAxis.x != 0),
                                         glm::vec4(1, 0, 1, 1.0f),
                                         glm::vec4(1, 1, 0, 1.0f),
                                         glm::vec4(1, 0, 0, 1.0f));

  m_model.colorizePart(m_PartRotationX, cRotationX);

  const glm::vec4 cRotationY = GET_COLOR(grabbedAxisFull ? 0 : (m_grabbedAxis.y != 0),
                                         hoveredAxisFull ? 0 : (m_hoveredAxis.y != 0),
                                         glm::vec4(1, 0, 1, 1.0f),
                                         glm::vec4(1, 1, 0, 1.0f),
                                         glm::vec4(0, 1, 0, 1.0f));

  m_model.colorizePart(m_PartRotationX + 1, cRotationY);

  const glm::vec4 cRotationZ = GET_COLOR(grabbedAxisFull ? 0 : (m_grabbedAxis.z != 0),
                                         hoveredAxisFull ? 0 : (m_hoveredAxis.z != 0),
                                         glm::vec4(1, 0, 1, 1.0f),
                                         glm::vec4(1, 1, 0, 1.0f),
                                         glm::vec4(0, 0, 1, 1.0f));

  m_model.colorizePart(m_PartRotationX + 2, cRotationZ);

  const glm::vec4 cRotationS = GET_COLOR(grabbedAxisFull ? 0 : (m_grabbedAxis.w != 0),
                                         hoveredAxisFull ? 0 : (m_hoveredAxis.w != 0),
                                         glm::vec4(1, 0, 1, 1.0f),
                                         glm::vec4(1, 1, 0, 1.0f),
                                         glm::vec4(1, 1, 1, 1.0f));

  m_model.colorizePart(m_PartRotationX + 3, cRotationS);

  #undef GET_COLOR

  // Upload buffer into GPU

  m_model.updateIntoGPU();
}

//----------------------------------------------------------------------------

void gizmo::clearGPU()
{
  m_model.clearGPU();
}

//----------------------------------------------------------------------------

bool gizmo::loadShader(shader *shaderToUse)
{
  if (shaderToUse != nullptr)
  {
    m_shader = shaderToUse;
    m_shaderOwnder = false;
  }
  else
  {
    m_shader = new shader;
    m_shaderOwnder = true;
    return m_shader->loadShader(shader::PRGM_3D, shader::PRGM_COLOR, "gizmo");
  }
  return true;
}

//----------------------------------------------------------------------------

void gizmo::clearShader()
{
  if (m_shaderOwnder)
  {
    TRE_ASSERT(m_shader != nullptr);
    m_shader->clearShader();
    delete m_shader;
  }
  m_shader = nullptr;
  m_shaderOwnder = true;
}

//----------------------------------------------------------------------------
}
