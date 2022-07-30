#include "windowHelper.h"

#include <iostream>

namespace tre {

// Root =======================================================================

bool windowHelper::SDLInit(Uint32 sdl_init_flags, const char * windowname, Uint32 sdl_window_flags, int gl_depth_bits)
{
  // Init SDL2
  if(SDL_Init(sdl_init_flags) < 0)
  {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Initialization failed",
                             "Failed to initialize SDL.",
                             nullptr);
    return false;
  }
  // Set OpenGL version
#ifdef TRE_OPENGL_ES
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#endif
  // Double Buffer
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE  , gl_depth_bits);
  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  m_displayModeWindow.w = int(currentdm.w * 0.8 / 8)*8;
  m_displayModeWindow.h = int(currentdm.h * 0.8 / 8)*8;
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);
  TRE_LOG("SDL Window resolution : " << m_displayModeWindow.w << " * " << m_displayModeWindow.h);
  // Create Window
  m_window = SDL_CreateWindow(windowname,
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             m_displayModeWindow.w, m_displayModeWindow.h, SDL_WINDOW_OPENGL | sdl_window_flags);
  if (!m_window)
  {
    SDL_Log("Couldn't create window: %s",SDL_GetError());
    return false;
  }
  // Misc
  SDL_StopTextInput();
  // End
  return true;
}

// ----------------------------------------------------------------------------

bool windowHelper::SDLEvent_onWindow(const SDL_Event & event)
{
  switch(event.type)
  {
  case SDL_KEYDOWN:
    if (SDL_IsTextInputActive() == SDL_FALSE && event.key.keysym.sym == SDLK_f)
    {
      SDLToggleFullScreen();
      return true;
    }
    break;
  case SDL_WINDOWEVENT:
    // SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
    // if (win == (SDL_Window*)data) ...
    if      (event.window.event == SDL_WINDOWEVENT_CLOSE)
    {
      m_controls.m_quit = true;
      return true;
    }
    else if (event.window.event == SDL_WINDOWEVENT_RESIZED)
    {
      if (m_window_isfullscreen == false)
      {
        TRE_LOG("Window resized (" << event.window.data1 << " * " << event.window.data2 << ")");
        m_displayModeWindow.w = event.window.data1;
        m_displayModeWindow.h = event.window.data2;
        OpenGLResize(event.window.data1, event.window.data2);
        return true;
      }
    }
    else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
    {
      m_controls.m_hasFocus = false;
    }
    else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
    {
      m_controls.m_hasFocus = true;
    }
    break;
  }
  return false;
}

// ----------------------------------------------------------------------------

void windowHelper::SDLToggleFullScreen()
{
  if (m_window_isfullscreen==true)
  {
    SDL_SetWindowFullscreen(m_window,0);
    OpenGLResize(m_displayModeWindow.w,m_displayModeWindow.h);
    m_window_isfullscreen = false;
    TRE_LOG("Out of full-screen (" << m_displayModeWindow.w << " * " << m_displayModeWindow.h << ")");
  }
  else
  {
    SDL_SetWindowFullscreen(m_window,SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_DisplayMode currentdm;
    SDL_GetWindowDisplayMode(m_window,&currentdm);
    OpenGLResize(currentdm.w,currentdm.h);
    m_window_isfullscreen = true;
    TRE_LOG("Go full-screen (" << currentdm.w << " * " << currentdm.h << ")");
  }
}

// ----------------------------------------------------------------------------

void windowHelper::SDLQuit()
{
  const char * mysdlerror = SDL_GetError();
   if (*mysdlerror != '\0') { TRE_LOG(mysdlerror); }

  if (m_window!=nullptr) SDL_DestroyWindow(m_window);
  m_window = nullptr;

  SDL_Quit();
}

// ----------------------------------------------------------------------------

bool windowHelper::SDLImageInit(int sdl_image_flags)
{
#ifdef TRE_WITH_SDL2_IMAGE
  if (IMG_Init(sdl_image_flags) == sdl_image_flags)
    return true;

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                           "Initialization failed",
                           "Failed to initialize asked codecs on SDL_Image.",
                           nullptr);
#endif
  return false;
}

// ----------------------------------------------------------------------------

void windowHelper::SDLImageQuit()
{
#ifdef TRE_WITH_SDL2_IMAGE
  IMG_Quit();
#endif
}

// ----------------------------------------------------------------------------

bool windowHelper::OpenGLInit()
{
  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext)
  {
    const std::string msg = std::string(SDL_GetError()) + std::string("\nA graphics card and driver with OpenGL support is required.\nInstalling the latest driver may resolve the issue.\n\nThe program will close.");
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             "Initialization failed",
                             msg.c_str(),
                             nullptr);
    return false;
  }

#ifdef WIN32
  GLenum glewerrinit = glewInit();
  if (GLEW_OK != glewerrinit)
  {
    TRE_LOG(glewGetErrorString(glewerrinit));
    return false;
  }
#endif

  SDL_GL_SetSwapInterval(1); // Vertical-sync

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#if !defined(TRE_EMSCRIPTEN) && !defined(TRE_OPENGL_ES)
  glEnable(GL_MULTISAMPLE);
#endif

  IsOpenGLok("windowHelper::OpenGLInit");

  OpenGLResize(m_displayModeWindow.w,m_displayModeWindow.h);

  TRE_LOG("OpenGL Vendor: " << glGetString(GL_VENDOR));
  TRE_LOG("OpenGL Renderer: " << glGetString(GL_RENDERER));
  TRE_LOG("OpenGL Version: " << glGetString(GL_VERSION));
#ifdef WIN32
  TRE_LOG("OpenGLEW Version: " << glewGetString(GLEW_VERSION));
#endif

  return true;
}

// ----------------------------------------------------------------------------

void windowHelper::OpenGLCamera(float near, float far, float fov)
{
  m_far = far;
  m_near = near;
  m_fov = fov;

  const float invratio = float(m_resolutioncurrent.y)/float(m_resolutioncurrent.x);
  tre::compute3DFrustumProjection(m_matProjection3D, invratio, m_fov * float(M_PI / 180.), m_near, m_far);
  tre::compute2DOrthoProjection(m_matProjection2D, invratio);
}

// ----------------------------------------------------------------------------

void windowHelper::OpenGLResize(const int width, const int height)
{
  m_resolutioncurrent.x = width;
  m_resolutioncurrent.y = height;

  if(m_resolutioncurrent.x <= 0) m_resolutioncurrent.x = 1;
  if(m_resolutioncurrent.y <= 0) m_resolutioncurrent.y = 1;

  m_resolutioncurrentInv.x = 1.f / m_resolutioncurrent.x;
  m_resolutioncurrentInv.y = 1.f / m_resolutioncurrent.y;

  const float invratio = float(m_resolutioncurrent.y)/float(m_resolutioncurrent.x);
  tre::compute3DFrustumProjection(m_matProjection3D, invratio, m_fov * float(M_PI / 180.), m_near, m_far);
  tre::compute2DOrthoProjection(m_matProjection2D, invratio);

  m_controls.m_viewportResized = true;
}

// ----------------------------------------------------------------------------

void windowHelper::OpenGLQuit()
{
  tre::IsOpenGLok("On quit");
  if (m_glContext) SDL_GL_DeleteContext(m_glContext);
  m_glContext = nullptr;
}

// Controls ===================================================================

bool windowHelper::s_controls::treatSDLEvent(const SDL_Event & event)
{
  switch(event.type)
  {
  case SDL_KEYDOWN:
    if       (event.key.keysym.sym == SDLK_LSHIFT ||
             event.key.keysym.sym == SDLK_RSHIFT  ) m_keySHIFT = true;
    else if (event.key.keysym.sym == SDLK_LCTRL ||
             event.key.keysym.sym == SDLK_RCTRL  ) m_keyCTRL = true;
    else if (event.key.keysym.sym == SDLK_LALT ||
             event.key.keysym.sym == SDLK_RALT  ) m_keyALT = true;
#ifndef TRE_EMSCRIPTEN
    else if (event.key.keysym.sym == SDLK_ESCAPE) m_quit = true;
#endif
    else if (event.key.keysym.sym == SDLK_UP   ||
             event.key.keysym.scancode == SDL_SCANCODE_W /*physical location*/) m_keyUP = true;
    else if (event.key.keysym.sym == SDLK_DOWN ||
             event.key.keysym.scancode == SDL_SCANCODE_S) m_keyDOWN  = true;
    else if (event.key.keysym.sym == SDLK_LEFT ||
             event.key.keysym.scancode == SDL_SCANCODE_A) m_keyLEFT  = true;
    else if (event.key.keysym.sym == SDLK_RIGHT||
             event.key.keysym.scancode == SDL_SCANCODE_D) m_keyRIGHT = true;
    else if (event.key.keysym.sym == SDLK_p) m_pause = ! m_pause;
    else if (event.key.keysym.sym == SDLK_HOME) m_home = true;
    else
      return false;
    return true;
  case SDL_KEYUP:
    if      (event.key.keysym.sym == SDLK_LSHIFT ||
             event.key.keysym.sym == SDLK_RSHIFT  ) m_keySHIFT = false;
    else if (event.key.keysym.sym == SDLK_LCTRL ||
             event.key.keysym.sym == SDLK_RCTRL  ) m_keyCTRL = false;
    else if (event.key.keysym.sym == SDLK_LALT ||
             event.key.keysym.sym == SDLK_RALT  ) m_keyALT = false;
    else if (event.key.keysym.sym == SDLK_UP   ||
             event.key.keysym.scancode == SDL_SCANCODE_W) m_keyUP    = false;
    else if (event.key.keysym.sym == SDLK_DOWN ||
             event.key.keysym.scancode == SDL_SCANCODE_S) m_keyDOWN  = false;
    else if (event.key.keysym.sym == SDLK_LEFT ||
             event.key.keysym.scancode == SDL_SCANCODE_A) m_keyLEFT  = false;
    else if (event.key.keysym.sym == SDLK_RIGHT||
             event.key.keysym.scancode == SDL_SCANCODE_D) m_keyRIGHT = false;
    else if (event.key.keysym.sym == SDLK_HOME) m_home = false;
    else
      return false;
    return true;
  case SDL_MOUSEBUTTONDOWN:
    if      (event.button.button == SDL_BUTTON_LEFT ) m_mouseLEFT  = s_controls::MASK_BUTTON_PRESSED;
    else if (event.button.button == SDL_BUTTON_RIGHT) m_mouseRIGHT = s_controls::MASK_BUTTON_PRESSED;
    m_mouse.x = event.button.x;
    m_mouse.y = event.button.y;
    m_mousePrev = m_mouse;
    return true;
  case SDL_MOUSEBUTTONUP:
    if      (event.button.button == SDL_BUTTON_LEFT ) m_mouseLEFT  = s_controls::MASK_BUTTON_RELEASED;
    else if (event.button.button == SDL_BUTTON_RIGHT) m_mouseRIGHT = s_controls::MASK_BUTTON_RELEASED;
    m_mouse.x = event.button.x;
    m_mouse.y = event.button.y;
    return true;
  case SDL_MOUSEMOTION:
    m_mouse.x = event.button.x;
    m_mouse.y = event.button.y;
    return true;
  case SDL_MOUSEWHEEL:
    m_mouse.z = - event.wheel.y;
    return true;
  default:
    return false;
  }
}

// Timings ====================================================================

void windowHelper::s_timer::newFrame()
{
  oldtime = SDL_GetTicks();
}

// ----------------------------------------------------------------------------

void  windowHelper::s_timer::endFrame_beforeGPUPresent()
{
  const Uint32 newtime = SDL_GetTicks();
  worktime = float(newtime - oldtime) * 1.e-3f;
  worktime_average = 0.9f * worktime_average + 0.1f * worktime;
}

// ----------------------------------------------------------------------------

void windowHelper::s_timer::endFrame(const uint waitForFPS /* = 0 means no wait */, const bool isSimPaused)
{
  const Uint32 newtime = SDL_GetTicks();
  if (waitForFPS > 0)
  {
    const Uint32 dtms = 1000 / waitForFPS;
    if (newtime < oldtime + dtms)
    {
      SDL_Delay(dtms + oldtime - newtime );
    }
    else
    {
      TRE_LOG("The program is lagging !!! " << dtms << " ms was exceeded : " << newtime - oldtime << " ms");
    }
    frametime = dtms * 1.e-3f;
  }
  else
  {
    frametime = float(newtime - oldtime) * 1.e-3f;
  }
  frametime_average = 0.9f * frametime_average + 0.1f * frametime;
  if (!isSimPaused)
    scenetime += frametime;
}

// Camera =====================================================================

void windowHelper::s_view2D::treatControlEvent(const s_controls &control, const float dt)
{
  const glm::vec2 mouseCurr_clipSpace = glm::vec2(-1.f + 2.f * float(control.m_mouse.x) / float(m_parentWindow->m_resolutioncurrent.x),
                                                   1.f - 2.f * float(control.m_mouse.y) / float(m_parentWindow->m_resolutioncurrent.y));

  const glm::vec2 mouseCurr_viewSpace = mouseCurr_clipSpace / glm::vec2(m_parentWindow->m_matProjection2D[0][0], m_parentWindow->m_matProjection2D[1][1]);

  const glm::vec2 mouseCurr_worldSpace = (mouseCurr_viewSpace - glm::vec2(m_matView[2])) / glm::vec2(m_matView[0][0],  m_matView[1][1]);

  if (m_mouseBound)
  {
#ifdef TRE_EMSCRIPTEN
    if (!m_mouseBoundPrev)
    {
      m_mousePrev = mouseCurr_viewSpace;
      m_matViewPrev = m_matView;
    }
    else
    {
      m_matView[2] = m_matViewPrev[2] + glm::vec3(mouseCurr_viewSpace - m_mousePrev, 0.f);
    }
#else
    if (!m_mouseBoundPrev)
    {
      m_mousePrev = glm::vec2(0.f);
      m_matViewPrev = m_matView;
    }
    else
    {
      m_mousePrev -= mouseCurr_viewSpace;
      m_matView[2] = m_matViewPrev[2] + glm::vec3(- m_mousePrev, 0.f);
    }
    SDL_WarpMouseInWindow(m_parentWindow->m_window, m_parentWindow->m_resolutioncurrent.x / 2, m_parentWindow->m_resolutioncurrent.y / 2);
#endif
  }
  else if (m_mouveOnBounds && m_parentWindow->m_window_isfullscreen)
  {
    const float power = dt * (control.m_keySHIFT ? 20.f : 1.f);

    if      (mouseCurr_clipSpace.x < -0.8f)
      m_matView[2].x -= (0.8f + mouseCurr_clipSpace.x) * 5.f * m_mouseSensitivity.x * power;
    else if (mouseCurr_clipSpace.x >  0.8f)
      m_matView[2].x += (0.8f - mouseCurr_clipSpace.x) * 5.f * m_mouseSensitivity.x * power;
    else if (mouseCurr_clipSpace.y < -0.8f)
      m_matView[2].y -= (0.8f + mouseCurr_clipSpace.y) * 5.f * m_mouseSensitivity.y * power;
    else if (mouseCurr_clipSpace.y >  0.8f)
      m_matView[2].y += (0.8f - mouseCurr_clipSpace.y) * 5.f * m_mouseSensitivity.y * power;
  }

#ifndef TRE_EMSCRIPTEN
  if (m_mouseBoundPrev != m_mouseBound)
    SDL_ShowCursor(m_mouseBoundPrev);
#endif

  m_mouseBoundPrev = m_mouseBound;

  if (m_keyBound)
  {
    const float power = dt * (control.m_keySHIFT ? 20.f : 1.f);
    if (control.m_keyLEFT)  m_matView[2].x += m_keySensitivity.x * power;
    if (control.m_keyRIGHT) m_matView[2].x -= m_keySensitivity.x * power;
    if (control.m_keyDOWN)  m_matView[2].y += m_keySensitivity.y * power;
    if (control.m_keyUP)    m_matView[2].y -= m_keySensitivity.y * power;

    if (control.m_home) m_matView = glm::mat3(1.f);
  }

  if (control.m_mouse.z != 0)
  {
    float zoomFactor = glm::max(1.f - m_mouseSensitivity.z * control.m_mouse.z, 1.e-6f);
    if (control.m_keySHIFT)
      zoomFactor = zoomFactor * zoomFactor;

    const float newVX = mouseCurr_viewSpace.x - mouseCurr_worldSpace.x * (m_matView[0].x * zoomFactor);
    const float newVY = mouseCurr_viewSpace.y - mouseCurr_worldSpace.y * (m_matView[1].y * zoomFactor);

    m_matViewPrev[2].x += newVX - m_matView[2].x;
    m_matViewPrev[2].y += newVY - m_matView[2].y;

    m_matView[2].x = newVX;
    m_matView[2].y = newVY;
    m_matView[0] *= zoomFactor;
    m_matView[1] *= zoomFactor;

    m_matViewPrev[0] = m_matView[0]; // useless for now, but keep it
    m_matViewPrev[1] = m_matView[1]; // useless for now, but keep it
  }
}

// ----------------------------------------------------------------------------

void windowHelper::s_view3D::treatControlEvent(const s_controls &control, const float dt)
{
  const glm::vec2 mouseCurr_clipSpace = glm::vec2(-1.f + 2.f * float(control.m_mouse.x) / float(m_parentWindow->m_resolutioncurrent.x),
                                                   1.f - 2.f * float(control.m_mouse.y) / float(m_parentWindow->m_resolutioncurrent.y));

  if (m_mouseBound)
  {
#ifdef TRE_EMSCRIPTEN
    if (!m_mouseBoundPrev)
    {
      m_mousePrev = mouseCurr_clipSpace;
      m_matViewPrev = m_matView;
    }
    else
    {
      const glm::vec2 deltaMouse = mouseCurr_clipSpace - m_mousePrev;
#else
    if (!m_mouseBoundPrev)
    {
      m_mousePrev = glm::vec2(0.f);
      m_matViewPrev = m_matView;
    }
    else
    {
      m_mousePrev -= mouseCurr_clipSpace;
      const glm::vec2 deltaMouse = - m_mousePrev;
#endif
      m_matView = glm::rotate(glm::mat4(1.f), deltaMouse.x, glm::vec3(m_matViewPrev[1])) * m_matViewPrev;
      m_matView = glm::rotate(glm::mat4(1.f), deltaMouse.y, glm::vec3(-1.f,0.f,0.f)) * m_matView;
    }
#ifndef TRE_EMSCRIPTEN
    SDL_WarpMouseInWindow(m_parentWindow->m_window, m_parentWindow->m_resolutioncurrent.x / 2, m_parentWindow->m_resolutioncurrent.y / 2);
#endif
  }
  else if (m_mouveOnBounds && m_parentWindow->m_window_isfullscreen)
  {
    if      (mouseCurr_clipSpace.x < -0.8f)
      m_matView[3] += (0.8f + mouseCurr_clipSpace.x) * 5.f * m_mouseSensitivity.x * dt * glm::vec4(-1.f,0.f,0.f,0.f);
    else if (mouseCurr_clipSpace.x >  0.8f)
      m_matView[3] += (0.8f - mouseCurr_clipSpace.x) * 5.f * m_mouseSensitivity.x * dt * glm::vec4(1.f,0.f,0.f,0.f);
    else if (mouseCurr_clipSpace.y < -0.8f)
      m_matView[3] += (0.8f + mouseCurr_clipSpace.y) * 5.f * m_mouseSensitivity.y * dt * glm::vec4(0.f,-1.f,0.f,0.f);
    else if (mouseCurr_clipSpace.y >  0.8f)
      m_matView[3] += (0.8f - mouseCurr_clipSpace.y) * 5.f * m_mouseSensitivity.y * dt * glm::vec4(0.f,1.f,0.f,0.f);
  }

#ifndef TRE_EMSCRIPTEN
  if (m_mouseBoundPrev != m_mouseBound)
    SDL_ShowCursor(m_mouseBoundPrev);
#endif

  m_mouseBoundPrev = m_mouseBound;

  if (m_keyBound)
  {
    glm::vec4 dp = glm::vec4(0.f);

    if (control.m_keyUP)    dp += m_keySensitivity.z * dt * glm::vec4(0.f,0.f,1.f,0.f);
    if (control.m_keyDOWN)  dp += m_keySensitivity.z * dt * glm::vec4(0.f,0.f,-1.f,0.f);
    if (control.m_keyLEFT)  dp += m_keySensitivity.x * dt * glm::vec4(1.f,0.f,0.f,0.f);
    if (control.m_keyRIGHT) dp += m_keySensitivity.x * dt * glm::vec4(-1.f,0.f,0.f,0.f);
    if (control.m_keyCTRL)  dp += m_keySensitivity.y * dt * glm::vec4(0.f,1.f,0.f,0.f);
    if (control.m_keySHIFT) dp += m_keySensitivity.y * dt * glm::vec4(0.f,-1.f,0.f,0.f);

    m_matView[3] += dp;

    m_matViewPrev[3] += m_matViewPrev * glm::inverse(m_matView) * dp;

    if (control.m_home) m_matViewPrev = m_matView = glm::mat4(1.f);
  }

  if (control.m_mouse.z != 0)
  {
    const glm::vec4 dp = m_mouseSensitivity.w * dt * (control.m_mouse.z > 0.f ? glm::vec4(0.f,0.f,-1.f,0.f) : glm::vec4(0.f,0.f,1.f,0.f));

    m_matView[3] += dp;

    m_matViewPrev[3] += m_matViewPrev * glm::inverse(m_matView) * dp;
  }
}

// ============================================================================

} // namespace
