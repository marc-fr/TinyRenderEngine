
#include "baker.h"
#include "model.h"
#include "texture.h"
#include "font.h"
#include "windowHelper.h"

#include <iostream> // std::cout std::endl

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

bool test_bakePicture()
{
  const std::string fileBMP = TESTIMPORTPATH "resources/font_arial_88.bmp"; // texture with alpha.
  const std::string fileMAP = TESTIMPORTPATH "resources/cubemap_outside_UVcoords";
  const std::string filePNG = TESTIMPORTPATH "resources/quad.png";

  tre::baker container;

  container.openBakedFile_forWrite("test_exportimportPicture.dat", 234u);

  bool status = true;

#define _LoadAndBakeBMP(flags) status &= tre::texture::write(container.getBlockWriteAndAdvance(), tre::texture::loadTextureFromBMP(fileBMP), flags, true);

  _LoadAndBakeBMP(tre::texture::MMASK_MIPMAP);
  _LoadAndBakeBMP(tre::texture::MMASK_FORCE_NO_ALPHA);
  _LoadAndBakeBMP(tre::texture::MMASK_ALPHA_ONLY);
  _LoadAndBakeBMP(tre::texture::MMASK_COMPRESS);
  _LoadAndBakeBMP(tre::texture::MMASK_COMPRESS | tre::texture::MMASK_FORCE_NO_ALPHA);
  _LoadAndBakeBMP(tre::texture::MMASK_MIPMAP | tre::texture::MMASK_ANISOTROPIC);

#undef _LoadAndBakeBMP

  {
    const std::array<SDL_Surface*, 6> cubeFcaes = { tre::texture::loadTextureFromBMP(fileMAP + ".xpos.bmp"), tre::texture::loadTextureFromBMP(fileMAP + ".xneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(fileMAP + ".ypos.bmp"), tre::texture::loadTextureFromBMP(fileMAP + ".yneg.bmp"),
                                                    tre::texture::loadTextureFromBMP(fileMAP + ".zpos.bmp"), tre::texture::loadTextureFromBMP(fileMAP + ".zneg.bmp"), };
    status &= tre::texture::writeCube(container.getBlockWriteAndAdvance(), cubeFcaes, tre::texture::MMASK_MIPMAP, true);
  }

  { status &= tre::font::write(container.getBlockWriteAndAdvance(), { tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true); }

#ifdef TRE_WITH_SDL2_IMAGE
  { status &= tre::texture::write(container.getBlockWriteAndAdvance(), tre::texture::loadTextureFromFile(fileBMP), 0, true); }
  { status &= tre::texture::write(container.getBlockWriteAndAdvance(), tre::texture::loadTextureFromFile(filePNG), 0, true); }
#endif

#ifdef TRE_WITH_FREETYPE
  {
    std::vector<tre::font::s_fontCache> fonts  = { {},      {},      {},      {}     , {}      };
    const std::vector<unsigned>         fSizes = { 12,      24,      32,      64     , 128     };
    for (std::size_t i = 0; i < 5; ++i)
      fonts[i] = tre::font::loadFromTTF(TESTIMPORTPATH "resources/DejaVuSans.ttf", fSizes[i]);
    status &= tre::font::write(container.getBlockWriteAndAdvance(), fonts, true);
  }
#endif

  status &= tre::IsOpenGLok("test_bakePicture");

  container.flushAndCloseFile();

  return status;
}

bool test_exportimportPicture()
{
  bool status = true;

  tre::baker container;

  unsigned versionBis;
  container.openBakedFile_forRead("test_exportimportPicture.dat", versionBis);

  status &= (versionBis == 234u);
  status &= (container.blocksCount() > 5);

  { tre::texture t; status &= container.readBlock(&t); t.clear(); }
  { tre::texture t; status &= container.readBlock(&t); t.clear(); }

  status &= tre::IsOpenGLok("test_exportimportPicture - import");

  container.flushAndCloseFile();

  return status;
}

const char addData[] = "Hello, this is additional data !";

bool test_bakeMesh()
{
  bool status = true;

  // load meshes

  tre::modelStaticIndexed3D mesh3D(tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_COLOR);

  status &= mesh3D.loadfromWavefront(TESTIMPORTPATH "resources/objects.obj", TESTIMPORTPATH "resources/objects.mtl");
  if (!status) return false;

  std::vector<std::string> partOrder;
  partOrder.push_back(std::string("CubeFlat"));
  partOrder.push_back(std::string("CubeSmoothed"));
  partOrder.push_back(std::string("Icosphere"));
  partOrder.push_back(std::string("Monkey"));
  status &= mesh3D.reorganizeParts(partOrder);  // reorganize parts in

  tre::modelRaw2D mesh2D;
  mesh2D.createPart(6);
  mesh2D.fillDataRectangle(0, 0, glm::vec4(-2.f, 0.f, 2.f, 5.f), glm::vec4(0.2f, 0.3f, 0.4f, 1.f), glm::vec4(0.f,0.f,1.f,1.f));

  // bake

  tre::baker container;

  container.openBakedFile_forWrite("test_exportimportMesh.dat", 134u);

  container.writeBlock(&mesh3D);
  container.writeBlock(&mesh2D);

  // additional data
  std::ostream &bufferOut = container.getBlockWriteAndAdvance();
  if (bufferOut) bufferOut.write(addData, sizeof(addData));
  else           status = false;

  container.flushAndCloseFile();

  return status;
}

bool test_exportimportMesh()
{
  bool status = true;

  tre::baker container;
  unsigned versionBis;

  status &= container.openBakedFile_forRead("test_exportimportMesh.dat", versionBis);

  status &= (versionBis == 134u);
  status &= (container.blocksCount() == 3);

  tre::modelStaticIndexed3D mesh3D;
  tre::modelRaw2D           mesh2D;

  container.readBlock(&mesh3D);
  container.readBlock(&mesh2D);

  status &= mesh3D.partInfo(0).m_name.find("CubeFlat") != std::string::npos;
  status &= mesh2D.partInfo(0).m_size == 6;

  // additional data
  char bufferRead[33];
  std::istream &bufferIn = container.getBlockReadAndAdvance();
  if (bufferIn) bufferIn.read(bufferRead, sizeof(bufferRead));
  else          status = false;

  status &= (strcmp(addData, bufferRead) == 0);

  return status;
}

// =============================================================================

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  tre::windowHelper context;

  if (!context.SDLInit(SDL_INIT_VIDEO, "no-window", SDL_WINDOW_HIDDEN))
  {
    std::cout << "[debug] Fail to initialize SDL2 : " << SDL_GetError() << std::endl;
    return -1;
  }

#ifdef TRE_WITH_SDL2_IMAGE
  if (!context.SDLImageInit(IMG_INIT_PNG))
  {
    std::cout << "[debug] Fail to initialize SDL_Image : " << IMG_GetError() << std::endl;
    context.SDLQuit();
    return -1;
  }
#endif

  if (!context.OpenGLInit())
  {
    std::cout << SDL_GetError() << std::endl;
    context.SDLImageQuit();
    context.SDLQuit();
    return -1;
  }

  int Ntest = 0;
  int Nok = 0;
  bool status;

  {
    ++Ntest;
    status = test_bakePicture();
    if (status) { std::cout << "=== bake textures: OK" << std::endl; ++Nok; }
    else        { std::cout << "=== bake textures: ERROR" << std::endl; }

    ++Ntest;
    status = test_exportimportPicture();
    if (status) { std::cout << "=== import baked-textures: OK" << std::endl; ++Nok; }
    else        { std::cout << "=== import baked-textures: ERROR" << std::endl; }
  }

  {
    ++Ntest;
    status = test_bakeMesh();
    if (status) { std::cout << "=== bake meshes: OK" << std::endl; ++Nok; }
    else        { std::cout << "=== bake meshes: ERROR" << std::endl; }

    ++Ntest;
    status = test_exportimportMesh();
    if (status) { std::cout << "=== import baked-meshes: OK" << std::endl; ++Nok; }
    else        { std::cout << "=== import baked-meshes: ERROR" << std::endl; }
  }

  // Finalize

  std::cout << "END: " << Nok << " PASSED , " << Ntest-Nok << " FAILED" << std::endl;

  tre::IsOpenGLok("End");

  context.OpenGLQuit();
  context.SDLImageQuit();
  context.SDLQuit();

  return Nok == Ntest ? 0 : -2;
}
