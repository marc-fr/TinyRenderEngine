#include "tre_profiler.h"

#ifdef TRE_PROFILE

#include "tre_shader.h"
#include "tre_texture.h"
#include "tre_font.h"
#include "tre_textgenerator.h"

namespace tre {

// ============================================================================

#define SATURATE_MAX(val, m) (val < 0.f ? 0.f : (val > m ? m : val))

static glm::vec4 _color_Advance_Hue(const glm::vec4 &color, float hue_add /**< hue is in [0,1] range.*/)
{
  // Convert RBG -> HSV
  float vmax = color.r;
  float vmin = color.r;
  uint imax = 0;
  if (color.g > vmax) { vmax = color.g; imax = 1; }
  if (color.b > vmax) { vmax = color.b; imax = 2; }
  if (color.g < vmin) vmin = color.g;
  if (color.b < vmin) vmin = color.b;

  const float delta = vmax - vmin;
  if (delta < 1.e-4f)
    return color;

  const float sat = delta / vmax;

  float hueXsat;
  if      (imax == 0) hueXsat = (color.g - color.b);
  else if (imax == 1) hueXsat = 2.f * sat + (color.b - color.r);
  else                hueXsat = 4.f * sat + (color.r - color.g);

  // now, offset the hue
  hueXsat += (1.f + fmodf(hue_add, 1.f)) * 6.f * sat;
  hueXsat = fmodf(hueXsat, 6.f * sat);

  // Convert HSV -> RBG
  glm::vec4 newColor( 1.f - SATURATE_MAX(2.f * sat - fabsf(hueXsat - 3.f * sat), sat),
                      1.f - SATURATE_MAX(fabsf(hueXsat - 2.f * sat) - sat, sat),
                      1.f - SATURATE_MAX(fabsf(hueXsat - 4.f * sat) - sat, sat),
                      0.f);
  newColor *= vmax;
  newColor.w = 1.f;

  return newColor;
}

#undef SATURATE_MAX

// ============================================================================

profiler::scope::scope(const std::string &name, const glm::vec4 color) : m_color(color), m_name(name)
{
  m_tick_start = systemclock::now();
}

profiler::scope::scope(profiler *owner, const std::string &name, const glm::vec4 color) : m_color(color), m_name(name)
{
  attach(owner);
  m_tick_start = systemclock::now();
}

profiler::scope::~scope()
{
  if (m_owner == nullptr)
    return;

  s_context * ctx = m_owner->get_threadContext();

  const systemtick tick_end = systemclock::now();

  // create the record
  if (m_owner->isEnabled())
  {
    ctx->m_records.push_back(s_record());
    s_record &rc = ctx->m_records.back();

    rc.m_start = std::chrono::duration<double>(m_tick_start - m_owner->m_frameStartTick).count();
    rc.m_duration = std::chrono::duration<double>(tick_end - m_tick_start).count();
    rc.m_color = m_color;
    rc.m_path = m_path;
  }

  // pop the scope in the profile's thread context
  ctx->m_scopeCurrent = m_parent;
}

void profiler::scope::attach(profiler *owner)
{
  TRE_ASSERT(m_owner == nullptr);
  TRE_ASSERT(owner != nullptr);
  if (!owner->isEnabled())
    return;

  // attach the scope to the profiler's thread context.
  m_owner = owner;
  s_context * ctx = m_owner->get_threadContext();
  m_parent = ctx->m_scopeCurrent;
  ctx->m_scopeCurrent = this;
  // compute path
  {
    std::vector<std::string> tmpPath;
    scope * spath = this;
    while (spath != nullptr)
    {
      tmpPath.push_back(spath->m_name);
      spath = spath->m_parent;
    }
    m_path.reserve(tmpPath.size() + 1);
    m_path.push_back(ctx->m_name); // thread-name
    for (uint iP = 0; iP < tmpPath.size(); ++iP)
      m_path.push_back(tmpPath[tmpPath.size() - 1 - iP]); // ... path
  }
  // compute color if needed
  if (m_color.w == 0.f)
  {
    glm::vec4 directParentColor = m_parent != nullptr ? m_parent->m_color : glm::vec4(0.2f, 0.2f, 1.f, 1.f);
    const std::size_t nLevel = m_path.size() - 1;
    uint nRecSameParent = 0;
    {
      std::size_t iRec = ctx->m_records.size();
      while (iRec-- > 0)
      {
        if (ctx->m_records[iRec].isSamePath(m_path, 1))
        {
          m_color = ctx->m_records[iRec].m_color;
          return; // hard break of the loop
        }
        if (ctx->m_records[iRec].isChildOf(m_path, 1))
        {
          if (ctx->m_records[iRec].length() == m_path.size())
            ++nRecSameParent;
        }
      }
    }
    m_color = _color_Advance_Hue(directParentColor, - 0.07f + nLevel * 0.01f - nRecSameParent * 0.11f);
  }
}

// ============================================================================

void profiler::newframe()
{

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
  // for all threads ... (do lock)
  if (m_enabled)
  {
    TRE_ASSERT(m_context.m_scopeCurrent == nullptr);

    // collect ...
    if (!m_paused)
    {
      collect();

      TRE_ASSERT(m_timeOverFrames.size() == 0x100)
      m_frameIndex = (m_frameIndex + 1) & 0x0FF;
    }

    const systemtick tick_end = systemclock::now();
    m_timeOverFrames[m_frameIndex] = std::chrono::duration<float>(tick_end - m_frameStartTick).count();
  }
}

void profiler::collect()
{
  m_collectedRecords = m_context.m_records; // TODO : std::move ??

  m_hoveredRecord = -1;

  // collect mean-values
  if (m_meanvalueRecords.empty())
  {
    m_meanvalueRecords = m_collectedRecords;

    // TODO: make unique "path"  entries
  }
  else
  {
    // treat the collected records
    for (const s_record & cRec : m_collectedRecords)
    {
      // find it in the mean value ...
      int recordMeanIndex = -1;
      for (uint iC = 0; iC < m_meanvalueRecords.size(); ++iC)
      {
        if (m_meanvalueRecords[iC].isSamePath(cRec.m_path))
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
  }

  m_context.m_records.clear();
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
  default:
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

  uint levelmax = 4;
  for (const s_record & rec : m_collectedRecords)
  {
    const uint recL = rec.length();
    if (recL > levelmax) levelmax = recL;
  }
  const double dYlevel = m_dYthread / levelmax;

  uint irec = 0;
  for (const s_record & rec : m_collectedRecords)
  {
    const double x0 = m_xStart + rec.m_start * m_dX / m_dTime;
    const double x1 = std::max(x0 + rec.m_duration * m_dX / m_dTime, x0 + 2.f * dxPixel);
    TRE_ASSERT(rec.length() >= 2); // root + first-zone
    const uint level = rec.length() - 2;

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

  create_data();
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

void profiler::create_data()
{
  m_partTri = m_model.createPart(1024);
  m_partLine = m_model.createPart(1024);
  m_partText = m_model.createPart(1024);
}

void profiler::compute_data()
{
  static const glm::vec4 colorGridPrimary = glm::vec4(0.0f, 0.7f, 0.0f, 1.0f);
  static const glm::vec4 colorGridSecond  = glm::vec4(0.0f, 0.7f, 0.0f, 0.6f);

  const uint nThread = 1;
  const uint nTime = 20;

  const float xEnd = 1000.f;
  const float yEnd = m_yStart + nThread * m_dYthread;

  m_model.resizePart(m_partTri, m_collectedRecords.size() * 6 + ((m_hoveredRecord != -1) ? 6 : 0));
  m_model.resizePart(m_partLine, (nThread + 1 + nTime + 2 + m_collectedRecords.size() + 3 + m_timeOverFrames.size()) * 2);
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

    for (uint iF = 0; iF < m_timeOverFrames.size() / 2; ++iF)
    {
      const uint tIndex = (1 + iF + m_frameIndex) & 0x0FF;
      const float tframe = m_timeOverFrames[tIndex] * 0.5f;
      if (tframe > maxframetime) maxframetime = tframe;
    }
    for (uint iF = m_timeOverFrames.size() / 2; iF < m_timeOverFrames.size(); ++iF)
    {
      const uint tIndex = (1 + iF + m_frameIndex) & 0x0FF;
      const float tframe = m_timeOverFrames[tIndex];
      if (tframe > maxframetime) maxframetime = tframe;
    }

    if (fabsf(maxframetime * m_dY * 5.f / 0.020f - 0.6f) > 0.2f)
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
    uint levelmax = 4;
    for (const s_record & rec : m_collectedRecords)
    {
      const uint recL = rec.length();
      if (recL > levelmax) levelmax = recL;
    }
    const double dYlevel = m_dYthread / levelmax;

    int irec = 0;
    for (const s_record & rec : m_collectedRecords)
    {
      const double x0 = m_xStart + rec.m_start * m_dX / m_dTime;
      const double x1 = x0 + rec.m_duration * m_dX / m_dTime;
      TRE_ASSERT(rec.length() >= 2); // root + first-zone
      const uint level = rec.length() - 2;

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
    txtInfo.setupBasic(m_font, m_dYthread * 0.7f, m_context.m_name.c_str(), glm::vec2(m_xTitle + 0.01f, m_yStart + iT * m_dYthread + m_dYthread));
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
    txtInfo.setupBasic(m_font, m_dYthread * 0.5f, txt, glm::vec2(x - 0.5f * m_dYthread, m_yStart - 0.02f * m_dYthread));
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
  }

  // create graph-zone
  {
    const float pixelX_scren = 2.f / m_viewportSize.x;
    const float pixelX_model = pixelX_scren / m_PV[0][0] / m_matModel[0][0];
    const float dX = pixelX_model;

    const float x0 = m_xStart;
    const float xN = m_xStart + (m_timeOverFrames.size() - 1) * dX;
    const float y0 = yEnd + m_dYthread;
    const float dY = m_dY * 5.f / 0.020f;
    const float y1ms  = y0 + 0.001f * dY;
    const float y10ms = y0 + 0.010f * dY;

    TRE_ASSERT(m_timeOverFrames.size() == 0x100);
    for (uint iF = 0; iF < m_timeOverFrames.size(); ++iF)
    {
      const uint tIndex = (1 + iF + m_frameIndex) & 0x0FF;
      const float xFT = x0 + float(iF) * dX;
      const float yFT = y0 + m_timeOverFrames[tIndex] * dY;
      const float green = expf(-m_timeOverFrames[tIndex] * 100.f);
      const float blue = expf(-m_timeOverFrames[tIndex] * 600.f);
      m_model.fillDataLine(m_partLine, offsetLine + iF * 2, xFT, y0, xFT, yFT, glm::vec4(1.f, green, blue, 1.f));
    }
    offsetLine += 2 * m_timeOverFrames.size();


    m_model.fillDataLine(m_partLine, offsetLine + 0, x0, y0, xN, y0, glm::vec4(0.f, 0.f, 1.f, 0.8f));
    m_model.fillDataLine(m_partLine, offsetLine + 2, x0, y1ms, xN, y1ms, glm::vec4(1.f, expf(-0.1f), expf(-0.6f), 0.6f));
    m_model.fillDataLine(m_partLine, offsetLine + 4, x0, y10ms, xN, y10ms, glm::vec4(1.f, expf(-1.0f), expf(-6.0f), 0.4f));
    offsetLine += 6;

    textgenerator::s_textInfo txtInfo;

    txtInfo.setupBasic(m_font, m_dYthread * 0.7f, "frame", glm::vec2(m_xTitle, y0 + m_dYthread * 0.5));
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);

    txtInfo.setupBasic(m_font, m_dYthread * 0.5f, "1ms", glm::vec2(x0 - 1.5f * m_dYthread, y1ms + m_dYthread * 0.25f));
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);

    txtInfo.setupBasic(m_font, m_dYthread * 0.5f, "10ms", glm::vec2(x0 - 1.5f * m_dYthread, y10ms + m_dYthread * 0.25f));
    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);
  }

  // create tooltip
  if (m_hoveredRecord != -1)
  {
    TRE_ASSERT(m_hoveredRecord >= 0 && m_hoveredRecord < m_collectedRecords.size());
    const s_record &hrec = m_collectedRecords[m_hoveredRecord];

    const s_record *mrec = nullptr;
    for (const s_record &mrecLoop : m_meanvalueRecords)
    {
      if (mrecLoop.isSamePath(hrec.m_path))
      {
        mrec = &mrecLoop;
        break;
      }
    }

    std::string txt = hrec.m_path.front();
    for (uint i = 1 ; i < hrec.length(); ++i)
      txt += '/' + hrec.m_path[i];
    char txtT[128];
    std::snprintf(txtT, 127, "\n%.3f ms (mean: %.3f ms)",
                  int(hrec.m_duration*1000000)*0.001f,
                  mrec == nullptr ? 0.f : int(mrec->m_duration*1000000)*0.001f);
    txtT[127] = 0;
    txt += txtT;

    textgenerator::s_textInfo txtInfo;
    txtInfo.setupBasic(m_font, m_dYthread * 0.7f, txt.c_str());

    textgenerator::s_textInfoOut txtInfoOut;
    textgenerator::generate(txtInfo, nullptr, 0, 0, &txtInfoOut); // without mesh

    txtInfo.m_zone = glm::vec4(m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y,
                               m_mousePosition.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);

    textgenerator::generate(txtInfo, &m_model, m_partText, offsetText, nullptr);
    offsetText += textgenerator::geometry_VertexCount(txtInfo.m_text);


    const glm::vec4 AABB(m_mousePosition.x, m_mousePosition.y,
                         m_mousePosition.x + txtInfoOut.m_maxboxsize.x, m_mousePosition.y + txtInfoOut.m_maxboxsize.y);

    m_model.fillDataRectangle(m_partTri, m_collectedRecords.size() * 6, AABB, glm::vec4(0.f, 0.f, 0.f, 0.7f), glm::vec4(0.f));
  }
}

// ============================================================================

profiler profilerRoot; // static profiler

} // namespace

#endif // TRE_PROFILE
