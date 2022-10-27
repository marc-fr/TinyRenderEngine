#include "tre_texture.h"

namespace tre {

static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN  , "Only implemented with little-endian system.");

//-----------------------------------------------------------------------------

static const unsigned k_Config = (SDL_BYTEORDER == SDL_LIL_ENDIAN ? 0x04 : 0x08) |
#ifdef TRE_OPENGL_ES
                                 0x01 |
#else
                                 0x02 |
#endif
                                 0;

//-----------------------------------------------------------------------------

///< Helper function to get the internal OpenGL-format
static GLenum getTexInternalFormat(uint nbComponants, bool isCompressed)
{
  TRE_ASSERT(nbComponants > 0 && nbComponants <= 4);
#ifdef TRE_OPENGL_ES
  static const GLenum formats[4] = { GL_R8, GL_RG8, GL_RGB8, GL_RGBA8 };
  static const GLenum formatsCompressed[4] = { GL_COMPRESSED_R11_EAC, GL_COMPRESSED_RG11_EAC, GL_COMPRESSED_RGB8_ETC2, GL_COMPRESSED_RGBA8_ETC2_EAC };
#else
  static const GLenum formats[4] = { GL_RED, GL_RG, GL_RGB, GL_RGBA };
  static const GLenum formatsCompressed[4] = { GL_COMPRESSED_RED_RGTC1, GL_COMPRESSED_RG_RGTC2, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT };
#endif
  return (isCompressed) ? formatsCompressed[nbComponants - 1] : formats[nbComponants - 1];;
}

//-----------------------------------------------------------------------------

///< Helper function to get the upload OpenGL-format of SDL textures
static GLenum getTexFormatSource(const SDL_Surface *surface)
{
  if (surface->format->BytesPerPixel == 4)
  {
    if ((surface->format->Rmask == 0x00FF0000) &&
        (surface->format->Gmask == 0x0000FF00) &&
        (surface->format->Bmask == 0x000000FF) &&
        (surface->format->Amask == 0xFF000000 || surface->format->Amask == 0))
      return GL_BGRA;
    else if ((surface->format->Rmask == 0x000000FF) &&
             (surface->format->Gmask == 0x0000FF00) &&
             (surface->format->Bmask == 0x00FF0000) &&
             (surface->format->Amask == 0xFF000000 || surface->format->Amask == 0))
      return GL_RGBA;
  }
  else if (surface->format->BytesPerPixel == 3)
  {
    if ((surface->format->Rmask == 0x00FF0000) &&
        (surface->format->Gmask == 0x0000FF00) &&
        (surface->format->Bmask == 0x000000FF))
      return GL_BGR;
    else if ((surface->format->Rmask == 0x000000FF) &&
             (surface->format->Gmask == 0x0000FF00) &&
             (surface->format->Bmask == 0x00FF0000))
      return GL_RGB;
  }
  TRE_FATAL("unrecognized surface format");
  return GL_INVALID_VALUE;
}

//-----------------------------------------------------------------------------

SDL_Surface* texture::loadTextureFromBMP(const std::string & filename)
{
  SDL_Surface* tex = SDL_LoadBMP(filename.c_str());
  if(tex)
  {
    TRE_LOG("Texture " << filename << " loaded : size = (" << tex->w << " x " << tex->h << ") and components = " << int(tex->format->BytesPerPixel) << ")");
  }
  else
  {
    TRE_LOG("Faild to load texture " << filename << ", SDLerror:" << std::endl << SDL_GetError());
  }
  return tex;
}

//-----------------------------------------------------------------------------

SDL_Surface* texture::loadTextureFromFile(const std::string &filename)
{
#ifdef TRE_WITH_SDL2_IMAGE
  SDL_Surface* tex = IMG_Load(filename.c_str());
  if(tex)
  {
    TRE_LOG("Texture " << filename << " loaded : size = (" << tex->w << " x " << tex->h << ") and components = " << int(tex->format->BytesPerPixel) << ")");
  }
  else
  {
    TRE_LOG("Faild to load texture " << filename << ", SDLerror:" << std::endl << SDL_GetError());
  }
  return tex;
#else
  TRE_LOG("Cannot load texture " << filename << ", SDL2_IMAGE not available");
  return nullptr;
#endif
}

//-----------------------------------------------------------------------------

SDL_Surface* texture::combine(const SDL_Surface* surfR, const SDL_Surface* surfG)
{
  TRE_ASSERT(surfR->w == surfG->w && surfR->h == surfG->h && surfR->format->BytesPerPixel == surfG->format->BytesPerPixel);
  TRE_ASSERT(surfR->format->BytesPerPixel == 3 || surfR->format->BytesPerPixel == 4);
  TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN); // 0xAARRGGBB
  TRE_ASSERT(surfR->pitch == surfR->format->BytesPerPixel * surfR->w);

  SDL_Surface *surfRG = SDL_CreateRGBSurface(0, surfR->w, surfR->h, 32, 0, 0, 0, 0);
  if (!surfRG) return nullptr;
  TRE_ASSERT(surfRG->format->BytesPerPixel == 4);

  if (surfR->format->BytesPerPixel == 4)
  {
    const int npixels = surfRG->w * surfRG->h;
    int * pxINR = static_cast<int*>(surfR->pixels);
    int * pxING = static_cast<int*>(surfG->pixels);
    int * pxOUT = static_cast<int*>(surfRG->pixels);
    for (int ip=0;ip<npixels;++ip)
    {
      pxOUT[ip] = (pxINR[ip] & 0x00FF0000) | (pxING[ip] & 0x0000FF00);
    }
  }
  else if (surfR->format->BytesPerPixel == 3)
  {
    const int npixels = surfRG->w * surfRG->h;
    char * pxINR = static_cast<char*>(surfR->pixels);
    char * pxING = static_cast<char*>(surfG->pixels);
    int * pxOUT = static_cast<int*>(surfRG->pixels);
    for (int ip=0;ip<npixels;++ip)
    {
      const int r = pxINR[ip * 3 + 2] & 0xFF;
      const int g = pxING[ip * 3 + 1] & 0xFF;
      pxOUT[ip] = (r << 16) | (g << 8);
    }
  }

  return surfRG;
}

//-----------------------------------------------------------------------------

bool texture::load(SDL_Surface *surface, int modemask, const bool freeSurface)
{
  if (surface == nullptr) return false;

  // init
  m_type = TI_2D;
  m_useMipmap           = (modemask & MMASK_MIPMAP     ) != 0;
  m_useCompress         = (modemask & MMASK_COMPRESS   ) != 0;
  m_useAnisotropic      = (modemask & MMASK_ANISOTROPIC) != 0;
  m_useMagFilterNearest = (modemask & MMASK_NEAREST_MAG_FILTER) != 0;
  TRE_ASSERT(!m_useAnisotropic || m_useMipmap); // Mipmap is need for Anisotropic
#ifdef TRE_OPENGL_ES
  if (m_useMipmap && m_useCompress)
  {
    TRE_LOG("OpenGL-ES: Cannot generate mipmaps of compressed texture. Skip compression."); // handle offline mipmaps generation ?
    m_useCompress = false;
  }
#endif

  // retrieve info from texture
  m_w = surface->w;
  m_h = surface->h;
  TRE_ASSERT(m_w >= 4 && (m_w & 0x03) == 0);
  TRE_ASSERT(m_h >= 4 && (m_h & 0x03) == 0);
  GLenum externalformat = getTexFormatSource(surface);
  m_components = surface->format->BytesPerPixel;
  if (modemask & MMASK_FORCE_NO_ALPHA) m_components = 3;
  if (modemask & MMASK_RG_ONLY) m_components = 2;
  if (modemask & MMASK_ALPHA_ONLY) m_components = 1;

  s_SurfaceTemp surfLocal = s_SurfaceTemp(surface);

  if (m_components == 1 && surface->format->BytesPerPixel != 1)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    // WebGL does not support texture swizzle.
#ifdef TRE_EMSCRIPTEN
    TRE_ASSERT(surfLocal.pxByteSize == 4);
    const int npixels = surfLocal.w * surfLocal.h;
    uint * pixels = reinterpret_cast<uint*>(surfLocal.pixels);
    for (uint ip=0;ip<npixels;++ip) pixels[ip] |= 0x00FFFFFF;
    m_components = 4;
#else
    _rawPack_A8(surfLocal);
    externalformat = GL_RED;
#endif
  }
  if (m_components == 2 && surface->format->BytesPerPixel != 2) // not need, but try to keep same code between the load and write
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawPack_RG8(surfLocal);
    externalformat = GL_RG;
  }
  if (m_components == 3  && surface->format->BytesPerPixel != 3) // not need, but try to keep same code between the load and write
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawPack_RemoveAlpha8(surfLocal);
    TRE_ASSERT(externalformat == GL_BGRA || externalformat == GL_RGBA);
    externalformat = (externalformat == GL_BGRA) ? GL_BGR : GL_RGB;
  }
  if (externalformat == GL_BGR || externalformat == GL_BGRA)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawConvert_BRG_to_RGB(surfLocal);
    externalformat = (externalformat == GL_BGR) ? GL_RGB : GL_RGBA;
  }

  const GLenum internalformat = getTexInternalFormat(m_components, m_useCompress);

   bool success = true;

  // load texture with OpenGL

  glGenTextures(1,&m_handle);
  glBindTexture(GL_TEXTURE_2D,m_handle);

  if (m_useCompress)
  {
#if defined(TRE_OPENGL_ES) || true // use the CPU-compressor
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    const uint  bufferByteSize = _rawCompress(surfLocal, internalformat); // in-place
    if (bufferByteSize == 0)
    {
      TRE_LOG("texture::load - failed to compress the picture (CPU compressor)");
      success = false;
    }
    else
    {
      glCompressedTexImage2D(GL_TEXTURE_2D, 0, internalformat, surfLocal.w, surfLocal.h, 0, bufferByteSize, surfLocal.pixels);
    }
#else
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, surfLocal.w, surfLocal.h, 0, externalformat, GL_UNSIGNED_BYTE, surfLocal.pixels);
#endif
  }
  else
  {
    glTexImage2D(GL_TEXTURE_2D, 0, internalformat, surfLocal.w, surfLocal.h, 0, externalformat, GL_UNSIGNED_BYTE, surfLocal.pixels);
  }

  success &= IsOpenGLok("texture::load - upload pixels");

  set_parameters();
  // check
#ifndef TRE_OPENGL_ES
  GLint isCompressed = GL_FALSE;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &isCompressed);
  TRE_ASSERT((isCompressed != GL_FALSE) == m_useCompress);
#endif
  success &= IsOpenGLok("texture::load - complete texture");
  // end
  if (freeSurface) SDL_FreeSurface(surface);
  glBindTexture(GL_TEXTURE_2D,0);
  return success;
}

//-----------------------------------------------------------------------------

bool texture::loadCube(const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface)
{
  const bool isValid = ((cubeFaces[0] != nullptr) & (cubeFaces[1] != nullptr) & (cubeFaces[1] != nullptr) &
                        (cubeFaces[3] != nullptr) & (cubeFaces[4] != nullptr) & (cubeFaces[5] != nullptr));

  if (!isValid)
  {
    if (freeSurface)
    {
      for (auto &s : cubeFaces) SDL_FreeSurface(s);
    }
    return false;
  }

  // init
  m_type = TI_CUBEMAP;
  m_useMipmap           = (modemask & MMASK_MIPMAP     ) != 0;
  m_useCompress         = (modemask & MMASK_COMPRESS   ) != 0;
  m_useAnisotropic      = (modemask & MMASK_ANISOTROPIC) != 0;
  m_useMagFilterNearest = (modemask & MMASK_NEAREST_MAG_FILTER) != 0;
  TRE_ASSERT((modemask & MMASK_ALPHA_ONLY) == 0); // alpha-only modifier not supported for cubemaps
  TRE_ASSERT((modemask & MMASK_FORCE_NO_ALPHA) == 0); // no-alpha modifier not supported for cubemaps
  TRE_ASSERT((modemask & MMASK_RG_ONLY) == 0); // 2-chanels modifier not supported for cubemaps
#ifdef TRE_OPENGL_ES
  if (m_useMipmap && m_useCompress)
  {
    TRE_LOG("OpenGL-ES: Cannot generate mipmaps of compressed texture. Skip compression."); // handle offline mipmaps generation ?
    m_useCompress = false;
  }
#endif

  // retrieve info from texture
  m_w = cubeFaces[0]->w;
  m_h = cubeFaces[0]->h;
  TRE_ASSERT(m_w >= 4 && (m_w & 0x03) == 0);
  TRE_ASSERT(m_h >= 4 && (m_h & 0x03) == 0);
  GLenum externalformat = getTexFormatSource(cubeFaces[0]);
  m_components = cubeFaces[0]->format->BytesPerPixel;
  if (modemask & MMASK_FORCE_NO_ALPHA) m_components = 3;
  if (modemask & MMASK_RG_ONLY) m_components = 2;
  if (modemask & MMASK_ALPHA_ONLY) m_components = 1;

  GLenum internalformat = getTexInternalFormat(m_components, m_useCompress);

  bool needConvertReverse = false;
  if (externalformat == GL_BGR || externalformat == GL_BGRA)
  {
    externalformat = (externalformat == GL_BGR) ? GL_RGB : GL_RGBA;
    needConvertReverse = true;
  }

  // loop on cube faces - load texture with OpenGL

  bool success = true;

  glGenTextures(1,&m_handle);
  glBindTexture(GL_TEXTURE_CUBE_MAP,m_handle);

  for (int iface = 0; iface < 6; ++iface)
  {
    SDL_Surface *surface = cubeFaces[iface];
    if (m_w != uint(surface->w) || m_h != uint(surface->h))
    {
      TRE_LOG("Mismatching-dimension for cubemap face " << iface);
      success = false;
      break;
    }
    TRE_ASSERT(surface->format->BytesPerPixel == cubeFaces[0]->format->BytesPerPixel);

    s_SurfaceTemp surfLocal = s_SurfaceTemp(surface);

    if (!freeSurface && (needConvertReverse || m_useCompress)) surfLocal.copyToOwnBuffer();

    if (needConvertReverse) _rawConvert_BRG_to_RGB(surfLocal);

    if (m_useCompress)
    {
#if defined(TRE_OPENGL_ES) || true // use the CPU-compressor
      const uint  bufferByteSize = _rawCompress(surfLocal, internalformat); // inplace compression
      if (bufferByteSize == 0)
      {
        TRE_LOG("texture::load - failed to compress the picture (CPU compressor)");
        glBindTexture(GL_TEXTURE_2D,0);
        clear();
        return false;
      }
      glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface, 0, internalformat, surfLocal.w, surfLocal.h, 0, bufferByteSize, surfLocal.pixels);
  #else
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,internalformat,surflocal.w,surflocal.h,0,externalformat,GL_UNSIGNED_BYTE,surflocal.pixels);
  #endif
    }
    else
    {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,internalformat,surfLocal.w,surfLocal.h,0,externalformat,GL_UNSIGNED_BYTE,surfLocal.pixels);
    }
    success &= IsOpenGLok("texture::loadCube - upload cube face");
  }
  // complete the texture
  set_parameters();
  success &= IsOpenGLok("texture::loadCube - complete texture");
  // end
  if (freeSurface)
  {
    for (auto &s : cubeFaces) SDL_FreeSurface(s);
  }
  glBindTexture(GL_TEXTURE_CUBE_MAP,0);
  return success;
}

//-----------------------------------------------------------------------------

bool texture::loadWhite()
{
  SDL_Surface *tmpSurface = SDL_CreateRGBSurface(0, 4, 4, 32, 0, 0, 0, 0);
  if (tmpSurface == nullptr) return false;

  uint *p0 = static_cast<uint*>(tmpSurface->pixels);
  for (uint ip = 0; ip < 16; ++ip) p0[ip] = 0xFFFFFFFF;

  return load(tmpSurface, 0, true);
}

//-----------------------------------------------------------------------------

bool texture::loadCheckerboard(uint width, uint height)
{
  SDL_Surface *tmpSurface = SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0); // reminder: on little-endian, the default 32bits format is BGRA (that is 0xAARRGGBB)
  if (tmpSurface == nullptr) return false;

  uint pWB[2] = { 0xFFFFFFFF, 0xFF000000 };

  uint *p0 = static_cast<uint*>(tmpSurface->pixels);
  for (uint jrow = 0; jrow < height; ++jrow)
  {
    for (uint icol = 0; icol < width; ++icol)
    {
      (*p0++) = pWB[(jrow + icol) & 0x01];
    }
  }

  return load(tmpSurface, MMASK_NEAREST_MAG_FILTER, true);
}

//-----------------------------------------------------------------------------

#define TEXTURE_BIN_VERSION 0x003

bool texture::write(std::ostream &outbuffer, SDL_Surface *surface, int modemask, const bool freeSurface)
{
  uint components = (surface != nullptr) ? surface->format->BytesPerPixel : 4u /*whatever*/;
  if (modemask & MMASK_FORCE_NO_ALPHA) components = 3;
  if (modemask & MMASK_RG_ONLY) components = 2;
  if (modemask & MMASK_ALPHA_ONLY) components = 1;
  GLenum       sourceformat = getTexFormatSource(surface);

  // header
  uint tinfo[8];
  tinfo[0] = (surface != nullptr) ? TI_2D : TI_NONE;
  tinfo[1] = (surface != nullptr) ? surface->w : 0u;
  tinfo[2] = (surface != nullptr) ? surface->h : 0u;
  tinfo[3] = (modemask & (MMASK_MIPMAP | MMASK_COMPRESS | MMASK_ANISOTROPIC | MMASK_NEAREST_MAG_FILTER));
  tinfo[4] = k_Config; // internal
  tinfo[5] = 0; // unused
  tinfo[6] = components;
  tinfo[7] = TEXTURE_BIN_VERSION;

  outbuffer.write(reinterpret_cast<const char*>(&tinfo), sizeof(tinfo));

  if (surface == nullptr) return false;

  s_SurfaceTemp surfLocal = s_SurfaceTemp(surface);

  // transform data
  if (components == 1 && surface->format->BytesPerPixel != 1)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawPack_A8(surfLocal);
    sourceformat = GL_RED;
  }
  if (components == 2 && surface->format->BytesPerPixel != 2)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawPack_RG8(surfLocal);
    sourceformat = GL_RG;
  }
  if (components == 3  && surface->format->BytesPerPixel != 3)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawPack_RemoveAlpha8(surfLocal);
    TRE_ASSERT(sourceformat == GL_BGRA || sourceformat == GL_RGBA);
    sourceformat = (sourceformat == GL_BGRA) ? GL_BGR : GL_RGB;
  }
  if (sourceformat == GL_BGR || sourceformat == GL_BGRA)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    _rawConvert_BRG_to_RGB(surfLocal);
    sourceformat = (sourceformat == GL_BGR) ? GL_RGB : GL_RGBA;
  }

  uint pixelData_ByteSize = components * surface->w * surface->h;

  if ((modemask & MMASK_COMPRESS) != 0)
  {
    if (!freeSurface) surfLocal.copyToOwnBuffer();
    pixelData_ByteSize = _rawCompress(surfLocal, getTexInternalFormat(components, true));
  }

  // final write

  outbuffer.write(reinterpret_cast<const char*>(&pixelData_ByteSize), sizeof(pixelData_ByteSize));
  outbuffer.write(reinterpret_cast<const char*>(surfLocal.pixels), pixelData_ByteSize);

  if (freeSurface) SDL_FreeSurface(surface);

  return true;
}

// ----------------------------------------------------------------------------

bool texture::writeCube(std::ostream &outbuffer, const std::array<SDL_Surface *, 6> &cubeFaces, int modemask, const bool freeSurface)
{
  const bool isValid = ((cubeFaces[0] != nullptr) & (cubeFaces[1] != nullptr) & (cubeFaces[1] != nullptr) &
                        (cubeFaces[3] != nullptr) & (cubeFaces[4] != nullptr) & (cubeFaces[5] != nullptr));

  uint components = isValid ? cubeFaces[0]->format->BytesPerPixel : 4u /*whatever*/;
  TRE_ASSERT((modemask & MMASK_ALPHA_ONLY) == 0); // alpha-only modifier not supported for cubemaps
  TRE_ASSERT((modemask & MMASK_FORCE_NO_ALPHA) == 0); // no-alpha modifier not supported for cubemaps
  TRE_ASSERT((modemask & MMASK_RG_ONLY) == 0); // 2-chanels modifier not supported for cubemaps

  // header
  uint tinfo[8];
  tinfo[0] = isValid ? TI_CUBEMAP : TI_NONE;
  tinfo[1] = isValid ? cubeFaces[0]->w : 4u;
  tinfo[2] = isValid ? cubeFaces[0]->h : 4u;
  tinfo[3] = (modemask & (MMASK_MIPMAP | MMASK_COMPRESS | MMASK_ANISOTROPIC | MMASK_NEAREST_MAG_FILTER));
  tinfo[4] = k_Config; // internal
  tinfo[5] = 0; // unused
  tinfo[6] = components;
  tinfo[7] = TEXTURE_BIN_VERSION;

  outbuffer.write(reinterpret_cast<const char*>(&tinfo), sizeof(tinfo));

  if (!isValid)
  {
    if (freeSurface)
    {
      for (auto &s : cubeFaces) SDL_FreeSurface(s);
    }
    return false;
  }

  uint pixelData_ByteSize = components * cubeFaces[0]->w * cubeFaces[0]->h;

  if ((modemask & MMASK_COMPRESS) != 0)
  {
    TRE_ASSERT(freeSurface == true); // local-surface not implemented.
    const GLenum internalformat = getTexInternalFormat(components, true);
    pixelData_ByteSize = _rawCompress(cubeFaces[0], internalformat);
    const uint pxbt1   = _rawCompress(cubeFaces[1], internalformat); TRE_ASSERT(pixelData_ByteSize == pxbt1); (void)pxbt1;
    const uint pxbt2   = _rawCompress(cubeFaces[2], internalformat); TRE_ASSERT(pixelData_ByteSize == pxbt2); (void)pxbt2;
    const uint pxbt3   = _rawCompress(cubeFaces[3], internalformat); TRE_ASSERT(pixelData_ByteSize == pxbt3); (void)pxbt3;
    const uint pxbt4   = _rawCompress(cubeFaces[4], internalformat); TRE_ASSERT(pixelData_ByteSize == pxbt4); (void)pxbt4;
    const uint pxbt5   = _rawCompress(cubeFaces[5], internalformat); TRE_ASSERT(pixelData_ByteSize == pxbt5); (void)pxbt5;
  }

  outbuffer.write(reinterpret_cast<const char*>(&pixelData_ByteSize), sizeof(pixelData_ByteSize));
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[0]->pixels), pixelData_ByteSize);
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[1]->pixels), pixelData_ByteSize);
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[2]->pixels), pixelData_ByteSize);
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[3]->pixels), pixelData_ByteSize);
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[4]->pixels), pixelData_ByteSize);
  outbuffer.write(reinterpret_cast<const char*>(cubeFaces[5]->pixels), pixelData_ByteSize);

  if (freeSurface)
  {
    for (auto &s : cubeFaces) SDL_FreeSurface(s);
  }

  return true;
}

//-----------------------------------------------------------------------------

bool texture::read(std::istream &inbuffer)
{
  // header
  uint tinfo[8];
  inbuffer.read(reinterpret_cast<char*>(&tinfo), sizeof(tinfo) );

  if      (tinfo[0]==TI_2D     ) m_type = TI_2D;
  else if (tinfo[0]==TI_CUBEMAP) m_type = TI_CUBEMAP;
  else
  {
    TRE_LOG("texture::read invalid texture type (reading " << tinfo[0] << ")");
    return false;
  }
  m_w = tinfo[1];
  m_h = tinfo[2];
  m_useMipmap           = 0 != (tinfo[3] & MMASK_MIPMAP);
  m_useCompress         = 0 != (tinfo[3] & MMASK_COMPRESS);
  m_useAnisotropic      = 0 != (tinfo[3] & MMASK_ANISOTROPIC);
  m_useMagFilterNearest = 0 != (tinfo[3] & MMASK_NEAREST_MAG_FILTER);
  m_components = tinfo[6];
  // validation
  if (tinfo[7] != TEXTURE_BIN_VERSION)
  {
    TRE_LOG("texture::read invalid texture version (expecting " << TEXTURE_BIN_VERSION << " but reading " << tinfo[7] << ")");
    return false;
  }
  if (tinfo[4] != k_Config)
  {
    TRE_LOG("texture::read invalid config used (expecting " << k_Config << " but reading " << tinfo[4] << ")");
    return false;
  }

#ifdef TRE_OPENGL_ES
  if (m_useMipmap && m_useCompress)
  {
    TRE_LOG("texture: OpenGL-ES: Cannot generate mipmaps of compressed texture. Skip mipmap.");
    m_useMipmap = false;
  }
#endif

  if (!m_useMipmap && m_useAnisotropic)
  {
    TRE_LOG("texture: Cannot use anisotropic filter with usemap. Disable anisotropic filter");
    m_useAnisotropic = false;
  }

  // WebGL does not support texture swizzle -> expand to RGBA with RGB=white
#ifdef TRE_EMSCRIPTEN
  const bool doConvertAtoRGBA = (m_components == 1);
  if (doConvertAtoRGBA)
  {
    TRE_ASSERT(!m_useCompress); // not implemented.
    m_components = 4;
  }
#else
  const bool doConvertAtoRGBA = false;
#endif

  bool success = true;

  // data and load into GPU
  const GLenum internalformat = getTexInternalFormat(m_components, m_useCompress);
#ifdef TRE_OPENGL_ES
  static const GLenum _formats[4] = { GL_RED, GL_RG, GL_RGB, GL_RGBA };
  const GLenum format = _formats[m_components - 1];
#else
  const GLenum format = internalformat;
#endif

  std::vector<char> readBuffer;
  uint dataSize = 0;
  inbuffer.read(reinterpret_cast<char*>(&dataSize), sizeof(uint));
  TRE_ASSERT(int(dataSize) > 0);

  glGenTextures(1,&m_handle);
  if (m_type==TI_2D)
  {
    readBuffer.resize(dataSize);
    TRE_ASSERT(readBuffer.size() == dataSize);
    inbuffer.read(readBuffer.data(), int(dataSize));
    if (doConvertAtoRGBA) _rawUnpack_A8_to_RGBA8(readBuffer);
    glBindTexture(GL_TEXTURE_2D,m_handle);
    if (m_useCompress) glCompressedTexImage2D(GL_TEXTURE_2D,0,internalformat,m_w,m_h,0,readBuffer.size(),readBuffer.data());
    else               glTexImage2D(GL_TEXTURE_2D,0,internalformat,m_w,m_h,0,format,GL_UNSIGNED_BYTE,readBuffer.data());
    success &= tre::IsOpenGLok("texture::read - upload pixels");
    set_parameters();
    success &= tre::IsOpenGLok("texture::read - complete texture");
    glBindTexture(GL_TEXTURE_2D,0);
  }
  else if (m_type==TI_CUBEMAP)
  {
    glBindTexture(GL_TEXTURE_CUBE_MAP,m_handle);
    for (uint iface = 0; iface < 6; ++iface)
    {
      readBuffer.resize(dataSize);
      TRE_ASSERT(readBuffer.size() == dataSize);
      inbuffer.read(readBuffer.data(), int(dataSize));
      if (doConvertAtoRGBA) _rawUnpack_A8_to_RGBA8(readBuffer);
      if (m_useCompress) glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,internalformat,m_w,m_h,0,readBuffer.size(),readBuffer.data());
      else               glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,internalformat,m_w,m_h,0,format,GL_UNSIGNED_BYTE,readBuffer.data());
      success &= tre::IsOpenGLok("texture::read - upload cube face pixels");
    }
    set_parameters();
    success &= tre::IsOpenGLok("texture::read - complete cube texture");
    glBindTexture(GL_TEXTURE_CUBE_MAP,0);
  }

  return success;
}

#undef TEXTURE_BIN_VERSION

//-----------------------------------------------------------------------------

void texture::clear()
{
  if (m_handle!=0) glDeleteTextures(1,&m_handle);
  m_handle = 0;
  m_useCompress = m_useMipmap = m_useAnisotropic = m_useMagFilterNearest = false;
  m_type = TI_NONE;
  m_w = m_h = 0;
}

//-----------------------------------------------------------------------------

void texture::set_parameters()
{
  TRE_ASSERT(m_type==TI_2D || m_type==TI_CUBEMAP);

  const GLenum target = (m_type==TI_2D) ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;

  if (m_useMipmap) glGenerateMipmap(target);
  glTexParameteri(target,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(target,GL_TEXTURE_WRAP_T,GL_REPEAT);
  if (m_useMagFilterNearest) glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  else                       glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  if (m_useMipmap) glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  else             glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  if (m_components==1)
  {
#ifdef TRE_OPENGL_ES
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_R, GL_ONE);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_G, GL_ONE);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_B, GL_ONE);
    glTexParameteri(target, GL_TEXTURE_SWIZZLE_A, GL_RED);
#else
    GLint swizzleMask[] = { GL_ONE, GL_ONE, GL_ONE, GL_RED };
    glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
#endif
  }
  if (m_useAnisotropic==true)
  {
    if (GL_EXT_texture_filter_anisotropic)
    {
      GLfloat amountanisotropic(0.f);
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&amountanisotropic);
      TRE_LOG("Anisotropic filter is supported ! with amount = " << amountanisotropic);
      if (amountanisotropic>4.f) amountanisotropic=4.f;
      glTexParameterf(target,GL_TEXTURE_MAX_ANISOTROPY_EXT,amountanisotropic);
    }
    else
    {
      TRE_LOG("Anisotropic filter is not supported !");
    }
  }
}

//-----------------------------------------------------------------------------

void texture::_rawConvert_BRG_to_RGB(const s_SurfaceTemp &surf)
{
  if (surf.pxByteSize == 3)
  {
    for (uint j = 0; j < surf.h; ++j)
    {
      uint8_t* pixelsRow = surf.pixels + surf.pitch * j;
      for (uint i = 0; i < surf.w; ++i)
      {
        std::swap(pixelsRow[0], pixelsRow[2]);
        pixelsRow += 3;
      }
    }
  }
  else if (surf.pxByteSize == 4)
  {
    TRE_ASSERT(surf.pitch == surf.pxByteSize * surf.w);
    const uint npixels = surf.w * surf.h;
    uint * pixels = reinterpret_cast<uint*>(surf.pixels);
    for (uint ip=0;ip<npixels;++ip) pixels[ip] = (pixels[ip] & 0xFF00FF00) | ((pixels[ip] & 0x00FF0000) >> 16) | ((pixels[ip] & 0x000000FF) << 16);
  }
  else
  {
    TRE_FATAL("texture::_rawConvert_BRG_to_RGB: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

void texture::_rawPack_A8(s_SurfaceTemp &surf)
{
  if (surf.pxByteSize == 4)
  {
    TRE_ASSERT(surf.pitch == surf.pxByteSize * surf.w);
    const uint npixels = surf.w * surf.h;
    uint * pixelsIn = reinterpret_cast<uint*>(surf.pixels);
    uint8_t * pixelsOut = surf.pixels;

    for (uint ip=0;ip<npixels;++ip) pixelsOut[ip] = (pixelsIn[ip] >> 24) & 0xFF;

    surf.pxByteSize = 1;
    surf.pitch = surf.w;
  }
  else if (surf.pxByteSize >= 2)
  {
    uint8_t * pixelsIn = surf.pixels;
    uint8_t * pixelsOut = surf.pixels;
    for (uint j = 0; j < surf.h; ++j)
    {
      uint8_t* pixelsInRow = pixelsIn + surf.pitch * j;
      for (uint i = 0; i < surf.w; ++i)
      {
        pixelsOut[0] = pixelsInRow[0] & 0xFF;
        pixelsInRow += surf.pxByteSize;
        pixelsOut++;
      }
    }

    surf.pxByteSize = 1;
    surf.pitch = surf.w;
  }
  else if (surf.pxByteSize != 1)
  {
    TRE_FATAL("texture::_rawPack_to_A8: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

void texture::_rawPack_RG8(s_SurfaceTemp &surf)
{
  if (surf.pxByteSize == 4)
  {
    TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN); // 0xAARRGGBB
    TRE_ASSERT(surf.pitch == surf.pxByteSize * surf.w);
    const uint npixels = surf.w * surf.h;
    uint * pixelsIn = reinterpret_cast<uint*>(surf.pixels);
    uint8_t * pixelsOut = surf.pixels;

    for (uint ip=0;ip<npixels;++ip)
    {
      pixelsOut[ip*2+0] = (pixelsIn[ip] >> 16) & 0xFF;
      pixelsOut[ip*2+1] = (pixelsIn[ip] >>  8) & 0xFF;
    }

    surf.pxByteSize = 2;
    surf.pitch = 2 * surf.w;
  }
  else if (surf.pxByteSize == 3)
  {
    uint8_t * pixelsIn = surf.pixels;
    uint8_t * pixelsOut = surf.pixels;
    for (uint j = 0; j < surf.h; ++j)
    {
      uint8_t* pixelsInRow = pixelsIn + surf.pitch * j;
      for (uint i = 0; i < surf.w; ++i)
      {
        pixelsOut[0] = pixelsInRow[0] & 0xFF;
        pixelsOut[1] = pixelsInRow[1] & 0xFF;
        pixelsInRow += 3;
        pixelsOut += 2;
      }
    }

    surf.pxByteSize = 2;
    surf.pitch = 2 * surf.w;
  }
  else if (surf.pxByteSize != 2)
  {
    TRE_FATAL("texture::_rawPack_to_RG8: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

void texture::_rawPack_RemoveAlpha8(s_SurfaceTemp &surf)
{
  TRE_ASSERT(surf.pxByteSize == 4);
  TRE_ASSERT(surf.pitch == surf.pxByteSize * surf.w);

  const uint npixels = surf.w * surf.h;
  const uint8_t * pixelsIn = surf.pixels;
  uint8_t * pixelsOut = surf.pixels;

  for (uint ip=0;ip<npixels;++ip)
  {
    pixelsOut[0] = pixelsIn[0] & 0xFF;
    pixelsOut[1] = pixelsIn[1] & 0xFF;
    pixelsOut[2] = pixelsIn[2] & 0xFF;
    pixelsOut += 3;
    pixelsIn += 4;
  }

  surf.pxByteSize = 3;
  surf.pitch = 3 * surf.w;
}

//-----------------------------------------------------------------------------

void texture::_rawUnpack_A8_to_RGBA8(std::vector<char> &pixelData)
{
  const std::size_t npixels = pixelData.size();
  pixelData.resize(npixels * 4);
  for (std::size_t ip = npixels; ip-- != 0; )
  {
    pixelData[4 * ip + 0] = 0xFF;
    pixelData[4 * ip + 1] = 0xFF;
    pixelData[4 * ip + 2] = 0xFF;
    pixelData[4 * ip + 3] = pixelData[ip];
  }
}

//-----------------------------------------------------------------------------

texture::s_SurfaceTemp::s_SurfaceTemp(SDL_Surface *surf)
: w(surf->w), h(surf->h),
  pitch(surf->pitch), pxByteSize(surf->format->BytesPerPixel),
  pixels(static_cast<uint8_t*>(surf->pixels))
{
}

//-----------------------------------------------------------------------------

void texture::s_SurfaceTemp::copyToOwnBuffer()
{
  TRE_ASSERT(pixels != nullptr);
  if (!pixelsLocalBuffer.empty()) return; // already done

  pixelsLocalBuffer.resize(pitch * h);
  memcpy(pixelsLocalBuffer.data(), pixels, pitch * h);
  pixels = pixelsLocalBuffer.data();
}

//-----------------------------------------------------------------------------

} // namespace
