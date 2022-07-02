#ifndef MODEL_TOOLS_H
#define MODEL_TOOLS_H

#include "openglinclude.h"

#include "model.h"
#include "utils.h"

#include <vector>
#include <functional>

namespace tre {

/**
 * @namespace modelTools
 * Collection of algorithm about mesh processing.
 * Those functions are meant to be called in offline processes (aka not for real time usage).
 */
namespace modelTools {

//=============================================================================
// 2D ...

/// @brief computeConvexeEnvelop2D_XY
/// @param threshold
void computeConvexeEnvelop2D_XY(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, const float threshold, std::vector<glm::vec2> &outEnvelop);

/// @brief triangulate from a 2D envelop. Interior is defined as the "left"-side of the envelop's edges.
void triangulate(const std::vector<glm::vec2> &envelop, std::vector<uint> &listTriangles);

//=============================================================================
// 3D ...

/// @brief computeConvexeSkin3D
/// @param threshold
/// @param outSkinTri
void computeConvexeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, const float threshold, std::vector<glm::vec3> &outSkinTri);

/// @brief computeSkin3D
/// @param outSkinTri
void computeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, std::vector<glm::vec3> &outSkinTri);

/// @brief compute out-normals for a part, with smoothed shading for indexed-mesh, flat shading elsewhere.
void computeOutNormal(const s_modelDataLayout &layout, const s_partInfo &part);

/// @brief compute tangent-space (tangent over the parametric-coord "u")
void computeTangentFromUV(const s_modelDataLayout &layout, const s_partInfo &part);

/// @brief decimate mesh. Modify the index-buffer only. Returns the new index-count.
/// @param threshold Curvature limit
std::size_t decimateKeepVertex(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold);

/// @brief decimate mesh. May modify vertex-data (merge vertex, ...). Returns the new index-count.
/// @param threshold Curvature limit
std::size_t decimateChangeVertex(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold);

/// @brief tetrahedralize from a 3D surface.
bool tetrahedralize(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<uint> &listTetrahedrons,
                    std::function<void(float progress)> *progressNotifier = nullptr);

//=============================================================================

} // namespace

} // namespace

#endif // MODEL_TOOLS_H
