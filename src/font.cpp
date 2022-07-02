#include "font.h"

#include <fstream>

#ifdef TRE_WITH_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace tre {

// ============================================================================

void font::s_fontMap::read(std::istream &inbuffer)
{
  inbuffer.read(reinterpret_cast<char*>(&m_fsize), sizeof(uint));
  inbuffer.read(reinterpret_cast<char*>(&m_hline), sizeof(float));

  for (uint ichar = 0; ichar < m_charMap.size(); ++ichar)
  {
    inbuffer.read(reinterpret_cast<char*>(&m_charMap[ichar]), sizeof(s_charInfo));
  }
}

// ----------------------------------------------------------------------------

void font::s_fontMap::write(std::ostream &outbuffer) const
{
  outbuffer.write(reinterpret_cast<const char*>(&m_fsize) , sizeof(uint));
  outbuffer.write(reinterpret_cast<const char*>(&m_hline) , sizeof(float));

  for (uint ichar = 0; ichar < m_charMap.size(); ++ichar)
  {
    outbuffer.write(reinterpret_cast<const char*>(&m_charMap[ichar]) , sizeof(s_charInfo));
  }
}

// ============================================================================

bool font::loadNewFontMapFromBMPandFNT(const std::string &filebasename)
{
  TRE_ASSERT(m_texture.m_handle == 0);
  TRE_ASSERT(m_fontMaps.empty());

  const std::string filenameBMP = filebasename + ".bmp";
  const std::string filenameFNT = filebasename + ".fnt";
  // load texture-2D
  if (!m_texture.loadNewTextureFromBMP(filenameBMP, texture::MMASK_ALPHA_ONLY))
    return false;

  s_fontMap fm = _readFNT(filenameFNT, m_texture.m_w, m_texture.m_h);
  if (fm.m_fsize == 0) // durty validity test
    return false;

  m_fontMaps.push_back(fm);
  return true;
}

// ----------------------------------------------------------------------------

bool font::loadNewFontMapFromTTF(const std::string &filename, const std::vector<uint> &fontsizesPixel)
{
#ifdef TRE_WITH_FREETYPE

  bool success = true;

  // load all resources

  std::vector<SDL_Surface*> textures;
  std::vector<s_fontMap>    fontmaps;

  FT_Library library;
  success &= FT_Init_FreeType(&library) == 0;

  FT_Face face;
  success &= FT_New_Face(library, filename.c_str(), 0, &face) == 0;

  std::vector<uint> fontsizesPixelOrdered = fontsizesPixel;
  sortAndUniqueBull(fontsizesPixelOrdered);
  // reverse
  for (uint i = 0, iStop = fontsizesPixelOrdered.size(); i < iStop / 2; ++i)
    std::swap(fontsizesPixelOrdered[i], fontsizesPixelOrdered[iStop - 1 - i]);

  for (uint fsize : fontsizesPixelOrdered)
  {
    success &= FT_Set_Pixel_Sizes(face, 0, fsize) == 0;
    if (!success) break;

    const uint texSize = (6 + (fsize * 6) / 16) * 16;

    SDL_Surface* surface = SDL_CreateRGBSurface(0, texSize, texSize, 32, 0, 0, 0, 0);
    s_fontMap fm;
    fm.m_fsize = fsize;
    fm.m_hline = float(fsize);

    const float faceDescent = float(face->descender) / float(face->height);

    TRE_ASSERT(sizeof(uint) == 4);
    uint *outPixels = reinterpret_cast<uint*>(surface->pixels);

    glm::uvec2 posGlyph = glm::uvec2(1);
    glm::uvec2 posMax = glm::uvec2(0);

    for (uint ic = 32; ic < 126; ++ic)
    {
      // Generate bitmap
      if (FT_Load_Char(face, ic, FT_LOAD_NO_BITMAP) == 0 &&
          FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0)
      {
        FT_GlyphSlot glyph = face->glyph;
        const glm::uvec2 sizeGlyph = glm::uvec2(glyph->bitmap.width, glyph->bitmap.rows);
        // advance position in the local texture
        if (posGlyph.x + sizeGlyph.x >= texSize)
        {
          posGlyph.x = 0;
          posGlyph.y = posMax.y + 2;
        }
        if (posGlyph.y + sizeGlyph.y >= texSize)
        {
          success = false;
          break;
        }
        // convert freetype mono-bitmap into SDL_RGBA8 sub-texture
        for (uint iy = 0, iyStop = sizeGlyph.y; iy < iyStop; ++iy)
        {
          for (uint ix = 0, ixStop = glyph->bitmap.width; ix < ixStop; ++ix)
          {
            const unsigned char coverage = glyph->bitmap.buffer[iy * glyph->bitmap.pitch + ix];
            // only need alpha, because the texture is loaded with texture::MMASK_ALPHA_ONLY. rgb values are set for debuging purpose
            const uint      rgba = 0x01010101 * coverage;
             outPixels[(posGlyph.y + iy) * texSize + posGlyph.x + ix] = rgba;
          }
        }
        // load fontmap (keep pixel-unit for now)
        s_charInfo &charInfo = fm.m_charMap[ic];
        charInfo.xadvance = glyph->advance.x / 64;
        charInfo.cax = float(posGlyph.x);
        charInfo.cay = float(posGlyph.y);
        charInfo.cbx = float(posGlyph.x + sizeGlyph.x);
        charInfo.cby = float(posGlyph.y + sizeGlyph.y);
        charInfo.xoffs = glyph->bitmap_left;
        charInfo.yoffs = int(fsize * (1.f + faceDescent)) - int(glyph->bitmap_top);
        charInfo.flag = 1;
        // advance (part 2)
        if (posMax.x < posGlyph.x + sizeGlyph.x) posMax.x = posGlyph.x + sizeGlyph.x + 2;
        if (posMax.y < posGlyph.y + sizeGlyph.y) posMax.y = posGlyph.y + sizeGlyph.y + 2;
        posGlyph.x += sizeGlyph.x + 2;
      }
      // Generate kering
      //if (FT_Get_Kerning() == 0)
      //{
      //}
    }

    textures.push_back(surface);
    fontmaps.push_back(fm);
  }

  success &= FT_Done_Face(face) == 0;
  success &= FT_Done_FreeType(library) == 0;

  // merge textures

  std::vector<glm::ivec4> textureCoords; // {x,y,w,h}

  SDL_Surface *finalTexture = nullptr;

  _packTextures(textures, finalTexture, textureCoords);

  const std::size_t fstart = filename.find_last_of('/');
  std::string filebasename;
  if (fstart != std::string::npos && fstart + 1 < filename.size() - 4)
    filebasename = filename.substr(fstart + 1, filename.size()-4 - (fstart + 1));
  else
    filebasename = "multifont";

#ifdef TRE_DEBUG
  const std::string ffinalBMP = filebasename + "_final.bmp";
  SDL_SaveBMP(finalTexture, ffinalBMP.c_str());
#endif

  m_texture.loadNewTextureFromSDLSurface(finalTexture, filebasename, texture::MMASK_ALPHA_ONLY);
  //m_texture.loadNewTextureWhite1x1("white TEST");
  //m_texture.loadNewTextureCheckerboard(finalTexture->w, finalTexture->h);

  const glm::vec2 invFinalSize = 1.f / glm::vec2(m_texture.m_w, m_texture.m_h);

  for (SDL_Surface *surf : textures) SDL_FreeSurface(surf);
  SDL_FreeSurface(finalTexture);

  // merge fontMap

  if (textures.size() == fontmaps.size())
  {
    for (uint it = 0, istop = fontmaps.size(); it < istop; ++it)
    {
      const glm::vec2 uvOffset = glm::vec2(textureCoords[it]) * invFinalSize;

      s_fontMap &fontmap = fontmaps[it];
      fontmap.m_hline *= invFinalSize.y;
      for (s_charInfo &ci : fontmap.m_charMap)
      {
        ci.cax = uvOffset.x + ci.cax * invFinalSize.x;
        ci.cay = uvOffset.y + ci.cay * invFinalSize.y;
        ci.cbx = uvOffset.x + ci.cbx * invFinalSize.x;
        ci.cby = uvOffset.y + ci.cby * invFinalSize.y;
        ci.xadvance = invFinalSize.x * ci.xadvance;
        ci.xoffs = invFinalSize.x * ci.xoffs;
        ci.yoffs = invFinalSize.y * ci.yoffs;
      }

      m_fontMaps.push_back(fontmap);
    }
  }
  else
  {
    success = false;
  }

  // end

  return success;
#else
  TRE_LOG("Fail to load font " << filename);
  return false;
#endif
}

// ----------------------------------------------------------------------------

struct s_rawGliph
{
  char    m_keyCode[8];
  int32_t m_yOffset;
  int32_t m_xAdvanceOffset;
};

// The LED gliph is encoded on a 7x5 map such as each row is a last bits of a char
// For example, 'A' is encoded:
// .XXX. -> 0x0E
// X...X -> 0x11
// X...X -> 0x11
// XXXXX -> 0x1F
// X...X -> 0x11
// X...X -> 0x11
// X...X -> 0x11

static const uint _kLED_size = 76;

static const s_rawGliph _kLED_gliph[_kLED_size] = { { {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 'A'}, 0, 0 },
                                                    { {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E, 'B'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x10, 0x11, 0x11, 0x0E, 'C'}, 0, 0 },
                                                    { {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E, 'D'}, 0, 0 },
                                                    { {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F, 'E'}, 0, 0 },
                                                    { {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10, 'F'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x10, 0x17, 0x11, 0x0E, 'G'}, 0, 0 },
                                                    { {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11, 'H'}, 0, 0 },
                                                    { {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E, 'I'}, 0, 0 },
                                                    { {0x03, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E, 'J'}, 0, 0 },
                                                    { {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11, 'K'}, 0, 0 },
                                                    { {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F, 'L'}, 0, 0 },
                                                    { {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11, 'M'}, 0, 0 },
                                                    { {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 'N'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 'O'}, 0, 0 },
                                                    { {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10, 'P'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x11, 0x15, 0x13, 0x0F, 'Q'}, 0, 0 },
                                                    { {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11, 'R'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x10, 0x0E, 0x01, 0x11, 0x0E, 'S'}, 0, 0 },
                                                    { {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 'T'}, 0, 0 },
                                                    { {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E, 'U'}, 0, 0 },
                                                    { {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04, 'V'}, 0, 0 },
                                                    { {0x11, 0x11, 0x11, 0x11, 0x15, 0x15, 0x0A, 'W'}, 0, 0 },
                                                    { {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11, 'X'}, 0, 0 },
                                                    { {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 'Y'}, 0, 0 },
                                                    { {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F, 'Z'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E, '0'}, 0, 0 },
                                                    { {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E, '1'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F, '2'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E, '3'}, 0, 0 },
                                                    { {0x03, 0x05, 0x09, 0x11, 0x1F, 0x01, 0x01, '4'}, 0, 0 },
                                                    { {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E, '5'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x10, 0x1E, 0x11, 0x11, 0x0E, '6'}, 0, 0 },
                                                    { {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04, '7'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E, '8'}, 0, 0 },
                                                    { {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x11, 0x0E, '9'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0C, 0x12, 0x12, 0x12, 0x0D, 'a'}, 0, 0 },
                                                    { {0x00, 0x10, 0x10, 0x1C, 0x12, 0x12, 0x1C, 'b'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0C, 0x12, 0x10, 0x12, 0x0C, 'c'}, 0, 0 },
                                                    { {0x00, 0x02, 0x02, 0x0E, 0x12, 0x12, 0x0E, 'd'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0C, 0x12, 0x1E, 0x10, 0x0E, 'e'}, 0, 0 },
                                                    { {0x00, 0x0C, 0x10, 0x10, 0x1C, 0x10, 0x10, 'f'}, 0, -1 },
                                                    { {0x00, 0x0C, 0x12, 0x12, 0x0E, 0x02, 0x0C, 'g'}, -1, 0 },
                                                    { {0x00, 0x10, 0x10, 0x1C, 0x12, 0x12, 0x12, 'h'}, 0, 0 },
                                                    { {0x00, 0x10, 0x00, 0x10, 0x10, 0x10, 0x10, 'i'}, 0, -3 },
                                                    { {0x02, 0x00, 0x02, 0x02, 0x02, 0x12, 0x0C, 'j'}, -1, -1 },
                                                    { {0x00, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12, 'k'}, 0, 0 },
                                                    { {0x00, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 'l'}, 0, -2 },
                                                    { {0x00, 0x00, 0x0A, 0x15, 0x15, 0x11, 0x11, 'm'}, 0, 0 },
                                                    { {0x00, 0x00, 0x14, 0x1A, 0x12, 0x12, 0x12, 'n'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0C, 0x12, 0x12, 0x12, 0x0C, 'o'}, 0, 0 },
                                                    { {0x00, 0x1C, 0x12, 0x12, 0x1C, 0x10, 0x10, 'p'}, -1, 0 },
                                                    { {0x00, 0x0E, 0x12, 0x12, 0x0E, 0x02, 0x02, 'q'}, -1, 0 },
                                                    { {0x00, 0x00, 0x16, 0x18, 0x10, 0x10, 0x10, 'r'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0E, 0x10, 0x0C, 0x02, 0x1C, 's'}, 0, 0 },
                                                    { {0x00, 0x10, 0x10, 0x1C, 0x10, 0x10, 0x0C, 't'}, 0, 0 },
                                                    { {0x00, 0x00, 0x12, 0x12, 0x12, 0x12, 0x0C, 'u'}, 0, 0 },
                                                    { {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04, 'v'}, 0, 0 },
                                                    { {0x00, 0x00, 0x11, 0x11, 0x11, 0x15, 0x0A, 'w'}, 0, 0 },
                                                    { {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 'x'}, 0, 0 },
                                                    { {0x00, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04, 'y'}, -1, 0 },
                                                    { {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F, 'z'}, 0, 0 },
                                                    { {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, '.'}, 0, -1 },
                                                    { {0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x08, ','}, -1, -1 },
                                                    { {0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, ':'}, 0, -1 },
                                                    { {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00, '-'}, 0, 0 },
                                                    { {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00, '+'}, 0, 0 },
                                                    { {0x00, 0x00, 0x0A, 0x04, 0x0A, 0x00, 0x00, '*'}, 0, 0 },
                                                    { {0x01, 0x01, 0x02, 0x04, 0x08, 0x08, 0x10, '/'}, 0, 0 },
                                                    { {0x00, 0x04, 0x08, 0x10, 0x08, 0x04, 0x00, '<'}, 0, 0 },
                                                    { {0x00, 0x10, 0x08, 0x04, 0x08, 0x10, 0x00, '>'}, 0, 0 },
                                                    { {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02, '('}, 0, 0 },
                                                    { {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08, ')'}, 0, 0 },
                                                    { {0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, '!'}, 0, -2 },
                                                    { {0x0C, 0x12, 0x02, 0x04, 0x08, 0x00, 0x08, '?'}, 0, -2 },
                                                    { {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, ' '}, 0, 1 },
                                                  };

bool font::loadNewFontMapLed(const uint ptSize, const uint ptMargin)
{
  TRE_ASSERT(ptSize > 0);

  const uint ptSpan = ptSize + ptMargin;
  const uint gliphW = 5 * ptSize + 4 * ptMargin;
  const uint gliphH = 7 * ptSize + 6 * ptMargin;
  const uint spanW = gliphW + 2 * ptSize;
  const uint spanH = gliphH + 2 * ptSize;
  const uint W = spanW * 8;
  const uint H = spanH * 12;

  SDL_Surface *surf = SDL_CreateRGBSurface(0, W, H, 32, 0, 0, 0, 0);

  const glm::vec2 invFinalSize = 1.f / glm::vec2(W, H);
  const glm::vec2 gliphSize = glm::vec2(gliphW, gliphH) * invFinalSize;
  const glm::vec2 spanSize = glm::vec2(spanW, spanH) * invFinalSize;

  s_fontMap fontmap;
  fontmap.m_fsize = spanH;
  fontmap.m_hline = float(spanH) * invFinalSize.y;

  int32_t *surfPixel = static_cast<int32_t*>(surf->pixels);
  memset(surfPixel, 0, W * H * 4 /*32bits*/);

  for (uint ic = 0; ic < _kLED_size; ++ic)
  {
    s_charInfo &charInfo = fontmap.m_charMap[_kLED_gliph[ic].m_keyCode[7]];
    const char *charCode = &_kLED_gliph[ic].m_keyCode[0];

    const uint px = spanW * (ic % 8);
    const uint py = spanH * (ic / 8);

    charInfo.cax = px * invFinalSize.x;
    charInfo.cay = py * invFinalSize.y;
    charInfo.cbx = charInfo.cax + gliphSize.x;
    charInfo.cby = charInfo.cay + gliphSize.y;
    charInfo.xadvance = spanSize.x + float(_kLED_gliph[ic].m_xAdvanceOffset) * ptSize * invFinalSize.x;
    charInfo.xoffs = 0.f;
    charInfo.yoffs = -float(_kLED_gliph[ic].m_yOffset) * ptSize * invFinalSize.y;
    charInfo.flag = 1;

    for (uint iy = 0; iy < 7; ++iy)
    {
      for (uint ix = 0; ix < 5; ++ix)
      {
        const char    pxCode = charCode[iy] & (0x10 >> ix);
        const int32_t pxValue = (pxCode != 0) ? -1 /*0xFFFFFFFF*/ : 0 /*0x00000000*/;
        memset(surfPixel + W * (py + ptSpan * iy) + (px + ptSpan * ix), pxValue, ptSize * 4 /*32bits*/);
      }
      for (uint iy2 = 1; iy2 < ptSize; ++iy2)
        memcpy(surfPixel + W * (py + ptSpan * iy + iy2) + px,  surfPixel + W * (py + ptSpan * iy) + px, gliphW * 4 /*32bits*/);
    }
  }

   char textureName[16];
   std::snprintf(textureName, 15, "Led-Font-%d-%d", ptSize, ptMargin);
   textureName[15] = '\0';

#ifdef TRE_DEBUG
   const std::string ffinalBMP = std::string(textureName) + "_final.bmp";
   SDL_SaveBMP(surf, ffinalBMP.c_str());
#endif

  m_texture.loadNewTextureFromSDLSurface(surf, textureName, texture::MMASK_ALPHA_ONLY);
  m_fontMaps.push_back(fontmap);

  SDL_FreeSurface(surf);

  return true;
}

// ----------------------------------------------------------------------------

void font::read(std::istream & inbuffer)
{
  m_texture.read(inbuffer);

  uint fontMapSize;
  inbuffer.read(reinterpret_cast<char*>(&fontMapSize), sizeof(uint));

  m_fontMaps.resize(fontMapSize);

  for (s_fontMap &fm : m_fontMaps)
    fm.read(inbuffer);
}

// ----------------------------------------------------------------------------

void font::write(std::ostream & outbuffer) const
{
  m_texture.write(outbuffer);

  const uint fontMapSize = m_fontMaps.size();
  outbuffer.write(reinterpret_cast<const char*>(&fontMapSize) , sizeof(uint));

  for (const s_fontMap &fm : m_fontMaps)
    fm.write(outbuffer);
}

// ----------------------------------------------------------------------------

void font::clear()
{
  m_texture.clear();
  m_fontMaps.clear();
}


font::s_fontMap font::_readFNT(const std::string &fileFNT, uint textureWidth, uint textureHeight)
{
  s_fontMap fontMap;

  const float invW = 1.f / textureWidth;
  const float invH = 1.f / textureHeight;
  // load font data
  std::ifstream myFile(fileFNT.c_str());
  if (! myFile)
  {
    TRE_LOG("Fail to read file " << fileFNT);
    return fontMap;
  }
  // read
  std::string line;
  std::getline(myFile,line); //header info
  {
    const std::size_t posS = line.find("size=");
    if (posS != std::string::npos)
    {
      int sz;
      sscanf(&line[posS],"size=%d",&sz);
      fontMap.m_fsize = sz;
    }
  }
  std::getline(myFile,line); //header common
  {
    int lineHegiht;
    sscanf(line.data(),"common lineHeight=%d",&lineHegiht);
    fontMap.m_hline = lineHegiht * invH;
  }
  std::getline(myFile,line); //header page
  std::getline(myFile,line); //header chars
  uint nchar; sscanf(line.data(),"chars count=%d",&nchar);
  for (uint ichar = 0; ichar < nchar && std::getline(myFile,line) ;++ichar)
  {
    int id,x,y,wp,hp,xoffs,yoffs,xadvance;
    sscanf(line.data(),"char id=%d x=%d y=%d width=%d height=%d xoffset=%d yoffset=%d xadvance=%d",
                            &id,  &x,  &y,  &wp,     &hp,      &xoffs,    &yoffs,    &xadvance);
    s_charInfo tmpvalue;
    tmpvalue.cax =  x      * invW;
    tmpvalue.cbx =  (x+wp) * invW;
    tmpvalue.cay =  y      * invH;
    tmpvalue.cby =  (y+hp) * invH;
    tmpvalue.xoffs = xoffs * invW;
    tmpvalue.yoffs = yoffs * invH;
    tmpvalue.xadvance = xadvance * invW;
    if (id<0 || id>=128) continue;
    tmpvalue.flag = (id <= 32) ? 2 : 1;
    fontMap.m_charMap[id] = tmpvalue;
  }
  myFile.close();
  TRE_LOG("Text-map " << fileFNT << " loaded");
  return fontMap;
}

void font::_packTextures(const std::vector<SDL_Surface*> textures, SDL_Surface *&packedTexture, std::vector<glm::ivec4> &coords)
{
  // compute textures coords

  coords.resize(textures.size()); // {x,y,w,h}

  glm::ivec2 posBlock = glm::ivec2(0);
  glm::ivec2 coordMax = glm::ivec2(0);

  for (std::size_t it = 0, itStop = textures.size(); it < itStop; ++it)
  {
    const glm::ivec2 sizetexture = glm::ivec2(textures[it]->w, textures[it]->h);

    if (posBlock.x + sizetexture.x > coordMax.x)
    {
      posBlock.x = 0;
      posBlock.y = coordMax.y;
    }

    coords[it] = glm::ivec4(posBlock, sizetexture);

    const uint extendedWidth = (sizetexture.x & ~0x000F) + 0x0010;
    const glm::ivec2 sizeBlock = glm::ivec2(extendedWidth, sizetexture.y);

    if (coordMax.x < posBlock.x + sizetexture.x) coordMax.x = posBlock.x + sizeBlock.x;
    if (coordMax.y < posBlock.y + sizetexture.y) coordMax.y = posBlock.y + sizeBlock.y;

    posBlock.x += sizetexture.x;
  }

  packedTexture = SDL_CreateRGBSurface(0, coordMax.x, coordMax.y, 32, 0, 0, 0, 0);
  TRE_ASSERT(packedTexture != nullptr);

  for (std::size_t it = 0, istop = textures.size(); it < istop; ++it)
  {
    TRE_ASSERT(textures[it]->w == coords[it].z);
    TRE_ASSERT(textures[it]->h == coords[it].w);
    SDL_Rect srcRect = {0, 0, textures[it]->w, textures[it]->h};
    SDL_Rect dstRect = {coords[it].x, coords[it].y, coords[it].z, coords[it].w};
    SDL_BlitSurface(textures[it], &srcRect, packedTexture, &dstRect);
  }
}

// ----------------------------------------------------------------------------

const font::s_fontMap &font::get_bestFontMap(uint fontSizePixel) const
{
  TRE_ASSERT(!m_fontMaps.empty());

  uint distMatch = 0;
  int      distMin = 4096;

  for (uint imap = 0; imap < m_fontMaps.size(); ++imap)
  {
    const int distLocal = int(m_fontMaps[imap].m_fsize) - int(fontSizePixel);
    if (distLocal >= 0 && distLocal < distMin)
    {
      distMatch = imap;
      distMin = distLocal;
    }
  }

  return m_fontMaps[distMatch];
}

// ============================================================================

} // namespace
