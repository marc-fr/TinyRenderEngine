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

  enum textureInfoType { TI_NONE, TI_2D, TI_2DARRAY, TI_CUBEMAP };

  static const int MMASK_MIPMAP             = 0x01;
  static const int MMASK_COMPRESS           = 0x02;
  static const int MMASK_ANISOTROPIC        = 0x04;
  static const int MMASK_ALPHA_ONLY         = 0x10;
  static const int MMASK_RG_ONLY            = 0x20;
  static const int MMASK_FORCE_NO_ALPHA     = 0x40;
  static const int MMASK_NEAREST_MAG_FILTER = 0x80; ///< By default, the mag-filter is "linear".

  texture() {}
  texture(const texture &) = delete;
  texture(texture &&other) { *this = std::move(other); }
  ~texture() { TRE_ASSERT(m_handle == 0); }

private:
  texture & operator =(const texture &) = default;
public:
  texture & operator =(texture &&other)
  {
    if (this != &other)
    {
      *this = other;
      other.m_handle = 0;
    }
    return *this;
  }

  static SDL_Surface *loadTextureFromBMP(const std::string & filename); ///< Load from a BMP file and create an SDL_Surface. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).
  static SDL_Surface *loadTextureFromFile(const std::string & filename); ///< Load from a file (any file format supported by SDL_Image) and create an SDL_Surface. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).

  static SDL_Surface *combine(const SDL_Surface *surfaceR, const SDL_Surface *surfaceG); ///< Combine surfaceR and surfaceG into a single surface with 2 channels. Returns nullptr on failure. The caller becomes the owner of the SDL_Surface (and is responsible to free it).

  bool load(SDL_Surface *surface, int modemask, const bool freeSurface); ///< Load from SDL_Surface into GPU as 2D-Texture. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  bool loadCube(const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface);  ///< Load from SDL_Surface into GPU as CubeMap-Texture. Textures lost: X+, X-, Y+, Y-, Z+, Z-. Using freeSurface=true allows to apply modifiers in-place to the pixel data.

  bool loadWhite(); ///< Load a plain-white texture into GPU as 2D-texture.
  bool loadCheckerboard(uint width, uint height); ///< Load a texture with checker-board pattern into GPU as 2D-texture.

  static bool write(std::ostream &outbuffer, SDL_Surface *surface, int modemask, const bool freeSurface); ///< Bake and write a surface into binary-format. Using freeSurface=true allows to apply modifiers in-place to the pixel data.
  static bool writeCube(std::ostream &outbuffer, const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface); ///< Bake and write a cubemap-surface into binary-format. Using freeSurface=true allows to apply modifiers in-place to the pixel data.

  bool read(std::istream &inbuffer); ///< load texture from binary-file, and load it into GPU.

  void clear(); ///< Clear texture from GPU

  uint    m_w = 0;      ///< Width
  uint    m_h = 0;      ///< Height
  GLuint  m_handle = 0; ///< OpenGL handle

protected:

  void set_parameters(); ///< Once the texture is created and binded, it sets the OpenGL parameters to the texture.

  uint m_components = 0; ///< Nbr of components, between 1 and 4 included. "1" is specific: it means alpha-only, so the shader will resolve the color with (r=1,b=1,g=1,a=value).
  textureInfoType m_type = TI_NONE;
  bool m_useMipmap = false;
  bool m_useCompress = false;
  bool m_useAnisotropic = false;
  bool m_useMagFilterNearest = false;


private:

  struct s_SurfaceTemp
  {
     uint     w, h, pitch, pxByteSize;
     uint8_t  *pixels;

     std::vector<uint8_t> pixelsLocalBuffer;

     s_SurfaceTemp() : pixels(nullptr) {}
     s_SurfaceTemp(SDL_Surface *surf);

     void copyToOwnBuffer();
  };

  static void _rawConvert_BRG_to_RGB(const s_SurfaceTemp &surf);
  static void _rawPack_A8(s_SurfaceTemp &surf);
  static void _rawPack_RG8(s_SurfaceTemp &surf);
  static void _rawPack_RemoveAlpha8(s_SurfaceTemp &surf);
  static void _rawUnpack_A8_to_RGBA8(std::vector<char> &pixelData);
  static uint _rawCompress(const s_SurfaceTemp &surf, GLenum targetFormat); ///< compress textures on CPU (inplace, erase the surface's pixels). Returns the buffer byte-size, or zero on failure.
};

} // namespace

#endif // TEXTURE_H
