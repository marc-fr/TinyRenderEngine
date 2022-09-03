
#include <iostream>
#include <sstream>
#include <array>

#include "windowHelper.h"
#include "ui.h"
#include "font.h"

#include "audio.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

class widgetUVmeter : public tre::ui::widget
{
public:
  struct s_value
  {
    float m_peak;
    float m_RMS;
  };
  s_value wvalue;

  virtual uint get_vcountSolid() const override { return 18; }

  virtual glm::vec2 get_zoneSizeDefault() const override
  {
    const float h = get_parentWindow()->resolve_sizeH(get_parentWindow()->get_fontSize());
    return glm::vec2(5.f * h, h);
  }

  void compute_data() override
  {
    auto & objsolid = get_parentUI()->getDrawModel();

    objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 0, m_zone, glm::vec4(0.f), glm::vec4(0.f));

    const float valx1 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue.m_peak);
    const glm::vec4 zoneBar1 = glm::vec4(m_zone.x,m_zone.y,valx1,m_zone.w);
    const glm::vec4 zoneC1 = glm::vec4(0.5f * wvalue.m_peak, 0.5f * (1.f - wvalue.m_peak), 0.f, 1.f);
    objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 6, zoneBar1, glm::vec4(0.f), glm::vec4(0.f));
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6    ) = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6 + 1) = zoneC1;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6 + 2) = zoneC1;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6 + 3) = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6 + 4) = zoneC1;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 6 + 5) = glm::vec4(0.f, 0.5f, 0.f, 1.f);

    const float valx2 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue.m_RMS);
    const glm::vec4 zoneBar2 = glm::vec4(m_zone.x,m_zone.y,valx2,m_zone.w);
    const glm::vec4 zoneC2 = glm::vec4(wvalue.m_peak, 1.f - wvalue.m_peak, 0.f, 1.f);
    objsolid.fillDataRectangle(m_adSolid.part, m_adSolid.offset + 12, zoneBar2, glm::vec4(0.f), glm::vec4(0.f));
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12    ) = glm::vec4(0.f, 1.f, 0.f, 1.f);
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12 + 1) = zoneC2;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12 + 2) = zoneC2;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12 + 3) = glm::vec4(0.f, 1.f, 0.f, 1.f);
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12 + 4) = zoneC2;
    objsolid.layout().m_colors.get<glm::vec4>(m_adSolid.offset + 12 + 5) = glm::vec4(0.f, 1.f, 0.f, 1.f);
  }

  void setValueModified() { m_isUpdateNeededData = true; }
};

// =============================================================================

class widgetWaveform : public tre::ui::widgetPicture
{
public:
  virtual uint get_vcountLine() const override { return tre::ui::widgetPicture::get_vcountLine() + 2; }

  float wcursor = 0.f;

  void setCursor(float c) { m_isUpdateNeededData |= (c != wcursor); wcursor = c; }

  virtual void compute_data() override
  {
    tre::ui::widgetPicture::compute_data();

    auto & objsolid = get_parentUI()->getDrawModel();
    const uint adPart = m_adrLine.part;
    const uint adOffset = m_adrLine.offset + tre::ui::widgetPicture::get_vcountLine();

    const float xC = m_zone.x + (m_zone.z - m_zone.x) * wcursor;
    const glm::vec2 pA = glm::vec2(xC, m_zone.y);
    const glm::vec2 pB = glm::vec2(xC, m_zone.w);
    objsolid.fillDataLine(adPart, adOffset, pA, pB, glm::vec4(0.3f, 1.f, 0.3f, 1.f));
  }

  static void genWaveForm(const tre::soundData &s, SDL_Surface *tex, uint y0, uint y1)
  {
    TRE_ASSERT(tex->format->BytesPerPixel == 4);
    const uint halfHeight_px = (y1 - y0 - 1) / 2;
    const uint zeroLine_px = y0 + halfHeight_px;
    const uint width = tex->w;
    uint32_t * __restrict pixels = reinterpret_cast<uint32_t*>(tex->pixels);

    const uint samples = s.size();
    const uint samplesPerPixel = std::max(1u, samples / width);
    std::vector<float> buffer;
    buffer.resize(samplesPerPixel);

    for (uint x = 0; x < width; ++x)
    {
      const uint cursorStart = (x * samples) / width;
      float valuePeak = 0.f;
      float valueRMS = 0.f;
      s.extractData_Float(0, cursorStart, samplesPerPixel, buffer.data());
      //
      for (uint i = 0; i < samplesPerPixel; ++i)
      {
        valuePeak = std::max(valuePeak, std::abs(buffer[i]));
        valueRMS += buffer[i] * buffer[i];
      }
      valueRMS = std::sqrt(valueRMS / samplesPerPixel);
      //
      const uint valuePeak_px = valuePeak * halfHeight_px;
      const uint valueRMS_px = valuePeak * halfHeight_px;

      for (uint y = zeroLine_px - valuePeak_px, yE = zeroLine_px + 1 + valuePeak_px; y < yE; ++y) pixels[x + width * y] = 0xFF000080;
      for (uint y = zeroLine_px - valueRMS_px , yE = zeroLine_px + 1 + valueRMS_px ; y < yE; ++y) pixels[x + width * y] = 0xFF4040FF;
    }
  }
};

// =============================================================================

static tre::windowHelper windowCtx;
static tre::font         font;
static tre::baseUI2D     baseUI;
static tre::ui::window   *windowMain = nullptr;

static bool withGUI = true;
static bool withAudio = true;

static const std::string                nameClick = "music-click.wav";
static const std::string                nameWave = "sin440Hz.wav";
static const std::array<std::string, 4> nameTracks = { "music-base.wav", "music-clav.wav", "music-strings.wav", "music-piano.wav" };

struct s_music
{
  std::array<tre::soundData, 4> m_audio;
  std::array<tre::sound2D, 4>   m_tracks;
  std::string                   m_name;
  unsigned                      m_compression;

  s_music(const std::string &name, unsigned compression) : m_name(name), m_compression(compression)
  {
    for (std::size_t i = 0; i < m_audio.size(); ++i)
      m_tracks[i].setAudioData(&m_audio[i]);
  }
};

static std::array<s_music, 2> listSound = { s_music("click", 0), s_music("sin440Hz", 0) }; // only the first track is used here.
static std::array<s_music, 5> listMusic = { s_music("origin", 0), s_music("no-compression", 0), s_music("compression 64kb", 64000), s_music("compression 32kb", 32000), s_music("compression 16kb", 16000) };

static tre::audioContext audioCtx;

static tre::texture textureWaveforms;
static const unsigned textureWaveformMaxCount = 32;

// =============================================================================

static int app_init()
{
  // - Init

  if(!windowCtx.SDLInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO, "test Sound", SDL_WINDOW_RESIZABLE))
    return -1;

  if(!windowCtx.OpenGLInit())
    return -1;

  // -> Set pipeline state and clear the window

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.f,0.f,0.f,0.f);
  glClear(GL_COLOR_BUFFER_BIT);

  SDL_GL_SwapWindow(windowCtx.m_window);

  // -> Load other resources

  if (!font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true))
  {
    TRE_LOG("Fail to load the font. Cannot have the GUI.");
    withGUI = false;
  }

  // - Init Audio

  std::vector<std::string> audioDeviceNames;
  tre::audioContext::getDevicesName(audioDeviceNames);

  TRE_LOG("List of audio device(s): ");
  for (const auto &name : audioDeviceNames)
  {
    TRE_LOG("- " << name);
  }

#define AUDIO_SETTINGS_OVERWRITE 0

#if AUDIO_SETTINGS_OVERWRITE == 1
  SDL_AudioSpec     overwriteSpec;
  overwriteSpec.freq = 48000;       // samples per seconds
  overwriteSpec.channels = 2;       // stereo
  overwriteSpec.format = AUDIO_S16; // 16-bits signed (little-endian)

  withAudio &= audioCtx.startSystem(nullptr, 20, &overwriteSpec);
#else
  withAudio &= audioCtx.startSystem(nullptr);
#endif

  // -> Draw loading screen

  // TODO ...

  // -> Load music

  if (withAudio)
  {
    withAudio &= listSound[0].m_audio[0].loadFromWAVfile((TESTIMPORTPATH "resources/") + nameClick);
    withAudio &= listSound[1].m_audio[0].loadFromWAVfile((TESTIMPORTPATH "resources/") + nameWave);

    for (std::size_t i = 0; i < nameTracks.size(); ++i)
      withAudio &= listMusic[0].m_audio[i].loadFromWAVfile((TESTIMPORTPATH "resources/") + nameTracks[i]);

    std::stringstream tmpBuffer(std::ios_base::binary | std::ios_base::in | std::ios_base::out);
    TRE_ASSERT(!(!tmpBuffer));

    for (std::size_t k = 1; k < listMusic.size(); ++k)
    {
      for (std::size_t i = 0; i < nameTracks.size(); ++i) listMusic[0].m_audio[i].write(tmpBuffer, listMusic[k].m_compression);
      for (std::size_t i = 0; i < nameTracks.size(); ++i) withAudio &= listMusic[k].m_audio[i].read(tmpBuffer);
      tmpBuffer.seekg(0);
      tmpBuffer.seekp(0);
    }
  }

  if (withAudio)
  {
    withAudio &= listSound[0].m_audio[0].convertTo(audioCtx.getAudioSpec()->freq, audioCtx.getAudioSpec()->format);
    withAudio &= listSound[1].m_audio[0].convertTo(audioCtx.getAudioSpec()->freq, audioCtx.getAudioSpec()->format);

    audioCtx.addSound(&listSound[0].m_tracks[0]);
    audioCtx.addSound(&listSound[1].m_tracks[0]);

    for (std::size_t k = 0; k < listMusic.size(); ++k)
    {
      for (std::size_t i = 0; i < nameTracks.size(); ++i)
      {
        withAudio &= listMusic[k].m_audio[i].convertTo(audioCtx.getAudioSpec()->freq, audioCtx.getAudioSpec()->format);
        audioCtx.addSound(&listMusic[k].m_tracks[i]);
      }
    }
  }

  // -> Create the UI

   windowMain = baseUI.create_window();

  if (withGUI)
  {
    baseUI.set_defaultFont(&font);
    baseUI.updateCameraInfo(windowCtx.m_matProjection2D, windowCtx.m_resolutioncurrent);

    windowMain->set_fontSize(tre::ui::s_size(18,tre::ui::SIZE_PIXEL));
    windowMain->set_alignMask(tre::ui::ALIGN_MASK_CENTERED);
    windowMain->set_color(glm::vec4(0.f));
    windowMain->set_mat3(glm::mat3(1.f));
    windowMain->set_layoutGrid(64,8);
    windowMain->set_cellMargin(tre::ui::s_size(2,tre::ui::SIZE_PIXEL));
    windowMain->set_visible(true);
    windowMain->create_widgetText(0,0)->set_text("name")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,1)->set_text("play")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,3)->set_text("repeat")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,4)->set_text("mute")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,5)->set_text("volume")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,6)->set_text("UV meter")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,7)->set_text("Waveform")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
  }

  if (withAudio && withGUI)
  {
    unsigned iRaw = 1;

    const unsigned textureWaveformSlot = baseUI.addTexture(&textureWaveforms);
    SDL_Surface *textureWaveformLoading = SDL_CreateRGBSurface(0, 128, 32 * textureWaveformMaxCount, 32, 0, 0, 0, 0);
    unsigned iWaveform = 0;

    for (auto &s : listSound)
    {
      // main row for music.
      {
        char txtBuf[32];
        std::snprintf(txtBuf, 32,"sound: %s", s.m_audio[0].getName().c_str());
        windowMain->create_widgetText(iRaw, 0)->set_text(txtBuf);

        windowMain->create_widgetBoxCheck(iRaw, 1)->set_value(false)->set_isactive(true)->set_iseditable(true)
                  ->wcb_modified_finished = [&s](tre::ui::widget *w)
        {
          tre::ui::widgetBoxCheck *wBox = static_cast<tre::ui::widgetBoxCheck*>(w);
          s.m_tracks[0].control().m_isPlaying = wBox->get_value();
        };
        s.m_tracks[0].control().m_isPlaying = false;

        windowMain->create_widgetBoxCheck(iRaw, 3)->set_value(true)->set_isactive(true)->set_iseditable(true)
                  ->wcb_modified_finished = [&s](tre::ui::widget *w)
        {
          tre::ui::widgetBoxCheck *wBox = static_cast<tre::ui::widgetBoxCheck*>(w);
          s.m_tracks[0].control().m_isRepeating = wBox->get_value();
        };
        s.m_tracks[0].control().m_isRepeating = true;

        windowMain->create_widgetBoxCheck(iRaw,4)->set_value(false)->set_isactive(true)->set_iseditable(true);
        windowMain->create_widgetBar(iRaw,5)->set_value(0.5f)->set_isactive(true)->set_iseditable(true);

        auto *widMute = windowMain->get_widgetBoxCheck(iRaw,4);
        auto *widBar = windowMain->get_widgetBar(iRaw,5);

        widMute->wcb_modified_finished = [&s, widBar](tre::ui::widget *w)
        {
          tre::ui::widgetBoxCheck *wMute = static_cast<tre::ui::widgetBoxCheck*>(w);
          s.m_tracks[0].control().m_target.m_volume = !wMute->get_value() ? widBar->get_value() : 0.f;
          s.m_tracks[0].control().m_targetDelay = 0.f;
        };

        widBar->wcb_modified_ongoing = [&s, widMute](tre::ui::widget *w)
        {
          tre::ui::widgetBar *wBar = static_cast<tre::ui::widgetBar*>(w);
          s.m_tracks[0].control().m_target.m_volume = !widMute->get_value() ? wBar->get_value() : 0.f;
          s.m_tracks[0].control().m_targetDelay = 0.2f;
        };

        s.m_tracks[0].control().m_target.m_volume = 0.5f;
        s.m_tracks[0].control().m_targetDelay = 2.f;

        auto     *wUVmeter = new widgetUVmeter();
        unsigned localCountdown = 10;
        wUVmeter->wcb_animate = [&s, localCountdown](tre::ui::widget *w, float dt) mutable
        {
          widgetUVmeter *wM = static_cast<widgetUVmeter*>(w);
          if (s.m_tracks[0].feedback().m_playedSampleCount == 0 && --localCountdown != 0) return;
          localCountdown = 10;
          const float aP = (s.m_tracks[0].feedback().m_playedLevelPeak > wM->wvalue.m_peak) ? 1.f : 1.f - expf(-dt/0.020f);
          const float aR = (s.m_tracks[0].feedback().m_playedLevelRMS  > wM->wvalue.m_RMS ) ? 1.f : 1.f - expf(-dt/0.100f);
          wM->wvalue.m_peak = (1.f - aP) * wM->wvalue.m_peak + aP * s.m_tracks[0].feedback().m_playedLevelPeak;
          wM->wvalue.m_RMS  = (1.f - aR) * wM->wvalue.m_RMS  + aR * s.m_tracks[0].feedback().m_playedLevelRMS;
          wM->setValueModified();
        };
        windowMain->set_widget(wUVmeter, iRaw, 6);

        auto  *wWaveform = new widgetWaveform();
        {
          TRE_ASSERT(iWaveform < textureWaveformMaxCount);
          widgetWaveform::genWaveForm(s.m_audio[0], textureWaveformLoading, 0 + 32 * iWaveform, 32 + 32 * iWaveform);
          wWaveform->set_texId(textureWaveformSlot)->set_texUV(glm::vec4(0.f, iWaveform * 1.f / textureWaveformMaxCount, 1.f, (iWaveform + 1.f) / textureWaveformMaxCount));
          ++iWaveform;
        }
        wWaveform->wcb_animate = [&s](tre::ui::widget *w, float )
        {
          widgetWaveform *wW = static_cast<widgetWaveform*>(w);
          wW->setCursor(float(s.m_tracks[0].feedback().m_playedSampleCursor)/float(s.m_audio[0].size()));
        };
        windowMain->set_widget(wWaveform, iRaw, 7);

      }
      ++iRaw;
    }

    // tracks of the music
    for (s_music &m : listMusic)
    {
      windowMain->create_widgetText(iRaw, 0)->set_text("music " + m.m_name);

      // global control

      windowMain->create_widgetBoxCheck(iRaw, 1)->set_value(false)->set_isactive(true)->set_iseditable(true)
                ->wcb_modified_finished = [&m](tre::ui::widget *w)
      {
        tre::ui::widgetBoxCheck *wBox = static_cast<tre::ui::widgetBoxCheck*>(w);
        for (tre::sound2D &s : m.m_tracks) s.control().m_isPlaying = wBox->get_value();
      };

      for (tre::sound2D &s : m.m_tracks) s.control().m_isPlaying = false;

      windowMain->create_widgetBoxCheck(iRaw, 3)->set_value(true)->set_isactive(true)->set_iseditable(true)
                ->wcb_modified_finished = [&m](tre::ui::widget *w)
      {
        tre::ui::widgetBoxCheck *wBox = static_cast<tre::ui::widgetBoxCheck*>(w);
        for (tre::sound2D &s : m.m_tracks) s.control().m_isRepeating = wBox->get_value();
      };

      for (tre::sound2D &s : m.m_tracks) s.control().m_isRepeating = true;

      auto *widGlobalMute = windowMain->create_widgetBoxCheck(iRaw, 4);
      widGlobalMute->set_iseditable(true)->set_isactive(true);
      widGlobalMute->wcb_modified_finished = [iRaw, &m](tre::ui::widget *w)
      {
        tre::ui::widgetBoxCheck *wGMute = static_cast<tre::ui::widgetBoxCheck*>(w);
        for (uint it = 0; it < m.m_tracks.size(); ++it)
        {
          tre::ui::widgetBoxCheck *wLMute = windowMain->get_widgetBoxCheck(iRaw + 1 + it, 4);
          tre::ui::widgetBar      *wLVol = windowMain->get_widgetBar(iRaw + 1 + it, 5);
          m.m_tracks[it].control().m_target.m_volume = !wGMute->get_value() && !wLMute->get_value() ? wLVol->get_value() : 0.f;
          m.m_tracks[it].control().m_targetDelay = 0.f;
        }
      };

      ++iRaw;

      // track control
      for (tre::sound2D &s : m.m_tracks)
      {
        windowMain->create_widgetText(iRaw, 0)->set_text("- track " + s.audioData()->getName())->set_color(glm::vec4(0.6f, 0.6f, 0.8f, 1.f));

        windowMain->create_widgetBoxCheck(iRaw, 4)->set_value(false)->set_isactive(true)->set_iseditable(true)->set_color(glm::vec4(0.6f, 0.6f, 0.8f, 1.f));
        windowMain->create_widgetBar(iRaw, 5)->set_value(0.5f)->set_isactive(true)->set_iseditable(true)->set_color(glm::vec4(0.6f, 0.6f, 0.8f, 1.f));

        auto *widMute = windowMain->get_widgetBoxCheck(iRaw, 4);
        auto *widBar = windowMain->get_widgetBar(iRaw, 5);

        widMute->wcb_modified_finished = [&s, widGlobalMute, widBar](tre::ui::widget *w)
        {
          tre::ui::widgetBoxCheck *wMute = static_cast<tre::ui::widgetBoxCheck*>(w);
          s.control().m_target.m_volume = !widGlobalMute->get_value() && !wMute->get_value() ? widBar->get_value() : 0.f;
          s.control().m_targetDelay = 0.f;
        };

        widBar->wcb_modified_ongoing = [&s, widGlobalMute, widMute](tre::ui::widget *w)
        {
          tre::ui::widgetBar *wBar = static_cast<tre::ui::widgetBar*>(w);
          s.control().m_target.m_volume = !widGlobalMute->get_value() && !widMute->get_value() ? wBar->get_value() : 0.f;
          s.control().m_targetDelay = 0.2f;
        };

        s.control().m_target.m_volume = 0.5f;
        s.control().m_targetDelay = 2.f;

        auto     *wUVmeter = new widgetUVmeter();
        unsigned localCountdown = 10;
        wUVmeter->wcb_animate = [&s, localCountdown](tre::ui::widget *w, float dt) mutable
        {
          widgetUVmeter *wM = static_cast<widgetUVmeter*>(w);
          if (s.feedback().m_playedSampleCount == 0 && --localCountdown != 0) return;
          localCountdown = 10;
          const float aP = (s.feedback().m_playedLevelPeak > wM->wvalue.m_peak) ? 1.f : 1.f - expf(-dt/0.020f);
          const float aR = (s.feedback().m_playedLevelRMS  > wM->wvalue.m_RMS ) ? 1.f : 1.f - expf(-dt/0.100f);
          wM->wvalue.m_peak = (1.f - aP) * wM->wvalue.m_peak + aP * s.feedback().m_playedLevelPeak;
          wM->wvalue.m_RMS  = (1.f - aR) * wM->wvalue.m_RMS  + aR * s.feedback().m_playedLevelRMS;
          wM->setValueModified();
        };
        windowMain->set_widget(wUVmeter, iRaw, 6);

        auto  *wWaveform = new widgetWaveform();
        {
          TRE_ASSERT(iWaveform < textureWaveformMaxCount);
          widgetWaveform::genWaveForm(*s.audioData(), textureWaveformLoading, 0 + 32 * iWaveform, 32 + 32 * iWaveform);
          wWaveform->set_texId(textureWaveformSlot)->set_texUV(glm::vec4(0.f, iWaveform * 1.f / textureWaveformMaxCount, 1.f, (iWaveform + 1.f) / textureWaveformMaxCount));
          ++iWaveform;
        }
        wWaveform->wcb_animate = [&s](tre::ui::widget *w, float )
        {
          widgetWaveform *wW = static_cast<widgetWaveform*>(w);
          wW->setCursor(float(s.feedback().m_playedSampleCursor)/float(s.audioData()->size()));
        };
        windowMain->set_widget(wWaveform, iRaw, 7);

        ++iRaw;
      }
    }

    {
      windowMain->create_widgetText(iRaw, 0)->set_text("reset all time-cursors")
                ->set_withborder(true)->set_isactive(true)
                ->wcb_clicked_left = [](tre::ui::widget *)
      {
        for (auto & s : listSound)
        {
          s.m_tracks[0].control().setCursor(0);
        }
        for (auto & m : listMusic)
        {
          for (tre::sound2D &s : m.m_tracks) s.control().setCursor(0);
        }
      };
      ++iRaw;

    }

    textureWaveforms.load(textureWaveformLoading, 0, true);
  }

  if (withGUI)
  {
    baseUI.loadShader();
    baseUI.loadIntoGPU();
  }

  windowCtx.m_timing.initialize();

  return 0;
}

// =============================================================================

static void app_update()
{

  windowCtx.m_controls.newFrame();
  windowCtx.m_timing.newFrame(0, false);

  // Event
  SDL_Event rawEvent;
  while(SDL_PollEvent(&rawEvent) == 1)
  {
    windowCtx.SDLEvent_onWindow(rawEvent);
    windowCtx.m_controls.treatSDLEvent(rawEvent);
    baseUI.acceptEvent(rawEvent);
  }

  if (windowCtx.m_controls.m_viewportResized)
  {
    baseUI.updateCameraInfo(windowCtx.m_matProjection2D, windowCtx.m_resolutioncurrent);
  }

  // Audio update

  if (withAudio)
  {
    audioCtx.updateSystem();
  }

  // Video update

  glViewport(0, 0, windowCtx.m_resolutioncurrent.x,windowCtx.m_resolutioncurrent.y);
  glClear(GL_COLOR_BUFFER_BIT);

  if (withGUI)
  {
    baseUI.animate(windowCtx.m_timing.frametime);
    baseUI.updateIntoGPU();
    baseUI.draw();
  }

  SDL_GL_SwapWindow(windowCtx.m_window); // let the v-sync do the job ...
}

// =============================================================================

static void app_quit()
{
  TRE_LOG("Main loop exited");

  audioCtx.stopSystem();

  baseUI.clearShader();
  baseUI.clearGPU();
  baseUI.clear();
  font.clear();

  textureWaveforms.clear();

  windowCtx.OpenGLQuit();
  windowCtx.SDLQuit();

  TRE_LOG("End.");
}

// =============================================================================


int main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  if (app_init() != 0)
    return -1;

#ifdef TRE_EMSCRIPTEN
  //emscripten_request_animation_frame_loop(app_update, nullptr);
  emscripten_set_main_loop(app_update, 0, true);

  // emscripten_set_fullscreenchange_callback
  // emscripten_set_canvas_element_size
#else
  while(!windowCtx.m_controls.m_quit)
  {
    app_update();
  }

  app_quit();

#endif

  return 0;
}
