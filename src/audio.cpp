#include "tre_audio.h"

#include "tre_utils.h"

#ifdef TRE_WITH_OPUS
#include "opus.h"
#endif

namespace tre
{

// soundBase ==================================================================

static std::string _basename(const std::string &filePath)
{
  const std::size_t posSepL = filePath.rfind('/');
  const std::size_t posSepW = filePath.rfind('\\');
  const std::size_t posSep = (posSepL != std::string::npos) ? posSepL : posSepW;
  const std::size_t posStart = (posSep == std::string::npos) ? 0 : posSep + 1;
  const std::size_t posDot = filePath.rfind('.');
  const std::size_t posEnd = (posDot == std::string::npos || posDot < posStart) ? filePath.length() : posDot;

  return filePath.substr(posStart, posEnd - posStart);
}

// ----------------------------------------------------------------------------

bool soundData::loadFromWAVfile(const std::string & wavFile)
{
  // Load the WAV
  SDL_AudioSpec wavSpec; // "format", "freq" and "channels" are filled
  Uint32 wavLength = 0;
  Uint8 *wavBuffer = nullptr;
  if(SDL_LoadWAV(wavFile.c_str(), &wavSpec, &wavBuffer, &wavLength) == nullptr)
  {
    TRE_LOG("fail to audio file: " << SDL_GetError());
    return false;
  }

  // meta-data
  m_freq = wavSpec.freq;
  m_format = wavSpec.format;
  m_Nchannels = wavSpec.channels;
  m_name = _basename(wavFile);

  m_Nsamples = wavLength / (SDL_AUDIO_BITSIZE(m_format) / 8) / m_Nchannels;
  TRE_ASSERT(wavLength == SDL_AUDIO_BITSIZE(m_format) / 8 * m_Nchannels * m_Nsamples);

  // data
  TRE_ASSERT(sizeof(audio_t) == 4);
  m_rawData.resize((wavLength + 3) / 4, 0);

  memcpy(m_rawData.data(), wavBuffer, wavLength);

  SDL_FreeWAV(wavBuffer);

  TRE_LOG("Audio loaded: samples = " << m_Nsamples << ", buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

bool soundData::loadFromSDLAudio(unsigned samples, int freq, SDL_AudioFormat format, uint8_t channels, uint8_t *audioBuffer, const std::string &name /*= std::string()*/)
{
  // meta-data
  m_Nsamples = samples;
  m_freq = freq;
  m_format = format;
  m_Nchannels = channels;
  m_name = name;

  const unsigned wavLength = SDL_AUDIO_BITSIZE(m_format) / 8 * m_Nchannels * m_Nsamples;

  // data
  TRE_ASSERT(sizeof(audio_t) == 4);
  m_rawData.resize((wavLength + 3) / 4, 0);
  memcpy(m_rawData.data(), audioBuffer, wavLength);

  TRE_LOG("Audio loaded: samples = " << m_Nsamples << ", buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

#define SOUND_BIN_VERSION 0x002

bool soundData::write(std::ostream &stream, unsigned bitrate /* = 0 */) const
{
#ifndef TRE_WITH_OPUS
  bitrate = 0;
#endif
  if (m_Nchannels > 2) bitrate = 0;
  if (bitrate != 0)
  {
    if (m_freq != 48000 || m_format != AUDIO_S16) // supported freq by OPUS
    {
      soundData tmpSound = *this;
      if (tmpSound.convertTo(48000, AUDIO_S16))
        return tmpSound.write(stream, bitrate);
      else
        bitrate = 0;
    }
  }
  TRE_ASSERT(bitrate == 0 || (m_freq == 48000 && m_format == AUDIO_S16));

#ifdef TRE_PRINTS
  const std::ios::pos_type streamStart = stream.tellp();
#endif

  // header
  uint header[8];
  header[0] = SOUND_BIN_VERSION;
  header[1] = m_Nsamples;
  header[2] = m_Nchannels;
  header[3] = 1;
  header[4] = (bitrate != 0) ? 1 : 0;
  header[5] = uint(m_freq);
  header[6] = uint(m_format);
  TRE_ASSERT(sizeof(m_format) <= sizeof(uint));

  header[7] = m_name.size();

  stream.write(reinterpret_cast<const char*>(header), sizeof(header));

  // name
  if (m_name.size() > 0) stream.write(m_name.data(), m_name.size());

  // encoding setup
#ifdef TRE_WITH_OPUS
  OpusEncoder *audioEncoder = nullptr;
  if (bitrate != 0)
  {
    int errorCode = 0;
    audioEncoder = opus_encoder_create(48000, m_Nchannels, OPUS_APPLICATION_AUDIO, &errorCode);
    TRE_ASSERT(audioEncoder != nullptr && errorCode == 0);
    opus_encoder_ctl(audioEncoder, OPUS_SET_BITRATE(bitrate));
  }
#endif

  // audio data

#ifdef TRE_WITH_OPUS
  if (bitrate != 0)
  {
    // encode
    const int bufferEncodeByteSize = 2880 * sizeof(int16_t) * m_Nchannels;

    std::vector<uint8_t> bufferEncode;
    bufferEncode.resize(bufferEncodeByteSize);

    opus_encoder_ctl(audioEncoder, OPUS_RESET_STATE);

    { // Hack: prevent to read garbage when encoding the last chunk.
      const unsigned rawData_AlignedByteSize = ((m_Nsamples + 2880 - 1) / 2880) * 2880 * sizeof(int16_t) * m_Nchannels;
      const_cast<soundData*>(this)->m_rawData.resize((rawData_AlignedByteSize + 3) / 4, 0.f);
    }

    const int16_t *audioBufferSrc = reinterpret_cast<const int16_t*>(m_rawData.data());

    uint encodedSamples = 0;
    while (encodedSamples < m_Nsamples)
    {
      const int bufferWriteSize = opus_encode(audioEncoder, audioBufferSrc, 2880, bufferEncode.data(), bufferEncodeByteSize);
      TRE_ASSERT(bufferWriteSize > 0);
      stream.write(reinterpret_cast<const char*>(&bufferWriteSize), sizeof(int));
      stream.write(reinterpret_cast<const char*>(bufferEncode.data()), bufferWriteSize);
      encodedSamples += 2880;
      audioBufferSrc += 2880 * m_Nchannels;
    }
  }
#endif
  if (bitrate == 0)
  {
    const int bufferSize = (SDL_AUDIO_BITSIZE(m_format) / 8) * m_Nchannels * m_Nsamples;
    stream.write(reinterpret_cast<const char*>(&bufferSize), sizeof(int));
    stream.write(reinterpret_cast<const char*>(m_rawData.data()), bufferSize);
  }

#ifdef TRE_PRINTS
  const std::ios::pos_type streamEnd = stream.tellp();
  TRE_LOG("Write audio (Samples=" << m_Nsamples << ", Compression=" << (header[4]==1 ? "Opus" : "None") <<
          ", Write-size=" << (streamEnd-streamStart)/8/1024 << " ko)");
#endif

#ifdef TRE_WITH_OPUS
  if (audioEncoder != nullptr) opus_encoder_destroy(audioEncoder);
#endif

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::read(std::istream &stream)
{
  // header
  uint header[8];
  stream.read(reinterpret_cast<char*>(header), sizeof(header));
  TRE_ASSERT(header[0] == SOUND_BIN_VERSION);

  m_Nsamples  = header[1];
  m_Nchannels = uint8_t(header[2]);
  m_freq      = int(header[5]);
  m_format    = SDL_AudioFormat(header[6]);
  m_name.resize(header[7]);

  // name
  if (m_name.size() > 0)
    stream.read(const_cast<char*>(m_name.data()), m_name.size());

#ifdef TRE_WITH_OPUS
  OpusDecoder  *audioDecoder = nullptr;
  if (header[4] == 1)
  {
    int errorCode = 0;
    audioDecoder = opus_decoder_create(48000, m_Nchannels, &errorCode);
    TRE_ASSERT(audioDecoder != nullptr && errorCode == 0);
  }
#else
  if (header[4] == 1) return false;
#endif

  // audio data

  const uint bytesPerSample = (SDL_AUDIO_BITSIZE(m_format) / 8) * m_Nchannels;
  m_rawData.resize((m_Nsamples + 2880 * m_Nchannels) * bytesPerSample / 4 + 1);

#ifdef TRE_WITH_OPUS
  if (header[4] == 1)
  {
    std::vector<uint8_t> bufferDecode;

    opus_decoder_ctl(audioDecoder, OPUS_RESET_STATE);

    int16_t *audioBufferDst = reinterpret_cast<int16_t*>(m_rawData.data());

    uint decodedSamples = 0;
    while (decodedSamples < m_Nsamples)
    {
      int bufferDecodeByteSize = 0;
      stream.read(reinterpret_cast<char*>(&bufferDecodeByteSize), sizeof(int));
      bufferDecode.resize(bufferDecodeByteSize);
      stream.read(reinterpret_cast<char*>(bufferDecode.data()), bufferDecodeByteSize);

      const int ret = opus_decode(audioDecoder, bufferDecode.data(), bufferDecodeByteSize, audioBufferDst, 2880, 0);
      TRE_ASSERT(ret == 2880);
      decodedSamples += 2880;
      audioBufferDst += 2880 * m_Nchannels;
    }
  }
#endif
  if (header[4] == 0)
  {
    int Ndata = 0;
    stream.read(reinterpret_cast<char*>(&Ndata), sizeof(int));
    TRE_ASSERT(uint(Ndata) == m_Nsamples * bytesPerSample);

    stream.read(reinterpret_cast<char*>(m_rawData.data()), Ndata);
  }

  TRE_LOG("Read audio (Samples=" << m_Nsamples << ", Total-Buffer-size=" << m_Nchannels * m_Nsamples * 2 / 1024 << " ko)");

#ifdef TRE_WITH_OPUS
  if (audioDecoder != nullptr) opus_decoder_destroy(audioDecoder);
#endif

  return true;
}

#undef SOUND_BIN_VERSION

// ----------------------------------------------------------------------------

bool soundData::convertTo(int freq, SDL_AudioFormat format)
{
  if (freq == m_freq && format == m_format)
    return true;

  SDL_AudioCVT wavConvertor;
  const int wanConvertorStatus = SDL_BuildAudioCVT(&wavConvertor,
                                                   m_format, m_Nchannels, m_freq,
                                                   format, m_Nchannels, freq);
  if (wanConvertorStatus <= 0)
  {
    TRE_LOG("fail to create the audio convertor: " << SDL_GetError());
    return false;
  }

  const unsigned rawData_ByteSize = (SDL_AUDIO_BITSIZE(m_format) / 8) * m_Nchannels * m_Nsamples;
  TRE_ASSERT(rawData_ByteSize <= m_rawData.size() * sizeof(audio_t));

  TRE_ASSERT(sizeof(audio_t) == 4);
  const unsigned rawData_NeededSize = (rawData_ByteSize * wavConvertor.len_mult + 3) / 4;
  m_rawData.resize(rawData_NeededSize);

  wavConvertor.buf = reinterpret_cast<uint8_t*>(m_rawData.data());
  wavConvertor.len = rawData_ByteSize;

  SDL_ConvertAudio(&wavConvertor);

  m_freq = freq;
  m_format = format;
  m_Nsamples = wavConvertor.len_cvt / m_Nchannels / (SDL_AUDIO_BITSIZE(m_format) / 8);
  TRE_ASSERT(unsigned(wavConvertor.len_cvt) == (SDL_AUDIO_BITSIZE(m_format) / 8) * m_Nchannels * m_Nsamples);

  m_rawData.resize((wavConvertor.len_cvt + 3) / 4);

  TRE_LOG("Audio converted: samples = " << m_Nsamples << ", buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

void soundData::extractData_I32(unsigned channelIdx, unsigned sampleOffset, unsigned sampleCount, int * __restrict outData) const
{
  if (channelIdx >= m_Nchannels || sampleOffset >= m_Nsamples)
  {
    memset(outData, 0, sampleCount);
    return;
  }

  TRE_ASSERT(m_format == AUDIO_S16); // TODO: support other formats
  const int16_t * __restrict inBuffer = reinterpret_cast<const int16_t*>(rawData().data());

  for (; sampleOffset < m_Nsamples && sampleCount != 0; ++sampleOffset)
  {
    *outData = int(inBuffer[sampleOffset * m_Nchannels + channelIdx]);
    ++outData;
    --sampleCount;
  }

  if (sampleCount != 0) memset(outData, 0, sampleCount);
}

// ----------------------------------------------------------------------------

void soundData::extractData_Float(unsigned channelIdx, unsigned sampleOffset, unsigned sampleCount, float * __restrict outData) const
{
  if (channelIdx >= m_Nchannels || sampleOffset >= m_Nsamples)
  {
    memset(outData, 0, sampleCount);
    return;
  }

  TRE_ASSERT(m_format == AUDIO_S16); // TODO: support other formats
  const int16_t * __restrict inBuffer = reinterpret_cast<const int16_t*>(rawData().data());

  for (; sampleOffset < m_Nsamples && sampleCount != 0; ++sampleOffset)
  {
    *outData = float(inBuffer[sampleOffset * m_Nchannels + channelIdx]) / float(0xFFFF);
    ++outData;
    --sampleCount;
  }

  if (sampleCount != 0) memset(outData, 0, sampleCount);
}

// s_sound2Dsampler ===========================================================

s_sound2Dsampler s_sound2Dsampler::mix(const s_sound2Dsampler &infoStart, const s_sound2Dsampler &infoEnd, float cursor)
{
  s_sound2Dsampler ret;
  const float cursor2 = 1.f - cursor;
  ret.m_volume = infoStart.m_volume * cursor2 + infoEnd.m_volume * cursor;
  ret.m_pan    = infoStart.m_pan * cursor2 + infoEnd.m_pan * cursor;
  return ret;
}

// ----------------------------------------------------------------------------

void s_sound2Dsampler::sample(const soundData &sound, unsigned sampleStart, unsigned sampleCount,
                              const s_sound2Dsampler &infoStart, const s_sound2Dsampler &infoEnd,
                              float * __restrict outBufferAdd, float &outValuePeak, float &outValueRMS)
{
  TRE_ASSERT(sampleStart <= sound.size());
  TRE_ASSERT(sampleStart + sampleCount <= sound.size());

  TRE_ASSERT(sound.format() == AUDIO_S16);
  TRE_ASSERT(sound.channels() <= 2);

  const uint                 chan = sound.channels();
  const int16_t * __restrict inBuffer = reinterpret_cast<const int16_t*>(sound.rawData().data());
  inBuffer += chan * sampleStart;

  outValuePeak = 0.f;
  outValueRMS = 0.f;

  const float dvol = (infoEnd.m_volume - infoStart.m_volume) / (sampleCount - 1);
  const float dpan = (infoEnd.m_pan    - infoStart.m_pan   ) / (sampleCount - 1);

  if (chan == 1)
  {
    for (uint i = 0; i < sampleCount; ++i)
    {
      const float vol = infoStart.m_volume + dvol * i;
      const float v = (float(*inBuffer++) / 0x7FFF) * vol;
      const float pan = infoStart.m_pan + dpan * i;
      const float pL = 1.f - std::max(pan, 0.f);
      const float pR = 1.f + std::min(pan, 0.f);
      *outBufferAdd++ += v * pL; // L
      *outBufferAdd++ += v * pR; // R
      outValuePeak = glm::max(outValuePeak, fabsf(v));
      outValueRMS += v*v;
    }
  }
  else // (chan == 2)
  {
    for (uint i = 0; i < sampleCount; ++i)
    {
      const float vol = infoStart.m_volume + dvol * i;
      const float vL = (float(*inBuffer++) / 0x7FFF) * vol;
      const float vR = (float(*inBuffer++) / 0x7FFF) * vol;
      const float pan = infoStart.m_pan + dpan * i;
      const float pL = 1.f - std::max(pan, 0.f);
      const float pR = 1.f + std::min(pan, 0.f);
      *outBufferAdd++ += vL * pL; // L
      *outBufferAdd++ += vR * pR; // R
      outValuePeak = glm::max(outValuePeak, glm::max(fabsf(vL), fabsf(vR)));
      outValueRMS += pL*vL*vL + pR*vR*vR;
    }
  }
}

// s_sound3Dsampler ===========================================================

// TODO ...

// audio-Context methods ======================================================

bool audioContext::startSystem(const char *deviceName, uint bufferMS, SDL_AudioSpec *requiredAudioSpec)
{
  TRE_ASSERT(m_deviceID == 0 && m_audioSpec == nullptr);

  // Open the audio device
  SDL_AudioSpec audioSettingsWanted;

  if (requiredAudioSpec == nullptr)
  {
    // Default audio settings
    audioSettingsWanted.freq = 44100; // samples per seconds
    audioSettingsWanted.channels = 2; // stereo
    audioSettingsWanted.format = AUDIO_S16LSB; // 16-bits signed (little-endian)
    static_assert(SDL_BYTEORDER == SDL_LIL_ENDIAN, "Only implemented with little-endian system.");
  }
  else
  {
    audioSettingsWanted = *requiredAudioSpec;
  }

  audioSettingsWanted.callback = audio_callback;
  audioSettingsWanted.userdata = & m_audioCallbackContext;

  uint samples = (audioSettingsWanted.freq * bufferMS) / 1000;
  // -> power of 2
  {
    int count = 0;
    while ((samples = samples >> 1) != 0) ++count;
    samples = 0x1 << count;
  }
  if (samples <  512) samples = 512;
  if (samples > 8096) samples = 8096;

  audioSettingsWanted.samples = uint16_t(samples);

  m_audioSpec = new SDL_AudioSpec;

  m_deviceID = SDL_OpenAudioDevice(deviceName, 0/*no capture*/, &audioSettingsWanted, m_audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (m_deviceID < 2)
  {
    TRE_LOG("Couldn't open audio device: " << SDL_GetError());
    delete m_audioSpec; m_audioSpec = nullptr;
    return false;
  }

  TRE_LOG("Audio device " << (deviceName == nullptr ? "(default)" : deviceName) << " is opened.\n(" <<
          "freq = " << m_audioSpec->freq / 1000.f << " kHz, " <<
          "hardware-buffer = " << m_audioSpec->samples * 1000 / m_audioSpec->freq << " ms, " <<
          "channels = " << uint(m_audioSpec->channels) << ", "
          "bytesPerSample = " << SDL_AUDIO_BITSIZE(m_audioSpec->format) / 8 << " )");

  if (m_audioSpec->channels != 2 || m_audioSpec->format != AUDIO_S16LSB)
  {
    TRE_LOG("Invalid audio specifications.");
    stopSystem();
    return false;
  }
  if (m_audioSpec->size != 2 * sizeof(int16_t) * m_audioSpec->samples)
  {
    TRE_LOG("Unexpected audio buffer size : " << m_audioSpec->size << " != " << 2 * sizeof(int16_t) * m_audioSpec->samples);
    stopSystem();
    return false;
  }

  // Setup the callback context
  m_audioCallbackContext.ac_bufferF32.resize(m_audioSpec->samples * m_audioSpec->channels);
  m_audioCallbackContext.ac_freq = m_audioSpec->freq;
  m_audioCallbackContext.ac_channels = 2;

  // Start playing
  SDL_PauseAudioDevice(m_deviceID, 0);

  return true;
}

// ----------------------------------------------------------------------------

void audioContext::updateSystem()
{
  TRE_ASSERT(m_deviceID >= 2 && m_audioSpec != nullptr);

  SDL_LockAudioDevice(m_deviceID);

  m_audioCallbackContext.ac_listSounds = m_listSounds;

  for (soundInterface *sb : m_listSounds)
  {
    sb->sync();
  }

  SDL_UnlockAudioDevice(m_deviceID);
}

// ----------------------------------------------------------------------------

void audioContext::stopSystem()
{
  if (m_deviceID >= 2)
  {
    SDL_CloseAudioDevice(m_deviceID);
    m_deviceID = 0;
  }

  if (m_audioSpec != nullptr)
  {
    delete m_audioSpec;
    m_audioSpec = nullptr;
  }

  m_audioCallbackContext.ac_freq = 0;
  m_audioCallbackContext.ac_channels = 0;
  m_audioCallbackContext.ac_bufferF32.clear();
  m_audioCallbackContext.ac_listSounds.clear();
}

// ----------------------------------------------------------------------------

void audioContext::addSound(soundInterface *sound)
{
  TRE_ASSERT(sound->freq() == m_audioCallbackContext.ac_freq); // TODO: remove this. The implemented sampler only supports the same freq.

  for (soundInterface *sBis : m_listSounds)
  {
    if (sBis == sound) return; // already added !
  }

  sound->sync(); // sync data.

  m_listSounds.push_back(sound);
}

// ----------------------------------------------------------------------------

void audioContext::removeSound(soundInterface *sound)
{
  for (soundInterface* &sBis : m_listSounds)
  {
    if (sBis == sound)
    {
      sBis = m_listSounds.back(); // replace the pointer.
      m_listSounds.pop_back();
      return;
    }
  }
}

// ----------------------------------------------------------------------------

void audioContext::getDevicesName(std::vector<std::string> &devices)
{
  devices.clear();

  const uint nDevices = uint(SDL_GetNumAudioDevices(0 /*capture*/));
  devices.resize(nDevices);

  for (uint i = 0; i < nDevices; ++i)
    devices[i] = SDL_GetAudioDeviceName(int(i), 0 /*capture*/);
}

// ----------------------------------------------------------------------------

static void _copystream_F32LR_I16LR(const float * __restrict instream, int16_t * __restrict outstream, uint nsamples)
{
  const int16_t *outstop = outstream + 2 * nsamples;
  while (outstream < outstop)
  {
    const float vF32 = glm::clamp(*instream++, -1.f, 1.f);
    *outstream++ = int16_t(vF32 * 0x7FFF);
  }
}

void audioContext::s_audioCallbackContext::run(uint8_t * stream, int len)
{
  TRE_ASSERT(ac_channels == 2);

  const uint sampleCount = uint(len) / sizeof(int16_t) / ac_channels;

  memset(ac_bufferF32.data(), 0, sizeof(float) * ac_bufferF32.size());

  // mix sounds with floats

  for (soundInterface *bs : ac_listSounds)
    bs->sample(ac_bufferF32.data(), sampleCount);

  // convert to 16-bit signed-integers and fill dst buffer.

  int16_t * __restrict  outPtr = reinterpret_cast<int16_t*>(stream);
  _copystream_F32LR_I16LR(ac_bufferF32.data(), outPtr, sampleCount);
}

// ============================================================================

} // namespace tre
