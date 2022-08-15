
#include "shader.h"
#include "rendertarget.h"
#include "model.h"
#include "model_tools.h"
#include "font.h"
#include "ui.h"
#include "windowHelper.h"

#include <string>
#include <chrono>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale
#include <glm/gtx/component_wise.hpp>

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

struct s_taskMeshProcessingContext
{
  // main mesh
  tre::model  *m_mesh = nullptr;  //[input]
  std::size_t  m_part = 0;        //[input]
  std::size_t  m_partDecimateLevel1 = 0;

  // 2D-mesh
  tre::modelRaw2D         *m_mesh2D;  //[input]
  std::size_t              m_partEnvelop2D = 0;

  // Raw-results
  std::vector<glm::vec2> m_envelop2D;
  std::vector<uint>      m_tetrahedrons;

  // Debug mesh
  tre::modelIndexed  *m_meshDebug = nullptr;  //[input]
  std::size_t         m_partDebug_Slot1 = 0;
  std::size_t         m_partDebug_Slot2 = 0;
  std::size_t         m_partDebug_Slot3 = 0;

  // Status
  bool   m_completed = false;
  double m_timeElapsedDecimateLevel1 = 0.;
  double m_timeElapsedEnvelop2D = 0.;
  double m_timeElapsedTetrahedrization = 0.;
  double m_timeElapsedDebug = 0.;
  bool   m_ongoing = false; // warning: not used in the task, only on the main-thread.
  float  m_progressTetrahedrization = 0.f;

  typedef std::chrono::steady_clock systemclock;
  typedef systemclock::time_point   systemtick;

  void run()
  {
    systemtick frameStartTick = systemclock::now();

   m_partDecimateLevel1 = m_mesh->decimatePart(m_part, 0.05f);

    systemtick frameStep1Tick = systemclock::now();

    tre::modelTools::computeConvexeEnvelop2D_XY(m_mesh->layout(), m_mesh->partInfo(m_part), glm::mat4(1.f), 1.e-2f, m_envelop2D);

    systemtick frameStep2Tick = systemclock::now();

    std::function<void(float)> fnNotify = [this](float progress) { this->m_progressTetrahedrization = progress; };
    tre::modelTools::tetrahedralize(m_mesh->layout(), m_mesh->partInfo(m_part), m_tetrahedrons, &fnNotify);

    systemtick frameEndTick = systemclock::now();

    // upload the envelop
    m_partEnvelop2D = m_mesh2D->createPart(m_envelop2D.size() * 2);
    const glm::vec4 envColor = glm::vec4(1.f, 0.f, 0.f, 1.f);
    for (std::size_t ipt = 0; ipt < m_envelop2D.size() - 1; ++ipt)
      m_mesh2D->fillDataLine(m_partEnvelop2D, ipt * 2, m_envelop2D[ipt], m_envelop2D[ipt + 1], envColor);
    m_mesh2D->fillDataLine(m_partEnvelop2D, m_envelop2D.size() * 2 - 2, m_envelop2D.back(), m_envelop2D.front(), envColor);

    // compute debug mesh
    m_partDebug_Slot1 = computeMeshDebugDiff(*m_meshDebug, *m_mesh, m_part,               *m_mesh, m_partDecimateLevel1);
    m_partDebug_Slot2 = computeMeshDebugDiff(*m_meshDebug, *m_mesh, m_partDecimateLevel1, *m_mesh, m_part);
    m_partDebug_Slot3 = computeMeshDebugTetra(*m_meshDebug, *m_mesh, m_tetrahedrons);

    systemtick frameDebug = systemclock::now();

    // end
    m_timeElapsedDecimateLevel1   = std::chrono::duration<double>(frameStep1Tick - frameStartTick).count();
    m_timeElapsedEnvelop2D        = std::chrono::duration<double>(frameStep2Tick - frameStep1Tick).count();
    m_timeElapsedTetrahedrization = std::chrono::duration<double>(frameEndTick - frameStep2Tick).count();
    m_timeElapsedDebug            = std::chrono::duration<double>(frameDebug - frameEndTick).count();

    m_completed = true;
  }

  static std::size_t computeMeshDebugDiff(tre::modelIndexed &meshOut,
                                          const tre::model &meshIn, const std::size_t partIn,
                                          const tre::model &meshCompare, const std::size_t partCompare)
  {
    const std::size_t meshOriginIndiceStart = meshIn.partInfo(partIn).m_offset;
    const std::size_t meshOriginIndiceCount = meshIn.partInfo(partIn).m_size;

    const tre::s_modelDataLayout &inputLayout = meshIn.layout();

    // Curvature

    std::vector<float> curvatureNormalizedPerVertex;
    curvatureNormalizedPerVertex.resize(inputLayout.m_vertexCount, 1.f);
    {
      // compute connectivity - copy of "_computeConnectivity_vert2tri" !!
      std::vector<std::vector<unsigned> > vertexToTriangles;
      vertexToTriangles.resize(inputLayout.m_vertexCount);
      const std::size_t istop = meshOriginIndiceStart + meshOriginIndiceCount;
      for (std::size_t i = meshOriginIndiceStart, iT = 0; i < istop; i+=3, ++iT)
      {
        vertexToTriangles[inputLayout.m_index[i + 0]].push_back(iT);
        vertexToTriangles[inputLayout.m_index[i + 1]].push_back(iT);
        vertexToTriangles[inputLayout.m_index[i + 2]].push_back(iT);
      }

      // compute curvature per vertex
      for (std::size_t iV = 0; iV < inputLayout.m_vertexCount; ++iV)
      {
        // get vertice around "iV" + get the normal
        const std::vector<unsigned> &neighbors = vertexToTriangles[iV];
        if (neighbors.size() < 2)
          continue;
        std::vector<unsigned> neighborsVertice;
        neighborsVertice.reserve(2 * neighbors.size());
        for (unsigned iT : neighbors)
        {
          const unsigned vertexA = inputLayout.m_index[meshOriginIndiceStart + iT * 3 + 0];
          if (vertexA != iV)
            neighborsVertice.push_back(vertexA);
          const unsigned vertexB = inputLayout.m_index[meshOriginIndiceStart + iT * 3 + 1];
          if (vertexB != iV)
            neighborsVertice.push_back(vertexB);
          const unsigned vertexC = inputLayout.m_index[meshOriginIndiceStart + iT * 3 + 2];
          if (vertexC != iV)
            neighborsVertice.push_back(vertexC);
          tre::sortAndUniqueBull(neighborsVertice);
        }
        std::vector<glm::vec3> listPts(neighborsVertice.size());
        for (std::size_t i = 0; i < neighborsVertice.size(); ++i)
          listPts[i] = inputLayout.m_positions.get<glm::vec3>(neighborsVertice[i]);
        glm::vec3 curve1, curve2;
        tre::surfaceCurvature(inputLayout.m_positions.get<glm::vec3>(iV), inputLayout.m_normals.get<glm::vec3>(iV), listPts, curve1, curve2);
        const float curvature = glm::length(curve1) + glm::length(curve2);
        curvatureNormalizedPerVertex[iV] = expf(-1.f * curvature);
      }
    }

    // Hausdorff-distance

    std::vector<float> distanceNormalizedPerVertex;
    distanceNormalizedPerVertex.resize(inputLayout.m_vertexCount, 2.f);

    const std::size_t meshCompareIndiceStart = meshCompare.partInfo(partCompare).m_offset;
    const std::size_t meshCompareIndiceCount = meshCompare.partInfo(partCompare).m_size;
    const tre::s_modelDataLayout &compareLayout = meshCompare.layout();

    for (std::size_t i = meshOriginIndiceStart, istop = meshOriginIndiceStart + meshOriginIndiceCount; i < istop; ++i)
    {
      const unsigned ivert = inputLayout.m_index[i];
      if (distanceNormalizedPerVertex[ivert] < 2.f)
        continue;
      const glm::vec3 pt  = inputLayout.m_positions.get<glm::vec3>(ivert);
      const glm::vec3 dir = inputLayout.m_normals.get<glm::vec3>(ivert);
      float distance = std::numeric_limits<float>::infinity();
      // naive !!!
      for (std::size_t ic = meshCompareIndiceStart, icstop = meshCompareIndiceStart + meshCompareIndiceCount; ic < icstop; ic += 3)
      {
        const glm::vec3 ptA = compareLayout.m_positions.get<glm::vec3>(compareLayout.m_index[ic + 0]);
        const glm::vec3 ptB = compareLayout.m_positions.get<glm::vec3>(compareLayout.m_index[ic + 1]);
        const glm::vec3 ptC = compareLayout.m_positions.get<glm::vec3>(compareLayout.m_index[ic + 2]);
        distance = std::min(distance, fabsf(tre::triangleProject3D(ptA, ptB, ptC, pt, dir)));
      }
      distanceNormalizedPerVertex[ivert] = expf(-1.e2f * distance);
    }

    std::vector<GLuint> indicesOut(meshOriginIndiceCount);
    for (std::size_t i = 0; i < meshOriginIndiceCount; ++i) indicesOut[i] = i;
    const std::size_t partOut = meshOut.createPartFromIndexes(indicesOut.data(), meshOriginIndiceCount);

    const std::size_t meshOutIndiceStart = meshOut.layout().m_index[meshOut.partInfo(partOut).m_offset];
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = meshOut.layout().m_positions.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> norIt = meshOut.layout().m_normals.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec4> colIt = meshOut.layout().m_colors.begin<glm::vec4>(meshOutIndiceStart);

    for (std::size_t i = meshOriginIndiceStart, istop = meshOriginIndiceStart + meshOriginIndiceCount; i < istop; i += 3)
    {
      const unsigned indA = inputLayout.m_index[i + 0];
      const unsigned indB = inputLayout.m_index[i + 1];
      const unsigned indC = inputLayout.m_index[i + 2];
      const glm::vec3 ptA = inputLayout.m_positions.get<glm::vec3>(indA);
      const glm::vec3 ptB = inputLayout.m_positions.get<glm::vec3>(indB);
      const glm::vec3 ptC = inputLayout.m_positions.get<glm::vec3>(indC);
      float area, quality;
      tre::triangleQuality(ptA, ptB, ptC, &area, &quality);
      const float normalizedArea = 1.f - expf(-1.e2f * area);
      *posIt++ = ptA;
      *posIt++ = ptB;
      *posIt++ = ptC;
      *norIt++ = inputLayout.m_normals.get<glm::vec3>(indA);
      *norIt++ = inputLayout.m_normals.get<glm::vec3>(indB);
      *norIt++ = inputLayout.m_normals.get<glm::vec3>(indC);
      *colIt++ = glm::vec4(normalizedArea, quality, curvatureNormalizedPerVertex[indA], distanceNormalizedPerVertex[indA]);
      *colIt++ = glm::vec4(normalizedArea, quality, curvatureNormalizedPerVertex[indB], distanceNormalizedPerVertex[indB]);
      *colIt++ = glm::vec4(normalizedArea, quality, curvatureNormalizedPerVertex[indC], distanceNormalizedPerVertex[indC]);
    }

    return partOut;
  }

  static std::size_t computeMeshDebugTetra(tre::modelIndexed &meshOut, const tre::model &meshIn, const std::vector<uint> &listTetra)
  {
    TRE_ASSERT(listTetra.size() % 4 == 0);
    const std::size_t inTetraCount = listTetra.size() / 4;
    const std::size_t outVertexCount = inTetraCount * 12;

    const tre::s_modelDataLayout::s_vertexData &inPositions = meshIn.layout().m_positions;

    if (inTetraCount == 0)
      return meshOut.createRawPart(0);

    std::vector<GLuint> indicesOut(outVertexCount);
    for (std::size_t i = 0; i < outVertexCount; ++i) indicesOut[i] = i;
    const std::size_t partOut = meshOut.createPartFromIndexes(indicesOut.data(), outVertexCount);

    const std::size_t meshOutIndiceStart = meshOut.layout().m_index[meshOut.partInfo(partOut).m_offset];
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = meshOut.layout().m_positions.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> norIt = meshOut.layout().m_normals.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec4> colIt = meshOut.layout().m_colors.begin<glm::vec4>(meshOutIndiceStart);

    for (std::size_t iT = 0; iT < inTetraCount; ++iT)
    {
      const glm::vec3 ptA = inPositions.get<glm::vec3>(listTetra[iT * 4 + 0]);
      const glm::vec3 ptB = inPositions.get<glm::vec3>(listTetra[iT * 4 + 1]);
      const glm::vec3 ptC = inPositions.get<glm::vec3>(listTetra[iT * 4 + 2]);
      const glm::vec3 ptD = inPositions.get<glm::vec3>(listTetra[iT * 4 + 3]);
      const glm::vec3 ptCenter = 0.25f * (ptA + ptB + ptC + ptD); // not barycentric - but dont care.
      const glm::vec3 ptA_in = 0.95f * ptA + 0.05f * ptCenter;
      const glm::vec3 ptB_in = 0.95f * ptB + 0.05f * ptCenter;
      const glm::vec3 ptC_in = 0.95f * ptC + 0.05f * ptCenter;
      const glm::vec3 ptD_in = 0.95f * ptD + 0.05f * ptCenter;
      glm::vec3       nABC = glm::normalize(glm::cross(ptB - ptA, ptC - ptA));
      if (glm::dot(nABC, ptD - ptA) > 0.f) nABC = -nABC;
      glm::vec3       nABD = glm::normalize(glm::cross(ptB - ptA, ptD - ptA));
      if (glm::dot(nABD, ptC - ptA) > 0.f) nABD = -nABD;
      glm::vec3       nACD = glm::normalize(glm::cross(ptC - ptA, ptD - ptA));
      if (glm::dot(nACD, ptB - ptA) > 0.f) nACD = -nACD;
      glm::vec3       nBCD = glm::normalize(glm::cross(ptC - ptB, ptD - ptB));
      if (glm::dot(nBCD, ptA - ptB) > 0.f) nBCD = -nBCD;

      float volume, quality;
      tre::tetrahedronQuality(ptA, ptB, ptC, ptD, &volume, &quality);
      volume = 1.f - expf(-1.e0f * volume); // normalized volume
      quality = 1.f - (1.f - quality) * (1.f - quality); // remapped quality
      const glm::vec4 col = glm::vec4(volume, quality, 0.f, 0.f);

      *posIt++ = ptA_in;
      *posIt++ = ptB_in;
      *posIt++ = ptC_in;
      *norIt++ = nABC;
      *norIt++ = nABC;
      *norIt++ = nABC;
      *colIt++ = col;
      *colIt++ = col;
      *colIt++ = col;

      *posIt++ = ptA_in;
      *posIt++ = ptB_in;
      *posIt++ = ptD_in;
      *norIt++ = nABD;
      *norIt++ = nABD;
      *norIt++ = nABD;
      *colIt++ = col;
      *colIt++ = col;
      *colIt++ = col;

      *posIt++ = ptA_in;
      *posIt++ = ptC_in;
      *posIt++ = ptD_in;
      *norIt++ = nACD;
      *norIt++ = nACD;
      *norIt++ = nACD;
      *colIt++ = col;
      *colIt++ = col;
      *colIt++ = col;

      *posIt++ = ptB_in;
      *posIt++ = ptC_in;
      *posIt++ = ptD_in;
      *norIt++ = nBCD;
      *norIt++ = nBCD;
      *norIt++ = nBCD;
      *colIt++ = col;
      *colIt++ = col;
      *colIt++ = col;
    }

    return partOut;
  }
};

int taskMeshOperation(void *arg)
{
  s_taskMeshProcessingContext *context = reinterpret_cast<s_taskMeshProcessingContext*>(arg);
  context->run();
  return 0;
}

// =============================================================================

int main(int argc, char **argv)
{
  // - Arguments

  char meshFile[128] = TESTIMPORTPATH "resources/objects.obj";
  //char meshFile[128] = TESTIMPORTPATH "resources/test.obj";

  if (argc >= 2)
  {
    strncpy(static_cast<char*>(meshFile), argv[1], 256);
  }

  tre::windowHelper myWindow;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "test Mesh 3D", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

  // - Upload mesh

  tre::modelSemiDynamic3D meshes(0, tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL);
#define TEST_ID 0
#if TEST_ID == 1
  meshes.createPartFromPrimitive_box(glm::mat4(1.f), 2.f);
  //meshes.createPartFromPrimitive_cone(glm::mat4(1.f), 1.f, 1.f, 14);
  meshes.createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), 1.f, 10, 7);
#elif TEST_ID == 2 // distorted prisme
  {
    const float     cosA = cosf(2.6f), sinA = sinf(2.6f);
    const GLuint    indices[8 * 3] = { 0,1,2,  3,4,5,  0,2,5,5,3,0, 2,1,4,4,5,2, 1,4,3,3,0,1 };
    const GLfloat   vertices[6 * 3] = { -0.5f,0.f,-2.f,  0.5f,0.f,-2.f,  0.f,0.7f,-2.f,
                                        -0.5f*cosA,-0.5f*sinA,2.f,  0.5f*cosA,0.5f*sinA,2.f,  -0.7f*sinA,0.7f*cosA,2.f };
    meshes.createPartFromIndexes(&indices[0], 24, &vertices[0]);
    meshes.layout().m_normals.get<glm::vec3>(0) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(0));
    meshes.layout().m_normals.get<glm::vec3>(1) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(1));
    meshes.layout().m_normals.get<glm::vec3>(2) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(2));
    meshes.layout().m_normals.get<glm::vec3>(3) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(3));
    meshes.layout().m_normals.get<glm::vec3>(4) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(4));
    meshes.layout().m_normals.get<glm::vec3>(5) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(5));
  }
#elif TEST_ID == 0
  if (!meshes.loadfromWavefront(meshFile))
  {
    TRE_LOG("Fail to load " << meshFile);
    myWindow.OpenGLQuit();
    myWindow.SDLQuit();
    return -3;
  }
  else
  {
    TRE_LOG("Mesh " << meshFile << "loaded successfully");
  }
#endif

  // - Re-scale mesh to [-1,1]

  for (std::size_t iPart = 0; iPart < meshes.partCount(); ++iPart)
  {
    TRE_LOG("Mesh part " << iPart << " : name = " << meshes.partInfo(iPart).m_name << ", indiceCount = " << meshes.partInfo(iPart).m_size);
    meshes.computeBBoxPart(iPart);
    const tre::s_boundbox &bbox = meshes.partInfo(iPart).m_bbox;
    const glm::vec3 extend = bbox.extend();
    const float     extendMax = glm::compMax(extend);
    TRE_ASSERT(extendMax > 0.f);
    glm::mat4 tr(1.f);
    tr[0][0] = tr[1][1] = tr[2][2] = 2.f / extendMax;
    tr[3] = glm::vec4(-bbox.center() * 2.f / extendMax, 1.f);
    meshes.transformPart(iPart, tr);
  }

  meshes.loadIntoGPU();

  tre::modelRaw2D mesh2D;

  mesh2D.loadIntoGPU();

  tre::modelSemiDynamic3D meshDebug(0, tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_COLOR);

  meshDebug.loadIntoGPU();

  // - Create thread context for mesh processing

  const std::size_t meshPartCount = meshes.partCount();
  std::size_t       meshPartSelected = 3; // TODO: 0

  if (argc >= 3)
  {
    meshPartSelected = std::size_t(atoi(argv[2]));
  }

  std::vector<s_taskMeshProcessingContext> meshPartContext;
  meshPartContext.resize(meshPartCount);
  for (std::size_t iPart = 0; iPart < meshPartCount; ++iPart)
  {
    meshPartContext[iPart].m_mesh = &meshes;
    meshPartContext[iPart].m_part = iPart;
    meshPartContext[iPart].m_mesh2D = &mesh2D;
    meshPartContext[iPart].m_meshDebug = &meshDebug;
  }

  // - Load shaders

  tre::shader shaderMainMaterial;
  shaderMainMaterial.loadShader(tre::shader::PRGM_3D,
                                tre::shader::PRGM_UNICOLOR |
                                tre::shader::PRGM_LIGHT_SUN);

  tre::shader::s_UBOdata_sunLight sunLight;
  sunLight.direction = glm::normalize(glm::vec3(-0.243f,-0.970f,0.f));
  sunLight.color = glm::vec3(0.9f);
  sunLight.colorAmbiant = glm::vec3(0.4f);

  tre::shader shaderWireframe;
  tre::shader shaderWireframePlain;
  {
    const char *srcGeom_Wireframe_plain =
    "#version 330 core\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 3) out;\n"
    "in vec3 geomNormal[];\n"
    "out vec3 pixelNormal;\n"
    "noperspective out vec3 pixelDist;\n"
    "void main(){\n"
    "  vec2 v0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;\n"
    "  vec2 v1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;\n"
    "  vec2 v2 = gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;\n"
    "  vec2 e01 = v1 - v0;\n"
    "  vec2 e12 = v2 - v1;\n"
    "  vec2 e20 = v0 - v2;\n"
    "  float area = abs(e01.x*e12.y - e01.y*e12.x);\n"
    "  \n"
    "  gl_Position = gl_in[0].gl_Position;\n"
    "  pixelNormal = geomNormal[0];\n"
    "  pixelDist = vec3(area/length(e12), 0., 0.);\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[1].gl_Position;\n"
    "  pixelNormal = geomNormal[1];\n"
    "  pixelDist = vec3(0., area/length(e20), 0.);\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[2].gl_Position;\n"
    "  pixelNormal = geomNormal[2];\n"
    "  pixelDist = vec3(0., 0., area/length(e01));\n"
    "  EmitVertex();\n"
    "  \n"
    "  EndPrimitive();\n"
    "}\n";

    const char *srcGeom_Wireframe_line =
    "#version 330 core\n"
    "layout(triangles) in;\n"
    "layout(line_strip, max_vertices = 4) out;\n"
    "in vec3 geomNormal[];\n"
    "out vec3 pixelNormal;\n"
    "noperspective out vec3 pixelDist;\n"
    "void main(){\n"
    "  vec2 v0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;\n"
    "  vec2 v1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;\n"
    "  vec2 v2 = gl_in[2].gl_Position.xy / gl_in[2].gl_Position.w;\n"
    "  vec2 e01 = v1 - v0;\n"
    "  vec2 e12 = v2 - v1;\n"
    "  vec2 e20 = v0 - v2;\n"
    "  float area = abs(e01.x*e12.y - e01.y*e12.x);\n"
    "  \n"
    "  gl_Position = gl_in[0].gl_Position;\n"
    "  pixelNormal = geomNormal[0];\n"
    "  pixelDist = vec3(area/length(e12), 0., 0.);\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[1].gl_Position;\n"
    "  pixelNormal = geomNormal[1];\n"
    "  pixelDist = vec3(0., area/length(e20), 0.);\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[2].gl_Position;\n"
    "  pixelNormal = geomNormal[2];\n"
    "  pixelDist = vec3(0., 0., area/length(e01));\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[0].gl_Position;\n"
    "  pixelNormal = geomNormal[0];\n"
    "  pixelDist = vec3(area/length(e12), 0., 0.);\n"
    "  EmitVertex();\n"
    "  \n"
    "  EndPrimitive();\n"
    "}\n";

    const char *srcFrag_Wireframe =
    "#version 330 core\n"
    "layout(location = 0) out vec4 color;\n"
    "in vec3 pixelNormal;\n"
    "noperspective in vec3 pixelDist;\n"
    "uniform vec4 uniColor;\n"
    "void main(){\n"
    "  vec4 diffuseColor = uniColor;\n"
    "  float dist = min(min(pixelDist.x, pixelDist.y), pixelDist.z);\n"
    "  dist = exp(-dist*dist*10000);\n"
    "  vec3 direction = vec3(0.,-1.,0.);\n"
    "  float sunCosTheta = clamp( -dot(pixelNormal,direction)*0.90+0.10, 0.f, 1.f);"
    "  color.xyz = /*sunCosTheta * */ diffuseColor.xyz * dist;\n"
    "  color.w   = diffuseColor.w;\n"
    "}\n";

    tre::shader::s_layout shLayout(tre::shader::PRGM_3D);
    shLayout.hasBUF_Normal = true;
    shLayout.hasUNI_MPVM = true;
    shLayout.hasUNI_MModel = true;
    shLayout.hasUNI_uniColor = true;
    shLayout.hasOUT_Color0 = true;
    shaderWireframe.loadCustomShaderWithGeom(shLayout,
                                             srcGeom_Wireframe_line,
                                             srcFrag_Wireframe,
                                            "wirefrime_line");

    shaderWireframePlain.loadCustomShaderWithGeom(shLayout,
                                                  srcGeom_Wireframe_plain,
                                                  srcFrag_Wireframe,
                                                 "wirefrime_plain");
  }

  tre::shader shaderDataVisu;
  {
    const char *srcFrag_Color =
    "vec3 Light(vec3 albedo)\n"
    "{\n"
    "  vec3 N = normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n"
    "  vec3 L = - normalize((MView * vec4(m_sunlight.direction, 0.f)).xyz);\n"
    "  vec3 V = - normalize((MView * vec4(pixelPosition, 1.f)).xyz);\n"
    "  float islighted_sun = 1.f;\n"
    "  vec3 lsun = BlinnPhong(albedo, m_sunlight.color,\n"
    "                         N, L, V,\n"
    "                         3.f, 0.5f, 0.f);\n"
    "  vec3 lamb = BlinnPhong_ambiante(albedo, m_sunlight.colorAmbiant,\n"
    "                                  N, L, 0.5f, 0.f);\n"
    "  return lsun * islighted_sun + lamb;\n"
    "}\n"
    "void main(){\n"
    "  float data = clamp(dot(uniColor, pixelColor), 0.f, 1.f);\n"
    "  vec4 diffuseColor = vec4(1.f - data, 2.f * (1.f - data) * data, data, 1.f);\n"
    "  color.xyz = Light(diffuseColor.xyz);\n"
    "  color.w   = diffuseColor.w;\n"
    "}\n";

    tre::shader::s_layout shLayout(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR | tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN);

    shaderDataVisu.loadCustomShader(shLayout, srcFrag_Color, "dataVisualisation");
  }

  tre::shader::updateUBO_sunLight(sunLight);

  tre::shader shaderMesh2D;
  shaderMesh2D.loadShader(tre::shader::PRGM_2Dto3D, tre::shader::PRGM_COLOR, "envelop2D");

  // - load UI

  tre::font font;
  {
    font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);
  }

  tre::baseUI2D bUI_main;
  bUI_main.set_defaultFont(&font);
  tre::ui::window &wUI_main = *bUI_main.create_window();
  wUI_main.set_layoutGrid(4, 2);
  wUI_main.set_fontSize(tre::ui::s_size(20, tre::ui::SIZE_PIXEL));
  wUI_main.set_color(glm::vec4(0.f, 0.f, 0.f, 0.5f));
  wUI_main.set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));

  wUI_main.create_widgetText(0, 0)->set_text("model (left/right):");
  wUI_main.create_widgetText(0, 1);
  wUI_main.create_widgetText(1, 0)->set_text("shader (F2/F3):");
  wUI_main.create_widgetText(1, 1);
  wUI_main.create_widgetText(2, 0)->set_text("visu (F6/F7):");
  wUI_main.create_widgetText(2, 1);
  wUI_main.create_widgetText(3, 0)->set_text("show tetra. (F9):");
  wUI_main.create_widgetText(3, 1);

  tre::ui::window &wUI_result = *bUI_main.create_window();
  wUI_result.set_layoutGrid(8, 3);
  wUI_result.set_fontSize(tre::ui::s_size(20, tre::ui::SIZE_PIXEL));
  wUI_result.set_color(glm::vec4(0.f, 0.f, 0.f, 0.5f));
  wUI_result.set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));
  wUI_result.set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);
  wUI_result.set_colAlignment(2, tre::ui::ALIGN_MASK_CENTERED);

  wUI_result.create_widgetText(0, 1)->set_text("size");
  wUI_result.create_widgetText(0, 2)->set_text("time");

  wUI_result.create_widgetText(1, 0)->set_text("model original");
  wUI_result.create_widgetText(1, 1);
  wUI_result.create_widgetText(1, 2)->set_text("--");

  wUI_result.create_widgetText(2, 0)->set_text("model decimated");
  wUI_result.create_widgetText(2, 1);
  wUI_result.create_widgetText(2, 2);

  wUI_result.create_widgetText(3, 0)->set_text("model envelop");
  wUI_result.create_widgetText(3, 1);
  wUI_result.create_widgetText(3, 2);

  wUI_result.create_widgetText(4, 0)->set_text("model tetra");
  wUI_result.create_widgetText(4, 1);
  wUI_result.create_widgetText(4, 2);

  wUI_result.create_widgetText(7, 0)->set_text("model debug");
  wUI_result.create_widgetText(7, 1)->set_text("--");
  wUI_result.create_widgetText(7, 2);

  bUI_main.loadShader();
  bUI_main.loadIntoGPU();

  // - scene and event variables

  SDL_Event event;

  int shaderMode = 0;
  const int NshaderMode = 4;
  tre::shader* listShader[NshaderMode] = { &shaderMainMaterial, &shaderWireframe, &shaderWireframePlain, &shaderDataVisu};

  int visuMode = 0;
  const int NvisuMode = 5;
  std::string listDataVisu[NvisuMode] = { "area", "quality", "curvature", "distance" };

  bool  showTetrahedrization = false;

  glm::mat4 mView = glm::mat4(1.f);
  mView[0][0] = 2.f; // viewport is on width/2
  mView[3][0] =  0.f;
  mView[3][1] = -2.f;
  mView[3][2] = -5.f;

  glm::mat4 mModelPrev = glm::mat4(1.f);
  glm::mat4 mModel = glm::mat4(1.f);

  float mModelScale = 1.f;

  // - MAIN LOOP ------------

  while(!myWindow.m_controls.m_quit)
  {
    // event actions + updates --------

    myWindow.m_controls.newFrame();
    myWindow.m_timing.newFrame(0, myWindow.m_controls.m_pause);

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myWindow.m_controls.treatSDLEvent(event);

      if (event.type == SDL_KEYDOWN)
      {
        if      (event.key.keysym.sym == SDLK_F1) { shaderMode = 0; }
        else if (event.key.keysym.sym == SDLK_F2) { shaderMode = shaderMode == 0 ? NshaderMode - 1 : shaderMode - 1; }
        else if (event.key.keysym.sym == SDLK_F3) { shaderMode = shaderMode == NshaderMode - 1 ? 0 : shaderMode + 1; }
        else if (event.key.keysym.sym == SDLK_F4) { shaderMode = NshaderMode - 1; }

        else if (event.key.keysym.sym == SDLK_F6) { visuMode = visuMode == 0 ? NvisuMode - 1 : visuMode - 1; }
        else if (event.key.keysym.sym == SDLK_F7) { visuMode = visuMode == NvisuMode - 1 ? 0 : visuMode + 1; }

        else if (event.key.keysym.sym == SDLK_F9) { showTetrahedrization = ! showTetrahedrization; }

        else if (event.key.keysym.sym == SDLK_o) { myWindow.m_timing.scenetime = 0.f; }

        else if (event.key.keysym.sym == SDLK_RIGHT)
          meshPartSelected = (meshPartSelected == meshPartCount - 1) ? 0 : meshPartSelected + 1;
        else if (event.key.keysym.sym == SDLK_LEFT)
          meshPartSelected = (meshPartSelected == 0) ? meshPartCount - 1 : meshPartSelected - 1;
      }
      else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
      {
        mModelPrev = mModel;
        myWindow.m_controls.m_pause = true;
      }
    }

    //if (myWindow.m_controls.m_keyUP  ) // TODO, additionnal control for debug
    //if (myWindow.m_controls.m_keyDOWN) // TODO, additionnal control for debug

    if (myWindow.m_controls.m_home) mModelScale = 1.f;
    if (myWindow.m_controls.m_mouse.z < 0.f) mModelScale *= 1.2f;
    if (myWindow.m_controls.m_mouse.z > 0.f) mModelScale /= 1.2f;
    if (!myWindow.m_controls.m_pause) myWindow.m_controls.m_mouseLEFT = 0; // hack, cancel any mouse action

    if ((myWindow.m_controls.m_mouseLEFT & tre::windowHelper::s_controls::MASK_BUTTON_PRESSED) != 0)
    {
      const glm::vec2 diff = glm::vec2(myWindow.m_controls.m_mouse - myWindow.m_controls.m_mousePrev) * myWindow.m_resolutioncurrentInv;
      mModel = glm::rotate(glm::rotate(glm::mat4(1.f), diff.x, glm::vec3(0.f,1.f,0.f)), diff.y, glm::vec3(1.f,0.f,0.f)) * mModelPrev;
    }

    //-> Mesh operations ------------

    bool hasMeshThreadRunning = false;
    for (const s_taskMeshProcessingContext &meshContextCurr : meshPartContext)
      hasMeshThreadRunning |= (meshContextCurr.m_ongoing && !meshContextCurr.m_completed);

    s_taskMeshProcessingContext &meshContextSelected = meshPartContext[meshPartSelected];
    if (!meshContextSelected.m_completed &&
        !meshContextSelected.m_ongoing &&
        !hasMeshThreadRunning /*multiple mesh op cannot run simultaneously, because they share the same meshes (createPart may relocate data ...)*/)
    {
      // create and launch a task
      meshContextSelected.m_ongoing = true;
      SDL_CreateThread(taskMeshOperation, "mesh-operation", static_cast<void*>(&meshContextSelected));
    }
    bool meshUpdateNeeded = false;
    for (s_taskMeshProcessingContext &meshContextCurr : meshPartContext)
    {
      if (meshContextCurr.m_completed)
      {
        if (meshContextCurr.m_ongoing) meshUpdateNeeded = true;
        meshContextCurr.m_ongoing = false;
      }
    }
    if (meshUpdateNeeded)
    {
      meshes.updateIntoGPU();
      meshDebug.updateIntoGPU();
      mesh2D.updateIntoGPU();
    }

    // main render pass -------------

    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // - render mesh

    glEnable(GL_DEPTH_TEST);

    tre::shader & curShader = * listShader[shaderMode];

    const glm::mat4 mPV(myWindow.m_matProjection3D * mView);
    const glm::vec4 ucolorMain(0.7f,0.7f,0.7f,1.f);
    const glm::vec4 uChoiceVisu(visuMode == 0 ? 1.f : 0.f,
                                visuMode == 1 ? 1.f : 0.f,
                                visuMode == 2 ? 1.f : 0.f,
                                visuMode == 3 ? 1.f : 0.f);

    glUseProgram(curShader.m_drawProgram);

    if (curShader.layout().hasUNI_uniColor && &curShader != &shaderDataVisu)
      glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMain));

    if (curShader.layout().hasUNI_uniColor && &curShader == &shaderDataVisu)
      glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uChoiceVisu));

    if (!myWindow.m_controls.m_pause)
      mModel = glm::rotate(mModel, myWindow.m_timing.frametime * 6.28f * 0.2f, glm::vec3(0.8f,0.6f,0.f));

    glm::mat4 curModel = mModel;
    curModel[0] *= mModelScale;
    curModel[1] *= mModelScale;
    curModel[2] *= mModelScale;
    curModel[3] = glm::vec4(0.f,2.0f,0.f,1.0f);

    curShader.setUniformMatrix(mPV * curModel, curModel);

    glViewport(0, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);

    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing && &curShader == &shaderDataVisu)
    {
      meshDebug.drawcall(meshContextSelected.m_partDebug_Slot1, 1, true);
    }
    else
    {
      meshes.drawcall(meshContextSelected.m_part, 1, true);
    }

    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      glViewport(myWindow.m_resolutioncurrent.x / 2, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);
      if (showTetrahedrization)
      {
        meshDebug.drawcall(meshContextSelected.m_partDebug_Slot3, 1, true);
      }
      else if (&curShader == &shaderDataVisu)
      {
        meshDebug.drawcall(meshContextSelected.m_partDebug_Slot2, 1, false);
      }
      else
      {
        meshes.drawcall(meshContextSelected.m_partDecimateLevel1, 1, false);
      }
    }

    tre::IsOpenGLok("main render pass - draw meshes");

    // - render Envelop

    glDisable(GL_DEPTH_TEST);

    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      glViewport(myWindow.m_resolutioncurrent.x / 2, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);
      glUseProgram(shaderMesh2D.m_drawProgram);
      shaderMesh2D.setUniformMatrix(mPV * curModel, curModel);
      mesh2D.drawcall(meshContextSelected.m_partEnvelop2D, 1, true, GL_LINES);
    }

    // - render UI

    glm::mat3 m2Dhalf = myWindow.m_matProjection2D;
    m2Dhalf[0][0] *= 2.f;
    bUI_main.updateCameraInfo(m2Dhalf, glm::ivec2(myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y));

    {
      glm::mat3 mat(1.f);
      mat[2][0] = - 1.f /  m2Dhalf[0][0];
      mat[2][1] =   1.f /  m2Dhalf[1][1];
      wUI_main.set_mat3(mat);
      wUI_result.set_mat3(mat);
    }

    wUI_main.get_widgetText(0, 1)->set_text(meshContextSelected.m_mesh->partInfo(meshContextSelected.m_part).m_name); // model
    wUI_main.get_widgetText(1, 1)->set_text(curShader.getName()); // shader
    if (&curShader == &shaderDataVisu)
      wUI_main.get_widgetText(2, 1)->set_text(listDataVisu[visuMode]);
    else
      wUI_main.get_widgetText(2, 1)->set_text("");
    wUI_main.get_widgetText(3, 1)->set_text(showTetrahedrization ? "ON" : "OFF");

    char txt[128];

    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      std::snprintf(txt, 127, "%ld tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_part).m_size / 3);
      wUI_result.get_widgetText(1, 1)->set_text(txt);

      std::snprintf(txt, 127, "%ld tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_partDecimateLevel1).m_size / 3);
      wUI_result.get_widgetText(2, 1)->set_text(txt);

      std::snprintf(txt, 127, "%d ms", int(meshContextSelected.m_timeElapsedDecimateLevel1 * 1.e3));
      wUI_result.get_widgetText(2, 2)->set_text(txt);

      std::snprintf(txt, 127, "%d pts", int(meshContextSelected.m_envelop2D.size()));
      wUI_result.get_widgetText(3, 1)->set_text(txt);

      std::snprintf(txt, 127, "%d ms", int(meshContextSelected.m_timeElapsedEnvelop2D * 1.e3));
      wUI_result.get_widgetText(3, 2)->set_text(txt);

      std::snprintf(txt, 127, "%d tetra", int(meshContextSelected.m_tetrahedrons.size() / 4));
      wUI_result.get_widgetText(4, 1)->set_text(txt);

      std::snprintf(txt, 127, "%d ms", int(meshContextSelected.m_timeElapsedTetrahedrization * 1.e3));
      wUI_result.get_widgetText(4, 2)->set_text(txt);

      std::snprintf(txt, 127, "%d ms", int(meshContextSelected.m_timeElapsedDebug * 1.e3));
      wUI_result.get_widgetText(7, 2)->set_text(txt);
    }
    else
    {
      wUI_result.get_widgetText(1, 1)->set_text("");
      wUI_result.get_widgetText(2, 1)->set_text("");
      wUI_result.get_widgetText(2, 2)->set_text("0 ms");
      wUI_result.get_widgetText(3, 1)->set_text("");
      wUI_result.get_widgetText(3, 2)->set_text("0 ms");

      std::snprintf(txt, 127, "%d %%", int(meshContextSelected.m_progressTetrahedrization * 100));
      wUI_result.get_widgetText(4, 1)->set_text(txt);

      wUI_result.get_widgetText(4, 2)->set_text("0 ms");
      wUI_result.get_widgetText(7, 2)->set_text("0 ms");
    }

    bUI_main.updateIntoGPU();

    glViewport(0, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);
    wUI_main.set_visible(true);
    wUI_result.set_visible(false);
    bUI_main.draw();

    glViewport(myWindow.m_resolutioncurrent.x / 2, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);
    wUI_main.set_visible(false);
    wUI_result.set_visible(true);
    bUI_main.draw();

    tre::IsOpenGLok("UI render pass");

    // end render pass --------------

    myWindow.m_timing.endFrame_beforeGPUPresent();

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  shaderMainMaterial.clearShader();
  shaderWireframe.clearShader();
  shaderWireframePlain.clearShader();

  shaderDataVisu.clearShader();

  tre::shader::clearUBO();

  shaderMesh2D.clearShader();

  meshes.clearGPU();
  meshDebug.clearGPU();
  mesh2D.clearGPU();

  font.clear();

  bUI_main.clear();
  bUI_main.clearGPU();
  bUI_main.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
