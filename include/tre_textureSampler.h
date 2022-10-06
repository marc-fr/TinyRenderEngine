#ifndef TEXTURESAMPLER_H
#define TEXTURESAMPLER_H

#define GLM_FORCE_INTRINSICS
#include <glm/glm.hpp>

#include <vector>

struct SDL_Surface;
typedef	struct tiff TIFF;

// ----------------------------------------------------------------------------

namespace tre {

/**
 * @brief textureSampler provides functions to re-sample textures to a new UV space.
 * Those functions should be used with off-line processes or in background thread, as they are not optimized/simplified to run in real-time (use the GPU for real-time needs).
 */

namespace textureSampler {

// ----------------------------------------------------------------------------

/**
 * @brief s_ImageData_R32F is a custom surface, with 1 sample per pixel, encoded in 32bit floats.
 */
struct s_ImageData_R32F
{
  unsigned w,h;
  float* pixels = nullptr;
};

// ----------------------------------------------------------------------------

/// @{
/** resample texture with homogeneous transform:
 * It samples the inTexture with the homogeneous transform defined in the inTexture uv-space by [in_coord0, in_coord0 + in_coordU_incr] * [in_coord0, in_coord0 + in_coordV_incr]
 * Note: those functions use the OpenGL coordinate-system ((0,0) is the Top-Left corner)
 */

void resample(SDL_Surface *outTexture     , SDL_Surface *inTexture, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));
void resample(SDL_Surface *outTexture     , TIFF        *inTexture, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));
void resample(s_ImageData_R32F *outTexture, SDL_Surface *inTexture, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));
void resample(s_ImageData_R32F *outTexture, TIFF        *inTexture, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));

/// @}

/// @{
/** generate normals from texture:
 * Like resample() functions, it samples the inTexture within the homogeneous transform defined by in_coord0, in_coordU_incr, in_coordV_incr.
 * The normal is computed such as n = normalize(-factor * dz/du, factor * dz/dv, 1) where (du,dv) is in the outTexture uv-space unit.
 * In other words, "factor" is the dimenseion-less ratio of the elevation per pixel-unit over the map covering distance on the U "axis"
 * Warning about the pixel-unit: uint8_t values from the texture will be remapped to [0.f, 1f].
 * Note: those functions use the OpenGL coordinate-system ((0,0) is the Top-Left corner)
 */

void mapNormals(SDL_Surface *outTexture, SDL_Surface *inTexture, const float factor, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));
void mapNormals(SDL_Surface *outTexture, TIFF        *inTexture, const float factor, const glm::vec2 &in_coord0 = glm::vec2(0.f, 0.f), const glm::vec2 &in_coordU_incr = glm::vec2(1.f, 0.f), const glm::vec2 &in_coordV_incr = glm::vec2(0.f, 1.f));

/// @}

// ----------------------------------------------------------------------------

//

/// @{
/** resample with equirectangular sphere projection:
 * It samples the inTexture with a sub-equirectangular sphere projection such as the outTexture has on its corner
 * - in_coord0 for the Top-Left corner,
 * - in_coord0 + in_coordU_incr for the Top-Right corner,
 * - in_coord0 + in_coordV_incr for the Bottom-Left corner,
 * - in_coord0 + in_coordU_incr + in_coordV_incr for the Bottom-Right corner.
 * Note: those functions use the OpenGL cube-map coordinate-system (Left-Hand Y-Up).
 */

void resample_toCubeMap(SDL_Surface *outTexture     , SDL_Surface *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);
void resample_toCubeMap(SDL_Surface *outTexture     , TIFF        *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);
void resample_toCubeMap(s_ImageData_R32F *outTexture, SDL_Surface *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);
void resample_toCubeMap(s_ImageData_R32F *outTexture, TIFF        *inTextureEquirectangular, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);

/// @}

/// @{
/** generate normals from equirectangular sphere projected texture:
 * Like resample() functions, it samples the inTexture within the sub-equirectangular projection defined by in_coord0, in_coordU_incr, in_coordV_incr.
 * The normal is computed such as n = normalize(-factor * dz/dx, factor * dz/dy, 1) where (dx,dy) is the projected (du,dv) from the outTexture uv-space unit.
 * In other words, "factor" is the elevation per pixel-unit, relative to the radius. The input-texture is assumed to be a full equirectangular projection.
 * Warning about the pixel-unit: uint8_t values from the texture will be remapped to [0.f, 1f].
 * Note: those functions use the OpenGL cube-map coordinate-system (Left-Hand Y-Up).
 */

void mapNormals_toCubeMap(SDL_Surface *outTexture, SDL_Surface *inTextureEquirectangular, const float factor, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);
void mapNormals_toCubeMap(SDL_Surface *outTexture, TIFF        *inTextureEquirectangular, const float factor, const glm::vec3 &in_coord0, const glm::vec3 &in_coordU_incr, const glm::vec3 &in_coordV_incr);

/// @}

// ----------------------------------------------------------------------------

} // namespace textureSampler

} // namespace tre

#endif // TEXTURESAMPLER_H
