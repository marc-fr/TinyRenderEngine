#include "tre_fixed_scheduler.h"

#include <string>

// ----------------------------------------------------------------------------

static int kNchecks = 0;
static int kNfailed = 0;
#define CHECK(cond)                                                                          \
  do                                                                                         \
  {                                                                                          \
    ++kNchecks;                                                                              \
    if (!(cond)) { ++kNfailed; TRE_LOG("  FAILED (" << #cond << ") at line " << __LINE__); } \
  } while (0)

static void section(const char* name) { TRE_LOG("== " << name); }
static void section(const char* name, bool threaded) { TRE_LOG("== " << name << (threaded ? " [threaded]" : " [inline]")); }

static bool nop(void*) { return false; } // never does any work

// Table entry from run-time values (rate and lane are uint8_t: brace-init from a variable would narrow).
template <class S>
static typename S::s_task task(typename S::Fn fn, void* ctx, unsigned rate, unsigned lane = 0, const char* name = nullptr)
{
  return typename S::s_task{fn, ctx, static_cast<uint8_t>(rate), static_cast<uint8_t>(lane), name};
}

template <class L>
static unsigned maxLoad(const L& lane)
{
  unsigned mx = 0;
  for (unsigned k = 0; k < lane.due.size(); ++k)
    if (lane.load(k) > mx) mx = lane.load(k);
  return mx;
}

template <class L>
static unsigned sumLoad(const L& lane)
{
  unsigned sum = 0;
  for (unsigned k = 0; k < lane.due.size(); ++k) sum += lane.load(k);
  return sum;
}

// Workers are not driven by tick(): wait (up to ~1s) for them to make progress.
template <class F>
static bool waitFor(F cond)
{
  for (int i = 0; i < 1000 && !cond(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  return cond();
}

static unsigned absDiff(unsigned a, unsigned b) { return a > b ? a - b : b - a; }

// ----------------------------------------------------------------------------

struct Agent
{
  uint64_t state = 1;
  unsigned budget;
  unsigned runs = 0;
  explicit Agent(unsigned b = 2000) : budget(b) {}
  static bool think(void* p)
  {
    Agent& a = *static_cast<Agent*>(p);
    ++a.runs;
    for (unsigned i = 0; i < a.budget; ++i) // stand-in for planning work
      a.state = a.state * 6364136223846793005ull + 1442695040888963407ull;
    return true;
  }
};

template <bool kThreaded>
static void testDemo()
{
  section("demo: agents on 2 free-running workers, main lane ticked", kThreaded);
  std::array<Agent, 8> agents;
  Agent planner{20000};
  int   uiTicks = 0;

  using Sched = tre::fixedScheduler<3, 16, kThreaded>; // main + 2 workers, 16-slot cycle, 12 of 16 tasks used
  typename Sched::s_task tasks[] =
  {
    { [](void* p) { ++*static_cast<int*>(p); return true; }, &uiTicks, 1, 0, "ui" }, // main, rate 1
    { &Agent::think, &planner  , 4, 1, "planner" },                      // w1, rate 4, heavy
    { &Agent::think, &agents[0], 2, 1, "agent.w1" },                     // w1, rate 2
    { &Agent::think, &agents[1], 2, 1, "agent.w1" },
    { &Agent::think, &agents[2], 2, 1, "agent.w1" },
    { &Agent::think, &agents[3], 2, 1, "agent.w1" },
    { &Agent::think, &agents[4], 2, 2, "agent.w2" },                     // w2, rate 2
    { &Agent::think, &agents[5], 2, 2, "agent.w2" },
    { &Agent::think, &agents[6], 2, 2, "agent.w2" },
    { &Agent::think, &agents[7], 2, 2, "agent.w2" },
    { nop, nullptr, 3, 2, "noop" },                                      // w2, rate 3
    { nop, nullptr, 0, 0, "frame-hook" },                                // main, rate 0
  };
  Sched sched;
  CHECK(sched.start(tasks));

  unsigned busyTicks = 0;
  for (int f = 0; f < 256; ++f)
  {
    busyTicks += sched.tick();
    std::this_thread::sleep_for(std::chrono::microseconds(500)); // "frame"
  }
  sched.stop();

  if (Sched::s_stats::kEnabled)
  {
    sched.logLoad();
    sched.logStats();
  }

  CHECK(uiTicks == 128); // lane 0 runs inline: always exact
  // threaded: lane 0 = "ui" + "frame-hook" (never works) -> idle every other tick
  // inline:   every task is on lane 0, and the 8 rate-2 agents cover every slot -> never idle
  const unsigned expectBusy = kThreaded ? 128 : 256;
  CHECK(busyTicks == expectBusy);
  if (Sched::s_stats::kEnabled) CHECK(sched.laneStats(0).idle == 256 - expectBusy);
  if constexpr (kThreaded)
  {
    // workers free-run: no absolute count, but within a lane the rates hold (rate 2 = 4x rate 4)
    CHECK(planner.runs > 0);
    for (unsigned i = 0; i < 4; ++i) CHECK(absDiff(agents[i].runs, 4 * planner.runs) <= 4);
    for (unsigned i = 4; i < 8; ++i) CHECK(absDiff(agents[i].runs, agents[4].runs) <= 1);
    CHECK(maxLoad(Sched::makeLane(tasks, 1)) == 2 && maxLoad(Sched::makeLane(tasks, 2)) == 2);
  }
  else
  {
    CHECK(planner.runs == 16);
    for (const Agent& a : agents) CHECK(a.runs == 64);
  }
}

// ----------------------------------------------------------------------------

namespace seq
{

std::string g_out;
bool a(void*) { g_out += 'A'; return true; }
bool b(void*) { g_out += 'B'; return true; }

void test()
{
  section("sequence: 2-tasks (A,B) on lane 0");
  using S = tre::fixedScheduler<1, 2>;
  {
    // B at rate 1 (half rate) -> (A B A) repeated
    S::s_task ab[] = { { a }, { b, nullptr, 1 } };
    S s;
    s.start(ab);
    for (int f = 0; f < 8; ++f) s.tick();
    TRE_LOG("  " << g_out);
    CHECK(g_out == "ABAABAABAABA");
  }
  g_out.clear();
  {
    // B at rate 2 (quarter rate) -> (A B A A) repeated
    S::s_task ab[] = { { a }, { b, nullptr, 2 } };
    S q;
    q.start(ab);
    for (int f = 0; f < 8; ++f) q.tick();
    TRE_LOG("  " << g_out);
    CHECK(g_out == "ABAAAABAAA");
  }
  g_out.clear();
  {
    // pause freezes lane 0, reset restarts it at slot 0
    S::s_task ab[] = { { a }, { b, nullptr, 1 } };
    S s;
    s.start(ab);
    for (int f = 0; f < 3; ++f) s.tick(); // slots 0,1,2: AB A AB
    s.pause();
    for (int f = 0; f < 3; ++f) s.tick(); // ignored
    s.reset();                            // resumes at slot 0
    for (int f = 0; f < 2; ++f) s.tick(); // slots 0,1: AB A
    TRE_LOG("  " << g_out);
    CHECK(g_out == "ABAAB" "ABA");
  }
}

} // namespace seq

// ----------------------------------------------------------------------------

static void testBalance()
{
  section("balance: phases follow the table order and spread the tasks");
  {
    // 2^r tasks of rate r fill each slot exactly once, for every r
    using S = tre::fixedScheduler<1, 16>;
    for (unsigned r = 0; r <= 4; ++r)
    {
      std::array<S::s_task, 16> t{};
      const unsigned n = 1u << r;
      for (unsigned i = 0; i < n; ++i) t[i] = task<S>(nop, nullptr, r);
      const S::s_lane L = S::makeLane({t.data(), n}, 0);
      bool flat = true;
      for (unsigned k = 0; k < S::kSlots; ++k) flat &= L.load(k) == 1;
      CHECK(flat);
    }
  }
  {
    // harmonic mix 1/2 + 1/4 + 1/8 + 1/16 + 1/16 = 1 task per slot, fastest first: perfect packing
    using S = tre::fixedScheduler<1, 5>;
    S::s_task t[] = { { nop, nullptr, 1 }, { nop, nullptr, 2 }, { nop, nullptr, 3 }, { nop, nullptr, 4 }, { nop, nullptr, 4 } };
    const S::s_lane L = S::makeLane(t, 0);
    CHECK(maxLoad(L) == 1);
    CHECK(sumLoad(L) == 16);
  }
}

// ----------------------------------------------------------------------------

namespace pausing
{

std::atomic<unsigned> g_a{0};
std::atomic<unsigned> g_b{0};
bool a(void*) { g_a.fetch_add(1, std::memory_order_relaxed); return true; }
bool b(void*) { g_b.fetch_add(1, std::memory_order_relaxed); return true; }

// pause() does not wait: wait until the counter stops moving (or ~1s)
static unsigned settle(const std::atomic<unsigned>& c)
{
  unsigned v = c.load();
  for (int i = 0; i < 200; ++i)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const unsigned w = c.load();
    if (w == v) break;
    v = w;
  }
  return v;
}

void test()
{
  section("pause / reset on a free-running worker (no sync)");
  using S = tre::fixedScheduler<2, 2, true>;
  S::s_task t[] = { { a, nullptr, 0, 1 }, { b, nullptr, 4, 1 } }; // lane 1: A every slot, B on slot 0
  S s;
  CHECK(s.start(t));
  CHECK(waitFor([] { return g_a.load() >= 100; })); // no tick(): the worker runs on its own

  s.pause(); // returns at once
  CHECK(s.paused());
  const unsigned a0 = settle(g_a);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(g_a.load() == a0); // parked
  const unsigned b0 = g_b.load();

  s.reset(); // un-pause, restart at slot 0
  CHECK(!s.paused());
  CHECK(waitFor([a0] { return g_a.load() >= a0 + 40; }));
  s.pause();
  const unsigned da = settle(g_a) - a0;
  const unsigned db = g_b.load() - b0;
  TRE_LOG("  " << da << " slots, B ran " << db << " times since reset");
  CHECK(db == (da + 15) / 16); // B is due on slot 0, the first one after reset
  s.stop();                    // from paused
}

} // namespace pausing

// ----------------------------------------------------------------------------

namespace idling
{

std::atomic<unsigned> g_idle{0};
std::atomic<unsigned> g_sparse{0};
bool idle(void*)   { g_idle.fetch_add(1, std::memory_order_relaxed); return false; }
bool sparse(void*) { g_sparse.fetch_add(1, std::memory_order_relaxed); return true; }

void test()
{
  section("idle: a worker sleeps after a slot whose tasks did nothing, not after an empty slot");
  using S = tre::fixedScheduler<3, 2, true>;
  S::s_task t[] =
  {
    { idle  , nullptr, 0, 1 }, // lane 1: due on every slot, never works -> sleeps after every slot
    { sparse, nullptr, 4, 2 }, // lane 2: works on slot 0, the 15 others are empty -> never sleeps
  };
  S s;
  const auto t0 = std::chrono::steady_clock::now();
  CHECK(s.start(t));
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  s.stop();
  const unsigned us = static_cast<unsigned>(
    std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
  const unsigned ni = g_idle.load(), ns = g_sparse.load();
  TRE_LOG("  " << us << " us: idle lane " << ni << " slots, sparse lane " << ns << " cycles");

  CHECK(ni > 0);
  CHECK(ni <= us / S::kIdleSleepUs + 1); // sleep_for() sleeps at least that long: hard upper bound
  CHECK(ns > us / S::kIdleSleepUs);      // more cycles than one sleep per cycle would allow
  if (S::s_stats::kEnabled)
  {
    CHECK(s.laneStats(1).idle == ni);
    CHECK(s.laneStats(2).idle == 0);
  }
}

} // namespace idling

// ----------------------------------------------------------------------------

int main(int, char**)
{
  using Any = tre::fixedScheduler<1, 1>;
  TRE_LOG("test mode: threaded + inline" << (Any::s_stats::kEnabled ? " + TRE_PROFILE" : ""));
  testDemo<true>();
  testDemo<false>();
  seq::test();
  testBalance();
  pausing::test();  // threaded only
  idling::test();   // threaded only
  TRE_LOG(kNchecks - kNfailed << "/" << kNchecks << " checks passed");
  return kNfailed;
}
