#ifndef WINDOWHELPER_DEFINED
#define WINDOWHELPER_DEFINED

#include "openglinclude.h"
#include "utils.h"

namespace tre {

/**
 * @brief The windowHelper class
 *
 * This is a collection of helpers, related to common I/O control of a SDL application.
 * It provides:
 * - event wrapper on the window's event (resize, close, ...)
 * - event wrapper on common application's events (mouse, keyboard, ...)
 * - basic timer (time control over a frame)
 * - event wrapper on views (camera events with the mouse or the keyboard, either 2D and 3D)
 *
 */
class windowHelper
{
public:
  windowHelper() : m_view2D(this), m_view3D(this) {}
  ~windowHelper() { TRE_ASSERT(m_window == nullptr); TRE_ASSERT(m_glContext == nullptr); }

  // SDL

  SDL_Window      *m_window = nullptr;
  SDL_DisplayMode m_displayModeWindow;
  bool            m_window_isfullscreen = false;

  bool SDLInit(Uint32 sdl_init_flags, const char * windowname, Uint32 sdl_window_flags = 0, int gl_depth_bits = 24);

  bool SDLEvent_onWindow(const SDL_Event &event); ///< Process events

  void SDLToggleFullScreen();

  void SDLQuit();

  // SDL_Image

  bool SDLImageInit(int sdl_image_flags);

  void SDLImageQuit();

  // OpenGL

  SDL_GLContext m_glContext = nullptr;
  glm::ivec2      m_resolutioncurrent = glm::ivec2(1);
  glm::vec2       m_resolutioncurrentInv = glm::vec2(1.f);
  float           m_near = 0.01f;
  float           m_far = 100.f;
  float           m_fov = 70.f;
  glm::mat3       m_matProjection2D;
  glm::mat4       m_matProjection3D;

  bool OpenGLInit();

  void OpenGLCamera(float near, float far, float fov);

  void OpenGLResize(const int width, const int height);

  void OpenGLQuit();

  // Controls

  /**
   * @brief The s_controls struct
   */
  struct s_controls
  {
    static const int MASK_BUTTON_PRESSED  = 0x01; ///< button pressed. Kept on the next frame (this is drag).
    static const int MASK_BUTTON_RELEASED = 0x02; ///< button released. Lost on the next frame (this is drop).
    glm::ivec3 m_mouse = glm::ivec3(0);           ///< current viewport-space position (x,y) and delta-wheel(z)
    glm::ivec3 m_mousePrev = glm::ivec3(0);       ///< mouse position when a button was pressed.
    int m_mouseLEFT = 0x00, m_mouseRIGHT = 0x00; // 8bits mask (0x**): handle drag & drop
    bool m_keySHIFT = false, m_keyCTRL = false, m_keyALT = false;
    bool m_keyUP = false, m_keyDOWN = false, m_keyLEFT = false, m_keyRIGHT = false;
    bool m_pause = false, m_quit = false, m_home = false;
    bool m_viewportResized = false;               ///< automatically set to true in OpenGLResize() for "m_controls"
    bool m_hasFocus = true;                       ///< automatically updated by SDLEvent_onWindow() for "m_controls"

    void newFrame()
    {
      m_mouse.z = 0;
      m_mouseLEFT  &= ~s_controls::MASK_BUTTON_RELEASED;
      m_mouseRIGHT &= ~s_controls::MASK_BUTTON_RELEASED;
      m_viewportResized = false;
    }

    bool treatSDLEvent(const SDL_Event &event);
  };

  s_controls m_controls;

  // Basic timer

  /**
   * @brief The s_timer struct
   */
  struct s_timer
  {
    float worktime_average = 0.f;
    float worktime = 0;

    float frametime_average = 0.f;
    float frametime = 0.f;
    Uint32 oldtime;

    float scenetime = 0.f;

    uint ndata = 0;

    void initialize();
    void newFrame(const uint waitForFPS /* = 0 means no wait */, const bool isSimPaused);
    void endFrame_beforeGPUPresent(); // optionnal (needed to distinguish the work-time and the frame-time)
  };

  s_timer m_timing;

  // Views (cameras)

  /**
   * @brief The s_view2D struct
   */
  struct s_view2D
  {
    glm::mat3   m_matView = glm::mat3(1.f);
    glm::mat3   m_matViewPrev;

    windowHelper *m_parentWindow = nullptr;

    bool        m_keyBound = false;      ///< the view reacts on keyboard actions.
    glm::vec3   m_keySensitivity = glm::vec3(0.2f);
    bool        m_mouseBound = false;    ///< the mouse cursor is bound to a point, so the view is following the opposite motion of the mouse.
    bool        m_mouseBoundPrev = true;
    bool        m_mouveOnBounds = false; ///< when the mouse goes to the screen bounds, the view is moving.
    glm::vec3   m_mouseSensitivity = glm::vec3(0.2f); ///< motion (x,y) + zoom (with wheel)

    glm::vec2   m_mousePrev = glm::vec2(0.f); ///< (x,y)_viewSpace

    s_view2D(windowHelper *parentWindow) : m_parentWindow(parentWindow) { TRE_ASSERT(m_parentWindow != nullptr); }

    void setKeyBinding(bool withKeyboard) { m_keyBound = withKeyboard; }
    void setMouseBinding(bool isBound) { m_mouseBound = isBound; }
    void setScreenBoundsMotion(bool isMoving) { m_mouveOnBounds = isMoving; m_mouseBound &= (!isMoving); }

    void treatControlEvent(const s_controls &control, const float dt);
  };

  s_view2D m_view2D;

  /**
   * @brief The s_view3D struct
   */
  struct s_view3D
  {
    glm::mat4   m_matView = glm::mat4(1.f);
    glm::mat4   m_matViewPrev;

    windowHelper *m_parentWindow = nullptr;

    bool        m_keyBound = false;      ///< the view reacts on keyboard actions.
    glm::vec3   m_keySensitivity = glm::vec3(4.f);
    bool        m_mouseBound = false;    ///< the mouse cursor is bound to a point, so the view is following the opposite motion of the mouse.
    bool        m_mouseBoundPrev = true;
    bool        m_mouveOnBounds = false; ///< when the mouse goes to the screen bounds, the view is moving.
    glm::vec4   m_mouseSensitivity = glm::vec4(4.f, 4.f, 4.f, 12.f); ///< motion (x,y,z) + wheel

    glm::vec2   m_mousePrev = glm::vec2(0.f); ///< (x,y)_clipSpace

    s_view3D(windowHelper *parentWindow) : m_parentWindow(parentWindow) { TRE_ASSERT(m_parentWindow != nullptr); }

    void setKeyBinding(bool withKeyboard) { m_keyBound = withKeyboard; }
    void setMouseBinding(bool isBound) { m_mouseBound = isBound; }
    void setScreenBoundsMotion(bool isMoving) { m_mouveOnBounds = isMoving; m_mouseBound &= (!isMoving); }

    void setMouseMotionMode();

    void treatControlEvent(const s_controls &control, const float dt);
  };

  s_view3D m_view3D;
};

} // namespace

#endif
