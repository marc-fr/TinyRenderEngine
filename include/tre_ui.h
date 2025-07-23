#ifndef UI_H
#define UI_H

#include "tre_utils.h"
#include "tre_model.h"
#include "tre_texture.h"

#include <vector>
#include <string>
#include <functional>

namespace tre {

class shader;
class font;

class baseUI;
class baseUI2D;
class baseUI3D;

namespace ui {

class widget;
class window;

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

/// Color modes for color modifiers
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
  glm::vec4 m_colorBackground = glm::vec4(0.f);   ///< background (0dp elevation)
  glm::vec4 m_colorSurface = glm::vec4(0.2f, 0.2f, 0.2f, 0.5f); ///< surface (1dp elevation)
  glm::vec4 m_colorPrimary = glm::vec4(0.4f, 0.4f, 0.4f, 0.7f);  ///< plain color for objects

  glm::vec4 m_colorOnSurface = glm::vec4(1.f); ///< text/line on background or surface
  glm::vec4 m_colorOnObject = glm::vec4(1.f);  ///< text/line on objects

  float            factor = 1.f;

  bool operator!=(const s_colorTheme &) const { return true; }

  glm::vec4 resolveColor(const glm::vec4 &baseColor, float modifier) const { return transformColor(baseColor, COLORTHEME_LIGHTNESS, modifier * factor); }
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

  s_size m_totalHeight, m_totalWidth; ///< Total size (given by the user)

  void set_dimension(uint rows, uint cols);
  uint index(uint iRow, uint iCol) const { TRE_ASSERT(iRow < m_dimension.y); TRE_ASSERT(iCol < m_dimension.x); return iCol + iRow * m_dimension.x; }
  glm::vec2 computeWidgetZones(const ui::window &win, const glm::vec2 &offset = glm::vec2(0.f)); ///< Computes the zone assigned to each widget. Returns the global size.
  void clear();
};

// Other layout ?

// Geometry helpers ===========================================================

void fillNull(glm::vec4 * __restrict &buffer, const unsigned count);
void fillRect(glm::vec4 * __restrict &buffer, const glm::vec4 &AxyBxy, const glm::vec4 &color, const glm::vec4 &AuvBuv = glm::vec4(0.f));
void fillLine(glm::vec4 * __restrict &buffer, const glm::vec2 &p1, const glm::vec2 &p2, const glm::vec4 &color);

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
    wname * set_##aname(const atype & a_##aname) { if (w##aname != a_##aname) { setUpdateNeeded##updateFlag(); w##aname = a_##aname; } return this; } \
  protected :  \
    atype w##aname ainit;

// -------------------------------------------------------------

class widget
{
public:
  widget() {}
  virtual ~widget() {}

  /// @name attributes
  /// @{
  widget_DECLAREATTRIBUTE(widget,glm::vec4,color,= glm::vec4(-1.f), Data) ///< main color overwrite
  widget_DECLAREATTRIBUTE(widget,bool,isactive,= false, Data) ///< When true, the widget receives events and can trigger callbacks
  widget_DECLAREATTRIBUTE(widget,bool,iseditable,= false, Data) ///< When true, the widget is editable from events
  widget_DECLAREATTRIBUTE(widget,bool,ishighlighted, = false, Data) ///< read & write attribute: "acceptEvent" modifies the value; user can change the value, but no callback will be triggered in this case.
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
  struct s_drawElementCount
  {
    unsigned m_vcountSolid = 0; ///< Return nbr of vertices needed for solid drawing (triangles)
    unsigned m_vcountLine = 0;  ///< Return nbr of vertices needed for solid drawing (lines)
    unsigned m_vcountPict = 0;  ///< Return nbr of vertices needed for textured drawing
    unsigned m_vcountText = 0;  ///< Return nbr of vertices needed for text drawing
    unsigned m_textureSlot = -1; ///< Return texture for textured-drawing (a widget cannot use more than 1 texture slot, excluding the font of the text)

    s_drawElementCount operator+=(const s_drawElementCount &other)
    {
      m_vcountSolid += other.m_vcountSolid;
      m_vcountLine += other.m_vcountLine;
      m_vcountPict += other.m_vcountPict;
      m_vcountText += other.m_vcountText;
      return *this;
    }
  };
  virtual s_drawElementCount get_drawElementCount() const = 0;
  virtual glm::vec2 get_zoneSizeDefault() const = 0; ///< Return the default widget size (in the window's space).
  virtual void compute_data() = 0; ///< fill the drawing buffers
  virtual void acceptEvent(s_eventIntern &event) { acceptEventBase_focus(event); acceptEventBase_click(event); }
  virtual void animate(float dt) { if (wcb_animate != nullptr) wcb_animate(this, dt); }
  /// @}

  /// @name helpers
  /// @{
public:
  bool getIsOverPosition(const glm::vec3 & position) const { return (m_zone.x <= position.x) && (m_zone.z >= position.x) && (m_zone.y <= position.y) && (m_zone.w >= position.y); } ///< Return true if the position is under the m_zone. Local space position. z is ignored.
  widget* set_colorAlpha(float alpha) { if (alpha != wcolor.a) { wcolor.a = alpha; setUpdateNeededData(); } return this; }
  void setUpdateNeededAddress() const;
  void setUpdateNeededLayout() const;
  void setUpdateNeededData() const;
  /// @}

  /// @name intern helpers
  /// @{
protected:
  void acceptEventBase_focus(s_eventIntern &event); ///< trigger focus-state
  void acceptEventBase_click(s_eventIntern &event); ///< trigger simple click-callbacks (warning: event can be modified)
  void set_zone(const glm::vec4 &assignedZone) { if (m_zone != assignedZone) { m_zone = assignedZone; setUpdateNeededData(); } }

  glm::vec4  m_zone = glm::vec4(0.f); ///< Widget zone (xmin,ymin,xmax,ymax)
  window *m_parentWindow = nullptr;
  struct s_objAddress
  {
    uint part = 0u;
    uint offset = 0u;
  };
  mutable s_objAddress m_adSolid, m_adrLine, m_adrPict, m_adrText; ///< Address plage for each specific object.
  glm::vec4 * __restrict getDrawBuffer_Solid() const;
  glm::vec4 * __restrict getDrawBuffer_Line() const;
  glm::vec4 * __restrict getDrawBuffer_Pict() const;
  glm::vec4 * __restrict getDrawBuffer_Text() const;
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
    virtual s_drawElementCount get_drawElementCount() const override; \
    virtual glm::vec2 get_zoneSizeDefault() const override; \
    virtual void compute_data() override; \
    virtual void acceptEvent(s_eventIntern &event) override;


class widgetText : public widget
{
  widget_DECLARECONSTRUCTORS(widgetText)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetText,std::string,text,= "", Address) ///< Text to be drawn. Note: it makes a local copy of the text (safe but copy data).
  widget_DECLAREATTRIBUTE(widgetText,float,fontsizeModifier,= 1.f, Layout) ///< Font-size factor in regards with the parent window's font-size.
  widget_DECLAREATTRIBUTE(widgetText,bool,withborder,= false, Address) ///< True if a border will be drawn
  widget_DECLAREATTRIBUTE(widgetText,glm::vec4,colorBackground,= glm::vec4(-1.f), Data) ///< Overwrite the background color

  glm::vec4 resolveColorFront() const;
  glm::vec4 resolveColorBack() const;
};

class widgetTextTranslatate : public widgetText
{
public:
  widgetTextTranslatate() : widgetText() {}
  virtual ~widgetTextTranslatate() {}

protected:
  virtual s_drawElementCount get_drawElementCount() const override;
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

  virtual s_drawElementCount get_drawElementCount() const override;
  virtual glm::vec2 get_zoneSizeDefault() const override;
  virtual void compute_data() override;
  virtual void acceptEvent(s_eventIntern &event) override;
  virtual void animate(float dt) override;

  widget_DECLAREATTRIBUTE(widgetTextEdit,bool,allowMultiLines,= false, Layout)
  widget_DECLAREATTRIBUTE(widgetTextEdit,float,cursorAnimSpeed,= 1.f, Data)

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
  widget_DECLAREATTRIBUTE(widgetPicture,uint,texId,= uint(-1), Address)
  widget_DECLAREATTRIBUTE(widgetPicture,glm::vec4,texUV,= glm::vec4(0.f,0.f,1.f,1.f), Layout)
  widget_DECLAREATTRIBUTE(widgetPicture,float,heightModifier,= 1.f, Layout) ///< height factor in regards with the default cell's height.
  widget_DECLAREATTRIBUTE(widgetPicture,bool,snapPixels, = false, Data) ///< snap pixels to entire zoom factor

  glm::vec4 resolveColorFill() const;
};

class widgetBar : public widget
{
  widget_DECLARECONSTRUCTORS(widgetBar)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetBar,float,valuemin,= 0.f, Data)
  widget_DECLAREATTRIBUTE(widgetBar,float,valuemax,= 1.f, Data)
  widget_DECLAREATTRIBUTE(widgetBar,float,value,= 0.f, Data)
  widget_DECLAREATTRIBUTE(widgetBar,bool,withborder,= true, Address) ///< True if a border must to be drawn
  widget_DECLAREATTRIBUTE(widgetBar,bool,withthreshold,= false, Address)
  widget_DECLAREATTRIBUTE(widgetBar,float,valuethreshold,= 1.f, Data)
  widget_DECLAREATTRIBUTE(widgetBar,bool,withtext,= false, Address)
  widget_DECLAREATTRIBUTE(widgetBar,float,snapInterval, = 0.f, Data) ///< Negative or zero value means no snapping
  widget_DECLAREATTRIBUTE(widgetBar,float,widthFactor, = 5.f, Data) ///< Width/Height ratio

  glm::vec4 resolveColorPlain() const;
  glm::vec4 resolveColorFill() const;
  glm::vec4 resolveColorOutLine() const;
  glm::vec4 resolveColorText() const;

public:
  std::function<std::string(float value)>  wcb_valuePrinter;
};

class widgetBarZero : public widgetBar
{
public:
  widgetBarZero() : widgetBar() { wvaluemin = -1.f; }
  virtual ~widgetBarZero() {}

  virtual s_drawElementCount get_drawElementCount() const override;
  virtual void compute_data();
};

class widgetSlider : public widget
{
  widget_DECLARECONSTRUCTORS(widgetSlider)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetSlider,float,valuemin,= 0.f, Data)
  widget_DECLAREATTRIBUTE(widgetSlider,float,valuemax,= 1.f, Data)
  widget_DECLAREATTRIBUTE(widgetSlider,float,value,= 0.f, Data)
  widget_DECLAREATTRIBUTE(widgetSlider,float,widthFactor, = 5.f, Data) ///< Width/Height ratio
};

class widgetSliderInt : public widget
{
  widget_DECLARECONSTRUCTORS(widgetSliderInt)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetSliderInt, int, valuemin, = 0, Data)
  widget_DECLAREATTRIBUTE(widgetSliderInt, int, valuemax, = 10, Data)
  widget_DECLAREATTRIBUTE(widgetSliderInt, int, value, = 0, Data)
  widget_DECLAREATTRIBUTE(widgetSliderInt, float, widthFactor, = 5.f, Data) ///< Width/Height ratio
};

class widgetBoxCheck : public widget
{
  widget_DECLARECONSTRUCTORS(widgetBoxCheck)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetBoxCheck,bool,value,= false, Data)
  widget_DECLAREATTRIBUTE(widgetBoxCheck,float,margin,= 0.15f, Data)
  widget_DECLAREATTRIBUTE(widgetBoxCheck,float,thin,= 0.15f, Data)
};

class widgetLineChoice : public widget
{
  widget_DECLARECONSTRUCTORS(widgetLineChoice)
  widget_DECLARECOMMUNMETHODS()
  widget_DECLAREATTRIBUTE(widgetLineChoice,std::vector<std::string>,values,, Address)
  widget_DECLAREATTRIBUTE(widgetLineChoice,uint,selectedIndex,= 0, Data)
  widget_DECLAREATTRIBUTE(widgetLineChoice,bool,cyclic,= false, Data)

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

class window
{
protected:
  window(tre::baseUI * parent) : m_parentUI(parent) { wOwnWidgets.fill(nullptr); }
  ~window() { clear(); }

  /// @name global properties
  /// @{

public:
  void set_isactiveWindow(bool a_active); ///< like set_isactive(), but trigger a dummy event when the window get inactived.
  void set_isvisibleWindow(bool a_visible); ///< like set_isvisible(), but a dummy event is triggered.
protected:
  bool whasFocus = false;

#define window_PROPERTY(atype,aname,ainit,updateFlag) \
  public: \
    const atype &get_##aname() const { return w##aname; } \
    void  set_##aname(atype a_##aname) { updateFlag |= (w##aname != a_##aname); w##aname = a_##aname; } \
  protected: \
    atype w##aname = ainit;

  window_PROPERTY(bool,isvisible, true, m_isUpdateNeededDummy);
  window_PROPERTY(bool,isactive, true, m_isUpdateNeededDummy);
  window_PROPERTY(s_size, fontSize, s_size(16, SIZE_PIXEL), m_isUpdateNeededLayout)
  window_PROPERTY(glm::vec4, colormask, glm::vec4(1.f), m_isUpdateNeededData)
  window_PROPERTY(s_colorTheme, colortheme, s_colorTheme(), m_isUpdateNeededData)
  window_PROPERTY(uint, alignMask, ALIGN_MASK_LEFT_TOP, m_isUpdateNeededData)

  window_PROPERTY(glm::mat4, mat4, glm::mat4(1.f), m_isUpdateNeededLayout) // used for 2D-ui
  window_PROPERTY(glm::mat3, mat3, glm::mat3(1.f), m_isUpdateNeededLayout) // used for 3D-ui

#undef window_PROPERTY

public:
  window* set_topbar(const std::string &name, bool canBeMoved, bool canBeClosed);
  window* set_topbarName(const std::string &name); ///< rename the top-bar title (only after "set_topbar" call)
  window* set_transparency(const float alpha) { m_isUpdateNeededData |= (wcolormask.w != alpha); wcolormask.w = alpha; return this; } ///< set the global transparency

public:
  void set_layoutGrid(uint row, uint col) { m_isUpdateNeededAddress = true; wlayout.set_dimension(row, col); } ///< set the layout.
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
  void compute_adressPlage();
  void compute_data();
  void acceptEvent(s_eventIntern &event);
  void animate(float dt);
  void clear();
  bool getIsOverPosition(const glm::vec3 & position) const;
  void acceptEventBase_focus(s_eventIntern &event);
public:
  void setUpdateNeededAddress() { m_isUpdateNeededAddress = true; } ///< object's Address update is needed (nbr of vertice changed, ...)
  void setUpdateNeededLayout() { m_isUpdateNeededLayout = true; } ///< layout update is needed (widget gets resized, ..., typically when get_zoneSizeDefault() will return a new different value)
  void setUpdateNeededData() { m_isUpdateNeededData = true; }   ///< vertice-data will changed (positions, color, ...)
private:
  void compute_AddressPlage(); ///< set Address-plage, and resize the parts in the m_model
  void compute_layout(); ///< compute and set zone of widgets
  glm::vec4 m_zone = glm::vec4(0.f);
  s_layoutGrid wlayout;
  std::array<widget*, 2> wOwnWidgets;
  union { glm::mat3 m3; glm::mat4 m4; } wmatPrev; ///< On move, store the origin matrix
  bool m_isMoved = false;

  struct s_objAddress
  {
    uint part = 0u;
    uint offset = 0u;
  };
  s_objAddress m_adSolid, m_adrLine, m_adrPict, m_adrText; ///< Address plage for all objects in the window
  /// @}

  bool m_isUpdateNeededAddress = true; ///< true when an object's Address update is needed (nbr of vertice changed, ...)
  bool m_isUpdateNeededLayout = true; ///< true when a layout update is needed (widget gets resized, ..., typically when get_zoneSizeDefault() will return a new different value)
  bool m_isUpdateNeededData = true;   ///< true when the vertice-data will changed (positions, color, ...)
  bool m_isUpdateNeededDummy = true;

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
  window_DECLAREWIDGETHELPER(widgetSliderInt)
  window_DECLAREWIDGETHELPER(widgetBoxCheck)
  window_DECLAREWIDGETHELPER(widgetLineChoice)

#undef window_DECLAREWIDGETHELPER

  widgetText* get_selfwidget_Title() const { return static_cast<widgetText*>(wOwnWidgets[0]); }
  widget*     get_selfwidget_CloseBox() const { return wOwnWidgets[1]; }

  void set_selfwidget_Title(widgetText * w); ///< widget of the top-bar (the widget must inherit from "widgetText")
  void set_selfwidget_CloseBox(widget * w);
  /// @}

  /// @name callbacks
  /// @{
public:
  std::function<void(window *)> wcb_gain_focus = nullptr; ///< Triggered when the widget gain focus. No need to set hasfocus = true (done internally).
  std::function<void(window *)> wcb_loss_focus = nullptr; ///< Triggered when the widget gain focus. No need to set hasfocus = false (done internally).
  std::function<void(window *, float)> wcb_animate = nullptr; ///< Called when the root "animate" method is called.
  /// @}


  /// @name parent-hood
  /// @{
public:
  baseUI*   get_parentUI() const { return m_parentUI; }
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

  modelRaw2D & getDrawModel() { return m_model; }
  glm::vec4 * __restrict getDrawBuffer(const unsigned offset = 0) const { return reinterpret_cast<glm::vec4 *>(m_model.layout().m_positions.m_data + (8 * offset)); }

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
