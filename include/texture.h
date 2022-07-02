#ifndef TEXTURE_H
#define TEXTURE_H

#include "openglinclude.h"

#include "utils.h"

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

  enum textureInfoType { TI_2D, TI_2DARRAY, TI_CUBEMAP };

  static const int MMASK_MIPMAP         = 0x01;
  static const int MMASK_COMPRESS       = 0x02;
  static const int MMASK_ANISOTROPIC    = 0x04;
  static const int MMASK_ALPHA_ONLY     = 0x10;
  static const int MMASK_RG_ONLY        = 0x20;
  static const int MMASK_FORCE_NO_ALPHA = 0x40;

  texture(const std::string & pname = "empty", textureInfoType ptype = TI_2D) : m_name(pname),m_type(ptype) {}
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

  bool loadNewTextureFromBMP(const std::string & filename, int modemask = 0x00); ///< Load from a BMP file and create a OpenGL 2D-texture
  bool loadNewTxComb2FromBMP(const std::string & filenameR, const std::string & filenameG, int modemask = 0x00); ///< Load from BMP files and create a OpenGL 2D-texture, by combining 2 textures (red-filenameR->red, green-filenameG->green)

  bool loadNewTextureFromFile(const std::string & filename, int modemask = 0x00); ///< Load from a file (any file format supported by SDL_Image) and create a OpenGL 2D-texture
  bool loadNewTxComb2FromFile(const std::string & filenameR, const std::string & filenameG, int modemask = 0x00); ///< Load from files  (any file format supported by SDL_Image) and create a OpenGL 2D-texture, by combining 2 textures (red-filenameR->red, green-filenameG->green)

  bool loadNewCubeTexFromBMP(const std::string & filebasename, int modemask = 0x00); ///< Load from BMP files (with suffix .xpos.bmp, .xneg.bmp, ...) and create a OpenGL CubeMap-texture

  bool loadNewTextureFromSDLSurface(SDL_Surface *surface, const std::string &name, int modemask = 0x00); ///< Load from SDL_Surface and create a OpenGL 2D-texture

  bool loadNewTextureWhite(const std::string &name = "white-4x4");
  bool loadNewTextureCheckerboard(uint width, uint height, const std::string &name = "checkerboard");

  void read(std::istream & inbuffer); ///< load texture from .dat file and load it into GPU memory
  void write(std::ostream & outbuffer) const; ///< dump texture into .dat file
  void clear(); ///< Clear texture from RAM and GPU

  uint    m_w = 0;      ///< Width
  uint    m_h = 0;      ///< Height
  GLuint  m_handle = 0; ///< OpenGL handle

protected:

  void set_parameters(); ///< Once the texture is created and binded, it sets the OpenGL parameters to the texture.

  std::string m_name;
  bool m_useMipmap = false;
  bool m_useCompress = false;
  bool m_useAnisotropic = false;
  uint m_components = 0; ///< Nbr of components, between 1 and 4 included. "1" is specific: it means alpha-only, so the shader will resolve the color with (r=1,b=1,g=1,a=value).
  textureInfoType m_type;

private:
  static void _rawCombine_R_G(const SDL_Surface *surfR, const SDL_Surface *surfG, SDL_Surface *surfRG);
  static void _rawConvert_BRG_to_RGB(SDL_Surface *surface);
  static void _rawPack_A8(SDL_Surface *surface);
  static void _rawPack_RG8(SDL_Surface *surface);
  static void _rawCompress(SDL_Surface *surface, GLenum targetFormat, uint8_t *outBuffer); ///< compress textures on CPU (needed for OpenGL-ES that cannot compress textures on GPU)
};

} // namespace

#endif // TEXTURE_H
