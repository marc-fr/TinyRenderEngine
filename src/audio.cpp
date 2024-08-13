#include "tre_audio.h"

#include "tre_utils.h"

#ifdef TRE_PROFILE
#include <chrono>
#endif

#ifdef TRE_WITH_OPUS
#include "opus.h"
#endif

#define SOUND_BIN_VERSION 0x003

namespace tre
{

// soundData::s_RawSDL ========================================================

bool soundData::s_RawSDL::loadFromWAV(const std::string & wavFile)
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

  if (wavSpec.channels == 1)      m_stereo = false;
  else if (wavSpec.channels == 2) m_stereo = true;
  else
  {
    TRE_LOG("unsupported nbr of channels in audio file: " << SDL_GetError());
    return false;
  }

  m_nSamples = wavLength / (SDL_AUDIO_BITSIZE(m_format) / 8) / wavSpec.channels;
  TRE_ASSERT(wavLength == SDL_AUDIO_BITSIZE(m_format) / 8 * wavSpec.channels * m_nSamples);

  // data
  TRE_ASSERT(sizeof(audio_t) == 4);
  m_rawData.resize((wavLength + 3) / 4, 0);

  memcpy(m_rawData.data(), wavBuffer, wavLength);

  SDL_FreeWAV(wavBuffer);

  TRE_LOG("Audio loaded: samples = " << m_nSamples << ", freq = " << m_freq / 1000 << " kHz, buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::loadFromSDLAudio(unsigned int samples, int freq, SDL_AudioFormat format, bool stereo, uint8_t *audioBuffer)
{
  // meta-data
  m_nSamples = samples;
  m_freq = freq;
  m_format = format;
  m_stereo = stereo;

  const unsigned wavLength = SDL_AUDIO_BITSIZE(m_format) / 8 * (stereo ? 2 : 1) * m_nSamples;

  // data
  TRE_ASSERT(sizeof(audio_t) == 4);
  m_rawData.resize((wavLength + 3) / 4, 0);
  memcpy(m_rawData.data(), audioBuffer, wavLength);

  TRE_LOG("Audio loaded: samples = " << m_nSamples << ", freq = " << m_freq / 1000 << " kHz, buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::write(std::ostream &stream) const
{
  const int bufferSize = (SDL_AUDIO_BITSIZE(m_format) / 8) * (m_stereo ? 2 : 1) * m_nSamples;

  // header
  uint header[8];
  header[0] = SOUND_BIN_VERSION;
  header[1] = m_nSamples;
  header[2] = m_stereo ? 2 : 1;
  header[3] = 0; // RawSDL
  header[4] = 0;
  header[5] = uint(m_freq);
  header[6] = uint(m_format);
  header[7] = bufferSize;
  static_assert(sizeof(SDL_AudioFormat) <= sizeof(uint), "SDL_AudioFormat is too big");
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

  // data
  stream.write(reinterpret_cast<const char*>(m_rawData.data()), bufferSize);

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::read(std::istream &stream)
{
  // header
  uint header[8];
  stream.read(reinterpret_cast<char*>(&header), sizeof(header));
  TRE_ASSERT(header[0] == SOUND_BIN_VERSION);
  if (header[3] != 0)
  {
    TRE_LOG("s_RawSDL::read: incompatible object-type " << header[3] << " (expected 0)");
    return false;
  }

  m_nSamples  = header[1];
  m_stereo    = (header[2] == 2);
  m_freq      = int(header[5]);
  m_format    = SDL_AudioFormat(header[6]);

  const int bufferSize = (SDL_AUDIO_BITSIZE(m_format) / 8) * (m_stereo ? 2 : 1) * m_nSamples;
  TRE_ASSERT(bufferSize == int(header[7]));

  TRE_ASSERT(sizeof(audio_t) == 4);
  m_rawData.resize((bufferSize + 3) / 4, 0);

  stream.read(reinterpret_cast<char*>(m_rawData.data()), bufferSize);

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::convertTo(int freq, SDL_AudioFormat format)
{
  if (freq == m_freq && format == m_format)
    return true;

  const uint nchans = (m_stereo ? 2 : 1);

  SDL_AudioCVT wavConvertor;
  const int wanConvertorStatus = SDL_BuildAudioCVT(&wavConvertor,
                                                   m_format, nchans, m_freq,
                                                   format, nchans, freq);
  if (wanConvertorStatus <= 0)
  {
    TRE_LOG("fail to create the audio convertor: " << SDL_GetError());
    return false;
  }

  const unsigned rawData_ByteSize = (SDL_AUDIO_BITSIZE(m_format) / 8) * nchans * m_nSamples;
  TRE_ASSERT(rawData_ByteSize <= m_rawData.size() * sizeof(audio_t));

  TRE_ASSERT(sizeof(audio_t) == 4);
  const unsigned rawData_NeededSize = (rawData_ByteSize * wavConvertor.len_mult + 3) / 4;
  m_rawData.resize(rawData_NeededSize);

  wavConvertor.buf = reinterpret_cast<uint8_t*>(m_rawData.data());
  wavConvertor.len = rawData_ByteSize;

  SDL_ConvertAudio(&wavConvertor);

  m_freq = freq;
  m_format = format;
  m_nSamples = wavConvertor.len_cvt / nchans / (SDL_AUDIO_BITSIZE(m_format) / 8);
  TRE_ASSERT(unsigned(wavConvertor.len_cvt) == (SDL_AUDIO_BITSIZE(m_format) / 8) * nchans * m_nSamples);

  m_rawData.resize((wavConvertor.len_cvt + 3) / 4);

  TRE_LOG("Audio converted: samples = " << m_nSamples << ", buffer-size = " << m_rawData.size() * sizeof(audio_t) / 1024 << " ko");
  return true;
}

// soundData::s_Opus ==========================================================

const int soundData::s_Opus::m_freq;

// ----------------------------------------------------------------------------

struct s_oggPage
{
  uint64_t ogg_granPos;
  uint32_t ogg_pageId;
  uint8_t  ogg_segmentNbr;
  uint8_t  ogg_headerType;
  std::vector<uint8_t> ogg_segmentTable;

  bool read(std::istream &ins)
  {
    uint32_t ogg_magicNnbr = 0u;
    uint8_t  ogg_version = 0u;
    ins.read(reinterpret_cast<char*>(&ogg_magicNnbr) , sizeof(ogg_magicNnbr));  // "OggS"
    ins.read(reinterpret_cast<char*>(&ogg_version)   , sizeof(ogg_version));    // 0
    if (ogg_magicNnbr != 0x5367674f || ogg_version != 0) return false;
    uint32_t ogg_serialNbr;
    uint32_t ogg_CRCsum;
    ins.read(reinterpret_cast<char*>(&ogg_headerType), sizeof(ogg_headerType)); // 0x1: append from previous page, 0x2: first page(bos), 0x4: last page (eos)
    ins.read(reinterpret_cast<char*>(&ogg_granPos)   , sizeof(ogg_granPos));    // global pos
    ins.read(reinterpret_cast<char*>(&ogg_serialNbr) , sizeof(ogg_serialNbr));
    ins.read(reinterpret_cast<char*>(&ogg_pageId)    , sizeof(ogg_pageId));     // page id
    ins.read(reinterpret_cast<char*>(&ogg_CRCsum)    , sizeof(ogg_CRCsum));
    ins.read(reinterpret_cast<char*>(&ogg_segmentNbr), sizeof(ogg_segmentNbr)); // nbr of segment in this page
    if (ogg_segmentNbr != 0)
    {
      ogg_segmentTable.resize(ogg_segmentNbr);
      ins.read(reinterpret_cast<char*>(ogg_segmentTable.data()), ogg_segmentNbr);
    }
    else
    {
      ogg_segmentTable.clear();
    }
    return true;
  }
};

bool soundData::s_Opus::loadFromOPUS(const std::string &filename)
{
#ifdef TRE_WITH_OPUS

  std::ifstream readerOpus;
  readerOpus.open(filename.c_str(), std::ios_base::binary);
  if (!readerOpus)
  {
    TRE_LOG("Failed to load OPUS file " << filename);
    return false;
  }

  s_oggPage oggPage;

  if (!oggPage.read(readerOpus)) // page that contains the Opus-head
  {
    TRE_LOG("loadFromOPUS: invalid ogg header for the OpusHead section in file " << filename);
    return false;
  }
  if (oggPage.ogg_headerType != 2u || oggPage.ogg_pageId != 0u || oggPage.ogg_segmentNbr != 1u)
  {
    TRE_LOG("loadFromOPUS: invalid ogg BOS page in file " << filename);
    return false;
  }

  // read of the Opus Head
  uint8_t  opusH_chanCount = 0u;
  uint16_t opusH_preSkip = 0u;
  uint32_t opusH_freq = 0u;
  {
    uint64_t opusH_magicNbr;  readerOpus.read(reinterpret_cast<char*>(&opusH_magicNbr) , sizeof(opusH_magicNbr));  // "OpusHead"
    uint8_t  opusH_version;   readerOpus.read(reinterpret_cast<char*>(&opusH_version)  , sizeof(opusH_version));   // 1, but can be in [0,15] range
                              readerOpus.read(reinterpret_cast<char*>(&opusH_chanCount), sizeof(opusH_chanCount));
                              readerOpus.read(reinterpret_cast<char*>(&opusH_preSkip)  , sizeof(opusH_preSkip));
                              readerOpus.read(reinterpret_cast<char*>(&opusH_freq)     , sizeof(opusH_freq));      // sampling rate (Hz) before encoding
    int16_t  opusH_gainBd;    readerOpus.read(reinterpret_cast<char*>(&opusH_gainBd)   , sizeof(opusH_gainBd));    // value * 10^(gain/(20*256))
    uint8_t  opusH_mapFamily; readerOpus.read(reinterpret_cast<char*>(&opusH_mapFamily), sizeof(opusH_mapFamily));
    if (opusH_magicNbr != 0x646165487375704F)
    {
      TRE_LOG("loadFromOPUS: invalid opus header (magic number must be 'OpusHead')");
      return false;
    }
    if (opusH_version != 1 || opusH_chanCount == 0 || opusH_mapFamily != 0 /* TODO ? */ || opusH_chanCount >= 3 /* enforced with family == 0 */ || opusH_freq != m_freq)
    {
      TRE_LOG("loadFromOPUS: invalid or not-supported opus configuration");
      return false;
    }
  }

  if (!oggPage.read(readerOpus)) // page that contains the Opus-Tags
  {
    TRE_LOG("loadFromOPUS: invalid ogg header for the OpusTags section in file " << filename);
    return false;
  }
  {
    uint64_t opusT_magicNbr;   readerOpus.read(reinterpret_cast<char*>(&opusT_magicNbr)  , sizeof(opusT_magicNbr));    // "OpusTags"
    if (opusT_magicNbr != 0x736761547375704F)
    {
      TRE_LOG("loadFromOPUS: invalid opus header (magic number must be 'OpusHead')");
      return false;
    }
    // ignore the rest of the data: seek foward
    int soffset = 0;
    for (const uint8_t lacing : oggPage.ogg_segmentTable) soffset += lacing;
    soffset -= 8;
    TRE_ASSERT(soffset > 0);
    readerOpus.seekg(soffset, std::ios_base::cur);
  }

  m_stereo = (opusH_chanCount == 2);
  m_nSamples = 0;
  m_blokcs.clear();

  int  sampleOffset = -opusH_preSkip;
  bool appendSegement = false;

  while (true)
  {
    if (!oggPage.read(readerOpus)) // page that contains the audio data
      return false;

    for (uint8_t is = 0; is < oggPage.ogg_segmentNbr; ++is)
    {
      const uint8_t bufSize = oggPage.ogg_segmentTable[is];
      if (!appendSegement)
      {
        TRE_ASSERT(m_blokcs.empty() || !m_blokcs.back().m_data.empty()); // no empty block
        m_blokcs.emplace_back();
        m_blokcs.back().m_sampleStart = sampleOffset;
      }
      s_block &currentBlock = m_blokcs.back();
      if (bufSize != 0)
      {
        const std::size_t oldSize = currentBlock.m_data.size();
        currentBlock.m_data.resize(oldSize + bufSize);
        readerOpus.read(reinterpret_cast<char *>(&currentBlock.m_data[oldSize]), bufSize);
      }
      if (bufSize == 255)
      {
        appendSegement = true;
        continue;
      }
      appendSegement = false;

      const int ns = opus_packet_get_nb_samples(currentBlock.m_data.data(), bufSize, m_freq);
      sampleOffset += ns;
    }

    if ((oggPage.ogg_headerType & 0x4) != 0)
    {
      TRE_ASSERT(oggPage.ogg_granPos <= std::numeric_limits<uint>::max());
      m_nSamples = uint(oggPage.ogg_granPos - opusH_preSkip);
      break; // last page
    }
  }
  TRE_ASSERT(!appendSegement);

  if (m_nSamples == 0)
  {
    TRE_LOG("s_Opus::loadFromOPUS: the last ogg page was not recognized, the nbr of samples will be higher than the audio source length (" << filename <<")");
    m_nSamples = sampleOffset;
  }

  TRE_LOG("Audio loaded: samples = " << m_nSamples << ", freq = " << m_freq / 1000 << " kHz, compressed-audio-pages = " << m_blokcs.size());
  return true;
#else
  TRE_LOG("s_Opus::loadFromOPUS: FAILED (the current build does not include OPUS) on the file " << filename);
  return false;
#endif
}

// ----------------------------------------------------------------------------

bool soundData::s_Opus::loadFromRaw(soundData::s_RawSDL &rawData, unsigned bitrate)
{
  TRE_ASSERT(bitrate != 0);
  if (rawData.m_nSamples == 0) return false;

#ifdef TRE_WITH_OPUS

  if (rawData.m_freq != m_freq || rawData.m_format != AUDIO_S16) // supported format by OPUS
  {
    if (!rawData.convertTo(m_freq, AUDIO_S16))
      return false;
  }

  m_nSamples = rawData.m_nSamples;
  m_stereo = rawData.m_stereo;

  const uint nchans = rawData.m_stereo ? 2 : 1;

  // encoding setup
  OpusEncoder *audioEncoder = nullptr;
  if (bitrate != 0)
  {
    int errorCode = 0;
    audioEncoder = opus_encoder_create(m_freq, nchans, OPUS_APPLICATION_AUDIO, &errorCode);
    TRE_ASSERT(audioEncoder != nullptr && errorCode == 0);
    opus_encoder_ctl(audioEncoder, OPUS_SET_BITRATE(bitrate));
  }

  // encode
  const int blockSampleCount = 2880; // 60ms of sound at 48 kHz.
  const int bufferEncodeByteSize = blockSampleCount * sizeof(int16_t) * nchans;

  std::vector<uint8_t> bufferEncode;
  bufferEncode.resize(bufferEncodeByteSize);

  m_blokcs.reserve(1 + (rawData.m_nSamples - 1) / blockSampleCount);

  opus_encoder_ctl(audioEncoder, OPUS_RESET_STATE);

  {
    // prevent to read garbage when encoding the last chunk.
    const unsigned rawData_AlignedByteSize = ((rawData.m_nSamples + blockSampleCount - 1) / blockSampleCount) * blockSampleCount * sizeof(int16_t) * nchans;
    rawData.m_rawData.resize((rawData_AlignedByteSize + 3) / 4, 0);
  }

  const int16_t * __restrict audioBufferSrc = reinterpret_cast<const int16_t*>(rawData.m_rawData.data());

  uint encodedSamples = 0;
  uint totalDataBytes = 0;
  while (encodedSamples < rawData.m_nSamples)
  {
    const int bufferWriteSize = opus_encode(audioEncoder, audioBufferSrc, blockSampleCount, bufferEncode.data(), bufferEncodeByteSize);
    TRE_ASSERT(bufferWriteSize > 0);

    m_blokcs.emplace_back();
    s_block &curBlock = m_blokcs.back();

    curBlock.m_sampleStart = encodedSamples;
    curBlock.m_data.resize(bufferWriteSize);
    memcpy(curBlock.m_data.data(), bufferEncode.data(), bufferWriteSize);

    encodedSamples += blockSampleCount;
    audioBufferSrc += blockSampleCount * nchans;
    totalDataBytes += bufferWriteSize;
  }

  TRE_LOG("s_Opus::compress: Samples = " << m_nSamples << ", Compression = " << bitrate / 1000 << " kb/s, Raw-Size = " << rawData.m_rawData.size()/1024 << " kB, Compressed-Size = "  << totalDataBytes/1024 << " kB");

  opus_encoder_destroy(audioEncoder);

  return true;
#else
  (void)bitrate;
  TRE_LOG("s_Opus::loadFromRaw: FAILED (the current build does not include OPUS)");
  return false;
#endif
}

// ----------------------------------------------------------------------------

bool soundData::s_Opus::write(std::ostream &stream) const
{
  // header
  uint header[8];
  header[0] = 0x003;
  header[1] = m_nSamples;
  header[2] = m_stereo ? 2 : 1;
  header[3] = 1; // Opus
  header[4] = 0;
  header[5] = m_freq; //freq
  header[6] = AUDIO_S16; // format
  header[7] = uint(m_blokcs.size());
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

  // data
  for (const s_block &b : m_blokcs)
  {
    stream.write(reinterpret_cast<const char*>(&b.m_sampleStart), sizeof(int));
    uint dsize = uint(b.m_data.size());
    TRE_ASSERT(dsize != 0);
    stream.write(reinterpret_cast<const char*>(&dsize), sizeof(uint));
    stream.write(reinterpret_cast<const char*>(b.m_data.data()), dsize);
  }

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_Opus::read(std::istream &stream)
{
  // header
  uint header[8];
  stream.read(reinterpret_cast<char*>(&header), sizeof(header));
  TRE_ASSERT(header[0] == SOUND_BIN_VERSION);
  if (header[3] != 1)
  {
    TRE_LOG("s_Opus::read: incompatible object-type " << header[3] << " (expected 1)");
    return false;
  }
  if (header[5] != m_freq)
  {
    TRE_LOG("s_Opus::read: incompatible audio frequence " << header[5] << " (expected " << m_freq << " Hz)");
    return false;
  }

  m_nSamples  = header[1];
  m_stereo    = (header[2] == 2);

  m_blokcs.resize(header[7]);

  // data
  for (s_block &b : m_blokcs)
  {
    stream.read(reinterpret_cast<char*>(&b.m_sampleStart), sizeof(int));
    unsigned dsize = 0;
    stream.read(reinterpret_cast<char*>(&dsize), sizeof(unsigned));
    TRE_ASSERT(dsize != 0);
    b.m_data.resize(dsize);
    stream.read(reinterpret_cast<char*>(b.m_data.data()), dsize);
  }

#ifndef TRE_WITH_OPUS
  TRE_LOG("soundData::s_Opus::read: the current build does not include OPUS, so the audio data will be useless.");
#endif

  return true;
}

// ----------------------------------------------------------------------------

unsigned soundData::s_Opus::getBlockAtSampleId(int sid) const
{
  for (unsigned ib = 0; ib < m_blokcs.size(); ++ib)
  {
    if (sid < m_blokcs[ib].m_sampleStart)
      return unsigned(ib - 1);
  }
  return unsigned(m_blokcs.size() - 1);
}

// soundSampler ===============================================================

soundSampler::s_sampler_Opus::s_sampler_Opus()
{
  m_decompressedBuffer.fill(0);
}

// ----------------------------------------------------------------------------

soundSampler::s_sampler_Opus::~s_sampler_Opus()
{
#ifdef TRE_WITH_OPUS
  if (m_decoder != nullptr) opus_decoder_destroy(m_decoder);
  m_decoder = nullptr;
#endif
}

// ----------------------------------------------------------------------------

bool soundSampler::s_sampler_Opus::decodeSlot(const soundData::s_Opus &data, unsigned slot)
{
#ifdef TRE_WITH_OPUS
  if (m_decoder == nullptr)
  {
    int error;
    m_decoder = opus_decoder_create(soundData::s_Opus::m_freq, 2, &error);
    if (m_decoder == nullptr)
      return false;
    opus_decoder_ctl(m_decoder, OPUS_RESET_STATE);
  }
  if (slot == unsigned(-1) || slot >= data.m_blokcs.size())
    return false;
  if (slot == m_decompressedSlot)
    return true;

  const int blockSampleCount = opus_packet_get_nb_samples(data.m_blokcs[slot].m_data.data(), opus_int32(data.m_blokcs[slot].m_data.size()), soundData::s_Opus::m_freq);
  if (blockSampleCount < 0 || blockSampleCount > 2880)
    return false;

  const int ret = opus_decode(m_decoder, data.m_blokcs[slot].m_data.data(), opus_int32(data.m_blokcs[slot].m_data.size()), m_decompressedBuffer.data(), blockSampleCount, 0);
  m_decompressedSlot = slot;
  m_decompressedCount = blockSampleCount;

  // copy last data (LR)
  m_decompressedBuffer[blockSampleCount * 2 + 0] = m_decompressedBuffer[blockSampleCount * 2 - 2 + 0];
  m_decompressedBuffer[blockSampleCount * 2 + 1] = m_decompressedBuffer[blockSampleCount * 2 - 2 + 1];

  return (ret > 0);
#else
  (void)data;
  (void)slot;
  return false;
#endif // OPUS
}

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

#ifdef TRE_PROFILE
  m_perftime_total     = m_audioCallbackContext.ac_perftime_total;
  m_perftime_nbrCall   = m_audioCallbackContext.ac_perftime_nbrCall;
  m_perftime_nbrSample = m_audioCallbackContext.ac_perftime_nbrSample;
  m_audioCallbackContext.ac_perftime_total = 0.f;
  m_audioCallbackContext.ac_perftime_nbrCall = 0;
  m_audioCallbackContext.ac_perftime_nbrSample = 0;
#endif

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
#ifdef TRE_PROFILE
  typedef std::chrono::steady_clock systemclock;
  const systemclock::time_point tickStart =  systemclock::now();
#endif

  TRE_ASSERT(ac_channels == 2);

  const uint sampleCount = uint(len) / sizeof(int16_t) / ac_channels;

  TRE_ASSERT(sampleCount * ac_channels <= ac_bufferF32.size());

  memset(ac_bufferF32.data(), 0, sizeof(float) * ac_bufferF32.size());

  // mix sounds with floats

  for (soundInterface *bs : ac_listSounds)
    bs->sample(ac_bufferF32.data(), sampleCount, ac_freq);

  // convert to 16-bit signed-integers and fill dst buffer.

  int16_t * __restrict  outPtr = reinterpret_cast<int16_t*>(stream);
  _copystream_F32LR_I16LR(ac_bufferF32.data(), outPtr, sampleCount);

#ifdef TRE_PROFILE
  const systemclock::time_point tickEnd =  systemclock::now();
  const double timeElapsed = std::chrono::duration<double>(tickEnd - tickStart).count();
  ac_perftime_nbrCall += 1;
  ac_perftime_nbrSample += sampleCount;
  ac_perftime_total += float(timeElapsed);
#endif
}

// ============================================================================

} // namespace tre
