#ifndef AUDIO_H
#define AUDIO_H

#include <tre_utils.h> // ASSERT

#include <string>
#include <fstream>
#include <vector>
#include <math.h>
#include <limits>

// foward decl of the SDL
struct SDL_AudioSpec;
typedef uint32_t SDL_AudioDeviceID;
typedef uint16_t SDL_AudioFormat;

// foward decl of Opus
struct OpusDecoder;


namespace tre
{

// ============================================================================

/**
 * The soundData namespace holds default implemented audio data handlers.
 * The audio data can be baked into binary stream (and so binary files).
 */
namespace soundData
{

/**
 * @brief The s_RawSDL struct
 */
struct s_RawSDL
{
  unsigned        m_nSamples = 0;
  int             m_freq = 0;
  SDL_AudioFormat m_format = 0;
  bool            m_stereo; ///< mono (1 channel) / stereo (2 channels)

  typedef uint32_t audio_t;
  std::vector<audio_t> m_rawData; ///< audio data (re-interpreted as 'audio_t')

  bool loadFromWAV(const std::string & filename); ///< Load from a WAV file.
  bool loadFromSDLAudio(unsigned samples, int freq, SDL_AudioFormat format, bool stereo, uint8_t *audioBuffer); ///< Load from SDL audio data (it performs a deep copy).

  bool write(std::ostream &stream) const; ///< Write baked-file (with compression if bitrate != 0)
  bool read(std::istream &stream); ///< Load baked-file

  bool convertTo(int freq, SDL_AudioFormat format);
};

/**
 * @brief The s_Opus struct
 * format is 16-bit signed integers.
 */
struct s_Opus
{
  static const int m_freq = 48000;
  unsigned        m_nSamples = 0;
  bool            m_stereo; ///< mono (1 channel) / stereo (2 channels)

  struct s_block
  {
    int                  m_sampleStart; ///< sample-index of the first encoded sample. Can be negative on the first block with the codec pre-skip.
    std::vector<uint8_t> m_data; ///< encoded samples.
  };
  std::vector<s_block> m_blokcs;

  bool loadFromOPUS(const std::string & filename); ///< Load from an OPUS file.
  bool loadFromRaw(s_RawSDL &rawData, unsigned bitrate); ///< Load and compress raw-data with Opus encoder, and store compressed data. The "rawData" might be converted to another format.

  bool write(std::ostream &stream) const; ///< Write baked-file (with compression if bitrate != 0)
  bool read(std::istream &stream); ///< Load baked-file

  unsigned getBlockAtSampleId(int sid) const;
};

} // namespace "soundData"

// ============================================================================

/**
 * @brief The soundInterface class defines the interface (API) needed to be processed by the audio-context.
 */
class soundInterface
{
public:
  soundInterface() {}
  virtual ~soundInterface() {}

  /**
   * @brief synchronization point
   * Called outside of the sound-thread, to exchange safely data between the audio-thread and other threads.
   */
  virtual void sync() = 0;

  /**
   * @brief raw sampling by the audio-thread
   * Called inside the sound-thread, when the audio-thread requires stereo-data (LR, LR, LR, ...)
   * @param outBufferAdd audio buffer (data must be 'added')
   * @param sampleCount (number of samples (1 sample is 2 floats, representing the left and right chanel data)
   * @param sampleFraq (frequency required for the out buffer
   */
  virtual void sample(float* __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) = 0;
};

// ============================================================================

/**
 * @brief The audioContext class opens a SDL audio device and it feeds the audio data.
 * It plays sounds that have been recorded by "addSound(soundInterface*)". The caller is responsible to keep the given pointer valid.
 * The sound-list update and the sound-data change will take effect on the next "updateSystem()" call.
 * In particular, the sound-pointer can be freed after the call of "removeSound(...)" and "updateSystem()" in this order.
 */
class audioContext
{
public:
  audioContext() {}
  ~audioContext() { stopSystem(); }

  /**
   * @brief create the audio-context
   * @param name of the device. Can be null.
   * @param determine the buffer size on the hardware to hold X miliseconds of audio. Depending on the hardware, the buffer-size is not guaranted.
   * @param Only freq, channels, format are needed when requiredAudioSpec is provided. Default settings if null.
   * @return true if the audio system is running, false if failed.
   */
  bool startSystem(const char *deviceName, unsigned bufferMS = 33, SDL_AudioSpec *requiredAudioSpec = nullptr);

  void updateSystem(); ///< sync with the audio-context (send the sound-list, send sound-data, receive play stats). To be called on the main application tick.

  void stopSystem(); ///< shut-down the audio-context and clear data.

  const SDL_AudioSpec *getAudioSpec() const { return m_audioSpec; }

  void addSound(soundInterface *sound); ///< add a sound playable by the audio. The sound is actually ready to be played once 'updateSystem()' is called.
  void removeSound(soundInterface *sound); ///< remove a sound playable by the audio. The sound is actually removed on the next 'updateSystem()' call. Do not free it before !

protected:
  SDL_AudioDeviceID m_deviceID = 0;
  SDL_AudioSpec     *m_audioSpec = nullptr;

  std::vector<soundInterface*> m_listSounds;

#ifdef TRE_PROFILE
protected:
  float    m_perftime_total = 0.f;
  unsigned m_perftime_nbrCall = 0;
  unsigned m_perftime_nbrSample = 0;
public:
  float    getPerfTime_Total() const { return m_perftime_total; } ///< Elapsed time spent in the audio callback (in seconds)
  unsigned getPerfTime_nbrCall() const { return m_perftime_nbrCall; }
  unsigned getPerfTime_nbrSample() const { return m_perftime_nbrSample; }
#else
public:
  float    getPerfTime_Total() const { return 0.f; }
  unsigned getPerfTime_nbrCall() const { return 0; }
  unsigned getPerfTime_nbrSample() const { return 0; }
#endif

public:
  static void getDevicesName(std::vector<std::string> &devices); ///< Helper, that gets the list of detected devices

private:
  struct s_audioCallbackContext
  {
    std::vector<soundInterface*> ac_listSounds;
    std::vector<float>           ac_bufferF32;
    int                          ac_freq = 0;
    unsigned                     ac_channels = 0;
#ifdef TRE_PROFILE
  float                          ac_perftime_total = 0.f;
  unsigned                       ac_perftime_nbrCall = 0;
  unsigned                       ac_perftime_nbrSample = 0;
#endif

    void run(uint8_t* stream, int len);
  };
  s_audioCallbackContext m_audioCallbackContext;

  static void audio_callback(void *userdata, uint8_t* stream, int len)
  {
    s_audioCallbackContext *ctx = reinterpret_cast<s_audioCallbackContext*>(userdata);
    ctx->run(stream, len);
  }
};

// ============================================================================

/**
 * The namespace "soundSampler" is an implementation of sound sampling,
 * using audio data from the soundData namespace.
 * It also defines some "controls", that transform the sampled data (gain, post-effects, ...)
 *
 * The controls must define the two following methods:
 * - <control> mix(<control>, <control>, float cursor);
 * - <void> apply(float &valueL, float &valueR) const;
 *
 * The sampler is defining the following method and members
 * - <void> sample(<soundData>, <controls>, <controls>, float* outBufferAdd, unsigned sampleCount, int sampleFreq);
 */
namespace soundSampler
{

// Controls: ---

struct s_noControl
{
  s_noControl() {}

  static s_noControl mix(const s_noControl &, const s_noControl &, const float) { return s_noControl(); }

  void apply(float &valueL, float &valueR) const { (void)valueL; (void)valueR; }
};

struct s_stereoControl
{
  s_stereoControl(const float vol = 0.f, const float pan = 0.f) : m_volume(vol), m_pan(pan) {}

  float        m_volume; ///< [0, +inf[
  float        m_pan;    ///< [-1, 1]

  static s_stereoControl mix(const s_stereoControl &a, const s_stereoControl &b, const float t)
  {
    const float ta = 1.f - t;
    return s_stereoControl(a.m_volume * ta + b.m_volume * t, a.m_pan * ta + b.m_pan * t);
  }

  void apply(float &valueL, float &valueR) const
  {
    const float pL = 1.f - std::max(m_pan, 0.f);
    const float pR = 1.f + std::min(m_pan, 0.f);
    valueL *= m_volume * pL;
    valueR *= m_volume * pR;
  }
};

// Samplers: ---

struct s_sampler_Raw
{
  float    m_cursor = 0.f;
  bool     m_repet = false;

  float   m_valuePeak = 0.f; ///< [out]
  float   m_valueRMS = 0.f;  ///< [out]

  /// "sample" returns stereo float audio stream, at "sampleFreq" Hz.
  template<class _controls, typename _rawType>
  void _sample(const soundData::s_RawSDL &data, const _controls &controlsStart, const _controls &controlsEnd,
              float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
  {
    TRE_ASSERT(SDL_AUDIO_BITSIZE(data.m_format) / 8 == sizeof(_rawType));

    const float                 freqRatio = float(data.m_freq) / float(sampleFreq);
    const unsigned              dataSampleCount = data.m_nSamples;
    const int                   dataSampleCountM2 = int(dataSampleCount) - 2;
    const _rawType * __restrict dataRawTyped = reinterpret_cast<const _rawType*>(data.m_rawData.data());
    unsigned                    curSample = 0;
    const float                 invSampleCountM1 = 1.f / (sampleCount - 1);
    static const float          valueNormalizer = 1.f / float(std::numeric_limits<_rawType>::max());
    const _rawType              mV = std::numeric_limits<_rawType>::max();

    TRE_ASSERT(dataSampleCount != 0);
    if (m_repet)
    {
      while (m_cursor >= dataSampleCount) m_cursor -= dataSampleCount;
    }

    m_valuePeak = 0.f;
    m_valueRMS = 0.f;

    while (m_cursor < dataSampleCount && curSample < sampleCount)
    {
      if (data.m_stereo)
      {
        unsigned isample = 0;
        float    localCursor = m_cursor;
        while (localCursor < dataSampleCount && curSample < sampleCount)
        {
          const int localCursorInt = std::min(int(localCursor), dataSampleCountM2);
          const float valueL_0 = float(dataRawTyped[2 * localCursorInt + 0]) * valueNormalizer;
          const float valueR_0 = float(dataRawTyped[2 * localCursorInt + 1]) * valueNormalizer;
          const float valueL_1 = float(dataRawTyped[2 * localCursorInt + 2]) * valueNormalizer;
          const float valueR_1 = float(dataRawTyped[2 * localCursorInt + 3]) * valueNormalizer;
          const float w_1 = localCursor - localCursorInt;
          const float w_0 = 1.f - w_1;
          float valueL = w_0 * valueL_0 + w_1 * valueL_1;
          float valueR = w_0 * valueR_0 + w_1 * valueR_1;
          _controls::mix(controlsStart, controlsEnd, curSample * invSampleCountM1).apply(valueL, valueR);
          ++curSample;
          *outBufferAdd++ += valueL;
          *outBufferAdd++ += valueR;
          m_valuePeak = std::max(m_valuePeak, std::max(std::abs(valueL), std::abs(valueR)));
          m_valueRMS += 0.5f * (valueL * valueL + valueR * valueR);
          localCursor = m_cursor + (++isample) * freqRatio;
        }
        m_cursor = localCursor;
      }
      else
      {
        unsigned isample = 0;
        float    localCursor = m_cursor;
        while (localCursor < dataSampleCount && curSample < sampleCount)
        {
          const int localCursorInt = std::min(int(localCursor), dataSampleCountM2);
          const float value_0 = float(dataRawTyped[localCursorInt + 0]) * valueNormalizer;
          const float value_1 = float(dataRawTyped[localCursorInt + 1]) * valueNormalizer;
          const float w_1 = localCursor - localCursorInt;
          const float w_0 = 1.f - w_1;
          float valueL = w_0 * value_0 + w_1 * value_1;
          float valueR = valueL;
          _controls::mix(controlsStart, controlsEnd, curSample * invSampleCountM1).apply(valueL, valueR);
          ++curSample;
          *outBufferAdd++ += valueL;
          *outBufferAdd++ += valueR;
          m_valuePeak = std::max(m_valuePeak, std::max(std::abs(valueL), std::abs(valueR)));
          m_valueRMS += 0.5f * (valueL * valueL + valueR * valueR);
          localCursor = m_cursor + (++isample) * freqRatio;
        }
        m_cursor = localCursor;
      }

      if (m_repet)
      {
        if (m_cursor >= dataSampleCount) m_cursor -= dataSampleCount;
      }
    }

    m_valueRMS = std::sqrt(m_valueRMS / sampleCount);
  }

  /// "sample" returns stereo float audio stream, at "sampleFreq" Hz.
  template<class _controls>
  void sample(const soundData::s_RawSDL &data, const _controls &controlsStart, const _controls &controlsEnd,
              float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
  {
    if (data.m_format == AUDIO_S16)
      _sample<_controls, int16_t>(data, controlsStart, controlsEnd, outBufferAdd, sampleCount, sampleFreq);
    else if (data.m_format == AUDIO_S32)
      _sample<_controls, int32_t>(data, controlsStart, controlsEnd, outBufferAdd, sampleCount, sampleFreq);
    else
      TRE_FATAL("soundSampler: the audio format (" << int(data.m_format) << ") for raw-sampling is not supported.");
  }
};

struct s_sampler_Opus
{
  s_sampler_Opus();
  ~s_sampler_Opus();

  float    m_cursor = 0.f;
  bool     m_repet = false;

  OpusDecoder                        *m_decoder = nullptr;
  unsigned                           m_decompressedSlot = unsigned(-1);
  unsigned                           m_decompressedCount = unsigned(-1); ///< nbr of samples in the buffer. Maximun 2880 (60ms of sound at 48 kHz).
  std::array<int16_t, 2880 * 2 + 16> m_decompressedBuffer; ///< decompressed data (extra data to handle linear-interpolation on the last element) [maximun size allowed]

  float   m_valuePeak = 0.f; ///< [out]
  float   m_valueRMS = 0.f;  ///< [out]

  bool decodeSlot(const soundData::s_Opus &data, unsigned slot);

  /// "sample" returns stereo float audio stream, at "sampleFreq" Hz.
  template<class _controls>
  void sample(const soundData::s_Opus &data, const _controls &controlsStart, const _controls &controlsEnd,
              float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
  {
    const float        freqRatio = float(soundData::s_Opus::m_freq) / float(sampleFreq);
    const unsigned     dataSampleCount = data.m_nSamples;
    unsigned           curSample = 0;
    const float        invSampleCountM1 = 1.f / (sampleCount - 1);
    static const float valueNormalizer = 1.f / float(0x7FFF);

    TRE_ASSERT(dataSampleCount != 0);
    if (m_repet)
    {
      while (m_cursor >= dataSampleCount) m_cursor -= dataSampleCount;
    }

    m_valuePeak = 0.f;
    m_valueRMS = 0.f;

    while (m_cursor < dataSampleCount && curSample < sampleCount)
    {
      const int      cursorInt = int(m_cursor);
      const unsigned targetSlot = data.getBlockAtSampleId(cursorInt);
      if (targetSlot == unsigned(-1))
      {
        TRE_LOG("audio sampling: invalid input data from a soundData::s_Opus");
        return; // bad data
      }
      if (!decodeSlot(data, targetSlot))
      {
        TRE_LOG("audio sampling: failed to decode slot " << targetSlot << " from a soundData::s_Opus");
        return;
      }

      const int      dataDecodedSampleOffset = data.m_blokcs[targetSlot].m_sampleStart;
      const unsigned dataDecodedSampleCount = std::min(m_decompressedCount, dataSampleCount - dataDecodedSampleOffset);

      unsigned isample = 0;
      float    localCursorOffset = m_cursor - dataDecodedSampleOffset;
      float    localCursor = localCursorOffset;
      while (localCursor < dataDecodedSampleCount && curSample < sampleCount)
      {
        const int localCursorInt = int(localCursor); // implicit std::trunc + float-to-int
        const float valueL_0 = float(m_decompressedBuffer[2 * localCursorInt + 0]) * valueNormalizer;
        const float valueR_0 = float(m_decompressedBuffer[2 * localCursorInt + 1]) * valueNormalizer;
        const float valueL_1 = float(m_decompressedBuffer[2 * localCursorInt + 2]) * valueNormalizer;
        const float valueR_1 = float(m_decompressedBuffer[2 * localCursorInt + 3]) * valueNormalizer;
        const float w_1 = localCursor - localCursorInt;
        const float w_0 = 1.f - w_1;
        float valueL = w_0 * valueL_0 + w_1 * valueL_1;
        float valueR = w_0 * valueR_0 + w_1 * valueR_1;
        _controls::mix(controlsStart, controlsEnd, curSample * invSampleCountM1).apply(valueL, valueR);
        ++curSample;
        *outBufferAdd++ += valueL;
        *outBufferAdd++ += valueR;
        m_valuePeak = std::max(m_valuePeak, std::max(std::abs(valueL), std::abs(valueR)));
        m_valueRMS += 0.5f * (valueL * valueL + valueR * valueR);
        localCursor = localCursorOffset + (++isample) * freqRatio;
      }
      m_cursor += (localCursor - localCursorOffset);

      if (m_repet)
      {
        if (m_cursor >= dataSampleCount) m_cursor -= dataSampleCount;
      }
    }

    m_valueRMS = std::sqrt(m_valueRMS / sampleCount);
  }
};

} // namespace "soundSampler"

// ============================================================================

/**
 * Class "sound" is an implementation of sound sampling,
 * using audio data from the soundData namespace,
 * and using the soundSampler namespace,
 * and that inherits from soundInterface.
 */
template <class _controls>
class sound : public soundInterface
{
public:
  sound() {}
  virtual ~sound() {}

  // audio-data
public:
  void setAudioData(const soundData::s_RawSDL *audioD) { m_audioDataRaw = audioD; } ///< warning: unsafe. Don't use it while the sound is playing in the audioContext.
  void setAudioData(const soundData::s_Opus *audioD) { m_audioDataOpus = audioD; } ///< warning: unsafe. Don't use it while the sound is playing in the audioContext.
  const soundData::s_RawSDL *audioDataRaw() const { return m_audioDataRaw; }
  const soundData::s_Opus   *audioDataOpus() const { return m_audioDataOpus; }
protected:
  const soundData::s_RawSDL *m_audioDataRaw = nullptr;
  const soundData::s_Opus   *m_audioDataOpus = nullptr;

  // control
public:
  struct s_control
  {
    _controls m_target;
    float     m_targetDelay = -1.f;
    unsigned  m_cursor = unsigned(-1);
    bool      m_isPlaying = false;
    bool      m_isRepeating = false;

    void     setTarget(const _controls &s, float delay = 0.f) { m_target = s; m_targetDelay = delay; }
    void     setCursor(unsigned pos) { m_cursor = pos; }
  };
protected:
  s_control   m_control;
public:
  const s_control &control() const { return m_control; }
  s_control       &control()       { return m_control; }

  // feed-back
public:
  struct s_feedback
  {
    _controls m_playedControls; ///< last control values
    unsigned  m_playedSampleCursor = 0;
    unsigned  m_playedSampleCount = 0; ///< Sample count since the last 'sync'
    float     m_playedLevelPeak = 0.f;
    float     m_playedLevelRMS = 0.f;
  };
protected:
  s_feedback m_feedback;
public:
  const s_feedback &feedback() const { return m_feedback; }

  // safe data for the audio-callback. Resync them with 'sync' method.
protected:
  s_control                      ac_control;
  s_feedback                     ac_feedback;
  soundSampler::s_sampler_Raw    ac_samplerRaw; // sampler local data
  soundSampler::s_sampler_Opus   ac_samplerOpus; // sampler local data

  // interface
public:

  virtual void sync() override
  {
    // send
    ac_control.m_isPlaying = m_control.m_isPlaying;
    ac_control.m_isRepeating = m_control.m_isRepeating;
    ac_samplerRaw.m_repet = m_control.m_isRepeating;
    ac_samplerOpus.m_repet = m_control.m_isRepeating;

    if (m_control.m_targetDelay >= 0.f)
    {
      ac_control.m_target = m_control.m_target;
      ac_control.m_targetDelay = m_control.m_targetDelay;
      m_control.m_targetDelay = -1.f;
    }
    if (m_control.m_cursor != unsigned(-1))
    {
      ac_control.m_cursor = m_control.m_cursor;
      ac_samplerRaw.m_cursor = float(m_control.m_cursor);
      ac_samplerOpus.m_cursor = float(m_control.m_cursor);
      m_control.m_cursor = unsigned(-1);
    }

    // receive
    m_feedback = ac_feedback;

    // reset
    ac_feedback.m_playedSampleCount = 0;
    ac_feedback.m_playedLevelPeak = 0.f;
    ac_feedback.m_playedLevelRMS = 0.f;
  }

  /// sampling with float and stereo, called from the audio-callback
  virtual void sample(float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override
  {
    if (!ac_control.m_isPlaying) return;

    _controls endControls;
    if (ac_control.m_targetDelay >= 0.f)
    {
      const float dt = float(sampleCount) / float(sampleFreq);
      const float cursor = (dt != 0.f) ? std::min(1.f, dt / ac_control.m_targetDelay) : 1.f;
      endControls = _controls::mix(ac_feedback.m_playedControls, ac_control.m_target, cursor); // note: it won't behave as expected if the "mix" is not linear.
      ac_control.m_targetDelay -= dt;
    }
    else
    {
      endControls = ac_feedback.m_playedControls;
      ac_control.m_targetDelay = -1.f;
    }

#ifdef TRE_WITH_OPUS
    if (m_audioDataOpus != nullptr)
    {
      if (m_audioDataOpus->m_nSamples == 0) return; // no audio data ?!?

      ac_samplerOpus.sample(*m_audioDataOpus, ac_feedback.m_playedControls, endControls, outBufferAdd, sampleCount, sampleFreq);

      ac_feedback.m_playedControls = endControls;
      ac_feedback.m_playedLevelPeak = std::max(ac_feedback.m_playedLevelPeak, ac_samplerOpus.m_valuePeak);
      ac_feedback.m_playedLevelRMS  = std::sqrt((ac_feedback.m_playedLevelRMS * ac_feedback.m_playedLevelRMS * ac_feedback.m_playedSampleCount +
                                                 ac_samplerOpus.m_valueRMS    * ac_samplerOpus.m_valueRMS    * sampleCount                      ) /
                                                (ac_feedback.m_playedSampleCount + sampleCount));
      ac_feedback.m_playedSampleCount += sampleCount;
      ac_feedback.m_playedSampleCursor = unsigned(ac_samplerOpus.m_cursor);

      return;
    }
#endif

    if (m_audioDataRaw != nullptr)
    {
      if (m_audioDataRaw->m_nSamples == 0) return; // no audio data ?!?

      ac_samplerRaw.sample(*m_audioDataRaw, ac_feedback.m_playedControls, endControls, outBufferAdd, sampleCount, sampleFreq);

      ac_feedback.m_playedControls = endControls;
      ac_feedback.m_playedLevelPeak = std::max(ac_feedback.m_playedLevelPeak, ac_samplerRaw.m_valuePeak);
      ac_feedback.m_playedLevelRMS  = std::sqrt((ac_feedback.m_playedLevelRMS * ac_feedback.m_playedLevelRMS * ac_feedback.m_playedSampleCount +
                                                 ac_samplerRaw.m_valueRMS     * ac_samplerRaw.m_valueRMS     * sampleCount                      ) /
                                                (ac_feedback.m_playedSampleCount + sampleCount));
      ac_feedback.m_playedSampleCount += sampleCount;
      ac_feedback.m_playedSampleCursor = unsigned(ac_samplerRaw.m_cursor);

      return;
    }

    TRE_FATAL("audio::sample: unsupported audio data");
  }
};

// ============================================================================

typedef sound<soundSampler::s_noControl>     soundBasic;
typedef sound<soundSampler::s_stereoControl> sound2D;

// ============================================================================

} // namespace tre

#endif // AUDIO_H
