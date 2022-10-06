#ifndef AUDIO_H
#define AUDIO_H

#include <string>
#include <fstream>
#include <vector>
#include <math.h>

// foward decl of the SDL
struct SDL_AudioSpec;
typedef uint32_t SDL_AudioDeviceID;
typedef uint16_t SDL_AudioFormat;

namespace tre
{

// ============================================================================

/**
 * @brief The soundData class holds the sound data, and it supports the baking into binary file (using Opus compression if available)
 */
class soundData
{
public:
  soundData() {}
  virtual ~soundData() {}

  bool loadFromWAVfile(const std::string &wavFile);
  bool loadFromSDLAudio(unsigned samples, int freq, SDL_AudioFormat format, uint8_t channels, uint8_t *audioBuffer, const std::string &name = std::string());

  bool write(std::ostream &stream, unsigned bitrate = 0) const; ///< Write baked-file (with compression if bitrate != 0)
  bool read(std::istream &stream); ///< Load baked-file

  bool convertTo(int freq, SDL_AudioFormat format);

  // meta-data
  unsigned           size() const { return m_Nsamples; }
  int                freq() const { return m_freq; }
  SDL_AudioFormat    format() const { return m_format; }
  float              length() const { return float(m_Nsamples) / m_freq; } ///< returns duration in seconds
  unsigned           channels() const { return m_Nchannels; }
  const std::string  &getName() const { return m_name; }
  void               setName(const std::string &name) { m_name = name; }

  // data
   void              extractData_I32(unsigned channelIdx, unsigned sampleOffset, unsigned sampleCount, int * __restrict outData) const;
   void              extractData_Float(unsigned channelIdx, unsigned sampleOffset, unsigned sampleCount, float * __restrict outData) const;

protected:

  // meta-data
  unsigned        m_Nsamples = 0;
  int             m_freq = 0;
  SDL_AudioFormat m_format = 0;
  uint8_t         m_Nchannels = 0;
  std::string     m_name;

  // data
  typedef uint32_t audio_t;
  std::vector<audio_t> m_rawData; ///< audio data (re-interpreted as 'audio_t')

public:
  const std::vector<audio_t> &rawData() const { return m_rawData; }
};

// ============================================================================

/**
 * @brief The soundInterface class defines the interface (API) needed to be processed by the audio-context.
 */
class soundInterface
{
public:

  /**
   * @brief freq
   * @return the sampling-frequency (in Hz) of the sound
   */
  virtual int freq() = 0;

  /**
   * @brief synchronization point
   * Called outside of the sound-thread, to exchange safely data between data used in the audio-thread and other threads.
   */
  virtual void sync() = 0;

  /**
   * @brief raw sampling by the audio-thread
   * Called inside the sound-thread, when the audio-thread requires stereo-data (LR, LR, LR, ...)
   * @param outBufferAdd audio buffer (data must be 'added')
   * @param sampleCount (number of samples (1 sample is 2 floats, representing the left and right chanel data)
   */
  virtual void sample(float *outBufferAdd, unsigned sampleCount) = 0;
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

public:
  static void getDevicesName(std::vector<std::string> &devices); ///< Helper, that gets the list of detected devices

private:
  struct s_audioCallbackContext
  {
    std::vector<soundInterface*> ac_listSounds;
    std::vector<float>           ac_bufferF32;
    int                          ac_freq = 0;
    unsigned                     ac_channels = 0;

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

enum e_equalizerMode
{
  equalizer_None,
};
struct s_equalizerControl
{
  e_equalizerMode m_mode;
  float           m_freqRoot; ///< ]0, +inf[
  float           m_power;    ///< [0, +inf[
};

// ============================================================================

/**
 * Basic 2D-sampler of sound, to be used in the template parameter of the "sound" class.
 * It can sample mono or stereo audio, with a volume and panning modifiers.
 */
struct s_sound2Dsampler
{
  float m_volume = 0.f; ///< [0, +inf[
  float m_pan = 0.f;    ///< [-1, 1]

  static s_sound2Dsampler mix(const s_sound2Dsampler &infoStart, const s_sound2Dsampler &infoEnd, float cursor);

  static void sample(const soundData &sound, unsigned sampleStart, unsigned sampleCount,
                     const s_sound2Dsampler &infoStart, const s_sound2Dsampler &infoEnd,
                     float * __restrict outBufferAdd, float &outValuePeak, float &outValueRMS);
};

// ============================================================================

/**
* Basic 3D-sampler of sound, to be used in the template parameter of the "sound" class.
* TODO ...
*/
struct s_sound3Dsampler
{
  // TODO ...

  static s_sound3Dsampler mix(const s_sound3Dsampler &infoStart, const s_sound3Dsampler &infoEnd, float cursor);

  static void sample(const soundData &sound, unsigned sampleStart, unsigned sampleCount,
                     const s_sound3Dsampler &infoStart, const s_sound3Dsampler &infoEnd,
                     float * __restrict outBufferAdd, float &outValuePeak, float &outValueRMS);
};

// ============================================================================

template <class _sampler>
class sound : public soundInterface
{
public:
  sound(const soundData *audioData = nullptr) : m_audioData(audioData) { ac_control.m_cursor = 0; }
  virtual ~sound() {}

  // audio-data
public:
  void setAudioData(const soundData *audioD) { m_audioData = audioD; } // warning: unsafe. Don't use it while the sound is playing in the audioContext.
  const soundData *audioData() const { return m_audioData; }
protected:
  const soundData *m_audioData;

  // control
public:
  struct s_control
  {
    _sampler  m_target;
    float     m_targetDelay = -1.f;
    unsigned  m_cursor = unsigned(-1);
    bool      m_isPlaying = false;
    bool      m_isRepeating = false;

    void     setTarget(const _sampler &s, float delay = 0.f) { m_target = s; m_targetDelay = delay; }
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
    _sampler  m_playedSampler;
    unsigned  m_playedSampleCursor = 0; ///< Warning: the precision is limited by the audio-thread frequency.
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
  mutable s_control  ac_control;
  mutable s_feedback ac_feedback;

  // interface
public:

  virtual int freq() override
  {
    return (m_audioData != nullptr) ? m_audioData->freq() : 0;
  }

  virtual void sync() override
  {
    // send
    ac_control.m_isPlaying = m_control.m_isPlaying;
    ac_control.m_isRepeating = m_control.m_isRepeating;

    if (m_control.m_targetDelay >= 0.f)
    {
      ac_control.m_target = m_control.m_target;
      ac_control.m_targetDelay = m_control.m_targetDelay;
      m_control.m_targetDelay = -1.f;
    }
    if (m_control.m_cursor != unsigned(-1))
    {
      ac_control.m_cursor = m_control.m_cursor;
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
  virtual void sample(float *outBufferAdd, unsigned sampleCount) override
  {
    if (m_audioData == nullptr) return;

    const unsigned audioSamples = m_audioData->size();
    const int      audioFreq = m_audioData->freq();

    if (!ac_control.m_isPlaying) return;
    if (ac_control.m_isRepeating) ac_control.m_cursor = ac_control.m_cursor % audioSamples;

    unsigned       sampleStart = ac_control.m_cursor;
    const unsigned sampleCountSave = sampleCount;

    if (sampleStart >= audioSamples && !ac_control.m_isRepeating) return;

    float levelPeak = 0.f, levelRMS = 0.f;

    for (unsigned iPass = 0; iPass < 3; ++iPass)
    {
      const bool     isTransitionActive = (ac_control.m_targetDelay != 0.f);
      const unsigned sampleCountBeforeTargetDelay = ac_control.m_targetDelay * audioFreq;
      const unsigned sampleCountCut = isTransitionActive ? sampleCountBeforeTargetDelay : sampleCount;

      const unsigned sampleStartPass = sampleStart % audioSamples;
      const unsigned sampleCountPass = std::min(sampleCount, std::min(audioSamples - sampleStartPass, sampleCountCut));

      if (isTransitionActive)
      {
        _sampler tmpSampler = _sampler::mix(ac_feedback.m_playedSampler, ac_control.m_target, float(sampleCountPass)/float(sampleCountBeforeTargetDelay));

        _sampler::sample(*m_audioData, sampleStartPass, sampleCountPass,
                         ac_feedback.m_playedSampler, tmpSampler,
                         outBufferAdd, levelPeak, levelRMS);

        ac_feedback.m_playedSampler = tmpSampler;

        if (ac_control.m_targetDelay != 0.f)
          ac_control.m_targetDelay = std::max(0.f, float(sampleCountBeforeTargetDelay - sampleCountPass)/audioFreq);
      }
      else
      {
        ac_feedback.m_playedSampler = ac_control.m_target;
        _sampler::sample(*m_audioData, sampleStartPass, sampleCountPass,
                         ac_control.m_target, ac_control.m_target,
                         outBufferAdd, levelPeak, levelRMS);
      }

      sampleCount -= sampleCountPass;
      sampleStart += sampleCountPass;
      outBufferAdd += sampleCountPass * 2;
      if (sampleCount == 0 || (sampleStart >= audioSamples && !ac_control.m_isRepeating)) break;
    }

    ac_feedback.m_playedSampleCursor = ac_control.m_cursor = sampleStart;
    ac_feedback.m_playedLevelPeak = std::max(ac_feedback.m_playedLevelPeak, levelPeak);
    ac_feedback.m_playedLevelRMS  = std::sqrt((ac_feedback.m_playedLevelRMS * ac_feedback.m_playedLevelRMS * ac_feedback.m_playedSampleCount + levelRMS) / (ac_feedback.m_playedSampleCount + sampleCountSave));
    ac_feedback.m_playedSampleCount += sampleCountSave;
  }
};

// ============================================================================

typedef sound<s_sound2Dsampler> sound2D;
typedef sound<s_sound3Dsampler> sound3D;

// ============================================================================

} // namespace tre

#endif // AUDIO_H
