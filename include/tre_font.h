#ifndef FONT_H
#define FONT_H

#include "tre_utils.h"
#include "tre_texture.h"

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

  struct s_fontMap
  {
    uint                        m_fsize = 0;   ///< size of the font (in pixels)
    float                       m_hline = 0.f; ///< height of a line (in UV-space)
    std::array<s_charInfo, 256> m_charMap;     ///< char info (in UV-space). [0-127] encodes the ASCII characters. [128-255] encodes the Latin-1 extension characters.

    void read(std::istream & inbuffer);
    void write(std::ostream & outbuffer) const;
  };

  /// @name I/O
  /// @{
public:

  struct s_fontCache
  {
    SDL_Surface *m_surface = nullptr;
    s_fontMap    m_map;
  };

  static s_fontCache loadFromBMPandFNT(const std::string &filebasename); ///< Create a font-texture and a font-map from a BMP-texture and a FNT-file.
  static s_fontCache loadFromTTF(const std::string &filename, const uint fontSizePixel); ///< Create a font-texture and a font-amp from a True-Type-font and a font-size.
  static s_fontCache loadProceduralLed(const uint ptSize, const uint ptMargin); ///< Create a font-texture and a font-amp from a prodedural LED-font.

  bool load(const std::vector<s_fontCache> &fonts, const bool freeSurfaces); ///< Load (into GPU) a font with its size-variants

  static bool write(std::ostream &outbuffer, const std::vector<s_fontCache> &surfaces, const bool freeSurfaces); ///< Bake a font into binary-file.

  bool read(std::istream &inbuffer); ///< Read and load (into GPU) a font from binary-file.

  void clear(); ///< Clear font from RAM and GPU

private:
  static s_fontMap _readFNT(const std::string &fileFNT);
  static void _packTextures(const std::vector<s_fontCache> &caches, SDL_Surface* &packedTexture, std::vector<glm::ivec4> &coords); ///< naive algo (which works better when the bigger texture is put first)
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
