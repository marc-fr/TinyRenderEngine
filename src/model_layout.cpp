#include "model.h"

#include <math.h>
#include <array>

namespace tre {

//=============================================================================

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
          if (offset < 0)
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

void s_modelDataLayout::collapseVertex(GLuint iVertA, const std::vector<GLuint> &iVertOther, float wVertA, const std::vector<float> wVertOther) const
{
  TRE_ASSERT(iVertOther.size() == wVertOther.size());

  // patch indices list (if needed):
  TRE_ASSERT(m_indexCount == 0 || m_index.m_data != nullptr);
  for (std::size_t Iind = 0; Iind < m_indexCount; ++Iind)
  {
    GLuint & ind = m_index[Iind];
    for (GLuint iVertB : iVertOther)
    {
      if (ind == iVertB) ind  = iVertA;
    }
  }

  float wTotal = wVertA;
  for (float w : wVertOther)
    wTotal += w;
  const float wNormalizer = 1.f / wTotal;

  // merge data inot "A"
  if (m_positions.m_size == 3)
  {
    glm::vec3 &pos = m_positions.get<glm::vec3>(iVertA);
    pos *= wVertA;
    for (std::size_t i = 0; i < iVertOther.size(); ++i)
      pos += wVertOther[i] * m_positions.get<glm::vec3>(iVertOther[i]);
    pos *= wNormalizer;
  }
  if (m_normals.m_size == 3)
  {
    glm::vec3 &normal = m_normals.get<glm::vec3>(iVertA);
    normal *= wVertA;
    for (std::size_t i = 0; i < iVertOther.size(); ++i)
      normal += wVertOther[i] * m_normals.get<glm::vec3>(iVertOther[i]);
    normal = glm::normalize(normal);
  }
  if (m_colors.m_size == 4)
  {
    glm::vec4 &color = m_colors.get<glm::vec4>(iVertA);
    color *= wVertA;
    for (std::size_t i = 0; i < iVertOther.size(); ++i)
      color += wVertOther[i] * m_colors.get<glm::vec4>(iVertOther[i]);
    color *= wNormalizer;
  }
  if (m_uvs.m_size == 2)
  {
    glm::vec2 &uv = m_uvs.get<glm::vec2>(iVertA);
    uv *= wVertA;
    for (std::size_t i = 0; i < iVertOther.size(); ++i)
      uv += wVertOther[i] * m_uvs.get<glm::vec2>(iVertOther[i]);
    uv *= wNormalizer;
  }
  if (m_tangents.m_size == 4)
  {
    glm::vec4 &tan = m_tangents.get<glm::vec4>(iVertA);
    tan *= wVertA * tan.w;
    for (std::size_t i = 0; i < iVertOther.size(); ++i)
    {
      glm::vec4 tanLocal = m_tangents.get<glm::vec4>(iVertOther[i]);
      tanLocal *= wVertOther[i] * tanLocal.w;
      tan += tanLocal;
     }
    tan.w = 0.f;
    tan = glm::normalize(tan);
    tan.w = 1.f;
  }
}

// ----------------------------------------------------------------------------

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

} // namespace
