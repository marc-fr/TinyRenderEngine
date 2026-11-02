#ifndef MODEL_IMPORTER_H
#define MODEL_IMPORTER_H

#include "tre_utils.h"
#include "tre_model.h"

namespace tre {

/**
 * @namespace modelImporter
 * Collection of importer from file-formats
 * Those functions are meant to be called in offline processes (aka not for real time usage).
 */
namespace modelImporter {

//=============================================================================

struct s_modelHierarchy;

/**
 * @brief The s_modelHierarchy struct
 */
struct s_modelHierarchy
{
  glm::mat4                     m_transform;
  std::size_t                   m_partId;
  std::vector<s_modelHierarchy> m_childs;
};

//=============================================================================
// Wavefront

/**
 * @brief addFromWavefront
 * Supported: "v", "vn", "vt", "f" (n-gons are fan-triangulated, so they must be convex),
 *             "o"/"g" (start a new part), "usemtl"/"mtllib" (diffuse color "Kd" and opacity "d").
 * Indices can be positive or negative (relative). Other keywords are ignored.
 * @return true on success. On failure, the model is not modified.
 */
bool addFromWavefront(modelIndexed &outModel, const std::string &objfile, const std::string &mtlfile = "");

//=============================================================================
// GLTF

/**
 * @brief addFromGLTF
 * Supported: ".gltf" (embedded or external buffers) and ".glb",
 *            the triangle primitives (the other modes are skipped), indexed or not,
 *            the attributes POSITION, NORMAL, TEXCOORD_0, COLOR_0,
 *            the base-color of the materials,
 *            the node hierarchy of the default scene.
 * Not supported: skins, morph-targets, sparse accessors, textures, animations, cameras, extensions.
 * An invalid file fails the import, and the model is not modified.
 * @return true on success. On failure, the model is not modified.
 */
bool addFromGLTF(modelIndexed &outModel, s_modelHierarchy &outHierarchy, const std::string &gltffile, const bool isBinary);

inline bool addFromGLTF(modelIndexed &outModel, const std::string &gltffile, const bool isBinary)
{
  s_modelHierarchy outHierarchy;
  return addFromGLTF(outModel, outHierarchy, gltffile, isBinary);
}

//=============================================================================

} // namespace modelImporter

} // namespace tre

#endif // MODEL_IMPORTER_H
