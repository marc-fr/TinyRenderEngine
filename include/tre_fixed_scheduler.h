#pragma once

// Fixed-size free-running task scheduler
// !! IMPORTANT !!
// Use "#define FS_THREADED" before including this file to enable threading.
// Otherwise, the plain inline mode is used.

#include "tre_utils.h" // TRE_ASSERT, TRE_LOG, tre::span

#include <algorithm>
#include <array>

#ifdef TRE_PROFILE
#include <chrono>
#endif

#ifdef FS_THREADED
#include <atomic>
#include <chrono>
#include <thread>
#endif

#if defined(_MSC_VER)
#include <intrin.h> // _BitScanForward
#endif

namespace tre
{

namespace detail
{

inline unsigned ctz32(uint32_t x) noexcept // x != 0
{
#if defined(_MSC_VER)
  unsigned long i = 0;
  _BitScanForward(&i, x);
  return static_cast<unsigned>(i);
#else
  return static_cast<unsigned>(__builtin_ctz(x));
#endif
}

inline unsigned popcount32(uint32_t x) noexcept
{
  unsigned n = 0;
  for (; x; x &= x - 1) ++n;
  return n;
}

} // namespace detail


/// Fixed-size task scheduler, free-running lanes
/// - NThreads: nbr of threads (<= 8), main-thread included: lane 0 = the thread calling tick(), lanes 1..N-1 = workers
/// - MaxTasks: capacity (<= 32). Max size of the task table
/// - task-rate: a task of rate r runs once every 2^r slots of its lane (0 = every slot, 1 = every other slot, ...)
/// Each lane owns its schedule and task table (a worker gets its copy at start). Lanes are not synchronized
/// A lane cycles through kSlots = 2^MaxRate slots. On each slot it runs its due tasks (in table order)
/// A worker runs a slot, yields, runs the next one. No frame sync.
/// Tasks are independent and light, as it can run on the main thread (notably without FS_THREADED).
template <unsigned NThreads, unsigned MaxTasks>
class fixedScheduler
{
public:
  static constexpr unsigned MaxRate = 4;
  static constexpr unsigned kSlots  = 1u << MaxRate;

  static_assert(NThreads >= 1 && NThreads <= 8, "at most 8 threads (main included)");
  static_assert(MaxTasks >= 1 && MaxTasks <= 32, "at most 32 tasks");
  static_assert(MaxRate <= 4, "at most 4 rates");

  using Fn = void (*)(void*);
  static constexpr unsigned kThreads = NThreads;

#ifdef FS_THREADED
  static constexpr unsigned kLanes = NThreads;
#else
  static constexpr unsigned kLanes = 1;
#endif

  /// One entry of the task table, owned by the caller.
  struct s_task
  {
    Fn          fn   = nullptr;
    void*       ctx  = nullptr;
    uint8_t     rate = 0;       // runs every 2^rate slots of its lane
    uint8_t     lane = 0;       // thread-id (0 = main thread). Ignored in inline mode
    const char* name = nullptr; // reporting only
  };

  /// What the run loop touches
  struct s_hot
  {
    Fn    fn  = nullptr;
    void* ctx = nullptr;
  };

  /// A lane's own schedule and task table
  struct s_lane
  {
    std::array<uint32_t, kSlots> due {}; // due[slot] = tasks to run on that slot
    std::array<s_hot, MaxTasks>  hot {};

    unsigned load(unsigned slot) const noexcept { return detail::popcount32(due[slot & (kSlots - 1)]); }
    uint32_t tasks() const noexcept
    {
      uint32_t m = 0;
      for (uint32_t d : due) m |= d;
      return m;
    }
  };

#ifdef TRE_PROFILE
  /// Profiling stats
  struct alignas(64) s_stats
  {
    static constexpr bool kEnabled = true;

    uint64_t calls   = 0;
    uint64_t totalNs = 0;
    uint64_t maxNs   = 0;

    static uint64_t now() noexcept
    {
      using namespace std::chrono;
      return static_cast<uint64_t>(duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
    }
    void record(uint64_t ns) noexcept
    {
      ++calls;
      totalNs += ns;
      if (ns > maxNs) maxNs = ns;
    }
    float avgUs() const { return calls ? float(totalNs) / float(calls) * 1e-3f : 0.f; }
    float maxUs() const { return float(maxNs) * 1e-3f; }
  };
  struct s_laneStats : s_stats
  {
    std::array<uint32_t, kSlots> due {}; // copy of the lane's schedule, for logLoad()
  };
#else
  struct s_stats
  {
    static constexpr bool     kEnabled = false;
    static constexpr uint64_t calls    = 0;
    static constexpr uint64_t totalNs  = 0;
    static constexpr uint64_t maxNs    = 0;

    static uint64_t now() noexcept { return 0; }
    void  record(uint64_t) noexcept {}
    float avgUs() const { return 0.f; }
    float maxUs() const { return 0.f; }
  };
  struct s_laneStats : s_stats {};
#endif

  fixedScheduler() = default;
  fixedScheduler(const fixedScheduler&)            = delete;
  fixedScheduler& operator=(const fixedScheduler&) = delete;
  ~fixedScheduler() { stop(); }

  /// Build lane's schedule
  static s_lane makeLane(span<s_task> tasks, unsigned l) noexcept
  {
    s_lane   L;
    uint32_t slotLoad[kSlots] = {};
    for (unsigned i = 0; i < tasks.size(); ++i)
    {
      const s_task& t = tasks[i];
      if (laneOf(t) != l) continue;
      const unsigned period = 1u << t.rate;

      unsigned best    = 0;
      uint32_t bestMax = UINT32_MAX;
      uint32_t bestSum = UINT32_MAX;
      for (unsigned ph = 0; ph < period; ++ph)
      {
        uint32_t mx  = 0;
        uint32_t sum = 0;
        for (unsigned s = ph; s < kSlots; s += period)
        {
          mx = std::max(mx, slotLoad[s]);
          sum += slotLoad[s];
        }
        if (mx < bestMax || (mx == bestMax && sum < bestSum))
        {
          best    = ph;
          bestMax = mx;
          bestSum = sum;
        }
      }

      L.hot[i] = {t.fn, t.ctx};
      for (unsigned s = best; s < kSlots; s += period)
      {
        ++slotLoad[s];
        L.due[s] |= uint32_t(1) << i;
      }
    }
    return L;
  }

  /// Setup, Validate, Start
  bool start(span<s_task> tasks)
  {
    // check (debug only)
    TRE_ASSERT(tasks.size() <= MaxTasks);
    for (size_t i = 0; i < tasks.size(); ++i)
    {
      const s_task& t = tasks[i];
      TRE_ASSERT(t.fn != nullptr && t.rate <= MaxRate && t.lane < NThreads);
    }

    m_main = makeLane(tasks, 0);
#ifdef TRE_PROFILE
    m_laneStats[0].due = m_main.due;
#endif

#ifdef FS_THREADED
    for (unsigned l = 1; l < kLanes; ++l)
    {
      const s_lane L = makeLane(tasks, l);
      if (!L.tasks()) continue; // no thread for an empty lane
#ifdef TRE_PROFILE
      m_laneStats[l].due = L.due;
#endif
      m_threads[l - 1] = std::thread([this, l, L] { workerLoop(l, L); });
    }
#endif
    return true;
  }

  /// Run lane 0's next slot, inline. No-op while paused.
  void tick() noexcept
  {
    if (m_state & kPaused) return;
    runlane(0, m_main, m_slot);
    m_slot = (m_slot + 1) & (kSlots - 1);
  }

  /// Lane 0 stops ticking at once, each worker pauses after completing its current slot
  void pause() noexcept
  {
    m_state |= kPaused;
    publish();
  }

  /// Un-pause, and every lane restarts at slot 0
  void reset() noexcept
  {
    m_state = (m_state & ~kPaused) + kEpoch;
    m_slot  = 0;
    publish();
  }

  bool paused() const noexcept { return m_state & kPaused; }

  /// Terminate: workers exit after their current slot, then are joined.
  void stop() noexcept
  {
    m_state |= kQuit;
    publish();
#ifdef FS_THREADED
    for (auto& t : m_threads)
    {
      if (t.joinable()) t.join();
    }
#endif
  }

  /// Stats are written by the lanes, unsynchronized
  s_stats     taskStats(unsigned i) const noexcept { return m_taskStats[i]; }
  s_laneStats laneStats(unsigned l) const noexcept { return m_laneStats[l]; }

  void logLoad() const
  {
#ifdef TRE_PROFILE
    char   txt[4096];
    size_t n = 0;

    for (unsigned l = 0; l < kLanes; ++l)
    {
      const auto& due = m_laneStats[l].due;
      n += std::snprintf(txt + n, sizeof(txt) - n, "\nthread-%u:", l);
      for (unsigned s = 0; s < kSlots; ++s) n += std::snprintf(txt + n, sizeof(txt) - n, " %u:%08X", detail::popcount32(due[s]), due[s]);
    }

    TRE_LOG("fixedScheduler: load balance:" << txt);
#else
    TRE_LOG("fixedScheduler: cannot log load, profiling disabled");
#endif
  }

  void logStats() const
  {
#ifdef TRE_PROFILE
    char   txt[4096];
    size_t n = 0;

    for (unsigned l = 0; l < kLanes; ++l)
    {
      const s_laneStats s = laneStats(l);
      n += std::snprintf(txt + n, sizeof(txt) - n, "\nthread-%u: %.3e slots, avg=%6.2f ms, max=%6.2f ms",
                         l, float(s.calls), s.avgUs() * 1.e-3f, s.maxUs() * 1.e-3f);
    }

    for (unsigned i = 0; i < MaxTasks; ++i)
    {
      const s_stats s = taskStats(i);
      if (s.calls == 0) break; // crude way to prune tasks
      n += std::snprintf(txt + n, sizeof(txt) - n, "\ntask--%02u: %.3e calls, avg=%6.2f ms, max=%6.2f ms",
                         i, float(s.calls), s.avgUs() * 1.e-3f, s.maxUs() * 1.e-3f);
    }

    TRE_LOG("fixedScheduler: stats:" << txt);
#else
    TRE_LOG("fixedScheduler: cannot log stats, profiling disabled");
#endif
  }

private:
  /// Control word: bit 0 = paused, bit 1 = quit, bits 2.. = reset epoch
  static constexpr uint32_t kPaused = 1u;
  static constexpr uint32_t kQuit   = 2u;
  static constexpr uint32_t kEpoch  = 4u;

  void publish() noexcept
  {
#ifdef FS_THREADED
    m_ctrl.store(m_state, std::memory_order_relaxed);
#endif
  }

  static unsigned laneOf(const s_task& t) noexcept
  {
#ifdef FS_THREADED
    return t.lane;
#else
    (void)t;
    return 0;
#endif
  }

  void runlane(unsigned l, const s_lane& L, unsigned slot) noexcept
  {
    uint32_t m = L.due[slot];
    if (!m) return;
    const uint64_t t0   = s_stats::now();
    uint64_t       prev = t0;
    while (m)
    {
      const unsigned i = detail::ctz32(m);
      m &= m - 1;
      L.hot[i].fn(L.hot[i].ctx); // call of the task's function here !
      const uint64_t t = s_stats::now();
      m_taskStats[i].record(t - prev);
      prev = t;
    }
    m_laneStats[l].record(prev - t0);
  }

#ifdef FS_THREADED
  void workerLoop(unsigned l, const s_lane &L) noexcept
  {
    unsigned slot  = 0;
    uint32_t epoch = m_ctrl.load(std::memory_order_relaxed) & ~(kPaused | kQuit);
    while (true)
    {
      const uint32_t c = m_ctrl.load(std::memory_order_relaxed);
      if (c & kQuit) return;
      if ((c & ~(kPaused | kQuit)) != epoch) // reset: back to slot 0
      {
        epoch = c & ~(kPaused | kQuit);
        slot  = 0;
      }
      if (c & kPaused)
      {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        continue;
      }
      runlane(l, L, slot);
      slot = (slot + 1) & (kSlots - 1);
      std::this_thread::yield(); // release pressure between slots
    }
  }

  alignas(64) std::atomic<uint32_t>   m_ctrl = 0; // control word, read by the workers
  std::array<std::thread, kLanes - 1> m_threads;
#endif

  s_lane                            m_main;           // lane 0, main-thread only
  unsigned                          m_slot   = 0;     // lane 0's next slot
  uint32_t                          m_state  = 0;     // control word, main-thread copy
  std::array<s_stats, MaxTasks>     m_taskStats {};
  std::array<s_laneStats, kLanes>   m_laneStats {};
};

// ============================================================================

} // namespace tre
