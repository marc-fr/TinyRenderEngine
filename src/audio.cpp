#include "tre_audio.h"

#include "tre_utils.h"

#include <array>

#ifdef TRE_PROFILE
#include <chrono>
#endif

#ifdef TRE_WITH_OPUS
#include "opus.h"
#else
struct OpusDecoder; // keeps sound2D::s_opusState valid without Opus
#endif

#ifdef TRE_WITH_MINIMP3
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "extern/minimp3.h"
#endif

#define SOUND_BIN_VERSION 0x004

namespace tre
{

// soundData::s_RawSDL ========================================================

bool soundData::s_RawSDL::loadFromWAV(const std::string & wavFile)
{
  SDL_AudioSpec wavSpec; // "format", "freq" and "channels" are filled
  Uint32 wavLength = 0;
  Uint8 *wavBuffer = nullptr;
  if(SDL_LoadWAV(wavFile.c_str(), &wavSpec, &wavBuffer, &wavLength) == nullptr)
  {
    TRE_LOG("fail to load audio file: " << SDL_GetError());
    return false;
  }

  if (wavSpec.channels != 1 && wavSpec.channels != 2)
  {
    TRE_LOG("s_RawSDL::loadFromWAV: not-supported nbr of channels (" << wavSpec.channels << ")");
    SDL_FreeWAV(wavBuffer);
    return false;
  }

  const bool ret = loadFromSDLAudio(wavSpec.freq, wavSpec.format, wavSpec.channels == 2, wavBuffer, wavLength);
  SDL_FreeWAV(wavBuffer);
  return ret;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::loadFromWAV(const std::vector<uint8_t> &fileBuffer)
{
  SDL_AudioSpec wavSpec; // "format", "freq" and "channels" are filled

  SDL_RWops* rw = SDL_RWFromConstMem(fileBuffer.data(), static_cast<int>(fileBuffer.size()));
  if (!rw) return false;

  Uint32 wavLength = 0;
  Uint8 *wavBuffer = nullptr;
  if(SDL_LoadWAV_RW(rw, 1, &wavSpec, &wavBuffer, &wavLength) == nullptr)
  {
    TRE_LOG("fail to load audio data: " << SDL_GetError());
    return false;
  }

  if (wavSpec.channels != 1 && wavSpec.channels != 2)
  {
    TRE_LOG("s_RawSDL::loadFromWAV: not-supported nbr of channels (" << wavSpec.channels << ")");
    SDL_FreeWAV(wavBuffer);
    return false;
  }

  const bool ret = loadFromSDLAudio(wavSpec.freq, wavSpec.format, wavSpec.channels == 2, wavBuffer, wavLength);
  SDL_FreeWAV(wavBuffer);
  return ret;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::loadFromMP3(const std::vector<uint8_t> &fileBuffer)
{
#ifdef TRE_WITH_MINIMP3
  mp3dec_t dec;
  mp3dec_init(&dec);
  mp3dec_frame_info_t info{};
  mp3d_sample_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
  static_assert(sizeof(mp3d_sample_t) == sizeof(int16_t));

  m_rawData.clear();
  m_rawData.reserve(fileBuffer.size() * 6);

  const uint8_t* p = fileBuffer.data();
  std::size_t left = fileBuffer.size();
  while (left > 0)
  {
    const int samples = mp3dec_decode_frame(&dec, p, int(left), frame, &info);
    if (info.frame_bytes == 0) break;
    p    += info.frame_bytes;
    left -= info.frame_bytes;
    if (samples > 0)
    {
      m_rawData.insert(m_rawData.end(), frame, frame + samples * info.channels);
    }
  }

  if (info.channels != 1 && info.channels != 2)
  {
    TRE_LOG("s_RawSDL::loadFromMP3: not-supported nbr of channels (" << info.channels << ")");
    return false;
  }

  m_stereo = (info.channels != 1);
  m_freq = info.hz;
  m_nSamples = unsigned(m_rawData.size()) / info.channels;
  return true;
#else
  (void)fileBuffer;
  TRE_LOG("s_RawSDL::loadFromMP3: FAILED (the current build does not include minimp3)");
  return false;
#endif
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::loadFromSDLAudio(int freq, SDL_AudioFormat format, bool stereo, uint8_t *audioBuffer, unsigned audioBufferByteSize)
{
  // meta-data
  const unsigned nchans = (stereo ? 2 : 1);
  m_nSamples = audioBufferByteSize / (SDL_AUDIO_BITSIZE(format) / 8) / nchans;
  m_freq = freq;
  m_stereo = stereo;

  // data
  m_rawData.resize(m_nSamples * nchans, 0);

  if (format == AUDIO_S16)
  {
    memcpy(m_rawData.data(), audioBuffer, m_rawData.size() * sizeof(int16_t));
  }
  else
  {
    SDL_AudioCVT wavConvertor;
    const int wanConvertorStatus = SDL_BuildAudioCVT(&wavConvertor,
                                                     format, nchans, freq,
                                                     AUDIO_S16, nchans, freq);
    if (wanConvertorStatus <= 0)
    {
      TRE_LOG("fail to create the audio convertor: " << SDL_GetError());
      return false;
    }

    static_assert(sizeof(int16_t) == 2);
    const unsigned rawDataNeededSize = std::max(audioBufferByteSize, nchans * m_nSamples * 2);
    m_rawData.resize((rawDataNeededSize + 1) / 2);

    memcpy(m_rawData.data(), audioBuffer, audioBufferByteSize);

    wavConvertor.buf = reinterpret_cast<Uint8*>(m_rawData.data());
    wavConvertor.len = audioBufferByteSize;

    SDL_ConvertAudio(&wavConvertor);

    TRE_ASSERT(wavConvertor.len_cvt == nchans * m_nSamples * sizeof(int16_t));
    m_rawData.resize(nchans * m_nSamples);
  }

  TRE_LOG("Audio loaded: samples = " << m_nSamples << ", freq = " << m_freq / 1000 << " kHz, buffer-size = " << m_rawData.size() * sizeof(int16_t) / 1024 << " ko");
  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::write(std::ostream &stream) const
{
  // header
  uint32_t header[8] = {};
  header[0] = SOUND_BIN_VERSION;
  header[1] = m_nSamples;
  header[2] = m_stereo ? 2 : 1;
  header[3] = 0; // RawSDL
  header[5] = uint32_t(m_freq);
  header[7] = uint32_t(m_rawData.size());
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

  // data
  stream.write(reinterpret_cast<const char*>(m_rawData.data()), m_rawData.size() * sizeof(int16_t));

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::read(std::istream &stream)
{
  // header
  uint32_t header[8];
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

  m_rawData.resize(header[7]);
  stream.read(reinterpret_cast<char*>(m_rawData.data()), m_rawData.size() * sizeof(int16_t));

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_RawSDL::convertTo(int freq)
{
  if (freq == m_freq) return true;

  const unsigned nchans = (m_stereo ? 2 : 1);

  SDL_AudioCVT wavConvertor;
  const int wanConvertorStatus = SDL_BuildAudioCVT(&wavConvertor,
                                                   AUDIO_S16, nchans, m_freq,
                                                   AUDIO_S16, nchans, freq);
  if (wanConvertorStatus <= 0)
  {
    TRE_LOG("fail to create the audio convertor: " << SDL_GetError());
    return false;
  }

  const unsigned rawDataByteSize = nchans * m_nSamples * sizeof(int16_t);
  TRE_ASSERT(nchans * m_nSamples <= m_rawData.size());

  const unsigned rawDataNeededByteSize = std::max(rawDataByteSize, rawDataByteSize * wavConvertor.len_mult);
  static_assert(sizeof(int16_t) == 2);
  m_rawData.resize((rawDataNeededByteSize + 1) / 2);

  wavConvertor.buf = reinterpret_cast<Uint8*>(m_rawData.data());
  wavConvertor.len = rawDataByteSize;

  SDL_ConvertAudio(&wavConvertor);

  m_nSamples = wavConvertor.len_cvt / nchans / sizeof(int16_t);
  m_rawData.resize(nchans * m_nSamples);

  TRE_LOG("Audio converted: samples = " << m_nSamples << ", buffer-size = " << m_rawData.size() * sizeof(int16_t) / 1024 << " ko");
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
    if (opusH_version != 1 || opusH_chanCount == 0 || opusH_mapFamily != 0 /* TODO ? */ || opusH_chanCount >= 3 /* enforced with family == 0 */)
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
  m_blocks.clear();

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
        TRE_ASSERT(m_blocks.empty() || !m_blocks.back().m_data.empty()); // no empty block
        m_blocks.emplace_back();
        m_blocks.back().m_sampleStart = sampleOffset;
      }
      s_block &currentBlock = m_blocks.back();
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

      const int ns = opus_packet_get_nb_samples(currentBlock.m_data.data(), opus_int32(currentBlock.m_data.size()), m_freq);
      sampleOffset += ns;
    }

    if ((oggPage.ogg_headerType & 0x4) != 0)
    {
      TRE_ASSERT(oggPage.ogg_granPos <= std::numeric_limits<unsigned>::max());
      m_nSamples = unsigned(oggPage.ogg_granPos - opusH_preSkip);
      break; // last page
    }
  }
  TRE_ASSERT(!appendSegement);

  if (m_nSamples == 0)
  {
    TRE_LOG("s_Opus::loadFromOPUS: the last ogg page was not recognized, the nbr of samples will be higher than the audio source length (" << filename <<")");
    m_nSamples = sampleOffset;
  }

  TRE_LOG("Audio loaded: samples = " << m_nSamples << ", freq = " << m_freq / 1000 << " kHz, compressed-audio-pages = " << m_blocks.size());
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

  if (rawData.m_freq != m_freq)
  {
    if (!rawData.convertTo(m_freq)) return false;
  }

  m_nSamples = rawData.m_nSamples;
  m_stereo = rawData.m_stereo;

  const unsigned nchans = rawData.m_stereo ? 2 : 1;

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

  m_blocks.clear();
  m_blocks.reserve(1 + (rawData.m_nSamples - 1) / blockSampleCount);

  opus_encoder_ctl(audioEncoder, OPUS_RESET_STATE);

  {
    // prevent to read garbage when encoding the last chunk.
    const unsigned rawDataElementCount = ((rawData.m_nSamples + blockSampleCount - 1) / blockSampleCount) * blockSampleCount * nchans;
    rawData.m_rawData.resize(rawDataElementCount, 0);
  }

  const int16_t * __restrict audioBufferSrc = rawData.m_rawData.data();

  unsigned encodedSamples = 0;
  unsigned totalDataBytes = 0;
  while (encodedSamples < rawData.m_nSamples)
  {
    const int bufferWriteSize = opus_encode(audioEncoder, audioBufferSrc, blockSampleCount, bufferEncode.data(), bufferEncodeByteSize);
    TRE_ASSERT(bufferWriteSize > 0);

    m_blocks.emplace_back();
    s_block &curBlock = m_blocks.back();

    curBlock.m_sampleStart = encodedSamples;
    curBlock.m_data.resize(bufferWriteSize);
    memcpy(curBlock.m_data.data(), bufferEncode.data(), bufferWriteSize);

    encodedSamples += blockSampleCount;
    audioBufferSrc += blockSampleCount * nchans;
    totalDataBytes += bufferWriteSize;
  }

  TRE_LOG("s_Opus::loadFromRaw: Samples = " << m_nSamples << ", Target-bitrate = " << bitrate / 1000 << " kb/s, Raw-Size = " << rawData.m_rawData.size()*sizeof(int16_t)/1024 << " kB, Compressed-Size = "  << totalDataBytes/1024 << " kB");

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
  uint32_t header[8] = {};
  header[0] = SOUND_BIN_VERSION;
  header[1] = m_nSamples;
  header[2] = m_stereo ? 2 : 1;
  header[3] = 1; // Opus
  header[5] = m_freq; //freq
  header[7] = uint32_t(m_blocks.size());
  stream.write(reinterpret_cast<const char*>(&header), sizeof(header));

  // data
  for (const s_block &b : m_blocks)
  {
    stream.write(reinterpret_cast<const char*>(&b.m_sampleStart), sizeof(int));
    const uint32_t dsize = uint32_t(b.m_data.size());
    TRE_ASSERT(dsize != 0);
    stream.write(reinterpret_cast<const char*>(&dsize), sizeof(uint32_t));
    stream.write(reinterpret_cast<const char*>(b.m_data.data()), dsize);
  }

  return true;
}

// ----------------------------------------------------------------------------

bool soundData::s_Opus::read(std::istream &stream)
{
  // header
  uint32_t header[8];
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

  m_blocks.resize(header[7]);

  // data
  for (s_block &b : m_blocks)
  {
    stream.read(reinterpret_cast<char*>(&b.m_sampleStart), sizeof(int));
    uint32_t dsize = 0;
    stream.read(reinterpret_cast<char*>(&dsize), sizeof(uint32_t));
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
  for (unsigned ib = 0; ib < m_blocks.size(); ++ib)
  {
    if (sid < m_blocks[ib].m_sampleStart)
      return unsigned(ib - 1);
  }
  return unsigned(m_blocks.size() - 1);
}

// audio-Context methods ======================================================

bool audioContext::startSystem(const char *deviceName, unsigned bufferMS, SDL_AudioSpec *requiredAudioSpec)
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

  unsigned samples = (audioSettingsWanted.freq * bufferMS) / 1000;
  // -> power of 2
  {
    int count = 0;
    while ((samples = samples >> 1) != 0) ++count;
    samples = 0x1 << count;
  }
  if (samples <  512) samples = 512;
  if (samples > 8192) samples = 8192;

  audioSettingsWanted.samples = uint16_t(samples);

  m_audioSpec = new SDL_AudioSpec;

  m_deviceID = SDL_OpenAudioDevice(deviceName, 0/*no capture*/, &audioSettingsWanted, m_audioSpec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (m_deviceID == 0)
  {
    TRE_LOG("Couldn't open audio device: " << SDL_GetError());
    delete m_audioSpec; m_audioSpec = nullptr;
    return false;
  }

  TRE_LOG("Audio device " << (deviceName == nullptr ? "(default)" : deviceName) << " is opened.\n(" <<
          "freq = " << m_audioSpec->freq / 1000.f << " kHz, " <<
          "hardware-buffer = " << m_audioSpec->samples * 1000 / m_audioSpec->freq << " ms, " <<
          "channels = " << m_audioSpec->channels << ", "
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
  m_perf_nbrCall     = m_audioCallbackContext.ac_perf_nbrCall;
  m_perf_elapsedTime = m_audioCallbackContext.ac_perf_elapsedTime;
  m_perf_accElapsedTime = 0.95f * m_perf_accElapsedTime + 0.05f * m_perf_elapsedTime;
  m_perf_accGenTime     = 0.95f * m_perf_accGenTime     + 0.05f * m_audioCallbackContext.ac_perf_genTime;
  m_audioCallbackContext.ac_perf_nbrCall = 0;
  m_audioCallbackContext.ac_perf_elapsedTime = 0.f;
  m_audioCallbackContext.ac_perf_genTime = 0.f;
#endif

  {
    s_audioCallbackContext &acc = m_audioCallbackContext;
    m_levels.m_peak = acc.ac_levels.m_peak;
    m_levels.m_RMS = (acc.ac_levels.m_sampleCount != 0) ? std::sqrt(acc.ac_levels.m_RMS / (2.f * acc.ac_levels.m_sampleCount)) : 0.f;
    m_levels.m_sampleCount = acc.ac_levels.m_sampleCount;
    acc.ac_levels = audioContext::s_levels();
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
  m_audioCallbackContext.ac_levels = s_levels();
  m_levels = s_levels();
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

  const int nDevices = SDL_GetNumAudioDevices(0 /*capture*/);
  if (nDevices <= 0) return;

  devices.resize(nDevices);
  for (int i = 0; i < nDevices; ++i)
    devices[i] = SDL_GetAudioDeviceName(int(i), 0 /*capture*/);
}

// ----------------------------------------------------------------------------

/// Peak and sum of squares of a LR stream
static void _measure_F32LR(const float * __restrict instream, unsigned nsamples, float &peak, float &sumSq)
{
  float          pk[8] = {}; // note: 8 independent accumulators let the compiler vectorize it (no need of -ffast-math).
  float          sq[8] = {};
  const unsigned nvalues = 2 * nsamples;
  unsigned       i = 0;
  for (; i + 8 <= nvalues; i += 8)
  {
    for (unsigned k = 0; k < 8; ++k)
    {
      const float v = instream[i + k];
      pk[k] = std::max(pk[k], std::abs(v));
      sq[k] += v * v;
    }
  }
  float p = 0.f, s = 0.f;
  for (unsigned k = 0; k < 8; ++k) { p = std::max(p, pk[k]); s += sq[k]; }
  for (; i < nvalues; ++i) { p = std::max(p, std::abs(instream[i])); s += instream[i] * instream[i]; }
  peak = p;
  sumSq = s;
}

/// Convert LR stream from float to int16
static void _copystream_F32LR_I16LR(const float * __restrict instream, int16_t * __restrict outstream, unsigned nsamples)
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

  const unsigned sampleCount = unsigned(len) / sizeof(int16_t) / ac_channels;

  TRE_ASSERT(sampleCount * ac_channels <= ac_bufferF32.size());

  memset(ac_bufferF32.data(), 0, sizeof(float) * ac_bufferF32.size());

  // mix sounds with floats

  for (soundInterface *bs : ac_listSounds)
    bs->sample(ac_bufferF32.data(), sampleCount, ac_freq);

  // measure the mix
  {
    float peak, sumSq;
    _measure_F32LR(ac_bufferF32.data(), sampleCount, peak, sumSq);
    ac_levels.m_peak = std::max(ac_levels.m_peak, peak);
    ac_levels.m_RMS += sumSq; // RMS resolution done later
    ac_levels.m_sampleCount += sampleCount;
  }

  // convert to 16-bit signed-integers and fill dst buffer.

  int16_t * __restrict  outPtr = reinterpret_cast<int16_t*>(stream);
  _copystream_F32LR_I16LR(ac_bufferF32.data(), outPtr, sampleCount);

#ifdef TRE_PROFILE
  const systemclock::time_point tickEnd =  systemclock::now();
  const double timeElapsed = std::chrono::duration<double, std::micro>(tickEnd - tickStart).count();
  ac_perf_nbrCall += 1;
  ac_perf_elapsedTime += float(timeElapsed * 1.e-6);
  ac_perf_genTime += sampleCount / float(ac_freq);
#endif
}

// sound2D::s_opusState =======================================================

struct sound2D::s_opusState
{
  static constexpr unsigned k_maxBlockSamples = 5760; ///< 120ms of sound at 48 kHz (max Opus packet duration)
  static constexpr int      k_preRollSamples  = 3840; ///< 80ms, decoder convergence after a seek (RFC 7845, sec. 4.6)

  OpusDecoder                                    *m_decoder = nullptr;
  unsigned                                       m_decodedSlot = unsigned(-1); ///< unsigned(-1) means: nothing decoded, decoder state is fresh (reset)
  unsigned                                       m_decodedCount = 0; ///< nbr of samples in the buffer.
  std::array<float, k_maxBlockSamples * 2 + 16>  m_decodedBuffer; ///< decoded data, stereo-interleaved, normalized [-1,1] (+1 padding frame for the linear-interpolation)

  s_opusState()
  {
    m_decodedBuffer.fill(0.f);
  }

  ~s_opusState()
  {
#ifdef TRE_WITH_OPUS
    if (m_decoder != nullptr) opus_decoder_destroy(m_decoder);
#endif
    m_decoder = nullptr;
  }
};

// sound2D::s_stereoControl ===================================================

static inline sound2D::s_stereoControl _mixControls(const sound2D::s_stereoControl &a, const sound2D::s_stereoControl &b, const float t)
{
  const float ta = 1.f - t;
  return sound2D::s_stereoControl(a.m_volume * ta + b.m_volume * t, a.m_pan * ta + b.m_pan * t);
}

static inline void _applyControls(const sound2D::s_stereoControl &c, float &valueL, float &valueR)
{
  const float pL = 1.f - std::max(c.m_pan, 0.f);
  const float pR = 1.f + std::min(c.m_pan, 0.f);
  valueL *= c.m_volume * pL;
  valueR *= c.m_volume * pR;
}

// sound2D ====================================================================

sound2D::~sound2D()
{
  if (ac_opusState) delete ac_opusState;
}

// ----------------------------------------------------------------------------

void sound2D::setAudioData(const soundData::s_RawSDL *audioD)
{
  m_audioDataRaw = audioD;
}

// ----------------------------------------------------------------------------

void sound2D::setAudioData(const soundData::s_Opus *audioD)
{
  m_audioDataOpus = audioD;

#ifdef TRE_WITH_OPUS
  if (audioD == nullptr) return;

  // allocate the decoder here (main thread)
  if (!ac_opusState) ac_opusState = new s_opusState;

  if (ac_opusState->m_decoder == nullptr)
  {
    int error = 0;
    ac_opusState->m_decoder = opus_decoder_create(soundData::s_Opus::m_freq, 2, &error);
    if (ac_opusState->m_decoder == nullptr)
    {
      TRE_LOG("sound2D::setAudioData: failed to create the Opus decoder (error " << error << ")");
      return;
    }
  }

  // new stream: forget the previously decoded block.
  opus_decoder_ctl(ac_opusState->m_decoder, OPUS_RESET_STATE);
  ac_opusState->m_decodedSlot = unsigned(-1);
  ac_opusState->m_decodedCount = 0;
#endif
}

// ----------------------------------------------------------------------------

void sound2D::sync()
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
    ac_phase = uint64_t(m_control.m_cursor) << 32;
    m_control.m_cursor = unsigned(-1);
  }
}

// ----------------------------------------------------------------------------

void sound2D::sample(float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
{
  if (!ac_control.m_isPlaying) return;

  s_stereoControl endControls;
  if (ac_control.m_targetDelay >= 0.f)
  {
    const float dt = float(sampleCount) / float(sampleFreq);
    const float cursor = (dt != 0.f) ? std::min(1.f, dt / ac_control.m_targetDelay) : 1.f;
    endControls = _mixControls(ac_mixPrev, ac_control.m_target, cursor);
    ac_control.m_targetDelay -= dt;
  }
  else
  {
    endControls = ac_mixPrev;
    ac_control.m_targetDelay = -1.f;
  }

  bool  sampled = false;

#ifdef TRE_WITH_OPUS
  if (m_audioDataOpus != nullptr)
  {
    if (m_audioDataOpus->m_nSamples == 0) return; // no audio data ?!?
    _sampleOpus(*m_audioDataOpus, ac_mixPrev, endControls, outBufferAdd, sampleCount, sampleFreq);
    sampled = true;
  }
#endif

  if (!sampled && m_audioDataRaw != nullptr)
  {
    if (m_audioDataRaw->m_nSamples == 0) return; // no audio data ?!?
    _sampleRaw(*m_audioDataRaw, ac_mixPrev, endControls, outBufferAdd, sampleCount, sampleFreq);
    sampled = true;
  }

  if (!sampled)
  {
    TRE_FATAL("sound2D::sample: unsupported audio data");
  }

  ac_mixPrev = endControls;
}

// ----------------------------------------------------------------------------

static inline void _gains(const sound2D::s_stereoControl &c, float &gL, float &gR, const float k = 1.f / 32767.f /* int16 normalizer */)
{
  gL = c.m_volume * (1.f - std::max(c.m_pan, 0.f)) * k;
  gR = c.m_volume * (1.f + std::min(c.m_pan, 0.f)) * k;
}

template<bool STEREO, class T>
static void _kernelRaw(const T * __restrict src, uint64_t phase, const uint64_t step,
                       float * __restrict out, const unsigned n,
                       const float gL0, const float dgL, const float gR0, const float dgR)
{
  if (step == (uint64_t(1) << 32)) // no interpolation
  {
    const T * __restrict s = src + (STEREO ? 2 : 1) * size_t(phase >> 32);
    for (unsigned i = 0; i < n; ++i)
    {
      const float fi = float(i);
      const float L = float(s[STEREO ? 2 * i + 0 : i]) * (gL0 + fi * dgL);
      const float R = float(s[STEREO ? 2 * i + 1 : i]) * (gR0 + fi * dgR);
      out[2 * i + 0] += L;
      out[2 * i + 1] += R;
    }
  }
  else
  {
    for (unsigned i = 0; i < n; ++i, phase += step)
    {
      const size_t idx = size_t(phase >> 32);
      const float  w1  = float(int32_t(uint32_t(phase) >> 1)) * (1.f / 2147483648.f);
      float l0, l1, r0, r1;
      if (STEREO) { l0 = float(src[2 * idx]); r0 = float(src[2 * idx + 1]); l1 = float(src[2 * idx + 2]); r1 = float(src[2 * idx + 3]); }
      else        { l0 = r0 = float(src[idx]);                              l1 = r1 = float(src[idx + 1]);                              }
      const float fi = float(i);
      const float L = (l0 + w1 * (l1 - l0)) * (gL0 + fi * dgL);
      const float R = (r0 + w1 * (r1 - r0)) * (gR0 + fi * dgR);
      out[2 * i + 0] += L;
      out[2 * i + 1] += R;
    }
  }
}

void sound2D::_sampleRaw(const soundData::s_RawSDL &data, const s_stereoControl &controlsStart, const s_stereoControl &controlsEnd,
                         float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
{
  float gL0, gR0, gL1, gR1;
  _gains(controlsStart, gL0, gR0);
  _gains(controlsEnd, gL1, gR1);
  const float    inv = (sampleCount > 1) ? 1.f / float(sampleCount - 1) : 0.f;
  const float    dgL = (gL1 - gL0) * inv;
  const float    dgR = (gR1 - gR0) * inv;
  const uint64_t step = (uint64_t(data.m_freq) << 32) / uint64_t(sampleFreq);
  const uint64_t endPhase = uint64_t(data.m_nSamples) << 32;
  const bool     repeat = ac_control.m_isRepeating;

  unsigned done = 0;
  if (repeat) ac_phase %= endPhase;

  while (done < sampleCount && ac_phase < endPhase)
  {
    const uint64_t nUntilEnd = (endPhase - ac_phase + step - 1) / step;
    const unsigned n = unsigned(std::min<uint64_t>(nUntilEnd, sampleCount - done));
    if (data.m_stereo)
      _kernelRaw<true>(data.m_rawData.data(), ac_phase, step, outBufferAdd + 2 * done, n,
                       gL0 + float(done) * dgL, dgL, gR0 + float(done) * dgR, dgR);
    else
      _kernelRaw<false>(data.m_rawData.data(), ac_phase, step, outBufferAdd + 2 * done, n,
                       gL0 + float(done) * dgL, dgL, gR0 + float(done) * dgR, dgR);
    ac_phase += uint64_t(n) * step;
    done += n;
    if (repeat && ac_phase >= endPhase) ac_phase -= endPhase;
  }
}

// ----------------------------------------------------------------------------

void sound2D::_sampleOpus(const soundData::s_Opus &data, const s_stereoControl &controlsStart, const s_stereoControl &controlsEnd,
                          float * __restrict outBufferAdd, unsigned sampleCount, int sampleFreq)
{
  if (ac_opusState == nullptr) return; // setAudioData failed to allocate the decoder

  float gL0, gR0, gL1, gR1;
  _gains(controlsStart, gL0, gR0, 1.f);
  _gains(controlsEnd, gL1, gR1, 1.f);
  const float    inv = (sampleCount > 1) ? 1.f / float(sampleCount - 1) : 0.f;
  const float    dgL = (gL1 - gL0) * inv;
  const float    dgR = (gR1 - gR0) * inv;
  const uint64_t step = (uint64_t(soundData::s_Opus::m_freq) << 32) / uint64_t(sampleFreq);
  const uint64_t endPhase = uint64_t(data.m_nSamples) << 32;
  const bool     repeat = ac_control.m_isRepeating;

  unsigned done = 0;
  if (repeat) ac_phase %= endPhase;

  while (done < sampleCount && ac_phase < endPhase)
  {
    const unsigned slot = data.getBlockAtSampleId(int(ac_phase >> 32));
    if (slot == unsigned(-1))
    {
      TRE_LOG("audio sampling: invalid input data from a soundData::s_Opus");
      return; // bad data
    }
    if (!_decodeOpusBlock(data, slot))
    {
      TRE_LOG("audio sampling: failed to decode slot " << slot << " from a soundData::s_Opus");
      return;
    }

    // phase relative to the decoded block (the first block can start before 0, with the pre-skip)
    const int64_t  blockStart = data.m_blocks[slot].m_sampleStart;
    const int64_t  blockEnd = std::min<int64_t>(blockStart + ac_opusState->m_decodedCount, data.m_nSamples);
    const uint64_t blockStartPhase = uint64_t(blockStart * (int64_t(1) << 32)); // two's complement wrap is fine
    const uint64_t localPhase = ac_phase - blockStartPhase;
    const uint64_t localEndPhase = uint64_t(blockEnd - blockStart) << 32;

    uint64_t nFrames;
    if (localPhase < localEndPhase)
    {
      nFrames = (localEndPhase - localPhase + step - 1) / step;
    }
    else // gap between 2 blocks (??): play silence
    {
      const uint64_t nextPhase = (slot + 1 < data.m_blocks.size()) ? uint64_t(int64_t(data.m_blocks[slot + 1].m_sampleStart)) << 32 : endPhase;
      nFrames = std::max<uint64_t>(1, (nextPhase - ac_phase + step - 1) / step);
    }
    const unsigned n = unsigned(std::min<uint64_t>(nFrames, sampleCount - done));

    if (localPhase < localEndPhase)
      _kernelRaw<true>(ac_opusState->m_decodedBuffer.data(), localPhase, step, outBufferAdd + 2 * done, n,
                       gL0 + float(done) * dgL, dgL, gR0 + float(done) * dgR, dgR);

    ac_phase += uint64_t(n) * step;
    done += n;
    if (repeat && ac_phase >= endPhase) ac_phase -= endPhase;
  }
}

// ----------------------------------------------------------------------------

bool sound2D::_decodeOpusBlock(const soundData::s_Opus &data, unsigned slot)
{
#ifdef TRE_WITH_OPUS
  TRE_ASSERT(ac_opusState != nullptr);
  s_opusState &st = *ac_opusState;

  if (st.m_decoder == nullptr)
    return false;
  if (slot >= data.m_blocks.size())
    return false;
  if (slot == st.m_decodedSlot)
    return true;

  float * __restrict buffer = st.m_decodedBuffer.data();
  const int          bufferCapacity = int(s_opusState::k_maxBlockSamples);

  // The decoder state is only valid for the next consecutive packet.
  // On a jump (seek, loop), reset it and pre-roll at least 80ms of the previous packets.
  // (unsigned(-1) + 1 == 0: a fresh decoder can start at slot 0 directly, the pre-skip handles it)
  if (slot != st.m_decodedSlot + 1)
  {
    opus_decoder_ctl(st.m_decoder, OPUS_RESET_STATE);
    unsigned first = slot;
    while (first > 0 && data.m_blocks[slot].m_sampleStart - data.m_blocks[first].m_sampleStart < s_opusState::k_preRollSamples)
      --first;
    for (unsigned s = first; s < slot; ++s)
    {
      const soundData::s_Opus::s_block &b = data.m_blocks[s];
      const int retPreRoll = opus_decode_float(st.m_decoder, b.m_data.data(), opus_int32(b.m_data.size()), buffer, bufferCapacity, 0); // output discarded
      (void)retPreRoll;
    }
  }

  const soundData::s_Opus::s_block &block = data.m_blocks[slot];
  const int ret = opus_decode_float(st.m_decoder, block.m_data.data(), opus_int32(block.m_data.size()), buffer, bufferCapacity, 0);
  if (ret <= 0)
  {
    opus_decoder_ctl(st.m_decoder, OPUS_RESET_STATE);
    st.m_decodedSlot = unsigned(-1);
    st.m_decodedCount = 0;
    return false;
  }

  st.m_decodedSlot = slot;
  st.m_decodedCount = unsigned(ret);

  // padding frame for the linear-interpolation (LR)
  buffer[ret * 2 + 0] = buffer[ret * 2 - 2];
  buffer[ret * 2 + 1] = buffer[ret * 2 - 1];

  return true;
#else
  (void)data;
  (void)slot;
  return false;
#endif // OPUS
}

// ============================================================================

} // namespace tre
