#ifndef FONT_H
#define FONT_H

#include "utils.h"
#include "texture.h"

#include <vector>
#include <array>
#include <string>
#include <iostream>

namespace tre {

class font
{
public:
  font() {}
  font(const font &) = delete;
  font(font &&other) : m_texture(std::move(other.m_texture)), m_fontMaps(std::move(other.m_fontMaps)) {}
  ~font() { TRE_ASSERT(m_texture.m_handle == 0); TRE_ASSERT(m_fontMaps.empty()); }

  font & operator =(const font &) = delete;
  font & operator =(font &&other)
  {
    if (this != &other)
    {
      m_texture = std::move(other.m_texture);
      m_fontMaps = std::move(other.m_fontMaps);
    }
    return *this;
  }

  struct s_charInfo
  {
    float cax, cay, cbx, cby, xoffs, yoffs, xadvance;
    int flag; ///< flag

    s_charInfo() : flag(0) {}
  };

  struct s_charKerning
  {
    char charLeft, charRight;
    uint ggTODO; // TODO
  };

  struct s_fontMap
  {
    uint                    m_fsize = 0; ///< size of the font (in pixels)
    float                       m_hline;     ///< height of a line (in UV-space)
    std::array<s_charInfo, 128> m_charMap;   ///< char info (in UV-space)

    void read(std::istream & inbuffer);
    void write(std::ostream & outbuffer) const;

    const s_charInfo &get_charMap(char c) const { TRE_ASSERT(c < m_charMap.size()); return m_charMap[c]; }
  };

  /// @name I/O
  /// @{
public:
  bool loadNewFontMapFromBMPandFNT(const std::string & filebasename); ///< Load and Create a simple font-map (the texture is loaded as an OpenGL Texture2D)
  bool loadNewFontMapFromTTF(const std::string &filename, const std::vector<uint> &fontsizesPixel);

  bool loadNewFontMapLed(const uint ptSize, const uint ptMargin = 0);

  void read(std::istream & inbuffer); ///< load font from .dat file and load it into GPU memory
  void write(std::ostream & outbuffer) const; ///< dump font into .dat file
  void clear(); ///< Clear font from RAM and GPU
private:
  static s_fontMap _readFNT(const std::string &fileFNT, uint textureWidth, uint textureHeight);
  static void _packTextures(const std::vector<SDL_Surface*> textures, SDL_Surface *&packedTexture, std::vector<glm::ivec4> &coords); ///< naive algo (which works better when the bigger texture is put first)
  /// @}

  /// @name Core
  /// @{
public:
  const texture &get_texture() const { return m_texture; }
  const s_fontMap &get_bestFontMap(uint fontSizePixel) const;
protected:
  texture m_texture; ///< Texture for the font. (The font owns the texture.)
  std::vector<s_fontMap> m_fontMaps;
  /// @}
};

} // namespace

#endif // FONT_H
