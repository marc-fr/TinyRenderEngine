
#include "ui.h"
#include "model.h" // a word-mesh
#include "gizmo.h"
#include "shader.h"
#include "texture.h"
#include "font.h"
#include "windowHelper.h"

#include <time.h>
#include <string>

#include <glm/gtc/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

struct s_sceneOption
{
  float m_ui_alpha = 0.8f;     // main window's transparency
  float m_ui_backalpha = 0.5f; // main window's background transparency
  float m_ui_size = 0.95f;     // main window's font-size
  float m_ui_saturation = 0.f; // main window's color saturation (color masking)
  float m_ui_cellMargin = 1.f; // main window's cells margin
};

// =============================================================================

class widgetTextAndReport : public tre::ui::widgetText
{
public:
  unsigned get_resolvedFontSizePixel() const
  {
    return unsigned( get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize()) / get_parentWindow()->resolve_sizeH(tre::ui::s_size::ONE_PIXEL) );
  }
  unsigned get_pickedFontSizePixel() const
  {
    return get_parentUI()->getDrawObject_Text().get_pickedFontSizePixel(m_adrText.part);
  }
};

// =============================================================================

class widgetColor : public tre::ui::widget
{
public:
  widgetColor() : widget() {}
  virtual ~widgetColor() override {}

  virtual unsigned get_vcountSolid() const override
  {
    return 6;
  }
  //virtual const tre::texture* get_textureSlot() const override; // grid texture ?
  virtual glm::vec2 get_zoneSizeDefault() const override
  {
    const float h = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());
    return glm::vec2(3.f * h, h);

  }
  virtual void compute_data() override
  {
    auto & objsolid = get_parentUI()->getDrawObject_Box();

    objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset, wzone, wcolor, glm::vec4(0.f));
  }
};

// =============================================================================

/**
 * @brief The s_uiManager struct handles all the UI process.
 * This shows a basic example of the usage of UI from TRE.
 */
struct s_uiManager
{
  s_sceneOption   &m_sceneOption;
  tre::gizmo      &m_cubeGizmo;

  tre::baseUI2D   menu;
  tre::ui::window *menuWmain = nullptr;
  tre::ui::window *menuWoption = nullptr;
  tre::ui::window *menuWcolor = nullptr;

  tre::baseUI3D    hud;
  tre::ui::window* hudWmain = nullptr;
  tre::ui::window* hudWfixedSize = nullptr;

  struct s_loadArgs
  {
    tre::font    *font = nullptr;
    tre::texture *picture1 = nullptr;
    bool withMain = true, withOption = true, with3D = true, withColor = true;
  };

  s_uiManager(s_sceneOption &sceneOption, tre::gizmo &gizmo) : m_sceneOption(sceneOption), m_cubeGizmo(gizmo) {}

  bool load(const s_loadArgs &args);
  void updateCameraInfo(const glm::ivec2 &screenSize, const glm::mat3 &matProj2D, const glm::mat4 &matProj3D, const glm::mat4 &matView3D);
  bool acceptEvent(const SDL_Event &event);
  void animate(float dt);
  void draw();
  void clear();
};

// ----------------------------------------------------------------------------

bool s_uiManager::load(const s_loadArgs &args)
{
  menu.set_defaultFont(args.font);

  if (args.withMain)
  {
    menuWmain = menu.create_window();
    TRE_ASSERT(menuWmain != nullptr);

    menuWmain->set_fontSize(0.05f);

    menuWmain->set_color(glm::vec4(0.2f,0.2f,0.2f,m_sceneOption.m_ui_backalpha));
    menuWmain->set_colormask(glm::vec4(0.8f,1.0f,0.95f,m_sceneOption.m_ui_alpha));
    menuWmain->set_alignMask(tre::ui::ALIGN_MASK_HORIZONTAL_CENTERED | tre::ui::ALIGN_MASK_VERTICAL_TOP);

    float localTime = 0.f;
    menuWmain->wcb_animate = [this, localTime](tre::ui::widget *self, float dt) mutable
    {
      glm::mat3 m(1.f);
      m[0][0] = this->m_sceneOption.m_ui_size;
      m[1][1] = this->m_sceneOption.m_ui_size;
      m[2][1] = (localTime < 2.f) ? 0.1f * (2.f - localTime) + 0.99f : 0.99f;

      tre::ui::window *selfW = static_cast<tre::ui::window*>(self);
      selfW->set_mat3(m);

      localTime += dt;
      if (localTime > 15.f) localTime = 0.f;
    };

    menuWmain->set_layoutGrid(12,5);
    menuWmain->set_cellMargin(tre::ui::s_size(m_sceneOption.m_ui_cellMargin, tre::ui::SIZE_PIXEL));

    widgetTextAndReport *wTR = new widgetTextAndReport;
    wTR->set_text("Widgets")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    menuWmain->set_widget(wTR,0,0);
    //menuWmain->create_widgetText(0,0)->set_text("Widgets")->set_color(glm::vec4(1.f,1.f,0.f,1.f));

    menuWmain->create_widgetText(0,1)->set_text("Info")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    menuWmain->create_widgetText(0,2)->set_text("Active")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    menuWmain->create_widgetText(0,3, 2,1)->set_text("Edit")->set_color(glm::vec4(1.f,1.f,0.f,1.f));

    menuWmain->create_widgetText(1,0)->set_text("- widget Picture:");
    menuWmain->create_widgetPicture(1,1)->set_texId(args.picture1);
    menuWmain->create_widgetPicture(1,2)->set_texId(args.picture1)->set_isactive(true);

    menuWmain->create_widgetText(2,0)->set_text("- widget Bar min:");
    menuWmain->create_widgetBar(2,1)->set_withborder(false);
    menuWmain->create_widgetBar(2,2)->set_withborder(false)->set_isactive(true);
    menuWmain->create_widgetBar(2,3)->set_withborder(false)->set_isactive(true)->set_iseditable(true);

    menuWmain->create_widgetText(3,0)->set_text("- widget Bar:");
    menuWmain->create_widgetBar(3,1)->set_withthreshold(true)->set_valuethreshold(0.5f)->set_withtext(true)
        ->set_value(0.4f);
    menuWmain->create_widgetBar(3,2)->set_withthreshold(true)->set_valuethreshold(0.5f)->set_withtext(true)
        ->set_value(0.4f)
        ->set_isactive(true);
    menuWmain->create_widgetBar(3,3)->set_withthreshold(true)->set_valuethreshold(0.5f)->set_withtext(true)
        ->set_value(0.4f)
        ->set_isactive(true)->set_iseditable(true);

    menuWmain->create_widgetText(4,0)->set_text("- widget Line select:");
    menuWmain->create_widgetLineChoice(4,1)->set_values({"Value 1", "2nd", "End"});
    menuWmain->create_widgetLineChoice(4,2)->set_values({"Value 1", "2nd", "End"})->set_isactive(true);
    menuWmain->create_widgetLineChoice(4,3)->set_values({"Value 1", "2nd", "End"})->set_isactive(true)->set_iseditable(true);

    menuWmain->create_widgetText(5,0)->set_text("- widget Check Box:");
    menuWmain->create_widgetBoxCheck(5,1);
    menuWmain->create_widgetBoxCheck(5,2)->set_isactive(true);
    menuWmain->create_widgetBoxCheck(5,3)->set_isactive(true)->set_iseditable(true);

    menuWmain->create_widgetText(6,0)->set_text("- widget Text:");
    menuWmain->create_widgetText(6,1)->set_text("info");
    menuWmain->create_widgetText(6,2)->set_text("click")->set_isactive(true);
    menuWmain->create_widgetTextEdit(6,3)->set_allowMultiLines(true)->set_text("edit me !");

    menuWmain->create_widgetText(7,0)->set_text("- widget Text fill:");
    menuWmain->create_widgetText(7,1)->set_text("info")
        ->set_withbackground(true)->set_withborder(true);
    menuWmain->create_widgetText(7,2)->set_text("click")
        ->set_withbackground(true)->set_withborder(true)->set_isactive(true);

    tre::ui::widget *wFeedBackFontSize = new widgetTextAndReport;
    menuWmain->set_widget(wFeedBackFontSize, 11,0, 1,55);
    wFeedBackFontSize->wcb_animate = [](tre::ui::widget *self, float)
    {
      widgetTextAndReport* selfTx = static_cast<widgetTextAndReport*>(self);

      const unsigned fontSizePixel = selfTx->get_resolvedFontSizePixel();
      const unsigned pickedSizePixel = selfTx->get_pickedFontSizePixel();

      char outT[128];
      std::snprintf(outT, 128, "resolved font size %d (font look-up %d)", fontSizePixel, pickedSizePixel);
      outT[127] = 0;

      selfTx->set_text(outT);
    };
  }

  if (args.withOption)
  {
    menuWoption = menu.create_window();
    TRE_ASSERT(menuWoption != nullptr);

    menuWoption->set_topbar("OPTION", true, false);
    menuWoption->set_color(glm::vec4(0.f,0.f,0.f,m_sceneOption.m_ui_backalpha));
    menuWoption->set_transparency(0.2f+0.8f*m_sceneOption.m_ui_alpha);
    menuWoption->set_alignMask(tre::ui::ALIGN_MASK_HORIZONTAL_LEFT | tre::ui::ALIGN_MASK_VERTICAL_CENTERED);

    menuWoption->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));
    menuWoption->set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));

    {
      glm::mat3 m(1.f);
      m[2][0] = -10.f; // the "resizeEvent" will move the window inside the screen.
      menuWoption->set_mat3(m);
    }

    menuWoption->set_layoutGrid(11,2);
    menuWoption->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    menuWoption->create_widgetText(0,0, 1,2)->set_text("Main window controls")->set_fontsizeModifier(1.1f)->set_color(glm::vec4(1.f, 0.f, 1.f, 1.f));

    menuWoption->create_widgetText(1,0)->set_text("transparency");
    tre::ui::widget *wAlpha = menuWoption->create_widgetBar(1,1)->set_value(m_sceneOption.m_ui_alpha)->set_isactive(true)->set_iseditable(true);
    wAlpha->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_sceneOption.m_ui_alpha = static_cast<tre::ui::widgetBar*>(self)->get_value();
      if (this->menuWmain != nullptr) this->menuWmain->set_transparency(m_sceneOption.m_ui_alpha);
    };

    menuWoption->create_widgetText(2,0)->set_text("background");
    tre::ui::widget *wBackalpha = menuWoption->create_widgetBar(2,1)->set_value(m_sceneOption.m_ui_backalpha)->set_isactive(true)->set_iseditable(true);
    wBackalpha->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_sceneOption.m_ui_backalpha = static_cast<tre::ui::widgetBar*>(self)->get_value();
      if (this->menuWmain != nullptr) this->menuWmain->set_color(glm::vec4(0.2f,0.2f,0.2f,m_sceneOption.m_ui_backalpha));
    };

    menuWoption->create_widgetText(3,0)->set_text("size");
    tre::ui::widget *wSize = menuWoption->create_widgetBar(3,1)->set_value(m_sceneOption.m_ui_size)->set_valuemin(0.1f)->set_valuemax(2.f)->set_isactive(true)->set_iseditable(true);
    wSize->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_sceneOption.m_ui_size = static_cast<tre::ui::widgetBar*>(self)->get_value();
    };

    menuWoption->create_widgetText(4,0)->set_text("color saturation");
    tre::ui::widget *wSatur = menuWoption->create_widgetBar(4,1)->set_value(m_sceneOption.m_ui_saturation)->set_isactive(true)->set_iseditable(true);
    wSatur->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_sceneOption.m_ui_saturation = static_cast<tre::ui::widgetBar*>(self)->get_value();
      const float r = 0.80f - 0.80f * this->m_sceneOption.m_ui_saturation;
      const float g = 1.00f;
      const float b = 0.95f - 0.80f * this->m_sceneOption.m_ui_saturation;
      if (this->menuWmain != nullptr) this->menuWmain->set_colormask(glm::vec4(r,g,b,this->m_sceneOption.m_ui_alpha));
    };

    menuWoption->create_widgetText(5,0)->set_text("cell margin (in Pixel)");
    tre::ui::widget *wCellMargin = menuWoption->create_widgetBar(5,1)->set_value(m_sceneOption.m_ui_cellMargin)->set_valuemin(0.f)->set_valuemax(16.f)->set_isactive(true)->set_iseditable(true);
    wCellMargin->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_sceneOption.m_ui_cellMargin = static_cast<tre::ui::widgetBar*>(self)->get_value();
      if (this->menuWmain != nullptr) this->menuWmain->set_cellMargin(tre::ui::s_size(this->m_sceneOption.m_ui_cellMargin, tre::ui::SIZE_PIXEL));
    };

    menuWoption->create_widgetText(7,0, 1,2)->set_text("Scene controls")->set_fontsizeModifier(1.1f)->set_color(glm::vec4(1.f, 0.f, 1.f, 1.f));

    menuWoption->create_widgetText(8,0)->set_text("gizmo local");
    tre::ui::widget *wGizmoLocal = menuWoption->create_widgetBoxCheck(8,1)->set_value(true)->set_isactive(true)->set_iseditable(true);
    wGizmoLocal->wcb_modified_finished = [this] (tre::ui::widget *self)
    {
      this->m_cubeGizmo.SetLocalFrame(static_cast<tre::ui::widgetBoxCheck*>(self)->get_value());
    };

    menuWoption->create_widgetText(9,0)->set_text("gizmo size");
    tre::ui::widget *wGizmoSize = menuWoption->create_widgetBar(9,1)->set_value(0.13f)->set_valuemin(0.01f)->set_valuemax(0.5f)
                                  ->set_isactive(true)->set_iseditable(true);
    wGizmoSize->wcb_modified_ongoing = [this] (tre::ui::widget *self)
    {
      this->m_cubeGizmo.GizmoSelfScale() = static_cast<tre::ui::widgetBar*>(self)->get_value();
    };

    tre::ui::widget *wFeedBackFontSize = new widgetTextAndReport;
    menuWoption->set_widget(wFeedBackFontSize, 10,0, 1,55);
    wFeedBackFontSize->wcb_animate = [](tre::ui::widget *self, float)
    {
      widgetTextAndReport* selfTx = static_cast<widgetTextAndReport*>(self);

      const unsigned fontSizePixel = selfTx->get_resolvedFontSizePixel();
      const unsigned pickedSizePixel = selfTx->get_pickedFontSizePixel();

      char outT[128];
      std::snprintf(outT, 128, "resolved font size %d (font look-up %d)", fontSizePixel, pickedSizePixel);
      outT[127] = 0;

      selfTx->set_text(outT);
    };
  }

  if (args.withColor)
  {
    menuWcolor = menu.create_window();
    TRE_ASSERT(menuWcolor != nullptr);

    menuWcolor->set_topbar("COLOR Testing", true, true);
    menuWcolor->set_fontSize(tre::ui::s_size(16, tre::ui::SIZE_PIXEL));

    {
      glm::mat3 m(1.f);
      m[2] = glm::vec3(0.8f, 0.5f, 1.f);
      menuWcolor->set_mat3(m);
    }

    menuWcolor->set_layoutGrid(16, 4);
    menuWcolor->set_cellMargin(tre::ui::s_size(3, tre::ui::SIZE_PIXEL));

    menuWcolor->create_widgetText(0, 0, 4, 1)->set_text("base color");
    tre::ui::widgetBar *wBaseR = menuWcolor->create_widgetBar(0, 1); wBaseR->set_value(0.5f)->set_color(glm::vec4(1.f, 0.3f, 0.3f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBaseG = menuWcolor->create_widgetBar(1, 1); wBaseG->set_value(0.7f)->set_color(glm::vec4(0.3f, 1.f, 0.3f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBaseB = menuWcolor->create_widgetBar(2, 1); wBaseB->set_value(0.5f)->set_color(glm::vec4(0.3f, 0.3f, 1.f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBaseA = menuWcolor->create_widgetBar(3, 1); wBaseA->set_value(1.0f)->set_isactive(true)->set_iseditable(true);

    widgetColor *wBase = new widgetColor;
    menuWcolor->set_widget(wBase, 0, 2, 4, 1);

    // menuWcolor->create_widgetLineSeparator

    menuWcolor->create_widgetText(5, 0, 4, 1)->set_text("back color");
    tre::ui::widgetBar *wBackR = menuWcolor->create_widgetBar(5, 1); wBackR->set_value(0.2f)->set_color(glm::vec4(1.f, 0.3f, 0.3f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBackG = menuWcolor->create_widgetBar(6, 1); wBackG->set_value(0.2f)->set_color(glm::vec4(0.3f, 1.f, 0.3f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBackB = menuWcolor->create_widgetBar(7, 1); wBackB->set_value(0.2f)->set_color(glm::vec4(0.3f, 0.3f, 1.f, 1.f))->set_isactive(true)->set_iseditable(true);
    tre::ui::widgetBar *wBackA = menuWcolor->create_widgetBar(8, 1); wBackA->set_value(0.1f)->set_isactive(true)->set_iseditable(true);

    widgetColor *wBack = new widgetColor;
    menuWcolor->set_widget(wBack, 5, 2, 4, 1);

    // menuWcolor->create_widgetLineSeparator

    menuWcolor->create_widgetText(10, 0)->set_text("blend");
    tre::ui::widgetBar *wBlendValue = menuWcolor->create_widgetBar(10, 1); wBlendValue->set_withtext(true)->set_isactive(true)->set_iseditable(true);
    widgetColor *wBlend = new widgetColor;
    menuWcolor->set_widget(wBlend, 10, 2);

    menuWcolor->create_widgetText(10, 3)->set_text("inv");

    menuWcolor->create_widgetText(11, 0)->set_text("bright");
    tre::ui::widgetBar *wBrightValue = menuWcolor->create_widgetBarZero(11, 1); wBrightValue->set_withtext(true)->set_isactive(true)->set_iseditable(true);
    widgetColor *wBright = new widgetColor;
    menuWcolor->set_widget(wBright, 11, 2);
    widgetColor *wBrightInv = new widgetColor;
    menuWcolor->set_widget(wBrightInv, 11, 3);

    menuWcolor->create_widgetText(12, 0)->set_text("saturation");
    tre::ui::widgetBar *wSaturationValue = menuWcolor->create_widgetBarZero(12, 1); wSaturationValue->set_withtext(true)->set_isactive(true)->set_iseditable(true);
    widgetColor *wSaturation = new widgetColor;
    menuWcolor->set_widget(wSaturation, 12, 2);
    widgetColor *wSaturationInv = new widgetColor;
    menuWcolor->set_widget(wSaturationInv, 12, 3);

    menuWcolor->create_widgetText(13, 0)->set_text("hue");
    tre::ui::widgetBar *wHueValue = menuWcolor->create_widgetBarZero(13, 1); wHueValue->set_withtext(true)->set_isactive(true)->set_iseditable(true);
    widgetColor *wHue = new widgetColor;
    menuWcolor->set_widget(wHue, 13, 2);
    widgetColor *wHueInv = new widgetColor;
    menuWcolor->set_widget(wHueInv, 13, 3);

    menuWcolor->wcb_animate = [=](tre::ui::widget *, float) // easy way (the values are re-computed each frames)
    {
      const glm::vec4 baseColor = glm::vec4(wBaseR->get_value(), wBaseG->get_value(), wBaseB->get_value(), wBaseA->get_value());
      const glm::vec4 backColor = glm::vec4(wBackR->get_value(), wBackG->get_value(), wBackB->get_value(), wBackA->get_value());

      wBase->set_color(baseColor);
      wBack->set_color(backColor);

      wBlend->set_color(tre::ui::blendColor(baseColor, backColor, wBlendValue->get_value()));
      wBright->set_color(tre::ui::transformColor(baseColor, tre::ui::COLORTHEME_LIGHTNESS, wBrightValue->get_value()));
      wSaturation->set_color(tre::ui::transformColor(baseColor, tre::ui::COLORTHEME_SATURATION, wSaturationValue->get_value()));
      wHue->set_color(tre::ui::transformColor(baseColor, tre::ui::COLORTHEME_HUE, wHueValue->get_value()));

      wBrightInv->set_color(tre::ui::inverseColor(baseColor, tre::ui::COLORTHEME_LIGHTNESS));
      wSaturationInv->set_color(tre::ui::inverseColor(baseColor, tre::ui::COLORTHEME_SATURATION));
      wHueInv->set_color(tre::ui::inverseColor(baseColor, tre::ui::COLORTHEME_HUE));
    };
  }

  menu.loadIntoGPU();
  menu.loadShader();

  if (args.with3D)
  {
    hud.set_defaultFont(args.font);

    hudWmain = hud.create_window();
    TRE_ASSERT(hudWmain != nullptr);

    hudWmain->set_color(glm::vec4(0.5f,0.5f,0.5f,0.9f));
    hudWmain->set_colormask(glm::vec4(0.3f,1.f,0.3f,0.7f));
    hudWmain->set_topbar("3D interface", true, true);
    {
      glm::mat4 m(1.f);
      m = glm::rotate(m, -0.6f, glm::vec3(0.f,1.f,0.f));
      m[3] = glm::vec4(3.f,1.f,0.f,1.f); // position in world-space.
      hudWmain->set_mat4(m);
    }

    hudWmain->set_fontSize(0.5f);

    hudWmain->set_layoutGrid(3,2);

    hudWmain->create_widgetText(0, 0)->set_text("Control bar");
    hudWmain->create_widgetBar(0,1)->set_value(0.2f)->set_valuethreshold(0.3f)->set_withthreshold(true)->set_iseditable(true)->set_isactive(true);

    hudWmain->create_widgetText(1, 0)->set_text("Control bar 2");
    hudWmain->create_widgetBarZero(1,1)->set_value(0.4f)->set_valuemin(-0.8f)->set_iseditable(true)->set_isactive(true);

    hudWfixedSize = hud.create_window();
    TRE_ASSERT(hudWfixedSize != nullptr);

    hudWfixedSize->set_color(glm::vec4(0.5f,0.5f,0.5f,0.9f));
    hudWfixedSize->set_colormask(glm::vec4(0.3f,1.f,0.3f,0.7f));
    hudWfixedSize->set_topbar("3D interface (fixed size)", true, true);
    {
      glm::mat4 m(1.f);
      m = glm::rotate(m, -0.5f, glm::vec3(0.f,1.f,0.f));
      m[3] = glm::vec4(3.f,2.f,0.f,1.f); // position in world-space.
      hudWfixedSize->set_mat4(m);
    }

    hudWfixedSize->set_fontSize(tre::ui::s_size(14, tre::ui::SIZE_PIXEL));

    hudWfixedSize->set_layoutGrid(3,2);

    hudWfixedSize->create_widgetText(0, 0)->set_text("Control bar");
    hudWfixedSize->create_widgetBar(0,1)->set_value(0.2f)->set_valuethreshold(0.3f)->set_withthreshold(true)->set_iseditable(true)->set_isactive(true);

    hudWfixedSize->create_widgetText(1, 0)->set_text("Control bar 2");
    hudWfixedSize->create_widgetBarZero(1,1)->set_value(0.4f)->set_valuemin(-0.8f)->set_iseditable(true)->set_isactive(true);
  }

  hud.loadIntoGPU();
  hud.loadShader();

  return true;
}

// ----------------------------------------------------------------------------

void s_uiManager::updateCameraInfo(const glm::ivec2 &screenSize, const glm::mat3 &matProj2D, const glm::mat4 &matProj3D, const glm::mat4 &matView3D)
{
  menu.updateCameraInfo(matProj2D, screenSize);
  hud.updateCameraInfo(matProj3D, matView3D, screenSize);

  if (menuWoption != nullptr)
  {
    glm::mat3 m = menuWoption->get_mat3();
    if (m[2][0] < -0.98f / matProj2D[0][0])
    {
      m[2][0] = -0.98f / matProj2D[0][0];
      menuWoption->set_mat3(m);
    }
  }
}

// ----------------------------------------------------------------------------

bool s_uiManager::acceptEvent(const SDL_Event &event)
{
  bool ret = false;
  ret |= menu.acceptEvent(event);
  ret |= hud.acceptEvent(event);
  return ret;
}

// ----------------------------------------------------------------------------

void s_uiManager::animate(float dt)
{
  menu.animate(dt);
  hud.animate(dt);
}

// ----------------------------------------------------------------------------

void s_uiManager::draw()
{
  menu.updateIntoGPU();
  hud.updateIntoGPU();

  menu.draw();
  hud.draw();
}

// ----------------------------------------------------------------------------

void s_uiManager::clear()
{
  menu.clear();
  hud.clear();

  // optional (the windows are hold by the UIs, so those pointers are invalid now.)
  {
    menuWmain = nullptr;
    menuWoption = nullptr;
    hudWmain = nullptr;
    hudWfixedSize = nullptr;
  }

  menu.clearGPU();
  hud.clearGPU();

  menu.clearShader();
  hud.clearShader();
}

// =============================================================================

int main(int argc, char **argv)
{
  tre::windowHelper myWindow;

  if (!myWindow.SDLInit(SDL_INIT_VIDEO, "test GUI", SDL_WINDOW_RESIZABLE))
    return -1;

  if (!myWindow.OpenGLInit())
    return -2;

  // - scene variables + initialisation
  glm::mat4 mView = glm::mat4(1.f);
  mView[3] = glm::vec4(0.f, -3.f, -10.f, 1.f);

  glm::mat4 mModelCube = glm::mat4(1.f);

  s_sceneOption sceneOption;

  tre::gizmo cubeGizmo;
  cubeGizmo.SetMode(tre::gizmo::GMODE_TRANSLATING);
  cubeGizmo.setTransfromToUpdate(&mModelCube);
  cubeGizmo.SetLocalFrame(true);

  cubeGizmo.loadIntoGPU();
  cubeGizmo.loadShader();

  // - load resources

  tre::shader::s_UBOdata_sunLight sunLight;
  sunLight.direction = glm::normalize(glm::vec3(-0.243f,-0.970f,0.f));
  sunLight.color = glm::vec3(0.9f,0.9f,0.9f);
  sunLight.colorAmbiant = glm::vec3(0.1f,0.1f,0.1f);

  tre::shader worldMaterial;
  worldMaterial.loadShader(tre::shader::PRGM_3D, tre::shader::PRGM_LIGHT_SUN | tre::shader::PRGM_UNICOLOR);

  tre::modelStaticIndexed3D worldMesh(tre::modelStaticIndexed3D::VB_NORMAL);
  worldMesh.createPartFromPrimitive_box(glm::mat4(1.f), 3.f);
  worldMesh.loadIntoGPU();

  const glm::vec4 ucolorCube(0.4f,0.4f,0.8f,1.f);

  tre::font texFont;
#ifdef TRE_WITH_FREETYPE
  std::vector<unsigned> texSizes = { 64, 12 };
  texFont.loadNewFontMapFromTTF(TESTIMPORTPATH "resources/DejaVuSans.ttf", texSizes);
#else
  texFont.loadNewFontMapFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88");
#endif

  tre::texture texTest;
  texTest.loadNewTextureFromBMP(TESTIMPORTPATH "resources/quad.bmp", tre::texture::MMASK_MIPMAP);

  // - create UI

  s_uiManager uiManager(sceneOption, cubeGizmo);

  {
    s_uiManager::s_loadArgs uiArgs;
    uiArgs.font = &texFont;
    uiArgs.picture1 = &texTest;

    //uiArgs.withMain = false;  // for debuging
    //uiArgs.with3D = false;    // for debuging
    //uiArgs.withColor = false; // for debuging

    uiManager.load(uiArgs);

    uiManager.updateCameraInfo(myWindow.m_resolutioncurrent, myWindow.m_matProjection2D, myWindow.m_matProjection3D, mView);
  }

  tre::IsOpenGLok("main: initialization");

  tre::checkLayoutMatch_Shader_Model(&worldMaterial, &worldMesh);

  // - event and time variables

  SDL_Event event;

  // - MAIN LOOP --->

  while(!myWindow.m_controls.m_quit)
  {
    myWindow.m_timing.newFrame();

    // event actions + updates --------

    const glm::mat4 mPV(myWindow.m_matProjection3D * mView);

    myWindow.m_controls.newFrame();

    cubeGizmo.updateCameraInfo(myWindow.m_matProjection3D, mView, myWindow.m_resolutioncurrent);
    uiManager.updateCameraInfo(myWindow.m_resolutioncurrent, myWindow.m_matProjection2D, myWindow.m_matProjection3D, mView);

    //-> SDL events
    while(SDL_PollEvent(&event) == 1)
    {
      myWindow.SDLEvent_onWindow(event);

      if (uiManager.acceptEvent(event))
        continue;
      if (cubeGizmo.acceptEvent(event))
        continue;
      if (myWindow.m_controls.treatSDLEvent(event))
        continue;
    }

    uiManager.animate(glm::min(myWindow.m_timing.frametime, 0.033f));

    // display ----------------------
    glViewport(0, 0, myWindow.m_resolutioncurrent.x, myWindow.m_resolutioncurrent.y);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    tre::shader::updateUBO_sunLight(sunLight);

    // => background picture

    //glDisable(GL_DEPTH_TEST);
    // TODO ...

    // => cube

    glEnable(GL_DEPTH_TEST);

    glUseProgram(worldMaterial.m_drawProgram);

    const glm::mat4 mPVMcube = mPV * mModelCube;

    worldMaterial.setUniformMatrix(mPVMcube, mModelCube, mView);

    glUniform4fv(worldMaterial.getUniformLocation(tre::shader::uniColor), 1, glm::value_ptr(ucolorCube));

    worldMesh.drawcallAll();

    // => gizmo

    glDisable(GL_DEPTH_TEST);

    cubeGizmo.updateIntoGPU();
    cubeGizmo.draw();

    // => UI

    uiManager.draw();

    // End render pass

    tre::IsOpenGLok("rendering");

    SDL_GL_SwapWindow( myWindow.m_window );

    myWindow.m_timing.endFrame(0, myWindow.m_controls.m_pause);
  }

  TRE_LOG("Main loop exited");
  TRE_LOG("Average CPU-frame: " << int(myWindow.m_timing.frametime_average * 1000) << " ms");

  // Finalize

  worldMesh.clearGPU();
  worldMaterial.clearShader();

  uiManager.clear();

  cubeGizmo.clearGPU();
  cubeGizmo.clearShader();

  texFont.clear();
  texTest.clear();

  tre::shader::clearUBO();

  tre::IsOpenGLok("quitting");

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

  TRE_LOG("Program finalized with success");

  return 0;
}
