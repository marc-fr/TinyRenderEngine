#include "tre_profiler.h"

#ifdef TRE_PROFILE

#include "tre_shader.h"
#include "tre_texture.h"
#include "tre_font.h"
#include "tre_textgenerator.h"

namespace tre {

// == Global variables ========================================================

profiler profilerRoot;

int              profilerThreadCount = 1;
thread_local int profilerThreadID = -1;

// == Helpers =================================================================

static float _hueFromColor(const glm::vec4 color)
{
  float vmax = color.r;
  float vmin = color.r;
  uint imax = 0;
  if (color.g > vmax) { vmax = color.g; imax = 1; }
  if (color.b > vmax) { vmax = color.b; imax = 2; }
  if (color.g < vmin) vmin = color.g;
  if (color.b < vmin) vmin = color.b;

  const float delta = vmax - vmin;
  if (delta < 1.e-3f) return 0.f;

  const float sat = delta / vmax;

  float hueXsat;
  if      (imax == 0) hueXsat = (color.g - color.b);
  else if (imax == 1) hueXsat = 2.f * sat + (color.b - color.r);
  else                hueXsat = 4.f * sat + (color.r - color.g);

  return hueXsat / (6.f * sat);
}

// ----------------------------------------------------------------------------

static glm::vec4 _colorFromHS(float hue /**< hue is in [0,1] range.*/, float sat /**< saturation is in [0,1] range */)
{
  hue = fmodf(hue, 1.f);
  TRE_ASSERT(sat > 0.f && sat <= 1.f);
  const float hueXsat = hue * 6.f * sat;
  // Convert HSV (hue, sat, value=1) -> RBG
  return glm::vec4( 1.f - glm::clamp(2.f * sat - fabsf(hueXsat - 3.f * sat), 0.f, sat),
                    1.f - glm::clamp(fabsf(hueXsat - 2.f * sat) - sat, 0.f, sat),
                    1.f - glm::clamp(fabsf(hueXsat - 4.f * sat) - sat, 0.f, sat),
                    1.f);
}

// == Profiler Scope ==========================================================

profiler::scope::scope() : m_color(glm::vec4(0.f)), m_parent(nullptr), m_owner(nullptr)
{
  m_name[0] = 0;
  m_tick_start = systemclock::now();
}

profiler::scope::scope(profiler *owner, const char *name, const glm::vec4 &color) : m_color(color), m_parent(nullptr), m_owner(nullptr)
{
  std::strncpy(m_name, name, 16);
#ifdef TRE_DEBUG
  for (char c : m_name)
  {
    if (c == '/') TRE_FATAL("profile: the name must not contain slash (/)");
    if (c == 0) break;
  }
#endif
  // attach the scope to the profiler's thread context.
  TRE_ASSERT(owner != nullptr);
  if (!owner->isEnabled()) return;
  m_owner = owner;
  s_context * ctx = m_owner->get_threadContext();
  m_parent = ctx->m_scopeCurrent;
  ctx->m_scopeCurrent = this;
  // start the timer
  m_tick_start = systemclock::now();
}

profiler::scope::~scope()
{
  if (m_owner == nullptr) return;

  s_context * ctx = m_owner->get_threadContext();

  const systemtick tick_end = systemclock::now();

  // create the record
  if (m_owner->isEnabled())
  {
    ctx->m_records.emplace_back();
    s_record &rc = ctx->m_records.back();

    rc.m_start = std::chrono::duration<double>(m_tick_start - m_owner->m_frameStartTick).count();
    rc.m_duration = std::chrono::duration<double>(tick_end - m_tick_start).count();
    rc.m_color = m_color;

    // compute the path
    scope * spath = this;
    std::size_t ic = 0;
    while (spath != nullptr)
    {
      for (char c : spath->m_name) { if (ic == 64 || c == 0) break; rc.m_path[ic++] = c; }
      if (ic == 64) break;
      rc.m_path[ic++] = '/';
      spath = spath->m_parent;
    }
    for (char c : ctx->m_name /* thread-name */) { if (ic == 64) break; rc.m_path[ic++] = c;  }
    ic = std::min(std::size_t(63), ic);
    rc.m_path[ic] = 0;

    // compute color if needed
    if (rc.m_color.w == 0.f)
    {
      const float hueOffset = (m_parent != nullptr) ? _hueFromColor(m_parent->m_color) : 0.f;
      uint hash = 5381;
      for (char c : rc.m_path) hash = ((hash << 5) + hash) + c; // DJB Hash Function
      //for (char c : rc.m_path) hash = c + (hash << 6) + (hash << 16) - hash; // SDBM Hash Function
      const float hue = hueOffset + float(hash & 0xF) / float(0xF * rc.depth());
      rc.m_color = _colorFromHS(hue, 0.8f);
    }
  }

  // pop the scope in the profile's thread context
  ctx->m_scopeCurrent = m_parent;
}

// == Profiler Main ===========================================================

profiler::profiler()
{
  m_recordsOverFrames.fill(s_frame());

  TRE_ASSERT(profilerThreadID == -1);
  profilerThreadID = 0;
}

profiler::~profiler()
{
  TRE_ASSERT(m_whiteTexture == nullptr);
  TRE_ASSERT(m_shader == nullptr);

  /* clear contexts ... */
}

void profiler::newframe()
{
  TRE_ASSERT(profilerThreadID == 0);

  // for all threads ... (do lock)
  if (m_enabled)
  {
    TRE_ASSERT(m_context.m_scopeCurrent == nullptr);
    m_context.m_records.clear();
  }

  m_frameStartTick = systemclock::now();
}

void profiler::endframe()
{
  TRE_ASSERT(profilerThreadID == 0);

  // for all threads ... (do lock)
  if (m_enabled)
  {
    TRE_ASSERT(m_context.m_scopeCurrent == nullptr);

    if (!m_paused)
    {
      m_collectedRecords = m_context.m_records; // copy

      m_hoveredRecord = -1;

      // treat the collected records
      for (const s_record & cRec : m_collectedRecords)
      {
        // find it in the mean value ...
        int recordMeanIndex = -1;
        for (uint iC = 0; iC < m_meanvalueRecords.size(); ++iC)
        {
          if (m_meanvalueRecords[iC].hasSamePath(cRec))
          {
            recordMeanIndex = iC;
            break;
          }
        }

        if (recordMeanIndex != -1)
        {
          // found, add the current value
          s_record & mRec = m_meanvalueRecords[recordMeanIndex];
          mRec.m_duration = 0.95f * mRec.m_duration + 0.05f * cRec.m_duration;
          mRec.m_start    = 0.f; // N/A
        }
        else
        {
          // not found, add new input
          m_meanvalueRecords.push_back(cRec);
          m_meanvalueRecords.back().m_start = 0.f;
        }
      }

      m_context.m_records.clear();
    }

    const systemtick tick_end = systemclock::now();

    if (!m_paused)
    {
      TRE_ASSERT(m_recordsOverFrames.size() == 0x100)
      m_frameIndex = (m_frameIndex + 1) & 0x0FF;

      m_recordsOverFrames[m_frameIndex].m_records.clear();
      for (const auto & rec : m_collectedRecords)
      {
        const int depth = rec.depth();
        TRE_ASSERT(depth >= 2);
        if (depth > 2) continue;
        m_recordsOverFrames[m_frameIndex].m_records.emplace_back(rec.m_color, rec.m_duration, 0);
      }
      m_recordsOverFrames[m_frameIndex].m_globalTime = std::chrono::duration<float>(tick_end - m_frameStartTick).count();
    }
  }
}

void profiler::initSubThread()
{
  TRE_ASSERT(profilerThreadID == -1);
  profilerThreadID = profilerThreadCount++;
  // TODO ...
}

// ============================================================================

void profiler::updateCameraInfo(const glm::mat3 &mProjView, const glm::vec2 &screenSize)
{
  m_PV = mProjView;
  m_viewportSize = screenSize;
}

bool profiler::acceptEvent(const SDL_Event &event)
{
  bool eventAccepted = false;

  const glm::ivec2 mouseCoord(event.button.x, event.button.y);

  switch(event.type)
  {
  case SDL_KEYDOWN:
    if (event.key.keysym.scancode == SDL_SCANCODE_B) // scancode: physical location of the keyboard key
    {
      m_paused = !m_paused;
      eventAccepted = true;
    }
    else if (event.key.keysym.scancode == SDL_SCANCODE_V) // scancode: physical location of the keyboard key
    {
      m_enabled = !m_enabled;
      eventAccepted = true;
    }
    break;
  case SDL_MOUSEBUTTONDOWN:
    if (event.button.button == SDL_BUTTON_LEFT)
    {
      eventAccepted = acceptEvent(mouseCoord);
    }
    break;
  case SDL_MOUSEBUTTONUP:
    if (event.button.button == SDL_BUTTON_LEFT)
    {
      eventAccepted = acceptEvent(mouseCoord);
    }
    break;
  case SDL_MOUSEMOTION:
    eventAccepted = acceptEvent(mouseCoord);
    break;
  }

  return eventAccepted;
}

bool profiler::acceptEvent(const glm::ivec2 &mousePosition)
{
  if (!m_paused)
    return false;

  bool accepted = false;

  // check hovering

  const glm::vec3 mouseScreanSpace(mousePosition.x * 2.f / m_viewportSize.x - 1.f, 1.f - mousePosition.y * 2.f / m_viewportSize.y, 1.f);
  m_mousePosition = glm::inverse(m_PV * m_matModel) * mouseScreanSpace;

  m_hoveredRecord = -1; // tmp, TODO have a timer

  const double dxPixel = 2.f / m_viewportSize.x; // TODO, consider m_PV and m_matModel

  int levelmax = 4;
  for (const s_record & rec : m_collectedRecords)
  {
    const int recL = rec.depth();
    if (recL > levelmax) levelmax = recL;
  }
  const double dYlevel = m_dYthread / levelmax;

  int irec = 0;
  for (const s_record & rec : m_collectedRecords)
  {
    const double x0 = m_xStart + rec.m_start * m_dX / m_dTime;
    const double x1 = std::max(x0 + rec.m_duration * m_dX / m_dTime, x0 + 2.f * dxPixel);
    TRE_ASSERT(rec.depth() >= 2); // root + first-zone
    const int level = rec.depth() - 2;

    if (x0 <= m_mousePosition.x && m_mousePosition.x <= x1 &&
        m_yStart + level * dYlevel <= m_mousePosition.y && m_mousePosition.y <= m_yStart + (level+1) * dYlevel)
    {
      m_hoveredRecord = irec;
      accepted = true;
      break;
    }

    ++irec;
  }

  return accepted;
}


// ============================================================================

void profiler::draw() const
{
  if (!m_enabled)
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
  if (!m_enabled)
    return;

  TRE_ASSERT(m_model.isLoadedGPU());

  compute_data();

  m_model.updateIntoGPU();
}

void profiler::clearGPU()
{
  m_font = nullptr;

  m_model.clearGPU();

  m_whiteTexture->clear();
  delete m_whiteTexture;
  m_whiteTexture = nullptr;
}

bool profiler::loadShader(shader *shaderToUse)
{
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
  if (m_shaderOwner)
  {
    TRE_ASSERT(m_shader != nullptr);
    m_shader->clearShader();
    delete m_shader;
  }
  m_shader = nullptr;
  m_shaderOwner = true;
}

void profiler::compute_data()
{
  static const glm::vec4 colorGridPrimary = glm::vec4(0.0f, 0.7f, 0.0f, 1.0f);
  static const glm::vec4 colorGridSecond  = glm::vec4(0.0f, 0.7f, 0.0f, 0.6f);

  const uint nThread = 1;
  const uint nTime = 20;

  const float xEnd = 1000.f;
  const float yEnd = m_yStart + nThread * m_dYthread;

  std::size_t lineCount_recordOverFrame = 0;
  for (const auto &rec : m_recordsOverFrames) lineCount_recordOverFrame += 2 + 2 * rec.m_records.size();

  m_model.clearParts();
  m_partTri = m_model.createPart(m_collectedRecords.size() * 6 + ((m_hoveredRecord != -1) ? 6 : 0));
  m_partLine = m_model.createPart((nThread + 1) * 2 + 2 + (nTime + 1) * 2 + m_collectedRecords.size() * 2 + lineCount_recordOverFrame + 6);

  m_partText = m_model.createPart(1024);
  m_model.colorizePart(m_partText, glm::vec4(0.f));

  std::size_t offsetLine = 0;
  std::size_t offsetTri = 0;
  std::size_t offsetText = 0;

  // computing scales
  {
    double lastzonetime = 0.;
    for (const s_record & rec : m_collectedRecords)
    {
      const double recendtime = rec.m_start + rec.m_duration;
      if (recendtime > lastzonetime) lastzonetime = recendtime;
    }

    if (fabs(lastzonetime * m_dX / m_dTime - 1.5f) > 1.0f)
    {
      m_dX = float(1.5f * m_dTime / lastzonetime);
    }

    float maxframetime = 0.f;
    for (const auto &rec : m_recordsOverFrames)
    {
      if (maxframetime < rec.m_globalTime) maxframetime = rec.m_globalTime;
    }

    if (std::abs(maxframetime * m_dY * 5.f / 0.020f - 0.6f) > 0.2f)
    {
      m_dY = (0.6f / 5.f * 0.020f / maxframetime);
    }

  }

  // create grid
  {
    for (uint j = 0; j <= nThread; ++j)
      m_model.fillDataLine(m_partLine, offsetLine + j * 2, m_xTitle, m_yStart + j * m_dYthread, xEnd, m_yStart + j * m_dYthread, colorGridPrimary);
    offsetLine += (nThread + 1) * 2;

    m_model.fillDataLine(m_partLine, offsetLine, m_xTitle, m_yStart, m_xTitle, yEnd, colorGridPrimary);
    offsetLine += 2;

    for (uint i = 0; i <= nTime; ++i)
      m_model.fillDataLine(m_partLine, offsetLine + i * 2, m_xStart + i * m_dX, m_yStart, m_xStart + i * m_dX, yEnd, (i % 2 == 0) ? colorGridPrimary : colorGridSecond);
    offsetLine += (nTime + 1) * 2;
  }

  // create recorded zones
  {
    int levelmax = 4;
    for (const s_record & rec : m_collectedRecords)
    {
      const int recL = rec.depth();
      if (recL > levelmax) levelmax = recL;
    }
    const double dYlevel = m_dYthread / levelmax;

    int irec = 0;
    for (const s_record & rec : m_collectedRecords)
    {
      const double x0 = m_xStart + rec.m_start * m_dX / m_dTime;
      const double x1 = x0 + rec.m_duration * m_dX / m_dTime;
      TRE_ASSERT(rec.depth() >= 2); // root + first-zone
      const uint level = rec.depth() - 2;

      const glm::vec4 AABB(x0, m_yStart + level * dYlevel, x1, m_yStart + (level+1) * dYlevel);
      glm::vec4 color = rec.m_color;
      if (m_hoveredRecord == irec) color = color * 0.5f + 0.5f;

      m_model.fillDataRectangle(m_partTri, offsetTri, AABB, color, glm::vec4(0.f));

      m_model.fillDataLine(m_partLine, offsetLine, AABB.x, AABB.y, AABB.x, AABB.w, color * 0.5f);

      offsetTri += 6;
      offsetLine += 2;
      ++irec;
    }
  }

  // create text
  for(uint iT = 0; iT < nThread; ++iT)
  {
    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, m_context.m_name.c_str(), glm::vec2(m_xTitle + 0.01f, m_yStart + iT * m_dYthread + m_dYthread));
    txtInfo.setupSize(m_dYthread);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
  }
  for (uint iT = 0; iT < nTime; iT += 2)
  {
    const double x = m_xStart + iT * m_dX;
    const double t = iT * m_dTime;
    char txt[16];
    std::snprintf(txt, 15, "%.f ms", t * 1000.f);

    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, txt, glm::vec2(x - 0.5f * m_dYthread, m_yStart - 0.02f * m_dYthread));
    txtInfo.setupSize(m_dYthread);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
  }
  TRE_ASSERT(offsetText <= 1024);

  // create time graph-zone
  {
    const float pixelX_scren = 2.f / m_viewportSize.x;
    const float pixelX_model = pixelX_scren / m_PV[0][0] / m_matModel[0][0];
    const float dX = pixelX_model;

    const float x0 = m_xStart;
    const float xN = m_xStart + (m_recordsOverFrames.size() - 1) * dX;
    const float y0 = yEnd + m_dYthread;
    const float dY = m_dY * 5.f / 0.020f;
    const float y1ms  = y0 + 0.001f * dY;
    const float y10ms = y0 + 0.010f * dY;

    TRE_ASSERT(m_recordsOverFrames.size() == 0x100);
    for (uint iF = 0; iF < m_recordsOverFrames.size(); ++iF)
    {
      const uint tIndex = (1 + iF + m_frameIndex) & 0x0FF;
      const float xFT = x0 + float(iF) * dX;
      float accT = 0.f;
      for (const auto &rec: m_recordsOverFrames[tIndex].m_records)
      {
        const float yA = y0 + accT * dY;
        const float yB = yA + float(rec.m_duration) * dY;
        m_model.fillDataLine(m_partLine, offsetLine, xFT, yA, xFT, yB, rec.m_color);
        offsetLine += 2;
        accT += float(rec.m_duration);
      }
      const float yFT = y0 + m_recordsOverFrames[tIndex].m_globalTime * dY;
      const float green = expf(-m_recordsOverFrames[tIndex].m_globalTime * 100.f);
      const float blue = expf(-m_recordsOverFrames[tIndex].m_globalTime * 600.f);
      m_model.fillDataLine(m_partLine, offsetLine, xFT, y0, xFT, yFT, glm::vec4(1.f, green, blue, 0.4f));
      offsetLine += 2;
    }
    m_model.fillDataLine(m_partLine, offsetLine + 0, x0, y0, xN, y0, glm::vec4(0.f, 0.f, 1.f, 0.8f));
    m_model.fillDataLine(m_partLine, offsetLine + 2, x0, y1ms, xN, y1ms, glm::vec4(1.f, expf(-0.1f), expf(-0.6f), 0.6f));
    m_model.fillDataLine(m_partLine, offsetLine + 4, x0, y10ms, xN, y10ms, glm::vec4(1.f, expf(-1.0f), expf(-6.0f), 0.4f));
    offsetLine += 6;

    textgenerator::s_textInfo txtInfo;

    txtInfo.setupBasic(m_font, "frame", glm::vec2(m_xTitle, y0 + m_dYthread * 0.5));
    txtInfo.setupSize(m_dYthread);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);

    txtInfo.setupBasic(m_font, "1ms", glm::vec2(x0 - 1.5f * m_dYthread, y1ms + m_dYthread * 0.25f));
    txtInfo.setupSize(m_dYthread * 0.5f);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);

    txtInfo.setupBasic(m_font, "10ms", glm::vec2(x0 - 1.5f * m_dYthread, y10ms + m_dYthread * 0.25f));
    txtInfo.setupSize(m_dYthread * 0.5f);
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
  }

  // create tooltip
  if (m_hoveredRecord != -1)
  {
    TRE_ASSERT(m_hoveredRecord >= 0 && m_hoveredRecord < int(m_collectedRecords.size()));
    const s_record &hrec = m_collectedRecords[m_hoveredRecord];

    const s_record *mrec = nullptr;
    for (const s_record &mrecLoop : m_meanvalueRecords)
    {
      if (mrecLoop.hasSamePath(hrec))
      {
        mrec = &mrecLoop;
        break;
      }
    }

    char txtT[128];
    // reverse path
    std::size_t ic = 0;
    std::size_t stop = std::strlen(hrec.m_path);
    TRE_ASSERT(stop <= 64);
    while (true)
    {
      std::size_t start = stop - 1;
      while (start != 0 && hrec.m_path[start] != '/') --start;
      if (start != 0) ++start;
      for (std::size_t i = start; i < stop; ++i) txtT[ic++] = hrec.m_path[i];
      if (start == 0) break;
      --start;
      txtT[ic++] = '/';
      stop = start;
    }
    txtT[ic++] = '\n';
    std::snprintf(txtT + ic, 127 - ic, "\n%.3f ms (mean: %.3f ms)",
                  int(hrec.m_duration*1000000)*0.001f,
                  mrec == nullptr ? 0.f : int(mrec->m_duration*1000000)*0.001f);
    txtT[127] = 0;

    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, txtT);
    txtInfo.setupSize(m_dYthread);

    textgenerator::s_textInfoOut txtInfoOut;
    textgenerator::generate(txtInfo, nullptr, 0, 0, &txtInfoOut); // without mesh

    txtInfo.m_zone = glm::vec4(m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y,
                               m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);

    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
    TRE_ASSERT(offsetText <= 1024);

    const glm::vec4 AABB(m_mousePosition.x, m_mousePosition.y,
                         m_mousePosition.x + txtInfoOut.m_maxboxsize.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);

    m_model.fillDataRectangle(m_partTri, m_collectedRecords.size() * 6, AABB, glm::vec4(0.f, 0.f, 0.f, 0.7f), glm::vec4(0.f));
  }
}

// ============================================================================

} // namespace

#endif // TRE_PROFILE
