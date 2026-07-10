
#include <vector>
#include <array>

#include "tre_utils.h"
#include "tre_ui.h"
#include "tre_font.h"
#include "tre_windowContext.h"
#include "tre_audio.h"

static constexpr float pi = 3.14159265358979323846f;

// Audo opening ===============================================================

//#define HAS_AUDIO_OPENER

#ifdef HAS_AUDIO_OPENER

#include <fstream>

#include <Windows.h>

#include <shobjidl.h> // IFileOpenDialog

#include <mmreg.h>
#include <msacm.h>
#pragma comment(lib, "msacm32.lib")
#undef min
#undef max

/// Blocking dialog-box. If not null, the returned buffer should be released by the caller.
static char* getFilePath()
{
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  if (!SUCCEEDED(hr)) return nullptr;
  char *ret = nullptr;

  IFileOpenDialog *pFileOpen = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen)); // Create the FileOpenDialog object.
  if (SUCCEEDED(hr) && (pFileOpen != nullptr))
  {
    hr = pFileOpen->Show(NULL); // Show the Open dialog box.

    if (SUCCEEDED(hr))
    {
      IShellItem *pItem = nullptr;
      hr = pFileOpen->GetResult(&pItem);
      if (SUCCEEDED(hr))
      {
        PWSTR filePathW = nullptr;
        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &filePathW);
        ret = (char *)malloc(256);
        if (SUCCEEDED(hr) && (filePathW != nullptr) && (ret != nullptr))
        {
          std::wcstombs(ret, filePathW, 256);
          ret[255] = 0;
          CoTaskMemFree(filePathW);
        }
        pItem->Release();
      }
    }
    pFileOpen->Release();
  }
  CoUninitialize();
  return ret;
}

/// Load an audio file
static bool loadAudioFile(const char* fpath, std::vector<float> &soundData, int targetFreq)
{
  // open file
  std::ifstream fstream(fpath, std::ios::binary);
  if (!fstream.is_open()) return false;

  // MPEG Audio
  {
    fstream.seekg(0, fstream.beg);

    // header: id3v2 tags (optionnal)
    char headerTagID3[10];
    fstream.read(headerTagID3, 10);
    const bool validID = (headerTagID3[0] == 'I' && headerTagID3[1] == 'D' && headerTagID3[2] == '3');
    const bool validVersion = (headerTagID3[3] != 0xFF) && (headerTagID3[4] != 0xFF);
    const bool hasExtentedHeader = (headerTagID3[5] & 0x40) != 0;
    //const bool hasFooter = (headerTagID3[5] & 0x10) != 0;
    const bool validFlags = (headerTagID3[5] & 0x0F) == 0;
    const uint32_t headerAddSize = (uint32_t(headerTagID3[6]) << 21) | (uint32_t(headerTagID3[7]) << 14) | (uint32_t(headerTagID3[8]) << 7) | (uint32_t(headerTagID3[9]));
    if (validID && validVersion && validFlags) fstream.seekg(headerAddSize, fstream.cur);

    // header: MPEG Audio
    uint8_t headerMPEG[4];
    fstream.read(reinterpret_cast<char*>(headerMPEG), 4);
    const bool     validSync = (headerMPEG[0] == 0xFF) && ((headerMPEG[1] & 0xE0) == 0xE0);
    const uint32_t versionId = (headerMPEG[1] >> 3) & 0x3; // 0: version 2.5, 1:reserved, 2: MPEG-V2, 3:MPEG-V1
    const uint32_t layerId = (headerMPEG[1] >> 1) & 0x3; // 0: reserved, 1: layer3, 2:layer2, 3:layer1
    const uint32_t freqId = (headerMPEG[2] >> 2) & 0x3;
    static uint32_t freqTable[4][4] = { { 11025, 12000, 8000, 0 }, { 0, 0, 0, 0 }, { 22050, 24000, 16000, 0 }, { 44100, 48000, 32000, 0 } };
    const uint32_t freq = freqTable[versionId][freqId];
    const uint32_t channelsId = (headerMPEG[3] >> 6) & 0x3;
    const uint32_t channels = (channelsId == 3) ? 1 : 2;

    if (validSync && (versionId != 1) && (layerId != 0) && (freq != 0))
    {
      fstream.seekg(-4, fstream.cur);

      // Using Win-API

      const DWORD MP3_FRAME_SIZE = 1024; // = 144 * bitRate(320*1000) / freq(44100) + padding (layer3-formula)
      const DWORD PCM_FRAME_SIZE = 1152 * 2 * 16 / 8; // = Nsamples(1152 with layer3) * BitsPerSamples(16) / Bytes
      WAVEFORMATEX         pcmFormat;
      {
        pcmFormat.wFormatTag = WAVE_FORMAT_PCM;
        pcmFormat.nChannels = channels; // stereo
        pcmFormat.nSamplesPerSec = freq; // 44.1kHz
        pcmFormat.wBitsPerSample = 16; // 16 bits
        pcmFormat.nBlockAlign = 2 * channels;
        pcmFormat.nAvgBytesPerSec = 2 * channels * freq;
        pcmFormat.cbSize = 0; // no more data to follow
      }
      MPEGLAYER3WAVEFORMAT mp3Format;
      {
        mp3Format.wfx.cbSize = MPEGLAYER3_WFX_EXTRA_BYTES;
        mp3Format.wfx.wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        mp3Format.wfx.nChannels = channels;
        mp3Format.wfx.nAvgBytesPerSec = 128 * (1024 / 8); // not really used but must be one of 64, 96, 112, 128, 160kbps
        mp3Format.wfx.wBitsPerSample = 0; // MUST BE ZERO
        mp3Format.wfx.nBlockAlign = 1; // MUST BE ONE
        mp3Format.wfx.nSamplesPerSec = freq; // 44.1kHz
        mp3Format.fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
        mp3Format.nBlockSize = MP3_FRAME_SIZE;
        mp3Format.nFramesPerBlock = 1; // MUST BE ONE
        mp3Format.nCodecDelay = 1393;
        mp3Format.wID = MPEGLAYER3_ID_MPEG;
      }

      HACMSTREAM acmMp3stream = NULL;
      MMRESULT ret = acmStreamOpen(&acmMp3stream, NULL, (LPWAVEFORMATEX)&mp3Format, &pcmFormat, NULL, 0, 0, ACM_STREAMOPENF_NONREALTIME);
      if (ret != 0 || acmMp3stream == NULL)
      {
        fstream.close();
        MessageBoxW(NULL, L"Failed to open acm-stream.", L"MP3 Decoder", MB_OK | MB_ICONERROR);
        return false;
      }

      char bufferInput[MP3_FRAME_SIZE];
      char bufferOutput[PCM_FRAME_SIZE];

      // prepare the decoder
      ACMSTREAMHEADER mp3streamHead;
      ZeroMemory(& mp3streamHead, sizeof(ACMSTREAMHEADER) );
      mp3streamHead.cbStruct = sizeof(ACMSTREAMHEADER);
      mp3streamHead.pbSrc = (LPBYTE)bufferInput;
      mp3streamHead.cbSrcLength = MP3_FRAME_SIZE;
      mp3streamHead.pbDst = (LPBYTE)bufferOutput;
      mp3streamHead.cbDstLength = PCM_FRAME_SIZE;
      ret = acmStreamPrepareHeader(acmMp3stream, &mp3streamHead, 0);

      soundData.clear();

      DWORD inputBufferUsed = MP3_FRAME_SIZE;
      while (ret == 0 && !fstream.eof())
      {
        if (inputBufferUsed != 0)
        {
          if (inputBufferUsed < MP3_FRAME_SIZE)
          {
            for (char *ibufDst = bufferInput, *ibufSrc = bufferInput + inputBufferUsed, *ibufStop = bufferInput + MP3_FRAME_SIZE; ibufSrc < ibufStop; ++ibufDst, ++ibufSrc) *ibufDst = *ibufSrc;
          }
          fstream.read(bufferInput + (MP3_FRAME_SIZE - inputBufferUsed), inputBufferUsed);
        }

        ret = acmStreamConvert(acmMp3stream, &mp3streamHead, ACM_STREAMCONVERTF_BLOCKALIGN);

        inputBufferUsed = mp3streamHead.cbSrcLengthUsed;

        if (mp3streamHead.cbSrcLengthUsed == 0 && mp3streamHead.cbDstLengthUsed == 0) break;

        if (mp3streamHead.cbDstLengthUsed != 0)
        {
          const std::size_t rdAdd = std::size_t(mp3streamHead.cbDstLengthUsed) / 2 / channels;
          const std::size_t rdSize = soundData.size();
          soundData.resize(rdSize + rdAdd);
          const int16_t     *bufIn = reinterpret_cast<const int16_t*>(mp3streamHead.pbDst);
          if (channels == 2)
          {
            for (std::size_t is = 0; is < rdAdd; ++is)
            {
              const int16_t vL = *(bufIn++);
              const int16_t vR = *(bufIn++);
              const float   vf = (float(vL) + float(vR)) / float(0x7FFF * 2);
              soundData[rdSize + is] = vf;
            }
          }
          else
          {
            for (std::size_t is = 0; is < rdAdd; ++is)
            {
              const int16_t vM = *(bufIn++);
              soundData[rdSize + is] = float(vM) / float(0x7FFF);
            }
          }
        }
      }

      acmStreamUnprepareHeader(acmMp3stream, &mp3streamHead, 0);
      acmStreamClose(acmMp3stream, 0);

      fstream.close();
      return true;
    }
  }

  // Container RIFF (WAV):
  {
    fstream.seekg(0, fstream.beg);
    char header[4] = {0, 0, 0, 0};
    fstream.read(header, sizeof(header));


  }

  // Container OGG (opus):
  {
    fstream.seekg(0, fstream.beg);
    char header[4] = {0, 0, 0, 0};
    fstream.read(header, sizeof(header));


  }

  fstream.close();
  MessageBoxW(NULL, L"Failed to recognize the audio codec.", L"Audio Decoder", MB_OK | MB_ICONERROR);
  return false;
}

#endif // HAS_AUDIO_OPENER

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

protected:
  unsigned ac_cursor = 0;
  unsigned ac_tempo = 0; // current tempo
  unsigned ac_tickid = 0;

public:
  virtual void sync() override final
  {
    m_tempo = glm::clamp(m_tempo, kTempoMin, kTempoMax);
    // send
    ac_tempo = unsigned(m_tempo);
    // recieve
    m_tickid = ac_tickid;
  }

  bool isCurrentTickEnabled() const
  {
    if (m_tempoSilenceCount == 0) return true;
    return (ac_tickid % (2 * m_tempoSilenceCount)) < m_tempoSilenceCount;
  }

  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override final
  {
    if (sampleFreq == 0) return;
    if (ac_tempo == 0) return;
    if (!m_play) return;

    const float    twoPiOverFreq = 2.f * pi * 1.e3f /* 1 kHz */ / float(sampleFreq);
    unsigned       tickLen = unsigned(sampleFreq) * 60 / ac_tempo;
    bool           hasTick = isCurrentTickEnabled();
    const unsigned incrTime = m_tempoDeltaTime; // local copy
    const int      incrValue = m_tempoDeltaIncr; // local copy
    const float    volume = m_volume; // local copy

    for (unsigned i = 0; i < sampleCount; ++i, ++ac_cursor)
    {
      if (ac_cursor >= tickLen)
      {
        ac_cursor = 0;
        ++ac_tickid;
        hasTick = isCurrentTickEnabled();
        if (incrTime != 0 && ac_tickid % incrTime == 0)
        {
          ac_tempo = m_tempo = glm::clamp(m_tempo + incrValue, kTempoMin, kTempoMax); // can have data-race here ...
          tickLen = unsigned(sampleFreq) * 60 / ac_tempo;
        }
      }
      if (hasTick) hasTick = (ac_cursor < 100);
      if (hasTick)
      {
        const float v = volume * std::sin(float(ac_cursor) * twoPiOverFreq) * (1.f - (0.2f * 0.01f) * float(ac_cursor));
        outBufferAdd[2*i+0] += v;
        outBufferAdd[2*i+1] += v;
      }
    }
  }
};

// -----------------------------------------------------------------------------

constexpr std::size_t kSamplesCountPerChunk = 12288;

class c_soundSampler : public tre::soundInterface
{
public:
  bool     m_play = false;
  float    m_volume = 0.5f;
  float    m_playCursorStart = 0; ///< [0-1]
  float    m_playCursorEnd   = 0; ///< [0-1]
  float    m_playCursor      = 0; ///< [0-1] (read-back)
  float    m_playCursorSet   = -1.f; ///< [0-1] (set when positive)

  /// Input audio (mono)
  /// The frequency is assumed to be the same as the audio-cb.
  std::vector<float> m_audioInput;
  int                m_frequencyInput = 0; // TODO

  float m_stretcherFactor = 1.f; ///< stretch factor
  float m_pitchFactor = 1.f; ///< pitch factor (1 = no change, 2 = octave up, 0.5 = octave down, etc.)

  /// Processed audio to be played (mono, stretched).
  /// The complete audio should be allocated without relocation (it is used in the audio-cb)
  /// The processed audio is computed outside.
  /// The frequency is assumed to be the same as the audio-cb.
  tre::chunkVector<float, 65536>  m_audioPlayBlockData; // raw buffer containing the elementary blocks (128kB of continguous memory)
  tre::chunkVector<uint8_t, 4096> m_audioPlayBlockFlag;

  std::size_t m_audioPlaySamplesDataPerBlock = 1024; ///< nbr of samples in the block to generate play-audio (must be a power of two)
  std::size_t m_audioPlaySamplesRealPerBlock =  900; ///< actually play-audio length

  int      freq = 0; ///< sampling frequency [Hz]
protected:
  bool     ac_play = false;
  float    ac_volume = 0.f;
  unsigned ac_playCursorStart = 0; ///< [sampleIndex], in played audio
  unsigned ac_playCursorEnd   = 0; ///< [sampleIndex], in played audio
  unsigned ac_playCursor      = 0; ///< [sampleIndex], in played audio
  std::size_t ac_audioPlayDataSizeLast = 0;

public:

  void resetGeneratedAudio()
  {
    const std::size_t psc = std::size_t(m_audioInput.size() * m_stretcherFactor) + 1; // needed count, but rounded up
    const std::size_t blockCount = psc / m_audioPlaySamplesRealPerBlock + 1; // always add one block

    m_audioPlayBlockData.resize(blockCount * m_audioPlaySamplesDataPerBlock);
    m_audioPlayBlockFlag.resize(blockCount);

    for (uint8_t &f : m_audioPlayBlockFlag) f = 0;
  }

  void        setStretchFactor(float stretch) { m_stretcherFactor = stretch; resetGeneratedAudio(); }
  void        setPitchFactor(float pitch)     { m_pitchFactor = pitch; resetGeneratedAudio(); }
  bool        empty() const                   { return m_audioInput.empty() || m_audioPlayBlockFlag.empty(); }
  std::size_t playSamplesCount() const        { return std::size_t(m_audioInput.size() * m_stretcherFactor); } // needed count

  std::size_t cursorToBlockId(float cursor) const
  {
    if (empty()) return 0;
    const std::size_t sampleIndex = std::size_t(cursor * playSamplesCount());
    const std::size_t blockId = sampleIndex / m_audioPlaySamplesRealPerBlock;
    TRE_ASSERT(blockId < m_audioPlayBlockFlag.size());
    return blockId;
  }

  virtual void sync() override final
  {
    if (freq == 0) return;
    if (empty()) { ac_play = false; m_playCursor = 0.f; return; }
    const std::size_t psc = playSamplesCount();

    // receive
    if (ac_audioPlayDataSizeLast != 0) m_playCursor = float(ac_playCursor) / float(ac_audioPlayDataSizeLast);

    // send
    ac_play = m_play;
    ac_playCursorStart = unsigned(m_playCursorStart * psc);
    ac_playCursorEnd   = unsigned(m_playCursorEnd   * psc);
    if (m_playCursorSet >= 0.f) { ac_playCursor = unsigned(m_playCursorSet * psc); m_playCursorSet = -1.f; }
    else if (psc != ac_audioPlayDataSizeLast) { ac_playCursor = unsigned(m_playCursor * psc);  }
    ac_audioPlayDataSizeLast = psc;
  }

  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override final
  {
    freq = sampleFreq;
    if (!ac_play) return;

    const unsigned blockCount = unsigned(m_audioPlayBlockFlag.size());
    const unsigned overlap = unsigned(m_audioPlaySamplesDataPerBlock - m_audioPlaySamplesDataPerBlock);

    for (unsigned i = 0; i < sampleCount; ++i, ++ac_playCursor)
    {
      ac_volume = ac_volume * 0.9f + m_volume * 0.1f;

      const unsigned blockId = ac_playCursor / unsigned(m_audioPlaySamplesRealPerBlock);
      const unsigned posInBlock = ac_playCursor - blockId * unsigned(m_audioPlaySamplesRealPerBlock);

      if (ac_playCursor >= ac_playCursorEnd) ac_playCursor = ac_playCursorStart - 1;
      if (blockId >= blockCount) continue;

      float v = m_audioPlayBlockData[blockId * unsigned(m_audioPlaySamplesDataPerBlock) + posInBlock];
      if (posInBlock < overlap && blockId != 0)
      {
        const float blend = float (posInBlock + 0.5f) / float(overlap);
        v = blend * v + (1.f - blend) * m_audioPlayBlockData[blockId * unsigned(m_audioPlaySamplesDataPerBlock) + posInBlock - overlap];
      }

      v *= ac_volume;
      outBufferAdd[2*i+0] += v;
      outBufferAdd[2*i+1] += v;
    }

  }
};

// Algorithm ==================================================================

struct s_stretcher
{
  enum
  {
    OptionMask_DualBand    = (1 << 0),
  };

  /// Compute Discrete-Fourier transform.
  /// spectre[k] = sum_j { signal[j] exp((-i 2 pi / N) j k) }
  /// As the input signal is pure-real, the given sprectrum is symetric: spectre[k] == conjugate(spectre[N-k]) for k !=0, spectre[0] is pure-real.
  /// The frequency obtained at k is: spectre(k * Fs / N), where Fs is the sampling frequency. So the spectre resolution is Fs / N.
  static void dft(const std::vector<float> &signal, std::vector<float> &spectre)
  {
    const std::size_t _Np = signal.size();
    TRE_ASSERT((_Np & (_Np - 1)) == 0);  // DFT requires power-of-two length
    TRE_ASSERT(signal.size() == _Np && spectre.size() == 2 * _Np);
    // Note: Cooley-Tukey algorithm
    // -> Prepare data
    {
      std::size_t target = 0;
      for (std::size_t pos = 0; pos < _Np; ++pos)
      {
        spectre[pos      ] = signal[target]; // real-part
        spectre[pos + _Np] = 0.f;            // imag-part
        std::size_t mask = _Np;
        while (target & (mask >>= 1)) target &= ~mask;
        target |= mask;
      }
    }
    // -> Perform Fourier-transform
    for (std::size_t step = 1; step < _Np; step <<= 1)
    {
      const std::size_t pairIncr = step << 1;
      const float delta = -pi / float(step);
      const float sinHalf = std::sin(delta * 0.5f);
      const float multRe = -2.f * sinHalf * sinHalf; // multiplier, real-part
      const float multIm = std::sin(delta);          // multiplier, imag-part
      float factorRe = 1.f;
      float factorIm = 0.f;
      for (std::size_t group = 0; group < step; ++group)
      {
        for (std::size_t pair = group; pair < _Np; pair += pairIncr)
        {
          const std::size_t pairB = pair + step;
          float &dataAre = spectre[pair  + 0];
          float &dataAim = spectre[pair  + _Np];
          float &dataBre = spectre[pairB + 0];
          float &dataBim = spectre[pairB + _Np];
          const float prodRe = factorRe * dataBre - factorIm * dataBim; // prod = factor * dataB
          const float prodIm = factorRe * dataBim + factorIm * dataBre;
          dataBre = dataAre - prodRe;
          dataBim = dataAim - prodIm;
          dataAre += prodRe;
          dataAim += prodIm;
        }
        const float factorRe_NEW = factorRe + multRe * factorRe - multIm * factorIm; // trigonometric recurrence (factor = exp(-i 2 pi group / step))
        factorIm += multRe * factorIm + multIm * factorRe;
        factorRe = factorRe_NEW;
      }
    }
  }

  /// Compute Inverse Discrete-Fourier transform.
  /// signal[j] = (1/N) sum_k { spectre[k] exp((i 2 pi / N) j k) }
  /// Warning: the spectre is modified, and leaved undefined.
  static void idft(std::vector<float> &spectre, std::vector<float> &signal)
  {
    const std::size_t _Np = signal.size();
    TRE_ASSERT((_Np & (_Np - 1)) == 0);  // DFT requires power-of-two length
    TRE_ASSERT(signal.size() == _Np && spectre.size() == 2 * _Np);
    // Note: Cooley-Tukey algorithm
    // -> Prepare data
    {
      std::size_t target = 0;
      for (std::size_t pos = 0; pos < _Np; ++pos)
      {
        if (target > pos)
        {
          std::swap(spectre[pos], spectre[target]); // real-part
          std::swap(spectre[pos + _Np], spectre[target + _Np]); // imag-part
        }
        std::size_t mask = _Np;
        while (target & (mask >>= 1)) target &= ~mask;
        target |= mask;
      }
    }
    // -> Perform Fourier-transform
    for (std::size_t step = 1; step < _Np; step <<= 1)
    {
      const std::size_t pairIncr = step << 1;
      const float delta = pi / float(step);
      const float sinHalf = std::sin(delta * 0.5f);
      const float multRe = -2.f * sinHalf * sinHalf; // multiplier, real-part
      const float multIm = std::sin(delta);          // multiplier, imag-part
      float factorRe = 1.f;
      float factorIm = 0.f;
      for (std::size_t group = 0; group < step; ++group)
      {
        for (std::size_t pair = group; pair < _Np; pair += pairIncr)
        {
          const std::size_t pairB = pair + step;
          float &dataAre = spectre[pair  + 0];
          float &dataAim = spectre[pair  + _Np];
          float &dataBre = spectre[pairB + 0];
          float &dataBim = spectre[pairB + _Np];
          const float prodRe = factorRe * dataBre - factorIm * dataBim; // prod = factor * dataB
          const float prodIm = factorRe * dataBim + factorIm * dataBre;
          dataBre = dataAre - prodRe;
          dataBim = dataAim - prodIm;
          dataAre += prodRe;
          dataAim += prodIm;
        }
        const float factorRe_NEW = factorRe + multRe * factorRe - multIm * factorIm; // trigonometric recurrence (factor = exp(-i 2 pi group / step))
        factorIm += multRe * factorIm + multIm * factorRe;
        factorRe = factorRe_NEW;
      }
    }
    // -> End
    const float invN = 1.f / float(_Np);
    for (std::size_t pos = 0; pos < _Np; ++pos)
    {
      signal[pos] = spectre[pos] * invN;
    }
  }


  std::vector<float> m_blockIn; // working data
  std::vector<float> m_blockSpectrum; // working data
  std::vector<float> m_blockOut; // working data

  void generateChunk(c_soundSampler &data, std::size_t chunkId, unsigned optionFlags)
  {
    TRE_ASSERT(chunkId < data.m_audioPlayBlockData.size());
    TRE_ASSERT(data.m_audioPlayBlockFlag[chunkId] == 0);
    const std::size_t blockSize = data.m_audioPlaySamplesDataPerBlock;

    m_blockIn.resize(blockSize);
    m_blockSpectrum.resize(blockSize * 2);
    m_blockOut.resize(blockSize);

    {
      // load input audio, pithched.
      const float       inputSampleOffset = float(chunkId * data.m_audioPlaySamplesRealPerBlock) / data.m_stretcherFactor;
      const std::size_t sampleInSize = data.m_audioInput.size();
      std::size_t is = 0;
      for (; is < blockSize; ++is)
      {
        const std::size_t isinc = std::size_t(inputSampleOffset + data.m_pitchFactor * (float(is) + 0.5f));
        if (isinc >= sampleInSize) break;
        m_blockIn[is] = data.m_audioInput[isinc];
      }
      for (; is < blockSize; ++is)
      {
        m_blockIn[is] = 0.f;
      }
    }

    if (optionFlags & OptionMask_DualBand)
    {
      dft(m_blockIn, m_blockSpectrum);
      for (std::size_t k = blockSize/2-100; k < blockSize/2; ++k) { m_blockSpectrum[k] = 0.f; m_blockSpectrum[k+blockSize] = 0.f; m_blockSpectrum[blockSize-k] = 0.f; m_blockSpectrum[blockSize-k+blockSize] = 0.f; } // TEST: low-pass filter (kill high freqs)
      idft(m_blockSpectrum, m_blockIn);
    }

    // final copy
    const std::size_t outputDataOffset = chunkId * blockSize;
    for (std::size_t is = 0; is < blockSize; ++is)
      data.m_audioPlayBlockData[outputDataOffset + is] = m_blockIn[is];

    data.m_audioPlayBlockFlag[chunkId] = 1;
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
    const int nSamples = int(wsndSampler.m_audioInput.size());
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
          const float v = wsndSampler.m_audioInput[is];
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
          const float v = wsndSampler.m_audioInput[is];
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
    wTextureUpload = SDL_CreateRGBSurface(0, 1024, 48, 32, 0, 0, 0, 0);
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
    ret.m_textureSlot = 0;
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
    const float cursorPosStart = m_zone.x + (m_zone.z - m_zone.x) * wcursorA;
    const float cursorPosEnd = m_zone.x + (m_zone.z - m_zone.x) * wcursorB;

    tre::ui::fillRect(dd.m_bufferSolid, glm::vec4(cursorPos - 5.f * dd.m_pixelSize.x, m_zone.y, cursorPos + 5.f * dd.m_pixelSize.x, m_zone.w), colorFront);

    tre::ui::fillRect(dd.m_bufferPict, m_zone, glm::vec4(1.f), glm::vec4(0.f, 0.f, 1.f, 1.f));

    const float h = wheightModifier * dd.resolve_sizeH(get_parentWindow()->get_fontHeight());

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

    // TODO ...
    wselectMode;

    const bool isClickedL = (event.mouseButtonIsPressed & SDL_BUTTON_LMASK) != 0 && (event.mouseButtonPrev & SDL_BUTTON_LMASK) == 0;

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

  c_soundSampler &wsndSampler;

  bool wDirty = false;
  void set_dirty() { wDirty = true; }

  void updateSpectrum()
  {
    const uint32_t colorLine = colorToRGBA(glm::vec4(0.f, 0.f, 0.f, 1.f));

    uint32_t * __restrict pxData = reinterpret_cast<uint32_t*>(wTextureUpload->pixels);
    const int w = wTextureUpload->w; // only first mip-map level (maybe add in futur)
    const int h = wTextureUpload->h;
    const int nSamples = int(wsndSampler.m_audioInput.size());
    const int nSamplesWindow = 128;

    std::fill_n(pxData, w * h, 0);
    // TODO

    wTexture.update(wTextureUpload, false);
  }

public:
  widgetSpectrum(c_soundSampler &sndSampler) : widget(), wsndSampler(sndSampler)
  {
    wTextureUpload = SDL_CreateRGBSurface(0, 1024, 48, 32, 0, 0, 0, 0);
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
    ret.m_textureSlot = 1;
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
s_stretcher              stretcher;

char *fileLoaded = nullptr;

glm::vec4 colorBack  = glm::vec4(1.0f, 1.0f, 1.0f, 1.f);
glm::vec4 colorFront = glm::vec4(0.0f, 0.0f, 0.0f, 1.f);

tre::baseUI2D uiMain;
tre::font     uiFont;

tre::ui::window *uiwTopBar = uiMain.create_window();
tre::ui::window *uiwTicker = uiMain.create_window();
tre::ui::window *uiwPlayer = uiMain.create_window();
tre::ui::window *uiwOption = uiMain.create_window();

int      uiTopBarSelectedID = 0;
unsigned uiStretcherOptions = 0;

enum e_colorTheme { ColorTheme_WhiteBlue };
e_colorTheme uiColorTheme = ColorTheme_WhiteBlue;
static const std::vector<const char *> kuiColorThemeNames = { "Light blue" };

// -----------------------------------------------------------------------------

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
    widT->wcb_clicked_left = [](tre::ui::widget *self) { uiTopBarSelectedID = 0; };
    widT->wcb_animate = [](tre::ui::widget* self, float) { self->set_color(glm::vec4(0.5f, 0.f, 1.f * (uiTopBarSelectedID == 0), 1.f)); };

    tre::ui::widget *widP = uiwTopBar->create_widgetText(0, 1)->set_text("Player")->set_isactive(true);
    widP->wcb_clicked_left = [](tre::ui::widget *self) { uiTopBarSelectedID = 1; };
    widP->wcb_animate = [](tre::ui::widget* self, float) { self->set_color(glm::vec4(0.5f, 0.f, 1.f * (uiTopBarSelectedID == 1), 1.f)); };

    tre::ui::widget *widO = uiwTopBar->create_widgetText(0, 2)->set_text("Option")->set_isactive(true);
    widO->wcb_clicked_left = [](tre::ui::widget *self) { uiTopBarSelectedID = 2; };
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

    tre::ui::widgetBar *wT = uiwTicker->create_widgetBar(2, 0, 1, 2)->set_value(soundTicker.m_tempo * 1.f)->set_valuemin(20)->set_valuemax(400)->set_snapInterval(1.f)->set_widthFactor(10.f)->set_withtext(true);
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

    widgetSpectrum *wSpectrum = new widgetSpectrum(soundSampler);

#ifdef HAS_AUDIO_OPENER
    uiwPlayer->create_widgetText(0, 0)->set_text("Open File ...")->set_isactive(true)
      ->wcb_clicked_left = [wPlayCursor](tre::ui::widget*)
    {
      char *fileRequested = getFilePath();
      if (fileRequested == nullptr) return;
      const bool loadOk = loadAudioFile(fileRequested, soundSampler.m_audioInput, soundSampler.freq);
      wPlayCursor->wcursorA    = soundSampler.m_playCursorStart = 0.f;
      wPlayCursor->wcursorB    = soundSampler.m_playCursorEnd = 1.f;
      wPlayCursor->wcursorCurr = soundSampler.m_playCursorSet = 0.f;
      soundSampler.resetGeneratedAudio();
      soundSampler.m_play = loadOk;
      wPlayCursor->set_dirty();
      if (fileLoaded != nullptr) free(fileLoaded);
      fileLoaded = nullptr;
      if (loadOk) fileLoaded = fileRequested;
    };
#else
    uiwPlayer->create_widgetText(0, 0)->set_text("Open Audio");
#endif
#if defined(HAS_AUDIO_OPENER) && !defined(TRE_DEBUG)
    uiwPlayer->create_widgetText(0, 1)->wcb_animate = [] (tre::ui::widget* self, float)
    {
      static const char *empty = "";
      //static_cast<tre::ui::widgetText*>(self)->set_text(fileLoaded != nullptr ? fileLoaded : empty);
    };
#else
    static const std::array<const char*, 6> testNames = { "NONE", "sin-440Hz", "sin-varying", "dual-sin", "kick", "noise" };
    uiwPlayer->create_widgetLineChoice(0, 1)->set_values(testNames)->set_selectedIndex(0)->set_cyclic(true)->set_isactive(true)->set_iseditable(true)
      ->wcb_modified_finished = [wPlayCursor](tre::ui::widget* self)
    {
      const int choice = static_cast<tre::ui::widgetLineChoice*>(self)->get_selectedIndex();
      soundSampler.freq = 44100;
      soundSampler.m_audioInput.clear();
      soundSampler.m_audioInput.resize(44100 * 5, 0.f); // 5 seconds
      switch (choice)
      {
        case 1: // sin-440Hz
          for (unsigned i = 0; i < soundSampler.m_audioInput.size(); ++i)
          {
            const float t = float(i) / float(soundSampler.freq); // time in seconds
            soundSampler.m_audioInput[i] = 0.8f * std::sin(2.f * pi * 440.f * t);
          }
        break;
        case 2: // sin-varying
          for (unsigned i = 0; i < soundSampler.m_audioInput.size(); ++i)
          {
            const float t = float(i) / float(soundSampler.freq); // time in seconds
            soundSampler.m_audioInput[i] = 0.8f * std::sin(2.f * pi * (440.f + 600.f * t / 5.f) * t);
          }
        break;
        case 3: // dual-sin
          for (unsigned i = 0; i < soundSampler.m_audioInput.size(); ++i)
          {
            const float t = float(i) / float(soundSampler.freq); // time in seconds
            soundSampler.m_audioInput[i] = 0.45f * std::sin(2.f * pi * 440.f * t) + 0.45f * std::sin(2.f * pi * 440.f * 2.5f * t + 0.1f);
          }
        break;
        case 4: // kick
          for (unsigned i = 0; i < soundSampler.m_audioInput.size(); ++i)
          {
            const float t = float(i) / float(soundSampler.freq); // time in seconds
            const float tA = t * (140.f / 60.f);
            const float tSelf = std::fmod(tA, 1.f);
            soundSampler.m_audioInput[i] = 0.9f * std::sin(2.f * pi * (80.f - 60.f * tSelf) * tSelf) * std::max(1.f - 1.5f * tSelf * tSelf, 0.f);
          }
          case 5: // noise
          // TODO
        break;
      }
      wPlayCursor->wcursorA    = soundSampler.m_playCursorStart = 0.f;
      wPlayCursor->wcursorB    = soundSampler.m_playCursorEnd = 1.f;
      wPlayCursor->wcursorCurr = soundSampler.m_playCursorSet = 0.f;
      soundSampler.resetGeneratedAudio();
      soundSampler.m_play = (choice != 0);
      wPlayCursor->set_dirty();
    };
#endif

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

    uiMain.addTexture(&wPlayCursor->wTexture); // slot 0
    uiMain.addTexture(&wSpectrum->wTexture); // slot 1
  }

  {
    uiwOption->set_alignMask(tre::ui::ALIGN_MASK_LEFT_TOP);
    uiwOption->set_layoutGrid(6, 2);
    uiwOption->set_colAlignment(0, tre::ui::ALIGN_MASK_CENTERED);
    uiwOption->set_colAlignment(1, tre::ui::ALIGN_MASK_CENTERED);

    unsigned row = -1;

    uiwOption->create_widgetText(++row, 0)->set_text("Stretcher: Dual Band");
    uiwOption->create_widgetBoxCheck(row, 1)->set_value((uiStretcherOptions &s_stretcher::OptionMask_DualBand) != 0)->set_iseditable(true)->set_isactive(true)
      ->wcb_modified_finished = [](tre::ui::widget *self)
    {
      const bool wval = static_cast<tre::ui::widgetBoxCheck*>(self)->get_value();
      uiStretcherOptions &= ~(s_stretcher::OptionMask_DualBand);
      if (wval) uiStretcherOptions |= (s_stretcher::OptionMask_DualBand);
      soundSampler.resetGeneratedAudio();
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
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_LEFT) soundSampler.m_playCursorSet = 0.f; // TODO
      if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RIGHT) soundSampler.m_playCursorSet = 1.f; // TODO
    }

    uiMain.acceptEvent(event);
  }

  if (winContext.m_viewportResized) onResize();

  // Update audio

  uiMain.animate(0.005f);

  // -> generate played-audio (by chuncks)
  if (!soundSampler.empty())
  {
    const std::size_t icCursorPlay   = soundSampler.cursorToBlockId(soundSampler.m_playCursor     );
    const std::size_t icCursorStart  = soundSampler.cursorToBlockId(soundSampler.m_playCursorStart);
    const std::size_t icCursorLast   = soundSampler.cursorToBlockId(soundSampler.m_playCursorEnd  );
    for (std::size_t step = 0, stepBudget = 2; step < stepBudget; ++step)
    {
      std::size_t chunkBest = 0;
      std::size_t scoreBest = 0;
      for (std::size_t i = icCursorStart; i <= icCursorLast; ++i)
      {
        if (soundSampler.m_audioPlayBlockFlag[i] != 0) continue;
        const std::size_t score = 1 + (i >= icCursorPlay ? 1 : 0) + (i == icCursorPlay ? 1 : 0);
        if (score > scoreBest) { chunkBest = i; scoreBest = score; }
      }
      if (soundSampler.m_audioPlayBlockFlag[chunkBest] != 0) break;
      TRE_LOG("Generate chunk " << chunkBest << " / " << soundSampler.m_audioPlayBlockFlag.size() << " (player at chunk " << icCursorPlay << ")");
      stretcher.generateChunk(soundSampler, chunkBest, uiStretcherOptions);
    }
  }
  else
  {
    soundSampler.m_play = false;
  }

  audioCtx.updateSystem();

  // Draw and Present

  glViewport(0, 0, winContext.m_resolutioncurrent.x, winContext.m_resolutioncurrent.y);
  glClearColor(colorBack.r, colorBack.b, colorBack.b, 1.f);
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
  (void)argc;
  (void)argv;

  if (!myInit()) return -1;

  onColorThemeChanged();
  onResize();

  while (!winContext.m_quit)
  {
    myUpdate();
  }

  myQuit();

  return 0;
}
