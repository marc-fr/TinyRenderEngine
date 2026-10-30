#include "tre_profiler.h"

#ifdef TRE_PROFILE

#include "tre_shader.h"
#include "tre_texture.h"
#include "tre_font.h"
#include "tre_textgenerator.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace tre {

// == Global variables ========================================================

profiler profilerRoot;

thread_local profiler::s_context     *profiler::a_context = nullptr;
thread_local profiler::s_threadGuard  profiler::a_threadGuard;

// == Helpers =================================================================

static constexpr std::uint32_t k_fnvOffset = 2166136261u;
static constexpr std::uint32_t k_fnvPrime  = 16777619u;

static inline std::uint32_t _hashChar(std::uint32_t h, char c)
{
  return (h ^ std::uint8_t(c)) * k_fnvPrime; // FNV-1a
}

static inline double _seconds(profiler::systemclock::duration d)
{
  return std::chrono::duration<double>(d).count();
}

/// Copy a null-terminated string, truncated to fit in "dst".
static std::size_t _copyString(char *dst, std::size_t dstSize, const char *src)
{
  std::size_t n = 0;
  while (n + 1 < dstSize && src[n] != 0) { dst[n] = src[n]; ++n; }
  dst[n] = 0;
  return n;
}

/// Append a null-terminated string at dst[len], truncated to fit in "dst". Returns the new length.
static int _appendString(char *dst, int dstSize, int len, const char *src)
{
  return len + int(_copyString(dst + len, std::size_t(dstSize - len), src));
}

// ----------------------------------------------------------------------------

static inline std::uint32_t _packColor(const glm::vec4 &color)
{
  const glm::vec4 c = glm::clamp(color, 0.f, 1.f) * 255.f + 0.5f;
  return std::uint32_t(c.r) | (std::uint32_t(c.g) << 8) | (std::uint32_t(c.b) << 16) | (std::uint32_t(c.a) << 24);
}

static inline glm::vec4 _unpackColor(std::uint32_t c)
{
  return glm::vec4(float(c & 0xFF), float((c >> 8) & 0xFF), float((c >> 16) & 0xFF), float(c >> 24)) * (1.f / 255.f);
}

static float _hueFromColor(const glm::vec4 &color)
{
  const float vmax = std::max(color.r, std::max(color.g, color.b));
  const float vmin = std::min(color.r, std::min(color.g, color.b));
  const float delta = vmax - vmin;
  if (delta < 1.e-3f) return 0.f;

  float h;
  if      (vmax == color.r) h =       (color.g - color.b) / delta;
  else if (vmax == color.g) h = 2.f + (color.b - color.r) / delta;
  else                      h = 4.f + (color.r - color.g) / delta;
  h /= 6.f;
  if (h < 0.f) h += 1.f;
  return h; // [0,1)
}

static glm::vec4 _colorFromHS(float hue /**< in [0,1) */, float sat /**< saturation is in ]0,1] range */)
{
  TRE_ASSERT(sat > 0.f && sat <= 1.f);
  const float hueXsat = hue * 6.f * sat;
  // Convert HSV (hue, sat, value=1) -> RBG
  return glm::vec4( 1.f - glm::clamp(2.f * sat - std::abs(hueXsat - 3.f * sat), 0.f, sat),
                    1.f - glm::clamp(std::abs(hueXsat - 2.f * sat) - sat, 0.f, sat),
                    1.f - glm::clamp(std::abs(hueXsat - 4.f * sat) - sat, 0.f, sat),
                    1.f);
}

// ----------------------------------------------------------------------------

static double _niceStep(double raw) ///< smallest value of the 1-2-5 sequence >= raw
{
  const double p = std::pow(10., std::floor(std::log10(raw)));
  const double m = raw / p;
  return p * (m <= 1. ? 1. : m <= 2. ? 2. : m <= 5. ? 5. : 10.);
}

// == Profiler Scope ==========================================================

profiler::scope::scope()
  : m_parent(nullptr), m_context(nullptr), m_name(nullptr), m_hue(0.f), m_color(0), m_hash(k_fnvOffset), m_level(-1)
{
}

profiler::scope::scope(profiler *owner, const char *name, const glm::vec4 &color)
  : m_parent(nullptr), m_context(nullptr), m_name(nullptr), m_hue(0.f), m_color(0), m_hash(k_fnvOffset), m_level(0)
{
  TRE_ASSERT(owner != nullptr);
  TRE_ASSERT(name != nullptr);
  TRE_ASSERT(std::strchr(name, '/') == nullptr); // name must not contain slash

  if (!owner->m_recording.load(std::memory_order_relaxed)) return;

  s_context *ctx = a_context;
  if (ctx == nullptr) return; // unregistered thread
  TRE_ASSERT(ctx->m_owner == owner);

  // attach the scope to the thread's context.
  m_context = ctx;
  m_name = name;
  m_parent = ctx->m_scopeCurrent;
  ctx->m_scopeCurrent = this;

  // hash of the path "thread/scope/child"
  if (m_parent != nullptr)
  {
    m_hash  = m_parent->m_hash;
    m_level = std::int16_t(m_parent->m_level + 1);
  }
  else
  {
    m_hash  = ctx->m_nameHash;
    m_level = 0;
  }
  m_hash = _hashChar(m_hash, '/');
  for (const char *c = name; *c != 0; ++c) m_hash = _hashChar(m_hash, *c);

  // resolve the color now, so the children can derive their hue from it.
  if (color.w != 0.f)
  {
    m_hue   = _hueFromColor(color);
    m_color = _packColor(color);
  }
  else
  {
    float hue;
    if (m_parent == nullptr) hue = float(m_hash & 0xFFFF) / 65536.f; // full hue range for top-level scopes
    else                     hue = m_parent->m_hue + (float(m_hash & 0xFF) / 255.f - 0.5f) * 0.3f / float(m_level); // around the parent's hue
    hue -= std::floor(hue);
    const float sat = std::max(0.35f, 0.8f - 0.1f * float(m_level)); // deeper is lighter
    m_hue   = hue;
    m_color = _packColor(_colorFromHS(hue, sat));
  }

  // start the timer
  m_tick_start = systemclock::now();
}

profiler::scope::~scope()
{
  if (m_context == nullptr) return;

  const systemtick tick_end = systemclock::now();

  s_context *ctx = m_context;
  TRE_ASSERT(ctx == a_context); // scopes must be closed by the thread that opened them
  TRE_ASSERT(ctx->m_scopeCurrent == this); // scopes must be closed in reverse order

  // pop the scope
  ctx->m_scopeCurrent = m_parent;

  // write the record
  s_record *rec = ctx->pushBegin();
  if (rec == nullptr) return; // ring full: dropped

  rec->m_start       = _seconds(m_tick_start - ctx->m_owner->m_epoch);
  rec->m_duration    = float(_seconds(tick_end - m_tick_start));
  rec->m_hash        = m_hash;
  rec->m_name        = m_name;
  rec->m_color       = m_color;
  rec->m_level       = m_level;
  rec->m_threadIndex = std::int16_t(ctx->m_threadIndex);

  ctx->pushEnd();
}

// == Thread contexts =========================================================

void profiler::s_context::flush(std::vector<s_record> *out)
{
  const unsigned r = m_readIndex.load(std::memory_order_relaxed);
  const unsigned w = m_writeIndex.load(std::memory_order_acquire);
  const unsigned n = w - r;
  if (n != 0)
  {
    if (out != nullptr)
    {
      // copy in (at most) 2 contiguous blocks: before and after the wrap-around
      const unsigned i0 = r & (k_ringSize - 1);
      const unsigned n0 = std::min(n, k_ringSize - i0);
      out->insert(out->end(), m_ring.begin() + i0, m_ring.begin() + (i0 + n0));
      out->insert(out->end(), m_ring.begin(), m_ring.begin() + (n - n0));
    }
    m_readIndex.store(w, std::memory_order_release); // the slots can be reused by the producer
  }
}

profiler::s_threadGuard::~s_threadGuard()
{
  if (m_context == nullptr) return;
  TRE_ASSERT(m_context->m_scopeCurrent == nullptr);
  a_context = nullptr;
  m_context->m_state.store(s_context::STATE_EXITED, std::memory_order_release); // stays registered, flagged as dead
  m_context = nullptr;
}

profiler::s_context *profiler::register_thread(const char *name)
{
  TRE_ASSERT(a_context == nullptr);

  const int index = m_contextCount.fetch_add(1);
  TRE_ASSERT(index >= 0 && index < 0x7FFF); // fits in s_record::m_threadIndex
  m_contexts.resize(index + 1);

  s_context &ctx = m_contexts[index];
  ctx.m_owner = this;
  ctx.m_threadIndex = index;
  if (name != nullptr) _copyString(ctx.m_name, k_threadNameSize, name);
  else                 std::snprintf(ctx.m_name, k_threadNameSize, "thread%d", int(index));
  std::uint32_t h = k_fnvOffset;
  for (const char *c = ctx.m_name; *c != 0; ++c) h = _hashChar(h, *c);
  ctx.m_nameHash = h;
  ctx.m_scopeCurrent = nullptr;
  ctx.m_state.store(s_context::STATE_ALIVE, std::memory_order_relaxed);

  a_threadGuard.m_context = &ctx; // first access of the guard on this thread: registers its destructor
  a_context = &ctx;
  return &ctx;
}

void profiler::initSubThread(const char *name)
{
  if (a_context != nullptr) return; // already registered: the first registration wins
#ifdef TRE_DEBUG
  if (name != nullptr && std::strchr(name, '/') != nullptr) TRE_FATAL("profile: the thread name must not contain slash (/)");
#endif
  register_thread(name);
}

profiler::s_context *profiler::register_main()
{
  s_context *ctx = a_context;
  if (ctx == nullptr) ctx = register_thread("main");
  if (m_frameThreadIndex == -1) m_frameThreadIndex = ctx->m_threadIndex;
  TRE_ASSERT(ctx->m_threadIndex == m_frameThreadIndex); // newframe() and endframe() must be called by the same thread
  TRE_ASSERT(ctx->m_scopeCurrent == nullptr);
  return ctx;
}

const char *profiler::threadName(int threadIndex) const
{
  if (threadIndex >= 0 && threadIndex < threadCount()) return m_contexts[threadIndex].m_name;
  return "?";
}

int profiler::buildPath(const s_record &rec, char *out, int outSize) const
{
  static constexpr int kMaxDepth = 16;
  const char *names[kMaxDepth];
  int depth = 0;
  names[depth++] = rec.m_name;

  // the parent is the record of the same thread, one level up, that contains the child's start
  // (records of the same thread and level do not overlap). It can be missing if still running.
  const s_record *cur = &rec;
  for (int level = rec.m_level - 1; level >= 0 && depth < kMaxDepth; --level)
  {
    const s_record *parent = nullptr;
    for (const s_record &r : m_records)
    {
      if (r.m_threadIndex == cur->m_threadIndex && r.m_level == level && r.m_start <= cur->m_start && r.end() >= cur->m_start)
      {
        parent = &r;
        break;
      }
    }
    names[depth++] = (parent != nullptr) ? parent->m_name : "?";
    if (parent != nullptr) cur = parent;
  }

  int len = _appendString(out, outSize, 0, threadName(rec.m_threadIndex));
  if (rec.m_level + 1 > depth) len = _appendString(out, outSize, len, "/...");
  for (int i = depth - 1; i >= 0; --i)
  {
    len = _appendString(out, outSize, len, "/");
    len = _appendString(out, outSize, len, names[i]);
  }
  return len;
}

// == Profiler Main ===========================================================

profiler::profiler() : m_epoch(systemclock::now()), m_frameStartTick(m_epoch)
{
}

profiler::~profiler()
{
  TRE_ASSERT(m_whiteTexture == nullptr);
  TRE_ASSERT(m_shader == nullptr);
  // m_contexts frees the contexts.
}

void profiler::newframe()
{
  register_main();

  // enabling/disabling takes effect at frame boundaries
  m_recording.store(m_enabled.load(std::memory_order_relaxed), std::memory_order_relaxed);

  m_frameStartTick = systemclock::now();
}

void profiler::endframe()
{
  const systemtick tick_end = systemclock::now(); // before the profiler's own work

  if (!m_enabled.load(std::memory_order_relaxed))
  {
    for (s_context &ctx : m_contexts) ctx.flush(nullptr);
    m_hoveredRecord = -1;
    m_dragging = false;
    return;
  }

  if (m_paused.load(std::memory_order_relaxed))
  {
    // frozen view: only keep the late records of the tasks started before the end of the last frame
    m_drained.clear();
    for (s_context &ctx : m_contexts) ctx.flush(&m_drained);
    for (const s_record &rec : m_drained)
    {
      if (rec.m_start >= m_frameEnd) continue;
      m_records.push_back(rec);
      m_recordsStart = std::min(m_recordsStart, rec.m_start);
      m_recordsEnd   = std::max(m_recordsEnd, rec.end());
    }
    return;
  }

  // 1. last frame: collect the records of all threads (wait-free: only the completed records are taken)
  m_frameStart = _seconds(m_frameStartTick - m_epoch);
  m_frameEnd   = _seconds(tick_end - m_epoch);
  m_records.clear();
  for (s_context &ctx : m_contexts) ctx.flush(&m_records);

  m_recordsStart = m_frameStart;
  m_recordsEnd   = m_frameEnd;
  m_viewStart    = m_frameStart;
  m_viewEnd      = m_frameEnd;
  m_hoveredRecord = -1;
  m_dragging = false;

  // 2. history
  m_frameIndex = (m_frameIndex + 1) % k_frameCount;
  s_frame &frame = m_recordsOverFrames[m_frameIndex];
  frame.m_start    = m_frameStart;
  frame.m_duration = float(m_frameEnd - m_frameStart);
  frame.m_bars.clear();

  for (const s_record &rec : m_records)
  {
    m_recordsStart = std::min(m_recordsStart, rec.m_start);
    m_recordsEnd   = std::max(m_recordsEnd, rec.end());

    // mean values (per call)
    s_mean &m = m_means[rec.m_hash];
    if (m.m_count == 0) m.m_duration = rec.m_duration;
    else                m.m_duration = 0.95 * m.m_duration + 0.05 * rec.m_duration;
    m.m_max = std::max(double(rec.m_duration), 0.99 * m.m_max);
    ++m.m_count;

    // flattened frame: top-level scopes of the frame thread
    if (rec.m_threadIndex == m_frameThreadIndex && rec.m_level == 0)
    {
      const double d = std::min(rec.end(), m_frameEnd) - std::max(rec.m_start, m_frameStart);
      if (d > 0.) frame.m_bars.push_back({ rec.m_color, float(d) });
    }
  }
}

// ============================================================================

void profiler::updateCameraInfo(const glm::mat3 &mView, const glm::vec2 &screenSize)
{
  tre::compute2DOrthoProjection(m_PV, float(screenSize.y) / float(screenSize.x));
  m_PV = m_PV * mView;
  m_viewportSize = screenSize;
}

glm::vec2 profiler::mouseToModel(const glm::ivec2 &mousePosition) const
{
  if (m_viewportSize.x <= 0.f || m_viewportSize.y <= 0.f) return glm::vec2(0.f);
  const glm::vec3 mouseScreenSpace(mousePosition.x * 2.f / m_viewportSize.x - 1.f, 1.f - mousePosition.y * 2.f / m_viewportSize.y, 1.f);
  return glm::vec2(glm::inverse(m_PV * m_matModel) * mouseScreenSpace);
}

bool profiler::isInTimeline(const glm::vec2 &pos) const
{
  const float xEnd = xOfTime(m_viewEnd);
  const float yEnd = m_yStart + float(std::max<std::size_t>(1, m_rowLevels.size())) * m_dYthread;
  return pos.x >= m_xStart && pos.x <= xEnd && pos.y >= m_yStart && pos.y <= yEnd;
}

void profiler::view_zoom(float xModel, double factor)
{
  const double duration    = m_viewEnd - m_viewStart;
  const double newDuration = glm::clamp(duration / factor, 1.e-6, 2.);
  const double actualFactor = duration / newDuration;
  const double tPivot = m_viewStart + double(xModel - m_xStart) / double(m_xScale);
  m_viewStart = tPivot - (tPivot - m_viewStart) / actualFactor;
  m_viewEnd   = m_viewStart + newDuration;
  m_xScale    = float(m_xScale * actualFactor); // the timeline keeps its width on screen
  view_clamp();
}

void profiler::view_pan(float dxModel)
{
  const double dt = -double(dxModel) / double(m_xScale);
  m_viewStart += dt;
  m_viewEnd   += dt;
  view_clamp();
}

void profiler::view_clamp()
{
  // keep the center of the view inside the recorded data
  const double center = 0.5 * (m_viewStart + m_viewEnd);
  const double shift = (center < m_recordsStart) ? m_recordsStart - center : (center > m_recordsEnd) ? m_recordsEnd - center : 0.;
  m_viewStart += shift;
  m_viewEnd   += shift;
}

void profiler::view_reset()
{
  m_viewStart = m_frameStart;
  m_viewEnd   = m_frameEnd;
  m_xScale    = float(1.5 / std::max(m_frameEnd - m_frameStart, 1.e-6));
}

bool profiler::acceptEvent(const SDL_Event &event)
{
  const bool paused = m_paused.load(std::memory_order_relaxed);
  const bool enabled = m_enabled.load(std::memory_order_relaxed);

  switch(event.type)
  {
  case SDL_KEYDOWN:
    if (event.key.keysym.scancode == SDL_SCANCODE_B) // scancode: physical location of the keyboard key
    {
      m_paused.store(!paused, std::memory_order_relaxed);
      m_dragging = false;
      return true;
    }
    if (event.key.keysym.scancode == SDL_SCANCODE_V)
    {
      m_enabled.store(!enabled, std::memory_order_relaxed);
      m_dragging = false;
      return true;
    }
    break;

  case SDL_MOUSEBUTTONDOWN:
  {
    if (!paused || !enabled) break;
    const glm::vec2 pos = mouseToModel(glm::ivec2(event.button.x, event.button.y));
    if (!isInTimeline(pos)) break;
    if (event.button.button == SDL_BUTTON_LEFT) // start panning
    {
      m_dragging = true;
      m_dragLastX = pos.x;
      m_hoveredRecord = -1;
      return true;
    }
    if (event.button.button == SDL_BUTTON_RIGHT) // reset the view
    {
      view_reset();
      m_hoveredRecord = -1;
      return true;
    }
    break;
  }

  case SDL_MOUSEBUTTONUP:
    if (event.button.button == SDL_BUTTON_LEFT && m_dragging)
    {
      m_dragging = false;
      acceptEvent(glm::ivec2(event.button.x, event.button.y)); // hover again
      return true;
    }
    break;

  case SDL_MOUSEMOTION:
    if (m_dragging)
    {
      const glm::vec2 pos = mouseToModel(glm::ivec2(event.motion.x, event.motion.y));
      view_pan(pos.x - m_dragLastX);
      m_dragLastX = pos.x;
      m_mousePosition = pos;
      return true;
    }
    return acceptEvent(glm::ivec2(event.motion.x, event.motion.y));

  case SDL_MOUSEWHEEL:
  {
    if (!paused || !enabled || !isInTimeline(m_mousePosition)) break; // m_mousePosition: from the last motion event
    int dy = event.wheel.y;
    if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) dy = -dy;
    if (dy == 0) break;
    view_zoom(m_mousePosition.x, std::pow(1.25, double(dy)));
    updateHover();
    return true;
  }
  }
  return false;
}

bool profiler::acceptEvent(const glm::ivec2 &mousePosition)
{
  if (!m_paused.load(std::memory_order_relaxed) || !m_enabled.load(std::memory_order_relaxed))
    return false;

  m_mousePosition = mouseToModel(mousePosition);
  return updateHover();
}

bool profiler::updateHover()
{
  m_hoveredRecord = -1;
  if (m_dragging) return false;

  const float minWidth = 2.f * pixelSizeModel();

  for (std::size_t irec = 0; irec < m_records.size(); ++irec) // the last hit is the one drawn on top
  {
    const s_record &rec = m_records[irec];
    if (!isVisible(rec)) continue;
    const glm::vec4 AABB = recordAABB(rec);
    const float x1 = std::max(AABB.z, AABB.x + minWidth);
    if (AABB.x <= m_mousePosition.x && m_mousePosition.x <= x1 &&
        AABB.y <= m_mousePosition.y && m_mousePosition.y <= AABB.w)
      m_hoveredRecord = int(irec);
  }
  return m_hoveredRecord != -1;
}

// ============================================================================

int profiler::rowOfThread(int threadIndex) const
{
  // the frame thread is always the first row
  if (m_frameThreadIndex < 0) return threadIndex;
  if (threadIndex == m_frameThreadIndex) return 0;
  return (threadIndex < m_frameThreadIndex) ? threadIndex + 1 : threadIndex;
}

int profiler::threadOfRow(int row) const
{
  if (m_frameThreadIndex < 0) return row;
  if (row == 0) return m_frameThreadIndex;
  return (row <= m_frameThreadIndex) ? row - 1 : row;
}

glm::vec4 profiler::recordAABB(const s_record &rec, int *clipFlags) const
{
  const double t0 = std::max(rec.m_start, m_viewStart);
  const double t1 = std::max(std::min(rec.end(), m_viewEnd), t0);
  const int    row = rowOfThread(rec.m_threadIndex);
  const int    nLevels = (row >= 0 && std::size_t(row) < m_rowLevels.size()) ? m_rowLevels[row] : k_minLevels;
  const int    level = std::min(int(rec.m_level), nLevels - 1);
  const float  dYlevel = m_dYthread / float(nLevels);
  const float  y0 = m_yStart + float(row) * m_dYthread + float(level) * dYlevel;
  if (clipFlags != nullptr)
    *clipFlags = ((rec.m_start < m_viewStart) ? CLIP_LEFT : 0) | ((rec.end() > m_viewEnd) ? CLIP_RIGHT : 0);
  return glm::vec4(xOfTime(t0), y0, xOfTime(t1), y0 + dYlevel);
}

float profiler::pixelSizeModel() const
{
  const glm::mat3 m = m_PV * m_matModel;
  if (m_viewportSize.x <= 0.f || std::abs(m[0][0]) < 1.e-12f) return 0.f;
  return 2.f / (m_viewportSize.x * std::abs(m[0][0]));
}

// ============================================================================

void profiler::draw() const
{
  if (!m_enabled.load(std::memory_order_relaxed) || !m_geometryReady)
    return;

  TRE_ASSERT(m_model.isLoadedGPU());
  TRE_ASSERT(m_shader != nullptr &&  m_shader->layout().category == shader::PRGM_2D);

  glEnable(GL_BLEND);

  glUseProgram(m_shader->m_drawProgram);

  m_shader->setUniformMatrix(m_PV * m_matModel);

  glUniform1i(m_shader->getUniformLocation(shader::TexDiffuse), 0);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_whiteTexture->m_handle);

  m_model.drawcall(m_partTri, 1, true);
  m_model.drawcall(m_partLine, 1, false, GL_LINES);
  if (m_tooltipVisible) m_model.drawcall(m_partTooltip, 1, false); // over the grid lines

  glBindTexture(GL_TEXTURE_2D, m_font->get_texture().m_handle);

  m_model.drawcall(m_partText, 1, false);
}

bool profiler::loadIntoGPU(font *fontToUse)
{
  TRE_ASSERT(!m_model.isLoadedGPU());
  TRE_ASSERT(fontToUse != nullptr);

  m_font = fontToUse;

  m_model.loadIntoGPU();

  m_whiteTexture = new texture();
  m_whiteTexture->loadWhite();

  return true;
}

void profiler::updateIntoGPU()
{
  if (!m_enabled.load(std::memory_order_relaxed))
    return;

  TRE_ASSERT(m_model.isLoadedGPU());

  compute_data();

  m_model.updateIntoGPU();
}

void profiler::clearGPU()
{
  m_font = nullptr;
  m_geometryReady = false;

  m_model.clearGPU();

  if (m_whiteTexture != nullptr)
  {
    m_whiteTexture->clear();
    delete m_whiteTexture;
    m_whiteTexture = nullptr;
  }
}

bool profiler::loadShader(shader *shaderToUse)
{
  TRE_ASSERT(m_shader == nullptr);
  if (shaderToUse != nullptr)
  {
    m_shader = shaderToUse;
    m_shaderOwner = false;
  }
  else
  {
    m_shader = new shader;
    m_shaderOwner = true;
    return m_shader->loadShader(shader::PRGM_2D, shader::PRGM_COLOR | shader::PRGM_TEXTURED, "profiler");
  }
  return true;
}

void profiler::clearShader()
{
  if (m_shaderOwner && m_shader != nullptr)
  {
    m_shader->clearShader();
    delete m_shader;
  }
  m_shader = nullptr;
  m_shaderOwner = true;
}

// ----------------------------------------------------------------------------

void profiler::compute_data()
{
  static const glm::vec4 colorGridPrimary = glm::vec4(0.0f, 0.7f, 0.0f, 1.0f);
  static const glm::vec4 colorGridSecond  = glm::vec4(0.0f, 0.7f, 0.0f, 0.6f);
  static const glm::vec4 colorFrameStart  = glm::vec4(1.0f, 1.0f, 1.0f, 0.4f);
  static const glm::vec4 colorFrameEnd    = glm::vec4(1.0f, 1.0f, 1.0f, 0.8f);
  static const glm::vec4 colorClipMarker  = glm::vec4(0.0f, 0.0f, 0.0f, 0.85f);

  const bool paused = m_paused.load(std::memory_order_relaxed);

  if (m_viewEnd - m_viewStart < 1.e-6) m_viewEnd = m_viewStart + 1.e-6;
  const double viewDuration = m_viewEnd - m_viewStart;

  // -- scales (with hysteresis; while paused, the horizontal scale is driven by the zoom) --
  if (!paused)
  {
    const double w = viewDuration * m_xScale;
    if (!(w > 0.5 && w < 2.5)) m_xScale = float(1.5 / viewDuration);
  }
  {
    float maxframetime = 0.f;
    for (const s_frame &frame : m_recordsOverFrames) maxframetime = std::max(maxframetime, frame.m_duration);
    if (maxframetime > 1.e-6f)
    {
      const float h = maxframetime * m_yScaleFrames;
      if (!(h > 0.4f && h < 0.8f)) m_yScaleFrames = 0.6f / maxframetime;
    }
  }

  // -- layout --
  // time grid: 1-2-5 steps, about 10 lines over the view, aligned on the start of the collected frame
  const double    gridStep  = _niceStep(viewDuration / 10.);
  const long long gridFirst = (long long)(std::ceil ((m_viewStart - m_frameStart) / gridStep - 1.e-9));
  const long long gridLast  = (long long)(std::floor((m_viewEnd   - m_frameStart) / gridStep + 1.e-9));
  const std::size_t nGrid   = (gridLast >= gridFirst) ? std::size_t(gridLast - gridFirst + 1) : 0;
  const float       xEnd    = xOfTime(m_viewEnd);

  const int nRow = std::max(1, threadCount());
  m_rowLevels.assign(std::size_t(nRow), k_minLevels);
  for (const s_record &rec : m_records)
  {
    const int row = rowOfThread(rec.m_threadIndex);
    if (row >= 0 && row < nRow) m_rowLevels[row] = std::max(m_rowLevels[row], int(rec.m_level) + 1);
  }
  const float yEnd = m_yStart + float(nRow) * m_dYthread;

  const float pixelX  = pixelSizeModel();
  const float dXframe = (pixelX > 0.f) ? pixelX : 0.002f;
  const float yFrame0 = yEnd + m_dYthread;
  const float y1ms    = yFrame0 + 0.001f * m_yScaleFrames;
  const float y10ms   = yFrame0 + 0.010f * m_yScaleFrames;

  // -- visible records and frame boundaries (counted first, to size the parts exactly) --
  struct s_visible { std::size_t m_index; glm::vec4 m_AABB; int m_clip; };
  std::vector<s_visible> visibles;
  std::size_t clipMarkerCount = 0;
  for (std::size_t irec = 0; irec < m_records.size(); ++irec)
  {
    const s_record &rec = m_records[irec];
    if (!isVisible(rec)) continue;
    s_visible v;
    v.m_index = irec;
    v.m_AABB = recordAABB(rec, &v.m_clip);
    if (pixelX > 0.f) v.m_AABB.z = std::max(v.m_AABB.z, v.m_AABB.x + pixelX); // at least 1 pixel wide
    clipMarkerCount += ((v.m_clip & CLIP_LEFT) ? 1 : 0) + ((v.m_clip & CLIP_RIGHT) ? 1 : 0);
    visibles.push_back(v);
  }

  std::size_t frameMarkerCount = 0;
  for (const s_frame &frame : m_recordsOverFrames)
  {
    if (frame.m_duration <= 0.f) continue;
    if (frame.m_start >= m_viewStart && frame.m_start <= m_viewEnd) ++frameMarkerCount;
    if (frame.end()   >= m_viewStart && frame.end()   <= m_viewEnd) ++frameMarkerCount;
  }

  // -- texts --
  struct s_text { char m_text[64]; glm::vec2 m_pos; float m_size; };
  std::vector<s_text> texts;
  texts.reserve(std::size_t(nRow) + nGrid / 2 + 8);
  auto addText = [&texts](const glm::vec2 &pos, float size) -> s_text & // the caller fills m_text
  {
    texts.emplace_back();
    texts.back().m_text[0] = 0;
    texts.back().m_pos = pos;
    texts.back().m_size = size;
    return texts.back();
  };

  for (int row = 0; row < nRow; ++row)
  {
    const int  ithread = threadOfRow(row);
    const bool dead = ithread < threadCount() && m_contexts[ithread].m_state.load(std::memory_order_relaxed) != s_context::STATE_ALIVE;
    s_text &label = addText(glm::vec2(m_xTitle + 0.01f, m_yStart + float(row + 1) * m_dYthread), m_dYthread);
    std::snprintf(label.m_text, sizeof(label.m_text), dead ? "%s (exited)" : "%s", threadName(ithread));
  }

  for (long long k = gridFirst; k <= gridLast; ++k)
  {
    if (k % 2 != 0) continue;
    const double t = double(k) * gridStep;
    s_text &label = addText(glm::vec2(xOfTime(m_frameStart + t) - 0.5f * m_dYthread, m_yStart - 0.02f * m_dYthread), m_dYthread);
    std::snprintf(label.m_text, sizeof(label.m_text), "%g ms", (k == 0) ? 0. : t * 1000.);
  }

  if (paused)
    _copyString(addText(glm::vec2(m_xStart, yEnd + 0.8f * m_dYthread), m_dYthread * 0.5f).m_text, sizeof(s_text::m_text), "paused - wheel: zoom, drag: pan, right-click: reset");

  _copyString(addText(glm::vec2(m_xTitle, yFrame0 + m_dYthread * 0.5f), m_dYthread).m_text, sizeof(s_text::m_text), "frame");
  _copyString(addText(glm::vec2(m_xStart - 1.5f * m_dYthread, y1ms  + m_dYthread * 0.25f), m_dYthread * 0.5f).m_text, sizeof(s_text::m_text), "1ms");
  _copyString(addText(glm::vec2(m_xStart - 1.5f * m_dYthread, y10ms + m_dYthread * 0.25f), m_dYthread * 0.5f).m_text, sizeof(s_text::m_text), "10ms");

  char tooltip[512];
  tooltip[0] = 0;
  if (m_hoveredRecord >= 0 && std::size_t(m_hoveredRecord) < m_records.size())
  {
    const s_record &hrec = m_records[m_hoveredRecord];
    const auto      itMean = m_means.find(hrec.m_hash);
    const bool      spans = hrec.m_start < m_frameStart || hrec.end() > m_frameEnd;
    char path[256];
    buildPath(hrec, path, int(sizeof(path)));
    std::snprintf(tooltip, sizeof(tooltip), "%s\n%.3f ms (mean: %.3f ms, max: %.3f ms)\nstart: %+.3f ms%s",
                  path,
                  hrec.m_duration * 1000.,
                  (itMean != m_means.end()) ? itMean->second.m_duration * 1000. : 0.,
                  (itMean != m_means.end()) ? itMean->second.m_max * 1000. : 0.,
                  (hrec.m_start - m_frameStart) * 1000.,
                  spans ? "\n(spans across frames)" : "");
  }

  std::size_t textVertexCount = 0;
  for (const s_text &t : texts) textVertexCount += textgenerator::geometry_VertexCount(t.m_text);
  if (tooltip[0] != 0) textVertexCount += textgenerator::geometry_VertexCount(tooltip);

  // -- parts --
  std::size_t lineCount_frameGraph = 0;
  for (const s_frame &frame : m_recordsOverFrames) lineCount_frameGraph += 2 + 2 * frame.m_bars.size();

  const std::size_t lineCount = std::size_t(nRow + 1) * 2 + 2   // row separators + title column
                              + nGrid * 2                        // time grid
                              + frameMarkerCount * 2             // frame boundaries
                              + visibles.size() * 2              // record's left edge
                              + clipMarkerCount * 4              // chevrons on the clipped edges
                              + lineCount_frameGraph + 6;        // frame graph

  m_model.clearParts();
  m_partTri     = m_model.createPart(visibles.size() * 6);
  m_partLine    = m_model.createPart(lineCount);
  m_partTooltip = m_model.createPart(6);
  m_partText    = m_model.createPart(textVertexCount);
  m_model.colorizePart(m_partText, glm::vec4(0.f));

  std::size_t offsetLine = 0;
  std::size_t offsetTri = 0;
  std::size_t offsetText = 0;

  auto addLine = [&](float x0, float y0, float x1, float y1, const glm::vec4 &color)
  {
    m_model.fillDataLine(m_partLine, offsetLine, x0, y0, x1, y1, color);
    offsetLine += 2;
  };

  // -- grid --
  for (int j = 0; j <= nRow; ++j)
  {
    const float y = m_yStart + float(j) * m_dYthread;
    addLine(m_xTitle, y, xEnd, y, colorGridPrimary);
  }
  addLine(m_xTitle, m_yStart, m_xTitle, yEnd, colorGridPrimary);

  for (long long k = gridFirst; k <= gridLast; ++k)
  {
    const float x = xOfTime(m_frameStart + double(k) * gridStep);
    addLine(x, m_yStart, x, yEnd, (k % 2 == 0) ? colorGridPrimary : colorGridSecond);
  }

  for (const s_frame &frame : m_recordsOverFrames)
  {
    if (frame.m_duration <= 0.f) continue;
    if (frame.m_start >= m_viewStart && frame.m_start <= m_viewEnd) addLine(xOfTime(frame.m_start), m_yStart, xOfTime(frame.m_start), yEnd, colorFrameStart);
    if (frame.end()   >= m_viewStart && frame.end()   <= m_viewEnd) addLine(xOfTime(frame.end()), m_yStart, xOfTime(frame.end()), yEnd, colorFrameEnd);
  }

  // -- recorded zones --
  for (const s_visible &v : visibles)
  {
    const glm::vec4 &AABB = v.m_AABB;
    glm::vec4 color = _unpackColor(m_records[v.m_index].m_color);
    if (m_hoveredRecord == int(v.m_index)) color = color * 0.5f + 0.5f;

    m_model.fillDataRectangle(m_partTri, offsetTri, AABB, color, glm::vec4(0.f));
    offsetTri += 6;

    addLine(AABB.x, AABB.y, AABB.x, AABB.w, color * 0.5f);

    // chevrons: the record continues outside the view
    const float h   = AABB.w - AABB.y;
    const float yM  = 0.5f * (AABB.y + AABB.w);
    const float yA  = AABB.y + 0.2f * h;
    const float yB  = AABB.w - 0.2f * h;
    const float w   = 0.3f * h;
    const float pad = 0.1f * h;
    if (v.m_clip & CLIP_LEFT)
    {
      addLine(AABB.x + pad, yM, AABB.x + pad + w, yA, colorClipMarker);
      addLine(AABB.x + pad, yM, AABB.x + pad + w, yB, colorClipMarker);
    }
    if (v.m_clip & CLIP_RIGHT)
    {
      addLine(AABB.z - pad, yM, AABB.z - pad - w, yA, colorClipMarker);
      addLine(AABB.z - pad, yM, AABB.z - pad - w, yB, colorClipMarker);
    }
  }

  // -- frame graph --
  {
    const float x0 = m_xStart;
    const float xN = m_xStart + float(k_frameCount - 1) * dXframe;

    for (unsigned iF = 0; iF < k_frameCount; ++iF)
    {
      const s_frame &frame = m_recordsOverFrames[(1 + iF + m_frameIndex) % k_frameCount]; // oldest first
      const float xFT = x0 + float(iF) * dXframe;
      float accT = 0.f;
      for (const s_frame::s_bar &bar : frame.m_bars)
      {
        const float yA = yFrame0 + accT * m_yScaleFrames;
        const float yB = yA + bar.m_duration * m_yScaleFrames;
        addLine(xFT, yA, xFT, yB, _unpackColor(bar.m_color));
        accT += bar.m_duration;
      }
      const float yFT = yFrame0 + frame.m_duration * m_yScaleFrames;
      const float green = std::exp(-frame.m_duration * 100.f);
      const float blue  = std::exp(-frame.m_duration * 600.f);
      addLine(xFT, yFrame0, xFT, yFT, glm::vec4(1.f, green, blue, 0.4f));
    }
    addLine(x0, yFrame0, xN, yFrame0, glm::vec4(0.f, 0.f, 1.f, 0.8f));
    addLine(x0, y1ms,    xN, y1ms,    glm::vec4(1.f, std::exp(-0.1f), std::exp(-0.6f), 0.6f));
    addLine(x0, y10ms,   xN, y10ms,   glm::vec4(1.f, std::exp(-1.0f), std::exp(-6.0f), 0.4f));
  }
  TRE_ASSERT(offsetLine == lineCount);
  TRE_ASSERT(offsetTri == visibles.size() * 6);

  // -- texts --
  for (const s_text &t : texts)
  {
    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, t.m_text, t.m_pos);
    txtInfo.setupSize(t.m_size);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(t.m_text);
  }

  // -- tooltip --
  m_tooltipVisible = (tooltip[0] != 0);
  if (m_tooltipVisible)
  {
    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, tooltip);
    txtInfo.setupSize(m_dYthread);

    textgenerator::s_textInfoOut txtInfoOut;
    textgenerator::generate(txtInfo, nullptr, 0, 0, &txtInfoOut); // without mesh

    txtInfo.m_zone = glm::vec4(m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y,
                               m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);

    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(tooltip);

    const glm::vec4 AABB(m_mousePosition.x, m_mousePosition.y,
                         m_mousePosition.x + txtInfoOut.m_maxboxsize.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);
    m_model.fillDataRectangle(m_partTooltip, 0, AABB, glm::vec4(0.f, 0.f, 0.f, 0.7f), glm::vec4(0.f));
  }
  TRE_ASSERT(offsetText == textVertexCount);

  m_geometryReady = true;
}

// ============================================================================

} // namespace

#endif // TRE_PROFILE
