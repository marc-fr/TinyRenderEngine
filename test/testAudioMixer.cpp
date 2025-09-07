
#include <iostream>
#include <sstream>
#include <array>
#include <random>

#include "tre_windowContext.h"
#include "tre_ui.h"
#include "tre_font.h"

#include "tre_audio.h"

#ifdef TRE_EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#ifndef TESTIMPORTPATH
#define TESTIMPORTPATH ""
#endif

// =============================================================================

class soundProceduralNoise : public tre::soundInterface
{
public:
  bool  m_isPlaying = false;
  bool  m_requestReset = false;
  float m_requestGain = 0.5f;
  float m_playedLevelPeak = 0.f;
  float m_playedLevelRMS  = 0.f;

protected:
  bool  ac_isPlaying = false;
  float ac_gain = 0.f;
  float ac_gainTarget = 0.f;
  float ac_playedLevelPeak = 0.f;
  float ac_playedLevelRMS  = 0.f;

protected:
  float                            g_lastSample = 0.f;
  std::mt19937                     g_randEngine;
  std::normal_distribution<float>  g_randDistGaussian;

public:

  enum class e_noiseColor
  {
    NOISE_BROWN,
    NOISE_WHITE,
  };
  e_noiseColor m_noiseColor;

  soundProceduralNoise(e_noiseColor nc)
  {
    m_noiseColor = nc;
    g_randEngine.seed(0);
    g_randDistGaussian = std::normal_distribution<float>(0.f, 1.f);
  }

  virtual void sync()
  {
    // send
    ac_isPlaying = m_isPlaying;
    if (m_requestReset)
    {
      g_randEngine.seed(0);
      m_requestReset = false;
    }
    ac_gainTarget = m_requestGain;
    // receive
    m_playedLevelPeak = ac_playedLevelPeak;
    m_playedLevelRMS  = ac_playedLevelRMS ;
  }

  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
  {
    ac_playedLevelPeak = 0.f;
    ac_playedLevelRMS  = 0.f;
    if (!ac_isPlaying) return;

    const float dt = 1.f / float(sampleFreq);
    const float kGainFade = -float(sampleCount) / 0.1f;
    float sAbsMax = 0.f;
    float sRMS = 0.f;

    switch (m_noiseColor)
    {
      case e_noiseColor::NOISE_BROWN:
        for (unsigned isample = 0; isample < sampleCount; ++isample)
        {
          const float g = ac_gainTarget + (ac_gain - ac_gainTarget) * std::exp(kGainFade * dt);
          const float rd = g_randDistGaussian(g_randEngine);
          g_lastSample = 0.99f * g_lastSample + 0.04f * rd; // leak integrator of gaussian noise
          const float v = g * g_lastSample;
          outBufferAdd[0] += v;
          outBufferAdd[1] += v;
          outBufferAdd += 2;
          sAbsMax = std::max(sAbsMax, std::abs(v));
          sRMS += v * v;
        }
        break;
      case e_noiseColor::NOISE_WHITE:
        for (unsigned isample = 0; isample < sampleCount; ++isample)
        {
          const float g = ac_gainTarget + (ac_gain - ac_gainTarget) * std::exp(kGainFade * dt);
          const float v = g * 0.45f * g_randDistGaussian(g_randEngine);
          outBufferAdd[0] += v;
          outBufferAdd[1] += v;
          outBufferAdd += 2;
          sAbsMax = std::max(sAbsMax, std::abs(v));
          sRMS += v * v;
        }
        break;
    }

    ac_gain = ac_gainTarget + (ac_gain - ac_gainTarget) * std::exp(kGainFade * dt);
    ac_playedLevelPeak = sAbsMax;
    ac_playedLevelRMS  = std::sqrt(sRMS / sampleCount);
  }
};

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

  virtual s_drawElementCount get_drawElementCount() const override
  {
    s_drawElementCount res;
    res.m_vcountSolid = 18;
    return res;
  }

  virtual glm::vec2 get_zoneSizeDefault(const s_drawData &dd) const override
  {
    const float h = dd.resolve_sizeH(m_parentWindow->get_lineHeight());
    return glm::vec2(5.f * h, h);
  }

  void compute_data(s_drawData &dd) override
  {
    tre::ui::fillRect(dd.m_bufferSolid, m_zone, glm::vec4(0.f));

    const float valx1 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue.m_peak);
    const glm::vec4 zoneC1 = glm::vec4(0.5f * wvalue.m_peak, 0.5f * (1.f - wvalue.m_peak), 0.f, 1.f);

    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    *dd.m_bufferSolid++ = glm::vec4(valx1   , m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC1;
    *dd.m_bufferSolid++ = glm::vec4(valx1   , m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC1;
    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    *dd.m_bufferSolid++ = glm::vec4(valx1   , m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC1;
    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);

    const float valx2 = m_zone.x + (m_zone.z-m_zone.x) * (wvalue.m_RMS);
    const glm::vec4 zoneC2 = glm::vec4(wvalue.m_peak, 1.f - wvalue.m_peak, 0.f, 1.f);

    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    *dd.m_bufferSolid++ = glm::vec4(valx2   , m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC2;
    *dd.m_bufferSolid++ = glm::vec4(valx2   , m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC2;
    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.y, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);
    *dd.m_bufferSolid++ = glm::vec4(valx2   , m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = zoneC2;
    *dd.m_bufferSolid++ = glm::vec4(m_zone.x, m_zone.w, 0.f, 0.f); *dd.m_bufferSolid++ = glm::vec4(0.f, 0.5f, 0.f, 1.f);
  }

  void setValueModified() { setUpdateNeededData(); }
};

// =============================================================================

class widgetWaveform : public tre::ui::widgetPicture
{
public:

  virtual s_drawElementCount get_drawElementCount() const override
  {
    s_drawElementCount res = tre::ui::widgetPicture::get_drawElementCount();
    res.m_vcountLine += 2;
    return res;
  }

  float wcursor = 0.f;

  void setCursor(float c) { if (c != wcursor) { wcursor = c; setUpdateNeededData(); } }

  virtual void compute_data(s_drawData &dd) override
  {
    tre::ui::widgetPicture::compute_data(dd);

    const float xC = m_zone.x + (m_zone.z - m_zone.x) * wcursor;
    const glm::vec2 pA = glm::vec2(xC, m_zone.y);
    const glm::vec2 pB = glm::vec2(xC, m_zone.w);
    tre::ui::fillLine(dd.m_bufferLine, pA, pB, glm::vec4(0.3f, 1.f, 0.3f, 1.f));
  }

  template<class _soundData, class _soundSampler>
  static void genWaveForm(const _soundData &data, SDL_Surface *tex, uint y0, uint y1)
  {
    TRE_ASSERT(tex->format->BytesPerPixel == 4);
    const uint halfHeight_px = (y1 - y0 - 1) / 2;
    const uint zeroLine_px = y0 + halfHeight_px;
    const uint width = tex->w;
    uint32_t * __restrict pixels = reinterpret_cast<uint32_t*>(tex->pixels);

    const uint samples = data.m_nSamples;
    if (samples == 0) return;
    const uint samplesPerPixel = std::max(1u, samples / width);
    std::vector<float> buffer;
    buffer.resize(samplesPerPixel * 2);

    _soundSampler                    sampler;
    tre::soundSampler::s_noControl   samplerControl;

    sampler.m_repet = false;

    for (uint x = 0; x < width; ++x)
    {
      sampler.m_cursor = (x * samples) / width;
      sampler.m_valuePeak = 0.f;
      sampler.m_valueRMS = 0.f;

      memset(buffer.data(), 0, samplesPerPixel * 2 * sizeof(float));

      sampler.sample(data, samplerControl, samplerControl, buffer.data(), samplesPerPixel, data.m_freq);

      float valuePeak = sampler.m_valuePeak;
      float valueRMS = sampler.m_valueRMS;

      valuePeak = (std::max(20.f * std::log(valuePeak / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
      valueRMS  = (std::max(20.f * std::log(valueRMS  / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]

      const uint valuePeak_px = valuePeak * halfHeight_px;
      const uint valueRMS_px = valueRMS * halfHeight_px;

      for (uint y = zeroLine_px - valuePeak_px, yE = zeroLine_px + 1 + valuePeak_px; y < yE; ++y) pixels[x + width * y] = 0xFF000080;
      for (uint y = zeroLine_px - valueRMS_px , yE = zeroLine_px + 1 + valueRMS_px ; y < yE; ++y) pixels[x + width * y] = 0xFF4040FF;
    }
  }
};

// =============================================================================

static tre::windowContext myWindow;
static tre::windowContext::s_controls myControls;
static tre::windowContext::s_timer myTimings;

static tre::font         font;
static tre::baseUI2D     baseUI;
static tre::ui::window   *windowMain = nullptr;

static bool withAudio = true;

static const std::string                nameClick = "music-click.wav";
static const std::string                nameWave = "sin440Hz.wav";
static const std::string                namePiano = "music-piano.opus";
static const std::string                nameStrings = "music-strings.opus";
static const std::array<std::string, 2> nameTracks = { "music-base.wav", "music-clav.wav" };

std::array<tre::soundData::s_RawSDL, 4> audioDataRaw;
std::array<tre::soundData::s_Opus, 2>   audioDataOpus;

struct s_music
{
  std::array<tre::soundData::s_Opus, 2> m_audioDataCompressed;
  std::array<tre::sound2D, 2>           m_tracks;
  std::string                           m_name;
  unsigned                              m_compression;

  s_music(const std::string &name, tre::soundData::s_RawSDL *data) : m_name(name), m_compression(0)
  {
    m_tracks[0].setAudioData(data);
  }

  s_music(const std::string &name, tre::soundData::s_Opus *data) : m_name(name), m_compression(0)
  {
    m_tracks[0].setAudioData(data);
  }

  s_music(const std::string &name, unsigned compression) : m_name(name), m_compression(compression)
  {
    for (std::size_t i = 0; i < m_tracks.size(); ++i)
      m_tracks[i].setAudioData(&m_audioDataCompressed[i]);
  }
};

static std::array<s_music, 4> listSound = { s_music("click", &audioDataRaw[0]), s_music("sin440Hz", &audioDataRaw[1]), s_music("piano.opus", &audioDataOpus[0]), s_music("strings.opus", &audioDataOpus[1]) }; // only the first track is used here.
static std::array<s_music, 4> listMusic = { s_music("origin", 0U), s_music("compression 64kb", 64000U), s_music("compression 32kb", 32000U), s_music("compression 16kb", 16000U) };

static std::array<soundProceduralNoise, 2> trackProceduralNoises = { soundProceduralNoise(soundProceduralNoise::e_noiseColor::NOISE_BROWN),
                                                                     soundProceduralNoise(soundProceduralNoise::e_noiseColor::NOISE_WHITE) };

static tre::audioContext audioCtx;

static tre::texture textureWaveforms;
static const unsigned textureWaveformMaxCount = 8;

// =============================================================================

static int app_init()
{
  // - Init

  if(!myWindow.SDLInit(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    return -1;

  // Retreive display information
  SDL_DisplayMode currentdm;
  SDL_GetDesktopDisplayMode(0,&currentdm);
  TRE_LOG("SDL Desktop resolution : " << currentdm.w << " * " << currentdm.h);

  if (!myWindow.SDLCreateWindow(int(currentdm.w * 0.8 / 8)*8, int(currentdm.h * 0.8 / 8)*8, "test Audio Mixer", SDL_WINDOW_RESIZABLE))
    return -2;

  if(!myWindow.OpenGLInit())
    return -3;

  // -> Set pipeline state and clear the window

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_DEPTH_TEST);
  glClearColor(0.f,0.f,0.f,0.f);
  glClear(GL_COLOR_BUFFER_BIT);

  SDL_GL_SwapWindow(myWindow.m_window);

  // -> Load other resources

  if (!font.load({ tre::font::loadFromBMPandFNT(TESTIMPORTPATH "resources/font_arial_88") }, true))
  {
    TRE_LOG("Fail to load the font \"font_arial_88\". Fallback to intern font");
    font.loadProceduralLed(2, 0);
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

  // -> Load music

  if (withAudio)
  {
    // load from files
    audioDataRaw[0].loadFromWAV((TESTIMPORTPATH "resources/") + nameClick);
    audioDataRaw[1].loadFromWAV((TESTIMPORTPATH "resources/") + nameWave);

    for (std::size_t i = 0; i < nameTracks.size(); ++i)
      audioDataRaw[2 + i].loadFromWAV((TESTIMPORTPATH "resources/") + nameTracks[i]);

    audioDataOpus[0].loadFromOPUS((TESTIMPORTPATH "resources/") + namePiano);
    audioDataOpus[1].loadFromOPUS((TESTIMPORTPATH "resources/") + nameStrings);

    // music-origin
    for (std::size_t i = 0; i < nameTracks.size(); ++i)
    {
      listMusic[0].m_tracks[i].setAudioData((tre::soundData::s_Opus*)nullptr); // unset from constructor
      listMusic[0].m_tracks[i].setAudioData(&audioDataRaw[2 + i]);
    }

    // perform compression
    for (std::size_t k = 1; k < listMusic.size(); ++k)
    {
      for (std::size_t i = 0; i < nameTracks.size(); ++i)
      {
        listMusic[k].m_audioDataCompressed[i].loadFromRaw(audioDataRaw[2 + i], listMusic[k].m_compression);
      }
    }

    // add all to the sound-context

    for (std::size_t k = 0; k < listSound.size(); ++k)
    {
      audioCtx.addSound(&listSound[k].m_tracks[0]);
    }

    for (std::size_t k = 0; k < listMusic.size(); ++k)
    {
      for (std::size_t i = 0; i < nameTracks.size(); ++i)
      {
        audioCtx.addSound(&listMusic[k].m_tracks[i]);
      }
    }

    for (std::size_t k = 0; k < trackProceduralNoises.size(); ++k)
    {
      audioCtx.addSound(&trackProceduralNoises[k]);
    }
  }

  // -> Create the UI

   windowMain = baseUI.create_window();

  {
    baseUI.set_defaultFont(&font);
    baseUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);

    windowMain->set_fontSize(tre::ui::s_size(20,tre::ui::SIZE_PIXEL));
    windowMain->set_alignMask(tre::ui::ALIGN_MASK_CENTERED);
    windowMain->set_mat3(glm::mat3(1.f));
    windowMain->set_layoutGrid(64,8);
    windowMain->set_cellMargin(tre::ui::s_size(2,tre::ui::SIZE_PIXEL));
    windowMain->create_widgetText(0,0)->set_text("name")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,1)->set_text("play")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,3)->set_text("repeat")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,4)->set_text("mute")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,5)->set_text("volume")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,6)->set_text("UV meter")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
    windowMain->create_widgetText(0,7)->set_text("Waveform")->set_color(glm::vec4(1.f,1.f,0.f,1.f));
  }

  if (withAudio)
  {
    unsigned iRaw = 1;

    const unsigned textureWaveformSlot = baseUI.addTexture(&textureWaveforms);

    // compute WAV-form
    SDL_Surface *textureWaveformLoading = SDL_CreateRGBSurface(0, 128, 32 * textureWaveformMaxCount, 32, 0, 0, 0, 0);

    TRE_ASSERT(audioDataRaw.size() == 4); // patch the code.
    TRE_ASSERT(audioDataOpus.size() == 2); // patch the code.
    widgetWaveform::genWaveForm<tre::soundData::s_RawSDL, tre::soundSampler::s_sampler_Raw >(audioDataRaw[0] , textureWaveformLoading, 0 + 32 * 0, 32 + 32 * 0);
    widgetWaveform::genWaveForm<tre::soundData::s_RawSDL, tre::soundSampler::s_sampler_Raw >(audioDataRaw[1] , textureWaveformLoading, 0 + 32 * 1, 32 + 32 * 1);
    widgetWaveform::genWaveForm<tre::soundData::s_Opus,   tre::soundSampler::s_sampler_Opus>(audioDataOpus[0], textureWaveformLoading, 0 + 32 * 2, 32 + 32 * 2);
    widgetWaveform::genWaveForm<tre::soundData::s_Opus,   tre::soundSampler::s_sampler_Opus>(audioDataOpus[1], textureWaveformLoading, 0 + 32 * 3, 32 + 32 * 3);
    widgetWaveform::genWaveForm<tre::soundData::s_RawSDL, tre::soundSampler::s_sampler_Raw >(audioDataRaw[2] , textureWaveformLoading, 0 + 32 * 4, 32 + 32 * 4);
    widgetWaveform::genWaveForm<tre::soundData::s_RawSDL, tre::soundSampler::s_sampler_Raw >(audioDataRaw[3] , textureWaveformLoading, 0 + 32 * 5, 32 + 32 * 5);

    unsigned isound = 0;
    for (auto &s : listSound)
    {
      // main row for music.
      {
        char txtBuf[32];
        std::snprintf(txtBuf, 32,"sound: %s", s.m_name.c_str());
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
          const float valuePeak = (std::max(20.f * std::log(s.m_tracks[0].feedback().m_playedLevelPeak / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
          const float valueRMS  = (std::max(20.f * std::log(s.m_tracks[0].feedback().m_playedLevelRMS  / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
          const float aP = (valuePeak > wM->wvalue.m_peak) ? 1.f : 1.f - expf(-dt/0.020f);
          const float aR = (valueRMS  > wM->wvalue.m_RMS ) ? 1.f : 1.f - expf(-dt/0.100f);
          wM->wvalue.m_peak = (1.f - aP) * wM->wvalue.m_peak + aP * valuePeak;
          wM->wvalue.m_RMS  = (1.f - aR) * wM->wvalue.m_RMS  + aR * valueRMS ;
          wM->setValueModified();
        };
        windowMain->set_widget(wUVmeter, iRaw, 6);

        auto  *wWaveform = new widgetWaveform();

        wWaveform->set_texId(textureWaveformSlot)->set_texUV(glm::vec4(0.f, (isound * 1.f) / textureWaveformMaxCount, 1.f, (isound + 1.f) / textureWaveformMaxCount));

        wWaveform->wcb_animate = [&s, isound](tre::ui::widget *w, float )
        {
          widgetWaveform *wW = static_cast<widgetWaveform*>(w);
          wW->setCursor(float(s.m_tracks[0].feedback().m_playedSampleCursor)/float(audioDataRaw[isound].m_nSamples));
        };
        windowMain->set_widget(wWaveform, iRaw, 7);

      }
      ++iRaw;
      ++isound;
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
      unsigned itrack = 0;
      for (tre::sound2D &s : m.m_tracks)
      {
        windowMain->create_widgetText(iRaw, 0)->set_text("- track " + nameTracks[itrack])->set_color(glm::vec4(0.6f, 0.6f, 0.8f, 1.f));

        windowMain->create_widgetBoxCheck(iRaw, 4)->set_value(false)->set_isactive(true)->set_iseditable(true)->set_color(glm::vec4(0.6f, 0.6f, 0.8f, 0.4f));
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
          const float valuePeak = (std::max(20.f * std::log(s.feedback().m_playedLevelPeak / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
          const float valueRMS  = (std::max(20.f * std::log(s.feedback().m_playedLevelRMS  / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
          const float aP = (valuePeak> wM->wvalue.m_peak) ? 1.f : 1.f - expf(-dt/0.020f);
          const float aR = (valueRMS > wM->wvalue.m_RMS ) ? 1.f : 1.f - expf(-dt/0.100f);
          wM->wvalue.m_peak = (1.f - aP) * wM->wvalue.m_peak + aP * valuePeak;
          wM->wvalue.m_RMS  = (1.f - aR) * wM->wvalue.m_RMS  + aR * valueRMS ;
          wM->setValueModified();
        };
        windowMain->set_widget(wUVmeter, iRaw, 6);

        auto  *wWaveform = new widgetWaveform();

        wWaveform->set_texId(textureWaveformSlot)->set_texUV(glm::vec4(0.f, (listSound.size() + itrack) * 1.f / textureWaveformMaxCount, 1.f, (listSound.size() + itrack + 1.f) / textureWaveformMaxCount));

        wWaveform->wcb_animate = [&s, itrack](tre::ui::widget *w, float )
        {
          widgetWaveform *wW = static_cast<widgetWaveform*>(w);
          wW->setCursor(float(s.feedback().m_playedSampleCursor)/float(audioDataRaw[2 + itrack].m_nSamples));
        };
        windowMain->set_widget(wWaveform, iRaw, 7);

        ++iRaw;
        ++itrack;
      }
    }

    for (std::size_t k = 0; k < trackProceduralNoises.size(); ++k)
    {
      windowMain->create_widgetText(iRaw, 0)->set_text("procedural noise");

      windowMain->create_widgetBoxCheck(iRaw, 1)->set_value(false)->set_isactive(true)->set_iseditable(true)
                ->wcb_modified_finished = [k](tre::ui::widget *w)
      {
        tre::ui::widgetBoxCheck *wBox = static_cast<tre::ui::widgetBoxCheck*>(w);
        trackProceduralNoises[k].m_isPlaying = wBox->get_value();
      };
      trackProceduralNoises[k].m_isPlaying = false;

      windowMain->create_widgetBoxCheck(iRaw, 4)->set_value(false)->set_isactive(true)->set_iseditable(true);
      windowMain->create_widgetBar(iRaw, 5)->set_value(0.5f)->set_isactive(true)->set_iseditable(true);

      auto *widMute = windowMain->get_widgetBoxCheck(iRaw, 4);
      auto *widBar = windowMain->get_widgetBar(iRaw, 5);

      widMute->wcb_modified_finished = [widBar, k](tre::ui::widget *w)
      {
        tre::ui::widgetBoxCheck *wMute = static_cast<tre::ui::widgetBoxCheck*>(w);
        trackProceduralNoises[k].m_requestGain = !wMute->get_value() ? widBar->get_value() : 0.f;
      };

      widBar->wcb_modified_ongoing = [widMute, k](tre::ui::widget *w)
      {
        tre::ui::widgetBar *wBar = static_cast<tre::ui::widgetBar*>(w);
        trackProceduralNoises[k].m_requestGain = !widMute->get_value() ? wBar->get_value() : 0.f;
      };

      auto     *wUVmeter = new widgetUVmeter();
      unsigned localCountdown = 10;
      wUVmeter->wcb_animate = [localCountdown, k](tre::ui::widget *w, float dt) mutable
      {
        widgetUVmeter *wM = static_cast<widgetUVmeter*>(w);
        if (--localCountdown != 0) return;
        localCountdown = 10;
        const float valuePeak = (std::max(20.f * std::log(trackProceduralNoises[k].m_playedLevelPeak / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
        const float valueRMS  = (std::max(20.f * std::log(trackProceduralNoises[k].m_playedLevelRMS  / 1.f), -80.f) + 80.f) / 80.f; // dB scale, clamped between [-80, 0]
        const float aP = (valuePeak> wM->wvalue.m_peak) ? 1.f : 1.f - expf(-dt/0.020f);
        const float aR = (valueRMS > wM->wvalue.m_RMS ) ? 1.f : 1.f - expf(-dt/0.100f);
        wM->wvalue.m_peak = (1.f - aP) * wM->wvalue.m_peak + aP * valuePeak;
        wM->wvalue.m_RMS  = (1.f - aR) * wM->wvalue.m_RMS  + aR * valueRMS ;
        wM->setValueModified();
      };
      windowMain->set_widget(wUVmeter, iRaw, 6);

      ++iRaw;
    }

    {
      windowMain->create_widgetText(iRaw, 0, 1, 8)->set_text("reset all time-cursors")
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

    {
      float avgTimeMSSmooth = 0.f;
      windowMain->create_widgetText(iRaw, 0, 1, 8)->wcb_animate = [avgTimeMSSmooth](tre::ui::widget *w, float ) mutable
      {
        if (audioCtx.getPerfTime_nbrCall() == 0) return;
        tre::ui::widgetText *wText = static_cast<tre::ui::widgetText*>(w);
        char txt[128];
        const float avgTimeMS = audioCtx.getPerfTime_nbrCall() == 0 ? 0.f : audioCtx.getPerfTime_Total() / audioCtx.getPerfTime_nbrCall() * 1000.f;
        avgTimeMSSmooth = 0.95f * avgTimeMSSmooth + 0.05f * avgTimeMS;
        const unsigned avgSample = audioCtx.getPerfTime_nbrCall() == 0 ? 0 : audioCtx.getPerfTime_nbrSample() / audioCtx.getPerfTime_nbrCall();
        const float    avgSampleTimeMS = float(avgSample) / float(audioCtx.getAudioSpec()->freq) * 1000.f;
        std::snprintf(txt, 127, "perf[audio-callback]: time = %0.1f ms per call, samples = %d (%.1f ms)", avgTimeMSSmooth, avgSample, avgSampleTimeMS);
        wText->set_text(txt);
      };
      ++iRaw;
    }
  }

  {
    baseUI.loadShader();
    baseUI.loadIntoGPU();
  }

  myTimings.initialize();

  return 0;
}

// =============================================================================

static void app_update()
{
  myWindow.SDLEvent_newFrame();
  myControls.newFrame();
  myTimings.newFrame(0, false);

  // Event
  SDL_Event rawEvent;
  while(SDL_PollEvent(&rawEvent) == 1)
  {
    myWindow.SDLEvent_onWindow(rawEvent);
    myControls.treatSDLEvent(rawEvent);
    baseUI.acceptEvent(rawEvent);
  }

  if (myWindow.m_viewportResized)
  {
    baseUI.updateCameraInfo(myWindow.m_matProjection2D, myWindow.m_resolutioncurrent);
  }

  // Audio update

  if (withAudio)
  {
    audioCtx.updateSystem();
  }

  // Video update

  glViewport(0, 0, myWindow.m_resolutioncurrent.x,myWindow.m_resolutioncurrent.y);
  glClear(GL_COLOR_BUFFER_BIT);

  {
    baseUI.animate(myTimings.frametime);
    baseUI.updateIntoGPU();
    baseUI.draw();
  }

  SDL_GL_SwapWindow(myWindow.m_window); // let the v-sync do the job ...
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

  myWindow.OpenGLQuit();
  myWindow.SDLQuit();

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
  while(!myWindow.m_quit && !myControls.m_quit)
  {
    app_update();
  }

  app_quit();

#endif

  return 0;
}
