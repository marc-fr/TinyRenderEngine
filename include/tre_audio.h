#pragma once

#include "tre_utils.h"

#include <string>
#include <fstream>
#include <vector>

// forward decl of the SDL
struct SDL_AudioSpec;
typedef uint32_t SDL_AudioDeviceID;
typedef uint16_t SDL_AudioFormat;

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
  bool            m_stereo = false; ///< mono (1 channel) / stereo (2 channels)

  std::vector<int16_t> m_rawData; ///< audio data (signed 16bit int)

  bool loadFromWAV(const std::string & filename); ///< Load from a WAV file
  bool loadFromWAV(const std::vector<uint8_t> & fileBuffer); ///< Load from a WAV file-buffer
  bool loadFromMP3(const std::vector<uint8_t> & fileBuffer); ///< Load from a MP3 file-buffer
  bool loadFromSDLAudio(int freq, SDL_AudioFormat format, bool stereo, uint8_t *audioBuffer, unsigned audioBufferByteSize); ///< Load from SDL audio data (it performs a deep copy)

  bool write(std::ostream &stream) const; ///< Write baked-file
  bool read(std::istream &stream); ///< Load baked-file

  bool convertTo(int freq);
};

/**
 * @brief The s_Opus struct
 * format is 16-bit signed integers.
 */
struct s_Opus
{
  static const int m_freq = 48000;
  unsigned        m_nSamples = 0;
  bool            m_stereo = false; ///< mono (1 channel) / stereo (2 channels)

  struct s_block
  {
    int                  m_sampleStart; ///< sample-index of the first encoded sample. Can be negative on the first block with the codec pre-skip.
    std::vector<uint8_t> m_data; ///< encoded samples.
  };
  std::vector<s_block> m_blocks;

  bool loadFromOPUS(const std::string & filename); ///< Load from an OPUS file.
  bool loadFromRaw(s_RawSDL &rawData, unsigned bitrate); ///< Load and compress raw-data with Opus encoder, and store compressed data. The "rawData" might be converted to another format.

  bool write(std::ostream &stream) const; ///< Write baked-file
  bool read(std::istream &stream); ///< Load baked-file

  unsigned getBlockAtSampleId(int sid) const; ///< Returns the index of the block containing sample sid, or unsigned(-1) if no block contains it
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
   * @param sampleCount number of samples (1 sample is 2 floats, representing the left and right channel data)
   * @param sampleFreq frequency required for the out buffer
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

  // feedback: levels
public:
  struct s_levels
  {
    float    m_peak = 0.f;      ///< max |value| over L and R. Above 1 means the mix is clipping.
    float    m_RMS = 0.f;       ///< RMS over L and R.
    unsigned m_sampleCount = 0; ///< nbr of samples measured. 0 if the audio-callback did not run since the previous update.
  };
  const s_levels &getLevels() const { return m_levels; }
protected:
  s_levels  m_levels;

protected:
  SDL_AudioDeviceID m_deviceID = 0;
  SDL_AudioSpec     *m_audioSpec = nullptr;

  std::vector<soundInterface*> m_listSounds;

#ifdef TRE_PROFILE
protected:
  unsigned m_perf_nbrCall = 0;
  float    m_perf_elapsedTime = 0.f;
  float    m_perf_accElapsedTime = 0.f;
  float    m_perf_accGenTime = 0.f;
public:
  unsigned getPerf_nbrCall() const { return m_perf_nbrCall; } ///< Nbr of calls of the audio callback, since the last sync.
  float    getPerf_total() const { return m_perf_elapsedTime; } ///< Elapsed time spent in the audio callback, since the last sync. (in seconds)
  float    getPerf_load() const { return m_perf_accElapsedTime / std::max(m_perf_accGenTime, 1.e-6f); } ///< Compute load of the audio thread. = ElapsedTime / SoundTimeGenerated
#else
public:
  unsigned getPerf_nbrCall() const { return 0; }
  float    getPerf_total() const { return 0.f; }
  float    getPerf_load() const { return 0.f; }
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
    unsigned                     ac_perf_nbrCall = 0;
    float                        ac_perf_elapsedTime = 0.f;
    float                        ac_perf_genTime = 0.f;
#endif
    audioContext::s_levels       ac_levels; // Here, "RMS" is not resolved but contains the sum of the squared values.

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
 * Class "sound2D" plays audio data from the soundData (raw or Opus),
 * with a stereo control (volume, pan) that is smoothly interpolated.
 * It implements soundInterface.
 */
class sound2D : public soundInterface
{
public:
  sound2D() {}
  ~sound2D() override;

  sound2D(const sound2D &) = delete;
  sound2D &operator=(const sound2D &) = delete;

  /// Stereo control applied on the sampled data.
  struct s_stereoControl
  {
    s_stereoControl(const float vol = 1.f, const float pan = 0.f) : m_volume(vol), m_pan(pan) {}
    float        m_volume; ///< [0, +inf[
    float        m_pan;    ///< [-1, 1]
  };

  // audio-data
public:
  void setAudioData(const soundData::s_RawSDL *audioD); ///< warning: unsafe. Don't use it while the sound is playing in the audioContext.
  void setAudioData(const soundData::s_Opus *audioD); ///< warning: unsafe. Don't use it while the sound is playing in the audioContext.
  const soundData::s_RawSDL *audioDataRaw() const { return m_audioDataRaw; }
  const soundData::s_Opus   *audioDataOpus() const { return m_audioDataOpus; }
protected:
  const soundData::s_RawSDL *m_audioDataRaw = nullptr;
  const soundData::s_Opus   *m_audioDataOpus = nullptr;

  // control
public:
  struct s_control
  {
    s_stereoControl m_target;
    float           m_targetDelay = -1.f;
    unsigned        m_cursor = unsigned(-1);
    bool            m_isPlaying = false;
    bool            m_isRepeating = false;

    void     setTarget(const s_stereoControl &s, float delay = 0.f) { m_target = s; m_targetDelay = delay; }
    void     setCursor(unsigned pos) { m_cursor = pos; }
  };
  const s_control &control() const { return m_control; }
  s_control       &control()       { return m_control; }
  unsigned        playedCursor() const { return unsigned(ac_phase >> 32); } // get the current played cursor. unsafe.
protected:
  s_control   m_control;

  // interface
public:
  void sync() override;
  void sample(float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq) override; ///< sampling with float and stereo, called from the audio-callback

  // safe data for the audio-callback. Resync them with 'sync' method.
protected:
  s_control       ac_control;
  s_stereoControl ac_mixPrev;
  uint64_t        ac_phase = 0; ///< play position: packed (sample-id 32bits) and (interpolation-cursor 32bits)

  struct s_opusState; ///< Opus decoder and decoded block (defined in the cpp)
  s_opusState *ac_opusState = nullptr;

private:
  void _sampleRaw(const soundData::s_RawSDL &data, const s_stereoControl &controlsStart, const s_stereoControl &controlsEnd,
                  float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq);
  void _sampleOpus(const soundData::s_Opus &data, const s_stereoControl &controlsStart, const s_stereoControl &controlsEnd,
                   float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq);
  bool _decodeOpusBlock(const soundData::s_Opus &data, unsigned slot);
};

// ============================================================================

} // namespace tre
