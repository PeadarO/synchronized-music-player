#include <algorithm>
#include <cmath>
#include <string>

#include <sys/time.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <gflags/gflags.h>
#include <portaudio.h>

#include "util.h"

const char* static_av_strerror(int errnum) {
  static char buffer[1024];
  av_strerror(errnum, buffer, sizeof(buffer));
  return buffer;
}

DEFINE_int64(start_us, 0, "time to start playing song in us from the Unix epoch,"
                          " or 0 for now");
DEFINE_string(host_api, "", "host api for PortAudio, or empty for default");
DEFINE_string(device, "", "device name for PortAudio, or empty for default");

DEFINE_bool(list_devices, false, "list PortAudio devices and exit");

// TERMINOLOGY USED:
// 'sample': The values from each channel sampled at a single instant.
// 'frame': Several samples, stored in a single packet.
// 'packet': A part of a media file.
// 'offset': An offset in an infinite buffer.

// CONSTANTS:
// The length of the playback buffer in bytes.
const int64_t kCyclicBufferSize = 1024 * 1024 * 4;
// The playback latency we request from PortAudio.
const PaTime kSuggestedOutputLatencySec = 0.05;
// How far back of our desired start position we will start decoding.
const int64_t kSeekSafetyBufferUs = kMillion;
const PaSampleFormat kInvalidPaSampleFormat = 0;

// With our different clocks and PortAudio storing time in double, we are bound
// to suffer from numeric instability when calculating the next offset we have
// to play or write to. We can however 'smooth' over these gaps if we assume
// that eventually our clocks will converge again: if the calculated offset
// (based on clock) and the expected offset (based on previous offset +
// previous number bytes) are sufficiently close, we will use the expected
// offset. Sufficiently close is defined by this constant.
const int32_t kMaxNextByteDelta = 32;

// GLOBAL VARIABLES:
// Information about the file format.
int32_t bytes_per_sample;
int32_t num_channels;
int32_t sample_rate;
PaSampleFormat sample_format;

// Byte to fill buffer with to output silence.
uint8_t silence_byte;

// Synchronization information. PortAudio has a per-stream clock, while we use
// Unix time to synchronize music. At the beginning of playback we sample both
// clocks, to pick an origin for both clocks at buffer position zero.
PaTime zero_pa_sec;
int64_t zero_unix_us;

// Playback information. We store offsets in an infinite buffer, though we only
// store a small slice in cyclic_buffer. The playback callback stores its
// current offset in stream_offset, which tells us where we can safely write
// the buffer.
uint8_t cyclic_buffer[kCyclicBufferSize];
int64_t stream_offset;

// Conversion functions from PortAudio seconds and Unix us to offsets.
inline int64_t ConvertPaSecToOffset(PaTime sec) {
  return static_cast<int64_t>((sec - zero_pa_sec) * sample_rate) * bytes_per_sample;
}
inline int64_t ConvertUnixUsToOffset(int64_t us) {
  return ((us - zero_unix_us) * sample_rate / 1000000) * bytes_per_sample;
}

PaSampleFormat ConvertAVToPaSampleFormat(AVSampleFormat format) {
  switch (format) {
    case AV_SAMPLE_FMT_U8: return paUInt8;
    case AV_SAMPLE_FMT_S16: return paInt16;
    case AV_SAMPLE_FMT_S32: return paInt32;
    case AV_SAMPLE_FMT_FLT: return paFloat32;
    default: return kInvalidPaSampleFormat;
  }
}

int64_t callback_next_offset = 0;
int32_t PaCallback(const void* untyped_input, void* untyped_output,
                   unsigned long num_samples,
                   const PaStreamCallbackTimeInfo* time_info,
                   PaStreamCallbackFlags status_flags,
                   void* user_data)
{
  uint8_t* const output = reinterpret_cast<uint8_t*>(untyped_output);
  int64_t offset = ConvertPaSecToOffset(time_info->outputBufferDacTime);

  const int32_t num_bytes = num_samples * bytes_per_sample;

  if (abs(offset - callback_next_offset) <= kMaxNextByteDelta) {
    offset = callback_next_offset;
  }
  callback_next_offset = offset + num_bytes;

  for (int32_t i = 0; i < num_bytes; i++) {
    output[i] = cyclic_buffer[(offset + i) % kCyclicBufferSize];
    cyclic_buffer[(offset + i) % kCyclicBufferSize] = silence_byte;
  }

  stream_offset = offset + num_bytes;
  return paContinue;
}

void ListDevices() {
  printf("PortAudio devices:\n");
  PaDeviceIndex num_devices = Pa_GetDeviceCount();
  for (PaDeviceIndex device = 0; device < num_devices; device++) {
    const PaDeviceInfo* info = Pa_GetDeviceInfo(device);
    const PaHostApiInfo* api_info = Pa_GetHostApiInfo(info->hostApi);

    const bool is_default = Pa_GetDefaultOutputDevice() == device;

    if (info->maxOutputChannels > 0) {
      printf("[%s] %s%s: maxOutputChannels=%d, defaultLowOutputLatency=%lf "
             "defaultHighOutputLatency=%lf\n", api_info->name, info->name,
             is_default ? " (default)" : "", info->maxOutputChannels,
             info->defaultLowOutputLatency, info->defaultHighOutputLatency);
    }
  }
}

PaDeviceIndex GetDeviceFromFlags() {
  PaHostApiIndex host_api = -1;
  if (FLAGS_host_api == "") {
    host_api = Pa_GetDefaultHostApi();
  } else {
    PaHostApiIndex num_host_apis = Pa_GetHostApiCount();
    for (PaHostApiIndex i = 0; i < num_host_apis; i++) {
      if (FLAGS_host_api == Pa_GetHostApiInfo(i)->name) {
        host_api = i;
      }
    }
  }
  if (host_api == -1) {
    Die("Invalid PortAudio host api");
  }

  const PaHostApiInfo* host_api_info = Pa_GetHostApiInfo(host_api);
  PaDeviceIndex device = -1;
  if (FLAGS_device == "") {
    device = host_api_info->defaultOutputDevice;
  } else {
    for (PaDeviceIndex i = 0; i < host_api_info->deviceCount; i++) {
      PaDeviceIndex index = Pa_HostApiDeviceIndexToDeviceIndex(host_api, i);
      if (FLAGS_device == Pa_GetDeviceInfo(index)->name) {
        device = index;
      }
    }
  }
  if (device == -1) {
    Die("Invalid PortAudio device");
  }
  return device;
}

int64_t play_next_offset = 0;
bool WaitForAndPlaySamplesAtUs(uint8_t* samples, int32_t num_bytes,
                               int64_t play_us) {
  int64_t offset = ConvertUnixUsToOffset(play_us);
  if (abs(offset - play_next_offset) <= kMaxNextByteDelta) {
    offset = play_next_offset;
  }
  play_next_offset = offset + num_bytes;

  // Can't play samples in the past.
  if (offset < stream_offset) {
    return false;
  }

  while (stream_offset + kCyclicBufferSize < offset + num_bytes) {
    // TODO: calculate a reasonable sleep period and buffer size
    Pa_Sleep(100);
  }

  for (int32_t i = 0; i < num_bytes; i++) {
    cyclic_buffer[(offset + i) % kCyclicBufferSize] = samples[i];
  }

  return true;
}

int32_t main(int32_t argc, char** argv) {
  std::string usage("This program plays a sound file timely. Sample usage:\n");
  usage += argv[0];
  usage += " --start_us=12345 song.mp3";
  google::SetUsageMessage(usage);
  google::SetVersionString("alpha 1");
  google::ParseCommandLineFlags(&argc, &argv, true);

  if (Pa_Initialize() != paNoError) {
    Die("Pa_Initialize failed");
  }

  if (FLAGS_list_devices) {
    ListDevices();
    if (argc < 2) {
      exit(0);
    }
  }

  if (argc != 2) {
    google::ShowUsageWithFlags(argv[0]);
    exit(0);
  }

  PaDeviceIndex output_device = GetDeviceFromFlags();

  av_log_set_flags(AV_LOG_SKIP_REPEATED);
  av_register_all();

  // Open file and initialize format_context.
  AVFormatContext* format_context = NULL;
  if (avformat_open_input(&format_context, argv[1], NULL /* file format */,
                          NULL /* options */) != 0) {
    Die("av_open_input_file failed");
  }

  // Initialize information about streams.
  if (avformat_find_stream_info(format_context, NULL /* options */) < 0) {
    Die("av_find_stream_info failed");
  }

  // Find a suitable audio stream by iterating through all streams and opening
  // the first audio stream for which we can find a codec. In case of failure,
  // audio_stream_idx is set to format_context->nb_streams.
  int32_t audio_stream_idx = 0;
  AVStream* av_stream = NULL;
  AVCodecContext* codec_context = NULL;

  const int32_t num_streams = static_cast<int32_t>(format_context->nb_streams);
  for (; audio_stream_idx < num_streams; audio_stream_idx++) {
    av_stream = format_context->streams[audio_stream_idx];
    codec_context = av_stream->codec;
    AVCodec* codec = avcodec_find_decoder(codec_context->codec_id);
    if (codec_context->codec_type == AVMEDIA_TYPE_AUDIO && codec != NULL &&
        avcodec_open2(codec_context, codec, NULL /* options */) >= 0) {
      break;
    }
  }
  if (audio_stream_idx == static_cast<int32_t>(format_context->nb_streams)) {
    Die("could not find audio stream with suitable codec (missing codecs?)");
  }

  // Verify that the audio format is understood by our code.
  if (av_sample_fmt_is_planar(codec_context->sample_fmt) &&
      codec_context->channels > 1) {
    Die("TODO: audio format is planar");
  }
  if (codec_context->channels > 2) {
    Die("TODO: audio format is not stereo or mono");
  }

  // Read stream information and prepare it for PortAudio.
  num_channels = codec_context->channels;
  sample_rate = codec_context->sample_rate;
  sample_format = ConvertAVToPaSampleFormat(codec_context->sample_fmt);
  if (sample_format == kInvalidPaSampleFormat) {
    Die("TODO: can't convert given sample format");
  }
  silence_byte = (sample_format == paUInt8) ? 0x80 : 0x00;
  bytes_per_sample = av_get_bytes_per_sample(codec_context->sample_fmt) * num_channels;

  // Setup PortAudio stream.
  PaStream* pa_stream = NULL;
  PaStreamParameters output_parameters;
  output_parameters.device = output_device;
  output_parameters.channelCount = num_channels;
  output_parameters.sampleFormat = sample_format;
  output_parameters.suggestedLatency = kSuggestedOutputLatencySec;
  output_parameters.hostApiSpecificStreamInfo = NULL;
  
  // Open and start the PortAudio stream, with a silent buffer.
  if (Pa_OpenStream(&pa_stream, NULL /* inputParameters */, &output_parameters,
                    sample_rate, paFramesPerBufferUnspecified, paNoFlag,
                    PaCallback, NULL /* userData */) != paNoError) {
    Die("Pa_OpenStream failed");
  }

  memset(cyclic_buffer, sizeof(cyclic_buffer), silence_byte);
  stream_offset = 0;
  if (Pa_StartStream(pa_stream) != paNoError) {
    Die("Pa_StartStream failed");
  }

  // Output latency information.
  const PaStreamInfo *pa_stream_info = Pa_GetStreamInfo(pa_stream);
  printf("output latency: %lf\n", pa_stream_info->outputLatency);

  // Setup synchronization information.
  zero_pa_sec = Pa_GetStreamTime(pa_stream);
  zero_unix_us = UnixUsNow();

  int64_t start_us = FLAGS_start_us == 0 ? zero_unix_us : FLAGS_start_us;

  // Setup decoder by seeking to our approximate position.
  const int64_t delta_us = zero_unix_us - start_us;

  const int64_t seek_position = (delta_us - kSeekSafetyBufferUs) *
      av_stream->time_base.den / (av_stream->time_base.num * kMillion);

  if (av_seek_frame(format_context, audio_stream_idx, seek_position,
                    AVSEEK_FLAG_BACKWARD) < 0) {
    Die("av_seek_frame failed");
  }

  // Store the position of the last sample so we can wait for it to be
  // played when exiting.
  int64_t end_offset = 0;

  // Play the stream, reading it frame by frame and decoding the frames.
  AVPacket packet;
  while (av_read_frame(format_context, &packet) >= 0) {
    if (packet.stream_index == audio_stream_idx) {
      AVFrame frame;
      int32_t got_frame;
      const int32_t ret = avcodec_decode_audio4(codec_context, &frame,
                                                &got_frame, &packet);

      if (ret < 0) {
        got_frame = 0;
        Warn("avcodec_decode_audio4: %s", static_av_strerror(ret));
      } else if (ret != packet.size) {
        Die("TODO: multiple frames in a single packet");
      }

      if (got_frame) {
        const int64_t num_bytes = frame.nb_samples * bytes_per_sample;
        if (num_bytes > kCyclicBufferSize) {
          Die("single decoded frame does not fit in cyclic_buffer");
        }

        const int64_t stream_us = frame.pkt_pts * kMillion *
            av_stream->time_base.num / av_stream->time_base.den;
        const int64_t play_us = start_us + stream_us;

        WaitForAndPlaySamplesAtUs(frame.data[0], num_bytes, play_us);
        end_offset = ConvertUnixUsToOffset(play_us) + num_bytes;
      }
    }
    av_free_packet(&packet);
  }

  // Clean up libavcodec state.
  avcodec_close(codec_context);
  avformat_close_input(&format_context);

  // Wait for playback to finish.
  while (stream_offset < end_offset) {
    Pa_Sleep(100);
  }

  // Clean up PortAudio state.
  if (Pa_StopStream(pa_stream) != paNoError) {
    Die("Pa_StopStream failed");
  }
  Pa_Terminate();

  return 0;
}

