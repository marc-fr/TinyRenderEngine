
#include "tre_utils.h"
#include "tre_textureSampler.h"

#ifdef TRE_WITH_LIBTIFF
#include "tiffio.h"
#endif

#include <string>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

void getSize(SDL_Surface *surf, unsigned &w, unsigned &h)
{
  w = unsigned(surf->w);
  h = unsigned(surf->h);
}

#ifdef TRE_WITH_LIBTIFF
void getSize(TIFF *surf, unsigned &w, unsigned &h)
{
  TIFFGetField(surf, TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(surf, TIFFTAG_IMAGELENGTH, &h);
}
#endif

// =============================================================================

template<class _S>
void genMipMap(_S *inSurface, unsigned level, const std::string &outFilePrefix)
{
  unsigned inW,inH;
  getSize(inSurface, inW, inH);

  while (level > 0)
  {
    inW /= 2;
    inH /= 2;
    --level;
  }

  inW = inW == 0 ? 1 : inW;
  inH = inH == 0 ? 1 : inH;

  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, inW, inH, 32, 0, 0, 0, 0);

  tre::textureSampler::resample(outSurface, inSurface);

#ifdef TRE_WITH_SDL2_IMAGE
  const std::string outFileName = outFilePrefix + ".png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  const std::string outFileName = outFilePrefix + ".bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");

  SDL_FreeSurface(outSurface);
}

// =============================================================================

enum e_cubeFace { CUBE_X_POS, CUBE_X_NEG, CUBE_Y_POS, CUBE_Y_NEG, CUBE_Z_POS, CUBE_Z_NEG };

static void _get_CubeMap_FaceCoord(e_cubeFace face, bool exterior, glm::vec3 &coord0, glm::vec3 &coordU, glm::vec3 &coordV)
{
  // Using OpenGL standard: the cube-map uses a Left-Handed Y-Up coordinate system.
  // https://www.khronos.org/opengl/wiki/Cubemap_Texture
  // note: The given UV-coords have already the "V" flipped, due to standard texture coordinates ((0,0) is the left-top)
  switch (face)
  {
    case CUBE_X_POS: { coord0 = glm::vec3( 1.f,  1.f,  1.f); coordU = glm::vec3( 0.f, 0.f, -2.f); coordV = glm::vec3(0.f, -2.f,  0.f); } break;
    case CUBE_X_NEG: { coord0 = glm::vec3(-1.f,  1.f, -1.f); coordU = glm::vec3( 0.f, 0.f,  2.f); coordV = glm::vec3(0.f, -2.f,  0.f); } break;
    case CUBE_Y_POS: { coord0 = glm::vec3(-1.f,  1.f, -1.f); coordU = glm::vec3( 2.f, 0.f,  0.f); coordV = glm::vec3(0.f,  0.f,  2.f); } break;
    case CUBE_Y_NEG: { coord0 = glm::vec3(-1.f, -1.f,  1.f); coordU = glm::vec3( 2.f, 0.f,  0.f); coordV = glm::vec3(0.f,  0.f, -2.f); } break;
    case CUBE_Z_POS: { coord0 = glm::vec3(-1.f,  1.f,  1.f); coordU = glm::vec3( 2.f, 0.f,  0.f); coordV = glm::vec3(0.f, -2.f,  0.f); } break;
    case CUBE_Z_NEG: { coord0 = glm::vec3( 1.f,  1.f, -1.f); coordU = glm::vec3(-2.f, 0.f,  0.f); coordV = glm::vec3(0.f, -2.f,  0.f); } break;
  }

  if (!exterior) // if cube is seen from interior, then apply a x-mirror
  {
    coordU *= -1.f;
    if (coordU.x <= -1.f) coord0.x =  1.f;
    if (coordU.x >=  1.f) coord0.x = -1.f;
    if (coordU.z <= -1.f) coord0.z =  1.f;
    if (coordU.z >=  1.f) coord0.z = -1.f;
  }
}

static const char * PART_NAME[6] = { "xpos", "xneg", "ypos", "yneg", "zpos", "zneg" };
static const char * SIDE_NAME[2] = { "outside", "inside" };

static void _get_CubeMap_FaceName(e_cubeFace face, bool exterior, std::string &name)
{
  name = std::string(SIDE_NAME[exterior ? 0 : 1]) + "." + std::string(PART_NAME[face]);
}

template<class _S>
static void _gen_CubeMap_FaceColor(_S *inSurface, e_cubeFace face, bool exterior, SDL_Surface *outSurface, const std::string &outFilePrefix)
{
  glm::vec3 coord0;
  glm::vec3 coordU;
  glm::vec3 coordV;
  _get_CubeMap_FaceCoord(face, exterior, coord0, coordU, coordV);

  tre::textureSampler::resample_toCubeMap(outSurface, inSurface, coord0, coordU, coordV);

  std::string suffix;
  _get_CubeMap_FaceName(face, exterior, suffix);

#ifdef TRE_WITH_SDL2_IMAGE
  const std::string outFileName = outFilePrefix + "." + suffix + ".png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  const std::string outFileName = outFilePrefix + "." + suffix + ".bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");
}

template<class _S>
static void _gen_CubeMap_FaceNormal(_S *inSurface, e_cubeFace face, bool exterior, SDL_Surface *outSurface, float factor, const std::string &outFilePrefix)
{
  glm::vec3 coord0;
  glm::vec3 coordU;
  glm::vec3 coordV;
  _get_CubeMap_FaceCoord(face, exterior, coord0, coordU, coordV);

  tre::textureSampler::mapNormals_toCubeMap(outSurface, inSurface, factor, coord0, coordU, coordV);

  std::string suffix;
  _get_CubeMap_FaceName(face, exterior, suffix);

#ifdef TRE_WITH_SDL2_IMAGE
  const std::string outFileName = outFilePrefix + "." + suffix + ".png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  const std::string outFileName = outFilePrefix + "." + suffix + ".bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");
}

// =============================================================================

template<class _S>
void genCubemaps(_S *inTexture, const std::string &filePrefix)
{
  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, 1024, 1024, 32, 0, 0, 0, 0);

  _gen_CubeMap_FaceColor(inTexture, CUBE_X_POS, true, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_X_NEG, true, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Y_POS, true, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Y_NEG, true, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_POS, true, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, true, outSurface, filePrefix + ".1024");

  _gen_CubeMap_FaceColor(inTexture, CUBE_X_POS, false, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_X_NEG, false, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Y_POS, false, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Y_NEG, false, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_POS, false, outSurface, filePrefix + ".1024");
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, false, outSurface, filePrefix + ".1024");

  SDL_FreeSurface(outSurface);
}

// =============================================================================

template<class _S>
void genCubeFrontLevels(_S *inTexture, const std::string &filePrefix)
{
  SDL_Surface *outSurface;

  outSurface = SDL_CreateRGBSurface(0, 2048, 2048, 32, 0, 0, 0, 0);
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, true, outSurface, filePrefix + ".2048");
  SDL_FreeSurface(outSurface);

  outSurface = SDL_CreateRGBSurface(0, 512, 512, 32, 0, 0, 0, 0);
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, true, outSurface, filePrefix + ".512");
  SDL_FreeSurface(outSurface);

  outSurface = SDL_CreateRGBSurface(0, 256, 256, 32, 0, 0, 0, 0);
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, true, outSurface, filePrefix + ".256");
  SDL_FreeSurface(outSurface);

  outSurface = SDL_CreateRGBSurface(0, 128, 128, 32, 0, 0, 0, 0);
  _gen_CubeMap_FaceColor(inTexture, CUBE_Z_NEG, true, outSurface, filePrefix + ".128");
  SDL_FreeSurface(outSurface);
}

// =============================================================================

template<class _S>
void genNormalMapFlat(_S *inTexture, float factor, const std::string &filePrefix)
{
  unsigned inW,inH;
  getSize(inTexture, inW, inH);

  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, inW, inH, 24, 0, 0, 0, 0);

  tre::textureSampler::mapNormals(outSurface, inTexture, factor);

#ifdef TRE_WITH_SDL2_IMAGE
  const std::string outFileName = filePrefix + ".NormalMap.png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  const std::string outFileName = filePrefix + ".NormalMap.bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");

  SDL_FreeSurface(outSurface);
}

// =============================================================================

template<class _S>
void genNormalMapFront(_S *inTexture, float factor, const std::string &filePrefix)
{
  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, 1024, 1024, 24, 0, 0, 0, 0);

  _gen_CubeMap_FaceNormal(inTexture, CUBE_Z_NEG, true, outSurface, factor, filePrefix + ".Normal");

  SDL_FreeSurface(outSurface);
}

// =============================================================================

template<class _S>
void genOversamplingFlat(_S *inTexture, float factor, const std::string &filePrefix)
{
  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, 1024, 1024, 24, 0, 0, 0, 0);

  tre::textureSampler::resample(outSurface, inTexture, glm::vec2(0.f, 0.f), glm::vec2(0.01f, 0.f), glm::vec2(0.f, 0.02f));

#ifdef TRE_WITH_SDL2_IMAGE
  std::string outFileName = filePrefix + ".overSampling.png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  std::string outFileName = filePrefix + ".overSampling.bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");

  tre::textureSampler::mapNormals(outSurface, inTexture, factor, glm::vec2(0.f, 0.f), glm::vec2(0.01f, 0.f), glm::vec2(0.f, 0.02f));

#ifdef TRE_WITH_SDL2_IMAGE
  outFileName = filePrefix + ".overSampling.Normal.png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  outFileName = filePrefix + ".overSampling.Normal.bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");

  SDL_FreeSurface(outSurface);
}

// =============================================================================

template<class _S>
void genRotated(_S *inTexture, float angle, const std::string &outFilePrefix)
{
  // get a square
  unsigned inW,inH;
  getSize(inTexture, inW, inH);

  const int outSize = int(glm::min(inW, inH));

  SDL_Surface *outSurface = SDL_CreateRGBSurface(0, outSize, outSize, 24, 0, 0, 0, 0);

  // set the coords
  const glm::vec2 tx = 0.7f * glm::vec2(cosf(angle), sinf(angle));
  const glm::vec2 ty = glm::vec2(-tx.y, tx.x);
  const glm::vec2 tS = glm::vec2(0.5f, 0.5f) - 0.5f * (tx + ty);

  tre::textureSampler::resample(outSurface, inTexture, tS, tx, ty);

#ifdef TRE_WITH_SDL2_IMAGE
  std::string outFileName = outFilePrefix + ".png";
  IMG_SavePNG(outSurface, outFileName.c_str());
#else
  const std::string outFileName = outFilePrefix + ".bmp";
  SDL_SaveBMP(outSurface, outFileName.c_str());
#endif
  TRE_LOG("Texture " << outFileName << " written.");
}

// =============================================================================

int main(int argc, char **argv)
{
  // Arguments
  std::string inputSDL_Map2D = TESTIMPORTPATH "resources/map_uv.png";
  std::string inputTiff_Map2D = TESTIMPORTPATH "resources/hemispherical_33p.tif";

  std::string outputSDL_Prefix = "imageSDL";
  std::string outputTiff_Prefix = "imageTIFF";

  if (argc >= 2)
    inputSDL_Map2D = argv[1];

  if (argc >= 3)
    inputTiff_Map2D = argv[2];

  if (argc >= 4)
    outputSDL_Prefix = argv[3];

  if (argc >= 5)
    outputSDL_Prefix = argv[4];

  // Init SDL2
  if(SDL_Init(SDL_INIT_VIDEO) < 0)
  {
    TRE_LOG("Fail to initialize SDL2 : " << SDL_GetError());
    SDL_Quit();
    return -1;
  }
  // Version d'OpenGL
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

#ifdef TRE_WITH_SDL2_IMAGE
  // Init SDL_Image
  int imgFlags = IMG_INIT_PNG;
  if( !( IMG_Init( imgFlags ) == imgFlags ) )
  {
      TRE_LOG("Fail to initialize SDL_Image : " << IMG_GetError());
      SDL_Quit();
      return -1;
  }
#endif

  bool status = true;

  // TEST: SDL_Surface sampling

  SDL_Surface *inputSurface = nullptr;
#ifdef TRE_WITH_SDL2_IMAGE
  inputSurface = IMG_Load(inputSDL_Map2D.c_str());
#else
  inputSurface = SDL_LoadBMP(inputSDL_Map2D.c_str());
#endif

  if (inputSurface)
  {
    TRE_LOG("Start sampling SDL texture ...");

    genMipMap(inputSurface, 0, outputSDL_Prefix + ".mipmap.0");
    genMipMap(inputSurface, 1, outputSDL_Prefix + ".mipmap.1");
    genMipMap(inputSurface, 2, outputSDL_Prefix + ".mipmap.2");

    genRotated(inputSurface, 0.10f, outputSDL_Prefix + ".rotate.10rad");

    genCubemaps(inputSurface, outputSDL_Prefix);

    genCubeFrontLevels(inputSurface, outputSDL_Prefix);

    genNormalMapFlat(inputSurface, 0.002f / 6.18f, outputSDL_Prefix);

    genNormalMapFront(inputSurface, 0.002f, outputSDL_Prefix);

    genOversamplingFlat(inputSurface, 0.002f, outputSDL_Prefix);

    SDL_FreeSurface(inputSurface);
  }
  else
  {
    TRE_LOG("Faild to load SDL texture " << inputSDL_Map2D << ", SDLerror:" << std::endl << SDL_GetError());
    status = false;
  }

  // TEST: TIFF sampling

#ifdef TRE_WITH_LIBTIFF
  TRE_LOG("Info: Testing of TIFF is enabled.");
  TIFF* tif = TIFFOpen(inputTiff_Map2D.c_str(), "r");
  if (tif)
  {
    TRE_LOG("Start sampling TIFF texture ...");

    genMipMap(tif, 2, outputTiff_Prefix + ".mipmap.2");

    genRotated(tif, 0.10f, outputTiff_Prefix + ".rotate.10rad");

    genCubeFrontLevels(tif, outputTiff_Prefix);

    genNormalMapFront(tif, 0.02f, outputTiff_Prefix);

    genOversamplingFlat(tif, 0.02f, outputTiff_Prefix);

    TIFFClose(tif);
  }
  else
  {
    TRE_LOG("Faild to load TIFF texture " << inputTiff_Map2D);
    status = false;
  }
#else
  TRE_LOG("Info: Testing of TIFF sampling is disabled.");
#endif


#ifdef TRE_WITH_SDL2_IMAGE
  IMG_Quit();
#endif
  SDL_Quit();

  TRE_LOG("Quit.");

  return (status ? 0 : -1);
}
