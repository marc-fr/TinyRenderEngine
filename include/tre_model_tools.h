#ifndef MODEL_TOOLS_H
#define MODEL_TOOLS_H

#include "tre_utils.h"

#include <vector>
#include <functional>

namespace tre {

struct s_modelDataLayout;
struct s_partInfo;
class modelIndexed;

/**
 * @namespace modelTools
 * Collection of algorithm about mesh processing.
 * Those functions are meant to be called in offline processes (aka not for real time usage).
 */
namespace modelTools {

//=============================================================================
// 2D ...

/// \brief computeBarycenter2D
/// \return the barycenter (xy) and the surface (z)
glm::vec3 computeBarycenter2D(const std::vector<glm::vec2> &inEnvelop);

/// @brief computeConvexeEnvelop2D_XY
/// @param threshold
void computeConvexeEnvelop2D_XY(const s_modelDataLayout &layout, const s_partInfo &part, const glm::mat4 &transform, const float threshold, std::vector<glm::vec2> &outEnvelop);

/// @brief triangulate from a 2D envelop. Interior is defined as the "left"-side of the envelop's edges.
/// @param listTriangles
void triangulate(const std::vector<glm::vec2> &envelop, std::vector<uint> &listTriangles);

//=============================================================================
// 3D ...

/// \brief computeBarycenter3D
/// \return the barycenter (xyz) and the volume (w)
glm::vec4 computeBarycenter3D(const s_modelDataLayout &layout, const s_partInfo &part);

/// @brief computeConvexeSkin3D extracts the triangles (3 clockwised points) of the convex hull of the mesh.
/// @param threshold
/// @param outSkinTri
void computeConvexeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, const float threshold, std::vector<glm::vec3> &outSkinTri);

/// @brief computeSkin3D extracts the triangles (3 clockwised points) of the skin. It assumes that the normals are correctly set.
/// @param outSkinTri
void computeSkin3D(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<glm::vec3> &outSkinTri);

/// @brief compute out-normals for a part, with smoothed shading for indexed-mesh, flat shading elsewhere.
void computeOutNormal(const s_modelDataLayout &layout, const s_partInfo &part, const bool clockwisedTriangles);

/// @brief compute tangent-space (tangent over the parametric-coord "u")
void computeTangentFromUV(const s_modelDataLayout &layout, const s_partInfo &part);

/// @brief decimate mesh. The algorithm is based on the local surface's curvature.
/// @param threshold Curvature limit
/// @return the new part. It returns (-1) on failure.
std::size_t decimateCurvature(modelIndexed &model, const std::size_t ipartIn, const float threshold);

/// @brief decimate mesh. The algorithm is based on voxels (aka 3d-grid). New vertices may be created, thus additional data (normals, uvs, ...) may be outdated.
/// @param gridResolution Size of the grid's cells
/// @return the new part. It returns (-1) on failure.
std::size_t decimateVoxel(modelIndexed &model, const std::size_t ipartIn, const float gridResolution, const bool keepSharpEdges);

/// @brief tetrahedralize from a 3D surface.
bool tetrahedralize(const s_modelDataLayout &layout, const s_partInfo &part, std::vector<uint> &listTetrahedrons,
                    std::function<void(float progress)> *progressNotifier = nullptr);

//=============================================================================

} // namespace

} // namespace

#endif // MODEL_TOOLS_H
