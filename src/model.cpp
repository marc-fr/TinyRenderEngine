#include "tre_model.h"

namespace tre {

// Helper

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

  TRE_ASSERT(m_layout.m_positions.m_size == 2);
  {
    m_layout.m_positions[first    ][0] = AxAyBxBy.x;
    m_layout.m_positions[first    ][1] = AxAyBxBy.y;
    m_layout.m_positions[first + 1][0] = AxAyBxBy.z;
    m_layout.m_positions[first + 1][1] = AxAyBxBy.y;
    m_layout.m_positions[first + 2][0] = AxAyBxBy.z;
    m_layout.m_positions[first + 2][1] = AxAyBxBy.w;

    m_layout.m_positions[first + 3][0] = AxAyBxBy.x;
    m_layout.m_positions[first + 3][1] = AxAyBxBy.y;
    m_layout.m_positions[first + 4][0] = AxAyBxBy.z;
    m_layout.m_positions[first + 4][1] = AxAyBxBy.w;
    m_layout.m_positions[first + 5][0] = AxAyBxBy.x;
    m_layout.m_positions[first + 5][1] = AxAyBxBy.w;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0 || m_layout.m_uvs.m_size == 2);
  if (m_layout.m_uvs.m_size == 2)
  {
    m_layout.m_uvs[first    ][0] = AuAvBuBv.x;
    m_layout.m_uvs[first    ][1] = AuAvBuBv.y;
    m_layout.m_uvs[first + 1][0] = AuAvBuBv.z;
    m_layout.m_uvs[first + 1][1] = AuAvBuBv.y;
    m_layout.m_uvs[first + 2][0] = AuAvBuBv.z;
    m_layout.m_uvs[first + 2][1] = AuAvBuBv.w;

    m_layout.m_uvs[first + 3][0] = AuAvBuBv.x;
    m_layout.m_uvs[first + 3][1] = AuAvBuBv.y;
    m_layout.m_uvs[first + 4][0] = AuAvBuBv.z;
    m_layout.m_uvs[first + 4][1] = AuAvBuBv.w;
    m_layout.m_uvs[first + 5][0] = AuAvBuBv.x;
    m_layout.m_uvs[first + 5][1] = AuAvBuBv.w;
  }

  TRE_ASSERT(m_layout.m_colors.m_size == 4);
  {
    m_layout.m_colors.get<glm::vec4>(first    ) = color;
    m_layout.m_colors.get<glm::vec4>(first + 1) = color;
    m_layout.m_colors.get<glm::vec4>(first + 2) = color;
    m_layout.m_colors.get<glm::vec4>(first + 3) = color;
    m_layout.m_colors.get<glm::vec4>(first + 4) = color;
    m_layout.m_colors.get<glm::vec4>(first + 5) = color;
  }

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

  TRE_ASSERT(m_layout.m_positions.m_size == 2);
  {
    m_layout.m_positions[first    ][0] = Ax;
    m_layout.m_positions[first    ][1] = Ay;
    m_layout.m_positions[first + 1][0] = Bx;
    m_layout.m_positions[first + 1][1] = By;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0 || m_layout.m_uvs.m_size == 2);
  if (m_layout.m_uvs.m_size == 2)
  {
    m_layout.m_uvs[first    ][0] = 0.f;
    m_layout.m_uvs[first    ][1] = 0.f;
    m_layout.m_uvs[first + 1][0] = 0.f;
    m_layout.m_uvs[first + 1][1] = 0.f;
  }

  TRE_ASSERT(m_layout.m_colors.m_size == 4);
  {
    m_layout.m_colors.get<glm::vec4>(first    ) = color;
    m_layout.m_colors.get<glm::vec4>(first + 1) = color;
  }

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
