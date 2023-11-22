#ifndef UI_H
#define UI_H

#include "tre_utils.h"
#include "tre_model.h"
#include "tre_texture.h"

#include <vector>
#include <string>
#include <functional>

namespace tre {

class shader;   // foward-declaration
class font;     // foward-declaration

class baseUI;   // foward-declaration
class baseUI2D; // foward-declaration
class baseUI3D; // foward-declaration

namespace ui {

class widget; // foward-declaration
class window; // foward-declaration

// Size =======================================================================

enum e_sizeUnit
{
  SIZE_NATIVE, ///< size is in the native space (eq. the window's model space)
  SIZE_PIXEL   ///< size is in the screen space (eq. in pixel unit)
};

struct s_size
{
  float       size;
  e_sizeUnit  unit;

  s_size() : size(0.f), unit(SIZE_NATIVE) {}
  s_size(float a_size, e_sizeUnit a_unit = SIZE_NATIVE) : size(a_size), unit(a_unit) {}

  bool operator==(const s_size &other) const { return size == other.size && unit == other.unit; }
  bool operator!=(const s_size &other) const { return size != other.size || unit != other.unit; }

  bool valid() const { return size > 0.f; }

  static const s_size ONE_PIXEL;
};

// Color ======================================================================

/// Color modes for color modifiers (highlighting, ...)
enum e_colorThemeMode
{
  COLORTHEME_LIGHTNESS,  ///< Use the brightness of the color
  COLORTHEME_HUE,        ///< Use the hue of the color
  COLORTHEME_SATURATION, ///< Use the different of the color with the parent color (or the background color)
};

glm::vec4 transformColor(const glm::vec4 &baseColor, e_colorThemeMode mode, float cursor);
glm::vec4 inverseColor(const glm::vec4 &baseColor, e_colorThemeMode mode);
glm::vec4 blendColor(const glm::vec4 &frontColor, const glm::vec4 &backColor, float cursor /**< in [0,1].*/);

struct s_colorTheme
{
  e_colorThemeMode mode;
  float            factor;

  s_colorTheme(e_colorThemeMode m, float f) : mode(m), factor(f) {}
  bool operator==(const s_colorTheme &other) const { return mode == other.mode && factor == other.factor; }
  bool operator!=(const s_colorTheme &other) const { return mode != other.mode || factor != other.factor; }

  glm::vec4 resolveColor(const glm::vec4 &baseColor, float modifier) const
  {
    return ui::transformColor(baseColor, mode, modifier * factor);
  }
};

// Language (auto-translate) ==================================================

#ifndef TRE_UI_NLANGUAGES
#define TRE_UI_NLANGUAGES 2
#endif

// Layout =====================================================================

enum e_alignment
{
  ALIGN_MASK_HORIZONTAL_LEFT     = 0x0001,
  ALIGN_MASK_HORIZONTAL_CENTERED = 0x0002,
  ALIGN_MASK_HORIZONTAL_RIGHT    = 0x0004,

  ALIGN_MASK_VERTICAL_TOP        = 0x0010,
  ALIGN_MASK_VERTICAL_CENTERED   = 0x0020,
  ALIGN_MASK_VERTICAL_BOTTOM     = 0x0040,

  ALIGN_MASK_CENTERED = ALIGN_MASK_HORIZONTAL_CENTERED | ALIGN_MASK_VERTICAL_CENTERED,
  ALIGN_MASK_LEFT_TOP = ALIGN_MASK_HORIZONTAL_LEFT | ALIGN_MASK_VERTICAL_TOP,
};

struct s_layoutGrid
{
  struct s_cell
  {
    widget     *m_widget = nullptr;
    glm::uvec2 m_span = glm::uvec2(1); ///< span (x:col, y:row)
    uint       m_alignMask = ALIGN_MASK_HORIZONTAL_LEFT | ALIGN_MASK_VERTICAL_CENTERED;
  };

  glm::uvec2          m_dimension = glm::uvec2(0); ///< Dimension (x:columns, y:rows)
  std::vector<s_cell> m_cells;
  s_size              m_cellMargin = s_size::ONE_PIXEL;

  std::vector<s_size> m_rowsHeight_User, m_colsWidth_User; ///< Size of rows and columns (given by the user)
  std::vector<float>  m_rowsHeight, m_colsWidth; ///< Size of rows and columns (real)

  std::vector<s_size> m_rowsInbetweenSpace, m_colsInbetweenSpace; ///< In-between size of rows and columns

  void set_dimension(uint rows, uint cols);
  uint index(uint iRow, uint iCol) const { TRE_ASSERT(iRow < m_dimension.y); TRE_ASSERT(iCol < m_dimension.x); return iCol + iRow * m_dimension.x; }
  glm::vec2 computeWidgetZones(const ui::window &win, const glm::vec2 &offset = glm::vec2(0.f)); ///< Computes the zone assigned to each widget. Returns the global size.
  void clear();
};

// Other layout ?

// Events =====================================================================

struct s_eventIntern
{
  glm::vec3 mousePos;     ///< mouse position (in the window's frame)
  glm::vec3 mousePosPrev; ///< same as mousePos, but on last mouse button-down event

  uint mouseButtonIsPressed = 0; ///< a button is pressed. Use SDL_BUTTON_*MASK for masking.
  uint mouseButtonPrev = 0;

  uint keyDown = 0; ///< a key is begin pressed. Use SDLK_** for value.
  const char *textInput = nullptr; ///< when using SDL_TextInput mode

  bool accepted = false;
};

// Widgets ====================================================================

#define widget_DECLAREATTRIBUTE(wname,atype,aname,ainit,updateFlag) \
  public : \
    const atype & get_##aname() const { return w##aname; } \
    wname * set_##aname(const atype & a_##aname) { updateFlag |= (w##aname != a_##aname); w##aname = a_##aname; return this; } \
  protected :  \
    atype w##aname ainit;

/**
 * @brief The widget class
 */
class widget
{
public:
  widget() {}
  virtual ~widget() {}

  /// @name attributes
  /// @{
  widget_DECLAREATTRIBUTE(widget,glm::vec4,color,= glm::vec4(0.8f, 0.8f, 0.8f, 1.f), m_isUpdateNeededData) ///< main color
  widget_DECLAREATTRIBUTE(widget,bool,isactive,= false, m_isUpdateNeededData) ///< When true, the widget receives events and can trigger callbacks
  widget_DECLAREATTRIBUTE(widget,bool,iseditable,= false, m_isUpdateNeededData) ///< When true, the widget is editable from events
  widget_DECLAREATTRIBUTE(widget,bool,ishighlighted, = false, m_isUpdateNeededData) ///< read & write attribute: "acceptEvent" modifies the value; user can change the value, but no callback will be triggered in this case.
  widget_DECLAREATTRIBUTE(widget,s_size,widthMin, = 0.f, m_isUpdateNeededLayout) ///< minimal width
  widget_DECLAREATTRIBUTE(widget,s_size,heightMin, = 0.f, m_isUpdateNeededLayout) ///< minimal height
  /// @}

  /// @name callbacks
  /// @{
public:
  std::function<void(widget *)> wcb_gain_focus = nullptr; ///< Triggered when the widget gain focus. No need to set hasfocus = true (done internally).
  std::function<void(widget *)> wcb_loss_focus = nullptr; ///< Triggered when the widget gain focus. No need to set hasfocus = false (done internally).

  std::function<void(widget *)> wcb_modified_ongoing = nullptr; ///< Triggered on every modifications
  std::function<void(widget *)> wcb_modified_finished = nullptr; ///< Triggered only at the last modification

  std::function<void(widget *)> wcb_clicked_left = nullptr; ///< Triggered when the user clicks on the widget with the left button
  std::function<void(widget *)> wcb_clicked_right = nullptr; ///< Triggered when the user clicks on the widget with the right button

  std::function<void(widget *, float)> wcb_animate = nullptr; ///< Called when the root "animate" method is called.
  /// @}

  /// @name virtual methods
  /// @{
protected:
  virtual uint get_vcountSolid() const { return 0; } ///< Return nbr of vertices needed for solid drawing (triangles)
  virtual uint get_vcountLine() const { return 0; } ///< Return nbr of vertices needed for solid drawing (lines)
  virtual uint get_vcountPict() const { return 0; } ///< Return nbr of vertices needed for textured drawing
  virtual uint get_vcountText() const { return 0; } ///< Return nbr of vertices needed for text drawing
  virtual uint get_textureSlot() const { return uint(-1); } ///< Return texture for textured-drawing (a widget cannot use more than 1 texture slot, excluding the font of the text)
  virtual glm::vec2 get_zoneSizeDefault() const = 0; ///< Return the default widget size (in the window's space).
  virtual void compute_data() = 0;
  virtual void acceptEvent(s_eventIntern &event) { acceptEventBase_focus(event); acceptEventBase_click(event); }
  virtual void animate(float dt) { if (wcb_animate != nullptr) wcb_animate(this, dt); }
  /// @}

  /// @name helpers
  /// @{
public:
  bool getIsOverPosition(const glm::vec3 & position) const; ///< Return true if the position is under the m_zone. Ignore position.z value.
  glm::vec4 resolve_color() const;
  widget* set_colorAlpha(float alpha) { m_isUpdateNeededData |= (alpha != wcolor.a); wcolor.a = alpha; return this; }
  /// @}

  /// @name intern helpers
  /// @{
protected:
  void acceptEventBase_focus(s_eventIntern &event); ///< trigger focus-state
  void acceptEventBase_click(s_eventIntern &event); ///< trigger simple click-callbacks (warning: event can be modified)
  void set_zone(const glm::vec4 &assignedZone) { m_isUpdateNeededData |= (m_zone != assignedZone); m_zone = assignedZone; }

  glm::vec4  m_zone = glm::vec4(0.f);        ///< Widget zone (xmin,ymin,xmax,ymax)
  bool       m_isUpdateNeededAdress = true;  ///< true when an object's adress update is needed (nbr of vertice changed, ...)
  bool       m_isUpdateNeededLayout = true;  ///< true when a layout update is needed (widget gets resized, ..., typically when get_zoneSizeDefault() will return a new different value)
  bool       m_isUpdateNeededData   = true;  ///< true when the vertice-data will changed (positions, color, ...)

  struct s_objAdress
  {
    uint part = 0u;
    uint offset = 0u;
  };
  mutable s_objAdress m_adSolid, m_adrLine, m_adrPict, m_adrText; ///< Adress plage for each specific object
  /// @}

  /// @name parent-hood
  /// @{
public:
  widget*               get_parent() const { TRE_ASSERT(m_parent != nullptr); return m_parent; }
  virtual window*       get_parentWindow() const { TRE_ASSERT(m_parent != nullptr); return m_parent->get_parentWindow(); }
  virtual tre::baseUI*  get_parentUI() const { TRE_ASSERT(m_parent != nullptr); return m_parent->get_parentUI(); }
  virtual float         resolve_colorModifier() const { TRE_ASSERT(m_parent != nullptr); return (wisactive && wishighlighted ? 1.f : 0.f) + m_parent->resolve_colorModifier(); }
private:
  widget *m_parent = nullptr;
  /// @}

friend class window;
friend struct s_layoutGrid;
};

// ----------------------------------------------------------------------------

#define widget_DECLARECONSTRUCTORS(cname) \
  public: \
    cname() : widget() {} \
    virtual ~cname() override {} \

#define widget_DECLARECOMMUNMETHODS() \
  protected : \
    virtual uint get_vcountSolid() const override; \
    virtual uint get_vcountLine() const override; \
    virtual uint get_vcountPict() const override; \
    virtual uint get_vcountText() const override; \
    virtual uint get_textureSlot() const override; \
    virtual glm::vec2 get_zoneSizeDefault() const override; \
    virtual void compute_data() override; \
    virtual void acceptEvent(s_eventIntern &event) override;


class widgetText : public widget
{
  widget_DECLARECONSTRUCTORS(widgetText)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetText,std::string,text,= "", m_isUpdateNeededAdress = m_isUpdateNeededLayout) ///< Text to be drawn. Note: it makes a local copy of the text (safe but copy data).
  widget_DECLAREATTRIBUTE(widgetText,float,fontsizeModifier,= 1.f, m_isUpdateNeededLayout) ///< Font-size factor in regards with the parent window's font-size.
  widget_DECLAREATTRIBUTE(widgetText,bool,withborder,= false, m_isUpdateNeededAdress) ///< True if a border will be drawn
  widget_DECLAREATTRIBUTE(widgetText,bool,withbackground,= false, m_isUpdateNeededAdress) ///< True if a background will be drawn
};

class widgetTextTranslatate : public widgetText
{
public:
  widgetTextTranslatate() : widgetText() {}
  virtual ~widgetTextTranslatate() {}

protected:
  virtual uint get_vcountText() const override;
  virtual glm::vec2 get_zoneSizeDefault() const override;
  virtual void compute_data() override;

  std::array<std::string, TRE_UI_NLANGUAGES> wtexts;

public:
  widgetText *set_text(const std::string &) { TRE_FATAL("do not call this."); return this; } ///< this has undefined behavior. Use wtexts instead.
  widgetTextTranslatate* set_texts(tre::span<std::string> values);
  widgetTextTranslatate* set_texts(tre::span<const char*> values);
  widgetTextTranslatate* set_text_LangIdx(const std::string &str, std::size_t lidx);
};

class widgetTextEdit : public widgetText
{
public:
  widgetTextEdit() : widgetText() { wisactive = wiseditable = true; }
  virtual ~widgetTextEdit() {}

  virtual uint get_vcountSolid() const override;
  virtual glm::vec2 get_zoneSizeDefault() const override;
  virtual void compute_data() override;
  virtual void acceptEvent(s_eventIntern &event) override;
  virtual void animate(float dt) override;

  widget_DECLAREATTRIBUTE(widgetTextEdit,bool,allowMultiLines,= false, m_isUpdateNeededLayout)
  widget_DECLAREATTRIBUTE(widgetTextEdit,float,cursorAnimSpeed,= 1.f, m_isUpdateNeededData)

public:
  bool get_isEditing() const { return wisEditing; }

private:
  bool  wisEditing = false;
  int   wcursorPos = -1;
  float wcursorAnimTime = 0.f;
};

class widgetPicture : public widget
{
  widget_DECLARECONSTRUCTORS(widgetPicture)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetPicture,uint,texId,= uint(-1), m_isUpdateNeededAdress)
  widget_DECLAREATTRIBUTE(widgetPicture,glm::vec4,texUV,= glm::vec4(0.f,0.f,1.f,1.f), m_isUpdateNeededLayout)
  widget_DECLAREATTRIBUTE(widgetPicture,float,heightModifier,= 1.f, m_isUpdateNeededLayout) ///< height factor in regards with the default cell's height.
  widget_DECLAREATTRIBUTE(widgetPicture,bool,snapPixels, = false, m_isUpdateNeededData) ///< snap pixels to entire zoom factor
};

class widgetBar : public widget
{
  widget_DECLARECONSTRUCTORS(widgetBar)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetBar,float,valuemin,= 0.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBar,float,valuemax,= 1.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBar,float,value,= 0.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBar,bool,withborder,= true, m_isUpdateNeededAdress) ///< True if a border must to be drawn
  widget_DECLAREATTRIBUTE(widgetBar,bool,withthreshold,= false, m_isUpdateNeededAdress)
  widget_DECLAREATTRIBUTE(widgetBar,float,valuethreshold,= 1.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBar,bool,withtext,= false, m_isUpdateNeededAdress)
  widget_DECLAREATTRIBUTE(widgetBar,float,snapInterval, = 0.f, m_isUpdateNeededData) ///< Negative or zero value means no snapping
  widget_DECLAREATTRIBUTE(widgetBar,float,widthFactor, = 5.f, m_isUpdateNeededData) ///< Width/Height ratio
};

class widgetBarZero : public widgetBar
{
public:
  widgetBarZero() : widgetBar() { wvaluemin = -1.f; }
  virtual ~widgetBarZero() {}

  virtual uint get_vcountLine() const;
  virtual void compute_data();
};

class widgetSlider : public widget
{
  widget_DECLARECONSTRUCTORS(widgetSlider)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetSlider,float,valuemin,= 0.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetSlider,float,valuemax,= 1.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetSlider,float,value,= 0.f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetSlider,float,snapInterval, = 0.f, m_isUpdateNeededData) ///< Negative or zero value means no snapping
  widget_DECLAREATTRIBUTE(widgetSlider,float,widthFactor, = 5.f, m_isUpdateNeededData) ///< Width/Height ratio
};

class widgetBoxCheck : public widget
{
  widget_DECLARECONSTRUCTORS(widgetBoxCheck)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetBoxCheck,bool,value,= false, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBoxCheck,float,margin,= 0.15f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBoxCheck,float,thin,= 0.15f, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetBoxCheck,bool,withBorder,= true, m_isUpdateNeededAdress)
};

class widgetLineChoice : public widget
{
  widget_DECLARECONSTRUCTORS(widgetLineChoice)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetLineChoice,std::vector<std::string>,values,, m_isUpdateNeededAdress)
  widget_DECLAREATTRIBUTE(widgetLineChoice,uint,selectedIndex,= 0, m_isUpdateNeededData)
  widget_DECLAREATTRIBUTE(widgetLineChoice,bool,cyclic,= false, m_isUpdateNeededData)

public:
  widgetLineChoice* set_values(tre::span<std::string> values); // overload
  widgetLineChoice* set_values(tre::span<const char*> values); // overload

protected:
  bool wisHoveredLeft = false;
  bool wisHoveredRight = false;
};

#undef widget_DECLARECONSTRUCTORS
#undef widget_DECLARECOMMUNMETHODS
#undef widget_DECLAREATTRIBUTE

// declare window =============================================================

/**
 * @brief The window class
 *
 * Inherits from class widget, but:
 * - m_zone means (xoffset, yoffset, width, height)
 */
class window : public widget
{
protected:
  window(tre::baseUI * parent) : widget(), m_parentUI(parent) { wcolor = glm::vec4(0.f); wOwnWidgets.fill(nullptr); }
  ~window() override { clear(); }

  /// @name global properties
  /// @{

public:
  bool get_visible() const { return wvisible; }
  void set_visible(bool a_visible); // if the window becomes not visible, then a dummy event is triggered.
protected:
  bool wvisible = true;

#define window_PROPERTY(atype,aname,ainit,updateFlag) \
  public: \
    const atype &get_##aname() const { return w##aname; } \
    void  set_##aname(atype a_##aname) { updateFlag |= (w##aname != a_##aname); w##aname = a_##aname; } \
  protected: \
    atype w##aname = ainit;

  window_PROPERTY(s_size, fontSize, s_size(16, SIZE_PIXEL), m_isUpdateNeededLayout)
  window_PROPERTY(glm::vec4, colormask, glm::vec4(1.f), m_isUpdateNeededData)
  window_PROPERTY(s_colorTheme, colortheme, s_colorTheme(COLORTHEME_LIGHTNESS, 0.1f), m_isUpdateNeededData)
  window_PROPERTY(uint, alignMask, ALIGN_MASK_LEFT_TOP, m_isUpdateNeededData)

  window_PROPERTY(glm::mat4, mat4, glm::mat4(1.f), m_isUpdateNeededLayout) // used for 2D-ui
  window_PROPERTY(glm::mat3, mat3, glm::mat3(1.f), m_isUpdateNeededLayout) // used for 3D-ui

#undef window_PROPERTY

public:
  window* set_topbar(const std::string &name, bool canBeMoved, bool canBeClosed);
  window* set_topbarName(const std::string &name); ///< rename the top-bar title (only after "set_topbar" call)
  window* set_transparency(const float alpha) { m_isUpdateNeededData |= (wcolormask.w != alpha); wcolormask.w = alpha; return this; } ///< set the global transparency

public:
  void set_layoutGrid(uint row, uint col) { m_isUpdateNeededAdress = true; wlayout.set_dimension(row, col); } ///< set the layout.
  void set_colWidth(uint col, const s_size width); ///< set the width of a column, that overwrites the automatic size. A negative value will unset the value.
  void set_rowHeight(uint row, const s_size height); /// set the height of a row, that overwrites the automatic size. A negative value will unset the value.
  void set_cellMargin(const s_size margin) { m_isUpdateNeededLayout |= (wlayout.m_cellMargin != margin); wlayout.m_cellMargin = margin; }
  void set_colAlignment(uint col, uint alignMask); ///< set the alignment of the widgets in the column
  void set_rowAlignment(uint row, uint alignMask); ///< set the alignment of the widgets in the row
  void set_colSpacement(uint col, const s_size width, const bool atLeft = true); ///< set a space in-between two columns
  void set_rowSpacement(uint row, const s_size height, const bool atTop = true); ///< set a space in-between two rows

  float     resolve_sizeW(s_size size) const { return resolve_sizeWH(size).x; }
  float     resolve_sizeH(s_size size) const { return resolve_sizeWH(size).y; }
  glm::vec2 resolve_sizeWH(s_size size) const; ///< returns the size of a pixel, in the local-space of the window.
  glm::vec2 resolve_pixelOffset() const; ///< returns a local-space position that is in the center of a pixel. The offset might not be minimal, nor positive.
  //0}

  /// @name feedback properties (must be called after the UI is loaded in GPU)
  /// @{
public:
  const s_layoutGrid &get_layout() const { return wlayout; }
  glm::vec4 get_zone() { compute_layout(); return glm::vec4(m_zone.x, m_zone.y, m_zone.x + m_zone.z, m_zone.y + m_zone.w); } ///< Returns (xmin, ymin, xmax, ymax)
  /// @}

  /// @name self-implementation
  /// @{
protected:
  virtual glm::vec2 get_zoneSizeDefault() const override { TRE_FATAL("should not be called"); return glm::vec2(0); }
  virtual void compute_data() override;
  virtual void acceptEvent(s_eventIntern &event) override;
  virtual void animate(float dt) override;
  void clear();
private:
  void compute_adressPlage(); ///< set adress-plage, and resize the parts in the m_model
  void compute_layout(); ///< compute and set zone of widgets
  s_layoutGrid wlayout;
  std::array<widget*, 2> wOwnWidgets;
  union { glm::mat3 m3; glm::mat4 m4; } wmatPrev; ///< On move, store the origin matrix
  bool m_isMoved = false;
  /// @}


  /// @name interface with widgets
  /// @{
public:
  widget* get_widget(uint row, uint col) { return wlayout.m_cells[wlayout.index(row, col)].m_widget; }
  const widget* get_widget(uint row, uint col) const { return wlayout.m_cells[wlayout.index(row, col)].m_widget; }
  void set_widget(widget * w, uint row, uint col, const uint span_row = 1, const uint span_col = 1); ///< the window class takes the ownership of the widget. Cannot be called after loadIntoGPU call.

#define window_DECLAREWIDGETHELPER(wname) \
  wname* get_##wname(uint row, uint col) { return static_cast<wname*>(get_widget(row, col)); } \
  const wname* get_##wname(uint row, uint col) const { return static_cast<const wname*>(get_widget(row, col)); } \
  wname* create_##wname(uint row, uint col, const uint span_row = 1, const uint span_col = 1) \
  { \
    wname * neww = new wname(); \
    set_widget(neww, row, col, span_row, span_col); \
    return neww; \
  }

  window_DECLAREWIDGETHELPER(widgetText)
  window_DECLAREWIDGETHELPER(widgetTextTranslatate)
  window_DECLAREWIDGETHELPER(widgetTextEdit)
  window_DECLAREWIDGETHELPER(widgetPicture)
  window_DECLAREWIDGETHELPER(widgetBar)
  window_DECLAREWIDGETHELPER(widgetBarZero)
  window_DECLAREWIDGETHELPER(widgetSlider)
  window_DECLAREWIDGETHELPER(widgetBoxCheck)
  window_DECLAREWIDGETHELPER(widgetLineChoice)

#undef window_DECLAREWIDGETHELPER

  widgetText* get_selfwidget_Title() const { return static_cast<widgetText*>(wOwnWidgets[0]); }
  widget*     get_selfwidget_CloseBox() const { return wOwnWidgets[1]; }

  void set_selfwidget_Title(widgetText * w); ///< widget of the top-bar (the widget must inherit from "widgetText")
  void set_selfwidget_CloseBox(widget * w);
  /// @}

  /// @name parent-hood
  /// @{
public:
  virtual window*   get_parentWindow() const override { return const_cast<window*>(this); }
  virtual baseUI*   get_parentUI() const override { return m_parentUI; }
  virtual float     resolve_colorModifier() const override { return (wisactive ? (wishighlighted ? 1.f : -0.3f) : 0.f); }
private:
  baseUI * m_parentUI;
  /// @}

friend class tre::baseUI;
friend class tre::baseUI2D;
friend class tre::baseUI3D;
};

} // namespace ui

// declare baseUI =============================================================

/**
 * @brief The baseUI class
 * It handles User-Interface.
 */
class baseUI
{
public:
  baseUI() { eventState.mousePosPrev = glm::vec3(-20.f,-20.f,0.f); m_textures.fill(nullptr); }
  virtual ~baseUI() { TRE_ASSERT(windowsList.empty()); TRE_ASSERT(m_shader == nullptr); TRE_ASSERT(m_textureWhite.m_handle == 0); }

  /// @name global settings
  /// @{
public:
  void set_defaultFont(const font * pfont) { m_defaultFont = pfont; } ///< The baseUI does not take ownership of the font.
  const font * get_defaultFont() const { return m_defaultFont; }
  virtual uint get_dimension() const = 0;
  void animate(float dt);
  void clear(); ///< Clear all RAM data (without clearing VRAM)
protected:
  const font* m_defaultFont = nullptr; ///< Font used for text rendering. The baseUI does not take ownership of the font.
  ui::s_eventIntern eventState; ///< Event
  /// @}

  /// @name builtin language support
  /// @{
public:
  void set_language(std::size_t lid);
  std::size_t get_language() const { return m_language; }
protected:
  std::size_t m_language = 0;
  /// @}

  /// @name textures
  /// @{
public:
  static const std::size_t s_textureSlotsCount = 4;
  std::size_t    addTexture(const texture *t); ///< Add a new texture. Returns the slot id. The baseUI does not take ownership of the texture.
  const texture *getTexture(uint id) const {  return (id < s_textureSlotsCount) ? m_textures[id] : nullptr; }
  std::size_t    getTextureSlot(const texture *t) const;
protected:
  std::array<const texture*, s_textureSlotsCount> m_textures; ///< Textures.
  texture                                         m_textureWhite;
  /// @}

  /// @name window
  /// @{
public:
  ui::window* create_window(); ///< Create new window, owned by baseUI. Cannot be called after "loadIntoGPU"
protected:
  std::vector<ui::window*> windowsList; ///< List of windows; this class has the ownership of all windows.
  /// @}

  /// @name GPU interface
  /// @{
public:
  virtual void draw() const = 0; ///< Bind the shader and the resources, Emit draw-calls

  bool loadIntoGPU(); ///< Create GPU handlers and load data into GPU
  void updateIntoGPU(); ///< Update data into GPU
  void clearGPU(); ///< Clean GPU handlers

  modelRaw2D    & getDrawModel () { return m_model;  }

  virtual bool loadShader(shader *shaderToUse = nullptr) = 0;
  void clearShader();

protected:
  void createData(); ///< Create the objects
  void updateData(); ///< Update RAM data, that will be synchronized on the GPU

  modelRaw2D    m_model; ///< main mesh-model: part0: solid-boxes, part1: solid-lines, part2: pictures, part[3-...]: pictures(texts)

  shader       *m_shader = nullptr; ///< Global shader for solid drawing
  bool          m_shaderOwner = true;
  /// @}

};

/**
 * @brief The baseUI2D class
 * It handles User-Interface in 2D-projection
 */
class baseUI2D : public baseUI
{
public:
  baseUI2D() : baseUI(), m_PV(1.f), m_viewport(1.f), m_PVinv(1.f) {}
  virtual ~baseUI2D() override {}

  virtual uint get_dimension() const override { return 2; }

  virtual void draw() const override;

  /// @name Events
  /// @{
public:
  void updateCameraInfo(const glm::mat3 &mProjView, const glm::ivec2 &screenSize);
  bool acceptEvent(const SDL_Event &event);
  bool acceptEvent(glm::ivec2 mousePosition, bool mouseLEFT, bool mouseRIGHT);
  glm::vec2 projectWindowPointFromScreen(const glm::vec2 &pixelCoords, const glm::mat3 &mModelWindow, bool isPosition = true) const; ///< Project a screen position to the window's frame coordinate.
  glm::vec2 projectWindowPointToScreen(const glm::vec2 &windowCoords, const glm::mat3 &mModelWindow, bool isPosition = true) const; ///< Project a window's frame point to the screen position.
protected:
  glm::mat3 m_PV;
  glm::vec2 m_viewport;
  glm::mat3 m_PVinv;
  /// @}

public:
  virtual bool loadShader(shader *shaderToUse = nullptr) override;
};

/**
 * @brief The baseUI3D class
 * It handles User-Interface in 3D-projection
 */
class baseUI3D : public baseUI
{
public:
  baseUI3D() : baseUI(), m_PV(1.f), m_viewport(1.f), m_cameraPosition(0.f), m_PVinv(1.f) {}
  virtual ~baseUI3D() override {}

  virtual uint get_dimension() const override { return 3; }

  virtual void draw() const override;

  /// @name Events
public:
  void updateCameraInfo(const glm::mat4 &mProj, const glm::mat4 &mView, const glm::ivec2 &screenSize);
  bool acceptEvent(const SDL_Event &event);
  bool acceptEvent(glm::ivec2 mousePosition, bool mouseLEFT, bool mouseRIGHT);
  glm::vec3 projectWindowPointFromScreen(const glm::vec2 &pixelCoords, const glm::mat4 &mModelWindow) const; ///< Project a screen position to the window's frame coordinate.
  glm::vec2 projectWindowPointToScreen(const glm::vec3 &windowCoords, const glm::mat4 &mModelWindow) const; ///< Project a window's frame point to the screen position.
  const glm::mat4 &get_matPV() const { return m_PV; } ///< Getter for the Proj-View matrix (Prefer to use project** methods)
  const glm::vec2 &get_viewport() const { return m_viewport; } ///< Getter for the Proj-View matrix (Prefer to use project** methods)
protected:
  glm::mat4 m_PV;
  glm::vec2 m_viewport;
  glm::vec3 m_cameraPosition;
  glm::mat4 m_PVinv;
  /// @}

public:
  virtual bool loadShader(shader *shaderToUse = nullptr) override;
};

} // namespace tre

#endif // UI_H
