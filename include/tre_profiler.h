#ifndef PROFILER_H
#define PROFILER_H

#include "tre_openglinclude.h"

#ifdef TRE_PROFILE

#include "tre_model.h"
#include "tre_utils.h" // chunkVector

#include <atomic>
#include <chrono>
#include <cstdint>
#include <unordered_map>

namespace tre {

class texture;  // forward decl.
class font;     // forward decl.
class shader;   // forward decl.

// ============================================================================

/**
 * @brief The profiler class
 *
 * - A thread is captured once registered: initSubThread() for the sub-threads.
 *   Scopes opened by an unregistered thread are not recorded.
 * - Each registered thread owns a context (s_context). Recording is wait-free.
 * - A thread that exits stays registered, flagged as dead.
 * - newframe() and endframe() are called by the "main" thread only.
 *   A task still running is collected by a later endframe().
 * - Scope names are not copied: they must have a static storage (string literals).
 *
 * Interactions: B pause/resume, V enable/disable.
 * While paused: hover for details, mouse wheel to zoom, left-drag to pan, right-click to reset the view.
 */
class profiler
{
public:

  profiler();
  ~profiler();

  profiler(const profiler &) = delete;
  profiler &operator=(const profiler &) = delete;

  typedef std::chrono::steady_clock systemclock;
  typedef systemclock::time_point   systemtick;

  static constexpr std::size_t k_threadNameSize = 24; ///< Max length of a thread name

private:
  struct s_context; // thread context (forward decl.)

  struct s_record
  {
    double        m_start;       ///< seconds since the profiler's epoch
    const char    *m_name;       ///< scope name
    float         m_duration;    ///< seconds
    std::uint32_t m_hash;        ///< hash of the full path "thread/scope/child"
    std::uint32_t m_color;       ///< RGBA8
    std::int16_t  m_level;       ///< 0 = top-level scope of the thread
    std::int16_t  m_threadIndex;

    double end() const { return m_start + double(m_duration); }
  };
  static_assert(sizeof(s_record) <= 32, "s_record should fit in 32 bytes");

public:
  class scope
  {
  public:
    scope();
    scope(profiler *owner, const char *name /* static storage */, const glm::vec4 &color = glm::vec4(0.f));
    ~scope();

    scope(const scope &) = delete;
    scope &operator=(const scope &) = delete;

  protected:
    scope         *m_parent;
    s_context     *m_context; ///< context of the thread
    const char    *m_name;
    systemtick    m_tick_start;
    float         m_hue;      ///< used by the children to derive their color
    std::uint32_t m_color;    ///< RGBA8
    std::uint32_t m_hash;
    std::int16_t  m_level;
  };

  /// @name Profiler main interface
  /// @{
public:
  void newframe(); ///< Signal a new frame capture, should be called by the "main" thread only
  void endframe(); ///< Close and flush the current frame capture, should be called by the "main" thread only

  void initSubThread(const char *name = nullptr); ///< Register the calling thread (required to capture it). The first registration wins.

  void pause(bool paused = true) { m_paused.store(paused, std::memory_order_relaxed); } ///< Freeze the view (only the late records of tasks started before the frozen frame's end are kept)
  bool isPaused() const { return m_paused.load(std::memory_order_relaxed); }

  void enable(bool enabled) { m_enabled.store(enabled, std::memory_order_relaxed); } ///< Enable/Disable the profiler (collecting and drawing). The recording state changes at the next newframe().
  bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }

private:
  struct s_context // thread context
  {
    static constexpr unsigned k_ringSize = 1024; ///< must be a power of 2

    profiler      *m_owner = nullptr;
    int           m_threadIndex = 0;
    char          m_name[k_threadNameSize] = {};
    std::uint32_t m_nameHash = 0;

    scope         *m_scopeCurrent = nullptr; ///< Current recording scope

    enum e_state : int { STATE_INIT = 0, STATE_ALIVE = 1, STATE_EXITED = 2 };
    std::atomic<int> m_state = STATE_INIT;

    // thread-safe ring-buffer (single-producer, single-consumer)
    alignas(64) std::array<s_record, k_ringSize> m_ring;
    alignas(64) std::atomic<unsigned> m_writeIndex = 0;       ///< written by producer
    unsigned                          m_readIndexCached = 0; ///< producer's copy of m_readIndex
    alignas(64) std::atomic<unsigned> m_readIndex = 0;       ///< written by consumer

    s_record *pushBegin() ///< returns nullptr if the ring is full
    {
      const unsigned w = m_writeIndex.load(std::memory_order_relaxed);
      if (w - m_readIndexCached >= k_ringSize) // looks full: fetch the consumer's progress (rare)
      {
        m_readIndexCached = m_readIndex.load(std::memory_order_acquire);
        if (w - m_readIndexCached >= k_ringSize) return nullptr;
      }
      return &m_ring[w & (k_ringSize - 1)];
    }
    void pushEnd()
    {
      m_writeIndex.store(m_writeIndex.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    void flush(std::vector<s_record> *out); ///< Append the completed records into "out", or discard them if nullptr.
  };
  static thread_local s_context *a_context; /// per-thread context

  struct s_threadGuard // thread-guard against exiting thread
  {
    s_context *m_context = nullptr;
    ~s_threadGuard();
  };
  static thread_local s_threadGuard a_threadGuard; // only touched when a thread registers, and at its exit

  // Registration
  static constexpr std::size_t        k_contextChunkSize = 4;
  chunkVector<s_context, k_contextChunkSize> m_contexts;
  std::atomic<int>                    m_contextCount = 0;

  s_context *register_thread(const char *name);
  s_context *register_main();

  const systemtick              m_epoch;
  systemtick                    m_frameStartTick;
  int                           m_frameThreadIndex = -1;

  std::atomic<bool>             m_recording{false}; ///< m_enabled, sampled at newframe()
  std::atomic<bool>             m_enabled{false};
  std::atomic<bool>             m_paused{false};

  struct s_mean
  {
    double   m_duration = 0.; ///< exponential moving average (per call)
    double   m_max = 0.;      ///< slowly decaying max (per call)
    unsigned m_count = 0;
  };

  std::unordered_map<std::uint32_t, s_mean> m_means; ///< key: path hash

  // last collected frame (timeline)
  std::vector<s_record>     m_records;          ///< records collected at the end of the last frame (all threads)
  std::vector<s_record>     m_drained;          ///< scratch buffer (paused)
  double                    m_frameStart = 0.;  ///< seconds since epoch
  double                    m_frameEnd = 1.e-3;
  double                    m_recordsStart = 0.; ///< min(m_frameStart, records' start)
  double                    m_recordsEnd = 1.e-3;///< max(m_frameEnd, records' end)

  // history (frame graph): flattened top-level scopes of the frame thread
  struct s_frame
  {
    struct s_bar ///< 8 bytes
    {
      std::uint32_t m_color; ///< RGBA8
      float         m_duration;
    };
    std::vector<s_bar> m_bars;
    double             m_start = 0.;    ///< seconds since epoch
    float              m_duration = 0.f;

    double end() const { return m_start + double(m_duration); }
  };
  static constexpr unsigned  k_frameCount = 0x100;
  std::array<s_frame, k_frameCount> m_recordsOverFrames;
  unsigned                   m_frameIndex = 0; ///< last collected frame

  const char *threadName(int threadIndex) const;
  int         buildPath(const s_record &rec, char *out, int outSize) const; ///< "thread/scope/child", from the enclosing records of the last frame
  /// @}

  /// @name Events
  /// @{
public:
  void updateCameraInfo(const glm::mat3 &mProjView, const glm::vec2 &screenSize);
  void updateModelMatrix(const glm::mat3 &mModel) { m_matModel = mModel; }
  bool acceptEvent(const SDL_Event &event);
  bool acceptEvent(const glm::ivec2 &mousePosition); ///< mouse move (hovering)

private:
  glm::vec2 mouseToModel(const glm::ivec2 &mousePosition) const;
  bool      updateHover(); ///< hit-test at m_mousePosition
  bool      isInTimeline(const glm::vec2 &modelPosition) const;
  void      view_zoom(float xModel, double factor); ///< factor > 1 zooms in, around xModel
  void      view_pan(float dxModel);
  void      view_clamp();
  void      view_reset();

  int       m_hoveredRecord = -1;  ///< index in m_records
  glm::vec2 m_mousePosition = glm::vec2(0.f); ///< model-space mouse position
  bool      m_dragging = false;
  float     m_dragLastX = 0.f;     ///< model-space
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
  enum e_clip { CLIP_LEFT = 1, CLIP_RIGHT = 2 };

  int       threadCount() const { return int(m_contexts.size()); }
  int       rowOfThread(int threadIndex) const;
  int       threadOfRow(int row) const;
  bool      isVisible(const s_record &rec) const { return rec.m_start < m_viewEnd && rec.end() > m_viewStart; }
  glm::vec4 recordAABB(const s_record &rec, int *clipFlags = nullptr) const; ///< (x0, y0, x1, y1) in model space, clipped to the view
  float     pixelSizeModel() const;
  float     xOfTime(double t) const { return m_xStart + float((t - m_viewStart) * m_xScale); }

  font          *m_font = nullptr;
  texture       *m_whiteTexture = nullptr;
  modelRaw2D    m_model;
  shader        *m_shader = nullptr;
  bool          m_shaderOwner = true;

  std::size_t m_partTri = 0;
  std::size_t m_partLine = 0;
  std::size_t m_partTooltip = 0;
  std::size_t m_partText = 0;
  bool        m_geometryReady = false;
  bool        m_tooltipVisible = false;

  // geometry data
  static constexpr float m_xTitle    = 0.f;
  static constexpr float m_xStart    = 0.4f;
  static constexpr float m_yStart    = 0.f;
  static constexpr float m_dYthread  = 0.1f;
  static constexpr int   k_minLevels = 4;

  double           m_viewStart = 0.;       ///< displayed time window, seconds since epoch (= last frame, or zoomed/panned while paused)
  double           m_viewEnd = 1.e-3;
  float            m_xScale = 1500.f;      ///< timeline: model units per second
  float            m_yScaleFrames = 30.f;  ///< frame graph: model units per second
  std::vector<int> m_rowLevels;            ///< number of levels per row (layout of the last compute_data)

  glm::vec2 m_viewportSize = glm::vec2(0.f);
  glm::mat3 m_PV = glm::mat3(1.f);
  glm::mat3 m_matModel = glm::mat3(1.f);
  /// @}

};

// ============================================================================

extern profiler profilerRoot;

inline void profiler_initThread(const char *name = nullptr) { profilerRoot.initSubThread(name); }

inline void profiler_newFrame() { profilerRoot.newframe(); }
inline void profiler_endframe() { profilerRoot.endframe(); }
inline void profiler_pause(bool paused) { profilerRoot.pause(paused); }
inline bool profiler_isPaused() { return profilerRoot.isPaused(); }
inline void profiler_enable(bool enable) { profilerRoot.enable(enable); }
inline bool profiler_isEnabled() { return profilerRoot.isEnabled(); }

inline void profiler_updateCameraInfo(const glm::vec2 &screenSize, const glm::mat3 &mView = glm::mat3(1.f)) { profilerRoot.updateCameraInfo(mView, screenSize); }
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

// The "" prefix only compiles with a string literal (static storage), as the scope name is not copied.
#define TRE_PROFILEDSCOPE(zonename, id) tre::profiler::scope scopedZone##id(&tre::profilerRoot, "" zonename);

#define TRE_PROFILEDSCOPE_COLORED(zonename, id, color) tre::profiler::scope scopedZone##id(&tre::profilerRoot, "" zonename, color);

#else // TRE_PROFILE

namespace tre {

class shader;        // foward decl.
class font;          // foward decl.


inline void profiler_initThread(const char * = nullptr) {}

inline void profiler_newFrame() {}
inline void profiler_endframe() {}
inline void profiler_pause(bool ) {}
inline bool profiler_isPaused() { return true; }
inline void profiler_enable(bool ) {}
inline bool profiler_isEnabled() { return false; }

inline void profiler_updateCameraInfo(const glm::vec2 &, const glm::mat3 & = glm::mat3(1.f)) {}
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
