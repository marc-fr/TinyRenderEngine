
#include "tre_shader.h"
#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_font.h"
#include "tre_ui.h"
#include "tre_windowContext.h"

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
  // input
  tre::modelIndexed *m_mesh = nullptr;
  std::size_t        m_part = 0;

  // Results
  std::vector<glm::vec2> m_envelop2D;
  std::vector<uint>      m_tetrahedrons;
  glm::vec4              m_centerAndVolume;
  std::size_t            m_partDecimateCurv = std::size_t(-1); // note: the "color" contains visualization-data
  std::size_t            m_partDecimateVoxel = std::size_t(-1); // note: the "color" contains visualization-data
  std::size_t            m_partEnvelop2D = std::size_t(-1); // rendered with LINES
  std::size_t            m_partTetrahedrons = std::size_t(-1); // note: the "color" contains visualization-data
  std::size_t            m_partBBox = std::size_t(-1); // rendered with LINES

  // Status
  bool   m_completed = false;
  double m_timeElapsedDecimateCurv = 0.;
  double m_timeElapsedDecimateVoxel = 0.;
  double m_timeElapsedEnvelop2D = 0.;
  double m_timeElapsedTetrahedrization = 0.;
  double m_timeElapsedDebug = 0.;
  bool   m_ongoing = false; // warning: not used in the task, only on the main-thread.
  float  m_progressTetrahedrization = 0.f;

  typedef std::chrono::steady_clock systemclock;
  typedef systemclock::time_point   systemtick;

  void run()
  {
    const systemtick tick0 = systemclock::now();

    m_partDecimateCurv = tre::modelTools::decimateCurvature(*m_mesh, m_part, 0.2f);

    const systemtick tick1 = systemclock::now();

    m_partDecimateVoxel = tre::modelTools::decimateVoxel(*m_mesh, m_part, 0.2f, true);

     const systemtick tick2 = systemclock::now();

    tre::modelTools::computeConvexeEnvelop2D_XY(m_mesh->layout(), m_mesh->partInfo(m_part), glm::mat4(1.f), 1.e-2f, m_envelop2D);

    const systemtick tick3 = systemclock::now();

    std::function<void(float)> fnNotify = [this](float progress) { this->m_progressTetrahedrization = progress; };
    tre::modelTools::tetrahedralize(m_mesh->layout(), m_mesh->partInfo(m_part), m_tetrahedrons, &fnNotify);

    const systemtick tick4 = systemclock::now();

    // upload the envelop
    if (!m_envelop2D.empty())
    {
      std::size_t vOffset = 0;
      m_partEnvelop2D = m_mesh->createPart(m_envelop2D.size() * 2, m_envelop2D.size(), vOffset);
      TRE_ASSERT(m_mesh->layout().m_positions.isMatching(3, 10));
      TRE_ASSERT(m_mesh->layout().m_colors.isMatching(4, 10));
      TRE_ASSERT(m_mesh->layout().m_normals.isMatching(3, 10));
      GLfloat * __restrict dataV = m_mesh->layout().m_positions.m_data + 10 * vOffset;
      GLuint  * __restrict dataI = m_mesh->layout().m_index.m_data + m_mesh->partInfo(m_partEnvelop2D).m_offset;
      for (std::size_t ip = 0; ip < m_envelop2D.size(); ++ip)
      {
        *dataV++ = m_envelop2D[ip].x; *dataV++ = m_envelop2D[ip].y; *dataV++ =0.f; // pos
        *dataV++ = 1.f; *dataV++ = 1.f; *dataV++ = 1.f; *dataV++ = 1.f; // color
        *dataV++ = 0.f; *dataV++ = 0.f; *dataV++ = 1.f; // normal
        *dataI++ = uint(vOffset + ip);
        *dataI++ = uint(vOffset + (ip == m_envelop2D.size() - 1 ? 0 : ip + 1));
      }
    }

    // post treatment of decimation
    if (m_partDecimateVoxel != std::size_t(-1) && m_mesh->layout().m_normals.hasData())
      tre::modelTools::computeOutNormal(m_mesh->layout(), m_mesh->partInfo(m_partDecimateVoxel), true);

    // upload the tetra mesh
    m_partTetrahedrons = computeMeshDebugTetra(*m_mesh, m_tetrahedrons);

    // show center
    {
      m_centerAndVolume = tre::modelTools::computeBarycenter3D(m_mesh->layout(), m_mesh->partInfo((m_part)));
      const std::array<uint, 6> indices = { 0, 1, 2, 3, 4, 5};
      const float vertices[] = { m_centerAndVolume.x - 0.1f, m_centerAndVolume.y, m_centerAndVolume.z,
                                 m_centerAndVolume.x + 0.1f, m_centerAndVolume.y, m_centerAndVolume.z,
                                 m_centerAndVolume.x, m_centerAndVolume.y - 0.1f, m_centerAndVolume.z,
                                 m_centerAndVolume.x, m_centerAndVolume.y + 0.1f, m_centerAndVolume.z,
                                 m_centerAndVolume.x, m_centerAndVolume.y, m_centerAndVolume.z - 0.1f,
                                 m_centerAndVolume.x, m_centerAndVolume.y, m_centerAndVolume.z + 0.1f };
      m_partBBox = m_mesh->createPartFromIndexes(indices, vertices);
    }

    _computeVisuData();

    systemtick tickE = systemclock::now();

    // end
    m_timeElapsedDecimateCurv     = std::chrono::duration<double>(tick1 - tick0).count();
    m_timeElapsedDecimateVoxel    = std::chrono::duration<double>(tick2 - tick1).count();
    m_timeElapsedEnvelop2D        = std::chrono::duration<double>(tick3 - tick2).count();
    m_timeElapsedTetrahedrization = std::chrono::duration<double>(tick4 - tick3).count();
    m_timeElapsedDebug            = std::chrono::duration<double>(tickE - tick4).count();

    m_completed = true;
  }

  static std::size_t computeMeshDebugTetra(tre::modelIndexed &mesh, const std::vector<uint> &listTetra)
  {
    TRE_ASSERT(listTetra.size() % 4 == 0);
    const std::size_t inTetraCount = listTetra.size() / 4;
    if (inTetraCount == 0) return std::size_t(-1);

    const std::size_t outVertexCount = inTetraCount * 12;

    const tre::s_modelDataLayout::s_vertexData &inPositions = mesh.layout().m_positions;

    const std::size_t partOut = mesh.createRawPart(outVertexCount);

    const std::size_t meshOutIndiceStart = mesh.layout().m_index[mesh.partInfo(partOut).m_offset];
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = mesh.layout().m_positions.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> norIt = mesh.layout().m_normals.begin<glm::vec3>(meshOutIndiceStart);
    tre::s_modelDataLayout::s_vertexData::iterator<glm::vec4> colIt = mesh.layout().m_colors.begin<glm::vec4>(meshOutIndiceStart);

    const float fReduct = 0.10f;
    const float fReductComp = 1.f - fReduct;

    for (std::size_t iT = 0; iT < inTetraCount; ++iT)
    {
      const glm::vec3 ptA = inPositions.get<glm::vec3>(listTetra[iT * 4 + 0]);
      const glm::vec3 ptB = inPositions.get<glm::vec3>(listTetra[iT * 4 + 1]);
      const glm::vec3 ptC = inPositions.get<glm::vec3>(listTetra[iT * 4 + 2]);
      const glm::vec3 ptD = inPositions.get<glm::vec3>(listTetra[iT * 4 + 3]);
      const glm::vec3 ptCenter = 0.25f * (ptA + ptB + ptC + ptD); // not barycentric - but dont care.
      const glm::vec3 ptA_in = fReductComp * ptA + fReduct * ptCenter;
      const glm::vec3 ptB_in = fReductComp * ptB + fReduct * ptCenter;
      const glm::vec3 ptC_in = fReductComp * ptC + fReduct * ptCenter;
      const glm::vec3 ptD_in = fReductComp * ptD + fReduct * ptCenter;
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

  void _computeVisuData()
  {
    // TODO
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

  char meshFile[256] = TESTIMPORTPATH "resources/objects.obj";
  //char meshFile[256] = TESTIMPORTPATH "resources/bumbedGrid.obj";

  if (argc >= 2)
  {
    strncpy(static_cast<char*>(meshFile), argv[1], 256);
  }

  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;
  tre::windowContext::s_timer myTimings;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Mesh 3D", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - Upload mesh

  tre::modelSemiDynamic3D meshes(0, tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_COLOR);

  {
#define TEST_ID 0
#if TEST_ID == 0
    bool meshesLoadStatus = true;
    const int meshStrLen = strnlen(meshFile, 256);
    const bool isOBJ = meshStrLen > 4 && meshFile[meshStrLen - 4] == '.' && meshFile[meshStrLen - 3] == 'o' && meshFile[meshStrLen - 2] == 'b' && meshFile[meshStrLen - 1] == 'j';
    const bool isGLB = meshStrLen > 4 && meshFile[meshStrLen - 4] == '.' && meshFile[meshStrLen - 3] == 'g' && meshFile[meshStrLen - 2] == 'l' && meshFile[meshStrLen - 1] == 'b';
    tre::modelImporter::s_modelHierarchy mh;
    if (isOBJ)      meshesLoadStatus = tre::modelImporter::addFromWavefront(meshes, meshFile);
    else if (isGLB) meshesLoadStatus = tre::modelImporter::addFromGLTF(meshes, mh, meshFile, true);
    else            meshesLoadStatus = false;
    if (!meshesLoadStatus)
    {
      TRE_LOG("Fail to load " << meshFile << ". Falls back to simple shapes.");
      meshes.createPartFromPrimitive_box(glm::mat4(1.f), 2.f);
      meshes.createPartFromPrimitive_cone(glm::mat4(1.f), 1.f, 1.f, 14);
      meshes.createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), 1.f, 10, 7);
    }
#elif TEST_ID == 1
    meshes.createPartFromPrimitive_box(glm::mat4(1.f), 2.f);
    meshes.createPartFromPrimitive_cone(glm::mat4(1.f), 1.f, 1.f, 14);
    meshes.createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), 1.f, 10, 7);
#elif TEST_ID == 2 // distorted prisme
    const float     cosA = cosf(2.6f), sinA = sinf(2.6f);
    const GLuint    indices[8 * 3] = { 0,1,2,  3,4,5,  0,2,5,5,3,0, 2,1,4,4,5,2, 1,4,3,3,0,1 };
    const GLfloat   vertices[6 * 3] = { -0.5f,0.f,-1.f,  0.5f,0.f,-1.f,  0.f,0.7f,-1.f,
                                        -0.5f*cosA,-0.5f*sinA,1.f,  0.5f*cosA,0.5f*sinA,1.f,  -0.7f*sinA,0.7f*cosA,1.f };
    meshes.createPartFromIndexes(&indices[0], 24, &vertices[0]);
    meshes.layout().m_normals.get<glm::vec3>(0) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(0));
    meshes.layout().m_normals.get<glm::vec3>(1) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(1));
    meshes.layout().m_normals.get<glm::vec3>(2) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(2));
    meshes.layout().m_normals.get<glm::vec3>(3) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(3));
    meshes.layout().m_normals.get<glm::vec3>(4) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(4));
    meshes.layout().m_normals.get<glm::vec3>(5) = -glm::normalize(meshes.layout().m_positions.get<glm::vec3>(5));
#endif
  }

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

  // - Randomize vertex color
  {
    const tre::s_modelDataLayout &layout = meshes.layout();
    auto colIt = layout.m_colors.begin<glm::vec4>();
    for (uint iv = 0; iv < layout.m_vertexCount; ++iv)
      *colIt++ = glm::vec4(float(rand() & 0xFF)/float(0xFF), float(rand() & 0xFF)/float(0xFF), float(rand() & 0xFF)/float(0xFF), 1.f);
  }

  meshes.loadIntoGPU();

  // - Create thread context for mesh processing

  const std::size_t meshPartCount = meshes.partCount();
  std::size_t       meshPartSelected = 0;

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
  }

  // - Load shaders

  tre::shader shaderFlat;
  shaderFlat.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_UNICOLOR);

  tre::shader shaderMainMaterial;
  shaderMainMaterial.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_UNICOLOR | tre::shader::PRGM_LIGHT_SUN);

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
    shaderWireframe.loadCustomShaderGF(shLayout, srcGeom_Wireframe_line, srcFrag_Wireframe, "wirefrime_line");
    shaderWireframePlain.loadCustomShaderGF(shLayout, srcGeom_Wireframe_plain, srcFrag_Wireframe, "wirefrime_plain");
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

  // - load UI

  int shaderMode = 0;
  const int NshaderMode = 4;
  tre::shader* listShader[NshaderMode] = { &shaderMainMaterial, &shaderWireframe, &shaderWireframePlain, &shaderDataVisu};

  int visuMode = 0;
  static const int NvisuMode = 5;

  int showMesh = 0;
  static const int NshowMesh = 3;

  bool  showContour = true;

  tre::font font;
  font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  tre::baseUI2D bUI_main;
  bUI_main.set_defaultFont(&font);
  tre::ui::window &wUI_main = *bUI_main.create_window();
  wUI_main.set_layoutGrid(5, 2);
  wUI_main.set_fontSize(tre::ui::s_size(20, tre::ui::SIZE_PIXEL));
  wUI_main.set_color(glm::vec4(0.f, 0.f, 0.f, 0.5f));
  wUI_main.set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));

  wUI_main.create_widgetText(0, 0)->set_text("model (left/right):");
  wUI_main.create_widgetText(0, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    static_cast<tre::ui::widgetText*>(self)->set_text(meshContextSelected.m_mesh->partInfo(meshContextSelected.m_part).m_name);
  };
  wUI_main.create_widgetText(1, 0)->set_text("shader (F2/F3):");
  wUI_main.create_widgetText(1, 1)->wcb_animate = [&shaderMode, &listShader](tre::ui::widget *self, float)
  {
    static_cast<tre::ui::widgetText*>(self)->set_text(listShader[shaderMode]->getName());
  };
  wUI_main.create_widgetText(2, 0)->set_text("visu (F6/F7):");
  wUI_main.create_widgetText(2, 1)->wcb_animate = [&shaderMode, &visuMode](tre::ui::widget *self, float)
  {
    static const char* listDataVisu[NvisuMode] = { "area", "quality", "curvature", "distance", "radom" };
    if (shaderMode == 3)
      static_cast<tre::ui::widgetText*>(self)->set_text(listDataVisu[visuMode]);
    else
      static_cast<tre::ui::widgetText*>(self)->set_text("");
  };
  wUI_main.create_widgetText(3, 0)->set_text("show mesh (F9):");
  wUI_main.create_widgetText(3, 1)->wcb_animate = [&showMesh](tre::ui::widget *self, float)
  {
    static const char* listShowMesh[NshowMesh] = { "decimate curv.", "decimate voxel", "tetra" };
    static_cast<tre::ui::widgetText*>(self)->set_text(listShowMesh[showMesh]);
  };
  wUI_main.create_widgetText(4, 0)->set_text("show contour (F10):");
  wUI_main.create_widgetText(4, 1)->wcb_animate = [&showContour](tre::ui::widget *self, float)
  {
    static_cast<tre::ui::widgetText*>(self)->set_text(showContour ? "ON" : "OFF");
  };

  tre::ui::window &wUI_result = *bUI_main.create_window();
  wUI_result.set_layoutGrid(8, 3);
  wUI_result.set_fontSize(tre::ui::s_size(20, tre::ui::SIZE_PIXEL));
  wUI_result.set_color(glm::vec4(0.f, 0.f, 0.f, 0.5f));
  wUI_result.set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));
  wUI_result.set_colAlignment(1, tre::ui::ALIGN_MASK_HORIZONTAL_RIGHT);
  wUI_result.set_colAlignment(2, tre::ui::ALIGN_MASK_HORIZONTAL_RIGHT);
  wUI_result.set_colWidth(1, tre::ui::s_size(100, tre::ui::SIZE_PIXEL));
  wUI_result.set_colWidth(2, tre::ui::s_size(100, tre::ui::SIZE_PIXEL));
  wUI_result.create_widgetText(0, 1)->set_text("size");
  wUI_result.create_widgetText(0, 2)->set_text("time");


  uint irow = 0;

  wUI_result.create_widgetText(++irow, 0)->set_text("model original");
  wUI_result.create_widgetText(irow, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    char txt[64];
    std::snprintf(txt, 63, "%zd tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_part).m_size / 3);
    static_cast<tre::ui::widgetText*>(self)->set_text(txt);
  };

  wUI_result.create_widgetText(++irow, 0)->set_text("model decimated Curv");
  wUI_result.create_widgetText(irow, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      if (meshContextSelected.m_partDecimateCurv != std::size_t(-1))
      {
        char txt[64];
        std::snprintf(txt, 63, "%zd tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_partDecimateCurv).m_size / 3);
        static_cast<tre::ui::widgetText*>(self)->set_text(txt);
      }
      else
      {
        static_cast<tre::ui::widgetText*>(self)->set_text("err.");
      }
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };
  wUI_result.create_widgetText(irow, 2)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d ms", int(meshContextSelected.m_timeElapsedDecimateCurv * 1.e3));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };

  wUI_result.create_widgetText(++irow, 0)->set_text("model decimated Voxel");
  wUI_result.create_widgetText(irow, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      if (meshContextSelected.m_partDecimateVoxel != std::size_t(-1))
      {
        char txt[64];
        std::snprintf(txt, 63, "%zd tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_partDecimateVoxel).m_size / 3);
        static_cast<tre::ui::widgetText*>(self)->set_text(txt);
      }
      else
      {
        static_cast<tre::ui::widgetText*>(self)->set_text("err.");
      }
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };
  wUI_result.create_widgetText(irow, 2)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d ms", int(meshContextSelected.m_timeElapsedDecimateVoxel * 1.e3));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };

  wUI_result.create_widgetText(++irow, 0)->set_text("model envelop");
  wUI_result.create_widgetText(irow, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d pts", int(meshContextSelected.m_envelop2D.size()));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };
  wUI_result.create_widgetText(irow, 2)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d ms", int(meshContextSelected.m_timeElapsedEnvelop2D * 1.e3));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };

  wUI_result.create_widgetText(++irow, 0)->set_text("model tetra");
  wUI_result.create_widgetText(irow, 1)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      if (!meshContextSelected.m_tetrahedrons.empty())
      {
        char txt[64];
        std::snprintf(txt, 63, "%d tetra", int(meshContextSelected.m_tetrahedrons.size() / 4));
        static_cast<tre::ui::widgetText*>(self)->set_text(txt);
      }
      else
      {
        static_cast<tre::ui::widgetText*>(self)->set_text("err.");
      }
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };
  wUI_result.create_widgetText(irow, 2)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d ms", int(meshContextSelected.m_timeElapsedTetrahedrization * 1.e3));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };

  wUI_result.create_widgetText(++irow, 0)->set_text("model debug");
  wUI_result.create_widgetText(irow, 2)->wcb_animate = [&meshPartContext, &meshPartSelected](tre::ui::widget *self, float)
  {
    const auto &meshContextSelected = meshPartContext[meshPartSelected];
    if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
    {
      char txt[64];
      std::snprintf(txt, 63, "%d ms", int(meshContextSelected.m_timeElapsedDebug * 1.e3));
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    }
    else
    {
      static_cast<tre::ui::widgetText*>(self)->set_text("");
    }
  };

  bUI_main.loadShader();
  bUI_main.loadIntoGPU();

  // - scene and event variables

  SDL_Event event;

  glm::mat4 mView = glm::mat4(1.f);
  mView[0][0] = 2.f; // viewport is on width/2
  mView[3][0] =  0.f;
  mView[3][1] = -2.f;
  mView[3][2] = -5.f;

  glm::mat4 mModelPrev = glm::mat4(1.f);
  glm::mat4 mModel = glm::mat4(1.f);

  float mModelScale = 1.f;

  // - MAIN LOOP ------------

  while(!myWindow.m_quit && !myControls.m_quit)
  {
    // event actions + updates --------

    myWindow.SDLEvent_newFrame();
    myControls.newFrame();
    myTimings.newFrame(0, myControls.m_pause);

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myControls.treatSDLEvent(event);

      if (event.type == SDL_KEYDOWN)
      {
        if      (event.key.keysym.sym == SDLK_F1) { shaderMode = 0; }
        else if (event.key.keysym.sym == SDLK_F2) { shaderMode = shaderMode == 0 ? NshaderMode - 1 : shaderMode - 1; }
        else if (event.key.keysym.sym == SDLK_F3) { shaderMode = shaderMode == NshaderMode - 1 ? 0 : shaderMode + 1; }
        else if (event.key.keysym.sym == SDLK_F4) { shaderMode = NshaderMode - 1; }

        else if (event.key.keysym.sym == SDLK_F6) { visuMode = visuMode == 0 ? NvisuMode - 1 : visuMode - 1; }
        else if (event.key.keysym.sym == SDLK_F7) { visuMode = visuMode == NvisuMode - 1 ? 0 : visuMode + 1; }

        else if (event.key.keysym.sym == SDLK_F9)  { showMesh = showMesh == NshowMesh - 1 ? 0 : showMesh + 1; }
        else if (event.key.keysym.sym == SDLK_F10) { showContour = ! showContour; }

        else if (event.key.keysym.sym == SDLK_o) { myTimings.scenetime = 0.f; }

        else if (event.key.keysym.sym == SDLK_RIGHT)
          meshPartSelected = (meshPartSelected == meshPartCount - 1) ? 0 : meshPartSelected + 1;
        else if (event.key.keysym.sym == SDLK_LEFT)
          meshPartSelected = (meshPartSelected == 0) ? meshPartCount - 1 : meshPartSelected - 1;

        if (event.key.keysym.sym == SDLK_LSHIFT) mModelScale *= 1.2f;
        if (event.key.keysym.sym == SDLK_LCTRL) mModelScale /= 1.2f;
      }
      else if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT)
      {
        mModelPrev = mModel;
        myControls.m_pause = true;
      }
    }

    if (myControls.m_home) mModelScale = 1.f;
    if (myControls.m_mouse.z < 0.f) mModelScale *= 1.2f;
    if (myControls.m_mouse.z > 0.f) mModelScale /= 1.2f;
    if (!myControls.m_pause) myControls.m_mouseLEFT = 0; // hack, cancel any mouse action

    if ((myControls.m_mouseLEFT & tre::windowContext::s_controls::MASK_BUTTON_PRESSED) != 0)
    {
      const glm::vec2 diff = glm::vec2(myControls.m_mouse - myControls.m_mousePrev) * myWindow.m_resolutioncurrentInv;
      mModel = glm::rotate(glm::rotate(glm::mat4(1.f), diff.x, glm::vec3(0.f,1.f,0.f)), diff.y, glm::vec3(1.f,0.f,0.f)) * mModelPrev;
    }

    if (!myControls.m_pause)
      mModel = glm::rotate(mModel, myTimings.frametime * 6.28f * 0.2f, glm::vec3(0.8f,0.6f,0.f));

    glm::mat4 curModel = mModel;
    curModel[0] *= mModelScale;
    curModel[1] *= mModelScale;
    curModel[2] *= mModelScale;
    curModel[3] = glm::vec4(0.f,2.0f,0.f,1.0f);

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
    if (meshUpdateNeeded) meshes.updateIntoGPU();

    // main render pass -------------

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_CULL_FACE);

    const glm::mat4 mPV(myWindow.m_matProjection3D * mView);
    const glm::vec4 ucolorMain(0.7f,0.7f,0.7f,1.f);
    const glm::vec4 uChoiceVisu(visuMode == 0 ? 1.f : 0.f,
                                visuMode == 1 ? 1.f : 0.f,
                                visuMode == 2 ? 1.f : 0.f,
                                visuMode == 3 ? 1.f : 0.f);

    // -- render mesh origin (left)
    {
      glViewport(0, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);

      int shaderModeOrigin = shaderMode;
      if ((!meshContextSelected.m_completed || meshContextSelected.m_ongoing) && shaderModeOrigin == 3)
        shaderModeOrigin = 0;
      tre::shader & curShader = * listShader[shaderModeOrigin];

      glUseProgram(curShader.m_drawProgram);
      if (shaderModeOrigin == 3)
        glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uChoiceVisu));
      else
        glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMain));

      curShader.setUniformMatrix(mPV * curModel, curModel);

      glEnable(GL_DEPTH_TEST);
      meshes.drawcall(meshContextSelected.m_part, 1, true);

      if (meshContextSelected.m_partBBox != std::size_t(-1))
      {
        glUseProgram(shaderFlat.m_drawProgram);
        const glm::vec4 uContour = glm::vec4(0.f, 1.f, 0.f, 1.f);
        glUniform4fv(shaderFlat.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uContour));
        shaderFlat.setUniformMatrix(mPV * curModel, curModel);
        glDisable(GL_DEPTH_TEST);
        meshes.drawcall(meshContextSelected.m_partBBox, 1, false, GL_LINES);
      }
    }

    glDisable(GL_CULL_FACE);

    // - render mesh transform (right)
    {
      glViewport(myWindow.m_resolutioncurrent.x / 2, 0, myWindow.m_resolutioncurrent.x / 2, myWindow.m_resolutioncurrent.y);

      tre::shader & curShader = * listShader[shaderMode];

      glUseProgram(curShader.m_drawProgram);
      if (shaderMode == 3)
        glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uChoiceVisu));
      else
        glUniform4fv(curShader.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorMain));

      curShader.setUniformMatrix(mPV * curModel, curModel);

      glEnable(GL_DEPTH_TEST);

      if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
      {
        if (showMesh == 0)
        {
          if (meshContextSelected.m_partDecimateCurv != std::size_t(-1)) meshes.drawcall(meshContextSelected.m_partDecimateCurv, 1, false);
        }
        else if (showMesh == 1)
        {
          if (meshContextSelected.m_partDecimateVoxel != std::size_t(-1)) meshes.drawcall(meshContextSelected.m_partDecimateVoxel, 1, false);
        }
        else if (showMesh == 2)
        {
          if (meshContextSelected.m_partTetrahedrons != std::size_t(-1)) meshes.drawcall(meshContextSelected.m_partTetrahedrons, 1, false);
        }
      }

      if (showContour)
      {
        glUseProgram(shaderFlat.m_drawProgram);
        const glm::vec4 uContour = glm::vec4(1.f, 0.f, 0.f, 1.f);
        glUniform4fv(shaderFlat.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uContour));
        shaderFlat.setUniformMatrix(mPV * curModel, curModel);
        glDisable(GL_DEPTH_TEST);
        meshes.drawcall(meshContextSelected.m_partEnvelop2D, 1, false, GL_LINES);
      }
    }

    tre::IsOpenGLok("main render pass - draw meshes");

    // - render UI

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

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

    bUI_main.animate(myTimings.frametime);
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

    myTimings.endFrame_beforeGPUPresent();

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  shaderFlat.clearShader();
  shaderMainMaterial.clearShader();
  shaderWireframe.clearShader();
  shaderWireframePlain.clearShader();
  shaderDataVisu.clearShader();

  tre::shader::clearUBO();

  meshes.clearGPU();

  font.clear();

  bUI_main.clear();
  bUI_main.clearGPU();
  bUI_main.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
