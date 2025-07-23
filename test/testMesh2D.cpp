
#include "tre_shader.h"
#include "tre_model.h"
#include "tre_model_tools.h"
#include "tre_contact_2D.h"
#include "tre_font.h"
#include "tre_ui.h"
#include "tre_windowContext.h"

#include <string>
#include <chrono>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

#define COLOR_ENVELOP_PT       glm::vec4(0.7f, 0.7f, 0.7f, 1.f)
#define COLOR_ENVELOP_CUR_PT   glm::vec4(0.0f, 1.0f, 0.0f, 1.f)
#define COLOR_ENVELOP_VIEW     glm::vec4(1.0f, 0.2f, 1.0f, 1.f)
#define COLOR_MESH_EDGE        glm::vec4(0.8f, 0.8f, 0.8f, 1.f)

// =============================================================================

struct s_meshTriangulation
{
  std::vector<glm::vec2>  m_envelop;

  tre::modelRaw2D         *m_mesh; ///< model that holds data for drawing
  std::size_t              m_meshPartEnvelop;
  std::size_t              m_meshPartTriangulated;
  std::size_t              m_meshPartWireframe;
  std::size_t              m_meshPartCenter;


  s_meshTriangulation(tre::modelRaw2D &mesh)
  {
    m_mesh = &mesh;
    m_meshPartEnvelop = mesh.createPart(0);
    m_meshPartTriangulated = mesh.createPart(0);
    m_meshPartWireframe = mesh.createPart(0);
    m_meshPartCenter = mesh.createPart(4);
  }

  void run()
  {
    std::vector<unsigned> listTriangles;
    tre::modelTools::triangulate(m_envelop, listTriangles);
    TRE_ASSERT(listTriangles.size() % 3 == 0);

    // generate the mesh

    const tre::s_modelDataLayout &layout = m_mesh->layout();

    m_mesh->resizePart(m_meshPartTriangulated, listTriangles.size());
    m_mesh->resizePart(m_meshPartWireframe, listTriangles.size() * 2); // it will draw same edge multiple times, but dont care !

    {
      auto posIt = layout.m_positions.begin<glm::vec2>(m_mesh->partInfo(m_meshPartTriangulated).m_offset);
      auto colorIt = layout.m_colors.begin<glm::vec4>(m_mesh->partInfo(m_meshPartTriangulated).m_offset);

      auto posTriIt = layout.m_positions.begin<glm::vec2>(m_mesh->partInfo(m_meshPartWireframe).m_offset);

      for (std::size_t iT = 0, iTend = listTriangles.size(); iT < iTend; iT += 3)
      {
        const glm::vec2 &ptA = m_envelop[listTriangles[iT + 0]];
        const glm::vec2 &ptB = m_envelop[listTriangles[iT + 1]];
        const glm::vec2 &ptC = m_envelop[listTriangles[iT + 2]];

        *posIt++ = ptA;
        *posIt++ = ptB;
        *posIt++ = ptC;

        float area = 0.f, quality = 0.f;
        tre::triangleQuality(glm::vec3(ptA, 0.f), glm::vec3(ptB, 0.f), glm::vec3(ptC, 0.f), &area, &quality);

        const glm::vec4 color = glm::vec4(0.8f, 1.f - exp(-100.f * area), quality, 0.f);

        *colorIt++ = color;
        *colorIt++ = color;
        *colorIt++ = color;

        *posTriIt++ = ptA;
        *posTriIt++ = ptB;
        *posTriIt++ = ptB;
        *posTriIt++ = ptC;
        *posTriIt++ = ptC;
        *posTriIt++ = ptA;
      }
    }

    m_mesh->colorizePart(m_meshPartWireframe, COLOR_MESH_EDGE);

    {
      const glm::vec2 center = tre::modelTools::computeBarycenter2D(m_envelop);
      auto posIt = layout.m_positions.begin<glm::vec2>(m_mesh->partInfo(m_meshPartCenter).m_offset);
      auto colorIt = layout.m_colors.begin<glm::vec4>(m_mesh->partInfo(m_meshPartCenter).m_offset);
      *posIt++ = center - glm::vec2(0.04f, 0.f);
      *posIt++ = center + glm::vec2(0.04f, 0.f);
      *posIt++ = center - glm::vec2(0.f, 0.04f);
      *posIt++ = center + glm::vec2(0.f, 0.04f);
      *colorIt++ = glm::vec4(0.4f, 1.f, 0.4f, 1.f);
      *colorIt++ = glm::vec4(0.4f, 1.f, 0.4f, 1.f);
      *colorIt++ = glm::vec4(0.4f, 1.f, 0.4f, 1.f);
      *colorIt++ = glm::vec4(0.4f, 1.f, 0.4f, 1.f);
    }
  }

  std::size_t getTriangle(glm::vec2 pt) const
  {
    const tre::s_modelDataLayout &layout = m_mesh->layout();
    const tre::s_partInfo        &part = m_mesh->partInfo(m_meshPartTriangulated);
    for (std::size_t tri = 0, triStop = part.m_size / 3, offset = part.m_offset; tri < triStop; ++tri, offset += 3)
    {
      if (tre::s_contact2D::point_tri(pt, layout.m_positions.get<glm::vec2>(offset + 0),
                                          layout.m_positions.get<glm::vec2>(offset + 1),
                                          layout.m_positions.get<glm::vec2>(offset + 2)))
        return tri;
    }
    return std::size_t(-1);
  }
};

// =============================================================================

int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  tre::windowContext myWindow;
  tre::windowContext::s_controls myControls;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Mesh 2D", SDL_WINDOW_RESIZABLE))
    return -2;

  if (!myWindow.OpenGLInit())
    return -3;

  // - Upload mesh

  tre::modelRaw2D mesh2D;

  s_meshTriangulation data(mesh2D);

  mesh2D.loadIntoGPU();

  // - Load shaders

  tre::shader shaderMainMaterial;
  shaderMainMaterial.loadShader(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR);

  tre::shader shaderDataVisu;
  {
    const char *srcFrag_Color =
    "void main(){\n"
    "  float data = clamp(dot(uniColor, pixelColor),0.,1.);\n"
    "  vec4 diffuseColor = vec4(1.f - 0.9f * data, 2.f * (1.f - data) * data + 0.2f, 0.8f * data + 0.2f, 1.f);\n"
    "  color = diffuseColor;\n"
    "}\n";

    tre::shader::s_layout shLayout(tre::shader::PRGM_2D, tre::shader::PRGM_COLOR | tre::shader::PRGM_UNICOLOR);

    shaderDataVisu.loadCustomShader(shLayout, srcFrag_Color, "dataVisualisation");
  }

  // - load UI

  tre::font font;
  font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true);

  tre::baseUI2D bUI_main;
  bUI_main.set_defaultFont(&font);
  tre::ui::window &wUI_main = *bUI_main.create_window();
  wUI_main.set_layoutGrid(3, 2);
  wUI_main.set_fontSize(tre::ui::s_size(20, tre::ui::SIZE_PIXEL));
  wUI_main.set_colormask(glm::vec4(1.f, 1.f, 1.f, 0.6f));

  wUI_main.create_widgetText(0, 0)->set_text("mode (Enter):");
  wUI_main.create_widgetText(0, 1);
  wUI_main.create_widgetText(1, 0)->set_text("visu (F6/F7):");
  wUI_main.create_widgetText(1, 1);
  wUI_main.create_widgetText(2, 0, 2, 1);

  bUI_main.loadShader();
  bUI_main.loadIntoGPU();

  // - scene and event variables

  SDL_Event event;

  enum e_mainMode { MM_EDIT, MM_VIEW };
  e_mainMode mainMode = MM_EDIT;
  data.m_envelop.push_back(glm::vec2());

  int visuMode = 0;
  const int NvisuMode = 3;
  std::string listDataVisu[NvisuMode] = { "flat", "area", "quality" };

  // - init rendering

  glBindFramebuffer(GL_FRAMEBUFFER,0);
  glDisable(GL_DEPTH_TEST);

  // - MAIN LOOP ------------

  while(!myControls.m_quit)
  {
    // event actions + updates --------

    myWindow.SDLEvent_newFrame();
    myControls.newFrame();

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);
      myControls.treatSDLEvent(event);

      if (event.type == SDL_KEYDOWN)
      {
        if      (event.key.keysym.sym == SDLK_F1) { visuMode = 0; }
        else if (event.key.keysym.sym == SDLK_F2) { visuMode = visuMode == 0 ? NvisuMode - 1 : visuMode - 1; }
        else if (event.key.keysym.sym == SDLK_F3) { visuMode = visuMode == NvisuMode - 1 ? 0 : visuMode + 1; }
        else if (event.key.keysym.sym == SDLK_F4) { visuMode = NvisuMode - 1; }

        if      (event.key.keysym.sym == SDLK_RETURN)
        {
          if (mainMode == MM_EDIT)
          {
            mainMode = MM_VIEW;
            data.m_envelop.pop_back();
            data.run();
          }
          else
          {
            mainMode = MM_EDIT;
            data.m_envelop.push_back(glm::vec2());
          }
        }
      }
    }

    const glm::vec2 invProj = 1.f / glm::vec2(myWindow.m_matProjection2D[0][0], myWindow.m_matProjection2D[1][1]);
    const glm::vec2 mousePos = (glm::vec2(-1.f, 1.f) + glm::vec2(2.f, -2.f) * glm::vec2(myControls.m_mouse) * myWindow.m_resolutioncurrentInv) * invProj;

    if (mainMode == MM_EDIT)
    {
      data.m_envelop.back() = mousePos;

      if (myControls.m_mouseLEFT == myControls.MASK_BUTTON_RELEASED)
      {
        data.m_envelop.push_back(mousePos);
      }
      else if (myControls.m_mouseRIGHT == myControls.MASK_BUTTON_RELEASED && data.m_envelop.size() > 1)
      {
        data.m_envelop.pop_back();
      }
    }
    else if (mainMode == MM_VIEW)
    {
      const std::size_t triHit = data.getTriangle(mousePos);
      //
      //...
    }

    // Geometry generation and update ------
    {
      if (mainMode == MM_EDIT)
      {
        mesh2D.resizePart(data.m_meshPartEnvelop, data.m_envelop.size() * 6);
        mesh2D.colorizePart(data.m_meshPartEnvelop, COLOR_ENVELOP_PT);
        std::size_t offset = mesh2D.partInfo(data.m_meshPartEnvelop).m_offset;
        glm::vec2 ptPrev = data.m_envelop.front();
        for (const glm::vec2 &pt : data.m_envelop)
        {
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt - glm::vec2(0.02f, 0.f);
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt + glm::vec2(0.02f, 0.f);
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt - glm::vec2(0.f, 0.02f);
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt + glm::vec2(0.f, 0.02f);
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = ptPrev;
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt;
          ptPrev = pt;
        }
        mesh2D.layout().m_colors.get<glm::vec4>(offset - 6) = COLOR_ENVELOP_CUR_PT;
        mesh2D.layout().m_colors.get<glm::vec4>(offset - 5) = COLOR_ENVELOP_CUR_PT;
        mesh2D.layout().m_colors.get<glm::vec4>(offset - 4) = COLOR_ENVELOP_CUR_PT;
        mesh2D.layout().m_colors.get<glm::vec4>(offset - 3) = COLOR_ENVELOP_CUR_PT;
        mesh2D.layout().m_colors.get<glm::vec4>(offset - 1) = COLOR_ENVELOP_CUR_PT;
       }
      else
      {
        mesh2D.resizePart(data.m_meshPartEnvelop, data.m_envelop.size() * 2);
        mesh2D.colorizePart(data.m_meshPartEnvelop, COLOR_ENVELOP_VIEW);
        std::size_t offset = mesh2D.partInfo(data.m_meshPartEnvelop).m_offset;
        glm::vec2 ptPrev = data.m_envelop.back();
        for (const glm::vec2 &pt : data.m_envelop)
        {
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = ptPrev;
          mesh2D.layout().m_positions.get<glm::vec2>(offset++) = pt;
          ptPrev = pt;
        }
      }

      mesh2D.updateIntoGPU();
    }

    // main render pass -------------

    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glClear(GL_COLOR_BUFFER_BIT);

    // - render mesh

    mesh2D.drawcall(0, 0, true); // just bind the VAO

    if (mainMode == MM_VIEW)
    {
      glUseProgram(shaderDataVisu.m_drawProgram);
      shaderDataVisu.setUniformMatrix(myWindow.m_matProjection2D);

      const glm::vec4 uChoiceVisu(visuMode == 0 ? 1.f : 0.f,
                                  visuMode == 1 ? 1.f : 0.f,
                                  visuMode == 2 ? 1.f : 0.f,
                                  visuMode == 3 ? 1.f : 0.f);
      glUniform4fv(shaderDataVisu.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(uChoiceVisu));

      mesh2D.drawcall(data.m_meshPartTriangulated, 1, false, GL_TRIANGLES);
    }

    glUseProgram(shaderMainMaterial.m_drawProgram);
    shaderMainMaterial.setUniformMatrix(myWindow.m_matProjection2D);

    if (mainMode == MM_VIEW)
    {
      mesh2D.drawcall(data.m_meshPartWireframe, 1, false, GL_LINES);
      mesh2D.drawcall(data.m_meshPartCenter, 1, false, GL_LINES);
    }

    mesh2D.drawcall(data.m_meshPartEnvelop, 1, false, GL_LINES);

    tre::IsOpenGLok("main render pass - draw meshes");

    // - render UI

    {
      glm::mat3 mat(1.f);
      mat[2][0] = - 1.f /  myWindow.m_matProjection2D[0][0];
      mat[2][1] =   1.f /  myWindow.m_matProjection2D[1][1];
      wUI_main.set_mat3(mat);
    }

    if (mainMode == MM_EDIT)
    {
      wUI_main.get_widgetText(0, 1)->set_text("EDITING");
      wUI_main.get_widgetText(1, 1)->set_text("");
      wUI_main.get_widgetText(2, 0)->set_text("left-click: create new point\nright-click delete last point\nEnter: validate");

    }
    else
    {
      TRE_ASSERT(mainMode == MM_VIEW);
      wUI_main.get_widgetText(0, 1)->set_text("VIEWING");
      wUI_main.get_widgetText(1, 1)->set_text(listDataVisu[visuMode]);
      wUI_main.get_widgetText(2, 0)->set_text("F1-F4: select visu modes\nEnter: edit");
    }

    bUI_main.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
    bUI_main.updateIntoGPU();
    bUI_main.draw();

    tre::IsOpenGLok("UI render pass");

    // end render pass --------------

    SDL_GL_SwapWindow( myWindow.m_window );
  }

  shaderMainMaterial.clearShader();
  shaderDataVisu.clearShader();

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
