#ifndef PROFILER_H
#define PROFILER_H

#include "openglinclude.h"

#include "textgenerator.h"

#ifdef TRE_PROFILE

#include "model.h"
#include "utils.h"

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

  profiler() { m_timeOverFrames.fill(0.f);  }
  ~profiler() { TRE_ASSERT(m_whiteTexture == nullptr); TRE_ASSERT(m_shader == nullptr); /* clear contexts ... */ }

  typedef std::chrono::steady_clock systemclock;
  typedef systemclock::time_point   systemtick;

  typedef std::vector<std::string>  recordpath; // TODO : optimize with identifier instead of string operations ?

private:
  struct s_record
  {
    glm::vec4 m_color;
    recordpath m_path;
    double m_start;
    double m_duration;

    inline uint length() const { return uint(m_path.size()); }

    bool isSamePath(const recordpath &otherPath, const bool withRoot = true) const
    {
      if (length() != otherPath.size())
        return false;
      for (uint iP = (withRoot ? 0 : 1); iP < length(); ++iP)
      {
        if (m_path[iP] != otherPath[iP]) return false;
      }
      return true;
    }

    bool isChildOf(const recordpath &otherPath, const uint upperLevel = 0) const
    {
      if (length() < otherPath.size())
        return false;
      const uint iPstop = (length() - upperLevel < otherPath.size()) ? length() - upperLevel : uint(otherPath.size());
      for (uint iP = 0; iP < iPstop; ++iP)
      {
        if (m_path[iP] != otherPath[iP]) return false;
      }
      return true;
    }
  };

public:
  class scope
  {
  public:
    scope(const std::string &name = "no-named", const glm::vec4 color = glm::vec4(0.f));
    scope(profiler *owner, const std::string &name, const glm::vec4 color = glm::vec4(0.f));
    ~scope();

    void attach(profiler *owner);

  protected:
    glm::vec4 m_color;
    scope * m_parent = nullptr;
    profiler * m_owner = nullptr;
    const std::string m_name;
    std::vector<std::string> m_path; // TODO : optimize with identifier instead of string operations ?
    systemtick m_tick_start;
  };

  /// @name Profiler main interface
  /// @{
public:
  void newframe(); ///< Should be called by one thread only
  void endframe(); ///< Should be called by one thread only

  void pause(bool paused = true) { m_paused = paused; } ///< Pause the collect (still recording frames but the data is trashed)
  bool isPaused() const { return m_paused; }

  void enable(bool enabled) { m_enabled = enabled; } ///< Enable/Disable the profiler (collecting and drawing)
  bool isEnabled() const { return m_enabled; }

protected:
  void collect(); ///< Collect the records. Should be called by one thread only. All threads must be synchronized by the caller

private:
  systemtick m_frameStartTick;

  struct s_context
  {
    std::string m_name = "root"; ///< Thread name
    scope *m_scopeCurrent = nullptr; ///<  Current scope
    std::vector<s_record> m_records; ///< Records
  };

  s_context m_context;
  // TODO : each thread will have its context and a pointer to its context

  std::vector<s_record>    m_collectedRecords;
  std::vector<s_record>    m_meanvalueRecords; // TODO: keep like this ??
  std::array<float, 0x100> m_timeOverFrames;
  uint                 m_frameIndex = 0;
  bool                     m_enabled = true;
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
  void create_data();
  void compute_data();

private:
  font          *m_font = nullptr;
  texture       *m_whiteTexture = nullptr;
  modelRaw2D    m_model;
  shader        *m_shader = nullptr;
  bool          m_shaderOwner = true;

  uint m_partTri;
  uint m_partLine;
  uint m_partText;

  // geometry data
  const float m_xTitle   = 0.f;
  const float m_xStart   = 0.4f;
  const float m_yStart   = 0.f;
  const float m_dTime    = 0.0005f; ///< time (second) per grid-line (dX)
  const float m_dYthread = 0.1f;

  float m_dX             = 0.15f;
  float m_dY             = 0.15f;

  glm::vec2 m_viewportSize;
  glm::mat3 m_PV;
  glm::mat3 m_matModel = glm::mat3(1.f);
  /// @}

};

// ============================================================================

extern profiler profilerRoot;

inline void profiler_initThread()
{

}

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

class texture;       // foward decl.
class shader;        // foward decl.

inline void profiler_initThread() {}

inline void profiler_newFrame() {}
inline void profiler_endframe() {}
inline void profiler_pause(bool paused) {}
inline bool profiler_isPaused() { return true; }
inline void profiler_enable(bool enable) {}
inline bool profiler_isEnabled() { return false; }

inline void profiler_updateCameraInfo(const glm::mat3 &mProjView, const glm::vec2 &screenSize) {}
inline void profiler_updateModelMatrix(const glm::mat3 &mModel) {}
inline bool profiler_acceptEvent(const SDL_Event &event) { return true; }
inline void profiler_acceptEvent(const glm::ivec2 &mousePosition) {}

inline void profiler_draw() {}
inline bool profiler_loadIntoGPU(font *fontToUse) { return true; }
inline void profiler_updateIntoGPU() {}
inline void profiler_clearGPU() {}
inline bool profiler_loadShader(shader *shaderToUse = nullptr) { return true; }
inline void profiler_clearShader() {}

} // namespace

#define TRE_PROFILEDSCOPE(zonename, id)

#define TRE_PROFILEDSCOPE_COLORED(zonename, id, color)

#endif // TRE_PROFILE

#endif // PROFILER_H
