
#include <vector>
#include <array>

#include "tre_utils.h"
#include "tre_ui.h"
#include "tre_font.h"
#include "tre_windowContext.h"
#include "tre_audio.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>   // link comdlg32.lib
#endif

static constexpr float pi = 3.14159265358979323846f;

// File open ==================================================================

namespace fileOpener
{

static std::vector<uint8_t> _fileBuffer;
static bool                 _fileLoaded = false; // Note: no atomic needed here (win is blocker, web is safe)

/// Load a file from a path (desktop), and make it available to "pollRequest".
static bool loadPath(const char *path)
{
  size_t size = 0;
  void *data = SDL_LoadFile(path, &size);
  if (data == nullptr)
  {
    TRE_LOG("fileOpener: failed to load " << path << " (" << SDL_GetError() << ")");
    return false;
  }
  _fileBuffer = std::vector<uint8_t>(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
  _fileLoaded = true;
  SDL_free(data);
  return true;
}

#if defined(__EMSCRIPTEN__)

// Note: link with -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_audio_file_picked -sEXPORTED_RUNTIME_METHODS=HEAPU8
EM_JS(void, js_open_audio_dialog, (), {
  const input = document.createElement('input');
  input.type = 'file';
  input.accept = '.wav,.mp3,audio/wav,audio/mpeg';
  input.onchange = async (e) => {
    const f = e.target.files[0];
    if (!f) return;
    const buf = new Uint8Array(await f.arrayBuffer());
    const ptr = _malloc(buf.length);
    HEAPU8.set(buf, ptr);
    _audio_file_picked(ptr, buf.length);
  };
  input.click();
});

static void browseRequest()
{
  js_open_audio_dialog();
}

#elif defined(_WIN32)

static void browseRequest()
{
  wchar_t path[MAX_PATH] = L"";
  OPENFILENAMEW ofn{};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = L"Audio (wav, mp3)\0*.wav;*.mp3\0All\0*.*\0";
  ofn.lpstrFile   = path;
  ofn.nMaxFile    = MAX_PATH;
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
  if (!GetOpenFileNameW(&ofn)) return;

  char utf8[MAX_PATH * 4];
  WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
  loadPath(utf8);
}

#else

static void browseRequest()
{
  TRE_LOG("fileOpener: no file dialog on this platform. Pass the file path as the first program argument.");
}

#endif

/// async request of the (async)-loaded file
static bool pollRequest(std::vector<uint8_t> &fileBuffer)
{
  if (!_fileLoaded) return false;
  _fileLoaded = false;
  if (_fileBuffer.empty()) return false;
  fileBuffer = std::move(_fileBuffer);
  _fileBuffer.clear();
  return true;
}

/// Decode a WAV or MP3 file-buffer into mono float samples.
static bool decodeAudio(const std::vector<uint8_t> &fileBuffer, std::vector<float> &mono, int &freq)
{
  tre::soundData::s_RawSDL raw;
  const bool isWAV = fileBuffer.size() >= 12 && std::memcmp(fileBuffer.data(), "RIFF", 4) == 0 && std::memcmp(fileBuffer.data() + 8, "WAVE", 4) == 0;
  const bool loaded = isWAV ? raw.loadFromWAV(fileBuffer) : raw.loadFromMP3(fileBuffer);
  if (!loaded || raw.m_nSamples == 0 || raw.m_freq <= 0) return false;

  static constexpr float k = 1.f / 32767.f;
  mono.resize(raw.m_nSamples);
  if (raw.m_stereo)
  {
    for (unsigned i = 0; i < raw.m_nSamples; ++i) mono[i] = 0.5f * k * (float(raw.m_rawData[2 * i + 0]) + float(raw.m_rawData[2 * i + 1]));
  }
  else
  {
    for (unsigned i = 0; i < raw.m_nSamples; ++i) mono[i] = k * float(raw.m_rawData[i]);
  }
  freq = raw.m_freq;
  return true;
}

} // namespace fileOpener

#if defined(__EMSCRIPTEN__)
extern "C" EMSCRIPTEN_KEEPALIVE void audio_file_picked(uint8_t* data, int size)
{
  fileOpener::_fileBuffer = std::vector<uint8_t>(data, data + size);
  fileOpener::_fileLoaded = true;
  free(data);
}
#endif

// Sound generators ===========================================================

class c_soundTicker : public tre::soundInterface
{
public:
  bool     m_play = false;
  float    m_volume = 0.5f;
  int      m_tempo = 60;
  static constexpr int kTempoMin = 10;
  static constexpr int kTempoMax = 320;
  unsigned m_tempoSilenceCount = 0;
  int      m_tempoDeltaIncr = 0;
  unsigned m_tempoDeltaTime = 0;
  unsigned m_tickid = 0; // read-back

  static constexpr float kTickFrequency = 1000.f;  ///< [Hz]
  static constexpr float kTickDecay     = 0.002f;  ///< [s], exponential decay time-constant
  static constexpr float kTickDuration  = 0.014f;  ///< [s], 7 time-constants (-60 dB): no click at the end

protected:
  int      m_tempoSent = 0; ///< last tempo exchanged with the audio-thread

  // audio-thread data (exchanged in sync)
  bool     ac_play = false;
  float    ac_volume = 0.f;
  float    ac_volumeCurrent = 0.f;
  unsigned ac_silenceCount = 0;
  int      ac_incrValue = 0;
  unsigned ac_incrTime = 0;
  unsigned ac_cursor = 0;
  unsigned ac_tempo = 0; // current tempo
  unsigned ac_tickid = 0;

public:
  virtual void sync() override final
  {
    m_tempo = glm::clamp(m_tempo, kTempoMin, kTempoMax);
    // tempo: the UI and the audio-thread (auto-increment) can both change it
    if (m_tempo != m_tempoSent) ac_tempo = unsigned(m_tempo); // changed by the UI
    else if (ac_tempo != 0)     m_tempo = int(ac_tempo);      // maybe changed by the audio-thread
    m_tempoSent = m_tempo;
    // send
    ac_play = m_play;
    ac_volume = m_volume;
    ac_silenceCount = m_tempoSilenceCount;
    ac_incrValue = m_tempoDeltaIncr;
    ac_incrTime = m_tempoDeltaTime;
    // receive
    m_tickid = ac_tickid;
  }

  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override final
  {
    if (sampleFreq <= 0 || ac_tempo == 0) return;
    if (!ac_play) { ac_volumeCurrent = 0.f; return; }

    const float    twoPiOverFreq = 2.f * pi * kTickFrequency / float(sampleFreq);
    const float    invDecaySamples = 1.f / (kTickDecay * float(sampleFreq));
    const unsigned tickSamples = unsigned(kTickDuration * float(sampleFreq));
    unsigned       tickLen = unsigned(sampleFreq) * 60 / ac_tempo;
    bool           tickEnabled = _isTickEnabled();

    const float    volumeStart = ac_volumeCurrent;
    const float    volumeStep = (sampleCount != 0) ? (ac_volume - ac_volumeCurrent) / float(sampleCount) : 0.f;

    for (unsigned i = 0; i < sampleCount; ++i, ++ac_cursor)
    {
      if (ac_cursor >= tickLen)
      {
        ac_cursor = 0;
        ++ac_tickid;
        tickEnabled = _isTickEnabled();
        if (ac_incrTime != 0 && ac_tickid % ac_incrTime == 0)
        {
          ac_tempo = unsigned(glm::clamp(int(ac_tempo) + ac_incrValue, kTempoMin, kTempoMax)); // sent back to the main-thread in sync()
          tickLen = unsigned(sampleFreq) * 60 / ac_tempo;
        }
      }
      if (tickEnabled && ac_cursor < tickSamples)
      {
        const float t = float(ac_cursor);
        const float v = (volumeStart + volumeStep * float(i)) * std::sin(t * twoPiOverFreq) * std::exp(-t * invDecaySamples);
        outBufferAdd[2*i+0] += v;
        outBufferAdd[2*i+1] += v;
      }
    }
    ac_volumeCurrent = ac_volume;
  }

private:
  bool _isTickEnabled() const
  {
    if (ac_silenceCount == 0) return true;
    return (ac_tickid % (2 * ac_silenceCount)) < ac_silenceCount;
  }
};

// Algorithm ==================================================================

/// Streaming phase vocoder with identity phase locking (Laroche & Dolson, 1999).
/// Changes the duration without changing the pitch.
/// Each "process" call produces k_hopS output samples, and moves the input position by k_hopS / timeScale.
class c_phaseVocoder
{
public:
  static constexpr unsigned k_N    = 2048;      ///< analysis/synthesis window (power of two)
  static constexpr unsigned k_hopS = k_N / 4;   ///< synthesis hop (75% overlap)
  static constexpr unsigned k_bins = k_N / 2 + 1;

  bool m_phaseLocking = true; ///< identity phase locking (less "phasiness"). False gives the classic phase vocoder.

  c_phaseVocoder()
  {
    for (unsigned n = 0; n < k_N; ++n) m_window[n] = 0.5f - 0.5f * std::cos(2.f * pi * float(n) / float(k_N)); // periodic Hann
    m_spec.resize(k_N);
    m_peaks.reserve(k_bins);
    reset(0.);
  }

  /// Full reset: clears the pending output. The next frame starts at "inputPos" (in input samples).
  void reset(double inputPos)
  {
    m_ola.fill(0.f);
    seek(inputPos);
  }

  /// Jump to "inputPos" with a phase reset. The pending output is kept: the previous content fades out with the window.
  void seek(double inputPos)
  {
    m_inputPos = inputPos;
    m_hasPrev = false;
  }

  double inputPosition() const { return m_inputPos; }

  /// Generate k_hopS output samples. timeScale > 1 means slower (longer output).
  void process(const float *input, std::size_t inputSize, double timeScale, float *out)
  {
    const long pos = long(std::floor(m_inputPos + 0.5));

    // analysis
    for (unsigned n = 0; n < k_N; ++n)
    {
      const long  i = pos + long(n) - long(k_N / 2); // frame centered on the input position
      const float x = (i >= 0 && std::size_t(i) < inputSize) ? input[i] : 0.f;
      m_spec[n] = glm::vec2(x * m_window[n], 0.f);
    }
    tre::fft(m_spec.data(), k_N, false);
    for (unsigned k = 0; k < k_bins; ++k)
    {
      m_mag[k] = glm::length(m_spec[k]);
      m_phaseA[k] = std::atan2(m_spec[k].y, m_spec[k].x);
    }

    // phase propagation
    const long hopA = pos - m_prevPos;
    if (!m_hasPrev || hopA <= 0)
    {
      for (unsigned k = 0; k < k_bins; ++k) m_phaseS[k] = m_phaseA[k]; // phase reset
    }
    else if (m_phaseLocking)
    {
      // peaks = local maxima over +/- 2 bins
      m_peaks.clear();
      for (unsigned k = 2; k + 2 < k_bins; ++k)
      {
        const float m = m_mag[k];
        if (m > m_mag[k-1] && m >= m_mag[k+1] && m > m_mag[k-2] && m >= m_mag[k+2]) m_peaks.push_back(k);
      }
      if (m_peaks.empty()) m_peaks.push_back(1);

      // propagate the peaks with their instantaneous frequency
      for (unsigned p : m_peaks) m_phaseSPeak[p] = _propagate(p, hopA);

      // identity phase locking: each bin keeps its phase offset to its peak (region boundary = mid-point)
      std::size_t ip = 0;
      for (unsigned k = 0; k < k_bins; ++k)
      {
        while (ip + 1 < m_peaks.size() && k > (m_peaks[ip] + m_peaks[ip + 1]) / 2) ++ip;
        const unsigned p = m_peaks[ip];
        m_phaseS[k] = _wrap(m_phaseSPeak[p] + (m_phaseA[k] - m_phaseA[p]));
      }
    }
    else
    {
      for (unsigned k = 0; k < k_bins; ++k) m_phaseS[k] = _propagate(k, hopA);
    }
    m_prevPhaseA = m_phaseA;
    m_prevPhaseS = m_phaseS;
    m_prevPos = pos;
    m_hasPrev = true;

    // synthesis
    for (unsigned k = 0; k < k_bins; ++k) m_spec[k] = m_mag[k] * glm::vec2(std::cos(m_phaseS[k]), std::sin(m_phaseS[k]));
    for (unsigned k = 1; k < k_N / 2; ++k) m_spec[k_N - k] = glm::vec2(m_spec[k].x, -m_spec[k].y); // hermitian -> real output
    tre::fft(m_spec.data(), k_N, true);

    // windowed overlap-add. Normalization: 1/N (inverse fft) and 1/1.5 (sum of Hann^2 at 75% overlap)
    const float scale = 1.f / (float(k_N) * 1.5f);
    for (unsigned n = 0; n < k_N; ++n) m_ola[n] += m_spec[n].x * m_window[n] * scale;
    std::copy_n(m_ola.begin(), k_hopS, out);
    std::copy(m_ola.begin() + k_hopS, m_ola.end(), m_ola.begin());
    std::fill(m_ola.end() - k_hopS, m_ola.end(), 0.f);

    m_inputPos += double(k_hopS) / timeScale;
  }

private:
  static float _wrap(float phase) // to [-pi, pi]: keeps the accumulated phases bounded (float precision)
  {
    return phase - 2.f * pi * std::floor(phase / (2.f * pi) + 0.5f);
  }

  float _propagate(unsigned k, long hopA) const
  {
    const float omega = 2.f * pi * float(k) / float(k_N);
    const float dphi = _wrap(m_phaseA[k] - m_prevPhaseA[k] - omega * float(hopA));
    const float instFreq = omega + dphi / float(hopA);
    return _wrap(m_prevPhaseS[k] + instFreq * float(k_hopS));
  }

  std::array<float, k_N>    m_window;
  std::array<float, k_N>    m_ola;
  std::vector<glm::vec2>    m_spec;
  std::array<float, k_bins> m_mag, m_phaseA, m_phaseS, m_prevPhaseA, m_prevPhaseS, m_phaseSPeak;
  std::vector<unsigned>     m_peaks;
  double                    m_inputPos = 0.;
  long                      m_prevPos = 0;
  bool                      m_hasPrev = false;
};

// -----------------------------------------------------------------------------

/// Player: time-stretch (phase vocoder) + pitch-shift (resampling), processed in the audio-thread.
class c_soundSampler : public tre::soundInterface
{
public:
  typedef std::shared_ptr<const std::vector<float>> t_audioPtr;

  bool     m_play = false;
  float    m_volume = 0.5f;
  float    m_playCursorStart = 0; ///< [0-1], loop start
  float    m_playCursorEnd   = 0; ///< [0-1], loop end
  float    m_playCursor      = 0; ///< [0-1] (read-back)
  float    m_playCursorSet   = -1.f; ///< [0-1] (set when positive)

  float    m_stretcherFactor = 1.f; ///< stretch factor (1 = no change, 2 = twice longer, 0.5 = twice faster)
  float    m_pitchFactor = 1.f; ///< pitch factor (1 = no change, 2 = octave up, 0.5 = octave down, etc.)
  bool     m_phaseLocking = true; ///< stretcher option

  /// Set the input audio (mono). The audio-thread picks it up on the next sync.
  void setInput(std::vector<float> &&samples, int frequency)
  {
    m_input = std::make_shared<const std::vector<float>>(std::move(samples));
    m_inputFreq = frequency;
  }
  const t_audioPtr &input() const     { return m_input; }
  int               inputFreq() const { return m_inputFreq; }
  bool              empty() const     { return !m_input || m_input->empty(); }

  void setStretchFactor(float stretch) { m_stretcherFactor = stretch; }
  void setPitchFactor(float pitch)     { m_pitchFactor = pitch; }

protected:
  t_audioPtr m_input;     ///< main-thread copy
  int        m_inputFreq = 0;

  // audio-thread data (exchanged in sync)
  t_audioPtr     ac_input;
  bool           ac_inputChanged = false;
  bool           ac_play = false;
  float          ac_volume = 0.f;        ///< target gain
  float          ac_gain = 0.f;          ///< current gain (ramped per callback)
  float          ac_stretch = 1.f;
  float          ac_pitch = 1.f;
  int            ac_inputFreq = 0;
  double         ac_loopStart = 0.;      ///< [input sample]
  double         ac_loopEnd = 0.;        ///< [input sample]
  double         ac_seekPos = -1.;       ///< [input sample], when positive
  double         ac_heardPos = 0.;       ///< [input sample], read-back
  bool           ac_loopWrapped = false; ///< the vocoder wrapped at the loop end (since the last seek)
  c_phaseVocoder ac_vocoder;
  std::array<float, 4 * c_phaseVocoder::k_hopS> ac_buf; ///< vocoder output, not played yet
  std::size_t    ac_bufCount = 0;
  double         ac_readPos = 0.;        ///< read position in ac_buf (resampling for the pitch and the frequency)

public:
  virtual void sync() override final
  {
    // receive
    if (ac_input && !ac_input->empty()) m_playCursor = float(ac_heardPos / double(ac_input->size()));
    else                                m_playCursor = 0.f;

    // send
    if (m_input != ac_input)
    {
      ac_input = m_input; // the old buffer (if any) is released here, in the main-thread
      ac_inputChanged = true;
    }
    const double inputSize = ac_input ? double(ac_input->size()) : 0.;

    ac_play = m_play && inputSize > 0.;
    ac_volume = m_volume;
    ac_stretch = glm::clamp(m_stretcherFactor, 0.25f, 4.f);
    ac_pitch = glm::clamp(m_pitchFactor, 0.5f, 2.f);
    ac_inputFreq = m_inputFreq;
    ac_vocoder.m_phaseLocking = m_phaseLocking;

    ac_loopStart = double(glm::clamp(m_playCursorStart, 0.f, 1.f)) * inputSize;
    ac_loopEnd   = double(glm::clamp(m_playCursorEnd  , 0.f, 1.f)) * inputSize;
    if (ac_loopEnd <= ac_loopStart + 1.) { ac_loopStart = 0.; ac_loopEnd = inputSize; }

    if (m_playCursorSet >= 0.f)
    {
      ac_seekPos = double(glm::clamp(m_playCursorSet, 0.f, 1.f)) * inputSize;
      m_playCursorSet = -1.f;
    }
  }

  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override final
  {
    if (sampleFreq <= 0 || !ac_input || ac_input->empty() || ac_inputFreq <= 0) return;

    if (ac_inputChanged)
    {
      ac_inputChanged = false;
      ac_vocoder.reset(ac_loopStart);
      ac_bufCount = 0;
      ac_readPos = 0.;
      ac_loopWrapped = false;
    }
    if (ac_seekPos >= 0.)
    {
      ac_vocoder.seek(ac_seekPos);
      ac_bufCount = 0; // drop the pending output for a responsive seek (the vocoder still fades it out)
      ac_readPos = 0.;
      ac_seekPos = -1.;
      ac_loopWrapped = false;
    }

    // fade-out on pause, fade-in on play: no click
    const float gainTarget = ac_play ? ac_volume : 0.f;
    if (ac_gain == 0.f && gainTarget == 0.f) return;
    const float gainStep = (sampleCount != 0) ? (gainTarget - ac_gain) / float(sampleCount) : 0.f;

    const std::vector<float> &input = *ac_input;
    const double readStep = double(ac_pitch) * double(ac_inputFreq) / double(sampleFreq);

    for (unsigned i = 0; i < sampleCount; ++i)
    {
      for (;;) // need ac_buf[r] and ac_buf[r+1]
      {
        const std::size_t r = std::size_t(ac_readPos);
        if (r + 1 < ac_bufCount) break;
        if (r > 0)
        {
          std::copy(ac_buf.begin() + r, ac_buf.begin() + ac_bufCount, ac_buf.begin());
          ac_bufCount -= r;
          ac_readPos -= double(r);
        }
        _produceHop(input);
      }
      const std::size_t r = std::size_t(ac_readPos);
      const float       w = float(ac_readPos - double(r));
      const float       v = (ac_buf[r] + w * (ac_buf[r + 1] - ac_buf[r])) * (ac_gain + gainStep * float(i));
      outBufferAdd[2*i+0] += v;
      outBufferAdd[2*i+1] += v;
      ac_readPos += readStep;
    }
    ac_gain = gainTarget;

    // read-back: input position of what is heard (vocoder latency + pending output)
    const double timeScale = double(ac_stretch) * double(ac_pitch);
    const double pending = double(c_phaseVocoder::k_N / 2 + c_phaseVocoder::k_hopS) + (double(ac_bufCount) - ac_readPos);
    ac_heardPos = ac_vocoder.inputPosition() - pending / timeScale;
    if (ac_heardPos < ac_loopStart && ac_loopWrapped) ac_heardPos += (ac_loopEnd - ac_loopStart); // just after a loop wrap
    ac_heardPos = glm::clamp(ac_heardPos, 0., double(input.size()));
  }

private:
  void _produceHop(const std::vector<float> &input)
  {
    const double pos = ac_vocoder.inputPosition();
    if (pos >= ac_loopEnd)
    {
      ac_vocoder.seek(ac_loopStart); // loop (the tail fades out)
      ac_loopWrapped = true;
    }
    else if (pos < ac_loopStart)
    {
      ac_vocoder.seek(ac_loopStart); // the loop region has changed
    }
    TRE_ASSERT(ac_bufCount + c_phaseVocoder::k_hopS <= ac_buf.size());
    ac_vocoder.process(input.data(), input.size(), double(ac_stretch) * double(ac_pitch), &ac_buf[ac_bufCount]);
    ac_bufCount += c_phaseVocoder::k_hopS;
  }
};

// UI Widgets =================================================================

static uint32_t colorToRGBA(const glm::vec4 &c, float alpha = 1.f)
{
  const glm::ivec4 cI = glm::clamp(glm::ivec4(c * 255.f + 0.5f), glm::ivec4(0), glm::ivec4(255));
  const int        aI = glm::clamp(int(alpha * 255.f + 0.5f), 0, 255);
  return (aI << 24) | (cI.b << 16) | (cI.g << 8) | (cI.r);
}

class widgetPlayCursor : public tre::ui::widget
{
public:
  float wcursorCurr = 0.f;
  float wcursorA = 0.f;
  float wcursorB = 1.f;
  int   wselectMode = 0; // TODO

  SDL_Surface *wTextureUpload = nullptr;
  tre::texture wTexture;
  unsigned     wTextureSlot = unsigned(-1); ///< slot returned by baseUI::addTexture

  c_soundSampler &wsndSampler;

  bool wDirty = false;
  void set_dirty() { wDirty = true; }

  void updateWaveForm()
  {
    const uint32_t colorLine = colorToRGBA(glm::vec4(0.f, 0.f, 0.f, 1.f));
    const uint32_t colorBack = 0x00000000;
    const uint32_t colorMain = colorToRGBA(get_parentWindow()->get_colortheme().m_colorOnSurface, 0.75f);
    const uint32_t colorFade = colorToRGBA(get_parentWindow()->get_colortheme().m_colorOnSurface, 0.50f);

    uint32_t * __restrict pxData = reinterpret_cast<uint32_t*>(wTextureUpload->pixels);
    const int w = wTextureUpload->w; // only first mip-map level (maybe add in futur)
    const int h = wTextureUpload->h;
    const c_soundSampler::t_audioPtr input = wsndSampler.input();
    const int nSamples = input ? int(input->size()) : 0;
    const int nSamplesPerPixel = nSamples / w;

    const int y0 = int(0.5f * float(h-1) + 0.5f);

    if (nSamplesPerPixel < 1)
    {
      // waveform (interpolated)
      std::fill_n(pxData, w * h, colorBack);
    }
    else if (nSamplesPerPixel < 4)
    {
      // peaks only
      for (int ix = 0; ix < w; ++ix)
      {
        float vMin = +1.f;
        float vMax = -1.f;
        const int sampleStart = ix * nSamplesPerPixel;
        const int sampleEnd = std::min(nSamples, (ix + 1) * nSamplesPerPixel);
        for (int is = sampleStart; is < sampleEnd; ++is)
        {
          const float v = (*input)[is];
          vMin = std::min(vMin, v);
          vMax = std::max(vMax, v);
        }
        const int yMin = int((0.5f * (1.f + vMin)) * float(h-1) + 0.5f);
        const int yMax = int((0.5f * (1.f + vMax)) * float(h-1) + 0.5f);
        for (int iy = 0; iy < h; ++iy)
        {
          uint32_t &px = pxData[iy * w + ix];
          if (iy >= yMin && iy <= yMax) px = colorMain;
          else                          px = colorBack;
        }
        pxData[y0 * w + ix] = colorLine;
      }
    }
    else
    {
      // peaks + RMS
      for (int ix = 0; ix < w; ++ix)
      {
        float vMin = +1.f;
        float vMax = -1.f;
        float vRMS = 0.f;
        const int sampleStart = ix * nSamplesPerPixel;
        const int sampleEnd = std::min(nSamples, (ix + 1) * nSamplesPerPixel);
        for (int is = sampleStart; is < sampleEnd; ++is)
        {
          const float v = (*input)[is];
          vMin = std::min(vMin, v);
          vMax = std::max(vMax, v);
          vRMS += v * v;
        }
        vRMS = std::sqrt(vRMS / float(sampleEnd - sampleStart));
        const int yMin = int((0.5f * (1.f + vMin)) * float(h-1) + 0.5f);
        const int yMax = int((0.5f * (1.f + vMax)) * float(h-1) + 0.5f);
        const int yRMS_A = int((0.5f * (1.f - vRMS)) * float(h-1) + 0.5f);
        const int yRMS_B = int((0.5f * (1.f + vRMS)) * float(h-1) + 0.5f);
        for (int iy = 0; iy < h; ++iy)
        {
          uint32_t &px = pxData[iy * w + ix];
          if (iy >= yRMS_A && iy <= yRMS_B)   px = colorMain;
          else if (iy >= yMin && iy <= yMax)  px = colorFade;
          else                                px = colorBack;
        }
        pxData[y0 * w + ix] = colorLine;
      }
    }

    wTexture.update(wTextureUpload, false);
  }

public:
  widgetPlayCursor(c_soundSampler &sndSampler) : widget(), wsndSampler(sndSampler)
  {
    wTextureUpload = SDL_CreateRGBSurfaceWithFormat(0, 1024, 48, 32, SDL_PIXELFORMAT_ABGR8888); // matches colorToRGBA (uploaded as GL_RGBA)
    wTexture.load(wTextureUpload, 0, false);
  }
  virtual ~widgetPlayCursor()
  {
    SDL_FreeSurface(wTextureUpload);
    wTexture.clear();
  }

  virtual s_drawElementCount get_drawElementCount() const
  {
    s_drawElementCount ret;
    ret.m_vcountSolid = 6 * 2;
    ret.m_vcountPict = 6 * 1;
    ret.m_vcountLine = 2 * 1;
    ret.m_textureSlot = wTextureSlot;
    return ret;
  }

  virtual glm::vec2 get_zoneSizeDefault(const s_drawData &dd) const
  {
    const float h = wheightModifier * dd.resolve_sizeH(get_parentWindow()->get_fontHeight());
    return glm::vec2(20.f * h, 3.f * h);
  }

  virtual void compute_data(s_drawData &dd)
  {
    const glm::vec4 &colorBack = get_parentWindow()->get_colortheme().m_colorSurface;
    const glm::vec4 &colorFront = get_parentWindow()->get_colortheme().m_colorPrimary;
    const glm::vec4 &colorFrontH = get_parentWindow()->get_colortheme().m_colorOnObject;

    tre::ui::fillRect(dd.m_bufferSolid, m_zone, colorBack);

    const float cursorPos = m_zone.x + (m_zone.z - m_zone.x) * wcursorCurr;
    // TODO: show the loop region (wcursorA, wcursorB)

    tre::ui::fillRect(dd.m_bufferSolid, glm::vec4(cursorPos - 5.f * dd.m_pixelSize.x, m_zone.y, cursorPos + 5.f * dd.m_pixelSize.x, m_zone.w), colorFront);

    tre::ui::fillRect(dd.m_bufferPict, m_zone, glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    // TODO ...

    tre::ui::fillLine(dd.m_bufferLine, glm::vec2(cursorPos, m_zone.y), glm::vec2(cursorPos, m_zone.w), colorFrontH);

  }

  virtual void animate(float)
  {
    wcursorCurr = wsndSampler.m_playCursor;
    if (wDirty)
    {
      updateWaveForm();
      wDirty = false;
    }
    setUpdateNeededData();
  }

  virtual bool accept_event(tre::ui::s_eventIntern &event)
  {
    acceptEventBase_focus(event);

    if (!wishighlighted) return false;

    // TODO: seek and loop-selection with the mouse (wselectMode)

    return false;
  }
};

// -----------------------------------------------------------------------------

class widgetSpectrum : public tre::ui::widget
{
public:
  float wcursorCurr = 0.f;
  int   wvisuMode = 0; // TODO

  SDL_Surface *wTextureUpload = nullptr;
  tre::texture wTexture;
  unsigned     wTextureSlot = unsigned(-1); ///< slot returned by baseUI::addTexture

  c_soundSampler &wsndSampler;

  bool wDirty = false;
  void set_dirty() { wDirty = true; }

  /// Spectrogram of the whole input: time on x (same scale as the waveform), log-frequency on y (high frequencies on top).
  /// Each pixel is the max power of the FFT bins in its frequency band, in dB relative to the max of the whole input.
  static void computeSpectrogram(const std::vector<float> &input, int fs, uint32_t * __restrict pxData, int w, int h, const glm::vec4 &color)
  {
    static constexpr std::size_t kN = 2048;          ///< FFT size (21.5 Hz per bin at 44.1 kHz)
    static constexpr float       kFreqMin = 40.f;    ///< [Hz], bottom row
    static constexpr float       kFreqMaxCap = 16000.f; ///< [Hz], top row (capped by the Nyquist frequency)
    static constexpr float       kRangeDB = 60.f;    ///< dynamic range shown

    std::fill_n(pxData, w * h, 0u);
    if (input.empty() || fs <= 0 || w <= 0 || h <= 0) return;

    // window
    std::vector<float> window(kN);
    for (std::size_t n = 0; n < kN; ++n) window[n] = 0.5f - 0.5f * std::cos(2.f * pi * float(n) / float(kN));

    // rows -> range of FFT bins (log-frequency scale)
    const float      freqMax = std::min(0.5f * float(fs), kFreqMaxCap);
    const float      binPerHz = float(kN) / float(fs);
    std::vector<int> rowBinLo(h), rowBinHi(h);
    for (int iy = 0; iy < h; ++iy)
    {
      const int   ib = h - 1 - iy; // band index, from the bottom
      const float fLo = kFreqMin * std::pow(freqMax / kFreqMin, float(ib    ) / float(h));
      const float fHi = kFreqMin * std::pow(freqMax / kFreqMin, float(ib + 1) / float(h));
      const int   bLo = glm::clamp(int(std::floor(fLo * binPerHz + 0.5f)), 1, int(kN / 2));
      const int   bHi = glm::clamp(int(std::floor(fHi * binPerHz + 0.5f)), bLo + 1, int(kN / 2) + 1); // at least 1 bin (exclusive bound)
      rowBinLo[iy] = bLo;
      rowBinHi[iy] = bHi;
    }

    // power per pixel
    std::vector<float>     level(std::size_t(w) * std::size_t(h));
    std::vector<glm::vec2> spec(kN);
    const double           samplesPerColumn = double(input.size()) / double(w);
    float                  levelMax = 0.f;

    for (int ix = 0; ix < w; ++ix)
    {
      const long start = long((double(ix) + 0.5) * samplesPerColumn) - long(kN / 2); // window centered on the column
      for (std::size_t n = 0; n < kN; ++n)
      {
        const long  i = start + long(n);
        const float x = (i >= 0 && std::size_t(i) < input.size()) ? input[std::size_t(i)] : 0.f;
        spec[n] = glm::vec2(x * window[n], 0.f);
      }
      tre::fft(spec.data(), kN, false);

      for (int iy = 0; iy < h; ++iy)
      {
        float p = 0.f;
        for (int k = rowBinLo[iy]; k < rowBinHi[iy]; ++k) p = std::max(p, glm::dot(spec[k], spec[k]));
        level[std::size_t(iy) * std::size_t(w) + std::size_t(ix)] = p;
        levelMax = std::max(levelMax, p);
      }
    }
    if (levelMax <= 0.f) return; // silence

    // power -> color (alpha = level in dB, over kRangeDB)
    std::array<uint32_t, 256> lut;
    for (std::size_t i = 0; i < lut.size(); ++i) lut[i] = colorToRGBA(color, float(i) / float(lut.size() - 1));

    const float invMax = 1.f / levelMax;
    for (std::size_t i = 0; i < level.size(); ++i)
    {
      const float db = 10.f * std::log10(std::max(level[i] * invMax, 1.e-12f));
      const float t = glm::clamp(1.f + db / kRangeDB, 0.f, 1.f);
      pxData[i] = lut[std::size_t(t * float(lut.size() - 1) + 0.5f)];
    }
  }

  void updateSpectrum()
  {
    uint32_t * __restrict pxData = reinterpret_cast<uint32_t*>(wTextureUpload->pixels);
    const int w = wTextureUpload->w; // only first mip-map level (maybe add in futur)
    const int h = wTextureUpload->h;

    const c_soundSampler::t_audioPtr input = wsndSampler.input();
    if (input) computeSpectrogram(*input, wsndSampler.inputFreq(), pxData, w, h, get_parentWindow()->get_colortheme().m_colorOnSurface);
    else       std::fill_n(pxData, w * h, 0u);

    wTexture.update(wTextureUpload, false);
  }

public:
  widgetSpectrum(c_soundSampler &sndSampler) : widget(), wsndSampler(sndSampler)
  {
    wTextureUpload = SDL_CreateRGBSurfaceWithFormat(0, 1024, 128, 32, SDL_PIXELFORMAT_ABGR8888);
    wTexture.load(wTextureUpload, 0, false);
  }
  virtual ~widgetSpectrum()
  {
    SDL_FreeSurface(wTextureUpload);
    wTexture.clear();
  }

  virtual s_drawElementCount get_drawElementCount() const
  {
    s_drawElementCount ret;
    ret.m_vcountLine = 2 * 1;
    ret.m_vcountPict = 6 * 1;
    ret.m_textureSlot = wTextureSlot;
    return ret;
  }

  virtual glm::vec2 get_zoneSizeDefault(const s_drawData &dd) const
  {
    const float h = wheightModifier * dd.resolve_sizeH(get_parentWindow()->get_fontHeight());
    return glm::vec2(20.f * h, 3.f * h);
  }

  virtual void compute_data(s_drawData &dd)
  {
    const glm::vec4 &colorFrontH = get_parentWindow()->get_colortheme().m_colorOnObject;

    tre::ui::fillRect(dd.m_bufferPict, m_zone, glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    const float cursorPos = m_zone.x + (m_zone.z - m_zone.x) * wcursorCurr;

    tre::ui::fillLine(dd.m_bufferLine, glm::vec2(cursorPos, m_zone.y), glm::vec2(cursorPos, m_zone.w), colorFrontH);
  }

  virtual void animate(float)
  {
    wcursorCurr = wsndSampler.m_playCursor;
    if (wDirty)
    {
      updateSpectrum();
      wDirty = false;
    }
    setUpdateNeededData();
  }
};
// MAIN =======================================================================

tre::windowContext winContext;

tre::audioContext        audioCtx;
std::vector<std::string> audioDevices;
std::size_t              audioDevicePicked = std::size_t(-1); // default

c_soundTicker            soundTicker;
c_soundSampler           soundSampler;
widgetPlayCursor         *uiPlayCursor = nullptr;
widgetSpectrum           *uiSpectrum = nullptr;

glm::vec4 colorBack  = glm::vec4(1.0f, 1.0f, 1.0f, 1.f);
glm::vec4 colorFront = glm::vec4(0.0f, 0.0f, 0.0f, 1.f);

tre::baseUI2D uiMain;
tre::font     uiFont;

tre::ui::window *uiwTopBar = uiMain.create_window();
tre::ui::window *uiwTicker = uiMain.create_window();
tre::ui::window *uiwPlayer = uiMain.create_window();
tre::ui::window *uiwOption = uiMain.create_window();

int      uiTopBarSelectedID = 0;

enum e_colorTheme { ColorTheme_WhiteBlue };
e_colorTheme uiColorTheme = ColorTheme_WhiteBlue;
static const std::vector<const char *> kuiColorThemeNames = { "Light blue" };

// -----------------------------------------------------------------------------

static void onColorThemeChanged();

static bool myInit()
{
  if (!winContext.SDLInit(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) ||
      !winContext.SDLCreateWindow(700, 350, "Tick Companion") ||
      !winContext.OpenGLInit())
  {
    TRE_LOG("error on SDL/OpenGL Init");
    return false;
  }

  {
    audioCtx.getDevicesName(audioDevices);

    audioCtx.startSystem(nullptr, 10u);

    audioCtx.addSound(&soundTicker);
    audioCtx.addSound(&soundSampler);
  }

  {
    //SDL_GetPrefPath("", "");
  }

  uiMain.loadShader();

  {
    std::vector<tre::font::s_fontCache> uiCache(1);
    uiCache[0] = tre::font::loadFromTTF(TESTIMPORTPATH "resources/DejaVuSans.ttf", 32);
    if (uiCache[0].m_surface == nullptr) uiCache[0] = tre::font::loadProceduralLed(2, 0);
    if (!uiFont.load(uiCache, true)) return false;
    uiMain.set_defaultFont(&uiFont);
  }

  {
    uiwTopBar->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
    uiwTopBar->set_layoutGrid(1, 4);

    tre::ui::widget *widT = uiwTopBar->create_widgetText(0, 0)->set_text("Ticker")->set_isactive(true);
    widT->wcb_clicked_left = [](tre::ui::widget *) { uiTopBarSelectedID = 0; };
    widT->wcb_animate = [](tre::ui::widget* self, float) { self->set_color(glm::vec4(0.5f, 0.f, 1.f * (uiTopBarSelectedID == 0), 1.f)); };

    tre::ui::widget *widP = uiwTopBar->create_widgetText(0, 1)->set_text("Player")->set_isactive(true);
    widP->wcb_clicked_left = [](tre::ui::widget *) { uiTopBarSelectedID = 1; };
    widP->wcb_animate = [](tre::ui::widget* self, float) { self->set_color(glm::vec4(0.5f, 0.f, 1.f * (uiTopBarSelectedID == 1), 1.f)); };

    tre::ui::widget *widO = uiwTopBar->create_widgetText(0, 2)->set_text("Option")->set_isactive(true);
    widO->wcb_clicked_left = [](tre::ui::widget *) { uiTopBarSelectedID = 2; };
    widO->wcb_animate = [](tre::ui::widget* self, float) { self->set_color(glm::vec4(0.5f, 0.f, 1.f * (uiTopBarSelectedID == 2), 1.f)); };

  #ifdef TRE_PROFILE
    tre::ui::widget *widI = uiwTopBar->create_widgetText(0, 3); // tmp here
    widI->set_heightModifier(0.5f);
    widI->wcb_animate = [](tre::ui::widget* self, float)
    {
      char buf[64];
      std::snprintf(buf, 64, "%.1f ms (load %.1f%%)", audioCtx.getPerf_total() * 1.e3f, audioCtx.getPerf_load() * 100.f);
      static_cast<tre::ui::widgetText*>(self)->set_text(buf);
    };
  #endif
  }

  {
    uiwTicker->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
    uiwTicker->set_layoutGrid(6, 2);
    uiwTicker->set_colAlignment(0, tre::ui::ALIGN_MASK_CENTERED);
    uiwTicker->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    uiwTicker->create_widgetText(0, 0)->set_text("Play");
    uiwTicker->create_widgetBoxCheck(0, 1)->set_value(soundTicker.m_play)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget* self)
    {
      soundTicker.m_play = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value();
    };

    uiwTicker->create_widgetText(1, 0)->set_text("Volume");
    tre::ui::widgetSlider *wV = uiwTicker->create_widgetSlider(1, 1)->set_value(soundTicker.m_volume)->set_valuemin(0.f)->set_valuemax(1.f)->set_widthFactor(10.f)->set_fillLeft(true);
    wV->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundTicker.m_volume = static_cast<tre::ui::widgetBar*>(self)->get_value();
    };

    tre::ui::widgetBar *wT = uiwTicker->create_widgetBar(2, 0, 1, 2)->set_value(soundTicker.m_tempo * 1.f)->set_valuemin(float(c_soundTicker::kTempoMin))->set_valuemax(float(c_soundTicker::kTempoMax))->set_snapInterval(1.f)->set_widthFactor(10.f)->set_withtext(true);
    wT->set_heightModifier(2.f)->set_iseditable(true)->set_isactive(true);
    wT->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundTicker.m_tempo = int(static_cast<tre::ui::widgetBar*>(self)->get_value() + 0.5f);
    };
    wT->wcb_animate = [](tre::ui::widget* self, float)
    {
      static_cast<tre::ui::widgetBar*>(self)->set_value(soundTicker.m_tempo * 1.f);
    };
    wT->wcb_valuePrinter = [](const float v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%d bpm", int(v));
      return std::string(txt);
    };

    uiwTicker->create_widgetText(3, 0)->set_text("Silent cycle");
    tre::ui::widgetSliderInt *wS = uiwTicker->create_widgetSliderInt(3, 1)->set_value(soundTicker.m_tempoSilenceCount)->set_valuemin(0)->set_valuemax(16)->set_withtext(true)->set_widthFactor(10.f);
    wS->set_iseditable(true)->set_isactive(true);
    wS->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundTicker.m_tempoSilenceCount = unsigned(static_cast<tre::ui::widgetSliderInt*>(self)->get_value());
    };
    wS->wcb_valuePrinter = [](const int v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%d", v);
      return std::string(txt);
    };

    uiwTicker->create_widgetText(4, 0)->set_text("Increment value");
    tre::ui::widgetSliderInt *wI = uiwTicker->create_widgetSliderInt(4, 1)->set_value(soundTicker.m_tempoDeltaIncr)->set_valuemin(-10)->set_valuemax(10)->set_withtext(true)->set_widthFactor(10.f);
    wI->set_iseditable(true)->set_isactive(true);
    wI->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundTicker.m_tempoDeltaIncr = static_cast<tre::ui::widgetSliderInt*>(self)->get_value();
    };
    wI->wcb_valuePrinter = [](const int v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%+d bpm", v);
      return std::string(txt);
    };

    uiwTicker->create_widgetText(5, 0)->set_text("Increment delay");
    tre::ui::widgetSliderInt *wD = uiwTicker->create_widgetSliderInt(5, 1)->set_value(soundTicker.m_tempoDeltaTime / 4)->set_valuemin(0)->set_valuemax(16)->set_withtext(true)->set_widthFactor(10.f);
    wD->set_iseditable(true)->set_isactive(true);
    wD->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundTicker.m_tempoDeltaTime = 4 * unsigned(static_cast<tre::ui::widgetSliderInt*>(self)->get_value());
    };
    wD->wcb_valuePrinter = [](const int v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%d", 4 * v);
      return std::string(txt);
    };

  }

  {
    uiwPlayer->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
    uiwPlayer->set_layoutGrid(7, 2);
    uiwPlayer->set_colAlignment(0, tre::ui::ALIGN_MASK_CENTERED);
    uiwPlayer->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    widgetPlayCursor *wPlayCursor = new widgetPlayCursor(soundSampler);
    wPlayCursor->set_isactive(true);
    uiPlayCursor = wPlayCursor;

    widgetSpectrum *wSpectrum = new widgetSpectrum(soundSampler);
    uiSpectrum = wSpectrum;

    uiwPlayer->create_widgetText(0, 0)->set_text("Open File ...")->set_isactive(true)
      ->wcb_clicked_left = [](tre::ui::widget*) { fileOpener::browseRequest(); };

    uiwPlayer->create_widgetText(1, 0)->set_text("Volume");
    tre::ui::widgetSlider *wV = uiwPlayer->create_widgetSlider(1, 1)->set_value(soundSampler.m_volume)->set_valuemin(0.f)->set_valuemax(1.f)->set_widthFactor(10.f)->set_fillLeft(true);
    wV->set_iseditable(true)->set_isactive(true);
    wV->wcb_modified_ongoing = [](tre::ui::widget* self)
    {
      soundSampler.m_volume = static_cast<tre::ui::widgetBar*>(self)->get_value();
    };

    uiwPlayer->create_widgetText(2, 0)->set_text("Time stretch");
    tre::ui::widgetSliderInt *wStretch = uiwPlayer->create_widgetSliderInt(2, 1)->set_value(0)->set_valuemin(-2)->set_valuemax(10)->set_withtext(true)->set_widthFactor(10.f);
    wStretch->set_iseditable(true)->set_isactive(true);
    wStretch->wcb_modified_finished = [](tre::ui::widget* self)
    {
      const float stretchFactor = 1.f + 0.1f * static_cast<tre::ui::widgetSliderInt*>(self)->get_value();
      soundSampler.setStretchFactor(stretchFactor);
    };
    wStretch->wcb_valuePrinter = [](const int v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%d%%", v * 10);
      return std::string(txt);
    };

    uiwPlayer->create_widgetText(3, 0)->set_text("Tune pitch");
    tre::ui::widgetSlider *wPitch = uiwPlayer->create_widgetSlider(3, 1)->set_value(0.f)->set_valuemin(-0.2f)->set_valuemax(0.2f)->set_widthFactor(10.f);
    wPitch->set_iseditable(true)->set_isactive(true);
    wPitch->wcb_modified_finished = [](tre::ui::widget* self)
    {
      const float pitchFactor = 1.f + float(int(static_cast<tre::ui::widgetSlider*>(self)->get_value() * 100)) / 100.f;
      soundSampler.setPitchFactor(pitchFactor);
    };
    wPitch->wcb_valuePrinter = [](const float v)
    {
      char txt[16];
      std::snprintf(txt, 15, "%+d%%", int(v * 100.f));
      return std::string(txt);
    };

    uiwPlayer->create_widgetText(4, 0)->set_text("Zoom out")->set_isactive(true);
    uiwPlayer->create_widgetText(4, 1)->set_text("Zoom in")->set_isactive(true);

    uiwPlayer->set_widget(wPlayCursor, 5, 0, 1, 2);
    uiwPlayer->set_widget(wSpectrum, 6, 0, 1, 2);

    wPlayCursor->wTextureSlot = unsigned(uiMain.addTexture(&wPlayCursor->wTexture)); // the textures are loaded in the widget constructors
    wSpectrum->wTextureSlot = unsigned(uiMain.addTexture(&wSpectrum->wTexture));
  }

  {
    uiwOption->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
    uiwOption->set_layoutGrid(6, 2);
    uiwOption->set_colAlignment(0, tre::ui::ALIGN_MASK_CENTERED);
    uiwOption->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    unsigned row = -1;

    uiwOption->create_widgetText(++row, 0)->set_text("Stretcher: phase locking");
    uiwOption->create_widgetBoxCheck(row, 1)->set_value(soundSampler.m_phaseLocking)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self)
    {
      soundSampler.m_phaseLocking = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value();
    };

    uiwOption->create_widgetText(++row, 0)->set_text("Ticker's keys");
    uiwOption->create_widgetText(row, 1)->set_text("Play/Pause: Space\n"
                                                   "Tempo Down/Up: Left/Right Arrow\n"
                                                  );

    uiwOption->create_widgetText(++row, 0)->set_text("Player's keys");
    uiwOption->create_widgetText(row, 1)->set_text("Play/Pause: Space\n"
                                                   "Back (-5s): Left Arrow\n"
                                                   "Forward (+5s): Right Arrow\n"
                                                  );

    uiwOption->create_widgetText(++row, 0)->set_text("Color theme");
    uiwOption->create_widgetLineChoice(row, 1)->set_values(kuiColorThemeNames)->set_selectedIndex(uiColorTheme)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget* self)
    {
      uiColorTheme = e_colorTheme(static_cast<tre::ui::widgetLineChoice*>(self)->get_selectedIndex());
      onColorThemeChanged();
    };

  }

  uiMain.loadIntoGPU();

  TRE_LOG("Start");

  return true;
}

// -----------------------------------------------------------------------------

static void onColorThemeChanged()
{
  tre::ui::s_colorTheme uiColorTheme;

  uiColorTheme.m_colorSurface = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
  uiColorTheme.m_colorPrimary = glm::vec4(0.7f, 0.7f, 1.f, 1.f);
  uiColorTheme.m_colorOnSurface = glm::vec4(0.1f, 0.1f, 0.1f, 1.f);
  uiColorTheme.m_colorOnObject = glm::vec4(0.1f, 0.1f, 0.4f, 1.f);

  uiwTopBar->set_colortheme(uiColorTheme);

  //uiColorTheme.m_colorSurface = glm::vec4(0.9f, 0.9f, 0.9f, 1.f);
  //uiColorTheme.m_colorPrimary = glm::vec4(0.7f, 0.7f, 1.f, 1.f);
  //uiColorTheme.m_colorOnSurface = glm::vec4(0.1f, 0.1f, 0.1f, 1.f);
  //uiColorTheme.m_colorOnObject = glm::vec4(0.1f, 0.1f, 0.4f, 1.f);

  uiwTicker->set_colortheme(uiColorTheme);
  uiwPlayer->set_colortheme(uiColorTheme);
  uiwOption->set_colortheme(uiColorTheme);
}

// -----------------------------------------------------------------------------

static void onResize()
{
  // camera info
  uiMain.updateCameraInfo(winContext.m_matProjection2D, winContext.m_resolutioncurrent);

  // font-size
  const float fontSizePX = std::ceil(std::min((1.f/30.f) * winContext.m_resolutioncurrent.x, (1.f/16.f) * winContext.m_resolutioncurrent.y));
  const tre::ui::s_size fs = tre::ui::s_size(fontSizePX, tre::ui::SIZE_PIXEL);
  const tre::ui::s_size mg = tre::ui::s_size(std::ceil(fontSizePX / 5.f), tre::ui::SIZE_PIXEL);
  uiwTopBar->set_fontSize(fs);
  uiwPlayer->set_fontSize(fs);
  uiwPlayer->set_cellMargin(mg);
  uiwTicker->set_fontSize(fs);
  uiwTicker->set_cellMargin(mg);
  uiwOption->set_fontSize(fs);
  uiwOption->set_cellMargin(mg);

  // placement

  const float ratio = float(winContext.m_resolutioncurrent.x)/float(winContext.m_resolutioncurrent.y);

  glm::mat3 m3(1.f);

  uiwTopBar->set_colWidth(0, 2.f * ratio / 4.f);
  uiwTopBar->set_colWidth(1, 2.f * ratio / 4.f);
  uiwTopBar->set_colWidth(2, 2.f * ratio / 4.f);
  uiwTopBar->set_colWidth(3, 2.f * ratio / 4.f);

  m3[2].x = -ratio;
  m3[2].y = 1.f;
  uiwTopBar->set_mat3(m3);

  uiwTicker->set_colWidth(0, 2.f * ratio / 2.f);
  uiwTicker->set_colWidth(1, 2.f * ratio / 2.f);

  m3[2].x = -ratio;
  m3[2].y = 1.f - (uiwTopBar->get_zone().w - uiwTopBar->get_zone().y);
  uiwTicker->set_mat3(m3);

  uiwPlayer->set_colWidth(0, 2.f * ratio / 2.f);
  uiwPlayer->set_colWidth(1, 2.f * ratio / 2.f);

  m3[2].x = -ratio;
  m3[2].y = 1.f - (uiwTopBar->get_zone().w - uiwTopBar->get_zone().y);
  uiwPlayer->set_mat3(m3);

  uiwOption->set_colWidth(0, 2.f * ratio / 2.f);
  uiwOption->set_colWidth(1, 2.f * ratio / 2.f);

  m3[2].x = -ratio;
  m3[2].y = 1.f - (uiwTopBar->get_zone().w - uiwTopBar->get_zone().y);
  uiwOption->set_mat3(m3);

}

// -----------------------------------------------------------------------------

static void myUpdate()
{
  SDL_Event event;

  // Events
  while(SDL_PollEvent(&event) == 1)
  {
    winContext.SDLEvent_onWindow(event);
    if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) winContext.m_quit = true;
    if (uiTopBarSelectedID == 0)
    {
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE) soundTicker.m_play = !soundTicker.m_play;
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT) soundTicker.m_tempo = soundTicker.m_tempo - 4; // TODO properly
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT) soundTicker.m_tempo = soundTicker.m_tempo + 4; // TODO properly
    }
    else if (uiTopBarSelectedID == 1)
    {
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_SPACE) soundSampler.m_play = !soundSampler.m_play;
      if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_RIGHT) && !soundSampler.empty())
      {
        const float delta = 5.f * float(soundSampler.inputFreq()) / float(soundSampler.input()->size()); // 5 seconds
        const float sign = (event.key.keysym.sym == SDLK_LEFT) ? -1.f : 1.f;
        soundSampler.m_playCursorSet = glm::clamp(soundSampler.m_playCursor + sign * delta, 0.f, 1.f);
      }
    }

    uiMain.acceptEvent(event);
  }

  if (winContext.m_viewportResized) onResize();

  // Update audio

  uiMain.animate(0.005f);

  // -> load the picked file
  {
    std::vector<uint8_t> fileBuffer;
    if (fileOpener::pollRequest(fileBuffer))
    {
      std::vector<float> audio;
      int                freq = 0;
      if (fileOpener::decodeAudio(fileBuffer, audio, freq))
      {
        TRE_LOG("Audio file loaded: " << audio.size() << " samples at " << freq << " Hz");
        soundSampler.setInput(std::move(audio), freq);
        soundSampler.m_playCursorStart = 0.f;
        soundSampler.m_playCursorEnd = 1.f;
        soundSampler.m_playCursorSet = 0.f;
        soundSampler.m_play = true;
        if (uiPlayCursor != nullptr)
        {
          uiPlayCursor->wcursorA = 0.f;
          uiPlayCursor->wcursorB = 1.f;
          uiPlayCursor->set_dirty();
        }
        if (uiSpectrum != nullptr)
        {
          uiSpectrum->set_dirty();
        }
      }
      else
      {
        TRE_LOG("Failed to decode the audio file (WAV or MP3 expected)");
      }
    }
  }

  if (soundSampler.empty()) soundSampler.m_play = false;

  audioCtx.updateSystem();

  // Draw and Present

  glViewport(0, 0, winContext.m_resolutioncurrent.x, winContext.m_resolutioncurrent.y);
  glClearColor(colorBack.r, colorBack.g, colorBack.b, 1.f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glEnable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);

  uiwTicker->set_isvisible(uiTopBarSelectedID == 0);
  uiwPlayer->set_isvisible(uiTopBarSelectedID == 1);
  uiwOption->set_isvisible(uiTopBarSelectedID == 2);
  uiMain.updateIntoGPU();
  uiMain.draw();

  SDL_GL_SwapWindow(winContext.m_window);

  SDL_Delay(5);
}

// -----------------------------------------------------------------------------

static void myQuit()
{
  TRE_LOG("Quit");

  uiMain.clear();
  uiMain.clearGPU();
  uiMain.clearShader();

  uiFont.clear();

  audioCtx.stopSystem();

  winContext.OpenGLQuit();
  winContext.SDLQuit();
}

// -----------------------------------------------------------------------------

int main(int argc, char **argv)
{
  if (!myInit()) return -1;

  onColorThemeChanged();
  onResize();

  if (argc > 1) fileOpener::loadPath(argv[1]); // optional: audio file to open

#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(myUpdate, 0, true); // never returns
#else
  while (!winContext.m_quit)
  {
    myUpdate();
  }

  myQuit();
#endif

  return 0;
}
