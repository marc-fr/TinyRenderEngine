#include "tre_model.h"

namespace tre {

//=============================================================================

std::size_t modelIndexed::createPartFromPrimitive_box(const glm::mat4 &transform, const float edgeLength)
{
  const float halfSize = edgeLength * 0.5f;

  const glm::vec3 pt000 = transform[3] - transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt001 = transform[3] - transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt010 = transform[3] - transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt011 = transform[3] - transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt100 = transform[3] + transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt101 = transform[3] + transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt110 = transform[3] + transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt111 = transform[3] + transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;

  const std::array<GLuint, 36> bufferInd = { 0, 1, 2, 3, 0, 2, //face Z-
                                             4, 6, 5, 7, 6, 4, //face Z+
                                             8, 9,10,11, 8,10, //face X-
                                            12,14,13,15,14,12, //face X+
                                            16,18,17,19,18,16, //face Y-
                                            20,21,22,23,20,22, //face Y+
  };
  const std::size_t partId = createPartFromIndexes(bufferInd, nullptr);
  m_partInfo[partId].m_name = "generated-box";

  const GLuint ver0 = m_layout.m_index[partInfo(partId).m_offset];

  m_layout.m_positions.get<glm::vec3>(ver0 +  0) = pt000; //face Z-
  m_layout.m_positions.get<glm::vec3>(ver0 +  1) = pt010;
  m_layout.m_positions.get<glm::vec3>(ver0 +  2) = pt110;
  m_layout.m_positions.get<glm::vec3>(ver0 +  3) = pt100;
  m_layout.m_positions.get<glm::vec3>(ver0 +  4) = pt001; //face Z+
  m_layout.m_positions.get<glm::vec3>(ver0 +  5) = pt011;
  m_layout.m_positions.get<glm::vec3>(ver0 +  6) = pt111;
  m_layout.m_positions.get<glm::vec3>(ver0 +  7) = pt101;
  m_layout.m_positions.get<glm::vec3>(ver0 +  8) = pt001; //face X-
  m_layout.m_positions.get<glm::vec3>(ver0 +  9) = pt011;
  m_layout.m_positions.get<glm::vec3>(ver0 + 10) = pt010;
  m_layout.m_positions.get<glm::vec3>(ver0 + 11) = pt000;
  m_layout.m_positions.get<glm::vec3>(ver0 + 12) = pt101; //face X+
  m_layout.m_positions.get<glm::vec3>(ver0 + 13) = pt111;
  m_layout.m_positions.get<glm::vec3>(ver0 + 14) = pt110;
  m_layout.m_positions.get<glm::vec3>(ver0 + 15) = pt100;
  m_layout.m_positions.get<glm::vec3>(ver0 + 16) = pt000; //face Y-
  m_layout.m_positions.get<glm::vec3>(ver0 + 17) = pt001;
  m_layout.m_positions.get<glm::vec3>(ver0 + 18) = pt101;
  m_layout.m_positions.get<glm::vec3>(ver0 + 19) = pt100;
  m_layout.m_positions.get<glm::vec3>(ver0 + 20) = pt010; //face Y+
  m_layout.m_positions.get<glm::vec3>(ver0 + 21) = pt011;
  m_layout.m_positions.get<glm::vec3>(ver0 + 22) = pt111;
  m_layout.m_positions.get<glm::vec3>(ver0 + 23) = pt110;

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal1xx = glm::normalize(glm::vec3(transform[0]));
    const glm::vec3 outNormalx1x = glm::normalize(glm::vec3(transform[1]));
    const glm::vec3 outNormalxx1 = glm::normalize(glm::vec3(transform[2]));
    const glm::vec3 outNormal0xx = -outNormal1xx;
    const glm::vec3 outNormalx0x = -outNormalx1x;
    const glm::vec3 outNormalxx0 = -outNormalxx1;

    m_layout.m_normals.get<glm::vec3>(ver0 +  0) = outNormalxx0; //face Z-
    m_layout.m_normals.get<glm::vec3>(ver0 +  1) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(ver0 +  2) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(ver0 +  3) = outNormalxx0;
    m_layout.m_normals.get<glm::vec3>(ver0 +  4) = outNormalxx1; //face Z+
    m_layout.m_normals.get<glm::vec3>(ver0 +  5) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(ver0 +  6) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(ver0 +  7) = outNormalxx1;
    m_layout.m_normals.get<glm::vec3>(ver0 +  8) = outNormal0xx; //face X-
    m_layout.m_normals.get<glm::vec3>(ver0 +  9) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 10) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 11) = outNormal0xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 12) = outNormal1xx; //face X+
    m_layout.m_normals.get<glm::vec3>(ver0 + 13) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 14) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 15) = outNormal1xx;
    m_layout.m_normals.get<glm::vec3>(ver0 + 16) = outNormalx0x; //face Y-
    m_layout.m_normals.get<glm::vec3>(ver0 + 17) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(ver0 + 18) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(ver0 + 19) = outNormalx0x;
    m_layout.m_normals.get<glm::vec3>(ver0 + 20) = outNormalx1x; //face Y+
    m_layout.m_normals.get<glm::vec3>(ver0 + 21) = outNormalx1x;
    m_layout.m_normals.get<glm::vec3>(ver0 + 22) = outNormalx1x;
    m_layout.m_normals.get<glm::vec3>(ver0 + 23) = outNormalx1x;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  {
     const glm::vec4 t0 = glm::abs(halfSize * transform[0]);
     const glm::vec4 t1 = glm::abs(halfSize * transform[1]);
     const glm::vec4 t2 = glm::abs(halfSize * transform[2]);
     const glm::vec4 tm = t0 + t1 + t2;
     const glm::vec4 bboxMin = transform[3] - tm;
     m_partInfo[partId].m_bbox.m_min = bboxMin;
     const glm::vec4 bboxMax = transform[3] + tm;
     m_partInfo[partId].m_bbox.m_max = bboxMax;
  }

  return partId;
}

std::size_t modelIndexed::createPartFromPrimitive_box_wireframe(const glm::mat4 &transform, const float edgeLength)
{
  const float halfSize = edgeLength * 0.5f;

  const glm::vec3 pt000 = transform[3] - transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt001 = transform[3] - transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt010 = transform[3] - transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt011 = transform[3] - transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt100 = transform[3] + transform[0] * halfSize - transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt101 = transform[3] + transform[0] * halfSize - transform[1] * halfSize + transform[2] * halfSize;
  const glm::vec3 pt110 = transform[3] + transform[0] * halfSize + transform[1] * halfSize - transform[2] * halfSize;
  const glm::vec3 pt111 = transform[3] + transform[0] * halfSize + transform[1] * halfSize + transform[2] * halfSize;

  const std::array<GLuint, 48> bufferInd = { 0, 1, 1, 3, 3, 2, 2, 0, //face X-
                                             4, 5, 5, 7, 7, 6, 6, 4, //face X+
                                             0, 1, 1, 5, 5, 4, 4, 0, //face Y-
                                             3, 2, 2, 6, 6, 7, 7, 3, //face Y+
                                             0, 2, 2, 6, 6, 4, 4, 0, //face Z-
                                             3, 7, 7, 5, 5, 1, 1, 3, //face Z+
  };

  const std::size_t partId = createPartFromIndexes(bufferInd, nullptr);
  m_partInfo[partId].m_name = "generated-box-wireframe";

  const GLuint ver0 = m_layout.m_index[partInfo(partId).m_offset];

  m_layout.m_positions.get<glm::vec3>(ver0 +  0) = pt000;
  m_layout.m_positions.get<glm::vec3>(ver0 +  1) = pt001;
  m_layout.m_positions.get<glm::vec3>(ver0 +  2) = pt010;
  m_layout.m_positions.get<glm::vec3>(ver0 +  3) = pt011;
  m_layout.m_positions.get<glm::vec3>(ver0 +  4) = pt100;
  m_layout.m_positions.get<glm::vec3>(ver0 +  5) = pt101;
  m_layout.m_positions.get<glm::vec3>(ver0 +  6) = pt110;
  m_layout.m_positions.get<glm::vec3>(ver0 +  7) = pt111;

  TRE_ASSERT(m_layout.m_normals.m_size == 0);
  TRE_ASSERT(m_layout.m_uvs.m_size == 0);
  TRE_ASSERT(m_layout.m_tangents.m_size == 0);

  {
     const glm::vec4 t0 = glm::abs(halfSize * transform[0]);
     const glm::vec4 t1 = glm::abs(halfSize * transform[1]);
     const glm::vec4 t2 = glm::abs(halfSize * transform[2]);
     const glm::vec4 tm = t0 + t1 + t2;
     const glm::vec4 bboxMin = transform[3] - tm;
     m_partInfo[partId].m_bbox.m_min = bboxMin;
     const glm::vec4 bboxMax = transform[3] + tm;
     m_partInfo[partId].m_bbox.m_max = bboxMax;
  }

  return partId;
}

std::size_t modelIndexed::createPartFromPrimitive_cone(const glm::mat4 &transform, const float radius, const float heigth, const uint subdiv)
{
  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = subdiv * 6;
  const std::size_t vertexAddCount = 2 + subdiv;

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const float angle = u * 2.f * M_PI / subdiv;
    m_layout.m_positions.get<glm::vec3>(vertexCurrCount + u) = center + axisT1 * (radius * cosf(angle)) + axisT2 * (radius * sinf(angle));
  }
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv + 0) = center;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv + 1) = center + heigth * axisUp;

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  const std::size_t indBase = vertexCurrCount + subdiv;
  const std::size_t indTop  = vertexCurrCount + subdiv + 1;

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const std::size_t ind0 = vertexCurrCount + u;
    const std::size_t ind1 = (u == subdiv - 1) ? vertexCurrCount : ind0 + 1;

    *(indicePtr++) = ind0;
    *(indicePtr++) = ind1;
    *(indicePtr++) = indBase;

    *(indicePtr++) = ind0;
    *(indicePtr++) = ind1;
    *(indicePtr++) = indTop;
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(indiceCurrCount) + indiceAddCount);

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  s_partInfo newPart("generated-cone");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  computeBBoxPart(ipart); // TODO better - inline !!

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_disk(const glm::mat4 &transform, const float radiusOut, const float radiusIn, const uint subdiv)
{
  TRE_ASSERT(radiusIn <= radiusOut);

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = radiusIn == 0.f ? subdiv * 3 : subdiv * 6;
  const std::size_t vertexAddCount = radiusIn == 0.f ? 1 + subdiv : 2.f * subdiv;

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t u = 0; u < subdiv; ++u)
  {
    const float angle = u * 2.f * M_PI / subdiv;
    m_layout.m_positions.get<glm::vec3>(vertexCurrCount + u) = center + axisT1 * (radiusOut * cosf(angle)) + axisT2 * (radiusOut * sinf(angle));
  }
  if (radiusIn == 0.f)
  {
    m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv) = center;
  }
  else
  {
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const float angle = (u + 0.5f) * 2.f * M_PI / subdiv;
      m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv + u) = center + axisT1 * (radiusIn * cosf(angle)) + axisT2 * (radiusIn * sinf(angle));
    }
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  if (radiusIn == 0.f)
  {
    const std::size_t indCenter = vertexCurrCount + subdiv;
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const std::size_t indA = vertexCurrCount + u;
      const std::size_t indB = (u == subdiv - 1) ? vertexCurrCount : indA + 1;

      *(indicePtr++) = indCenter;
      *(indicePtr++) = indA;
      *(indicePtr++) = indB;
    }
  }
  else
  {
    for (std::size_t u = 0; u < subdiv; ++u)
    {
      const std::size_t indOutA = vertexCurrCount + u;
      const std::size_t indOutB = (u == subdiv - 1) ? vertexCurrCount : indOutA + 1;

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

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(indiceCurrCount) + indiceAddCount);

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal = glm::normalize(glm::vec3(transform[1]));
    for (std::size_t i = 0; i < vertexAddCount; ++i)
      m_layout.m_normals.get<glm::vec3>(vertexCurrCount + i) = outNormal;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  s_partInfo newPart("generated-disk");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec3 t0 = glm::abs(radiusOut * axisT1);
    const glm::vec3 t2 = glm::abs(radiusOut * axisT2);
    const glm::vec3 tm = t0 + t2;
    const glm::vec3 bboxMin = center - tm;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec3 bboxMax = center + tm;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_halftorus(const glm::mat4 &transform, const float radiusMain, const float radiusIn, const bool closed, const uint subdiv_main, const uint subdiv_in)
{
  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = subdiv_main * subdiv_in * 6 + (closed ? subdiv_in * 6 : 0);
  const std::size_t vertexAddCount = subdiv_main * subdiv_in + (closed ? 2 : 0);

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  for (std::size_t uM = 0; uM < subdiv_main; ++uM)
  {
    const float angleM = uM * M_PI / (subdiv_main - 1.f);
    const glm::vec3 axisM = - cosf(angleM) * axisT2 + sinf(angleM) * axisT1;
    for (std::size_t uI = 0; uI < subdiv_in; ++uI)
    {
      const float angleI = uI * 2.f * M_PI / subdiv_in;
      m_layout.m_positions.get<glm::vec3>(vertexCurrCount + uM * subdiv_in + uI) =
        center + (radiusMain + radiusIn * cosf(angleI)) * axisM + (radiusIn * sinf(angleI)) * axisUp;
    }
  }
  if (closed)
  {
    m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv_main * subdiv_in + 0) = center - radiusMain * axisT2;
    m_layout.m_positions.get<glm::vec3>(vertexCurrCount + subdiv_main * subdiv_in + 1) = center + radiusMain * axisT2;
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  for (std::size_t uM = 0; uM < subdiv_main; ++uM)
  {
    const std::size_t indMA = vertexCurrCount + subdiv_in * uM;
    const std::size_t indMB = (uM == subdiv_main - 1) ? vertexCurrCount + subdiv_in * uM : indMA + subdiv_in;

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
  if (closed)
  {
    const std::size_t indBaseA = vertexCurrCount + subdiv_main * subdiv_in + 0;
    const std::size_t indBaseB = vertexCurrCount + subdiv_main * subdiv_in + 1;
    for (std::size_t uI = 0; uI < subdiv_in; ++uI)
    {
      const std::size_t uIA = uI;
      const std::size_t uIB = (uI == subdiv_in - 1) ? 0 : uIA + 1;

      *(indicePtr++) = indBaseA;
      *(indicePtr++) = vertexCurrCount + uIA;
      *(indicePtr++) = vertexCurrCount + uIB;

      *(indicePtr++) = indBaseB;
      *(indicePtr++) = vertexCurrCount + (subdiv_main - 1) * subdiv_in + uIA;
      *(indicePtr++) = vertexCurrCount + (subdiv_main - 1) * subdiv_in + uIB;
    }
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(indiceCurrCount) + indiceAddCount);

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents for now. TODO
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)

  s_partInfo newPart("generated-uvsphere");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  computeBBoxPart(ipart); // TODO better - inline !!

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_square(const glm::mat4 &transform, const float edgeLength)
{
  const float halfSizeOu = edgeLength * 0.5f;
  const float halfSizeIn = - halfSizeOu;

  const glm::vec3 squarePt00 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeIn);
  const glm::vec3 squarePt01 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeOu);
  const glm::vec3 squarePt10 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeIn);
  const glm::vec3 squarePt11 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeOu);

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = 6;
  const std::size_t vertexAddCount = 4;

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 0) = squarePt00;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 1) = squarePt01;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 2) = squarePt11;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 3) = squarePt10;

  m_layout.m_index[indiceCurrCount + 0] = vertexCurrCount + 0;
  m_layout.m_index[indiceCurrCount + 1] = vertexCurrCount + 1;
  m_layout.m_index[indiceCurrCount + 2] = vertexCurrCount + 2;

  m_layout.m_index[indiceCurrCount + 3] = vertexCurrCount + 2;
  m_layout.m_index[indiceCurrCount + 4] = vertexCurrCount + 3;
  m_layout.m_index[indiceCurrCount + 5] = vertexCurrCount + 0;

  if (m_layout.m_normals.m_size != 0)
  {
    TRE_ASSERT(m_layout.m_normals.m_size == 3);
    const glm::vec3 outNormal = glm::normalize(glm::vec3(transform[1]));
    m_layout.m_normals.get<glm::vec3>(vertexCurrCount + 0) = outNormal;
    m_layout.m_normals.get<glm::vec3>(vertexCurrCount + 1) = outNormal;
    m_layout.m_normals.get<glm::vec3>(vertexCurrCount + 2) = outNormal;
    m_layout.m_normals.get<glm::vec3>(vertexCurrCount + 3) = outNormal;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  s_partInfo newPart("generated-square");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec4 t0 = glm::abs(halfSizeOu * transform[0]);
    const glm::vec4 t2 = glm::abs(halfSizeOu * transform[2]);
    const glm::vec4 tm = t0 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_square_wireframe(const glm::mat4 &transform, const float edgeLength)
{
  const float halfSizeOu = edgeLength * 0.5f;
  const float halfSizeIn = - halfSizeOu;

  const glm::vec3 squarePt00 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeIn);
  const glm::vec3 squarePt01 = glm::vec3(transform[3] + transform[0] * halfSizeIn + transform[2] * halfSizeOu);
  const glm::vec3 squarePt10 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeIn);
  const glm::vec3 squarePt11 = glm::vec3(transform[3] + transform[0] * halfSizeOu + transform[2] * halfSizeOu);

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = 8;
  const std::size_t vertexAddCount = 4;

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 0) = squarePt00;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 1) = squarePt01;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 2) = squarePt11;
  m_layout.m_positions.get<glm::vec3>(vertexCurrCount + 3) = squarePt10;

  m_layout.m_index[indiceCurrCount + 0] = vertexCurrCount + 0;
  m_layout.m_index[indiceCurrCount + 1] = vertexCurrCount + 1;
  m_layout.m_index[indiceCurrCount + 2] = vertexCurrCount + 1;
  m_layout.m_index[indiceCurrCount + 3] = vertexCurrCount + 2;
  m_layout.m_index[indiceCurrCount + 4] = vertexCurrCount + 2;
  m_layout.m_index[indiceCurrCount + 5] = vertexCurrCount + 3;
  m_layout.m_index[indiceCurrCount + 6] = vertexCurrCount + 3;
  m_layout.m_index[indiceCurrCount + 7] = vertexCurrCount + 0;

  TRE_ASSERT(m_layout.m_normals.m_size == 0);
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs generation.
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont handle tangents generation.

  s_partInfo newPart("generated-square-wireframe");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec4 t0 = glm::abs(halfSizeOu * transform[0]);
    const glm::vec4 t2 = glm::abs(halfSizeOu * transform[2]);
    const glm::vec4 tm = t0 + t2;
    const glm::vec4 bboxMin = transform[3] - tm;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec4 bboxMax = transform[3] + tm;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_tube(const glm::mat4 &transform, const float radius, const float heigth, const bool closed, const uint subdiv_r, const uint subdiv_h)
{
  TRE_ASSERT(subdiv_r > 0);
  TRE_ASSERT(subdiv_h > 0);

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = subdiv_r * subdiv_h * 6;
  const std::size_t vertexAddCount = subdiv_r * (subdiv_h + 1) + (closed ? 2 : 0);

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

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
    const glm::vec3 axisRd = cosf(angle) * axisT1 + sinf(angle) * axisT2;
    for (std::size_t ih = 0; ih < subdiv_h + 1; ++ih)
    {
      m_layout.m_positions.get<glm::vec3>(vertexCurrCount + ir * (subdiv_h + 1) + ih) = center + axisRd * radius + (hStart + ih * hStep) * axisUp;
    }
  }
  if (closed)
  {
    const std::size_t vertexBase1 = vertexCurrCount + subdiv_r * (subdiv_h + 1);
    m_layout.m_positions.get<glm::vec3>(vertexBase1 + 0) = center + -heigth * 0.5f * axisUp;
    m_layout.m_positions.get<glm::vec3>(vertexBase1 + 1) = center +  heigth * 0.5f * axisUp;
  }

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  for (std::size_t ir = 0; ir < subdiv_r; ++ir)
  {
    const std::size_t base0 = vertexCurrCount + ir * (subdiv_h + 1);
    const std::size_t base1 = (ir == subdiv_r - 1) ? vertexCurrCount : base0 + subdiv_h + 1;

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

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(indiceCurrCount) + indiceAddCount);

  TRE_ASSERT(m_layout.m_normals.m_size == 0); // we dont generate the normals for now. TODO
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents for now. TODO
  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)

  s_partInfo newPart("generated-tube");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec3 t0 = glm::abs(radius * axisT1);
    const glm::vec3 t1 = glm::abs(hStart * axisUp);
    const glm::vec3 t2 = glm::abs(radius * axisT2);
    const glm::vec3 tm = t0 + t1 + t2;
    const glm::vec3 bboxMin = center - tm;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec3 bboxMax = center + tm;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_uvtrisphere(const glm::mat4 &transform, const float radius, const uint subdiv_u, const uint subdiv_v)
{
  const std::size_t nbU = ((subdiv_u / 2) + 1) * 2; // divisible per 2.
  const std::size_t nbV = subdiv_v > 1 ? subdiv_v : 1;

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = nbU * ((nbV * 2 - 1) * 3 + 3 + 3);
  const std::size_t vertexAddCount = 2 + (nbV * 2 + 1) * (nbU / 2);

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

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

  std::size_t vertexCurrent = vertexCurrCount;
  for (std::size_t u = 0; u < nbU / 2 ; ++u)
  {
    const float cosu0 = cosf((u * 2    ) * M_PI * 2.f / nbU);
    const float sinu0 = sinf((u * 2    ) * M_PI * 2.f / nbU);
    const float cosu1 = cosf((u * 2 + 1) * M_PI * 2.f / nbU);
    const float sinu1 = sinf((u * 2 + 1) * M_PI * 2.f / nbU);

    const glm::vec3 axisU0 = cosu0 * axisT1 + sinu0 * axisT2;
    const glm::vec3 axisU1 = cosu1 * axisT1 + sinu1 * axisT2;

    for (std::size_t v = 0; v < nbV; ++v)
    {
      const float cosv =   sinf((v + 1) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - cosf((v + 1) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU0 + sinv * axisUp;
      m_layout.m_normals.get<glm::vec3>(vertexCurrent) = outN;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
    for (std::size_t v = 0; v < nbV + 1; ++v)
    {
      const float cosv =   sinf((v + 0.5f) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - cosf((v + 0.5f) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU1 + sinv * axisUp;
      m_layout.m_normals.get<glm::vec3>(vertexCurrent) = outN;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
  }
  m_layout.m_normals.get<glm::vec3>(vertexCurrent) = -axisUp;
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center - radius * axisUp;
  m_layout.m_normals.get<glm::vec3>(vertexCurrent) = axisUp;
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * axisUp;
  TRE_ASSERT(vertexCurrent - vertexCurrCount == vertexAddCount);

  // indice ...

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  const std::size_t baseClose = vertexCurrCount + (nbU/2) * (nbV * 2 + 1);
  for (std::size_t u = 0; u < nbU/2; ++u)
  {
    const std::size_t base0 = vertexCurrCount + u * (nbV * 2 + 1);
    const std::size_t base1 = base0 + nbV;
    const std::size_t base0Next = (u == nbU/2 - 1) ? vertexCurrCount : base1 + nbV + 1;
    const std::size_t base1Next = base0Next + nbV;
    for (std::size_t v = 0; v < nbV - 1; ++v)
    {
      *(indicePtr++) = base0 + v;
      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0 + v + 1;

      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0Next + v;
      *(indicePtr++) = base0Next + v + 1;
    }
    for (std::size_t v = 0; v < nbV; ++v)
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

      *(indicePtr++) = base1 + nbV;
      *(indicePtr++) = base0Next + nbV - 1;
      *(indicePtr++) = base1Next + nbV;
    }
    {
      *(indicePtr++) = baseClose;
      *(indicePtr++) = base1Next;
      *(indicePtr++) = base1;

      *(indicePtr++) = baseClose + 1;
      *(indicePtr++) = base1 + nbV;
      *(indicePtr++) = base1Next + nbV;
    }
  }

  TRE_ASSERT(indicePtr == m_layout.m_index.getPointer(indiceCurrCount) + indiceAddCount);

  if (!generateNormals)
  {
    m_layout.m_normals.m_data = nullptr;
    m_layout.m_normals.m_size = 0;
    m_layout.m_normals.m_stride = 0;
  }

  TRE_ASSERT(m_layout.m_uvs.m_size == 0); // we dont handle UVs (for now ?)
  TRE_ASSERT(m_layout.m_tangents.m_size == 0); // we dont generate the tangents (for now ?)

  s_partInfo newPart("generated-uvtrisphere");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec3 rr = glm::vec3(fabsf(radius));
    const glm::vec3 bboxMin = center - rr;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec3 bboxMax = center + rr;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

std::size_t modelIndexed::createPartFromPrimitive_uvtrisphere_wireframe(const glm::mat4 &transform, const float radius, const uint subdiv_u, const uint subdiv_v)
{
  const std::size_t nbU = ((subdiv_u / 2) + 1) * 2; // divisible per 2.
  const std::size_t nbV = subdiv_v > 1 ? subdiv_v : 1;

  const std::size_t indiceCurrCount = get_NextAvailable_index();
  const std::size_t vertexCurrCount = get_NextAvailable_vertex();
  const std::size_t indiceAddCount = 0; // TODO ...... nbU * (nbV * 2 - 1);
  const std::size_t vertexAddCount = 2 + (nbV * 2 + 1) * (nbU / 2);

  reserveIndex(indiceCurrCount + indiceAddCount);
  reserveVertex(vertexCurrCount + vertexAddCount);

  // vertex ...

  const glm::vec3 center = transform[3];
  const glm::vec3 axisUp = transform[1];
  const glm::vec3 axisT1 = transform[2];
  const glm::vec3 axisT2 = transform[0];

  std::size_t vertexCurrent = vertexCurrCount;
  for (std::size_t u = 0; u < nbU / 2 ; ++u)
  {
    const float cosu0 = cosf((u * 2    ) * M_PI * 2.f / nbU);
    const float sinu0 = sinf((u * 2    ) * M_PI * 2.f / nbU);
    const float cosu1 = cosf((u * 2 + 1) * M_PI * 2.f / nbU);
    const float sinu1 = sinf((u * 2 + 1) * M_PI * 2.f / nbU);

    const glm::vec3 axisU0 = cosu0 * axisT1 + sinu0 * axisT2;
    const glm::vec3 axisU1 = cosu1 * axisT1 + sinu1 * axisT2;

    for (std::size_t v = 0; v < nbV; ++v)
    {
      const float cosv =   sinf((v + 1) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - cosf((v + 1) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU0 + sinv * axisUp;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
    for (std::size_t v = 0; v < nbV + 1; ++v)
    {
      const float cosv =   sinf((v + 0.5f) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const float sinv = - cosf((v + 0.5f) * M_PI / (nbV + 2)); // in fact, angle is in range [-pi,pi]
      const glm::vec3 outN = cosv * axisU1 + sinv * axisUp;
      m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * outN;
    }
  }
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center - radius * axisUp;
  m_layout.m_positions.get<glm::vec3>(vertexCurrent++) = center + radius * axisUp;
  TRE_ASSERT(vertexCurrent - vertexCurrCount == vertexAddCount);

  // indice ...

  TRE_FATAL("not reached. TODO");

  GLuint * indicePtr = m_layout.m_index.getPointer(indiceCurrCount);

  const std::size_t baseClose = vertexCurrCount + (nbU/2) * (nbV * 2 + 1);
  for (std::size_t u = 0; u < nbU/2; ++u)
  {
    const std::size_t base0 = vertexCurrCount + u * (nbV * 2 + 1);
    const std::size_t base1 = base0 + nbV;
    const std::size_t base0Next = (u == nbU/2 - 1) ? vertexCurrCount : base1 + nbV + 1;
    const std::size_t base1Next = base0Next + nbV;
    for (std::size_t v = 0; v < nbV - 1; ++v)
    {
      *(indicePtr++) = base0 + v;
      *(indicePtr++) = base0 + v + 1;
      *(indicePtr++) = base1 + v + 1;

      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0Next + v + 1;
      *(indicePtr++) = base0Next + v;
    }
    for (std::size_t v = 0; v < nbV; ++v)
    {
      *(indicePtr++) = base0 + v;
      *(indicePtr++) = base1 + v;
      *(indicePtr++) = base1 + v + 1;

      *(indicePtr++) = base1 + v;
      *(indicePtr++) = base1 + v + 1;
      *(indicePtr++) = base0Next + v;
    }
    {
      *(indicePtr++) = base1;
      *(indicePtr++) = base0Next;
      *(indicePtr++) = base1Next;

      *(indicePtr++) = base0Next + nbV - 1;
      *(indicePtr++) = base1Next + nbV;
      *(indicePtr++) = base1 + nbV;
    }
    {
      *(indicePtr++) = base1;
      *(indicePtr++) = base1Next;
      *(indicePtr++) = baseClose;

      *(indicePtr++) = baseClose + 1;
      *(indicePtr++) = base1Next + nbV;
      *(indicePtr++) = base1 + nbV;
    }
  }

  TRE_ASSERT(indicePtr - m_layout.m_index.getPointer(indiceCurrCount) == indiceAddCount);

  TRE_ASSERT(m_layout.m_normals.m_size == 0);
  TRE_ASSERT(m_layout.m_uvs.m_size == 0);
  TRE_ASSERT(m_layout.m_tangents.m_size == 0);

  s_partInfo newPart("generated-uvtrisphere_wireframe");
  newPart.m_offset = indiceCurrCount;
  newPart.m_size = indiceAddCount;
  const std::size_t ipart = m_partInfo.size();
  m_partInfo.push_back(newPart);

  {
    const glm::vec3 rr = glm::vec3(fabsf(radius));
    const glm::vec3 bboxMin = center - rr;
    m_partInfo[ipart].m_bbox.m_min = bboxMin;
    const glm::vec3 bboxMax = center + rr;
    m_partInfo[ipart].m_bbox.m_max = bboxMax;
  }

  return ipart;
}

//=============================================================================

} // namespace
