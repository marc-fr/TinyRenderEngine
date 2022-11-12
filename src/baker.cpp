#include "tre_baker.h"

#include "tre_model.h"
#include "tre_texture.h"
#include "tre_font.h"
#include "tre_audio.h"

#include <fstream>

namespace tre {

// ============================================================================

static const char k_signature[4] = { 'T', 'R', 'E', 0x00 };
static const char k_blockHeader[4] = { 'B', '.', 'H', 0x00 };
static const char k_footer[4] = { 'E', 'O', 'F', 0x00 };

// ============================================================================

bool baker::openBakedFile_forWrite(const std::string & filename, uint fileversion)
{
  m_fileOutDescriptor = new std::ofstream(filename.c_str(), std::ofstream::binary);

  if (!m_fileOutDescriptor || !(*m_fileOutDescriptor))
  {
    TRE_LOG("Fail to write file " << filename);
    if (m_fileOutDescriptor)
    {
      delete m_fileOutDescriptor;
      m_fileOutDescriptor = nullptr;
    }
    return false;
  }

  std::ofstream &myFile = *m_fileOutDescriptor;

  TRE_ASSERT(fileversion != 0); // version cannot be "zero"
  m_version = fileversion;

  s_header header;
  memset(&header, 0, sizeof(s_header));
  memcpy(header.m_signature, k_signature, 4);

  myFile.write(reinterpret_cast<const char*>(& header), sizeof(s_header));

  TRE_LOG("Bake-file opened for write " << filename << " (Version=" << fileversion << ")");

  return true;
}

// ============================================================================

bool baker::openBakedFile_forRead(const std::string &filename, uint &fileversion)
{
  m_fileInDescriptor = new std::ifstream(filename.c_str(), std::ifstream::binary);

  if (!m_fileInDescriptor || !(*m_fileInDescriptor))
  {
    TRE_LOG("Fail to read file " << filename);
    if (m_fileInDescriptor)
    {
      delete m_fileInDescriptor;
      m_fileInDescriptor = nullptr;
    }
    return false;
  }

  std::ifstream &myFile = *m_fileInDescriptor;

  s_header header;
  myFile.read(reinterpret_cast<char*>(& header), sizeof(s_header));

  fileversion = header.m_version;

  // read block

  myFile.seekg(std::ifstream::pos_type(header.m_blockTableAdress));

  uint nblocks = 0;
  myFile.read(reinterpret_cast<char*>(& nblocks), sizeof(uint));
  m_blocksAdress.resize(nblocks);
  for (uint64_t &bAd : m_blocksAdress)
    myFile.read(reinterpret_cast<char*>(& bAd), sizeof(uint64_t));

  // footer

  char footer[4];
  myFile.read(footer, sizeof(footer));
  TRE_ASSERT(std::strncmp(footer, k_footer, 4) == 0);

  TRE_LOG("Bake-file opened for read " << filename <<
          " (Version=" << header.m_version << ")" <<
          " (NBlocks=" << nblocks << ")");

  return true;
}

// ============================================================================

std::ostream& baker::getBlockWriteAndAdvance()
{
  TRE_ASSERT(m_fileOutDescriptor != nullptr);

  m_blocksAdress.push_back(uint64_t(m_fileOutDescriptor->tellp()));

  TRE_ASSERT(m_blocksAdress.back() != uint64_t(-1));

  m_fileOutDescriptor->write(k_blockHeader, sizeof(k_blockHeader));

  return *m_fileOutDescriptor;
}

// ============================================================================

std::istream &baker::getBlockReadAndAdvance()
{
  TRE_ASSERT(m_fileInDescriptor != nullptr);

  TRE_ASSERT(!m_blocksAdress.empty()); // TODO: handle this properly

  m_fileInDescriptor->seekg(std::ifstream::pos_type(m_blocksAdress.back()));

  char bh[4];
  m_fileInDescriptor->read(bh, sizeof(bh));
  TRE_ASSERT(std::strncmp(bh, k_blockHeader, 4) == 0);

  m_blocksAdress.pop_back();

  return *m_fileInDescriptor;
}

// ============================================================================

void baker::flushAndCloseFile()
{
  if (m_fileOutDescriptor)
  {
    s_header header;
    memset(&header, 0, sizeof(s_header));
    memcpy(header.m_signature, k_signature, 4);
    header.m_version = m_version;
    header.m_blockTableAdress = uint64_t(m_fileOutDescriptor->tellp());
    TRE_ASSERT(header.m_blockTableAdress != uint64_t(-1));
    // write table
    const uint nBlocks = uint(m_blocksAdress.size());
    m_fileOutDescriptor->write(reinterpret_cast<const char*>(& nBlocks), sizeof(uint));
    for (uint iB = nBlocks; iB-- > 0; )
      m_fileOutDescriptor->write(reinterpret_cast<const char*>(& m_blocksAdress[iB]), sizeof(uint64_t));
    // write EOF-stamp.
    header.m_footerAdress = uint64_t(m_fileOutDescriptor->tellp());
    m_fileOutDescriptor->write(k_footer, sizeof(k_footer));
    // write header
    m_fileOutDescriptor->seekp(0);
    m_fileOutDescriptor->write(reinterpret_cast<const char*>(& header), sizeof(s_header));
    // close
    TRE_LOG("baker::flush blocks=" << m_blocksAdress.size() << " with total-size=" << int(float(header.m_footerAdress) / 1024.f / 1024.f * 10) / 10.f << " MB");
    m_blocksAdress.clear();
    m_fileOutDescriptor->close();
    delete m_fileOutDescriptor;
    m_fileOutDescriptor = nullptr;
  }

  if (m_fileInDescriptor)
  {
    // close
    m_blocksAdress.clear();
    m_fileInDescriptor->close();
    delete m_fileInDescriptor;
    m_fileInDescriptor = nullptr;
  }
}

// ============================================================================

bool baker::writeBlock(const model *m)
{
  return m->write(getBlockWriteAndAdvance());
}

bool baker::readBlock(model *m)
{
  return m->read(getBlockReadAndAdvance());
}

bool baker::writeBlock(SDL_Surface *surface, int flags, const bool freeSurface)
{
  return tre::texture::write(getBlockWriteAndAdvance(), surface, flags, freeSurface);
}

bool baker::readBlock(texture *t)
{
  return t->read(getBlockReadAndAdvance());
}

bool baker::readBlock(font *f)
{
  f->read(getBlockReadAndAdvance());
  return true;
}

bool baker::writeBlock(const soundData::s_RawSDL *s)
{
  return s->write(getBlockWriteAndAdvance());
}

bool baker::readBlock(soundData::s_RawSDL *s)
{
  return s->read(getBlockReadAndAdvance());
}

bool baker::writeBlock(const soundData::s_Opus *s)
{
  return s->write(getBlockWriteAndAdvance());
}

bool baker::readBlock(soundData::s_Opus *s)
{
  return s->read(getBlockReadAndAdvance());
}

// ============================================================================

} // namespace
