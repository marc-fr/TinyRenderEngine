#ifndef TEXTURE_H
#define TEXTURE_H

#include "tre_utils.h"

#include <array>
#include <vector>
#include <string>
#include <iostream>

namespace tre {

/**
 * @brief The texture class
 * It is the base class for textures handling from OpenGL.
 * It also implement the saving/loading of extures onto ".dat" binary-files.
 */
class texture
{
public:

  enum textureInfoType { TI_NONE, TI_2D, TI_2DARRAY, TI_CUBEMAP, TI_3D };

  static const int MMASK_MIPMAP             = 0x0001;
  static const int MMASK_COMPRESS           = 0x0002;
  static const int MMASK_ANISOTROPIC        = 0x0004;
  static const int MMASK_SRBG_SPACE         = 0x0008; ///< The image data is in the sRGB color-space. (Enables auto-convertion to the linear-sapce when sampling it.)
  static const int MMASK_NEAREST_MAG_FILTER = 0x0010; ///< By default, the mag-filter is "linear".
  static const int MMASK_ALPHA_ONLY         = 0x1000;
  static const int MMASK_RG_ONLY            = 0x2000;
  static const int MMASK_FORCE_NO_ALPHA     = 0x4000;

  texture() {}
  texture(const texture &) = delete;
  ~texture() { TRE_ASSERT(m_handle == 0); }

  texture & operator =(const texture &) = delete;

  static SDL_Surface *loadTextureFromBMP(const std::string & filename); ///< Load from a BMP file and create an SDL_Surface. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).
  static SDL_Surface *loadTextureFromFile(const std::string & filename); ///< Load from a file (any file format supported by SDL_Image) and create an SDL_Surface. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).

  static SDL_Surface *combine(const SDL_Surface *surfaceR, const SDL_Surface *surfaceG); ///< Combine surfaceR and surfaceG into a single surface with 2 channels. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).

  bool load(SDL_Surface *surface, int modemask, const bool freeSurface); ///< Load from SDL_Surface into GPU as 2D-Texture. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool loadArray(const span<SDL_Surface*> &surfaces, int modemask, const bool freeSurface); ///< Load from multiple SDL_Surface into GPU as 2D-Array-Texture. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool loadCube(const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface);  ///< Load from SDL_Surface into GPU as CubeMap-Texture. Textures lost: X+, X-, Y+, Y-, Z+, Z-. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool load3D(const uint8_t *data, int w, int h, int d, bool formatFloat, uint components, int modemask); ///< Load from SDL_Surface into GPU as 3D-Texture. Using modifiers is not allowed. data can be null.

  bool update(SDL_Surface *surface, const bool freeSurface, const bool unbind = true); ///< Upload new pixels into a 2D-texture. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool updateArray(SDL_Surface *surface, int depthIndex, const bool freeSurface, const bool unbind = true); ///< Upload new pixels into a 2D-Array-Texture. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool update3D(const uint8_t *data, int w, int h, int d, bool formatFloat, uint components, const bool unbind = true); ///< Upload new pixels into a 3D-Texture. Using modifiers is not allowed.

  bool loadColor(const uint32_t cARGB); ///< Load a plain-colored texture into GPU as 2D-Texture.
  bool loadWhite() { return loadColor(0xFFFFFFFF); } ///< Load a plain-white texture into GPU as 2D-Texture.
  bool loadCheckerboard(uint width, uint height); ///< Load a texture with checker-board pattern into GPU as 2D-Texture.

  static bool write(std::ostream &outbuffer, SDL_Surface *surface, int modemask, const bool freeSurface); ///< Bake and write a surface into binary-format. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  static bool writeArray(std::ostream &outbuffer, const span<SDL_Surface*> &surfaces, int modemask, const bool freeSurface);
  static bool writeCube(std::ostream &outbuffer, const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface); ///< Bake and write a cubemap-surface into binary-format. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  static bool write3D(std::ostream &outbuffer, const uint8_t *data, int w, int h, int d, bool formatFloat, uint components, int modemask); ///< Bake and write a 3D-Texture into binary-format. Using modifiers is not allowed.

  bool read(std::istream &inbuffer); ///< load texture from binary-file, and load it into GPU.

  void clear(); ///< Clear texture from GPU

  int    m_w = 0;      ///< Width
  int    m_h = 0;      ///< Height
  int    m_d = 0;      ///< Depth (for 2D-Array or 3D textures)
  GLuint m_handle = 0; ///< OpenGL handle

protected:

  void set_parameters(); ///< Once the texture is created and binded, it sets the OpenGL parameters to the texture.

  uint m_components = 0; ///< Nbr of components, between 1 and 4 included. "1" is specific: it means alpha-only, so the shader will resolve the color with (r=1,b=1,g=1,a=value).
  textureInfoType m_type = TI_NONE;
  uint m_mask = 0;

  bool useMipmap()           const { return (m_mask & MMASK_MIPMAP) != 0; }
  bool useCompress()         const { return (m_mask & MMASK_COMPRESS) != 0; }
  bool useAnisotropic()      const { return (m_mask & MMASK_ANISOTROPIC) != 0; }
  bool useGammeCorreciton()  const { return (m_mask & MMASK_SRBG_SPACE) != 0; }
  bool useMagFilterNearest() const { return (m_mask & MMASK_NEAREST_MAG_FILTER) != 0; }

private:

  struct s_SurfaceTemp
  {
     uint     w, h, pitch, pxByteSize;
     uint8_t  *pixels;

     std::vector<uint8_t> pixelsLocalBuffer;

     s_SurfaceTemp() : w(0), h(0), pitch(0), pxByteSize(0), pixels(nullptr) {}
     s_SurfaceTemp(SDL_Surface *surf);

     void copyToOwnBuffer();
  };

  static void _rawConvert_BRG_to_RGB(const s_SurfaceTemp &surf);
  static void _rawPack_A8(s_SurfaceTemp &surf);
  static void _rawPack_RG8(s_SurfaceTemp &surf);
  static void _rawPack_RemoveAlpha8(s_SurfaceTemp &surf);
  static void _rawUnpack_A8_to_RGBA8(std::vector<char> &pixelData);
  static void _rawExtend_AddAlpha8(s_SurfaceTemp &surf);
  static uint _rawCompress(const s_SurfaceTemp &surf, GLenum targetFormat); ///< compress textures on CPU (inplace, erase the surface's pixels). Returns the buffer byte-size, or zero on failure.
};

} // namespace

#endif // TEXTURE_H
