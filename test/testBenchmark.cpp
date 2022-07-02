#define GLM_FORCE_MESSAGES // be sure that GLM uses the correct target architecture.

#ifndef TRE_PRINTS
#define TRE_PRINTS // force prints
#endif

#include "utils.h"
#include "model.h"
#include "model_tools.h"
#include "contact_2D.h"

#include <chrono>
#include <functional>
#include <stdlib.h> // rand,srand
#include <time.h>   // time

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

typedef std::chrono::steady_clock systemclock;
typedef systemclock::time_point   systemtick;

// =============================================================================

template<typename _T>
_T midvalue(const std::vector<_T> &values)
{
  _T out = _T(0);
  for (const _T &v : values) out += v;
  return out / _T(values.size());
}

template<typename _T>
_T minvalue(const std::vector<_T> &values)
{
  _T out = values.front();
  for (const _T &v : values)
  {
    if (v < out) out = v;
  }
  return out;
}

// =============================================================================

void testSorting()
{
  static const unsigned listNsize = 4;
  static const unsigned listN[listNsize] = { 20, 5000, 600000, 3000000 };
  static const unsigned Ntries = 4;

  std::vector<double>   tElapsed(Ntries);
  std::vector<unsigned> data;
  data.reserve(listN[listNsize-1]);

  typedef  std::pair<void(*)(tre::span<unsigned>), std::string> func;
  const func listFuncs[] =
  {
    func(tre::sortQuick, "quick"),
    func(tre::sortFusion, "fusion"),
    func(tre::sortRadix, "radix"),
    func(tre::sortInsertion, "insertion"),
  };

  for (unsigned iN = 0; iN < listNsize; ++iN)
  {
    const unsigned N = listN[iN];
    data.resize(N);
    for (unsigned iF = 0, iFend = (N < 10000) ? 4 : 3; iF < iFend; ++iF)
    {
      const func &f = listFuncs[iF];

      for (unsigned iT = 0; iT < Ntries; ++iT)
      {
        // prepare
        for (unsigned &d : data) d = unsigned(std::rand());

        // sort
        systemtick tStart = systemclock::now();
        f.first(data);
        systemtick tEnd = systemclock::now();

        // end
        tElapsed[iT] = std::chrono::duration<double>(tEnd - tStart).count();

        bool isSorted = true;
        for (unsigned i = 1; i < N; ++i) isSorted &= (data[i-1] <= data[i]);
        TRE_ASSERT(isSorted);
      }

      const double tMid = 1000. * midvalue(tElapsed);
      const double tMin = 1000. * minvalue(tElapsed);
      std::printf("N = %7d, sort = %12s, tMid = %6.1f ms, tMin = %6.1f ms (%.2e)\n",
                  N, f.second.c_str(), tMid, tMin, tMin);
    }
  }
}

// =============================================================================

static inline float randFloat(float fMin, float fMax)
{
  static const float invMaxRand = 1.f / float(RAND_MAX);
  return fMin + (fMax - fMin) * float(std::rand()) * invMaxRand;
}

void testContact2D()
{
  static const glm::vec4 boxBase = glm::vec4(-1.f, -1.f, 1.f, 1.f);
  static const glm::vec2 circleBase_c = glm::vec2(0.f, 0.f);
  static const float     circleBase_r = 2.f;
  static const glm::vec2 triBaseA = glm::vec2(-1.f, -1.f);
  static const glm::vec2 triBaseB = glm::vec2( 1.f, -1.f);
  static const glm::vec2 triBaseC = glm::vec2( 0.f,  1.f);
  static const std::vector<glm::vec2> poly5Base = { glm::vec2(-1.f, -1.f), glm::vec2(0.f, -1.2f), glm::vec2(1.f, -1.f), glm::vec2(0.2f, 1.f), glm::vec2(-0.2f,  1.f) };

  struct s_contactReport
  {
    double   m_tElapsed;
    unsigned m_Count;
    float    m_percentageInside;

    unsigned NperSecond() const { return unsigned(double(m_Count) / m_tElapsed); }
  };

  // point .vs. surface
  {
    typedef std::function<bool(const glm::vec2 &pt)> func;
    typedef std::function<bool(tre::s_contact2D cntInfo, const glm::vec2 &pt)> funcInfo;
    struct s_Pt_Surface
    {
      func        m_func;
      funcInfo    m_funcInfo;
      std::string m_name;
      float       m_coverage;

      s_Pt_Surface(func f, funcInfo fInfo, const std::string &name, float coverage) : m_func(f),  m_funcInfo(fInfo), m_name(name), m_coverage(coverage) {}
    };

    const s_Pt_Surface listTests[] =
    {
      s_Pt_Surface( [](const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_box(pt, boxBase); },
                    [](tre::s_contact2D cntInfo, const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_box(cntInfo, pt, boxBase); },
                    "box", 0.25f ),
      s_Pt_Surface( [](const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_circle(pt, circleBase_c, circleBase_r); },
                    [](tre::s_contact2D cntInfo, const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_circle(cntInfo, pt, circleBase_c, circleBase_r); },
                    "circle", 0.0f ),
      s_Pt_Surface( [](const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_tri(pt, triBaseA, triBaseB, triBaseC); },
                    [](tre::s_contact2D cntInfo, const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_tri(cntInfo, pt, triBaseA, triBaseB, triBaseC); },
                    "triangle", 0.0f ),
      s_Pt_Surface( [](const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_poly(pt, poly5Base); },
                    [](tre::s_contact2D cntInfo, const glm::vec2 &pt) -> bool { return tre::s_contact2D::point_poly(cntInfo, pt, poly5Base); },
                    "poly5", 0.0f ),
    };

    static const unsigned Npts = 32000;
    std::vector<glm::vec2> pts(Npts);
    for (glm::vec2 &pt : pts) pt = glm::vec2(randFloat(-2.f, 2.f), randFloat(-2.f, 2.f));

    for (unsigned iT = 0; iT < 4; ++iT)
    {
      const s_Pt_Surface &curTest = listTests[iT];

      s_contactReport report1;
      {
        unsigned NptsInside = 0;

        systemtick tStart = systemclock::now();
        for (const glm::vec2 &pt : pts)
          NptsInside += curTest.m_func(pt) ? 1 : 0; // "NptsInside" may remove potential un-roll optimisation
        systemtick tEnd = systemclock::now();

        report1.m_Count = Npts;
        report1.m_tElapsed = std::chrono::duration<double>(tEnd - tStart).count();
        report1.m_percentageInside = float(NptsInside) / float(Npts);
      }

      s_contactReport report2;
      {
        unsigned NptsInside = 0;
        tre::s_contact2D cntInfo;

        systemtick tStart = systemclock::now();
        for (const glm::vec2 &pt : pts)
          NptsInside += curTest.m_funcInfo(cntInfo, pt) ? 1 : 0; // "cntInfo" will remove potential un-roll optimisation
        systemtick tEnd = systemclock::now();

        report2.m_Count = Npts;
        report2.m_tElapsed = std::chrono::duration<double>(tEnd - tStart).count();
        report2.m_percentageInside = float(NptsInside) / float(Npts);
      }

      TRE_ASSERT(fabsf(report1.m_percentageInside - report2.m_percentageInside) < 1.e-12f);

      std::printf("point in %8s, rate(NoInfo) = %6d k/ms, rate(WithInfo) = %6d k/ms, coverage: %.3f (expected %.3f)\n",
                  curTest.m_name.c_str(),
                  report1.NperSecond() / 1000000, report2.NperSecond() / 1000000,
                  double(report1.m_percentageInside), double(curTest.m_coverage));

    }
  }

  // polygon .vs. surface
  {
    typedef std::vector<glm::vec2> poly;
    typedef std::function<bool(const poly &pts)> func;
    typedef std::function<bool(tre::s_contact2D cntInfo, const poly &pts)> funcInfo;
    struct s_Poly_Surface
    {
      func        m_func;
      funcInfo    m_funcInfo;
      std::string m_name;

      s_Poly_Surface(func f, funcInfo fInfo, const std::string &name) : m_func(f),  m_funcInfo(fInfo), m_name(name) {}
    };

    const s_Poly_Surface listTests[] =
    {
      s_Poly_Surface( [](const poly &p) -> bool { return tre::s_contact2D::box_poly(boxBase, p); },
                      [](tre::s_contact2D cntInfo, const poly &p) -> bool { return tre::s_contact2D::box_poly(cntInfo, boxBase, p); },
                      "box"),
      s_Poly_Surface( [](const poly &p) -> bool { return tre::s_contact2D::circle_poly(circleBase_c, circleBase_r, p); },
                      [](tre::s_contact2D cntInfo, const poly &p) -> bool { return tre::s_contact2D::circle_poly(cntInfo, circleBase_c, circleBase_r, p); },
                      "circle"),
      s_Poly_Surface( [](const poly &p) -> bool { return tre::s_contact2D::ydown_poly(0.f, p); },
                      [](tre::s_contact2D cntInfo, const poly &p) -> bool { return tre::s_contact2D::ydown_poly(cntInfo, 0.f, p); },
                      "ydown"),
      s_Poly_Surface( [](const poly &p) -> bool { return tre::s_contact2D::poly_poly(poly5Base, p); },
                      [](tre::s_contact2D cntInfo, const poly &p) -> bool { return tre::s_contact2D::poly_poly(cntInfo, poly5Base, p); },
                      "poly5"),
    };

    static const unsigned Npolys = 0xFFF * 16;
    std::vector<poly> polys(Npolys);
    for (unsigned iP = 0, nP = 4; iP < Npolys; ++iP)
    {
      const glm::vec2 c = glm::vec2(randFloat(-2.f, 2.f), randFloat(-2.f, 2.f));
      const float     r = randFloat(0.2f, 1.f);
      poly & p = polys[iP];
      p.resize(nP); // hmm, that's a bad practice to use std::vector everywhere. Use a tre::span<> !! (std::span is C++20)
      for (unsigned i = 0; i < nP; ++i)
        p[i] = c + r * (1.f + 0.05f * randFloat(-1.f, 1.f)) * glm::vec2(cosf(6.28f * i / nP), sinf(6.28f * i / nP));
      if ((iP & 0xFFF) == 0) ++nP;
    }

    for (unsigned iT = 0; iT < 4; ++iT)
    {
      const s_Poly_Surface &curTest = listTests[iT];

      s_contactReport report1;
      {
        unsigned NptsInside = 0;

        systemtick tStart = systemclock::now();
        for (const poly &p : polys)
          NptsInside += curTest.m_func(p) ? 1 : 0; // "NptsInside" may remove potential un-roll optimisation
        systemtick tEnd = systemclock::now();

        report1.m_Count = Npolys;
        report1.m_tElapsed = std::chrono::duration<double>(tEnd - tStart).count();
        report1.m_percentageInside = float(NptsInside) / float(Npolys);
      }

      s_contactReport report2;
      {
        unsigned NptsInside = 0;
        tre::s_contact2D cntInfo;

        systemtick tStart = systemclock::now();
        for (const poly &p : polys)
          NptsInside += curTest.m_funcInfo(cntInfo, p) ? 1 : 0; // "cntInfo" will remove potential un-roll optimisation
        systemtick tEnd = systemclock::now();

        report2.m_Count = Npolys;
        report2.m_tElapsed = std::chrono::duration<double>(tEnd - tStart).count();
        report2.m_percentageInside = float(NptsInside) / float(Npolys);
      }

      // TODO: TRE_ASSERT(fabsf(report1.m_percentageInside - report2.m_percentageInside) < 1.e-12f);
      // Sometimes, the "with-info" algo fails to find a proper penetration, so it returns false even when there is contact.

      std::printf("poly in %8s, rate(NoInfo) = %6.1f k/ms, rate(WithInfo) = %6.1f k/ms, coverage: %.3f - %.3f\n",
                  curTest.m_name.c_str(),
                  double(report1.NperSecond() / 100000) * 0.1, double(report2.NperSecond() / 100000) * 0.1,
                  double(report1.m_percentageInside), double(report2.m_percentageInside));

    }
  }

}

// =============================================================================

int main(int argc, char **argv)
{
  std::srand(time(nullptr)); // random generator

  bool doSorting = true;
  bool doContact2D = true;

  for (int iarg = 1; iarg < argc; ++iarg)
  {
    if (std::strcmp(argv[iarg], "-no-s") == 0)
      doSorting = false;
    else if (std::strcmp(argv[iarg], "-no-c2D") == 0)
      doContact2D = false;
  }

  static const char* boolStr[2] = { "disabled", "enabled " };
  std::cout << "Info:" << std::endl
            << "- run of sort algorithms       : " << boolStr[doSorting  ] << " (-no-s   to disable)" << std::endl
            << "- run of 2D-contact algorithms : " << boolStr[doContact2D] << " (-no-c2D to disable)" << std::endl;

#ifdef TRE_DEBUG
  static const std::string msgPrefix = "Checking (and benchmark with debug info): ";
#else
  static const std::string msgPrefix = "Benchmark: ";
#endif

  if (doSorting)
  {
    std::cout << msgPrefix << "Sorting ..." << std::endl;
    testSorting();
  }

  if (doContact2D)
  {
    std::cout << msgPrefix << "Contact 2D ..." << std::endl;
    testContact2D();
  }

  std::cout << "Program finalized with success." << std::endl;

  return 0;
}
