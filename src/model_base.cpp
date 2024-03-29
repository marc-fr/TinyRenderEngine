#include "tre_model.h"
#include "tre_model_tools.h"

#include <fstream>

namespace tre {

// s_partInfo =================================================================

bool s_partInfo::read(std::istream &inbuffer)
{
  uint header[4]; // {namesize, sizeof(uint), m_size, m_offset}
  inbuffer.read(reinterpret_cast<char*>(&header[0]), sizeof(header));
  TRE_ASSERT(header[1] == sizeof(uint));
  m_size = header[2];
  m_offset = header[3];
  if (header[0] > 0)
  {
    char *tmpname = new char[header[0]];
    inbuffer.read(tmpname, sizeof(char) * header[0]);
    m_name = std::string(tmpname, header[0]);
    delete[] tmpname;
  }
  m_bbox.read(inbuffer);
  return true;
}

bool s_partInfo::write(std::ostream &outbuffer) const
{
  uint header[4]; // {namesize, sizeof(uint), m_size, m_offset}
  header[0] = uint(m_name.size());
  header[1] = sizeof(uint);
  header[2] = uint(m_size); TRE_ASSERT(m_size <= std::numeric_limits<uint>::max());
  header[3] = uint(m_offset); TRE_ASSERT(m_offset <= std::numeric_limits<uint>::max());
  outbuffer.write(reinterpret_cast<const char*>(&header[0]), sizeof(header));
  if (!m_name.empty()) outbuffer.write(m_name.c_str(), sizeof(char) * m_name.size());
  m_bbox.write(outbuffer);
  return true;
}

// model: partition ===========================================================

std::size_t model::getPartWithName(const std::string &matchname) const
{
  for (std::size_t ip=0;ip<m_partInfo.size();++ip)
  {
    if (m_partInfo[ip].m_name.find(matchname) != std::string::npos )
      return ip;
  }
  return std::size_t(-1); // invalid
}

bool model::reorganizeParts(const std::vector<std::string> &matchnames)
{
  bool status = true;
  std::vector<s_partInfo> newdrawInfo;
  for (std::size_t in = 0; in < matchnames.size(); ++in)
  {
    int nFound = 0;
    for (std::size_t ip=0;ip<m_partInfo.size();++ip)
    {
      if (m_partInfo[ip].m_name.find( matchnames[in] ) != std::string::npos )
      {
        if (nFound++ == 0) newdrawInfo.push_back(m_partInfo[ip]);
      }
    }
    if (nFound==0)
    {
      status = false;
      TRE_LOG("model::reorganizeParts: part " << matchnames[in] << " not found in parts");
    }
    if (nFound>1)
    {
      status = false;
      TRE_LOG("model::reorganizeParts: " << matchnames[in] << " has multiple matches");
    }
  }
  m_partInfo = newdrawInfo;
  return status;
}

void model::movePart(std::size_t ipart, std::size_t dstIndex)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  TRE_ASSERT(dstIndex < m_partInfo.size());
  if (ipart != dstIndex)
    std::swap(m_partInfo[ipart], m_partInfo[dstIndex]);
}

void model::mergeParts(std::size_t ipart, std::size_t jpart)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  TRE_ASSERT(jpart < m_partInfo.size());
  TRE_ASSERT(ipart != jpart);
  const std::size_t ipartBeg = m_partInfo[ipart].m_offset;
  const std::size_t ipartEnd = ipartBeg + m_partInfo[ipart].m_size;
  const std::size_t jpartBeg = m_partInfo[jpart].m_offset;
  const std::size_t jpartEnd = jpartBeg + m_partInfo[jpart].m_size;

  s_partInfo &partOUT = m_partInfo[ipart];

  partOUT.m_bbox = m_partInfo[ipart].m_bbox + m_partInfo[jpart].m_bbox;
  partOUT.m_name = "(" + m_partInfo[ipart].m_name + " + " + m_partInfo[jpart].m_name + ")";

  if (ipartEnd == jpartBeg || m_partInfo[jpart].m_size == 0) // case without moving data
  {
    partOUT.m_size += m_partInfo[jpart].m_size;
  }
  else if (jpartEnd == ipartBeg || m_partInfo[ipart].m_size == 0) // case without moving data
  {
    partOUT.m_size += m_partInfo[jpart].m_size;
    partOUT.m_offset = m_partInfo[jpart].m_offset;
  }
  else // case with moving data
  {
    const std::size_t ipartFreeSpace = getFreeSpaceAfterPart(ipart);
    const std::size_t jpartFreeSpace = getFreeSpaceAfterPart(jpart);
    const bool ipartCanGrow = m_partInfo[jpart].m_size <= ipartFreeSpace;
    const bool jpartCanGrow = m_partInfo[ipart].m_size <= jpartFreeSpace;

    // choose a part to grow
    const bool keepI = ipartCanGrow && (!jpartCanGrow || m_partInfo[ipart].m_size > m_partInfo[jpart].m_size);
    const std::size_t partKeep = keepI ? ipart : jpart;
    const std::size_t partKeep_prevSize = m_partInfo[partKeep].m_size;
    resizePart(partKeep, m_partInfo[ipart].m_size + m_partInfo[jpart].m_size);

    // move the other part
    const std::size_t partMove = keepI ? jpart : ipart;
    if (m_layout.m_indexCount == 0)
      m_layout.copyVertex(m_partInfo[partMove].m_offset, m_partInfo[partMove].m_size, m_partInfo[partKeep].m_offset + partKeep_prevSize);
    else
      m_layout.copyIndex(m_partInfo[partMove].m_offset, m_partInfo[partMove].m_size, m_partInfo[partKeep].m_offset + partKeep_prevSize);

    // save new location
    partOUT.m_offset = m_partInfo[partKeep].m_offset;
    partOUT.m_size = m_partInfo[partKeep].m_size;
  }

  // remove old part
  m_partInfo.erase( m_partInfo.begin() + jpart);
}

void model::mergeAllParts()
{
  if (m_partInfo.size()==0) return;
  defragmentParts(); // compact the buffer-pages
  std::vector<s_partInfo> newdrawInfo(1);
  std::size_t totalsize = 0;
  s_boundbox totalbox = m_partInfo[0].m_bbox;
  for (std::size_t ip=0; ip < m_partInfo.size(); ++ip) totalsize += m_partInfo[ip].m_size;
  for (std::size_t ip=1; ip < m_partInfo.size(); ++ip) totalbox += m_partInfo[ip].m_bbox;
  newdrawInfo[0].m_size   = totalsize;
  newdrawInfo[0].m_offset = 0;
  newdrawInfo[0].m_bbox       = totalbox;
  m_partInfo = newdrawInfo;
}

void model::defragmentParts()
{
  // if the model is not-indexed, then move the vertex-data
  // if the model is indexed, then move the index-data (and keep the vertex-data as it is)

  const std::size_t nparts = m_partInfo.size();

  for (std::size_t ipart = 0; ipart < nparts; ++ipart)
  {
    s_partInfo & part = m_partInfo[ipart];
    const std::size_t partOffset = ipart == 0 ? 0 : m_partInfo[ipart - 1].m_offset + m_partInfo[ipart - 1].m_size;
    const std::size_t partOffsetEnd = partOffset + part.m_size;

    if (partOffset == part.m_offset) continue; // the part is already in place. jump to the next one.
    if (part.m_size == 0)
    {
      part.m_offset = partOffset;
      continue; // the part is empty. jump to the next one.
    }

    // clear buffer plage [partOffset, partOffset + part.size] and move the part in this plage.
    while (true)
    {
      std::size_t slotPart = std::size_t(-1);

      for (std::size_t jpart = ipart + 1; jpart < nparts; ++jpart)
      {
        const s_partInfo & other = m_partInfo[jpart];
        if (other.m_size == 0)
          continue;
        if (other.m_offset < partOffsetEnd)
        {
          slotPart = jpart;
          break;
        }
      }

      if (slotPart == std::size_t(-1))
        break;

      TRE_ASSERT(slotPart > ipart);
      s_partInfo & partToMove = m_partInfo[slotPart];
      // move the part "slotPart" elsewhere
      int rpart = -1;
      for (std::size_t kpart = ipart; kpart < nparts; ++kpart)
      {
        if (m_partInfo[kpart].m_offset + m_partInfo[kpart].m_size < partOffsetEnd)
          continue;
        const std::size_t freeSpaceAfterK = getFreeSpaceAfterPart(kpart);
        if (partToMove.m_size <= freeSpaceAfterK)
        {
          rpart = kpart;
          break;
        }
      }
      TRE_ASSERT(rpart != -1);
      const std::size_t dstOffset = m_partInfo[rpart].m_offset + m_partInfo[rpart].m_size;
      // move the part
      if (m_layout.m_indexCount > 0)
      {
        reserveIndex(dstOffset + partToMove.m_size);
        m_layout.copyIndex(partToMove.m_offset, partToMove.m_size, dstOffset);
      }
      else
      {
        reserveVertex(dstOffset + partToMove.m_size);
        m_layout.copyVertex(partToMove.m_offset, partToMove.m_size, dstOffset);
      }
      partToMove.m_offset = dstOffset;
    }


    if (part.m_offset < partOffsetEnd) // overlapping
    {
      TRE_ASSERT(partOffsetEnd > partOffset);
      const std::size_t copyMaxSize = partOffsetEnd - partOffset;
      std::size_t copyDone = 0;
      while (copyDone < part.m_size)
      {
#define myMin(a,b) ((a) < (b) ? (a) : (b))
        const std::size_t copySize = myMin(part.m_size - copyDone, copyMaxSize);
        if (m_layout.m_indexCount > 0) m_layout.copyIndex(part.m_offset + copyDone, copySize, partOffset + copyDone);
        else                           m_layout.copyVertex(part.m_offset + copyDone, copySize, partOffset + copyDone);
        copyDone += copySize;
      }
    }
    else // simple copy
    {
      if (m_layout.m_indexCount > 0) m_layout.copyIndex(part.m_offset, part.m_size, partOffset);
      else                           m_layout.copyVertex(part.m_offset, part.m_size, partOffset);
    }
    part.m_offset = partOffset;

  }

  const std::size_t lastOffset = m_partInfo.back().m_offset + m_partInfo.back().m_size;
  if (m_layout.m_indexCount > 0) resizeIndex(lastOffset);
  else                           resizeVertex(lastOffset);
}

// model: Layout operations ==================================================

std::size_t model::copyPart(std::size_t ipart, std::size_t pcount)
{
  TRE_ASSERT(m_layout.m_indexCount == 0); // this is the non-indexed version.
  TRE_ASSERT(ipart + pcount <= m_partInfo.size());

  if (pcount > 1)
    defragmentParts(); // be sure that the part-allocation space is contiguous

  const std::size_t    newpartId = m_partInfo.size();
  const std::string newpartName = std::string("copy of ") + m_partInfo[ipart].m_name;
  m_partInfo.emplace_back(newpartName);

  const std::size_t vertexCurrCount = m_layout.m_vertexCount;
  std::size_t vertexAddCount = 0;
  for (std::size_t ip = ipart; ip < ipart + pcount; ++ip) vertexAddCount += m_partInfo[ip].m_size;

  m_partInfo.back().m_offset = vertexCurrCount;
  m_partInfo.back().m_size = vertexAddCount;

  if (vertexAddCount == 0)
    return newpartId;

  reserveVertex(vertexCurrCount + vertexAddCount);

  m_layout.copyVertex(m_partInfo[ipart].m_offset, vertexAddCount, vertexCurrCount);

  // b-box
  s_boundbox bbox = m_partInfo[ipart].m_bbox;
  for (std::size_t ip = ipart + 1; ip < ipart + pcount; ++ip) bbox += m_partInfo[ip].m_bbox;
  m_partInfo.back().m_bbox = bbox;

  return newpartId;
}

void model::resizePart(std::size_t ipart, std::size_t count)
{
  TRE_ASSERT(m_layout.m_indexCount == 0); // this is the non-indexed version.
  TRE_ASSERT(ipart < m_partInfo.size());

  s_partInfo & partNew = m_partInfo[ipart];

  if (count <= partNew.m_size)
  {
    partNew.m_size = count;
    if (count == 0)
      partNew.m_offset = 0;
    return;
  }

  const s_partInfo partOld = m_partInfo[ipart]; // copy

  const std::size_t neededGrow = count - partOld.m_size;

  // get the free-space after the part itself

  if (partOld.m_size > 0 && getFreeSpaceAfterPart(ipart) >= neededGrow) // there is enough place left after the part.
  {
    partNew.m_size = count;
    reserveVertex(partNew.m_offset + partNew.m_size);
    return;
  }

  // find a free-slot (at the begin)

  const std::size_t freeSpaceAtBegin = getFreeSpaceAtBegin();

  if (freeSpaceAtBegin >= count)
  {
    partNew.m_offset = 0;
    partNew.m_size = count;
    reserveVertex(partNew.m_offset + partNew.m_size);
    // move existing
    m_layout.copyVertex(partOld.m_offset, partOld.m_size, partNew.m_offset);
    return;
  }

  // find a free-slot

  std::size_t slotT = std::size_t(-1);
  std::size_t slotT_freeSpace = SIZE_MAX;
  std::size_t slotFallback = std::size_t(-1);

  for (std::size_t iP = 0; iP < m_partInfo.size(); ++iP)
  {
    const std::size_t freeSpace = getFreeSpaceAfterPart(iP);
    if (freeSpace == SIZE_MAX)
    {
      slotFallback = iP;
    }
    else if (freeSpace >= count && freeSpace < slotT_freeSpace)
    {
      slotT = iP;
      slotT_freeSpace = freeSpace;
    }
  }

  const std::size_t slot = slotT != std::size_t(-1) ? slotT : slotFallback;
  TRE_ASSERT(slot != std::size_t(-1));

  {
    partNew.m_offset = m_partInfo[slot].m_offset + m_partInfo[slot].m_size;
    partNew.m_size = count;
    reserveVertex(partNew.m_offset + partNew.m_size);
    // move existing
    m_layout.copyVertex(partOld.m_offset, partOld.m_size, partNew.m_offset);
  }
}

void model::transformPart(std::size_t ipart, const glm::mat4 &transform)
{
  TRE_ASSERT(ipart<m_partInfo.size());
  m_layout.transform(m_partInfo[ipart].m_offset, m_partInfo[ipart].m_size, transform);
  m_partInfo[ipart].m_bbox = m_partInfo[ipart].m_bbox.transform(transform);
}

void model::computeBBoxPart(std::size_t ipart)
{
  TRE_ASSERT(ipart<m_partInfo.size());
  s_partInfo & part = m_partInfo[ipart];

  if (part.m_size == 0)
  {
    part.m_bbox = s_boundbox();
    return;
  }

  if (m_layout.m_indexCount == 0)
  {
    // direct access
    if (m_layout.m_positions.m_size == 2)
    {
      part.m_bbox = s_boundbox(glm::vec3(m_layout.m_positions.get<glm::vec2>(part.m_offset), 0.f));
      for (std::size_t iv = 1; iv < part.m_size; ++iv)
        part.m_bbox.addPointInBox(glm::vec3(m_layout.m_positions.get<glm::vec2>(part.m_offset + iv), 0.f));
      return;
    }
    else if (m_layout.m_positions.m_size == 3)
    {
      part.m_bbox = s_boundbox(m_layout.m_positions.get<glm::vec3>(part.m_offset));
      for (std::size_t iv = 1; iv < part.m_size; ++iv)
        part.m_bbox.addPointInBox(m_layout.m_positions.get<glm::vec3>(part.m_offset + iv));
      return;
    }
    TRE_FATAL("invalid m_layout.m_positions size");
  }
  else
  {
    // indirect access
    if (m_layout.m_positions.m_size == 2)
    {
      const GLuint iv0 = m_layout.m_index[part.m_offset];
      part.m_bbox = s_boundbox(glm::vec3(m_layout.m_positions.get<glm::vec2>(iv0), 0.f));
      for (std::size_t id = 1; id < part.m_size; ++id)
      {
        const GLuint iv = m_layout.m_index[part.m_offset + id];
        part.m_bbox.addPointInBox(glm::vec3(m_layout.m_positions.get<glm::vec2>(iv), 0.f));
      }
      return;
    }
    else if (m_layout.m_positions.m_size == 3)
    {
      const GLuint iv0 = m_layout.m_index[part.m_offset];
      part.m_bbox = s_boundbox(m_layout.m_positions.get<glm::vec3>(iv0));
      for (std::size_t id = 1; id < part.m_size; ++id)
      {
        const GLuint iv = m_layout.m_index[part.m_offset + id];
        part.m_bbox.addPointInBox(m_layout.m_positions.get<glm::vec3>(iv));
      }
      return;
    }
    TRE_FATAL("invalid m_layout.m_positions size");
  }
}

std::size_t model::decimatePart(std::size_t ipart, float threshold, const bool lightAlgo)
{
  const std::size_t Nind0 = m_layout.m_indexCount;
  const std::size_t Nver0 = m_layout.m_vertexCount;
  TRE_ASSERT(Nind0 != 0); // connectivity is needed.
  TRE_ASSERT(Nver0 != 0); // undefined behavior

  // create new partition
  TRE_ASSERT(ipart < m_partInfo.size());
  const std::size_t inewpart = m_partInfo.size();
  m_partInfo.push_back(s_partInfo());
  const s_partInfo & srcpart = m_partInfo[ipart];
  s_partInfo & dstpart = m_partInfo.back();

  dstpart.m_name = "decimate(" + srcpart.m_name + ")";
  dstpart.m_bbox = srcpart.m_bbox;

  // allocate index-data
  std::size_t last = 0; // last taken-position
  for (s_partInfo & p : m_partInfo)
  {
    std::size_t lastLocal = p.m_offset + p.m_size;
    if (lastLocal > last) last = lastLocal;
  }
  dstpart.m_offset = last;
  dstpart.m_size = srcpart.m_size;
  reserveIndex(dstpart.m_offset + dstpart.m_size);

  // copy index-data
  m_layout.copyIndex(srcpart.m_offset, srcpart.m_size, dstpart.m_offset);

  // apply decimate algo 1
  {
    const std::size_t newPartSize = modelTools::decimateKeepVertex(m_layout, dstpart, threshold);
    TRE_ASSERT(newPartSize <= dstpart.m_size);
    dstpart.m_size = newPartSize;
  }

  if (lightAlgo) return inewpart; // exit here for the "light-algo"

  // prepare copy of vertex-data and patch index values
  std::size_t vCount = 0;
  std::vector<std::size_t> vNewToOld;
  {
    vNewToOld.reserve(dstpart.m_size);
    std::vector<int> vertex2index(m_layout.m_vertexCount, -1);
    for (std::size_t ind = 0; ind < dstpart.m_size; ++ind)
    {
      const GLuint curV = m_layout.m_index[dstpart.m_offset + ind];
      if (vertex2index[curV] == -1)
      {
        vertex2index[curV] = Nver0 + vCount++;
        vNewToOld.push_back(curV);
      }
      m_layout.m_index[dstpart.m_offset + ind] = vertex2index[curV];
    }
    TRE_ASSERT(vNewToOld.size() == vCount);
  }

  // allocate and copy vertex-data
  {
    reserveVertex(Nver0 + vCount);
    for (std::size_t iV = 0; iV < vCount; ++iV)
    {
      m_layout.copyVertex(vNewToOld[iV], 1, Nver0 + iV);
    }
  }

  // apply decimate algo 2
  {
    const std::size_t newPartSize = modelTools::decimateChangeVertex(m_layout, dstpart, threshold);
    TRE_ASSERT(newPartSize <= dstpart.m_size);
    dstpart.m_size = newPartSize;
  }

  return inewpart;
}

void model::transform(const glm::mat4 &tr)
{
  m_layout.transform(tr);
  for (s_partInfo &info : m_partInfo)
    info.m_bbox = info.m_bbox.transform(tr);
}

std::size_t model::getFreeSpaceAtBegin() const
{
  std::size_t freeSpace = SIZE_MAX;
  for (const s_partInfo &part : m_partInfo)
  {
    if (part.m_size == 0)
      continue;
    if (part.m_offset == 0)
      return 0;
    freeSpace = (part.m_offset < freeSpace) ? part.m_offset : freeSpace;
  }
  return freeSpace;
}

std::size_t model::getFreeSpaceAfterPart(std::size_t ipart) const
{
  std::size_t freeSpace = SIZE_MAX;
  TRE_ASSERT(ipart < m_partInfo.size());
  const s_partInfo &partRef = m_partInfo[ipart];
  for (const s_partInfo &part : m_partInfo)
  {
    if (&part == &partRef)
      continue; // itself
    if (part.m_size == 0)
      continue;
    if (part.m_offset < partRef.m_offset)
      continue;
    if (part.m_offset == partRef.m_offset)
      return 0;
    TRE_ASSERT(part.m_offset >= partRef.m_offset + partRef.m_size);
    const std::size_t freeSpaceLocal = part.m_offset - (partRef.m_offset + partRef.m_size);
    freeSpace = (freeSpaceLocal < freeSpace) ? freeSpaceLocal : freeSpace;
  }
  return freeSpace;
}

// model: I/O ================================================================

bool model::readBase(std::istream &inbuffer)
{
  bool result = true;

  uint partInfoSize = 0;
  inbuffer.read(reinterpret_cast<char*>(&partInfoSize), sizeof(uint));

  m_partInfo.resize(partInfoSize);

  for (s_partInfo & part : m_partInfo)
    result &= part.read(inbuffer);

  return result;
}

bool model::writeBase(std::ostream &outbuffer) const
{
  bool result = true;

  const uint partInfoSize = m_partInfo.size();
  outbuffer.write(reinterpret_cast<const char*>(&partInfoSize), sizeof(uint));

  for (const s_partInfo & part : m_partInfo)
    result &= part.write(outbuffer);

  return result;
}


void model::drawcallAll(const bool bindVAO, GLenum mode) const
{
  drawcall(0, m_partInfo.size(), bindVAO, mode);
}

// modelIndexed ===============================================================

std::size_t modelIndexed::copyPart(std::size_t ipart, std::size_t pcount)
{
  // this is the indexed version.
  TRE_ASSERT(ipart + pcount < m_partInfo.size());

  if (pcount > 1)
    defragmentParts(); // be sure that the part-allocation space is contiguous

  const std::size_t    newpartId = m_partInfo.size();
  const std::string newpartName = std::string("copy of ") + m_partInfo[ipart].m_name;
  m_partInfo.emplace_back(newpartName);

  // get the index range

  const std::size_t indexSrcFirst = m_partInfo[ipart].m_offset;
  const std::size_t indexCurrCount = m_layout.m_indexCount;
  std::size_t indexAddCount = 0;
  for (std::size_t ip = ipart; ip < ipart + pcount; ++ip) indexAddCount += m_partInfo[ip].m_size;

  m_partInfo.back().m_offset = indexCurrCount;
  m_partInfo.back().m_size = indexAddCount;

  if (indexAddCount == 0)
    return newpartId;

  // get the vertex range (gore, but works)

  std::size_t vertexMin = m_layout.m_vertexCount;
  std::size_t vertexMax = 0;

  GLuint* indexPtr = m_layout.m_index.getPointer(indexSrcFirst);
  for (std::size_t ind0 = 0; ind0 < indexAddCount; ++ind0, ++indexPtr)
  {
    vertexMax = (vertexMax < *indexPtr) ? *indexPtr : vertexMax;
    vertexMin = (vertexMin > *indexPtr) ? *indexPtr : vertexMin;
  }
  TRE_ASSERT(vertexMin < vertexMax);

  // copy the vertex

  const std::size_t vertexCurrCount = m_layout.m_vertexCount;
  const std::size_t vertexAddCount = vertexMax - vertexMin + 1;

  reserveVertex(vertexCurrCount + vertexAddCount);

  m_layout.copyVertex(vertexMin, vertexAddCount, vertexCurrCount);

  // copy and shift the index

  reserveIndex(indexCurrCount + indexAddCount);

  TRE_ASSERT(vertexCurrCount > vertexMin);
  const std::size_t vertexShift = vertexCurrCount - vertexMin;
  for (std::size_t ind0 = 0; ind0 < indexAddCount; ++ind0)
  {
    const std::size_t indOld = ind0 + indexSrcFirst;
    const std::size_t indNew = ind0 + indexCurrCount;
    m_layout.m_index[indNew] = m_layout.m_index[indOld] + vertexShift;
  }

  // b-box

  s_boundbox bbox = m_partInfo[ipart].m_bbox;
  for (std::size_t ip = ipart + 1; ip < ipart + pcount; ++ip) bbox += m_partInfo[ip].m_bbox;
  m_partInfo.back().m_bbox = bbox;

  // end

  return newpartId;
}

void modelIndexed::resizePart(std::size_t ipart, std::size_t count)
{
  // this is the indexed version.
  TRE_ASSERT(ipart < m_partInfo.size());

  s_partInfo & partNew = m_partInfo[ipart];

  if (count <= partNew.m_size)
  {
    partNew.m_size = count;
    if (count == 0)
      partNew.m_offset = 0;
    return;
  }

  const s_partInfo partOld = m_partInfo[ipart]; // copy

  const std::size_t neededGrow = count - partOld.m_size;

  // get the free-space after the part itself

  if (partOld.m_size > 0 && getFreeSpaceAfterPart(ipart) >= neededGrow) // there is enough place left after the part.
  {
    partNew.m_size = count;
    reserveIndex(partNew.m_offset + partNew.m_size);
    return;
  }

  // find a free-slot (at the begin)

  const std::size_t freeSpaceAtBegin = getFreeSpaceAtBegin();

  if (freeSpaceAtBegin >= count)
  {
    partNew.m_offset = 0;
    partNew.m_size = count;
    reserveIndex(partNew.m_offset + partNew.m_size);
    // move existing
    m_layout.copyIndex(partOld.m_offset, partOld.m_size, partNew.m_offset);
    return;
  }

  // find a free-slot

  std::size_t slotT = std::size_t(-1);
  std::size_t slotT_freeSpace = SIZE_MAX;
  std::size_t slotFallback = std::size_t(-1);

  for (std::size_t iP = 0; iP < m_partInfo.size(); ++iP)
  {
    const std::size_t freeSpace = getFreeSpaceAfterPart(iP);
    if (freeSpace == SIZE_MAX)
    {
      slotFallback = iP;
    }
    else if (freeSpace >= count && freeSpace < slotT_freeSpace)
    {
      slotT = iP;
      slotT_freeSpace = freeSpace;
    }
  }

  const std::size_t slot = slotT != std::size_t(-1) ? slotT : slotFallback;
  TRE_ASSERT(slot != std::size_t(-1));

  {
    partNew.m_offset = m_partInfo[slot].m_offset + m_partInfo[slot].m_size;
    partNew.m_size = count;
    reserveIndex(partNew.m_offset + partNew.m_size);
    // move existing
    m_layout.copyIndex(partOld.m_offset, partOld.m_size, partNew.m_offset);
  }
}

std::size_t modelIndexed::createPart(std::size_t indiceCount, std::size_t vertexCount, std::size_t &firstVertex)
{
  TRE_ASSERT(indiceCount != 0);
  TRE_ASSERT(vertexCount != 0);
  const std::size_t Nver0 = get_NextAvailable_vertex();
  // create the part
  const std::size_t newPartId = m_partInfo.size();
  m_partInfo.emplace_back();
  // reserve index-data
  resizePart(newPartId, indiceCount);
  // reserve vertex-data
  reserveVertex(Nver0 + vertexCount);
  // return
  firstVertex = Nver0;
  return newPartId;
}

std::size_t modelIndexed::createPartFromIndexes(tre::span<GLuint> indices, const GLfloat *pvert)
{
  TRE_ASSERT(!indices.empty());
  // create the part
  const std::size_t newPartId = m_partInfo.size();
  m_partInfo.push_back(s_partInfo());
  const std::size_t Nver0 = get_NextAvailable_vertex();
  resizePart(newPartId, indices.size());
  s_partInfo & newPart = m_partInfo[newPartId];
  // copy and shift index data
  for (std::size_t i=0; i< indices.size(); ++i) m_IBuffer[newPart.m_offset + i] = Nver0 + indices[i]; //shift !
  // compute size of vertices to append // TODO : gore but works
  std::size_t Nmaxvert = 0;
  for (std::size_t i=0; i< indices.size(); ++i) { if (indices[i] > Nmaxvert) Nmaxvert = indices[i]; }
  if (Nmaxvert > 0) Nmaxvert++;
  // copy vertex data
  reserveVertex(Nver0 + Nmaxvert);
  if (pvert != nullptr)
  {
    TRE_ASSERT(m_layout.m_positions.m_size == 3);
    for (std::size_t v=0; v<Nmaxvert; ++v)
    {
      m_layout.m_positions[Nver0+v][0] = pvert[v*3+0];
      m_layout.m_positions[Nver0+v][1] = pvert[v*3+1];
      m_layout.m_positions[Nver0+v][2] = pvert[v*3+2];
    }
  }
  // bound-box
  if (pvert != nullptr && Nmaxvert > 0)
  {
    s_boundbox & box = newPart.m_bbox = s_boundbox(glm::vec3(pvert[0], pvert[1], pvert[2]));
    for (std::size_t v=1; v<Nmaxvert; ++v) box.addPointInBox(pvert[v*3+0],pvert[v*3+1],pvert[v*3+2]);
  }
  return newPartId;
}

std::size_t modelIndexed::createRawPart(std::size_t count)
{
  // create the part
  const std::size_t newPartId = m_partInfo.size();
  m_partInfo.emplace_back("no-name-rawPart");
  // compute index-plage and fill index
  const std::size_t indexStart = get_NextAvailable_index();
  const std::size_t vertexStart = get_NextAvailable_vertex();
  reserveIndex(indexStart + count);
  for (std::size_t i = 0; i < count; ++i)
    m_layout.m_index[indexStart + i] = vertexStart + i;
  // reserve vertex data (keep uninitialized)
  reserveVertex(vertexStart + count);
  // end
  m_partInfo[newPartId].m_offset = indexStart;
  m_partInfo[newPartId].m_size = count;
  return newPartId;
}

void modelIndexed::resizeRawPart(std::size_t ipart, std::size_t count)
{
  TRE_ASSERT(ipart < m_partInfo.size());

  s_partInfo &part = m_partInfo[ipart];

  if (count <= part.m_size)
  {
    part.m_size = count;
    return;
  }

  // need to grow: looking for new index plage and vertex plage(s)

  const std::size_t partSizeOld = part.m_size;
  const std::size_t growCount = count - part.m_size;
  const std::size_t vertexCountOld = get_NextAvailable_vertex();

  resizePart(ipart, count); // will resize (and relocate if needed) the index-buffer.

  // algo for the vertex-buffer (with index-assignement)
  // TODO !!

  reserveVertex(vertexCountOld + growCount);

  for (std::size_t i = 0; i < growCount; ++i)
    m_layout.m_index[part.m_offset + partSizeOld + i] = vertexCountOld + i;
}

void modelIndexed::resizeIndex(std::size_t count)
{
  TRE_ASSERT(m_layout.m_indexCount == m_IBuffer.size());

  m_IBuffer.resize(count);
  m_layout.m_index.m_data = m_IBuffer.data();
  m_layout.m_indexCount = count;
}

bool modelIndexed::read_IndexBuffer(std::istream & inbuffer)
{
  uint header[2]; // {indexcount, buffersize}
  inbuffer.read(reinterpret_cast<char*>(&header[0]), sizeof(header));
  resizeIndex(header[0]);
  TRE_ASSERT(m_IBuffer.size() == header[1]);
  inbuffer.read(reinterpret_cast<char*>(m_IBuffer.data()), m_IBuffer.size() * sizeof(GLuint));
  return true;
}

bool modelIndexed::write_IndexBuffer(std::ostream & outbuffer) const
{
  uint header[2]; // {indexcount, buffersize}
  header[0] = m_layout.m_indexCount; TRE_ASSERT(m_layout.m_indexCount <= std::numeric_limits<uint>::max());
  header[1] = m_IBuffer.size();
  outbuffer.write(reinterpret_cast<const char*>(&header[0]), sizeof(header));
  outbuffer.write(reinterpret_cast<const char*>(m_IBuffer.data()), m_IBuffer.size() * sizeof(GLuint));
  return true;
}

bool modelIndexed::loadIntoGPU_IndexBuffer(const bool clearCPUbuffer)
{
  reserveIndex(4);

  TRE_ASSERT(m_IBufferHandle == 0);
  TRE_ASSERT(m_IBuffer.size() == m_layout.m_indexCount);

  glGenBuffers(1, &m_IBufferHandle);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBufferHandle);
  TRE_ASSERT(m_layout.m_index.m_data != nullptr);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * m_IBuffer.size(), m_IBuffer.data(), GL_STATIC_DRAW);

  if (clearCPUbuffer)
  {
    m_IBuffer.clear();
    m_layout.m_index.m_data = nullptr;
  }

  return true;
}

void modelIndexed::updateIntoGPU_IndexBuffer()
{
  if (m_IBuffer.empty())
  {
    TRE_ASSERT(m_layout.m_indexCount == 0);
    return;
  }

  glBindVertexArray(0);

  TRE_ASSERT(m_IBufferHandle != 0);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_IBufferHandle);
  // orphenaing the previous VRAM buffer + fill data
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * m_IBuffer.size(), m_IBuffer.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,0);
}

void modelIndexed::clearGPU_IndexBuffer(const bool restoreCPUbuffer)
{
  if (m_IBufferHandle != 0) glDeleteBuffers(1, &m_IBufferHandle);
  m_IBufferHandle = 0;

  if (restoreCPUbuffer)
  {
    m_IBuffer.resize(0);
    const std::size_t countSave = m_layout.m_indexCount;
    m_layout.m_indexCount = 0;
    modelIndexed::resizeIndex(countSave);
  }
}

std::size_t modelIndexed::get_NextAvailable_vertex() const
{
  std::size_t nextVertex = 0;
  for (const s_partInfo & pi : m_partInfo)
  {
    for (std::size_t i = pi.m_offset, iStop = pi.m_offset + pi.m_size; i < iStop; ++i)
    {
      const std::size_t vert = m_layout.m_index[i] + 1;
      if (vert > nextVertex)
        nextVertex = vert;
    }
  }
  TRE_ASSERT(nextVertex <= m_layout.m_vertexCount);
  return nextVertex;
}

std::size_t modelIndexed::get_NextAvailable_index() const
{
  std::size_t nextIndex = 0;
  for (const s_partInfo & pi : m_partInfo)
  {
    const std::size_t partNext = pi.m_offset + pi.m_size;
    if (partNext > nextIndex)
      nextIndex = partNext;
  }
  TRE_ASSERT(nextIndex <= m_layout.m_indexCount);
  return nextIndex;
}

} // namespace
