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
  glm::mat4                     m_transform = glm::mat4(1.f);
  std::vector<std::size_t>      m_parts;
  std::vector<s_modelHierarchy> m_childs;
};

//=============================================================================
// Wavefront

/**
 * @brief addFromWavefront
 * @param outModel
 * @param objfile
 * @param mtlfile
 * @return
 */
bool addFromWavefront(modelIndexed &outModel, const std::string &objfile, const std::string &mtlfile = "");

//=============================================================================
// GLTF

/**
 * @brief addFromGLTF
 * @param outModel
 * @param outHierarchy
 * @param gltffile
 * @param isBinary
 * @return
 */
bool addFromGLTF(modelIndexed &outModel, s_modelHierarchy &outHierarchy, const std::string &gltffile, const bool isBinary = false /* glb file */);

//=============================================================================

} // namespace modelImporter

} // namespace tre

#endif // MODEL_IMPORTER_H
