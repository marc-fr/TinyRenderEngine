#include "texture.h"

namespace tre {

static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN  , "Only implemented with little-endian system.");

///< Helper function for the format storage in .dat file
static GLenum getTexFormatStorage(uint nbComponants, bool isCompressed)
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

///< Helper function for the format of SDL textures
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

#define TEXTURE_BIN_VERSION 0x002

void texture::read(std::istream &inbuffer)
{
  // header
  uint tinfo[8];
  inbuffer.read(reinterpret_cast<char*>(&tinfo), sizeof(tinfo) );

  bool readtypeok = true;
  if      (tinfo[0]==TI_2D     ) m_type = TI_2D;
  else if (tinfo[0]==TI_CUBEMAP) m_type = TI_CUBEMAP;
  else                                     readtypeok = false;
  const uint namesize = tinfo[1];
  m_w = tinfo[2];
  m_h = tinfo[3];
  m_useMipmap      = 0 != (tinfo[4] & MMASK_MIPMAP);
  m_useCompress    = 0 != (tinfo[4] & MMASK_COMPRESS);
  m_useAnisotropic = 0 != (tinfo[4] & MMASK_ANISOTROPIC);
  m_components = tinfo[6];
  // validation
  TRE_ASSERT(tinfo[7] == TEXTURE_BIN_VERSION);
  // texname
  if (namesize>0)
  {
    char * tmpname = new char[namesize+1];
    inbuffer.read(tmpname, sizeof(char)*namesize);
    tmpname[namesize] = 0;
    m_name = std::string(tmpname);
    delete[] tmpname;
  }
  // empty ?
  if (m_w * m_h == 0)
    return;
  // data and load into GPU
  const GLenum storedformat = getTexFormatStorage(m_components,m_useCompress);
  std::vector<char> readBuffer;
  glGenTextures(1,&m_handle);
  if (m_type==TI_2D)
  {
    uint dataSize = 0;
    inbuffer.read(reinterpret_cast<char*>(&dataSize), sizeof(uint));
    TRE_ASSERT(int(dataSize) > 0);
    readBuffer.resize(dataSize);
    inbuffer.read(readBuffer.data(), int(dataSize));

    glBindTexture(GL_TEXTURE_2D,m_handle);
    if (m_useCompress) glCompressedTexImage2D(GL_TEXTURE_2D,0,storedformat,m_w,m_h,0,dataSize,readBuffer.data());
    else               glTexImage2D(GL_TEXTURE_2D,0,storedformat,m_w,m_h,0,storedformat,GL_UNSIGNED_BYTE,readBuffer.data());
    set_parameters();
    glBindTexture(GL_TEXTURE_2D,0);
  }
  else if (m_type==TI_CUBEMAP)
  {
    glBindTexture(GL_TEXTURE_CUBE_MAP,m_handle);
    for (uint iface = 0; iface < 6; ++iface)
    {
      uint dataSize = 0;
      inbuffer.read(reinterpret_cast<char*>(&dataSize), sizeof(uint));
      TRE_ASSERT(int(dataSize) > 0);
      readBuffer.resize(dataSize);
      inbuffer.read(readBuffer.data(), int(dataSize));

      if (m_useCompress) glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,storedformat,m_w,m_h,0,dataSize,readBuffer.data());
      else               glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,storedformat,m_w,m_h,0,storedformat,GL_UNSIGNED_BYTE,readBuffer.data());
    }
    set_parameters();
    glBindTexture(GL_TEXTURE_CUBE_MAP,0);
  }
  IsOpenGLok("texture::read");
}

void texture::write(std::ostream &outbuffer) const
{
  // header
  uint tinfo[8];
  tinfo[0] = m_type;
  tinfo[1] = m_name.size();
  tinfo[2] = m_w;
  tinfo[3] = m_h;
  tinfo[4] = (m_useMipmap)*MMASK_MIPMAP | (m_useCompress)*MMASK_COMPRESS | (m_useAnisotropic)*MMASK_ANISOTROPIC;
  tinfo[5] = 0; // unused
  tinfo[6] = m_components;
  tinfo[7] = TEXTURE_BIN_VERSION;
  outbuffer.write(reinterpret_cast<const char*>(&tinfo), sizeof(tinfo));
  // textname
  if (!m_name.empty()) outbuffer.write(m_name.c_str(), sizeof(char)*m_name.size());
  if (m_w * m_h == 0) return;
  // format
  const GLenum storedformat = getTexFormatStorage(m_components, m_useCompress);
  TRE_ASSERT(storedformat != GL_INVALID_VALUE);
  // data
  std::vector<char> writeBuffer;
  if (m_type==TI_2D)
  {
    glBindTexture(GL_TEXTURE_2D,m_handle);

    uint dataSizeBytes = 0;
#ifdef TRE_OPENGL_ES
    if (m_useCompress) dataSizeBytes = (m_w / 4) * (m_h / 4) * (storedformat == GL_COMPRESSED_RGB8_ETC2 ? 8 : 16);
    else               dataSizeBytes = m_w * m_h * m_components;
#else
    if (m_useCompress) glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,(GLint*)&dataSizeBytes);
    else               dataSizeBytes = m_w * m_h * m_components;
#endif
    TRE_ASSERT(int(dataSizeBytes) > 0);
    writeBuffer.resize(dataSizeBytes);

    if (m_useCompress) glGetCompressedTexImage(GL_TEXTURE_2D,0,writeBuffer.data());
    else               glGetTexImage(GL_TEXTURE_2D,0,storedformat,GL_UNSIGNED_BYTE,writeBuffer.data());

    outbuffer.write(reinterpret_cast<const char*>(&dataSizeBytes), sizeof(uint));
    outbuffer.write(writeBuffer.data(), dataSizeBytes);

    glBindTexture(GL_TEXTURE_2D,0);
  }
  else if (m_type==TI_CUBEMAP)
  {
    glBindTexture(GL_TEXTURE_CUBE_MAP,m_handle);

    for (uint iface = 0; iface < 6; ++iface)
    {
      uint dataSizeBytes = 0;
#ifdef TRE_OPENGL_ES
      if (m_useCompress) dataSizeBytes = (m_w / 4) * (m_h / 4) * (storedformat == GL_COMPRESSED_RGB8_ETC2 ? 8 : 16);
      else               dataSizeBytes = m_w * m_h * m_components;
#else
      if (m_useCompress) glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,GL_TEXTURE_COMPRESSED_IMAGE_SIZE,(GLint*)&dataSizeBytes);
      else               dataSizeBytes = m_w * m_h * m_components;
#endif
      TRE_ASSERT(int(dataSizeBytes) > 0);
      writeBuffer.resize(dataSizeBytes);

      if (m_useCompress) glGetCompressedTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,writeBuffer.data());
      else               glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,storedformat,GL_UNSIGNED_BYTE,writeBuffer.data());

      outbuffer.write(reinterpret_cast<const char*>(&dataSizeBytes), sizeof(uint));
      outbuffer.write(writeBuffer.data(), dataSizeBytes);
    }

    glBindTexture(GL_TEXTURE_CUBE_MAP,0);
  }
  IsOpenGLok("texture::write");
}

#undef TEXTURE_BIN_VERSION

//-----------------------------------------------------------------------------

bool texture::loadNewTextureFromBMP(const std::string & filename, int modemask)
{
  SDL_Surface* SDLTex_Start = SDL_LoadBMP(filename.c_str());
  if(!SDLTex_Start)
  {
    TRE_LOG("Faild to load texture " << filename << ", SDLerror:" << std::endl << SDL_GetError());
    clear();
    return false;
  }
  const bool status = loadNewTextureFromSDLSurface(SDLTex_Start, filename, modemask);

  SDL_FreeSurface(SDLTex_Start);

  return status;
}

bool texture::loadNewTextureFromFile(const std::string & filename, int modemask)
{
#ifdef TRE_WITH_SDL2_IMAGE
  SDL_Surface* SDLTex_Start = IMG_Load(filename.c_str());
  if(!SDLTex_Start)
  {
    TRE_LOG("Faild to load texture " << filename << ", SDLerror:" << std::endl << SDL_GetError());
    clear();
    return false;
  }
  const bool status = loadNewTextureFromSDLSurface(SDLTex_Start, filename, modemask);

  SDL_FreeSurface(SDLTex_Start);

  return status;
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool texture::loadNewTxComb2FromBMP(const std::string &filenameR, const std::string &filenameG, int modemask)
{
  SDL_Surface* surfR = SDL_LoadBMP(filenameR.c_str());
  if(!surfR)
  {
    TRE_LOG("Failed to load texture " << filenameR << ", SDLerror:" << std::endl << SDL_GetError());
    clear();
    return false;
  }
  SDL_Surface* surfG = SDL_LoadBMP(filenameG.c_str());
  if(!surfG)
  {
    TRE_LOG("Failed to load texture " << filenameG << ", SDLerror:" << std::endl << SDL_GetError());
    SDL_FreeSurface(surfR);
    clear();
    return false;
  }

  SDL_Surface* surfCOMB = SDL_CreateRGBSurface(0, surfR->w, surfR->h, 32, 0, 0, 0, 0);

  _rawCombine_R_G(surfR, surfG, surfCOMB);

  SDL_FreeSurface(surfR);
  SDL_FreeSurface(surfG);

  const bool status = loadNewTextureFromSDLSurface(surfCOMB, filenameR + " combine RG", modemask | MMASK_RG_ONLY);

  SDL_FreeSurface(surfCOMB);

  return status;
}

bool texture::loadNewTxComb2FromFile(const std::string &filenameR, const std::string &filenameG, int modemask)
{
#ifdef TRE_WITH_SDL2_IMAGE
  SDL_Surface* surfR = IMG_Load(filenameR.c_str());
  if(!surfR)
  {
    TRE_LOG("Failed to load texture " << filenameR << ", SDLerror:" << std::endl << SDL_GetError());
    clear();
    return false;
  }
  SDL_Surface* surfG = IMG_Load(filenameG.c_str());
  if(!surfG)
  {
    TRE_LOG("Failed to load texture " << filenameG << ", SDLerror:" << std::endl << SDL_GetError());
    SDL_FreeSurface(surfR);
    clear();
    return false;
  }

  SDL_Surface* surfCOMB = SDL_CreateRGBSurface(0, surfR->w, surfR->h, 32, 0, 0, 0, 0);

  _rawCombine_R_G(surfR, surfG, surfCOMB);

  SDL_FreeSurface(surfR);
  SDL_FreeSurface(surfG);

  const bool status = loadNewTextureFromSDLSurface(surfCOMB, filenameR + " combine RG", modemask | MMASK_RG_ONLY);

  SDL_FreeSurface(surfCOMB);

  return status;
#else
  return false;
#endif
}

//-----------------------------------------------------------------------------

bool texture::loadNewCubeTexFromBMP(const std::string &filebasename, int modemask)
{
  // init
  m_name = filebasename;
  m_type = TI_CUBEMAP;
  m_useMipmap      = (modemask & MMASK_MIPMAP     ) != 0;
  m_useCompress    = (modemask & MMASK_COMPRESS   ) != 0;
  m_useAnisotropic = (modemask & MMASK_ANISOTROPIC) != 0;
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
  // process
  bool success = true;
  const std::string facenames[] = { ".xpos.bmp",".xneg.bmp",
                                    ".ypos.bmp",".yneg.bmp",
                                    ".zpos.bmp",".zneg.bmp" };
  GLenum externalformat,storeformat;
#ifdef TRE_OPENGL_ES
  bool needConvertReverse = false;
  std::vector<char> bufferCompress;
#endif
  // loop on cube faces - load texture with OpenGL
  glGenTextures(1,&m_handle);
  glBindTexture(GL_TEXTURE_CUBE_MAP,m_handle);
  for (int iface = 0; iface < 6; ++iface)
  {
    const std::string filename(filebasename+facenames[iface]);
    SDL_Surface* surface = SDL_LoadBMP(filename.c_str());
    if(!surface)
    {
      TRE_LOG("Faild to load texture " << filename << ", SDLerror:" << std::endl << SDL_GetError());
      success = false;
      break;
    }
    // retrieve info from texture
    if (iface==0)
    {
      externalformat = getTexFormatSource(surface);
#ifdef TRE_OPENGL_ES
      if (externalformat == GL_BGR || externalformat == GL_BGRA)
      {
        externalformat = (externalformat == GL_BGR) ? GL_RGB : GL_RGBA;
        needConvertReverse = true;
      }
#endif
      m_components = surface->format->BytesPerPixel;
      storeformat = getTexFormatStorage(m_components,m_useCompress);
      m_w = surface->w;
      m_h = surface->h;
      TRE_ASSERT(m_w >= 4 && (m_w & 0x03) == 0);
      TRE_ASSERT(m_h >= 4 && (m_h & 0x03) == 0);
    }
    else
    {
      if ((surface->format->BytesPerPixel==4 && m_components != 4) ||
          (surface->format->BytesPerPixel==3 && m_components != 3) )
      {
        TRE_LOG("Mismaching texure format for cubemap face " << filename);
        success = false;
        break;
      }
      if (m_w != uint(surface->w) || m_h != uint(surface->h))
      {
        TRE_LOG("Mismatching texture dimension for cubemap face " << filename);
        SDL_FreeSurface(surface);
        success = false;
        break;
      }
    }

#ifdef TRE_OPENGL_ES
    if (needConvertReverse) _rawConvert_BRG_to_RGB(surface);
#endif

    TRE_LOG("Texture cubeMap " << filename << " loaded : size = (" << surface->w << " x " << surface->h
                               << ") and components = " << m_components
                               << " mode = " << modemask);
    // load texture with OpenGL
#ifdef TRE_OPENGL_ES
  if (m_useCompress)
  {
    TRE_ASSERT(m_components >= 3); // unrelevant elsewhere.
    const uint compressedSizeBytes = (m_w / 4) * (m_h / 4) * (storeformat == GL_COMPRESSED_RGB8_ETC2 ? 8 : 16);
    std::vector<uint8_t> bufferCompress;
    bufferCompress.resize(compressedSizeBytes);
    _rawCompress(surface, storeformat, bufferCompress.data());
    glCompressedTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + iface, 0, storeformat, surface->w, surface->h, 0, compressedSizeBytes, bufferCompress.data());
  }
  else
#endif
  {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X+iface,0,storeformat,surface->w,surface->h,0,externalformat,GL_UNSIGNED_BYTE,surface->pixels);
  }
    SDL_FreeSurface(surface);
  }
  set_parameters();
  // end
  IsOpenGLok("texture::loadNewCubeTexFromBMP");
  glBindTexture(GL_TEXTURE_CUBE_MAP,0);
  if (!success) clear();
  return success;
}

//-----------------------------------------------------------------------------

bool texture::loadNewTextureFromSDLSurface(SDL_Surface *surface, const std::string & name, int modemask)
{
  TRE_ASSERT(surface != nullptr);
  // init
  m_name = name;
  m_type = TI_2D;
  m_useMipmap      = (modemask & MMASK_MIPMAP     ) != 0;
  m_useCompress    = (modemask & MMASK_COMPRESS   ) != 0;
  m_useAnisotropic = (modemask & MMASK_ANISOTROPIC) != 0;
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

  if (m_components == 1)
  {
    // WebGL does not support texture swizzle.
#ifdef TRE_EMSCRIPTEN
    TRE_ASSERT(surface->format->BytesPerPixel == 4);
    const int npixels = surface->w * surface->h;
    uint * pixels = static_cast<uint*>(surface->pixels);
    for (int ip=0;ip<npixels;++ip) pixels[ip] |= 0x00FFFFFF;
    m_components = 4;
#else
    _rawPack_A8(surface);
    externalformat = GL_RED;
#endif
  }
#ifdef TRE_OPENGL_ES
  if (m_components == 2 && externalformat != GL_RG)
  {
    _rawPack_RG8(surface);
    externalformat = GL_RG;
  }
  if (externalformat == GL_BGR || externalformat == GL_BGRA)
  {
    _rawConvert_BRG_to_RGB(surface);
    externalformat = (externalformat == GL_BGR) ? GL_RGB : GL_RGBA;
  }
#endif

  const GLenum storeformat = getTexFormatStorage(m_components, m_useCompress);

  // load texture with OpenGL
  TRE_LOG("Texture " << name << " loaded : size = (" << surface->w << " x " << surface->h
                     << ") and components = " << m_components
                     << " mode = " << modemask);
  glGenTextures(1,&m_handle);
  glBindTexture(GL_TEXTURE_2D,m_handle);
#ifdef TRE_OPENGL_ES
  if (m_useCompress)
  {
    TRE_ASSERT(m_components >= 3); // unrelevant elsewhere.
    const uint compressedSizeBytes = (m_w / 4) * (m_h / 4) * (storeformat == GL_COMPRESSED_RGB8_ETC2 ? 8 : 16);
    std::vector<uint8_t> bufferCompress;
    bufferCompress.resize(compressedSizeBytes);
    _rawCompress(surface, storeformat, bufferCompress.data());
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, storeformat, surface->w, surface->h, 0, compressedSizeBytes, bufferCompress.data());
  }
  else
#endif
  {
    glTexImage2D(GL_TEXTURE_2D,0,storeformat,surface->w,surface->h,0,externalformat,GL_UNSIGNED_BYTE,surface->pixels);
  }
  IsOpenGLok("texture::loadNewTextureFromSDLSurface load");

  set_parameters();
  // check
#ifndef TRE_OPENGL_ES
  GLint isCompressed = GL_FALSE;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_COMPRESSED, &isCompressed);
  TRE_ASSERT((isCompressed != GL_FALSE) == m_useCompress);
#endif
  // end
  IsOpenGLok("texture::loadNewTextureFromSDLSurface complete");
  glBindTexture(GL_TEXTURE_2D,0);
  return true;
}

//-----------------------------------------------------------------------------

bool texture::loadNewTextureWhite(const std::string &name)
{
  SDL_Surface *tmpSurface = SDL_CreateRGBSurface(0, 4, 4, 32, 0, 0, 0, 0);

  uint *p0 = static_cast<uint*>(tmpSurface->pixels);
  for (uint ip = 0; ip < 16; ++ip) p0[ip] = 0xFFFFFFFF;

  const bool status = loadNewTextureFromSDLSurface(tmpSurface, name);

  SDL_FreeSurface(tmpSurface);

  return status;
}

//-----------------------------------------------------------------------------

bool texture::loadNewTextureCheckerboard(uint width, uint height, const std::string &name)
{
  SDL_Surface *tmpSurface = SDL_CreateRGBSurface(0, width, height, 32, 0, 0, 0, 0); // reminder: on little-endian, the default 32bits format is BGRA (that is 0xAARRGGBB)

  uint pWB[2] = { 0xFFFFFFFF, 0xFF000000 };

  uint *p0 = static_cast<uint*>(tmpSurface->pixels);
  for (uint jrow = 0; jrow < height; ++jrow)
  {
    for (uint icol = 0; icol < width; ++icol)
    {
      (*p0++) = pWB[(jrow + icol) & 0x01];
    }
  }

  const bool status = loadNewTextureFromSDLSurface(tmpSurface, name);

  SDL_FreeSurface(tmpSurface);

  return status;
}

//-----------------------------------------------------------------------------

void texture::clear()
{
  if (m_handle!=0) glDeleteTextures(1,&m_handle);
  m_handle = 0;
  m_useCompress = m_useMipmap = m_useAnisotropic = false;
  m_w = m_h = 0;
}

//-----------------------------------------------------------------------------

void texture::set_parameters()
{
  TRE_ASSERT(m_type==TI_2D || m_type==TI_CUBEMAP);

  const GLenum target = (m_type==TI_2D) ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;

  if (m_useMipmap==true) glGenerateMipmap(target);
  glTexParameteri(target,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(target,GL_TEXTURE_WRAP_T,GL_REPEAT);
  glTexParameteri(target,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  if (m_useMipmap==true) glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
  else                   glTexParameteri(target,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
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
      TRE_LOG("Anisotropic filter is supported !! (for " << m_name << ") with amount = " << amountanisotropic);
      if (amountanisotropic>4.f) amountanisotropic=4.f;
      glTexParameterf(target,GL_TEXTURE_MAX_ANISOTROPY_EXT,amountanisotropic);
    }
    else
    {
      TRE_LOG("Anisotropic filter is not supported (for " << m_name << ")");
    }
  }
}

//-----------------------------------------------------------------------------

void texture::_rawCombine_R_G(const SDL_Surface* surfR, const SDL_Surface* surfG, SDL_Surface* surfRG)
{
  TRE_ASSERT(surfR->w == surfG->w && surfR->h == surfG->h && surfR->format->BytesPerPixel == surfG->format->BytesPerPixel);
  TRE_ASSERT(surfRG->format->BytesPerPixel == 4);
  TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN); // 0xAARRGGBB

  TRE_ASSERT(surfR->pitch == surfR->format->BytesPerPixel * surfR->w);

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
}

//-----------------------------------------------------------------------------

void texture::_rawConvert_BRG_to_RGB(SDL_Surface *surface)
{
  if (surface->format->BytesPerPixel == 3)
  {
    uint8_t* pixels = static_cast<uint8_t*>(surface->pixels);
    for (int j = 0; j < surface->h; ++j)
    {
      uint8_t* pixelsRow = pixels + surface->pitch * j;
      for (int i = 0; i < surface->w; ++i)
      {
        std::swap(pixelsRow[0], pixelsRow[2]);
        pixelsRow += 3;
      }
    }
  }
  else if (surface->format->BytesPerPixel == 4)
  {
    TRE_ASSERT(surface->pitch == surface->format->BytesPerPixel * surface->w);
    const int npixels = surface->w * surface->h;
    uint * pixels = static_cast<uint*>(surface->pixels);
    for (int ip=0;ip<npixels;++ip) pixels[ip] = (pixels[ip] & 0xFF00FF00) | ((pixels[ip] & 0x00FF0000) >> 16) | ((pixels[ip] & 0x000000FF) << 16);
  }
  else
  {
    TRE_FATAL("texture::_rawConvert_BRG_to_RGB: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

void texture::_rawPack_A8(SDL_Surface *surface)
{
  if (surface->format->BytesPerPixel == 4)
  {
    TRE_ASSERT(surface->pitch == surface->format->BytesPerPixel * surface->w);
    const int npixels = surface->w * surface->h;
    uint * pixelsIn = static_cast<uint*>(surface->pixels);
    uint8_t * pixelsOut = static_cast<uint8_t*>(surface->pixels);

    for (int ip=0;ip<npixels;++ip) pixelsOut[ip] = (pixelsIn[ip] >> 24) & 0xFF;
  }
  else if (surface->format->BytesPerPixel >= 2)
  {
    uint8_t * pixelsIn = static_cast<uint8_t*>(surface->pixels);
    uint8_t * pixelsOut = static_cast<uint8_t*>(surface->pixels);
    for (int j = 0; j < surface->h; ++j)
    {
      uint8_t* pixelsInRow = pixelsIn + surface->pitch * j;
      for (int i = 0; i < surface->w; ++i)
      {
        pixelsOut[0] = pixelsInRow[0] & 0xFF;
        pixelsInRow += surface->format->BytesPerPixel;
        pixelsOut++;
      }
    }
  }
  else
  {
    TRE_FATAL("texture::_rawPack_to_A8: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

void texture::_rawPack_RG8(SDL_Surface *surface)
{
  if (surface->format->BytesPerPixel == 4)
  {
    TRE_ASSERT(SDL_BYTEORDER == SDL_LIL_ENDIAN); // 0xAARRGGBB
    TRE_ASSERT(surface->pitch == surface->format->BytesPerPixel * surface->w);
    const int npixels = surface->w * surface->h;
    uint * pixelsIn = static_cast<uint*>(surface->pixels);
    uint8_t * pixelsOut = static_cast<uint8_t*>(surface->pixels);

    for (int ip=0;ip<npixels;++ip)
    {
      pixelsOut[ip*2+0] = (pixelsIn[ip] >> 16) & 0xFF;
      pixelsOut[ip*2+1] = (pixelsIn[ip] >>  8) & 0xFF;
    }
  }
  else if (surface->format->BytesPerPixel == 3)
  {
    uint8_t * pixelsIn = static_cast<uint8_t*>(surface->pixels);
    uint8_t * pixelsOut = static_cast<uint8_t*>(surface->pixels);
    for (int j = 0; j < surface->h; ++j)
    {
      uint8_t* pixelsInRow = pixelsIn + surface->pitch * j;
      for (int i = 0; i < surface->w; ++i)
      {
        pixelsOut[0] = pixelsInRow[0] & 0xFF;
        pixelsOut[1] = pixelsInRow[1] & 0xFF;
        pixelsInRow += 3;
        pixelsOut += 2;
      }
    }
  }
  else
  {
    TRE_FATAL("texture::_rawPack_to_RG8: Invalid input format")
  }
}

//-----------------------------------------------------------------------------

#ifdef TRE_OPENGL_ES

struct s_compressionReport
{
  uint  m_modeINDIVcount = 0;
  uint  m_modeDIFFcount = 0;
  float m_errorL2 = 0.f;
  float m_errorInf = 0.f;
};

// --------

static inline void _encodeColor_RGB8(glm::ivec3 &colorZone0, glm::ivec3 &colorZone1, uint &bufferEncoded_HIGH)
{
  // note: RGB8 (8bits, [0-255])
  static const glm::ivec3 _diffModeMin = glm::ivec3(-4*8, -4*8, -4*8);
  static const glm::ivec3 _diffModeMax = glm::ivec3( 3*8,  3*8,  3*8);
  const glm::ivec3        rgbDiff = colorZone1 - colorZone0;
  const bool              modeDiff = glm::all(glm::lessThanEqual(_diffModeMin, rgbDiff) & glm::lessThanEqual(rgbDiff, _diffModeMax));
  if (modeDiff)
  {
    colorZone0 = (colorZone0 * 8 + 33) / 66; // round from [0-255] to {8*a + a/4, a in [0,31]} ~ {33*a/4, a in [0,31]}
    TRE_ASSERT(colorZone0.r < 0x20 && colorZone0.g < 0x20 && colorZone0.b < 0x20);
    colorZone1 = (rgbDiff / 8) & 0x07; // note: the sign bit is already set (bit3)
    bufferEncoded_HIGH = (colorZone0.r << 3) | (colorZone1.r) | (colorZone0.g << 11) | (colorZone1.g << 8) | (colorZone0.b << 19) | (colorZone1.b << 16) | 0x02000000;  // warning: little-endian
    // decode the color back to RGB8
    colorZone1 = colorZone0 + (rgbDiff / 8);
    colorZone0 = colorZone0 * 8 | colorZone0 / 4;
    colorZone1 = colorZone1 * 8 | colorZone1 / 4;
  }
  else
  {
    colorZone0 = (colorZone0 + 8) / 17; // round from [0-255] to {17*a, a in [0,15]}
    colorZone1 = (colorZone1 + 8) / 17;
    TRE_ASSERT(colorZone0.r < 0x10 && colorZone0.g < 0x10 && colorZone0.b < 0x10);
    TRE_ASSERT(colorZone1.r < 0x10 && colorZone1.g < 0x10 && colorZone1.b < 0x10);
    bufferEncoded_HIGH = (colorZone0.r << 4) | (colorZone1.r) | (colorZone0.g << 12) | (colorZone1.g << 8) | (colorZone0.b << 20) | (colorZone1.b << 16); // warning: {R4.R4|G4.G4|B4.B4|..} -little-endian-> 0x.BGR
    // decode the color back to RGB8
    colorZone0 *= 0x11;
    colorZone1 *= 0x11;
  }
}

static inline glm::ivec3 _encodePixel_t3(const uint pxid, const uint tid, const int t3, const glm::ivec3 &colorBase, uint &bufferEncoded_LOW)
{
  static const int _tableETC1_val[8][2] = { {2, 8},  {5, 17},  {9, 29}, {13, 42}, {18, 60}, {24, 80},  {33, 106},  {47, 183} };
  static const int _tableETC1_sep3[8] = { 5*3, 11*3, 19*3, 27*3, 39*3, 52*3, 69*3, 105*3 };
  // encode
  TRE_ASSERT(tid < 8);
  const uint pixModSign = t3 < 0 ? 1 : 0;
  const uint pixModLarge = fabs(t3) > _tableETC1_sep3[tid] ? 1 : 0;
  TRE_ASSERT(pxid < 16);
  const uint n = pxid ^ 0x08;
  bufferEncoded_LOW |= (pixModSign << (0+n)) | (pixModLarge << (16+n)); // warning: little-endian on 4-bytes word
  // decode
  const int pxModifier = (1 - 2 * int(pixModSign)) * _tableETC1_val[tid][pixModLarge];
  return glm::clamp(colorBase + pxModifier, 0, 255);
}

static inline float _errorRGB8(const glm::ivec3 &d_rgb) // perceptual error [0,1]
{
  return (d_rgb.r * d_rgb.r * 0.299f / 65025.f) + (d_rgb.g * d_rgb.g * 0.587f / 65025.f) + (d_rgb.b * d_rgb.b * 0.114f / 65025.f);
}

static inline void _rawCompress4x4_RGB8_ETC2(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t *compressed, s_compressionReport *report = nullptr)
{
  const size_t byteIndexR = (pxByteSize == 4) ? 1 : 0;
  const size_t byteIndexG = byteIndexR + 1;
  const size_t byteIndexB = byteIndexR + 2;

  /* pixel layout: |  0 |  4 |  8 | 12 |
   *               |  1 |  5 |  9 | 13 |
   *               |  2 |  6 | 10 | 14 |
   *               |  3 |  7 | 11 | 15 | */

#define extR(i, j) int(pixelsIn[i * pxByteSize + j * pitch + byteIndexR] & 0xFF)
#define extG(i, j) int(pixelsIn[i * pxByteSize + j * pitch + byteIndexG] & 0xFF)
#define extB(i, j) int(pixelsIn[i * pxByteSize + j * pitch + byteIndexB] & 0xFF)
#define extRGB(i, j) glm::ivec3(extR(i, j), extG(i, j), extB(i, j))

  const glm::ivec3 rgb[16] = { extRGB(0,0), extRGB(0,1), extRGB(0,2), extRGB(0,3),
                               extRGB(1,0), extRGB(1,1), extRGB(1,2), extRGB(1,3),
                               extRGB(2,0), extRGB(2,1), extRGB(2,2), extRGB(2,3),
                               extRGB(3,0), extRGB(3,1), extRGB(3,2), extRGB(3,3) };

#undef extR
#undef extG
#undef extB
#undef extRGB

  // mode ETC1 (Individual, Differential): split 4x4 by either 2x4 either 4x2.
  // Each zone has a base color, each pixel within a zone has a color clamp(base.r + modifier, base.g + modifier, base.b + modifier)
  /* zone layout: Flip0: Zone0: 0  1  2  3  4  5  6  7 <=> Z00 ( 0  1  4  5) + Z10 ( 2  3  6  7)
   *                     Zone1: 8  9 10 11 12 13 14 15 <=> Z01 ( 8  9 12 13) + Z11 (10 11 14 15)
   *              Flip1: Zone0: 0  4  8 12  1  5  9 13 <=> Z00 ( 0  1  4  5) + Z01 ( 8  9 12 13)
   *                     Zone1: 2  6 10 14  3  7 11 15 <=> Z10 ( 2  3  6  7) + Z11 (10 11 14 15) */

  // Compute the base colors

  uint       rgbEncodedFlip0;
  glm::ivec3 rgbBaseFlip0[2];

  uint       rgbEncodedFlip1;
  glm::ivec3 rgbBaseFlip1[2];

  {
    // note: the least-square method leads to simply compute the average color.
    const glm::ivec3 r00 = rgb[ 0] + rgb[ 1] + rgb[ 4] + rgb[ 5];
    const glm::ivec3 r10 = rgb[ 2] + rgb[ 3] + rgb[ 6] + rgb[ 7];
    const glm::ivec3 r01 = rgb[ 8] + rgb[ 9] + rgb[12] + rgb[13];
    const glm::ivec3 r11 = rgb[10] + rgb[11] + rgb[14] + rgb[15];

    rgbBaseFlip0[0] = (r00 + r10) / 8; // Average - Zone0
    rgbBaseFlip0[1] = (r01 + r11) / 8; // Average - Zone1

    rgbBaseFlip1[0] = (r00 + r01) / 8; // Average - Zone0
    rgbBaseFlip1[1] = (r10 + r11) / 8; // Average - Zone1

    // Encode the base colors: select mode individual (RGB4-RGB4) or mode differential (RGB5-deltaRGB3)

    _encodeColor_RGB8(rgbBaseFlip0[0], rgbBaseFlip0[1], rgbEncodedFlip0);
    _encodeColor_RGB8(rgbBaseFlip1[0], rgbBaseFlip1[1], rgbEncodedFlip1);
  }

  // Compute the pixel's modifiers
  // This is a mixed-integer optimisation problem :(
  // Thus, we use a brute-force method.

#define getT(px, base) (rgb[px].r - base.r) + (rgb[px].g - base.g) + (rgb[px].b - base.b)

  uint bestTablesFlip = 0; // (encoded)
  uint bestPixelModifiers = 0; // (encoded)
  float bestError = std::numeric_limits<float>::infinity();

  for (uint fb = 0; fb < 2; ++fb)
  {
    const glm::ivec3 rgbBase_00 = (fb == 0) ? rgbBaseFlip0[0] : rgbBaseFlip1[0];
    const glm::ivec3 rgbBase_10 = (fb == 0) ? rgbBaseFlip0[0] : rgbBaseFlip1[1];
    const glm::ivec3 rgbBase_01 = (fb == 0) ? rgbBaseFlip0[1] : rgbBaseFlip1[0];
    const glm::ivec3 rgbBase_11 = (fb == 0) ? rgbBaseFlip0[1] : rgbBaseFlip1[1];

    // Compute the raw pixel modifiers

    const int t3[16] = { getT( 0, rgbBase_00), getT( 1, rgbBase_00), getT( 2, rgbBase_10), getT( 3, rgbBase_10),
                         getT( 4, rgbBase_00), getT( 5, rgbBase_00), getT( 6, rgbBase_10), getT( 7, rgbBase_10),
                         getT( 8, rgbBase_01), getT( 9, rgbBase_01), getT(10, rgbBase_11), getT(11, rgbBase_11),
                         getT(12, rgbBase_01), getT(13, rgbBase_01), getT(14, rgbBase_11), getT(15, rgbBase_11) }; // in fact, we have 3*t

    // Reduce the nbr of tables to evaluate (?)

    // Loop over tables (for each zone)

    glm::ivec3 rgbDelta;
    uint bestTableId_fb[2] = {0, 0};
    uint bestPixelModifiers_fb[2] = {0, 0};
    float bestError_fp[2] = { std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity() };

    for (uint tid = 0; tid < 8; ++tid)
    {
      uint pixelModifiers_00 = 0; float err_00;
      rgbDelta = _encodePixel_t3( 0, tid, t3[ 0], rgbBase_00, pixelModifiers_00) - rgb[ 0]; err_00  = _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 1, tid, t3[ 1], rgbBase_00, pixelModifiers_00) - rgb[ 1]; err_00 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 4, tid, t3[ 4], rgbBase_00, pixelModifiers_00) - rgb[ 4]; err_00 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 5, tid, t3[ 5], rgbBase_00, pixelModifiers_00) - rgb[ 5]; err_00 += _errorRGB8(rgbDelta);

      uint pixelModifiers_10 = 0; float err_10;
      rgbDelta = _encodePixel_t3( 2, tid, t3[ 2], rgbBase_01, pixelModifiers_10) - rgb[ 2]; err_10  = _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 3, tid, t3[ 3], rgbBase_01, pixelModifiers_10) - rgb[ 3]; err_10 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 6, tid, t3[ 6], rgbBase_01, pixelModifiers_10) - rgb[ 6]; err_10 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 7, tid, t3[ 7], rgbBase_01, pixelModifiers_10) - rgb[ 7]; err_10 += _errorRGB8(rgbDelta);

      uint pixelModifiers_01 = 0; float err_01;
      rgbDelta = _encodePixel_t3( 8, tid, t3[ 8], rgbBase_10, pixelModifiers_01) - rgb[ 8]; err_01  = _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3( 9, tid, t3[ 9], rgbBase_10, pixelModifiers_01) - rgb[ 9]; err_01 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3(12, tid, t3[12], rgbBase_10, pixelModifiers_01) - rgb[12]; err_01 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3(13, tid, t3[13], rgbBase_10, pixelModifiers_01) - rgb[13]; err_01 += _errorRGB8(rgbDelta);

      uint pixelModifiers_11 = 0; float err_11;
      rgbDelta = _encodePixel_t3(10, tid, t3[10], rgbBase_11, pixelModifiers_11) - rgb[10]; err_11  = _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3(11, tid, t3[11], rgbBase_11, pixelModifiers_11) - rgb[11]; err_11 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3(14, tid, t3[14], rgbBase_11, pixelModifiers_11) - rgb[14]; err_11 += _errorRGB8(rgbDelta);
      rgbDelta = _encodePixel_t3(15, tid, t3[15], rgbBase_11, pixelModifiers_11) - rgb[15]; err_11 += _errorRGB8(rgbDelta);

      const float errZ0 = (fb == 0) ? err_00 + err_10 : err_00 + err_01;
      if (errZ0 < bestError_fp[0])
      {
        bestError_fp[0] = errZ0;
        bestTableId_fb[0] = tid;
        bestPixelModifiers_fb[0] = (fb == 0) ? pixelModifiers_00 | pixelModifiers_10 : pixelModifiers_00 | pixelModifiers_01;
      }

      const float errZ1 = (fb == 0) ? err_01 + err_11 : err_10 + err_11;
      if (errZ1 < bestError_fp[1])
      {
        bestError_fp[1] = errZ1;
        bestTableId_fb[1] = tid;
        bestPixelModifiers_fb[1] = (fb == 0) ? pixelModifiers_01 | pixelModifiers_11 : pixelModifiers_10 | pixelModifiers_11;
      }

      TRE_ASSERT((bestPixelModifiers_fb[0] & bestPixelModifiers_fb[1]) == 0);
    }

    const float bestError_tmp = glm::max(bestError_fp[0], bestError_fp[1]);
    if (bestError_tmp < bestError)
    {
      bestError = bestError_tmp;
      bestTablesFlip = (bestTableId_fb[0] << (24+5)) | (bestTableId_fb[1] << (24+2)) | (fb << (24+0));
      bestPixelModifiers = bestPixelModifiers_fb[0] | bestPixelModifiers_fb[1];
    }
  }

#undef getT

  TRE_ASSERT((rgbEncodedFlip0 & bestTablesFlip) == 0);
  TRE_ASSERT((rgbEncodedFlip1 & bestTablesFlip) == 0);

  *reinterpret_cast<uint*>(&compressed[0]) = (((bestTablesFlip & 0x01000000) == 0) ? rgbEncodedFlip0 : rgbEncodedFlip1) | bestTablesFlip;
  *reinterpret_cast<uint*>(&compressed[4]) = bestPixelModifiers;

  if (report)
  {
    if (compressed[3] & 0x02) ++report->m_modeDIFFcount;
    else                      ++report->m_modeINDIVcount;
    bestError /= 8.f;
    report->m_errorL2 += bestError;
    report->m_errorInf = glm::max(report->m_errorInf, sqrtf(bestError));
  }
}

// --------

static inline int _encodePixel_alpha(const uint pxid, const uint tid, const int alpha, const int alphaBase, const int multiplier, uint64_t &bufferEncoded_BigEndian)
{
  static const int _tableAEC_val[16][4] = { {2, 5, 9, 14}, {2, 6, 9, 12}, {1, 4, 7, 12}, {1, 3, 5, 12},
                                            {2, 5, 7, 11}, {2, 6, 8, 10}, {3, 6, 7, 10}, {2, 4, 7, 10},
                                            {1, 5, 7,  9}, {1, 4, 7,  9}, {1, 3, 7,  9}, {1, 4, 6,  9},
                                            {2, 3, 6,  9}, {0, 1, 2,  9}, {3, 5, 7,  8}, {2, 4, 6,  8} };
  //static const int _tableAEC_sep[16][3] = { 5*3, 11*3, 19*3, 27*3, 39*3, 52*3, 69*3, 105*3 };
  // encode
  TRE_ASSERT(tid < 16);
  const int  t = alpha - alphaBase;
  const uint pixModSign = t < 0 ? 1 : 0;
  const uint pixModMagn = 0; // TODO
  TRE_ASSERT(pxid < 16);
  bufferEncoded_BigEndian |= uint64_t(pixModSign * 4 + pixModMagn) << (45 - pxid * 3);
  // decode
  const int pxModifier = (1 - 2 * int(pixModSign)) * (_tableAEC_val[tid][pixModMagn] + int(pixModSign));
  return glm::clamp(alphaBase + multiplier * pxModifier, 0, 255);
}

static inline float _errorA8(const int d_alpha)
{
  return d_alpha * d_alpha / 65025.f;
}

static inline void _rawCompress4x4_ALPHA_EAC(const uint8_t *pixelsIn, uint pxByteSize, uint pitch, uint8_t *compressed, s_compressionReport *report = nullptr)
{
  TRE_ASSERT(pxByteSize == 4);

  /* pixel layout: |  0 |  4 |  8 | 12 |
   *               |  1 |  5 |  9 | 13 |
   *               |  2 |  6 | 10 | 14 |
   *               |  3 |  7 | 11 | 15 | */

#define extA(i, j) int(pixelsIn[i * pxByteSize + j * pitch + 0] & 0xFF)

  const int alpha[16] = { extA(0,0), extA(0,1), extA(0,2), extA(0,3),
                          extA(1,0), extA(1,1), extA(1,2), extA(1,3),
                          extA(2,0), extA(2,1), extA(2,2), extA(2,3),
                          extA(3,0), extA(3,1), extA(3,2), extA(3,3) };

#undef extA

  // mode EAC: each pixel has a color clamp(base + multiplier * modifier), where only the modifier is definied per pixel.

  const int average = (alpha[ 0] + alpha[ 1] + alpha[ 2] + alpha[ 3] + alpha[ 4] + alpha[ 5] + alpha[ 6] + alpha[ 7] +
                       alpha[ 8] + alpha[ 9] + alpha[10] + alpha[11] + alpha[12] + alpha[13] + alpha[14] + alpha[15] ) / 16;

#define diffA(pxid) abs(alpha[pxid] - average)

  const int diffMax_part0 = std::max(std::max( diffA( 0), diffA( 1)), std::max( diffA( 2), diffA( 3)));
  const int diffMax_part1 = std::max(std::max( diffA( 4), diffA( 5)), std::max( diffA( 6), diffA( 7)));
  const int diffMax_part2 = std::max(std::max( diffA( 8), diffA( 9)), std::max( diffA(10), diffA(11)));
  const int diffMax_part3 = std::max(std::max( diffA(12), diffA(13)), std::max( diffA(14), diffA(15)));
  const int diffMax = std::max(std::max(diffMax_part0, diffMax_part1), std::max(diffMax_part2, diffMax_part3));

#undef diffA

  // Loop over tables

  uint64_t bestEncoded_BigEndian = 0;
  float    bestError = std::numeric_limits<float>::infinity();

  for (uint tid = 0; tid < 16; ++tid)
  {
    TRE_FATAL("TODO");

    // Get the multiplier
    int multiplier = 1;

    TRE_ASSERT(multiplier > 0 && multiplier < 16);

    // Shift the average
    int localAverage = average;

    TRE_ASSERT(localAverage >= 0 && localAverage < 0xFF);

    // Compute the error

    uint64_t localEncoded_BigEndian = (uint64_t(localAverage & 0xFF) << 56) | (uint64_t(multiplier & 0xF) << 52) | (uint64_t(tid & 0xF) << 48);
    float    localError;
    int      alphaDiff;

    alphaDiff = _encodePixel_alpha( 0, tid, alpha[ 0], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 0]; localError  = _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 1, tid, alpha[ 1], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 1]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 2, tid, alpha[ 2], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 2]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 3, tid, alpha[ 3], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 3]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 4, tid, alpha[ 4], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 4]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 5, tid, alpha[ 5], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 5]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 6, tid, alpha[ 6], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 6]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 7, tid, alpha[ 7], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 7]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 8, tid, alpha[ 8], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 8]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha( 9, tid, alpha[ 9], localAverage, multiplier, localEncoded_BigEndian) - alpha[ 9]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(10, tid, alpha[10], localAverage, multiplier, localEncoded_BigEndian) - alpha[10]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(11, tid, alpha[11], localAverage, multiplier, localEncoded_BigEndian) - alpha[11]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(12, tid, alpha[12], localAverage, multiplier, localEncoded_BigEndian) - alpha[12]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(13, tid, alpha[13], localAverage, multiplier, localEncoded_BigEndian) - alpha[13]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(14, tid, alpha[14], localAverage, multiplier, localEncoded_BigEndian) - alpha[14]; localError += _errorA8(alphaDiff);
    alphaDiff = _encodePixel_alpha(15, tid, alpha[15], localAverage, multiplier, localEncoded_BigEndian) - alpha[15]; localError += _errorA8(alphaDiff);

    if (localError < bestError)
    {
      bestEncoded_BigEndian = localEncoded_BigEndian;
      bestError = localError;
    }
  }

  compressed[ 0] = (bestEncoded_BigEndian >> 56) & 0xFF;
  compressed[ 1] = (bestEncoded_BigEndian >> 48) & 0xFF;
  compressed[ 2] = (bestEncoded_BigEndian >> 40) & 0xFF;
  compressed[ 3] = (bestEncoded_BigEndian >> 32) & 0xFF;
  compressed[ 4] = (bestEncoded_BigEndian >> 24) & 0xFF;
  compressed[ 5] = (bestEncoded_BigEndian >> 16) & 0xFF;
  compressed[ 6] = (bestEncoded_BigEndian >>  8) & 0xFF;
  compressed[ 7] = (bestEncoded_BigEndian      ) & 0xFF;
}

#endif // TRE_OPENGL_ES

// --------

void texture::_rawCompress(SDL_Surface *surface, GLenum targetFormat, uint8_t *outBuffer)
{
#ifdef TRE_OPENGL_ES
  const int     pitch = surface->pitch;
  const int     pxByteSize = surface->format->BytesPerPixel;
  const uint8_t *inPixels = reinterpret_cast<uint8_t*>(surface->pixels);

#if defined(TRE_DEBUG) || defined(TRE_PRINTS)
  s_compressionReport report;
  s_compressionReport *reportPtr = &report;
#else
  s_compressionReport *reportPtr =nullptr;
#endif

  if (targetFormat == GL_COMPRESSED_RGB8_ETC2)
  {
    for (int ih = 0; ih < surface->h; ih += 4)
    {
      for (int iw = 0; iw < surface->w; iw += 4)
      {
        _rawCompress4x4_RGB8_ETC2(inPixels + pitch * ih + pxByteSize * iw, pxByteSize, pitch, outBuffer, reportPtr);
        outBuffer += 8;
      }
    }
  }
  else if (targetFormat == GL_COMPRESSED_RGBA8_ETC2_EAC)
  {
    TRE_ASSERT(pxByteSize == 4);
    for (int ih = 0; ih < surface->h; ih += 4)
    {
      for (int iw = 0; iw < surface->w; iw += 4)
      {
        _rawCompress4x4_ALPHA_EAC(inPixels + pitch * ih + 4 * iw, 4, pitch, outBuffer, reportPtr);
        outBuffer += 8;
        _rawCompress4x4_RGB8_ETC2(inPixels + pitch * ih + 4 * iw, 4, pitch, outBuffer, reportPtr);
        outBuffer += 8;
      }
    }
  }
  else
  {
    TRE_FATAL("_rawCompress format not supported");
  }

  if (reportPtr != nullptr)
  {
    const uint countTotal = report.m_modeDIFFcount + report.m_modeINDIVcount;
    TRE_ASSERT(int(countTotal) == (surface->h / 4) * (surface->w / 4));
    report.m_errorL2 = sqrtf(report.m_errorL2 / countTotal);
    TRE_LOG("report of conversion to ETC2: " <<
            "modeINDI = " << report.m_modeINDIVcount << " (" << int(report.m_modeINDIVcount * 100.f / countTotal) << " %), " <<
            "modeDIFF = " << report.m_modeDIFFcount << " (" << int(report.m_modeDIFFcount * 100.f / countTotal) << " %), " <<
            "maxError = " << report.m_errorInf << ", L2error = " << report.m_errorL2);
  }

#else
  TRE_FATAL("_rawCompress not implemented (only done for OpenGL-ES formats");
#endif
}

//-----------------------------------------------------------------------------

} // namespace
