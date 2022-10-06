#ifndef GIZMO_H
#define GIZMO_H

#include "tre_model.h"

#include <vector>

namespace tre {

class shader;       // foward decl.

/**
* @brief The gizmo class
*
*
*/
class gizmo
{

public:

  enum s_mode
  {
    GMODE_NONE        = 0x00,
    GMODE_TRANSLATING = 0x01,
    GMODE_ROTATING    = 0x02,
    GMODE_SCALING     = 0x04,
    GMODE_SHOWAXIS    = 0x08,
  };

  struct  s_geometry
  {
    float m_RootSize = 0.1f;
    float m_AxisLength = 0.82f;
    float m_AxisRadius = 0.016f;
    float m_SquareDist = 0.35f;
    float m_SquareSize = 0.5f;
    float m_ArrowHeight = 0.15f;
    float m_ArrowRadius = 0.03f;

    bool operator==(const s_geometry &other) const
    {
      return fabs(m_RootSize - other.m_RootSize) < (1.e-3f * m_RootSize) &&
             fabs(m_AxisLength - other.m_AxisLength) < (1.e-3f * m_AxisLength) &&
             fabs(m_AxisRadius - other.m_AxisRadius) < (1.e-3f * m_AxisRadius) &&
             fabs(m_SquareDist - other.m_SquareDist) < (1.e-3f * m_SquareDist) &&
             fabs(m_SquareSize - other.m_SquareSize) < (1.e-3f * m_SquareSize) &&
             fabs(m_ArrowHeight - other.m_ArrowHeight) < (1.e-3f * m_ArrowHeight) &&
             fabs(m_ArrowRadius - other.m_ArrowRadius) < (1.e-3f * m_ArrowRadius);
    }
  };

  gizmo();
  ~gizmo();

  s_mode Mode() const { return m_Type; }
  void SetMode(s_mode mode) { if (!isGrabbed()) { m_Type = mode; m_hoveredAxis = glm::ivec4(0); _transformFromOutput(); } }
  void SetLocalFrame(bool enabled) { if (m_isFrameLocal != enabled) { m_isFrameLocal = enabled; _transformFromOutput(); }; }
  void setParentTransform(const glm::mat4 &transform); ///< Set the Gizmo "world-space" transform (ie. global-space)
  void setTransfromToUpdate(glm::mat4 *transform);
  bool isGrabbed() const { return m_clicked; }
  bool TransformChanged() const { return m_transformToUpdateChanged; }

  /// @name Events
  /// @{
  void updateCameraInfo(const glm::mat4 &mProj, const glm::mat4 &mView, const glm::ivec2 &screenSize);
  bool acceptEvent(const SDL_Event &event);
  bool acceptEvent(const glm::ivec2 &mousePosition, bool clicked);

  float &GizmoSelfScale() { return m_SelfScale; }
  /// @}

private:

  // MATH UTILS
  static glm::vec2 projectPointOnScreen(const glm::vec3 &point, const glm::mat4 &mvp, const glm::ivec2 &screenSize);
  static glm::vec3 unprojectPointToWorld(const glm::vec2 &pixelCoords, const glm::mat4 &inverseMVP, const glm::ivec2 &screenSize);

  float _getViewScale() const; // scale of the Gizmo, so it will always have the same size on the screen.
  float _getViewScalePre() const;
  void  _transformReset();
  void  _transformFromOutput(); // compute the m_Transform and other intern transforms from the outputs values
  void  _transformComputeRotating();
  void  _applyMouseMouve(const glm::ivec2 &mouseScreenPosition);

  // State
  s_mode     m_Type = GMODE_NONE;
  glm::mat4  m_parentTransform;       ///< transform that defines the "world-space"
  glm::mat4  m_parentPureRotation;    ///< the rotationnal-part of m_ParentTransform
  glm::vec3  m_mouseWorldPositionPrev;
  glm::mat4  m_transformPrev;         ///< save of "m_Transform" while the Gizmo is grabbed
  glm::mat4  m_transform;             ///< current-transform of the Gizmo (in the "world-space")
  glm::mat4  m_transformRotating[4];  ///< specific transforms for the rotation handles (x,y,z axis + screen-space)
  glm::mat4  m_transformToUpdatePrev; ///< save of "m_TransformToUpdate" while the Gizmo is grabbed
  float      m_SelfScale = 0.13f;     ///< global Gizmo scale
  bool       m_clicked = false;
  bool       m_isFrameLocal = false;  ///< false to keep the gizmo in the global frame, true to have the gizmo follow the local frame
                                      ///< in fact, other frames could be definied (euler-frame, global-after-parent, ...)

  // Output
  glm::mat4* m_transformToUpdate = nullptr;
  bool       m_transformToUpdateChanged = false; // true when  "*m_TransformToUpdate" values have been modified

  // Events
  glm::ivec4 m_grabbedAxis = glm::ivec4(0); // x,y,z and screen-space
  glm::ivec4 m_hoveredAxis = glm::ivec4(0); // x,y,z and screen-space

  struct CRay
  {
    glm::vec3 m_origin;
    glm::vec3 m_direction;

    CRay(const glm::vec3 &origin, const glm::vec3 &direction)
    {
      m_origin = origin;
      m_direction = glm::normalize(direction);
    }
  };

  void _checkSelection(glm::ivec4 &outSelection, const glm::ivec2 &mouseScreenPosition, glm::vec3 *outPosition = nullptr);
  void _checkSelectionRoot(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition = nullptr) const;
  void _checkSelectionQuads(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition = nullptr) const;
  void _checkSelectionAxis(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition = nullptr) const;
  void _checkSelectionSphere(const CRay &ray, glm::ivec4 &outSelectionAxis, glm::vec3 *outPosition = nullptr) const;

  /// @}

  /// @name Save viewport and camera info
  /// @{
  glm::vec2 m_viewportSize;
  glm::vec3 m_cameraPosition;
  glm::mat4 m_viewTransposed;
  glm::mat4 m_PV;
  glm::mat4 m_PVinv;
  /// @}

  /// @name Geometry and drawing
  /// @{
public:
  void draw() const; ///< Bind the shader and the resources, Emit draw-calls

  bool loadIntoGPU(); ///< Load a new model and generate model's parts
  void updateIntoGPU(); ///< Update the vertex-data of the model into the V-RAM.
  void clearGPU(); ///< Clean V-RAM.

  bool loadShader(shader *shaderToUse = nullptr);
  void clearShader();

private:
  const s_geometry   m_GeomDesc;
  modelSemiDynamic3D m_model;
  shader             *m_shader = nullptr;
  bool               m_shaderOwnder = true;

  std::size_t m_PartRoot;
  std::size_t m_PartSphere;
  std::size_t m_PartAxisTX;    ///< DrawAxeT{X,Y,Z} such as DrawAxeTY = DrawAxeTX + 1 and DrawAxeTZ = DrawAxeTX + 2
  std::size_t m_PartAxisSX;    ///< DrawAxeS{X,Y,Z}
  std::size_t m_PartSquareX;   ///< m_DrawSquare{X,Y,Z}
  std::size_t m_PartRotationX; ///< m_DrawRotation{X,Y,Z}
  std::size_t m_PartGhostT;    ///< root + translating-axis (ghost of the gizmo while grabbed) // TODO
  std::size_t m_PartGhostS;    ///< root + scaling-axis (ghost of the gizmo while grabbed) // TODO
  /// @}

};

//----------------------------------------------------------------------------

}

#endif
