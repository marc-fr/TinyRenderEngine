
#include "tre_shader.h"
#include "tre_model_importer.h"
#include "tre_model_tools.h"
#include "tre_font.h"
#include "tre_ui.h"
#include "tre_windowContext.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <string>
#include <chrono>
#include <thread>

typedef std::chrono::steady_clock systemclock;
typedef systemclock::time_point   systemtick;


// =============================================================================

struct s_partProcessingContext
{
  tre::modelIndexed *m_mesh = nullptr;
  std::size_t        m_partOrigin = -1;
  tre::s_boundbox    m_bbox;

  // Results
  std::vector<glm::vec2> m_envelop2D;
  std::vector<uint>      m_tetrahedrons;
  glm::vec3              m_center;
  float                  m_volume;

  std::size_t        m_partDecimateCurv = -1; // note: the "color" contains visualization-data
  std::size_t        m_partDecimateVoxel = -1; // note: the "color" contains visualization-data
  std::size_t        m_partEnvelop2D = -1; // rendered with LINES
  std::size_t        m_partTetrahedrons = -1; // note: the "color" contains visualization-data
  std::size_t        m_partBBox = -1; // rendered with LINES

  // Status
  bool   m_completed = false;
  double m_timeElapsedDecimateCurv = 0.;
  double m_timeElapsedDecimateVoxel = 0.;
  double m_timeElapsedEnvelop2D = 0.;
  double m_timeElapsedTetrahedrization = 0.;
  bool   m_ongoing = false; // warning: not used in the task, only on the main-thread.
  float  m_progressTetrahedrization = 0.f;
  std::thread m_thread;

  void run() // can run asynchroneously. Can in "m_mesh", but only 1 thread.
  {
    const systemtick tick0 = systemclock::now();

    //m_partDecimateCurv = tre::modelTools::decimateCurvature(*m_mesh, m_part, 0.2f);

    const systemtick tick1 = systemclock::now();

    //m_partDecimateVoxel = tre::modelTools::decimateVoxel(*m_mesh, m_part, 0.2f, true);

     const systemtick tick2 = systemclock::now();

    tre::modelTools::computeConvexeEnvelop2D_XY(m_mesh->layout(), m_mesh->partInfo(m_partOrigin), glm::mat4(1.f), 1.e-2f, m_envelop2D);

    const systemtick tick3 = systemclock::now();

    tre::modelTools::tetrahedralize(*m_mesh, m_partOrigin, std::numeric_limits<std::size_t>::max(), true, m_tetrahedrons);

    const systemtick tick4 = systemclock::now();

    {
      glm::vec4 cv = tre::modelTools::computeBarycenter3D(m_mesh->layout(), m_mesh->partInfo((m_partOrigin)));
      m_center = cv;
      m_volume = cv.w;
    }

    // end
    m_timeElapsedDecimateCurv     = std::chrono::duration<double>(tick1 - tick0).count();
    m_timeElapsedDecimateVoxel    = std::chrono::duration<double>(tick2 - tick1).count();
    m_timeElapsedEnvelop2D        = std::chrono::duration<double>(tick3 - tick2).count();
    m_timeElapsedTetrahedrization = std::chrono::duration<double>(tick4 - tick3).count();

    m_completed = true;
  }

  void finalize()
  {
    // upload the envelop
    if (!m_envelop2D.empty())
    {
      std::size_t vOffset = 0;
      m_partEnvelop2D = m_mesh->createPart(m_envelop2D.size() * 2, m_envelop2D.size(), vOffset);
      GLuint  * __restrict dataI = m_mesh->layout().m_index.m_data + m_mesh->partInfo(m_partEnvelop2D).m_offset;
      tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = m_mesh->layout().m_positions.begin<glm::vec3>(vOffset);
      for (std::size_t ip = 0; ip < m_envelop2D.size(); ++ip)
      {
        *posIt++ = glm::vec3(m_envelop2D[ip], 0.f); // pos
        *dataI++ = uint(vOffset + ip);
        *dataI++ = uint(vOffset + (ip == m_envelop2D.size() - 1 ? 0 : ip + 1));
      }
    }

    // create center-cross + the bbox mesh
    {
      const std::array<uint, 6> indices = { 0, 1, 2, 3, 4, 5};
      const float vertices[] = { m_center.x - 0.1f, m_center.y, m_center.z,
                                 m_center.x + 0.1f, m_center.y, m_center.z,
                                 m_center.x, m_center.y - 0.1f, m_center.z,
                                 m_center.x, m_center.y + 0.1f, m_center.z,
                                 m_center.x, m_center.y, m_center.z - 0.1f,
                                 m_center.x, m_center.y, m_center.z + 0.1f };
      m_partBBox = m_mesh->createPartFromIndexes(indices, vertices);
    }

    // create tetra mesh
    if (m_tetrahedrons.size() >= 4)
    {
      TRE_ASSERT(listTetra.size() % 4 == 0);
      const std::size_t inTetraCount = m_tetrahedrons.size() / 4;
      const std::size_t outVertexCount = inTetraCount * 12;

      const tre::s_modelDataLayout::s_vertexData &inPositions = m_mesh->layout().m_positions;

      const std::size_t partOut = m_mesh->createRawPart(outVertexCount);

      const std::size_t meshOutIndiceStart = m_mesh->layout().m_index[m_mesh->partInfo(partOut).m_offset];
      tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> posIt = m_mesh->layout().m_positions.begin<glm::vec3>(meshOutIndiceStart);
      tre::s_modelDataLayout::s_vertexData::iterator<glm::vec3> norIt = m_mesh->layout().m_normals.begin<glm::vec3>(meshOutIndiceStart);
      tre::s_modelDataLayout::s_vertexData::iterator<glm::vec4> colIt = m_mesh->layout().m_colors.begin<glm::vec4>(meshOutIndiceStart);

      const float fReduct = 0.10f;
      const float fReductComp = 1.f - fReduct;

      for (std::size_t iT = 0; iT < inTetraCount; ++iT)
      {
        const glm::vec3 ptA = inPositions.get<glm::vec3>(m_tetrahedrons[iT * 4 + 0]);
        const glm::vec3 ptB = inPositions.get<glm::vec3>(m_tetrahedrons[iT * 4 + 1]);
        const glm::vec3 ptC = inPositions.get<glm::vec3>(m_tetrahedrons[iT * 4 + 2]);
        const glm::vec3 ptD = inPositions.get<glm::vec3>(m_tetrahedrons[iT * 4 + 3]);
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

      m_partTetrahedrons = partOut;
    }
  }
};

std::vector<s_partProcessingContext> meshPartContext;

// =============================================================================

tre::windowContext             myWindow;
tre::windowContext::s_timer    myTimings;
tre::windowContext::s_controls myControls;
tre::windowContext::s_view3D   myView3D(&myWindow);

tre::shader shader3D;
tre::shader shaderWireframe;

tre::modelSemiDynamic3D   mesh;

float  mModelScale = 1.f;

int   showPartId = 0;
bool  showContour = true;
bool  showBox = true;
bool  showWireframe = false;

enum e_renderMode
{
  RENDER_SHADED,
  RENDER_NORMAL,
  RENDER_TRIANGLE_ID,
  RENDER_QUALITY,
};
e_renderMode renderMode = RENDER_SHADED;
static constexpr std::array<const char*, 4> kRenderModeNames = { "Shaded", "Normal", "Tri-ID", "Quality" };

enum e_showProcess
{
  SHOW_ORIGIN,
  SHOW_DECIMATE_1,
  SHOW_DECIMATE_2,
  SHOW_TETRA,
};
e_showProcess process = SHOW_ORIGIN;
static constexpr std::array<const char*, 4> kProcessNames = { "Origin", "Decimate 1", "Decimate 2", "Tetra" };

tre::font        font;
tre::baseUI2D    bUI_main;
tre::ui::window *wUI_rendering = nullptr;
tre::ui::window *wUI_property = nullptr;
tre::ui::window *wUI_layout = nullptr;
tre::ui::window *wUI_result = nullptr;

int uiPartIdHovered = -1;
int uiPartIdSelected = -1;

// ---------------------------------------------------------

static int app_init(int argc, char **argv)
{
  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "Mesh Viewer", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - Arguments

  char meshFile[256] = TESTIMPORTPATH "resources/objects.obj";

  if (argc >= 2)
  {
    strncpy(static_cast<char*>(meshFile), argv[1], 256);
  }

  if (argc >= 3)
  {
    showPartId = atoi(argv[2]);
  }

  // - Upload mesh

  mesh.setFlags(0, tre::modelStaticIndexed3D::VB_POSITION | tre::modelStaticIndexed3D::VB_NORMAL | tre::modelStaticIndexed3D::VB_COLOR | tre::modelStaticIndexed3D::VB_UV | tre::modelStaticIndexed3D::VB_SKIN);

  {
#define TEST_ID 0
#if TEST_ID == 0
    bool meshesLoadStatus = true;
    const std::size_t meshStrLen = strnlen(meshFile, 256);
    const bool isOBJ = meshStrLen > 4 && meshFile[meshStrLen - 4] == '.' && meshFile[meshStrLen - 3] == 'o' && meshFile[meshStrLen - 2] == 'b' && meshFile[meshStrLen - 1] == 'j';
    const bool isGLB = meshStrLen > 4 && meshFile[meshStrLen - 4] == '.' && meshFile[meshStrLen - 3] == 'g' && meshFile[meshStrLen - 2] == 'l' && meshFile[meshStrLen - 1] == 'b';
    tre::modelImporter::s_modelHierarchy mh;
    if (isOBJ)      meshesLoadStatus = tre::modelImporter::addFromWavefront(mesh, meshFile);
    else if (isGLB) meshesLoadStatus = tre::modelImporter::addFromGLTF(mesh, mh, meshFile, true);
    else            meshesLoadStatus = false;
    if (!meshesLoadStatus) return -10;
#elif TEST_ID == 1
    mesh.createPartFromPrimitive_box(glm::mat4(1.f), 0.5f);
    //mesh.createPartFromPrimitive_cone(glm::mat4(1.f), 1.f, 1.f, 14);
    mesh.createPartFromPrimitive_uvtrisphere(glm::mat4(1.f), 0.1f, 10, 7);
#elif TEST_ID == 2 // distorted prisme
    const float     cosA = std::cos(0.7f), sinA = std::sin(0.7f);
    const std::array<GLuint, 8 * 3>  indices  = { 0,2,1,  3,4,5,  2,0,3,3,5,2, 1,2,5,5,4,1, 0,1,4,4,3,0 };
    const std::array<GLfloat, 6 * 3> vertices = { -0.5f,0.f            ,-1.f,  0.5f,0.f           ,-1.f,   0.f,0.7f           ,-1.f,
                                                  -0.5f*cosA,-0.5f*sinA, 1.f,  0.5f*cosA,0.5f*sinA, 1.f,  -0.7f*sinA,0.7f*cosA, 1.f };
    mesh.createPartFromIndexes(indices, &vertices[0]);
    mesh.layout().m_normals.get<glm::vec3>(0) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(0));
    mesh.layout().m_normals.get<glm::vec3>(1) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(1));
    mesh.layout().m_normals.get<glm::vec3>(2) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(2));
    mesh.layout().m_normals.get<glm::vec3>(3) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(3));
    mesh.layout().m_normals.get<glm::vec3>(4) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(4));
    mesh.layout().m_normals.get<glm::vec3>(5) = glm::normalize(mesh.layout().m_positions.get<glm::vec3>(5));
#elif TEST_ID == 3 // open baked mesh
  // TODO ...
#endif
  }

  // - Create thread context for mesh processing

  meshPartContext.resize(mesh.partCount());
  glm::vec3 bext = glm::vec3(0.f);

  for (std::size_t iPart = 0; iPart < mesh.partCount(); ++iPart)
  {
    meshPartContext[iPart].m_mesh = &mesh;
    meshPartContext[iPart].m_partOrigin = iPart;
    meshPartContext[iPart].m_bbox = mesh.partInfo(iPart).m_bbox;
    bext = glm::max(bext, mesh.partInfo(iPart).m_bbox.extend());
  }

  mModelScale = 10.f / (1.e-6f + bext.x + bext.y + bext.z);

  myView3D.m_matView[3].z = -15.f;
  myView3D.m_keySensitivity = glm::vec3(0.2f);
  myView3D.m_mouseSensitivity = glm::vec4(0.2f, 0.2f, 0.2f, 3.f);

  mesh.loadIntoGPU();

  // - Load shaders

  {
    const char *srcFrag_Color =
      "vec3 kColors[8] = vec3[]( vec3(0.,0.,1.), vec3(0.,1.,0.), vec3(1.,0.,0.), vec3(0.,0.5,0.5), vec3(0.5,0.,0.5), vec3(0.5,0.5,0.), vec3(0.5,0.5,0.5), vec3(0.5,0.5,1.) );\n"
      "void main(){\n"
      "  vec3 Nworld = normalize(pixelNormal);\n"
      "  vec3 Nview  = normalize((MView * vec4(pixelNormal, 0.f)).xyz);\n"
      "  if      (uniColor.x == 0.f) color.xyz = vec3(0.5f + 0.5f * Nview.z);\n"
      "  else if (uniColor.x == 1.f) color.xyz = vec3(0.5f + 0.5f * Nworld.z);\n"
      "  else if (uniColor.x == 2.f) color.xyz = 0.5f + 0.5f * Nworld;\n"
      "  else if (uniColor.x == 3.f) color.xyz = vec3(pixelColor.x, 0.f, -1.f - pixelColor.x);\n"
      "  else if (uniColor.x == 4.f) color.xyz = kColors[int(pixelColor.y) & 0x7];\n"
      "  else if (uniColor.x == 8.f) color.xyz = vec3(0.f, 1.f, 0.f);\n"
      "  else                        color.xyz = vec3(1.f, 0.f, 1.f);\n"
      "  color.w = 1.f;\n"
      "}\n";

    tre::shader::s_layout shLayout(tre::shader::PRGM_3D, tre::shader::PRGM_COLOR | tre::shader::PRGM_UNICOLOR);
    shLayout.hasBUF_Normal = true;
    shLayout.hasPIX_Normal = true;
    shLayout.hasUNI_MPVM = true;
    shLayout.hasUNI_MView = true;
    shader3D.loadCustomShader(shLayout, srcFrag_Color, "data3D");
  }

#ifndef TRE_OPENGL_ES
  {
    const char *srcGeom_Wireframe_line =
    "#version 330 core\n"
    "layout(triangles) in;\n"
    "layout(line_strip, max_vertices = 4) out;\n"
    "void main(){\n"
    "  \n"
    "  gl_Position = gl_in[0].gl_Position;\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[1].gl_Position;\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[2].gl_Position;\n"
    "  EmitVertex();\n"
    "  \n"
    "  gl_Position = gl_in[0].gl_Position;\n"
    "  EmitVertex();\n"
    "  \n"
    "  EndPrimitive();\n"
    "}\n";

    const char *srcFrag_Wireframe =
    "void main(){\n"
    "  color = uniColor;\n"
    "}\n";

    tre::shader::s_layout shLayout(tre::shader::PRGM_3D, tre::shader::PRGM_UNICOLOR);
    shaderWireframe.loadCustomShaderGF(shLayout, srcGeom_Wireframe_line, srcFrag_Wireframe, "wirefrime_line");
  }
#endif // !TRE_OPENGL_ES

  // - load UI

  font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  bUI_main.set_defaultFont(&font);

  tre::ui::s_colorTheme theme;
  theme.m_colorBackground = glm::vec4(0.3f, 0.3f, 0.3f, 0.5f);

  {
    wUI_property = bUI_main.create_window();
    wUI_property->set_layoutGrid(2 + unsigned(mesh.partCount()), 1);
    wUI_property->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    wUI_property->set_cellMargin(tre::ui::s_size(2, tre::ui::SIZE_PIXEL));
    wUI_property->set_colortheme(theme);

    char txt[512];

    std::snprintf(txt, 512, "Model:\nIndex: %zd\nVertex: %zd", mesh.layout().m_indexCount, mesh.layout().m_vertexCount);
    wUI_property->create_widgetText(0, 0)->set_text(txt);

    std::snprintf(txt, 512, "Parts (%zd)", mesh.partCount());
    wUI_property->create_widgetText(1, 0)->set_text(txt);

    for (std::size_t ip = 0; ip < mesh.partCount(); ++ip)
    {
      const auto &pInfo = mesh.partInfo(ip);
      std::snprintf(txt, 512, "- part %zd:\nName: %s\nIndex range: [%zd - %zd]", ip, pInfo.m_name.c_str(), pInfo.m_offset, pInfo.m_offset + pInfo.m_size);
      tre::ui::widgetText *wt = wUI_property->create_widgetText(2 + unsigned(ip), 0)->set_text(txt);
      wt->set_isactive(true);
      wt->wcb_gain_focus = [ip](tre::ui::widget *) { uiPartIdHovered = int(ip); };
      wt->wcb_loss_focus = [ip](tre::ui::widget *) { if (uiPartIdHovered == ip) uiPartIdHovered = -1; };
      wt->wcb_clicked_left = [ip](tre::ui::widget *) { uiPartIdSelected = (uiPartIdSelected != ip) ? int(ip) : -1; };

      wt->wcb_animate = [ip](tre::ui::widget *self, float)
      {
        if (uiPartIdSelected == ip) self->set_color(glm::vec4(0.7f, 1.f, 0.7f, 1.f));
        else                        self->set_color(glm::vec4(-1.f));
      };
    }
  }

  {
    wUI_rendering = bUI_main.create_window();
    wUI_rendering->set_layoutGrid(8, 2);
    wUI_rendering->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    wUI_rendering->set_cellMargin(tre::ui::s_size(2, tre::ui::SIZE_PIXEL));
    wUI_rendering->set_colortheme(theme);
    wUI_rendering->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    unsigned irow = -1;

    wUI_rendering->create_widgetText(++irow, 0)->set_text("Process");
    wUI_rendering->create_widgetLineChoice(irow, 1)->set_values(kProcessNames)->set_selectedIndex(process)->set_cyclic(true)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self) { process = static_cast<e_showProcess>(static_cast<tre::ui::widgetLineChoice*>(self)->get_selectedIndex()); };

    wUI_rendering->create_widgetText(++irow, 0)->set_text("Render");
    wUI_rendering->create_widgetLineChoice(irow, 1)->set_values(kRenderModeNames)->set_selectedIndex(renderMode)->set_cyclic(true)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self) { renderMode = static_cast<e_renderMode>(static_cast<tre::ui::widgetLineChoice*>(self)->get_selectedIndex()); };

    wUI_rendering->create_widgetText(++irow, 0)->set_text("Show Box");
    wUI_rendering->create_widgetBoxCheck(irow, 1)->set_value(showBox)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self) { showBox = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value(); };

    wUI_rendering->create_widgetText(++irow, 0)->set_text("Show Contour");
    wUI_rendering->create_widgetBoxCheck(irow, 1)->set_value(showContour)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self) { showContour = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value(); };

#ifndef TRE_OPENGL_ES
    wUI_rendering->create_widgetText(++irow, 0)->set_text("Show Wireframe");
    wUI_rendering->create_widgetBoxCheck(irow, 1)->set_value(showWireframe)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self) { showWireframe = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value(); };
#endif
  }

  {
    wUI_result = bUI_main.create_window();
    wUI_result->set_layoutGrid(8, 3);
    wUI_result->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    wUI_result->set_cellMargin(tre::ui::s_size(2, tre::ui::SIZE_PIXEL));
    wUI_result->set_colAlignment(1, tre::ui::ALIGN_MASK_HORIZONTAL_RIGHT);
    wUI_result->set_colAlignment(2, tre::ui::ALIGN_MASK_HORIZONTAL_RIGHT);
    wUI_result->create_widgetText(0, 1)->set_text("size");
    wUI_result->create_widgetText(0, 2)->set_text("time");
    wUI_result->set_colortheme(theme);

    unsigned irow = -1;

    wUI_result->create_widgetText(++irow, 0)->set_text("model original");
    wUI_result->create_widgetText(irow, 1)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
      char txt[64];
      std::snprintf(txt, 63, "%zd tri", meshContextSelected.m_mesh->partInfo(meshContextSelected.m_partOrigin).m_size / 3);
      static_cast<tre::ui::widgetText*>(self)->set_text(txt);
    };

    wUI_result->create_widgetText(++irow, 0)->set_text("model decimated Curv");
    wUI_result->create_widgetText(irow, 1)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
      if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
      {
        if (meshContextSelected.m_partDecimateCurv != -1)
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
    wUI_result->create_widgetText(irow, 2)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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

    wUI_result->create_widgetText(++irow, 0)->set_text("model decimated Voxel");
    wUI_result->create_widgetText(irow, 1)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
      if (meshContextSelected.m_completed && !meshContextSelected.m_ongoing)
      {
        if (meshContextSelected.m_partDecimateVoxel != -1)
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
    wUI_result->create_widgetText(irow, 2)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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

    wUI_result->create_widgetText(++irow, 0)->set_text("model envelop");
    wUI_result->create_widgetText(irow, 1)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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
    wUI_result->create_widgetText(irow, 2)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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

    wUI_result->create_widgetText(++irow, 0)->set_text("model tetra");
    wUI_result->create_widgetText(irow, 1)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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
    wUI_result->create_widgetText(irow, 2)->wcb_animate = [](tre::ui::widget *self, float)
    {
      const auto &meshContextSelected = meshPartContext[showPartId];
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
  }

  {
    wUI_layout = bUI_main.create_window();
    wUI_layout->set_layoutGrid(1, 1);
    wUI_layout->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    wUI_layout->set_colortheme(theme);

    wUI_layout->create_widgetText(0, 0)->set_text("Layout Widget HERE"); // TODO
  }

  bUI_main.loadShader();
  bUI_main.loadIntoGPU();

  return 0;
}

// ---------------------------------------------------------

static void app_resized()
{
  bUI_main.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

  const float screenRatio = float(myWindow.m_resolutioncurrent.x) / float(myWindow.m_resolutioncurrent.y);

  glm::mat3 mat(1.f);

  wUI_rendering->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
  mat[2][0] = - screenRatio;
  mat[2][1] =   1.f;
  wUI_rendering->set_mat3(mat);

  wUI_property->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
  mat[2][0] = - screenRatio;
  mat[2][1] = 1.f + wUI_rendering->get_zone().y - 0.05f;
  wUI_property->set_mat3(mat);

  wUI_layout->set_alignMask(tre::ui::ALIGN_MASK_HORIZONTAL_LEFT | tre::ui::ALIGN_MASK_VERTICAL_BOTTOM);
  mat[2][0] = - screenRatio;
  mat[2][1] = - 1.f;
  wUI_layout->set_mat3(mat);
  wUI_layout->set_colWidth(0, 2.f * screenRatio);

  wUI_result->set_alignMask(tre::ui::ALIGN_MASK_HORIZONTAL_RIGHT | tre::ui::ALIGN_MASK_VERTICAL_TOP);
  mat[2][0] =   screenRatio;
  mat[2][1] =   1.f;
  wUI_result->set_mat3(mat);
}

// ---------------------------------------------------------

static void app_update()
{
  SDL_Event event;

  // event actions + updates --------

  myWindow.SDLEvent_newFrame();
  myControls.newFrame();
  myTimings.newFrame(0, myControls.m_pause);

  //-> SDL events
  while(SDL_PollEvent(&event) == 1)
  {
    myWindow.SDLEvent_onWindow(event);
    myControls.treatSDLEvent(event);
    
    if (!myView3D.m_mouseBound) bUI_main.acceptEvent(event);
  }

  if (myWindow.m_hasFocus)
    myView3D.treatControlEvent(myControls, 0.17f /*about 60 fps*/);

  myView3D.setKeyBinding(true);

  if (myControls.m_mouseRIGHT & myControls.MASK_BUTTON_RELEASED)
    myView3D.setMouseBinding(!myView3D.m_mouseBound);

  if (myWindow.m_viewportResized)
    app_resized();

  //-> Mesh operations ------------

  if      (uiPartIdSelected != -1) showPartId = uiPartIdSelected;
  else if (uiPartIdHovered  != -1) showPartId = uiPartIdHovered;
  else                             showPartId = 0;

  bool hasMeshThreadRunning = false;
  bool meshUpdateNeeded = false;
  for (auto &meshContextCurr : meshPartContext)
  {
    if (meshContextCurr.m_completed)
    {
      if (meshContextCurr.m_ongoing)
      {
        meshUpdateNeeded = true;
        meshContextCurr.finalize();
      }
      meshContextCurr.m_ongoing = false;
    }
    hasMeshThreadRunning |= meshContextCurr.m_ongoing;
  }

  auto &meshContextSelected = meshPartContext[showPartId];
  if (!meshContextSelected.m_completed &&
      !meshContextSelected.m_ongoing &&
      !hasMeshThreadRunning /*multiple mesh op cannot run simultaneously, because they share the same meshes (createPart may relocate data ...)*/)
  {
    // create and launch a task
    meshContextSelected.m_ongoing = true;
    meshContextSelected.m_thread = std::thread([&meshContextSelected]{ meshContextSelected.run(); });
  }

  if (meshUpdateNeeded) mesh.updateIntoGPU();

  // main render pass -------------

  glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glm::mat4 mModel = glm::mat4(1.f);
  mModel[0] *= mModelScale;
  mModel[1] *= mModelScale;
  mModel[2] *= mModelScale;

  const glm::mat4 matPVM(myWindow.m_matProjection3D * myView3D.m_matView * mModel);

  // render main mesh

  std::size_t partToRender = meshContextSelected.m_partOrigin;
  bool        useFaceCulling = true;

  switch (process)
  {
    case SHOW_DECIMATE_1:
    if (meshContextSelected.m_partDecimateCurv != -1) partToRender = meshContextSelected.m_partDecimateCurv;
    break;
    case SHOW_DECIMATE_2:
    if (meshContextSelected.m_partDecimateVoxel != -1) partToRender = meshContextSelected.m_partDecimateVoxel;
    break;
    case SHOW_TETRA:
    if (meshContextSelected.m_partTetrahedrons != -1) { partToRender = meshContextSelected.m_partTetrahedrons; useFaceCulling = false; }
    break;
    default:
    break;
  }

  {
    glUseProgram(shader3D.m_drawProgram);
    shader3D.setUniformMatrix(matPVM, mModel, myView3D.m_matView);

    if (useFaceCulling) glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    mesh.drawcall(0, 0, true); // bind

    switch (renderMode)
    {
      case RENDER_SHADED:
      glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 0.f, 0.f, 0.f, 0.f);
      break;
      case RENDER_NORMAL:
      glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 2.f, 0.f, 0.f, 0.f);
      break;
      case RENDER_TRIANGLE_ID:
      glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 4.f, 0.f, 0.f, 0.f);
      break;
      case RENDER_QUALITY:
      glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 3.f, 0.f, 0.f, 0.f);
      break;
    }
    mesh.drawcall(partToRender, 1, false);
  }

  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);

  if (showWireframe && shaderWireframe.m_drawProgram != 0)
  {
    glUseProgram(shaderWireframe.m_drawProgram);
    glUniform4f(shaderWireframe.getUniformLocation(tre::shader::uniColor), 0.f, 0.5f, 0.f, 0.8f); // true color
    shaderWireframe.setUniformMatrix(matPVM, mModel, myView3D.m_matView);
    mesh.drawcall(partToRender, 1, false);
  }

  if (showBox && meshContextSelected.m_partBBox != -1)
  {
    glUseProgram(shader3D.m_drawProgram);
    glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 8.f, 0.f, 0.f, 0.f);
    mesh.drawcall(meshContextSelected.m_partBBox, 1, false, GL_LINES);
  }

  if (showContour && meshContextSelected.m_partEnvelop2D != -1)
  {
    glUseProgram(shader3D.m_drawProgram);
    glUniform4f(shader3D.getUniformLocation(tre::shader::uniColor), 8.f, 0.f, 0.f, 0.f);
    mesh.drawcall(meshContextSelected.m_partEnvelop2D, 1, false, GL_LINES);
  }

  tre::IsOpenGLok("main render pass");

  // - render UI

  bUI_main.animate(myTimings.frametime);
  bUI_main.updateIntoGPU();
  bUI_main.draw();

  tre::IsOpenGLok("UI render pass");

  // end render pass --------------

  myTimings.endFrame_beforeGPUPresent();

  SDL_GL_SwapWindow( myWindow.m_window );
}

// ---------------------------------------------------------

static void app_quit()
{
  shader3D.clearShader();
  shaderWireframe.clearShader();

  mesh.clearGPU();

  font.clear();

  bUI_main.clear();
  bUI_main.clearGPU();
  bUI_main.clearShader();

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  for (auto &mt : meshPartContext)
  {
    if (mt.m_thread.joinable()) mt.m_thread.join();
  }

  TRE_LOG("Program finalized with success");
}

// ============================================================================

int main(int argc, char **argv)
{
  if (app_init(argc, argv) != 0)
    return -1;

  app_resized();

#ifdef TRE_EMSCRIPTEN
  emscripten_set_main_loop(app_update, 0, true);
#else
  while(!myWindow.m_quit && !myControls.m_quit)
  {
    app_update();
  }

  app_quit();

#endif

  return 0;
}
