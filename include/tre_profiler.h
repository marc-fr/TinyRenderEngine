#ifndef PROFILER_H
#define PROFILER_H

#include "tre_openglinclude.h"

#ifdef TRE_PROFILE

#include "tre_model.h"

#include <array>
#include <vector>
#include <string>
#include <chrono>

namespace tre {

class texture;  // foward decl.
class font;     // foward decl.
class shader;   // foward decl.

// ============================================================================

/**
 * @brief The profiler class
 *
 */
class profiler
{
public:

  profiler();
  ~profiler();

  typedef std::chrono::steady_clock systemclock;
  typedef systemclock::time_point   systemtick;

private:
  struct s_record
  {
    glm::vec4 m_color;
    double    m_start;
    double    m_duration;
    char      m_path[64];

    bool hasSamePath(const s_record &other) const
    {
      return std::strncmp(m_path, other.m_path, 64) == 0;
    }

    int depth() const
    {
      int d = 1;
      for (char c : m_path)
      {
        if (c == '/') ++d;
        if (c == 0) break;
      }
      return d;
    }
  };

public:
  class scope
  {
  public:
    scope();
    scope(profiler *owner, const char *name, const glm::vec4 &color = glm::vec4(0.f));
    ~scope();

  protected:
    glm::vec4  m_color;
    scope      *m_parent;
    profiler   *m_owner;
    char       m_name[16];
    systemtick m_tick_start;
  };

  /// @name Profiler main interface
  /// @{
public:
  void newframe(); ///< Should be called by one thread only
  void endframe(); ///< Should be called by one thread only

  void initSubThread(); ///< Allow to capture profiling on other threads

  void pause(bool paused = true) { m_paused = paused; } ///< Pause the collect (still recording frames but the data is trashed)
  bool isPaused() const { return m_paused; }

  void enable(bool enabled) { m_enabled = enabled; } ///< Enable/Disable the profiler (collecting and drawing)
  bool isEnabled() const { return m_enabled; }

private:
  systemtick m_frameStartTick;

  struct s_context
  {
    std::string m_name = "root"; ///< Thread name
    scope *m_scopeCurrent = nullptr; ///<  Current scope
    std::vector<s_record> m_records; ///< Records
  };

  s_context m_context; // TODO : each thread will have its context and a pointer to its context

  std::vector<s_record>    m_collectedRecords;
  std::vector<s_record>    m_meanvalueRecords; // TODO: keep like this ??

  struct s_frame
  {
    struct s_recordFlatten
    {
      glm::vec4 m_color;
      double    m_duration;
      int       m_threadId;
      s_recordFlatten() = default;
      s_recordFlatten(const glm::vec4 color, double duration, int threadId) : m_color(color), m_duration(duration), m_threadId(threadId) {}
    };
    std::vector<s_recordFlatten> m_records;
    float                        m_globalTime = 0.f;
  };
  std::array<s_frame, 0x100> m_recordsOverFrames;
  uint                       m_frameIndex = 0;

  bool                     m_enabled = false;
  bool                     m_paused = false;

  s_context *get_threadContext() { return &m_context; } // TODO ...
  void      set_threadName(const std::string &name) { get_threadContext()->m_name = name; }

  /// @}

  /// @name Events
  /// @{
public:
  void updateCameraInfo(const glm::mat3 &mProjView, const glm::vec2 &screenSize);
  void updateModelMatrix(const glm::mat3 &mModel) { m_matModel = mModel; }
  bool acceptEvent(const SDL_Event &event);
  bool acceptEvent(const glm::ivec2 &mousePosition);

private:
  int m_hoveredRecord = -1;
  glm::vec2 m_mousePosition; ///< model-space mouse position
  /// @}

  /// @name Geometry and drawing
  /// @{
public:
  void draw() const; ///< Bind the shader and the resources, Emit draw-calls

  bool loadIntoGPU(font *fontToUse); ///< Load a new model and generate model's parts
  void updateIntoGPU(); ///< Update the vertex-data of the model into the V-RAM.
  void clearGPU(); ///< Clean V-RAM.

  bool loadShader(shader *shaderToUse = nullptr);
  void clearShader();

protected:
  void compute_data();

private:
  font          *m_font = nullptr;
  texture       *m_whiteTexture = nullptr;
  modelRaw2D    m_model;
  shader        *m_shader = nullptr;
  bool          m_shaderOwner = true;

  std::size_t m_partTri;
  std::size_t m_partLine;
  std::size_t m_partText;

  // geometry data
  static constexpr float m_xTitle   = 0.f;
  static constexpr float m_xStart   = 0.4f;
  static constexpr float m_yStart   = 0.f;
  static constexpr float m_dTime    = 0.0005f; ///< time (second) per grid-line (dX)
  static constexpr float m_dYthread = 0.1f;

  float m_dX             = 0.15f;
  float m_dY             = 0.15f;

  glm::vec2 m_viewportSize;
  glm::mat3 m_PV;
  glm::mat3 m_matModel = glm::mat3(1.f);
  /// @}

};

// ============================================================================

extern profiler profilerRoot;

inline void profiler_initThread() { profilerRoot.initSubThread(); }

inline void profiler_newFrame() { profilerRoot.newframe(); }
inline void profiler_endframe() { profilerRoot.endframe(); }
inline void profiler_pause(bool paused) { profilerRoot.pause(paused); }
inline bool profiler_isPaused() { return profilerRoot.isPaused(); }
inline void profiler_enable(bool enable) { profilerRoot.enable(enable); }
inline bool profiler_isEnabled() { return profilerRoot.isEnabled(); }

inline void profiler_updateCameraInfo(const glm::mat3 &mProjView, const glm::vec2 &screenSize) { profilerRoot.updateCameraInfo(mProjView, screenSize); }
inline void profiler_updateModelMatrix(const glm::mat3 &mModel) { profilerRoot.updateModelMatrix(mModel); }
inline bool profiler_acceptEvent(const SDL_Event &event) { return profilerRoot.acceptEvent(event); }
inline void profiler_acceptEvent(const glm::ivec2 &mousePosition) { profilerRoot.acceptEvent(mousePosition); }

inline void profiler_draw() { profilerRoot.draw(); }
inline bool profiler_loadIntoGPU(font *fontToUse) { return profilerRoot.loadIntoGPU(fontToUse); }
inline void profiler_updateIntoGPU() { profilerRoot.updateIntoGPU(); }
inline void profiler_clearGPU() { profilerRoot.clearGPU(); }
inline bool profiler_loadShader(shader *shaderToUse = nullptr) { return profilerRoot.loadShader(shaderToUse); }
inline void profiler_clearShader() { profilerRoot.clearShader(); }

} // namespace

#define TRE_PROFILEDSCOPE(zonename, id) tre::profiler::scope scopedZone##id(&tre::profilerRoot, zonename);

#define TRE_PROFILEDSCOPE_COLORED(zonename, id, color) tre::profiler::scope scopedZone##id(&tre::profilerRoot, zonename, color);

#else // TRE_PROFILE

namespace tre {

class shader;        // foward decl.
class font;          // foward decl.


inline void profiler_initThread() {}

inline void profiler_newFrame() {}
inline void profiler_endframe() {}
inline void profiler_pause(bool ) {}
inline bool profiler_isPaused() { return true; }
inline void profiler_enable(bool ) {}
inline bool profiler_isEnabled() { return false; }

inline void profiler_updateCameraInfo(const glm::mat3 &, const glm::vec2 &) {}
inline void profiler_updateModelMatrix(const glm::mat3 &) {}
inline bool profiler_acceptEvent(const SDL_Event &) { return false; }
inline void profiler_acceptEvent(const glm::ivec2 &) {}

inline void profiler_draw() {}
inline bool profiler_loadIntoGPU(font *) { return true; }
inline void profiler_updateIntoGPU() {}
inline void profiler_clearGPU() {}
inline bool profiler_loadShader(shader * = nullptr) { return true; }
inline void profiler_clearShader() {}

} // namespace

#define TRE_PROFILEDSCOPE(zonename, id)

#define TRE_PROFILEDSCOPE_COLORED(zonename, id, color)

#endif // TRE_PROFILE

#endif // PROFILER_H
