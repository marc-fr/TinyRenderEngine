#include "tre_font.h"

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
  inbuffer.read(reinterpret_cast<char*>(&m_charMap[0]), sizeof(s_charInfo) * m_charMap.size());
}

// ----------------------------------------------------------------------------

void font::s_fontMap::write(std::ostream &outbuffer) const
{
  outbuffer.write(reinterpret_cast<const char*>(&m_fsize) , sizeof(uint));
  outbuffer.write(reinterpret_cast<const char*>(&m_hline) , sizeof(float));
  outbuffer.write(reinterpret_cast<const char*>(&m_charMap[0]) , sizeof(s_charInfo) * m_charMap.size());
}

// ============================================================================

font::s_fontCache font::loadFromBMPandFNT(const std::string &filebasename)
{
  const std::string filenameBMP = filebasename + ".bmp";
  const std::string filenameFNT = filebasename + ".fnt";

  s_fontCache ret;
  ret.m_surface = nullptr;
  ret.m_map = _readFNT(filenameFNT);
  if (ret.m_map.m_fsize != 0)
  {
   // load texture-2D
   ret.m_surface = texture::loadTextureFromBMP(filenameBMP);
  }
  return ret;
}

// ----------------------------------------------------------------------------

font::s_fontCache font::loadFromTTF(const std::string &filename, const uint fontSizePixel)
{
  s_fontCache ret;
  ret.m_surface = nullptr;
  ret.m_map.m_fsize = 0;

#ifdef TRE_WITH_FREETYPE

  FT_Error err;

  FT_Library library;
  err = FT_Init_FreeType(&library);
  if (err != 0)
  {
    const char *errMsg = FT_Error_String(err);
    TRE_LOG("font::loadFromTTF: failed to init the Free-Type library (" << (errMsg != nullptr ? errMsg : "null error") << ")");
    return ret;
  }

  FT_Face face;
  err = FT_New_Face(library, filename.c_str(), 0, &face);
  if (err != 0)
  {
    const char *errMsg = FT_Error_String(err);
    TRE_LOG("font::loadFromTTF: failed to load the font " << filename << " (" << (errMsg != nullptr ? errMsg : "null error") << ")");
    FT_Done_FreeType(library);
    return ret;
  }

  err = FT_Set_Pixel_Sizes(face, 0, fontSizePixel);
  if (err != 0)
  {
    const char *errMsg = FT_Error_String(err);
    TRE_LOG("font::loadFromTTF: failed to set the font-size of " << filename << " (" << (errMsg != nullptr ? errMsg : "null error") << ")");
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return ret;
  }

  const uint texSize = (2 + (fontSizePixel * 9) / 16) * 16; // ad-hoc formula
  ret.m_surface = SDL_CreateRGBSurface(0, texSize, texSize, 32, 0, 0, 0, 0);
  if (ret.m_surface == nullptr)
  {
    TRE_LOG("font::loadFromTTF: failed to create the SDL_Surface of size " << texSize);
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    return ret;
  }

  ret.m_map.m_fsize = fontSizePixel;
  ret.m_map.m_hline = float(fontSizePixel);

  const float faceDescent = float(face->descender) / float(face->height);

  uint *outPixels = reinterpret_cast<uint*>(ret.m_surface->pixels);

  {
    glm::uvec2 posGlyph = glm::uvec2(1);
    glm::uvec2 posMax = glm::uvec2(0);

    uint unicode = 32;

    while (unicode < 256)
    {
      // Generate bitmap
      if (FT_Load_Char(face, unicode, FT_LOAD_NO_BITMAP) == 0 &&
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
          TRE_LOG("font::loadFromTTF: not enough space to contain all glyphs with font-size = " << fontSizePixel << " and texture = " << texSize << " * " << texSize);
          break;
        }
        // convert freetype mono-bitmap into SDL_RGBA8 sub-texture
        for (uint iy = 0, iyStop = sizeGlyph.y; iy < iyStop; ++iy)
        {
          for (uint ix = 0, ixStop = glyph->bitmap.width; ix < ixStop; ++ix)
          {
            const unsigned char coverage = glyph->bitmap.buffer[iy * glyph->bitmap.pitch + ix];
            // only need alpha, because the texture is loaded with texture::MMASK_ALPHA_ONLY. rgb values are set for debuging purpose
            const uint          rgba = 0x01010101 * coverage;
             outPixels[(posGlyph.y + iy) * texSize + posGlyph.x + ix] = rgba;
          }
        }
        // load fontmap (keep pixel-unit for now)
        s_charInfo &charInfo = ret.m_map.m_charMap[unicode];
        charInfo.xadvance = float(glyph->advance.x / 64.f);
        charInfo.cax = float(posGlyph.x);
        charInfo.cay = float(posGlyph.y);
        charInfo.cbx = float(posGlyph.x + sizeGlyph.x);
        charInfo.cby = float(posGlyph.y + sizeGlyph.y);
        charInfo.xoffs = float(glyph->bitmap_left);
        charInfo.yoffs = float(fontSizePixel * (1 + faceDescent) - glyph->bitmap_top);
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
      // Advance
      ++unicode;
      if      (unicode == 127) unicode = 176; // skip until: degree sign
      else if (unicode == 177) unicode = 181; // skip until: micro sign
      else if (unicode == 182) unicode = 223; // skip until: eszett (German)
      else if (unicode == 247) ++unicode; // skip the math division sign
    }
  }

  FT_Done_Face(face);
  FT_Done_FreeType(library);

  TRE_LOG("font " << filename << " using " << fontSizePixel << " pixel-size loaded : texture-size = (" << ret.m_surface->w << " x " << ret.m_surface->h << ")");
#ifdef TRE_DEBUG
  const std::size_t fstartRaw = filename.find_last_of('/');
  const std::size_t fstart = (fstartRaw != std::string::npos) ? fstartRaw + 1 : 0;
  char textureName[32];
  std::snprintf(textureName, 31, "%s-%d.bmp", filename.substr(fstart).c_str(), fontSizePixel);
  textureName[31] = '\0';
   SDL_SaveBMP(ret.m_surface, textureName);
#endif

   return ret;
#else
  TRE_LOG("font::loadFromTTF: libTIFF not available, cannot load " << filename << ", ");
  return s_fontCache();
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

font::s_fontCache font::loadProceduralLed(const uint ptSize, const uint ptMargin)
{
  TRE_ASSERT(ptSize != 0);
  const uint ptSpan = ptSize + ptMargin;
  const uint gliphW = 5 * ptSize + 4 * ptMargin;
  const uint gliphH = 7 * ptSize + 6 * ptMargin;
  const uint spanW = gliphW + 2 * ptSize;
  const uint spanH = gliphH + 2 * ptSize;
  const uint W = spanW * 8;
  const uint H = spanH * 12;

  s_fontCache ret;

  ret.m_surface = SDL_CreateRGBSurface(0, W, H, 32, 0, 0, 0, 0);
  if (ret.m_surface == nullptr)
  {
    TRE_LOG("font::loadProceduralLed: failed to create the SDL_Surface of size " << W << " * " << H);
    return ret;
  }

  const glm::vec2 gliphSize = glm::vec2(gliphW, gliphH);
  const glm::vec2 spanSize = glm::vec2(spanW, spanH);

  ret.m_map.m_fsize = spanH;
  ret.m_map.m_hline = float(spanH);

  int32_t *surfPixel = static_cast<int32_t*>(ret.m_surface->pixels);
  memset(surfPixel, 0, W * H * 4 /*32bits*/);

  for (uint ic = 0; ic < _kLED_size; ++ic)
  {
    s_charInfo &charInfo = ret.m_map.m_charMap[_kLED_gliph[ic].m_keyCode[7]];
    const char *charCode = &_kLED_gliph[ic].m_keyCode[0];

    const uint px = spanW * (ic % 8);
    const uint py = spanH * (ic / 8);

    charInfo.cax = float(px);
    charInfo.cay = float(py);
    charInfo.cbx = charInfo.cax + gliphSize.x;
    charInfo.cby = charInfo.cay + gliphSize.y;
    charInfo.xadvance = spanSize.x + float(_kLED_gliph[ic].m_xAdvanceOffset) * ptSize;
    charInfo.xoffs = 0.f;
    charInfo.yoffs = -float(_kLED_gliph[ic].m_yOffset) * ptSize;
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

#ifdef TRE_DEBUG
  char textureName[32];
  std::snprintf(textureName, 31, "Led-Font-pt%d-margin%d.bmp", ptSize, ptMargin);
  textureName[31] = '\0';
   SDL_SaveBMP(ret.m_surface, textureName);
#endif

  return ret;
}

// ----------------------------------------------------------------------------

bool font::load(const std::vector<s_fontCache> &fonts, const bool freeSurfaces)
{
  TRE_ASSERT(!fonts.empty());

  for (const auto &cache : fonts)
  {
    if (cache.m_surface == nullptr || cache.m_map.m_fsize == 0)
    {
      TRE_LOG("font::load: failed because at least one of the font-variants is invalid.");
      return false;
    }
  }

  // merge textures

  std::vector<glm::ivec4> textureCoords; // {x,y,w,h}
  SDL_Surface *finalTexture = nullptr;

  _packTextures(fonts, finalTexture, textureCoords);

  if (freeSurfaces)
  {
    for (auto &s : fonts) SDL_FreeSurface(s.m_surface);
  }

#ifdef TRE_DEBUG
  char textureName[32];
  std::snprintf(textureName, 31, "Font-gathered-%d.bmp", int(fonts.size()));
  textureName[31] = '\0';
  SDL_SaveBMP(finalTexture, textureName);
#endif

  const bool loadSuccess = m_texture.load(finalTexture, texture::MMASK_ALPHA_ONLY, true);
  //const bool loadSuccess = m_texture.loadWhite();
  //const bool loadSuccess = m_texture.loadCheckerboard(finalTexture->w, finalTexture->h);

  const glm::vec2 invFinalSize = 1.f / glm::vec2(m_texture.m_w, m_texture.m_h);

  // merge fontMap

  m_fontMaps.resize(fonts.size());

  for (std::size_t it = 0, istop = m_fontMaps.size(); it < istop; ++it)
  {
    const glm::vec2 uvOffset = glm::vec2(textureCoords[it]) * invFinalSize;

    s_fontMap &fontmap = m_fontMaps[it];
    fontmap = fonts[it].m_map;
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
  }

  // end

  return loadSuccess;
}

// ----------------------------------------------------------------------------

#define FONT_BIN_VERSION 0x001

bool font::write(std::ostream &outbuffer, const std::vector<s_fontCache> &fonts, const bool freeSurfaces)
{
  TRE_ASSERT(!fonts.empty());

  // header
  uint header[4];
  header[0] = FONT_BIN_VERSION;
  header[1] = uint(fonts.size());
  header[2] = 0; // unused
  header[3] = 0; // unused


  for (const auto &cache : fonts)
  {
    if (cache.m_surface == nullptr || cache.m_map.m_fsize == 0)
    {
      TRE_LOG("font::write: failed because at least one of the font-variants is invalid.");
      header[1] = 0;
      outbuffer.write(reinterpret_cast<const char*>(&header) , sizeof(header));
      return false;
    }
  }

  outbuffer.write(reinterpret_cast<const char*>(&header) , sizeof(header));

  // merge textures

  std::vector<glm::ivec4> textureCoords; // {x,y,w,h}
  SDL_Surface *finalTexture = nullptr;

  _packTextures(fonts, finalTexture, textureCoords);

  if (freeSurfaces)
  {
    for (auto &s : fonts) SDL_FreeSurface(s.m_surface);
  }

  if (!tre::texture::write(outbuffer, finalTexture, texture::MMASK_ALPHA_ONLY, true)) return false;

  const glm::vec2 invFinalSize = 1.f / glm::vec2(finalTexture->w, finalTexture->h);

  // merge fontMap

  std::vector<s_fontMap> localMaps;
  localMaps.resize(fonts.size());

  for (std::size_t it = 0, istop = fonts.size(); it < istop; ++it)
  {
    const glm::vec2 uvOffset = glm::vec2(textureCoords[it]) * invFinalSize;

    s_fontMap &fontmap = localMaps[it];
    fontmap = fonts[it].m_map;
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
  }

  for (const s_fontMap &fm : localMaps) fm.write(outbuffer);

  return (outbuffer.tellp() != std::ios::pos_type(-1));
}

// ----------------------------------------------------------------------------

bool font::read(std::istream &inbuffer)
{
  uint header[4];
  inbuffer.read(reinterpret_cast<char*>(&header), sizeof(header));

  if (header[0] != FONT_BIN_VERSION)
  {
    TRE_LOG("font::read: font-bin version does not match (read " << header[0] << ", expecting " << FONT_BIN_VERSION << ")");
    return false;
  }

  const uint mapSize = header[1];

  if (mapSize == 0) return false;

  if (!m_texture.read(inbuffer)) return false;

  m_fontMaps.resize(mapSize);

  for (s_fontMap &fm : m_fontMaps) fm.read(inbuffer);

  return true;
}

#undef FONT_BIN_VERSION

// ----------------------------------------------------------------------------

void font::clear()
{
  m_texture.clear();
  m_fontMaps.clear();
}


font::s_fontMap font::_readFNT(const std::string &fileFNT)
{
  s_fontMap fontMap;

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
    fontMap.m_hline = float(lineHegiht);
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
    tmpvalue.cax =  float(x);
    tmpvalue.cbx =  float(x+wp);
    tmpvalue.cay =  float(y);
    tmpvalue.cby =  float(y+hp);
    tmpvalue.xoffs = float(xoffs);
    tmpvalue.yoffs = float(yoffs);
    tmpvalue.xadvance = float(xadvance);
    if (id<0 || id>=128) continue;
    tmpvalue.flag = (id <= 32) ? 2 : 1;
    fontMap.m_charMap[id] = tmpvalue;
  }
  myFile.close();
  TRE_LOG("Text-map " << fileFNT << " loaded");
  return fontMap;
}

// ----------------------------------------------------------------------------

void font::_packTextures(const std::vector<s_fontCache> &caches, SDL_Surface *&packedTexture, std::vector<glm::ivec4> &coords)
{
  TRE_ASSERT(!caches.empty());
  TRE_ASSERT(packedTexture == nullptr);

  // sort still needed ?
  /*
  std::vector<uint> fontsizesPixelOrdered = fontsizesPixel;
  sortAndUniqueBull(fontsizesPixelOrdered);
  // reverse
  for (uint i = 0, iStop = fontsizesPixelOrdered.size(); i < iStop / 2; ++i)
    std::swap(fontsizesPixelOrdered[i], fontsizesPixelOrdered[iStop - 1 - i]);
  */

  // compute textures coords

  coords.resize(caches.size()); // {x,y,w,h}

  glm::ivec2 posBlock = glm::ivec2(0);
  glm::ivec2 coordMax = glm::ivec2(0);

  for (std::size_t it = 0, itStop = caches.size(); it < itStop; ++it)
  {
    const glm::ivec2 sizetexture = glm::ivec2(caches[it].m_surface->w, caches[it].m_surface->h);

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

  for (std::size_t it = 0, istop = caches.size(); it < istop; ++it)
  {
    TRE_ASSERT(caches[it].m_surface->w == coords[it].z);
    TRE_ASSERT(caches[it].m_surface->h == coords[it].w);
    SDL_Rect srcRect = {0, 0, caches[it].m_surface->w, caches[it].m_surface->h};
    SDL_Rect dstRect = {coords[it].x, coords[it].y, coords[it].z, coords[it].w};
    SDL_BlitSurface(caches[it].m_surface, &srcRect, packedTexture, &dstRect);
  }
}

// ----------------------------------------------------------------------------

const font::s_fontMap &font::get_bestFontMap(uint fontSizePixel) const
{
  TRE_ASSERT(!m_fontMaps.empty());

  std::size_t distMatch = 0;
  int         distMin = 0;

  // get the max first
  for (std::size_t imap = 0; imap < m_fontMaps.size(); ++imap)
  {
    const int fi = m_fontMaps[imap].m_fsize;
    if (fi > distMin)
    {
      distMatch = imap;
      distMin = fi;
    }
  }

  // get the best match
  for (std::size_t imap = 0; imap < m_fontMaps.size(); ++imap)
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
