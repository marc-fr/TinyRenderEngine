#ifndef BAKER_H
#define BAKER_H

#include "utils.h"

#include <vector>
#include <string>
#include <fstream>

namespace tre {

class model;
class texture;
class font;

/**
 * @brief The baker class
 *
 */
class baker
{
public:
  baker() {}
  baker(const baker &) = delete;
  ~baker() { flushAndCloseFile(); }

  baker & operator =(const baker &) = delete;

  bool openBakedFile_forWrite(const std::string &filename, uint fileversion); ///< Opens a write-stream to a binary formatted file
  bool openBakedFile_forRead(const std::string &filename, uint &fileversion); ///< Opens a read-stream from a binary formatted file.

  std::ostream &getBlockWriteAndAdvance(); ///< Get the buffer for writing a 'block'.  openBakedFile_forWrite must be called before.
  std::istream &getBlockReadAndAdvance(); ///< Get the buffer for reading a 'block'. openBakedFile_forRead must be called before.

  bool writeBlock(const model *m); ///< Shortcut for getBlockWriteAndAdvance and bake a model
  bool readBlock(model *m); ///< Shortcut for getBlockReadAndAdvance and read a model from it

  bool writeBlock(const texture *t); ///< Shortcut for getBlockWriteAndAdvance and bake a texture
  bool readBlock(texture *t); ///< Shortcut for getBlockReadAndAdvance and read a texture from it

  bool writeBlock(const font *f); ///< Shortcut for getBlockWriteAndAdvance and bake a font
  bool readBlock(font *f); ///< Shortcut for getBlockReadAndAdvance and read a font from it

  void flushAndCloseFile(); ///< flush data and close of the files.

  std::size_t blocksCount() const { return m_blocksAdress.size(); } ///< Return the current blocks count (remaining blocks to read, or currently wrtien blocks)

protected:

  struct s_header
  {
    char     m_signature[4];
    uint32_t m_version;
    uint64_t m_footerAdress;
    uint64_t m_blockTableAdress;
    uint32_t __unused1;
    uint32_t __unused2;
  };

  std::ifstream *m_fileInDescriptor = nullptr;
  std::ofstream *m_fileOutDescriptor = nullptr;

  std::vector<uint64_t> m_blocksAdress;

  uint32_t m_version;
};

} // namespace

#endif // BAKER_H
