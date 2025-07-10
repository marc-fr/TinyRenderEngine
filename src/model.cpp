#include "tre_model.h"

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

void model::mergeParts(std::size_t ipart, std::size_t jpart, const bool keepEmpty_jpart)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  TRE_ASSERT(jpart < m_partInfo.size());
  TRE_ASSERT(ipart != jpart);
  const std::size_t ipartBeg = m_partInfo[ipart].m_offset;
  const std::size_t ipartEnd = ipartBeg + m_partInfo[ipart].m_size;
  const std::size_t jpartBeg = m_partInfo[jpart].m_offset;
  const std::size_t jpartEnd = jpartBeg + m_partInfo[jpart].m_size;

  m_partInfo[ipart].m_bbox = m_partInfo[ipart].m_bbox + m_partInfo[jpart].m_bbox;
  m_partInfo[ipart].m_name = "(" + m_partInfo[ipart].m_name + " + " + m_partInfo[jpart].m_name + ")";

  if (ipartEnd == jpartBeg || m_partInfo[jpart].m_size == 0) // case without moving data
  {
    m_partInfo[ipart].m_size += m_partInfo[jpart].m_size;
  }
  else if (jpartEnd == ipartBeg || m_partInfo[ipart].m_size == 0) // case without moving data
  {
    m_partInfo[ipart].m_size += m_partInfo[jpart].m_size;
    m_partInfo[ipart].m_offset = m_partInfo[jpart].m_offset;
  }
  else // case with moving data
  {
    const std::size_t ipartFreeSpace = getFreeSpaceAfterPart(ipart);
    const std::size_t jpartFreeSpace = getFreeSpaceAfterPart(jpart);
    const bool ipartCanGrow = m_partInfo[jpart].m_size <= ipartFreeSpace;
    const bool jpartCanGrow = m_partInfo[ipart].m_size <= jpartFreeSpace;

    // choose a part to grow
    const bool keepI = ipartCanGrow && (!jpartCanGrow || m_partInfo[ipart].m_size > m_partInfo[jpart].m_size);
    if (!keepI) std::swap(m_partInfo[ipart], m_partInfo[jpart]);

    const std::size_t partKeep_prevSize = m_partInfo[ipart].m_size;
    resizePart(ipart, m_partInfo[ipart].m_size + m_partInfo[jpart].m_size);

    // move the other part
    if (m_layout.m_indexCount == 0)
      m_layout.copyVertex(m_partInfo[jpart].m_offset, m_partInfo[jpart].m_size, m_partInfo[ipart].m_offset + partKeep_prevSize);
    else
      m_layout.copyIndex(m_partInfo[jpart].m_offset, m_partInfo[jpart].m_size, m_partInfo[ipart].m_offset + partKeep_prevSize);
  }

  // clear/remove old part
  if (keepEmpty_jpart) m_partInfo[jpart] = s_partInfo();
  else                 m_partInfo.erase(m_partInfo.begin() + jpart);
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

// model: Layout ==============================================================


void s_modelDataLayout::colorize(std::size_t ifirst, std::size_t icount, const glm::vec4 &unicolor) const
{
  if (icount==0) return;
  TRE_ASSERT(m_vertexCount > 0);
  bool wholeIndexedMesh = false;
  if (ifirst == 0 && icount == m_indexCount)
  {
    wholeIndexedMesh = true;
    icount = m_vertexCount;
  }

  if (m_indexCount != 0 && !wholeIndexedMesh)
  {
    TRE_ASSERT(m_index.m_data != nullptr);
    const std::size_t iend = ifirst + icount;
    TRE_ASSERT(iend <= m_indexCount);
    for (std::size_t iind = ifirst; iind < iend; ++iind)
    {
      const std::size_t ivert = m_index[iind];
      m_colors.get<glm::vec4>(ivert) = unicolor;
    }
  }
  else
  {
    TRE_ASSERT(ifirst + icount <= m_vertexCount);
    s_vertexData::iterator<glm::vec4> colorsIt = m_colors.begin<glm::vec4>(ifirst);
    for (std::size_t i = 0; i < icount; ++i)
    {
      *colorsIt++ = unicolor;
    }
  }
}

// ----------------------------------------------------------------------------

void s_modelDataLayout::colorize(const glm::vec4 &unicolor) const
{
  if (m_indexCount != 0)
    colorize(0, m_indexCount, unicolor);
  else
    colorize(0, m_vertexCount, unicolor);
}

// ----------------------------------------------------------------------------

void s_modelDataLayout::transform(std::size_t ifirst, std::size_t icount, const glm::mat4 &tr) const
{
  if (icount==0) return;
  TRE_ASSERT(m_vertexCount > 0);
  bool wholeIndexedMesh = false;
  if (ifirst == 0 && icount == m_indexCount)
  {
    wholeIndexedMesh = true;
    icount = m_vertexCount;
  }

  if (m_indexCount != 0 && !wholeIndexedMesh)
  {
    const std::size_t iend = ifirst + icount;
    TRE_ASSERT(iend <= m_indexCount);
    // naive but always works
    std::vector<bool> vertexDone(m_vertexCount,false);

    if (m_positions.m_size == 2)
    {
      for (std::size_t iind = ifirst; iind < iend; ++iind)
      {
        const std::size_t ivert = m_index[iind];
        if (!vertexDone[ivert])
        {
          vertexDone[ivert] = true;
          const glm::vec4 newP = tr * glm::vec4(m_positions.get<glm::vec2>(ivert), 0.f, 1.f);
          m_positions.get<glm::vec2>(ivert) = glm::vec2(newP);
        }
      }
    }
    else if (m_positions.m_size == 3)
    {
      for (std::size_t iind = ifirst; iind < iend; ++iind)
      {
        const std::size_t ivert = m_index[iind];
        if (!vertexDone[ivert])
        {
          vertexDone[ivert] = true;
          const glm::vec4 newP = tr * glm::vec4(m_positions.get<glm::vec3>(ivert), 1.f);
          m_positions.get<glm::vec3>(ivert) = glm::vec3(newP);
        }
      }
    }
    else
    {
      TRE_FATAL("error in mesh-layout: invalid position dimension");
    }

    if (m_normals.m_size == 3)
    {
      for (std::size_t iV = 0; iV < m_vertexCount; ++iV)
      {
        if (vertexDone[iV])
        {
          const glm::vec4 newN = tr * glm::vec4(m_normals.get<glm::vec3>(iV), 0.f);
          m_normals.get<glm::vec3>(iV) = glm::normalize(glm::vec3(newN));
        }
      }
    }

    if (m_tangents.m_size == 3)
    {
      for (std::size_t iV = 0; iV < m_vertexCount; ++iV)
      {
        if (vertexDone[iV])
        {
          const glm::vec4 newT = tr * glm::vec4(m_tangents.get<glm::vec3>(iV), 0.f);
          m_tangents.get<glm::vec3>(iV) = glm::normalize(glm::vec3(newT));
        }
      }
    }
  }
  else
  {
    TRE_ASSERT(ifirst + icount <= m_vertexCount);
    if (m_positions.m_size == 2)
    {
      s_vertexData::iterator<glm::vec2> posIt = m_positions.begin<glm::vec2>(ifirst);
      for (std::size_t i = 0; i < icount; ++i)
      {
        const glm::vec4 newP = tr * glm::vec4(*posIt, 0.f, 1.f);
        *posIt++ = glm::vec2(newP);
      }
    }
    else if (m_positions.m_size == 3)
    {
      s_vertexData::iterator<glm::vec3> posIt = m_positions.begin<glm::vec3>(ifirst);
      for (std::size_t i = 0; i < icount; ++i)
      {
        const glm::vec4 newP = tr * glm::vec4(*posIt, 1.f);
        *posIt++ = glm::vec3(newP);
      }
    }
    else
    {
      TRE_FATAL("error in mesh-layout: invalid position dimension");
    }

    if (m_normals.m_size == 3)
    {
      s_vertexData::iterator<glm::vec3> normalIt = m_normals.begin<glm::vec3>(ifirst);
      for (std::size_t i = 0; i < icount; ++i)
      {
        const glm::vec4 newN = tr * glm::vec4(*normalIt, 0.f);
        *normalIt++ = glm::normalize(glm::vec3(newN));
      }
    }

    if (m_tangents.m_size == 3)
    {
      s_vertexData::iterator<glm::vec3> tangentIt = m_tangents.begin<glm::vec3>(ifirst);
      for (std::size_t i = 0; i < icount; ++i)
      {
        const glm::vec4 newT = tr * glm::vec4(*tangentIt, 0.f);
        *tangentIt++ = glm::normalize(glm::vec3(newT));
      }
    }
  }
}

// ----------------------------------------------------------------------------

void s_modelDataLayout::transform(const glm::mat4 &tr) const
{
  if (m_indexCount != 0)
    transform(0, m_indexCount, tr);
  else
    transform(0, m_vertexCount, tr);
}

// ----------------------------------------------------------------------------

void s_modelDataLayout::copyIndex(std::size_t ifirst, std::size_t icount, std::size_t dstfirst) const
{
  if (icount == 0) return;
  TRE_ASSERT(ifirst + icount <= m_indexCount);
  TRE_ASSERT(dstfirst + icount <= m_indexCount);

  memcpy(m_index.getPointer(dstfirst),
         m_index.getPointer(ifirst),
         icount * sizeof(GLuint));
}

// ----------------------------------------------------------------------------

void s_modelDataLayout::copyVertex(std::size_t ivfirst, std::size_t ivcount, std::size_t dstfirst) const
{
  if (ivcount==0) return;
  TRE_ASSERT(ivfirst + ivcount <= m_vertexCount);
  TRE_ASSERT(dstfirst + ivcount <= m_vertexCount);

  // copy contiguous data (called "block")
  struct s_block
  {
    const GLfloat *src,*dst;
    std::size_t dim;

    static void addBlock(std::vector<s_block> & blocks, const s_vertexData & data, std::size_t ivsrc, std::size_t ivdst)
    {
      if (data.m_size == 0) return;
      TRE_ASSERT(data.m_data != nullptr);
      const GLfloat * dataSrc = data[ivsrc];
      const GLfloat * dataDst = data[ivdst];

      for (s_block & block : blocks)
      {
        const long offset = block.src - dataSrc;
        if (abs(offset) < long(block.dim))
        {
          TRE_ASSERT(block.dim == data.m_stride);
          if (offset > 0)
          {
            block.src = dataSrc;
            block.dst = dataDst;
          }
          return; // already in block
        }
      }
      blocks.push_back({dataSrc, dataDst, data.m_stride});
    }
  };

  std::vector<s_block> blocks;
  s_block::addBlock(blocks, m_positions, ivfirst, dstfirst);
  s_block::addBlock(blocks, m_normals  , ivfirst, dstfirst);
  s_block::addBlock(blocks, m_tangents , ivfirst, dstfirst);
  s_block::addBlock(blocks, m_colors   , ivfirst, dstfirst);
  s_block::addBlock(blocks, m_uvs      , ivfirst, dstfirst);

  for (const s_block & block : blocks)
    memcpy(const_cast<GLfloat*>(block.dst), block.src, block.dim * ivcount  * sizeof(GLfloat));
}

// ============================================================================

#define EPSILON 0.001f

std::size_t s_modelDataLayout::rayIntersect(std::size_t ifirst, std::size_t icount, const glm::vec3 & pos, const glm::vec3 & vecN) const
{
  TRE_ASSERT(m_indexCount > 0); // needed for connectivity
  TRE_ASSERT(m_vertexCount > 0);

  if (icount == 0) return 0;

  TRE_ASSERT(icount % 3 == 0);
  const std::size_t Ntri = icount / 3;

  std::size_t Ncross = 0;

  std::vector<bool> alreadycrossvertice(m_vertexCount,false);

  //std::cout << "[rayIntersect] P=" << pos[0]  << "," << pos[1]  << "," << pos[2]  << std::endl;
  //std::cout << "               N=" << vecN[0] << "," << vecN[1] << "," << vecN[2] << std::endl;

  for (std::size_t Itri = 0; Itri < Ntri; ++Itri)
  {
    // we need the solve : pos + t * vecN = (1-u-v) A + u B + v C
    // where (A,B,C) are the vertices of the triangle.
    // (u,v) will be the barycentric coordinates, and t the distance.
    // We define "AP" = pos - A
    const GLuint iV = m_index[ifirst + Itri * 3 + 0];
    const GLuint v1 = m_index[ifirst + Itri * 3 + 1];
    const GLuint v2 = m_index[ifirst + Itri * 3 + 2];
    const glm::vec3 pV = m_positions.get<glm::vec3>(iV);
    const glm::vec3 p1 = m_positions.get<glm::vec3>(v1);
    const glm::vec3 p2 = m_positions.get<glm::vec3>(v2);
    glm::vec3 coordUVT(0.f);
    const std::size_t hit = triangleRaytrace3D(pV, p1, p2, pos, vecN, &coordUVT);
    if (hit == 0) continue;
    // check if we already crossed vertices or edges
     if (coordUVT.x < EPSILON && coordUVT.y < EPSILON)
     {
       if (alreadycrossvertice[iV]==true) continue;
       alreadycrossvertice[iV]=true;
     }
     else if (coordUVT.x > (1.f-EPSILON) && coordUVT.y < EPSILON)
     {
       if (alreadycrossvertice[v1]==true) continue;
       alreadycrossvertice[v1]=true;
     }
     else if (coordUVT.x < EPSILON && coordUVT.y > (1.f-EPSILON))
     {
       if (alreadycrossvertice[v2]==true) continue;
       alreadycrossvertice[v2]=true;
     }
     else if (coordUVT.x < EPSILON)
     {
       const bool ishit = ( alreadycrossvertice[iV]==true || alreadycrossvertice[v2]==true );
       alreadycrossvertice[iV]=true;
       alreadycrossvertice[v2]=true;
       if (ishit==true) continue;
     }
     else if (coordUVT.y < EPSILON)
     {
       const bool ishit = ( alreadycrossvertice[iV]==true || alreadycrossvertice[v1]==true );
       alreadycrossvertice[iV]=true;
       alreadycrossvertice[v1]=true;
       if (ishit==true) continue;
     }
     else if ((coordUVT.x + coordUVT.y) > (1-EPSILON))
     {
       const bool ishit = ( alreadycrossvertice[v1]==true || alreadycrossvertice[v2]==true );
       alreadycrossvertice[v1]=true;
       alreadycrossvertice[v2]=true;
       if (ishit==true) continue;
     }
    // we have the cross !
    ++Ncross;
    /*std::cout << "               cross found with triangle ("
              << iV << "," << v1 << "," << v2 << ") (u,v,t,det)=("
              << coordu << "," << coordv << "," << coordt << "," << det << ")" << std::endl;*/
  }
  //std::cout << "               Ncross = " << Ncross << std::endl;
  return Ncross;
}

// ============================================================================

void s_modelDataLayout::clear()
{
  m_indexCount = 0;
  m_index.m_data = nullptr;

  m_vertexCount = 0;
  m_positions.m_data = nullptr;
  m_positions.m_size = 0;
  m_normals.m_data = nullptr;
  m_normals.m_size = 0;
  m_uvs.m_data = nullptr;
  m_uvs.m_size = 0;
  m_colors.m_data = nullptr;
  m_colors.m_size = 0;
  m_tangents.m_data = nullptr;
  m_tangents.m_size = 0;

  m_instanceCount = 0;
  m_instancedPositions.m_data = nullptr;
  m_instancedPositions.m_size = 0;
  m_instancedOrientations.m_data = nullptr;
  m_instancedOrientations.m_size = 0;
  m_instancedAtlasBlends.m_data = nullptr;
  m_instancedAtlasBlends.m_size = 0;
  m_instancedColors.m_data = nullptr;
  m_instancedColors.m_size = 0;
  m_instancedRotations.m_data = nullptr;
  m_instancedRotations.m_size = 0;
}

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

void modelIndexed::fillDataBox(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataBox_ISize() <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataBox_VSize() <= m_layout.m_vertexCount);

  static const std::array<GLuint, fillDataBox_ISize()> bufferInd =
  {
     0, 1, 2, 3, 0, 2, //face Z-
     4, 6, 5, 7, 6, 4, //face Z+
     8, 9,10,11, 8,10, //face X-
    12,14,13,15,14,12, //face X+
    16,18,17,19,18,16, //face Y-
    20,21,22,23,20,22, //face Y+
  };
  for (std::size_t i = 0; i < fillDataBox_ISize(); ++i)
    m_layout.m_index[offsetI + i] = GLuint(offsetV) + bufferInd[i];

  const float halfSize = edgeLength * 0.5f;

  const glm::vec3 pt000 = transform[3] - transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt001 = transform[3] - transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt010 = transform[3] - transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt011 = transform[3] - transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt100 = transform[3] + transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt101 = transform[3] + transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt110 = transform[3] + transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt111 = transform[3] + transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;

  m_layout.m_positions.get<glm::vec3>(offsetV +  0) = pt000; //face Z-
  m_layout.m_positions.get<glm::vec3>(offsetV +  1) = pt010;
  m_layout.m_positions.get<glm::vec3>(offsetV +  2) = pt110;
  m_layout.m_positions.get<glm::vec3>(offsetV +  3) = pt100;
  m_layout.m_positions.get<glm::vec3>(offsetV +  4) = pt001; //face Z+
  m_layout.m_positions.get<glm::vec3>(offsetV +  5) = pt011;
  m_layout.m_positions.get<glm::vec3>(offsetV +  6) = pt111;
  m_layout.m_positions.get<glm::vec3>(offsetV +  7) = pt101;
  m_layout.m_positions.get<glm::vec3>(offsetV +  8) = pt001; //face X-
  m_layout.m_positions.get<glm::vec3>(offsetV +  9) = pt011;
  m_layout.m_positions.get<glm::vec3>(offsetV + 10) = pt010;
  m_layout.m_positions.get<glm::vec3>(offsetV + 11) = pt000;
  m_layout.m_positions.get<glm::vec3>(offsetV + 12) = pt101; //face X+
  m_layout.m_positions.get<glm::vec3>(offsetV + 13) = pt111;
  m_layout.m_positions.get<glm::vec3>(offsetV + 14) = pt110;
  m_layout.m_positions.get<glm::vec3>(offsetV + 15) = pt100;
  m_layout.m_positions.get<glm::vec3>(offsetV + 16) = pt000; //face Y-
  m_layout.m_positions.get<glm::vec3>(offsetV + 17) = pt001;
  m_layout.m_positions.get<glm::vec3>(offsetV + 18) = pt101;
  m_layout.m_positions.get<glm::vec3>(offsetV + 19) = pt100;
  m_layout.m_positions.get<glm::vec3>(offsetV + 20) = pt010; //face Y+
  m_layout.m_positions.get<glm::vec3>(offsetV + 21) = pt011;
  m_layout.m_positions.get<glm::vec3>(offsetV + 22) = pt111;
  m_layout.m_positions.get<glm::vec3>(offsetV + 23) = pt110;

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal1xx = glm::normalize(glm::vec3(transform[0]));
    const glm::vec3 outNormalx1x = glm::normalize(glm::vec3(transform[1]));
    const glm::vec3 outNormalxx1 = glm::normalize(glm::vec3(transform[2]));
    const glm::vec3 outNormal0xx = -outNormal1xx;
    const glm::vec3 outNormalx0x = -outNormalx1x;
    const glm::vec3 outNormalxx0 = -outNormalxx1;

    m_layout.m_normals.get<glm::vec3>(offsetV +  0) = outNormalxx0; //face Z-
    m_layout.m_normals.get<glm::vec3>(offsetV +  1) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(offsetV +  2) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(offsetV +  3) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(offsetV +  4) = outNormalxx1; //face Z+
    m_layout.m_normals.get<glm::vec3>(offsetV +  5) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(offsetV +  6) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(offsetV +  7) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(offsetV +  8) = outNormal0xx; //face X-
    m_layout.m_normals.get<glm::vec3>(offsetV +  9) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 10) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 11) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 12) = outNormal1xx; //face X+
    m_layout.m_normals.get<glm::vec3>(offsetV + 13) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 14) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 15) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(offsetV + 16) = outNormalx0x; //face Y-
    m_layout.m_normals.get<glm::vec3>(offsetV + 17) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(offsetV + 18) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(offsetV + 19) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(offsetV + 20) = outNormalx1x; //face Y+
    m_layout.m_normals.get<glm::vec3>(offsetV + 21) = outNormalx1x;
    m_layout.m_normals.get<glm::vec3>(offsetV + 22) = outNormalx1x;
    m_layout.m_normals.get<glm::vec3>(offsetV + 23) = outNormalx1x;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataBox_VSize(); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
     const glm::vec4 t0 = glm::abs(halfSize * transform[0]);
     const glm::vec4 t1 = glm::abs(halfSize * transform[1]);
     const glm::vec4 t2 = glm::abs(halfSize * transform[2]);
     const glm::vec4 tm = t0 + t1 + t2;
     const glm::vec4 bboxMin = transform[3] - tm;
     const glm::vec4 bboxMax = transform[3] + tm;
     m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
     m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataBoxWireframe(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataBoxWireframe_ISize() <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataBoxWireframe_VSize() <= m_layout.m_vertexCount);

  static const std::array<GLuint, fillDataBoxWireframe_ISize()> bufferInd =
  {
    0, 1, 1, 3, 3, 2, 2, 0, //face X-
    4, 5, 5, 7, 7, 6, 6, 4, //face X+
    0, 1, 1, 5, 5, 4, 4, 0, //face Y-
    3, 2, 2, 6, 6, 7, 7, 3, //face Y+
    0, 2, 2, 6, 6, 4, 4, 0, //face Z-
    3, 7, 7, 5, 5, 1, 1, 3, //face Z+
  };
  for (std::size_t i = 0; i < fillDataBoxWireframe_ISize(); ++i)
    m_layout.m_index[offsetI + i] = GLuint(offsetV) + bufferInd[i];

  const float halfSize = edgeLength * 0.5f;

  const glm::vec3 pt000 = transform[3] - transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt001 = transform[3] - transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt010 = transform[3] - transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt011 = transform[3] - transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt100 = transform[3] + transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt101 = transform[3] + transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt110 = transform[3] + transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt111 = transform[3] + transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;

  m_layout.m_positions.get<glm::vec3>(offsetV +  0) = pt000;
  m_layout.m_positions.get<glm::vec3>(offsetV +  1) = pt001;
  m_layout.m_positions.get<glm::vec3>(offsetV +  2) = pt010;
  m_layout.m_positions.get<glm::vec3>(offsetV +  3) = pt011;
  m_layout.m_positions.get<glm::vec3>(offsetV +  4) = pt100;
  m_layout.m_positions.get<glm::vec3>(offsetV +  5) = pt101;
  m_layout.m_positions.get<glm::vec3>(offsetV +  6) = pt110;
  m_layout.m_positions.get<glm::vec3>(offsetV +  7) = pt111;

  if (m_layout.m_normals.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataBoxWireframe_VSize(); ++v)
      m_layout.m_normals.get<glm::vec3>(offsetV + v) = glm::vec3(0.f);
  }

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataBoxWireframe_VSize(); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
     const glm::vec4 t0 = glm::abs(halfSize * transform[0]);
     const glm::vec4 t1 = glm::abs(halfSize * transform[1]);
     const glm::vec4 t2 = glm::abs(halfSize * transform[2]);
     const glm::vec4 tm = t0 + t1 + t2;
     const glm::vec4 bboxMin = transform[3] - tm;
     const glm::vec4 bboxMax = transform[3] + tm;
     m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
     m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataCone(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, float heigth, uint subdiv, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataCone_ISize(subdiv) <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataCone_VSize(subdiv) <= m_layout.m_vertexCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const float angle = u * 2.f * M_PI / subdiv;
    m_layout.m_positions.get<glm::vec3>(offsetV + u) = center + axisT1 * (radius * std::cos(angle)) + axisT2 * (radius * std::sin(angle));
  }
  m_layout.m_positions.get<glm::vec3>(offsetV + subdiv + 0) = center;
  m_layout.m_positions.get<glm::vec3>(offsetV + subdiv + 1) = center + heigth * axisUp;

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(offsetI);

  const std::size_t indBase = offsetV + subdiv;
  const std::size_t indTop  = offsetV + subdiv + 1;

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const std::size_t ind0 = offsetV + u;
    const std::size_t ind1 = (u == subdiv - 1) ? offsetV : ind0 + 1;

    *(indicePtr++) = ind0;
    *(indicePtr++) = ind1;
    *(indicePtr++) = indBase;

    *(indicePtr++) = ind0;
    *(indicePtr++) = ind1;
    *(indicePtr++) = indTop;
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(offsetI) + fillDataCone_ISize(subdiv));

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataCone_VSize(subdiv); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
    const glm::vec4 t0 = glm::abs(radius * transform[0]);
    const glm::vec4 t2 = glm::abs(radius * transform[2]);
    const glm::vec4 tm = t0 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
    m_partInfo[ipart].m_bbox.addPointInBox(center + heigth * axisUp);
  }
}

void modelIndexed::fillDataDisk(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radiusOut, float radiusIn, uint subdiv, const glm::vec4 & color)
{
  TRE_ASSERT(radiusIn <= radiusOut);

  TRE_ASSERT(offsetI + fillDataDisk_ISize(radiusIn, subdiv) <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataDisk_VSize(radiusIn, subdiv) <= m_layout.m_vertexCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const float angle = u * 2.f * M_PI / subdiv;
    m_layout.m_positions.get<glm::vec3>(offsetV + u) = center + axisT1 * (radiusOut * std::cos(angle)) + axisT2 * (radiusOut * std::sin(angle));
  }
  if (radiusIn == 0.f)
  {
    m_layout.m_positions.get<glm::vec3>(offsetV + subdiv) = center;
  }
  else
  {
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const float angle = (u + 0.5f) * 2.f * M_PI / subdiv;
      m_layout.m_positions.get<glm::vec3>(offsetV + subdiv + u) = center + axisT1 * (radiusIn * std::cos(angle)) + axisT2 * (radiusIn * std::sin(angle));
    }
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(offsetI);

  if (radiusIn == 0.f)
  {
    const std::size_t indCenter = offsetV + subdiv;
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const std::size_t indA = offsetV + u;
      const std::size_t indB = (u == subdiv - 1) ? offsetV : indA + 1;

      *(indicePtr++) = indCenter;
      *(indicePtr++) = indA;
      *(indicePtr++) = indB;
    }
  }
  else
  {
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const std::size_t indOutA = offsetV + u;
      const std::size_t indOutB = (u == subdiv - 1) ? offsetV : indOutA + 1;

      const std::size_t indInA = indOutA + subdiv;
      const std::size_t indInB = indOutB + subdiv;

      *(indicePtr++) = indInA;
      *(indicePtr++) = indOutA;
      *(indicePtr++) = indOutB;

      *(indicePtr++) = indInA;
      *(indicePtr++) = indInB;
      *(indicePtr++) = indOutB;
    }
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(offsetI) + fillDataDisk_ISize(radiusIn, subdiv));

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal = glm::normalize(glm::vec3(transform[1]));
    for (std::size_t i = 0; i < fillDataDisk_VSize(radiusIn, subdiv); ++i)
      m_layout.m_normals.get<glm::vec3>(offsetV + i) = outNormal;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataDisk_VSize(radiusIn, subdiv); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
    const glm::vec3 t0 = glm::abs(radiusOut * axisT1);
    const glm::vec3 t2 = glm::abs(radiusOut * axisT2);
    const glm::vec3 tm = t0 + t2;
    const glm::vec3 bboxMin = center - tm;
    const glm::vec3 bboxMax = center + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataTorus(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radiusMain, float radiusIn, uint subdiv_main, uint subdiv_in, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataTorus_ISize(subdiv_main, subdiv_in) <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataTorus_VSize(subdiv_main, subdiv_in) <= m_layout.m_vertexCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t uM = 0; uM < subdiv_main; ++uM)
  {
    const float angleM = uM * M_PI / (subdiv_main - 1.f);
    const glm::vec3 axisM = - std::cos(angleM) * axisT2 + std::sin(angleM) * axisT1;
    for (std::size_t uI = 0; uI < subdiv_in; ++uI)
    {
      const float angleI = uI * 2.f * M_PI / subdiv_in;
      m_layout.m_positions.get<glm::vec3>(offsetV + uM * subdiv_in + uI) =
        center + (radiusMain + radiusIn * std::cos(angleI)) * axisM + (radiusIn * std::sin(angleI)) * axisUp;
    }
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(offsetI);

  for (std::size_t uM = 0; uM < subdiv_main; ++uM)
  {
    const std::size_t indMA = offsetV + subdiv_in * uM;
    const std::size_t indMB = (uM == subdiv_main - 1) ? offsetV + subdiv_in * uM : indMA + subdiv_in;

    for (std::size_t uI = 0; uI < subdiv_in; ++uI)
    {
      const std::size_t uIA = uI;
      const std::size_t uIB = (uI == subdiv_in - 1) ? 0 : uIA + 1;

      *(indicePtr++) = indMA + uIA;
      *(indicePtr++) = indMA + uIB;
      *(indicePtr++) = indMB + uIB;

      *(indicePtr++) = indMB + uIB;
      *(indicePtr++) = indMB + uIA;
      *(indicePtr++) = indMA + uIA;
    }
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(offsetI) + fillDataTorus_ISize(subdiv_main, subdiv_in));

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents for now. TODO
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataTorus_VSize(subdiv_main, subdiv_in); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
    const glm::vec4 t0 = glm::abs((radiusMain + radiusIn) * transform[0]);
    const glm::vec4 t1 = glm::abs(radiusIn                * transform[1]);
    const glm::vec4 t2 = glm::abs((radiusMain + radiusIn) * transform[2]);
    const glm::vec4 tm = t0 + t1 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataSquare(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataSquare_ISize() <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataSquare_VSize() <= m_layout.m_vertexCount);

  const float halfSizeOu = edgeLength * 0.5f;
  const float halfSizeIn = - halfSizeOu;

  const glm::vec3 squarePt00 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeIn);
  const glm::vec3 squarePt01 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeOu);
  const glm::vec3 squarePt10 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeIn);
  const glm::vec3 squarePt11 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeOu);

  m_layout.m_positions.get<glm::vec3>(offsetV + 0) = squarePt00;
  m_layout.m_positions.get<glm::vec3>(offsetV + 1) = squarePt01;
  m_layout.m_positions.get<glm::vec3>(offsetV + 2) = squarePt11;
  m_layout.m_positions.get<glm::vec3>(offsetV + 3) = squarePt10;

  m_layout.m_index[offsetI + 0] = offsetV + 0;
  m_layout.m_index[offsetI + 1] = offsetV + 1;
  m_layout.m_index[offsetI + 2] = offsetV + 2;

  m_layout.m_index[offsetI + 3] = offsetV + 2;
  m_layout.m_index[offsetI + 4] = offsetV + 3;
  m_layout.m_index[offsetI + 5] = offsetV + 0;

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal = glm::normalize(glm::vec3(transform[1]));
    m_layout.m_normals.get<glm::vec3>(offsetV + 0) = outNormal;
    m_layout.m_normals.get<glm::vec3>(offsetV + 1) = outNormal;
    m_layout.m_normals.get<glm::vec3>(offsetV + 2) = outNormal;
    m_layout.m_normals.get<glm::vec3>(offsetV + 3) = outNormal;
  }

  if (m_layout.m_tangents.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_tangents.m_size == 4);
    const glm::vec4 outTangent = glm::vec4(glm::normalize(glm::vec3(transform[0])), 1.f);
    m_layout.m_tangents.get<glm::vec4>(offsetV + 0) = outTangent;
    m_layout.m_tangents.get<glm::vec4>(offsetV + 1) = outTangent;
    m_layout.m_tangents.get<glm::vec4>(offsetV + 2) = outTangent;
    m_layout.m_tangents.get<glm::vec4>(offsetV + 3) = outTangent;
  }

  if (m_layout.m_uvs.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_uvs.m_size == 2);
    m_layout.m_uvs.get<glm::vec2>(offsetV + 0) = glm::vec2(0.f, 1.f);
    m_layout.m_uvs.get<glm::vec2>(offsetV + 1) = glm::vec2(0.f, 0.f);
    m_layout.m_uvs.get<glm::vec2>(offsetV + 2) = glm::vec2(1.f, 0.f);
    m_layout.m_uvs.get<glm::vec2>(offsetV + 3) = glm::vec2(1.f, 1.f);
  }

  if (m_layout.m_colors.m_size != 0)
  {
    m_layout.m_colors.get<glm::vec4>(offsetV + 0) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 1) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 2) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 3) = color;
  }

  {
    const glm::vec4 t0 = glm::abs(halfSizeOu * transform[0]);
    const glm::vec4 t2 = glm::abs(halfSizeOu * transform[2]);
    const glm::vec4 tm = t0 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataSquareWireframe(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float edgeLength, const glm::vec4 & color)
{
  TRE_ASSERT(offsetI + fillDataSquareWireframe_ISize() <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataSquareWireframe_VSize() <= m_layout.m_vertexCount);

  const float halfSizeOu = edgeLength * 0.5f;
  const float halfSizeIn = - halfSizeOu;

  const glm::vec3 squarePt00 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeIn);
  const glm::vec3 squarePt01 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeOu);
  const glm::vec3 squarePt10 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeIn);
  const glm::vec3 squarePt11 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeOu);

  m_layout.m_positions.get<glm::vec3>(offsetV + 0) = squarePt00;
  m_layout.m_positions.get<glm::vec3>(offsetV + 1) = squarePt01;
  m_layout.m_positions.get<glm::vec3>(offsetV + 2) = squarePt11;
  m_layout.m_positions.get<glm::vec3>(offsetV + 3) = squarePt10;

  m_layout.m_index[offsetI + 0] = offsetV + 0;
  m_layout.m_index[offsetI + 1] = offsetV + 1;
  m_layout.m_index[offsetI + 2] = offsetV + 1;
  m_layout.m_index[offsetI + 3] = offsetV + 2;
  m_layout.m_index[offsetI + 4] = offsetV + 2;
  m_layout.m_index[offsetI + 5] = offsetV + 3;
  m_layout.m_index[offsetI + 6] = offsetV + 3;
  m_layout.m_index[offsetI + 7] = offsetV + 0;

  TRE_ASSERT(m_layout.m_normals.m_size == 0);
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  if (m_layout.m_colors.m_size != 0)
  {
    m_layout.m_colors.get<glm::vec4>(offsetV + 0) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 1) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 2) = color;
    m_layout.m_colors.get<glm::vec4>(offsetV + 3) = color;
  }

  {
    const glm::vec4 t0 = glm::abs(halfSizeOu * transform[0]);
    const glm::vec4 t2 = glm::abs(halfSizeOu * transform[2]);
    const glm::vec4 tm = t0 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataTube(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, float heigth, bool closed, uint subdiv_r, uint subdiv_h, const glm::vec4 & color)
{
  TRE_ASSERT(subdiv_r > 0);
  TRE_ASSERT(subdiv_h > 0);

  TRE_ASSERT(offsetI + fillDataTube_ISize(closed, subdiv_r, subdiv_h) <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataTube_VSize(closed, subdiv_r, subdiv_h) <= m_layout.m_vertexCount);

  // vertex

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];
  const float hStart = - heigth * 0.5f;
  const float hStep  = heigth / subdiv_h;

  for (std::size_t ir = 0; ir < subdiv_r; ++ir)
  {
    const float angle = ir * 2.f * M_PI / subdiv_r;
    const glm::vec3 axisRd = std::cos(angle) * axisT1 + std::sin(angle) * axisT2;
    for (std::size_t ih = 0; ih < subdiv_h + 1; ++ih)
    {
      m_layout.m_positions.get<glm::vec3>(offsetV + ir * (subdiv_h + 1) + ih) = center + axisRd * radius + (hStart + ih * hStep) * axisUp;
    }
  }
  if (closed)
  {
    const std::size_t vertexBase1 = offsetV + subdiv_r * (subdiv_h + 1);
    m_layout.m_positions.get<glm::vec3>(vertexBase1 + 0) = center + -heigth * 0.5f * axisUp;
    m_layout.m_positions.get<glm::vec3>(vertexBase1 + 1) = center +  heigth * 0.5f * axisUp;
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(offsetI);

  for (std::size_t ir = 0; ir < subdiv_r; ++ir)
  {
    const std::size_t base0 = offsetV + ir * (subdiv_h + 1);
    const std::size_t base1 = (ir == subdiv_r - 1) ? offsetV : base0 + subdiv_h + 1;

    for (std::size_t ih = 0; ih < subdiv_h; ++ih)
    {
      *(indicePtr++) = base0 + ih;
      *(indicePtr++) = base0 + ih + 1;
      *(indicePtr++) = base1 + ih + 1;

      *(indicePtr++) = base1 + ih + 1;
      *(indicePtr++) = base1 + ih;
      *(indicePtr++) = base0 + ih;
    }
  }
  if (closed)
  {
    // TODO ...
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(offsetI) + fillDataTube_ISize(closed, subdiv_r, subdiv_h));

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents for now. TODO
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataTube_VSize(closed, subdiv_r, subdiv_h); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
    const glm::vec3 t0 = glm::abs(radius * axisT1);
    const glm::vec3 t1 = glm::abs(hStart * axisUp);
    const glm::vec3 t2 = glm::abs(radius * axisT2);
    const glm::vec3 tm = t0 + t1 + t2;
    const glm::vec3 bboxMin = center - tm;
    const glm::vec3 bboxMax = center + tm;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
}

void modelIndexed::fillDataUvtrisphere(std::size_t ipart, std::size_t offsetI, std::size_t offsetV, const glm::mat4 &transform, float radius, uint subdiv_u, uint subdiv_v, const glm::vec4 & color)
{
  subdiv_u = (subdiv_u & ~0x1);
  TRE_ASSERT(subdiv_u > 0);
  TRE_ASSERT(subdiv_v > 0);

  TRE_ASSERT(offsetI + fillDataUvtrisphere_ISize(subdiv_u, subdiv_v) <= partInfo(ipart).m_size);
  offsetI += partInfo(ipart).m_offset;
  TRE_ASSERT(offsetV + fillDataUvtrisphere_VSize(subdiv_u, subdiv_v) <= m_layout.m_vertexCount);

  // vertex ...

  TRE_ASSERT(m_layout.m_normals.m_size == 0 || m_layout.m_normals.m_size == 3);
  const bool generateNormals = m_layout.m_normals.m_size == 3;
  glm::vec3 dummyNormal;
  if (!generateNormals)
  {
    m_layout.m_normals.m_data = reinterpret_cast<GLfloat*>(&dummyNormal);
    m_layout.m_normals.m_size = 3;
    m_layout.m_normals.m_stride = 0;
  }

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  std::size_t vertexCurrent = offsetV;
  for (std::size_t u = 0; u < subdiv_u / 2 ; ++u)
  {
    const float cosu0 = std::cos((u * 2    ) * M_PI * 2.f / subdiv_u);
    const float sinu0 = std::sin((u * 2    ) * M_PI * 2.f / subdiv_u);
    const float cosu1 = std::cos((u * 2 + 1) * M_PI * 2.f / subdiv_u);
    const float sinu1 = std::sin((u * 2 + 1) * M_PI * 2.f / subdiv_u);

    const glm::vec3 axisU0 = cosu0 * axisT1 + sinu0 * axisT2;
    const glm::vec3 axisU1 = cosu1 * axisT1 + sinu1 * axisT2;

    for (std::size_t v = 0; v < subdiv_v; ++v)
    {
      const float cosv =   std::sin((v + 1) * M_PI / (subdiv_v + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - std::cos((v + 1) * M_PI / (subdiv_v + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU0 + sinv * axisUp;
      m_layout.m_normals.get<glm::vec3>(vertexCurrent) = outN;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
    for (std::size_t v = 0; v < subdiv_v + 1; ++v)
    {
      const float cosv =   std::sin((v + 0.5f) * M_PI / (subdiv_v + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - std::cos((v + 0.5f) * M_PI / (subdiv_v + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU1 + sinv * axisUp;
      m_layout.m_normals.get<glm::vec3>(vertexCurrent) = outN;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
  }
  m_layout.m_normals.get<glm::vec3>(vertexCurrent) = -axisUp;
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center - radius * axisUp;
  m_layout.m_normals.get<glm::vec3>(vertexCurrent) = axisUp;
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * axisUp;
  TRE_ASSERT(vertexCurrent - offsetV == fillDataUvtrisphere_VSize(subdiv_u, subdiv_v));

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(offsetI);

  const std::size_t baseClose = offsetV + (subdiv_u/2) * (subdiv_v * 2 + 1);
  for (std::size_t u = 0; u < subdiv_u/2; ++u)
  {
    const std::size_t base0 = offsetV + u * (subdiv_v * 2 + 1);
    const std::size_t base1 = base0 + subdiv_v;
    const std::size_t base0Next = (u == subdiv_u/2 - 1) ? offsetV : base1 + subdiv_v + 1;
    const std::size_t base1Next = base0Next + subdiv_v;
    for (std::size_t v = 0; v < subdiv_v - 1; ++v)
    {
      *(indicePtr++) = base0 + v;
      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0 + v + 1;

      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0Next + v;
      *(indicePtr++) = base0Next + v + 1;
    }
    for (std::size_t v = 0; v < subdiv_v; ++v)
    {
      *(indicePtr++) = base0 + v;
      *(indicePtr++) = base1 + v;
      *(indicePtr++) = base1 + v + 1;

      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base1 + v;
      *(indicePtr++) = base0Next + v;
    }
    {
      *(indicePtr++) = base1;
      *(indicePtr++) = base1Next;
      *(indicePtr++) = base0Next;

      *(indicePtr++) = base1 + subdiv_v;
      *(indicePtr++) = base0Next + subdiv_v - 1;
      *(indicePtr++) = base1Next + subdiv_v;
    }
    {
      *(indicePtr++) = baseClose;
      *(indicePtr++) = base1Next;
      *(indicePtr++) = base1;

      *(indicePtr++) = baseClose + 1;
      *(indicePtr++) = base1 + subdiv_v;
      *(indicePtr++) = base1Next + subdiv_v;
    }
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(offsetI) + fillDataUvtrisphere_ISize(subdiv_u, subdiv_v));

  if (!generateNormals)
  {
    m_layout.m_normals.m_data = nullptr;
    m_layout.m_normals.m_size = 0;
    m_layout.m_normals.m_stride = 0;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents (for now ?)

  if (m_layout.m_colors.m_size != 0)
  {
    for (std::size_t v = 0; v < fillDataUvtrisphere_VSize(subdiv_u, subdiv_v); ++v)
      m_layout.m_colors.get<glm::vec4>(offsetV + v) = color;
  }

  {
    const glm::vec3 rr = glm::vec3(std::abs(radius));
    const glm::vec3 bboxMin = center - rr;
    const glm::vec3 bboxMax = center + rr;
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMin);
    m_partInfo[ipart].m_bbox.addPointInBox(bboxMax);
  }
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

// Bind helper ================================================================

static void _bind_vertexAttribPointer_float(const s_modelDataLayout::s_vertexData &vertexData, GLuint argShader, const float *bufferOrigin)
{
  TRE_ASSERT(vertexData.m_size <= 4);
  if (vertexData.m_size > 0)
  {
    TRE_ASSERT(vertexData.m_data != nullptr);
    glEnableVertexAttribArray(argShader);
    glVertexAttribPointer(argShader, vertexData.m_size, GL_FLOAT, GL_FALSE, vertexData.m_stride * sizeof(GLfloat), reinterpret_cast<void*>((vertexData.m_data - bufferOrigin) * sizeof(GLfloat)));
    glVertexAttribDivisor(argShader, 0);
  }
  else
  {
    glDisableVertexAttribArray(argShader);
  }
}

static void _bind_instancedAttribPointer_float(const s_modelDataLayout::s_instanceData &instancedData, GLuint argShader, const float *bufferOrigin)
{
  if (instancedData.m_size == 12)
  {
    TRE_ASSERT(instancedData.m_data != nullptr);
    glEnableVertexAttribArray(argShader);
    glVertexAttribPointer(argShader, 4, GL_FLOAT, GL_FALSE, instancedData.m_stride * sizeof(GLfloat), reinterpret_cast<void*>((instancedData.m_data - bufferOrigin + 0) * sizeof(GLfloat)));
    glVertexAttribDivisor(argShader, GLuint(instancedData.m_divisor));
    ++argShader;
    glEnableVertexAttribArray(argShader);
    glVertexAttribPointer(argShader, 4, GL_FLOAT, GL_FALSE, instancedData.m_stride * sizeof(GLfloat), reinterpret_cast<void*>((instancedData.m_data - bufferOrigin + 4) * sizeof(GLfloat)));
    glVertexAttribDivisor(argShader, GLuint(instancedData.m_divisor));
    ++argShader;
    glEnableVertexAttribArray(argShader);
    glVertexAttribPointer(argShader, 4, GL_FLOAT, GL_FALSE, instancedData.m_stride * sizeof(GLfloat), reinterpret_cast<void*>((instancedData.m_data - bufferOrigin + 8) * sizeof(GLfloat)));
    glVertexAttribDivisor(argShader, GLuint(instancedData.m_divisor));

  }
  else if (instancedData.m_size > 0)
  {
    TRE_ASSERT(instancedData.m_size <= 4);
    TRE_ASSERT(instancedData.m_data != nullptr);
    glEnableVertexAttribArray(argShader);
    glVertexAttribPointer(argShader, instancedData.m_size, GL_FLOAT, GL_FALSE, instancedData.m_stride * sizeof(GLfloat), reinterpret_cast<void*>((instancedData.m_data - bufferOrigin) * sizeof(GLfloat)));
    glVertexAttribDivisor(argShader, GLuint(instancedData.m_divisor));
  }
  else
  {
    glDisableVertexAttribArray(argShader);
  }
}

// modelRaw2D =================================================================

void modelRaw2D::resizeVertex(std::size_t count)
{
  TRE_ASSERT(m_layout.m_vertexCount * 8 == m_VBuffer.size());

  m_layout.m_vertexCount = count;
  m_VBuffer.resize(count * 8);

  m_layout.m_positions.m_data = m_VBuffer.data();
  m_layout.m_positions.m_size = 2;
  m_layout.m_positions.m_stride = 8;

  m_layout.m_uvs.m_data = m_VBuffer.data() + 2;
  m_layout.m_uvs.m_size = 2;
  m_layout.m_uvs.m_stride = 8;

  m_layout.m_colors.m_data = m_VBuffer.data() + 4;
  m_layout.m_colors.m_size = 4;
  m_layout.m_colors.m_stride = 8;
}

std::size_t modelRaw2D::createPart(std::size_t partSize)
{
  m_partInfo.push_back(s_partInfo());
  resizePart(m_partInfo.size() - 1, partSize);
  return m_partInfo.size() - 1;
}

void modelRaw2D::fillDataFromRAM(std::size_t ipart, std::size_t offset, std::size_t count, GLfloat* __restrict vbuffer)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  s_partInfo & part = m_partInfo[ipart];

  TRE_ASSERT(m_layout.m_vertexCount * 8 == m_VBuffer.size());
  const std::size_t first = part.m_offset + offset;
  const std::size_t last = first + count;
  TRE_ASSERT(last <= part.m_size);

  memcpy(m_VBuffer.data() + 8 * first, vbuffer, count * 8);

  // update bounds
  TRE_ASSERT(m_layout.m_positions.m_size == 2);

  s_boundbox & box = part.m_bbox;
  for (std::size_t v = first; v < last; v++)
  {
    box.addPointInBox(m_layout.m_positions[v][0],m_layout.m_positions[v][1]);
  }
}

void modelRaw2D::fillDataRectangle(std::size_t ipart, std::size_t offset, const glm::vec4 & AxAyBxBy, const glm::vec4 & color, const glm::vec4 & AuAvBuBv)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  s_partInfo & part = m_partInfo[ipart];

  const std::size_t first = part.m_offset + offset;
  TRE_ASSERT(offset + 6 <= part.m_size);

  TRE_ASSERT(m_layout.m_positions.isMatching(2, 8));
  TRE_ASSERT(m_layout.m_uvs.isMatching(2, 8));
  TRE_ASSERT(m_layout.m_colors.isMatching(4, 8));

  float * __restrict vdata = m_layout.m_positions.m_data + 8 * first;

  *vdata++ = AxAyBxBy.x; *vdata++ = AxAyBxBy.y; *vdata++ = AuAvBuBv.x; *vdata++ = AuAvBuBv.y;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;
  *vdata++ = AxAyBxBy.z; *vdata++ = AxAyBxBy.y; *vdata++ = AuAvBuBv.z; *vdata++ = AuAvBuBv.y;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;
  *vdata++ = AxAyBxBy.z; *vdata++ = AxAyBxBy.w; *vdata++ = AuAvBuBv.z; *vdata++ = AuAvBuBv.w;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;

  *vdata++ = AxAyBxBy.x; *vdata++ = AxAyBxBy.y; *vdata++ = AuAvBuBv.x; *vdata++ = AuAvBuBv.y;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;
  *vdata++ = AxAyBxBy.z; *vdata++ = AxAyBxBy.w; *vdata++ = AuAvBuBv.z; *vdata++ = AuAvBuBv.w;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;
  *vdata++ = AxAyBxBy.x; *vdata++ = AxAyBxBy.w; *vdata++ = AuAvBuBv.x; *vdata++ = AuAvBuBv.w;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;

  // update bounds
  s_boundbox & box = part.m_bbox;
  box.addPointInBox(AxAyBxBy.x, AxAyBxBy.y);
  box.addPointInBox(AxAyBxBy.z, AxAyBxBy.w);
}

void modelRaw2D::fillDataLine(std::size_t ipart, std::size_t offset, const glm::vec2 &ptA, const glm::vec2 &ptB, const glm::vec4 &color)
{
  fillDataLine(ipart, offset, ptA.x, ptA.y, ptB.x, ptB.y, color);
}

void modelRaw2D::fillDataLine(std::size_t ipart, std::size_t offset, float Ax, float Ay, float Bx, float By, const glm::vec4 &color)
{
  TRE_ASSERT(ipart < m_partInfo.size());
  s_partInfo & part = m_partInfo[ipart];

  TRE_ASSERT(m_layout.m_vertexCount * 8 == m_VBuffer.size());
  const std::size_t first = part.m_offset + offset;
  TRE_ASSERT(offset + 2 <= part.m_size);

  TRE_ASSERT(m_layout.m_positions.isMatching(2, 8));
  TRE_ASSERT(m_layout.m_uvs.isMatching(2, 8));
  TRE_ASSERT(m_layout.m_colors.isMatching(4, 8));

  float * __restrict vdata = m_layout.m_positions.m_data + 8 * first;

  *vdata++ = Ax; *vdata++ = Ay; *vdata++ = 0.f; *vdata++ = 0.f;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;
  *vdata++ = Bx; *vdata++ = By; *vdata++ = 0.f; *vdata++ = 0.f;
  memcpy(vdata, glm::value_ptr(color), 4 * 4); vdata += 4;

  // update bounds
  s_boundbox & box = part.m_bbox;
  box.addPointInBox(Ax, By);
  box.addPointInBox(Bx, By);
}

bool modelRaw2D::read(std::istream &inbuffer)
{
  if (!readBase(inbuffer)) return false;

  uint header[2]; // { vertexCount, bufferSize }
  inbuffer.read(reinterpret_cast<char*>(&header[0]), sizeof(header));
  resizeVertex(header[0]);
  TRE_ASSERT(header[1] == m_VBuffer.size());
  inbuffer.read(reinterpret_cast<char*>(m_VBuffer.data()), m_VBuffer.size() * sizeof(GLfloat));

  return true;
}

bool modelRaw2D::write(std::ostream &outbuffer) const
{
  bool result = true;

  result &= writeBase(outbuffer);

  uint header[2]; // { vertexCount, bufferSize }
  header[0] = m_layout.m_vertexCount; TRE_ASSERT(m_layout.m_vertexCount <= std::numeric_limits<uint>::max());
  header[1] = m_VBuffer.size();
  outbuffer.write(reinterpret_cast<const char*>(&header[0]), sizeof(header));
  outbuffer.write(reinterpret_cast<const char*>(m_VBuffer.data()), m_VBuffer.size() * sizeof(GLfloat));

  return result;
}

bool modelRaw2D::loadIntoGPU()
{
  reserveVertex(16);

  TRE_ASSERT(m_VAO == 0);

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  loadIntoGPU_VertexBuffer();

  glBindVertexArray(0);

  return true;
}

void modelRaw2D::updateIntoGPU()
{
  TRE_ASSERT(m_VBufferHandle != 0);
  TRE_ASSERT(!m_VBuffer.empty());

  glBindVertexArray(0);

  glBindBuffer(GL_ARRAY_BUFFER, m_VBufferHandle);
  // orphenaing the previous VRAM buffer + fill data
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat)*m_VBuffer.size(), m_VBuffer.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER,0);

  IsOpenGLok("modelRaw2D::updateIntoGPU");
}

void modelRaw2D::clearGPU()
{
  if (m_VAO != 0) glDeleteVertexArrays(1,&m_VAO);
  m_VAO = 0;
  if (m_VBufferHandle != 0) glDeleteBuffers(1,&m_VBufferHandle);
  m_VBufferHandle = 0;

  IsOpenGLok("modelRaw2D::clearGPU");
}

void modelRaw2D::drawcall(std::size_t partfirst, std::size_t partcount, const bool bindVAO, GLenum mode) const
{
  TRE_ASSERT(m_VAO != 0);
  if (bindVAO) glBindVertexArray(m_VAO);

  if (partcount == 0) return;
  TRE_ASSERT((partfirst+partcount) <= m_partInfo.size());

#if 1 // def TRE_OPENGL_ES (rework this opt, avoid std::vector !)
  for (std::size_t ipart = partfirst; ipart < (partfirst+partcount); ++ipart)
  {
    if (m_partInfo[ipart].m_size > 0)
    {
      glDrawArrays(mode, m_partInfo[ipart].m_offset, m_partInfo[ipart].m_size);
    }
  }
#else
  std::vector<GLint>   tfirst(partcount);
  std::vector<GLsizei> tcount(partcount);
  std::size_t pcount = 0;
  for (std::size_t ipart = partfirst; ipart < (partfirst+partcount); ++ipart)
  {
    if (pcount > 0 && m_partInfo[ipart - 1].m_offset + m_partInfo[ipart - 1].m_size == m_partInfo[ipart].m_offset)
    {
      tcount[pcount - 1] += GLsizei(m_partInfo[ipart].m_size);
    }
    else if (m_partInfo[ipart].m_size > 0)
    {
      tfirst[pcount] = GLint(m_partInfo[ipart].m_offset);
      tcount[pcount] = GLsizei(m_partInfo[ipart].m_size);
      ++pcount;
    }
  }
  glMultiDrawArrays(mode, tfirst.data(), tcount.data(), GLsizei(pcount));
#endif

  IsOpenGLok("modelRaw2D::drawcall");
}

void modelRaw2D::loadIntoGPU_VertexBuffer(const bool clearCPUbuffer)
{
  // assign buffer
  TRE_ASSERT(!m_VBuffer.empty());
  TRE_ASSERT(m_VBuffer.size() == m_layout.m_vertexCount * 8);
  glGenBuffers( 1, &m_VBufferHandle );
  glBindBuffer(GL_ARRAY_BUFFER, m_VBufferHandle);
  glBufferData(GL_ARRAY_BUFFER, m_VBuffer.size() * sizeof(GLfloat), m_VBuffer.data(), GL_STATIC_DRAW);

  _bind_vertexAttribPointer_float(m_layout.m_positions, 0, m_VBuffer.data());
  _bind_vertexAttribPointer_float(m_layout.m_normals  , 1, m_VBuffer.data()); // will "glDisable" the vertex-attribute.
  _bind_vertexAttribPointer_float(m_layout.m_uvs      , 2, m_VBuffer.data());
  _bind_vertexAttribPointer_float(m_layout.m_colors   , 3, m_VBuffer.data());

  IsOpenGLok("modelRaw2D::loadIntoGPU_VertexBuffer");

  if (clearCPUbuffer)
  {
    m_VBuffer.clear();
    m_layout.m_positions.m_data = nullptr;
    m_layout.m_colors.m_data    = nullptr;
    m_layout.m_uvs.m_data       = nullptr;
  }
}

// modelStaticIndexed3D =======================================================

static void _memcpy_safe(float *dst, const float *src, std::size_t nfloats)
{
  const long distSigned = long(dst - src);

  if (distSigned > 0)
  {
    const std::size_t dist = distSigned;
    std::size_t offset = 0;
    dst += nfloats;
    src += nfloats;
    while (offset < nfloats)
    {
      const std::size_t copyCount = (offset + dist) > nfloats ? nfloats - offset : dist;
      dst -= copyCount;
      src -= copyCount;
      memcpy(dst, src, sizeof(float) * copyCount);
      offset += copyCount;
    }
  }
  else if (distSigned < 0)
  {
    const std::size_t dist = -distSigned;
    std::size_t offset = 0;
    while (offset < nfloats)
    {
      const std::size_t copyCount = (offset + dist) > nfloats ? nfloats - offset : dist;
      memcpy(dst, src, sizeof(float) * copyCount);
      offset += copyCount;
      dst += copyCount;
      src += copyCount;
    }
  }
}

void modelStaticIndexed3D::resizeVertex(std::size_t count)
{
  TRE_ASSERT(m_VBufferHandle == 0); // not loaded yet

  std::size_t sumSize = 0;

  if (m_flags & VB_POSITION) sumSize += m_layout.m_positions.m_size = 3;
  if (m_flags & VB_NORMAL  ) sumSize += m_layout.m_normals.m_size = 3;
  if (m_flags & VB_COLOR   ) sumSize += m_layout.m_colors.m_size = 4;
  if (m_flags & VB_TANGENT ) sumSize += m_layout.m_tangents.m_size = 4;
  if (m_flags & VB_UV      ) sumSize += m_layout.m_uvs.m_size = 2;

  TRE_ASSERT(m_layout.m_vertexCount * sumSize == m_VBuffer.size() || m_VBuffer.empty() /*buffer is cleared when loading*/);

  const std::size_t count_old = m_layout.m_vertexCount;

  m_layout.m_vertexCount = count;

  // set layout with data pointers : contigous data, as the size will not change (so attrib-pointer offset will not change)

  if (count != 0 && count < count_old && !m_VBuffer.empty()) // copy data when shrinking
  {
    std::size_t offset = 0;

#define COPYDATA(_flag, _vdata) \
  if (m_flags & _flag) \
  { \
    _memcpy_safe(m_VBuffer.data() + offset * count, m_VBuffer.data() + offset * count_old, count * _vdata.m_size); \
    offset += _vdata.m_size; \
  }

    COPYDATA(VB_POSITION, m_layout.m_positions)
    COPYDATA(VB_COLOR   , m_layout.m_colors)
    COPYDATA(VB_NORMAL  , m_layout.m_normals)
    COPYDATA(VB_TANGENT , m_layout.m_tangents)
    COPYDATA(VB_UV      , m_layout.m_uvs)

#undef COPYDATA
  }

  m_VBuffer.resize(count * sumSize);

  if (count_old != 0 && count > count_old) // copy data when growing
  {
    std::size_t offset = sumSize;

#define COPYDATA(_flag, _vdata) \
  if (m_flags & _flag) \
  { \
    offset -= _vdata.m_size; \
    _memcpy_safe(m_VBuffer.data() + offset * count, m_VBuffer.data() + offset * count_old, count_old * _vdata.m_size); \
  }

    COPYDATA(VB_UV      , m_layout.m_uvs)
    COPYDATA(VB_TANGENT , m_layout.m_tangents)
    COPYDATA(VB_NORMAL  , m_layout.m_normals)
    COPYDATA(VB_COLOR   , m_layout.m_colors)
    COPYDATA(VB_POSITION, m_layout.m_positions)

#undef COPYDATA
  }

  std::size_t dataOffset = 0;

#define SETLAYOUT(_flag, _vdata) \
  if (m_flags & _flag) \
  { \
    _vdata.m_stride = _vdata.m_size; \
    _vdata.m_data = m_VBuffer.data() + dataOffset; \
    dataOffset += _vdata.m_size * count; \
  }

  SETLAYOUT(VB_POSITION, m_layout.m_positions)
  SETLAYOUT(VB_COLOR   , m_layout.m_colors)
  SETLAYOUT(VB_NORMAL  , m_layout.m_normals)
  SETLAYOUT(VB_TANGENT , m_layout.m_tangents)
  SETLAYOUT(VB_UV      , m_layout.m_uvs)

#undef SETLAYOUT

  TRE_ASSERT(dataOffset == m_VBuffer.size());
}

bool modelStaticIndexed3D::read(std::istream & inbuffer)
{
  readBase(inbuffer);

  // read flags
  inbuffer.read(reinterpret_cast<char*>(&m_flags), sizeof(int));

  // read index buffer
  read_IndexBuffer(inbuffer);

  // read vertex buffer
  uint header[2]; // { vertexCount, bufferSize }
  inbuffer.read(reinterpret_cast<char*>(&header[0]), sizeof(header));
  resizeVertex(header[0]);
  TRE_ASSERT(header[1] == m_VBuffer.size());
  inbuffer.read(reinterpret_cast<char*>(m_VBuffer.data()), m_VBuffer.size() * sizeof(GLfloat));

  return true;
}

bool modelStaticIndexed3D::write(std::ostream & outbuffer) const
{
  writeBase(outbuffer);

  // write flags
  outbuffer.write(reinterpret_cast<const char*>(&m_flags), sizeof(int));

  // write index buffer
  write_IndexBuffer(outbuffer);

  // write vertex buffer
  uint header[2]; // { vertexCount, bufferSize }
  header[0] = m_layout.m_vertexCount; TRE_ASSERT(m_layout.m_vertexCount <= std::numeric_limits<uint>::max());
  header[1] = m_VBuffer.size();
  outbuffer.write(reinterpret_cast<const char*>(&header[0]), sizeof(header));
  outbuffer.write(reinterpret_cast<const char*>(m_VBuffer.data()), m_VBuffer.size() * sizeof(GLfloat));

  return true;
}

bool modelStaticIndexed3D::loadIntoGPU()
{
  reserveIndex(4);
  reserveVertex(4);

  TRE_ASSERT(m_VAO == 0);

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  // assign index-buffer

  loadIntoGPU_IndexBuffer(true);

  // assign vertex-buffer

  loadIntoGPU_VertexBuffer();

  glBindVertexArray(0);

  return true;
}

void modelStaticIndexed3D::clearGPU()
{
  if (m_VAO != 0)    glDeleteVertexArrays(1, &m_VAO);
  m_VAO = 0;

  clearGPU_IndexBuffer();

  if (m_VBufferHandle != 0) glDeleteBuffers(1, &m_VBufferHandle);
  m_VBufferHandle = 0;

  // the static vertex-data is not retrieved, so we just force the model to be cleared entirely.
  m_layout.clear();
  clearParts();

  IsOpenGLok("modelStaticIndexed3D::clearGPU");
}

void modelStaticIndexed3D::drawcall(std::size_t partfirst, std::size_t partcount, const bool bindVAO, GLenum mode) const
{
  TRE_ASSERT(m_VAO != 0);
  if (bindVAO) glBindVertexArray(m_VAO);

  if (partcount == 0) return;
  TRE_ASSERT((partfirst+partcount) <= m_partInfo.size());

#if 1 // def TRE_OPENGL_ES (rework this opt, avoid std::vector !)
  for (std::size_t ipart = partfirst; ipart < (partfirst+partcount); ++ipart)
  {
    if (m_partInfo[ipart].m_size > 0)
    {
      glDrawElements(mode, m_partInfo[ipart].m_size, GL_UNSIGNED_INT, reinterpret_cast<GLvoid*>(m_partInfo[ipart].m_offset * sizeof(GLfloat)));
    }
  }
#else
  std::vector<GLvoid*> tfirst(partcount);
  std::vector<GLsizei> tcount(partcount);
  GLsizei pcount = 0;
  for (std::size_t ipart = partfirst; ipart < (partfirst+partcount); ++ipart)
  {
    if (pcount > 0 && m_partInfo[ipart - 1].m_offset + m_partInfo[ipart - 1].m_size == m_partInfo[ipart].m_offset)
    {
      tcount[pcount - 1] += m_partInfo[ipart].m_size;
    }
    else if (m_partInfo[ipart].m_size > 0)
    {
      tfirst[pcount] = reinterpret_cast<GLvoid*>( m_partInfo[ipart].m_offset * sizeof(GLfloat) );
      tcount[pcount] = m_partInfo[ipart].m_size;
      ++pcount;
    }
  }
  glMultiDrawElements(mode, tcount.data(), GL_UNSIGNED_INT, tfirst.data(), pcount);
#endif

  IsOpenGLok("modelStaticIndexed3D::drawcall");
}

void modelStaticIndexed3D::loadIntoGPU_VertexBuffer()
{
  TRE_ASSERT(m_VBufferHandle == 0);
  TRE_ASSERT(!m_VBuffer.empty());

  glGenBuffers( 1, &m_VBufferHandle );
  glBindBuffer(GL_ARRAY_BUFFER, m_VBufferHandle);
  glBufferData(GL_ARRAY_BUFFER, m_VBuffer.size() * sizeof(GLfloat), m_VBuffer.data(), GL_STATIC_DRAW);

  if (m_flags & VB_POSITION) _bind_vertexAttribPointer_float(m_layout.m_positions, 0, m_VBuffer.data());
  if (m_flags & VB_NORMAL  ) _bind_vertexAttribPointer_float(m_layout.m_normals  , 1, m_VBuffer.data());
  if (m_flags & VB_UV      ) _bind_vertexAttribPointer_float(m_layout.m_uvs      , 2, m_VBuffer.data());
  if (m_flags & VB_COLOR   ) _bind_vertexAttribPointer_float(m_layout.m_colors   , 3, m_VBuffer.data());
  if (m_flags & VB_TANGENT ) _bind_vertexAttribPointer_float(m_layout.m_tangents , 4, m_VBuffer.data());

  IsOpenGLok("modelStaticIndexed3D::loadIntoGPU");

  {
    m_VBuffer.clear();
    if (m_flags & VB_POSITION) m_layout.m_positions.m_data = nullptr;
    if (m_flags & VB_NORMAL  ) m_layout.m_normals.m_data = nullptr;
    if (m_flags & VB_UV      ) m_layout.m_uvs.m_data = nullptr;
    if (m_flags & VB_COLOR   ) m_layout.m_colors.m_data = nullptr;
    if (m_flags & VB_TANGENT ) m_layout.m_tangents.m_data = nullptr;
  }
}

// modelSemiDynamic3D =======================================================

void modelSemiDynamic3D::resizeVertex(std::size_t count)
{
  TRE_ASSERT((m_flagsDynamic & m_flags) == 0); // enforce unique flags

  std::size_t sumSize = 0;

  if (m_flagsDynamic & VB_POSITION) sumSize += m_layout.m_positions.m_size = 3;
  if (m_flagsDynamic & VB_NORMAL  ) sumSize += m_layout.m_normals.m_size = 3;
  if (m_flagsDynamic & VB_COLOR   ) sumSize += m_layout.m_colors.m_size = 4;
  if (m_flagsDynamic & VB_TANGENT ) sumSize += m_layout.m_tangents.m_size = 4;
  if (m_flagsDynamic & VB_UV      ) sumSize += m_layout.m_uvs.m_size = 2;

  TRE_ASSERT(m_layout.m_vertexCount * sumSize == m_VBufferDyn.size() || m_VBufferDyn.empty());

  m_VBufferDyn.resize(count * sumSize);

  // set layout with data pointers : needs to be interleaved, as the size may change.
  // in fact, the flags should not be modified once the VAO is bound (so the attrib-pointer offset are set)

  // TODO : test aligned data ...

  std::size_t dataOffset = 0;

#define SETLAYOUT(_flag, _vdata) \
  if (m_flagsDynamic & _flag) \
  { \
    _vdata.m_stride = sumSize; \
    _vdata.m_data = m_VBufferDyn.data() + dataOffset; \
    dataOffset += _vdata.m_size; \
  }

  SETLAYOUT(VB_POSITION, m_layout.m_positions)
  SETLAYOUT(VB_COLOR   , m_layout.m_colors)
  SETLAYOUT(VB_NORMAL  , m_layout.m_normals)
  SETLAYOUT(VB_TANGENT , m_layout.m_tangents)
  SETLAYOUT(VB_UV      , m_layout.m_uvs)

#undef SETLAYOUT

  TRE_ASSERT(dataOffset == sumSize);

  // will set "m_layout.m_vertexCount = count;"
  modelStaticIndexed3D::resizeVertex(count);
}

bool modelSemiDynamic3D::read(std::istream &inbuffer)
{
  bool result = true;

  inbuffer.read(reinterpret_cast<char*>(&m_flagsDynamic), sizeof(int));

  result &= modelStaticIndexed3D::read(inbuffer);

  // dont read the dynamic buffer

  return result;
}

bool modelSemiDynamic3D::write(std::ostream &outbuffer) const
{
  bool result = true;

  outbuffer.write(reinterpret_cast<const char*>(&m_flagsDynamic), sizeof(int));

  result &= modelStaticIndexed3D::write(outbuffer);

  // dont write the dynamic buffer

  return result;
}

bool modelSemiDynamic3D::loadIntoGPU()
{
  reserveIndex(4);
  reserveVertex(4);

  TRE_ASSERT(m_VAO == 0);

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  // assign index-buffer
  loadIntoGPU_IndexBuffer(false); // index-data may be needed (for reading) when accessing dynamic vertex data

  // assign vertex-buffer (Static)

  if (m_flags != 0)
    modelStaticIndexed3D::loadIntoGPU_VertexBuffer();

  // assign vertex-buffer (Dynamic)

  if (m_flagsDynamic != 0)
    modelSemiDynamic3D::loadIntoGPU_VertexBuffer();

  glBindVertexArray(0);

  return true;
}

void modelSemiDynamic3D::updateIntoGPU()
{
  if(m_VBufferDyn.empty())
    return;

  glBindVertexArray(0);

  if (m_flags == 0)
    updateIntoGPU_IndexBuffer(); // if no static data, then the mesh connectivity can change too

  TRE_ASSERT(m_VBufferHandleDyn != 0);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBufferHandleDyn);
  // orphenaing the previous VRAM buffer + fill data
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * m_VBufferDyn.size(), m_VBufferDyn.data(), GL_STREAM_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER,0);

  IsOpenGLok("modelSemiDynamic3D::updateIntoGPU");
}

void modelSemiDynamic3D::clearGPU()
{
  if (m_VAO != 0)    glDeleteVertexArrays(1, &m_VAO);
  m_VAO = 0;

  clearGPU_IndexBuffer(m_flags == 0); // if no static data, then restore the mesh connectivity too

  if (m_VBufferHandle != 0) glDeleteBuffers(1, &m_VBufferHandle);
  m_VBufferHandle = 0;

  if (m_VBufferHandleDyn != 0) glDeleteBuffers(1, &m_VBufferHandleDyn);
  m_VBufferHandleDyn = 0;

  // if there is static vertex-data (which is not retrieved), so we just force the model to be cleared entirely.
  if (m_flags != 0)
  {
    m_layout.clear();
    clearParts();
  }
  else
  {
    m_VBufferDyn.resize(0);
    const std::size_t countSave = m_layout.m_vertexCount;
    m_layout.m_vertexCount = 0;
    modelSemiDynamic3D::resizeVertex(countSave);
  }

  IsOpenGLok("modelSemiDynamic3D::clearGPU");
}

void modelSemiDynamic3D::loadIntoGPU_VertexBuffer(const bool clearCPUbuffer)
{
  TRE_ASSERT(m_VBufferHandleDyn == 0);
  TRE_ASSERT(!m_VBufferDyn.empty());

  glGenBuffers(1, &m_VBufferHandleDyn);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBufferHandleDyn);
  glBufferData(GL_ARRAY_BUFFER, m_VBufferDyn.size() * sizeof(GLfloat), m_VBufferDyn.data(), GL_STREAM_DRAW);

  if (m_flagsDynamic & VB_POSITION) _bind_vertexAttribPointer_float(m_layout.m_positions, 0, m_VBufferDyn.data());
  if (m_flagsDynamic & VB_NORMAL  ) _bind_vertexAttribPointer_float(m_layout.m_normals  , 1, m_VBufferDyn.data());
  if (m_flagsDynamic & VB_UV      ) _bind_vertexAttribPointer_float(m_layout.m_uvs      , 2, m_VBufferDyn.data());
  if (m_flagsDynamic & VB_COLOR   ) _bind_vertexAttribPointer_float(m_layout.m_colors   , 3, m_VBufferDyn.data());
  if (m_flagsDynamic & VB_TANGENT ) _bind_vertexAttribPointer_float(m_layout.m_tangents , 4, m_VBufferDyn.data());

  IsOpenGLok("modelSemiDynamic3D::loadIntoGPU");

  if (clearCPUbuffer)
  {
    m_VBufferDyn.clear();
    if (m_flagsDynamic & VB_POSITION) m_layout.m_positions.m_data = nullptr;
    if (m_flagsDynamic & VB_NORMAL  ) m_layout.m_normals.m_data   = nullptr;
    if (m_flagsDynamic & VB_UV      ) m_layout.m_uvs.m_data       = nullptr;
    if (m_flagsDynamic & VB_COLOR   ) m_layout.m_colors.m_data    = nullptr;
    if (m_flagsDynamic & VB_TANGENT ) m_layout.m_tangents.m_data  = nullptr;
  }
}

// modelInstanced =========================================================

void modelInstanced::resizeInstance(std::size_t count)
{
  std::size_t sumSize = 0;

  s_modelDataLayout &layout = _layout();

  if (m_flagsInstanced & VI_POSITION   ) sumSize += layout.m_instancedPositions.m_size = 4; // a global scale is included in the "w" component
  if (m_flagsInstanced & VI_COLOR      ) sumSize += layout.m_instancedColors.m_size = 4;
  if (m_flagsInstanced & (VI_ATLAS | VI_BLEND) ) sumSize += layout.m_instancedAtlasBlends.m_size = 4;
  if (m_flagsInstanced & VI_ORIENTATION) sumSize += layout.m_instancedOrientations.m_size = 12;
  if (m_flagsInstanced & VI_ROTATION) sumSize += layout.m_instancedRotations.m_size = 1;

  TRE_ASSERT(layout.m_instanceCount * sumSize == m_InstBuffer.size() || m_InstBuffer.empty());

  layout.m_instanceCount = count;

  m_InstBuffer.resize(count * sumSize);

  // set layout with data pointers -> interleaved

  std::size_t dataOffset = 0;

#define SETLAYOUT(_flag, _vdata) \
  if (m_flagsInstanced & _flag) \
  { \
    _vdata.m_stride = sumSize; \
    _vdata.m_data = m_InstBuffer.data() + dataOffset; \
    dataOffset += _vdata.m_size; \
  }

  SETLAYOUT(VI_POSITION         , _layout().m_instancedPositions)
  SETLAYOUT(VI_COLOR            , _layout().m_instancedColors)
  SETLAYOUT(VI_ATLAS | VI_BLEND , _layout().m_instancedAtlasBlends)
  SETLAYOUT(VI_ORIENTATION      , _layout().m_instancedOrientations)
  SETLAYOUT(VI_ROTATION         , _layout().m_instancedRotations)

#undef SETLAYOUT

  TRE_ASSERT(dataOffset == sumSize);
}

bool modelInstanced::read(std::istream &inbuffer)
{
  bool result = true;

  inbuffer.read(reinterpret_cast<char*>(&m_flagsInstanced), sizeof(int));

  // dont read the instanced buffer

  return result;
}

bool modelInstanced::write(std::ostream &outbuffer) const
{
  bool result = true;

  outbuffer.write(reinterpret_cast<const char*>(&m_flagsInstanced), sizeof(int));

  // dont write the instanced buffer

  return result;
}

void modelInstanced::loadIntoGPU_InstancedBuffer(const bool clearCPUbuffer)
{
  TRE_ASSERT(m_InstBufferHandle == 0);
  TRE_ASSERT(!m_InstBuffer.empty());

  glGenBuffers(1, &m_InstBufferHandle);
  glBindBuffer(GL_ARRAY_BUFFER, m_InstBufferHandle);
  glBufferData(GL_ARRAY_BUFFER, m_InstBuffer.size() * sizeof(GLfloat), m_InstBuffer.data(), GL_STREAM_DRAW);

  _bind_instancedAttribPointer_float(_layout().m_instancedPositions   , 5, m_InstBuffer.data());
  _bind_instancedAttribPointer_float(_layout().m_instancedColors      , 6, m_InstBuffer.data());
  _bind_instancedAttribPointer_float(_layout().m_instancedAtlasBlends , 7, m_InstBuffer.data());
  _bind_instancedAttribPointer_float(_layout().m_instancedOrientations, 8, m_InstBuffer.data()); // 9 and 10
  _bind_instancedAttribPointer_float(_layout().m_instancedRotations   ,11, m_InstBuffer.data());

  IsOpenGLok("modelSemiDynamic3D::loadIntoGPU");

  if (clearCPUbuffer)
  {
    m_InstBuffer.clear();
    _layout().m_instancedPositions.m_data    = nullptr;
    _layout().m_instancedColors.m_data       = nullptr;
    _layout().m_instancedAtlasBlends.m_data  = nullptr;
    _layout().m_instancedOrientations.m_data = nullptr;
    _layout().m_instancedRotations.m_data    = nullptr;
  }
}

void modelInstanced::updateIntoGPU_InstancedBuffer()
{
  if (m_InstBuffer.empty())
  {
    TRE_ASSERT(_layout().m_instanceCount == 0);
    return;
  }

  glBindVertexArray(0);

  TRE_ASSERT(m_InstBufferHandle != 0);
  glBindBuffer(GL_ARRAY_BUFFER, m_InstBufferHandle);
  glBufferData(GL_ARRAY_BUFFER, m_InstBuffer.size() * sizeof(GLfloat), m_InstBuffer.data(), GL_STREAM_DRAW);

  glBindBuffer(GL_ARRAY_BUFFER, 0);

  IsOpenGLok("modelInstanced::updateIntoGPU");
}

void modelInstanced::clearGPU_InstancedBuffer(const bool restoreCPUbuffer)
{
  if (m_InstBufferHandle !=0 ) glDeleteBuffers(1, &m_InstBufferHandle);
  m_InstBufferHandle = 0;

  if (restoreCPUbuffer)
  {
    m_InstBuffer.resize(0);
    const std::size_t countSave = _layout().m_instanceCount;
    _layout().m_instanceCount = 0;
    modelInstanced::resizeInstance(countSave);
  }

  IsOpenGLok("modelInstanced::clearGPU");
}

// modelInstancedBillboard =================================================

std::size_t modelInstancedBillboard::createBillboard(const glm::vec4 &AxAyBxBy, const glm::vec4 &AuAvBuBv, const glm::vec4 &color)
{
  const std::size_t outPart = createPart(6);
  s_partInfo & part = m_partInfo[outPart];

  part.m_name = "billboard";

  fillDataRectangle(outPart, 0, AxAyBxBy, color, AuAvBuBv);

  return outPart;
}

std::size_t modelInstancedBillboard::createBillboard_wireframe(const glm::vec4 &AxAyBxBy, const glm::vec4 &color)
{
  const std::size_t outPart = createPart(8);
  s_partInfo & part = m_partInfo[outPart];

  part.m_name = "billboard-wireframe";

  fillDataLine(outPart, 0, AxAyBxBy.x, AxAyBxBy.y, AxAyBxBy.x, AxAyBxBy.w, color);
  fillDataLine(outPart, 2, AxAyBxBy.x, AxAyBxBy.w, AxAyBxBy.z, AxAyBxBy.w, color);
  fillDataLine(outPart, 4, AxAyBxBy.z, AxAyBxBy.w, AxAyBxBy.z, AxAyBxBy.y, color);
  fillDataLine(outPart, 6, AxAyBxBy.z, AxAyBxBy.y, AxAyBxBy.x, AxAyBxBy.y, color);

  return outPart;
}

bool modelInstancedBillboard::loadIntoGPU()
{
  TRE_ASSERT(m_VAO == 0);

  reserveInstance(128);

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  loadIntoGPU_VertexBuffer(true);
  loadIntoGPU_InstancedBuffer(false);

  glBindVertexArray(0);

  return true;
}

void modelInstancedBillboard::drawInstanced(std::size_t ipart, std::size_t instancedOffset, std::size_t instancedCount, const bool bindVAO, GLenum mode) const
{
  TRE_ASSERT(m_VAO != 0);
  if (bindVAO) glBindVertexArray(m_VAO);

  if (instancedCount == 0) return;
  TRE_ASSERT(ipart < m_partInfo.size());
  TRE_ASSERT(instancedOffset + instancedCount <= m_layout.m_instanceCount);

#ifdef TRE_OPENGL_ES
  glBindBuffer(GL_ARRAY_BUFFER, m_InstBufferHandle);
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedPositions;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 5, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedColors;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 6, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedAtlasBlends;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 7, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedOrientations;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 8, m_InstBuffer.data()); // 9 and 10
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedRotations;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 11, m_InstBuffer.data());
  }
  glDrawArraysInstanced(mode, m_partInfo[ipart].m_offset, m_partInfo[ipart].m_size, instancedCount);
#else
  glDrawArraysInstancedBaseInstance(mode, m_partInfo[ipart].m_offset, m_partInfo[ipart].m_size, instancedCount, instancedOffset);
#endif

  IsOpenGLok("modelInstancedBillboard::drawInstanced");
}

// modelInstancedMesh =================================================

bool modelInstancedMesh::loadIntoGPU()
{
  TRE_ASSERT(m_VAO == 0);

  reserveInstance(128);

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  loadIntoGPU_IndexBuffer(true);
  loadIntoGPU_VertexBuffer();
  loadIntoGPU_InstancedBuffer();

  glBindVertexArray(0);

  return true;
}

void modelInstancedMesh::drawInstanced(std::size_t ipart, std::size_t instancedOffset, std::size_t instancedCount, const bool bindVAO, GLenum mode) const
{
  TRE_ASSERT(m_VAO != 0);
  if (bindVAO) glBindVertexArray(m_VAO);

  if (instancedCount == 0) return;
  TRE_ASSERT(ipart < m_partInfo.size());
  TRE_ASSERT(instancedOffset + instancedCount <= m_layout.m_instanceCount);

#ifdef TRE_OPENGL_ES
  glBindBuffer(GL_ARRAY_BUFFER, m_InstBufferHandle);
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedPositions;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 5, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedColors;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 6, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedAtlasBlends;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 7, m_InstBuffer.data());
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedOrientations;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 8, m_InstBuffer.data()); // 9 and 10
  }
  {
    s_modelDataLayout::s_instanceData localInst = m_layout.m_instancedRotations;
    localInst.m_data = localInst.m_data + localInst.m_stride * instancedOffset;
    _bind_instancedAttribPointer_float(localInst, 11, m_InstBuffer.data());
  }

  glDrawElementsInstanced(mode, m_partInfo[ipart].m_size, GL_UNSIGNED_INT, reinterpret_cast<void*>(m_partInfo[ipart].m_offset * sizeof(GLfloat)), GLsizei(instancedCount));
#else
  glDrawElementsInstancedBaseInstance(mode, m_partInfo[ipart].m_size, GL_UNSIGNED_INT, reinterpret_cast<void*>(m_partInfo[ipart].m_offset * sizeof(GLfloat)), GLsizei(instancedCount), GLuint(instancedOffset));
#endif

  IsOpenGLok("modelInstancedBillboard::drawcall");
}


} // namespace
