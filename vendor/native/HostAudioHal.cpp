#include <hardware/audio.h>
#include "HostAudioPcm.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <atomic>
#include <mutex>
#include <vector>

namespace {
std::atomic<bool> micMuted{false};
struct Stream {
    union { audio_stream_out out; audio_stream_in in; };
    audio_config config{};
    audio_devices_t device{};
    bool capture = false;
    int socket = -1;
    uint64_t writtenFrames = 0;
    uint64_t presentationBase = 0;
    std::vector<unsigned char> hostPcm;
    std::mutex mutex;
    ~Stream() { if (socket >= 0) close(socket); }
};
Stream* state(const audio_stream* stream) { return reinterpret_cast<Stream*>(const_cast<audio_stream*>(stream)); }

bool transfer(int fd, void* data, size_t bytes, bool sendData) {
    auto* buffer = static_cast<char*>(data);
    while (bytes) {
        ssize_t result = sendData ? send(fd, buffer, bytes, MSG_NOSIGNAL) : recv(fd, buffer, bytes, 0);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        buffer += result; bytes -= result;
    }
    return true;
}
bool put(int fd, uint32_t value) { value = htonl(value); return transfer(fd, &value, 4, true); }
int get(int fd) { uint32_t value; return transfer(fd, &value, 4, false) ? static_cast<int32_t>(ntohl(value)) : -EPIPE; }
bool getLong(int fd, uint64_t* value) {
    uint32_t words[2];
    if (!transfer(fd, words, 8, false)) return false;
    *value = (static_cast<uint64_t>(ntohl(words[0])) << 32) | ntohl(words[1]);
    return true;
}
size_t frameBytes(const Stream* s) {
    unsigned channels = s->capture ? audio_channel_count_from_in_mask(s->config.channel_mask) : audio_channel_count_from_out_mask(s->config.channel_mask);
    return channels * audio_bytes_per_sample(s->config.format);
}
size_t bufferSize(const Stream* s) { return (s->config.sample_rate / 100 + 15) / 16 * 16 * frameBytes(s); }
size_t hostSampleBytes(const Stream* s) {
    return hostPcm16Conversion(s->config.format) ? 2 : audio_bytes_per_sample(s->config.format);
}
size_t hostBufferSize(const Stream* s) {
    return bufferSize(s) / audio_bytes_per_sample(s->config.format) * hostSampleBytes(s);
}
int connectHost(Stream* s) {
    if (s->socket >= 0) return 0;
    const char* root = getenv("A10VM_STATE_ROOT");
    if (!root) return -ENODEV;
    sockaddr_un address{}; address.sun_family = AF_UNIX;
    if (snprintf(address.sun_path, sizeof(address.sun_path), "%s/audio.sock", root) >= static_cast<int>(sizeof(address.sun_path))) return -ENAMETOOLONG;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;
    timeval timeout{5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) { int error = -errno; close(fd); return error; }
    int encoding = s->config.format == AUDIO_FORMAT_PCM_FLOAT ? 4 : s->config.format == AUDIO_FORMAT_PCM_8_BIT ? 3 : 2;
    unsigned channels = s->capture ? audio_channel_count_from_in_mask(s->config.channel_mask) : audio_channel_count_from_out_mask(s->config.channel_mask);
    bool sent = put(fd, 0x41313041) && put(fd, 1) && put(fd, s->capture) && put(fd, s->config.sample_rate) &&
            put(fd, channels) && put(fd, encoding) && put(fd, hostBufferSize(s));
    int result = sent ? get(fd) : -EPIPE;
    if (result == 0 && get(fd) <= 0) result = -EIO;
    if (result < 0) { close(fd); return result; }
    s->socket = fd;
    return 0;
}
int standby(audio_stream* stream) {
    Stream* s = state(stream); std::lock_guard<std::mutex> lock(s->mutex);
    // AudioFlinger subtracts its cumulative standby frame count from this HAL's
    // presentation position; a replacement host AudioTrack starts at zero.
    if (!s->capture) s->presentationBase = s->writtenFrames;
    if (s->socket >= 0) { close(s->socket); s->socket = -1; }
    return 0;
}
ssize_t pcm(Stream* s, void* data, size_t bytes) {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (!bytes || bytes > 65536 || bytes % frameBytes(s)) return -EINVAL;
    int result = connectHost(s);
    if (result < 0) return result;
    bool converted = hostPcm16Conversion(s->config.format);
    size_t hostBytes = converted ? bytes / audio_bytes_per_sample(s->config.format) * 2 : bytes;
    void* hostData = data;
    if (converted) {
        s->hostPcm.resize(hostBytes);
        hostData = s->hostPcm.data();
        if (!s->capture) toHostPcm16(data, hostData, hostBytes / 2, s->config.format);
    }
    if (s->capture && (!put(s->socket, 5) || !put(s->socket, micMuted.load()) || get(s->socket) < 0)) return -EPIPE;
    if (!put(s->socket, s->capture ? 2 : 1) || !put(s->socket, hostBytes)) return -EPIPE;
    if (!s->capture && !transfer(s->socket, hostData, hostBytes, true)) return -EPIPE;
    result = get(s->socket);
    if (result > static_cast<int>(hostBytes)) return -EPROTO;
    if (result > 0 && result % (frameBytes(s) / audio_bytes_per_sample(s->config.format) * hostSampleBytes(s)))
        return -EPROTO;
    if (s->capture && result > 0 && !transfer(s->socket, hostData, result, false)) return -EPIPE;
    if (converted && result > 0) {
        if (s->capture) fromHostPcm16(hostData, data, result / 2, s->config.format);
        result = result / 2 * audio_bytes_per_sample(s->config.format);
    }
    if (!s->capture && result > 0) s->writtenFrames += result / frameBytes(s);
    return result;
}
int position(Stream* s, uint64_t* frames, timespec* timestamp, bool lifetime = false) {
    std::lock_guard<std::mutex> lock(s->mutex);
    if (s->socket < 0) return -ENODATA;
    if (!put(s->socket, 3)) return -EPIPE;
    int result = get(s->socket); uint64_t nanos;
    if (result < 0) return result;
    if (!getLong(s->socket, frames) || !getLong(s->socket, &nanos)) return -EPIPE;
    if (lifetime) *frames += s->presentationBase;
    timestamp->tv_sec = nanos / 1000000000ULL; timestamp->tv_nsec = nanos % 1000000000ULL;
    return 0;
}
void common(Stream* s) {
    auto& c = s->out.common;
    c.get_sample_rate = [](const audio_stream* a) { return state(a)->config.sample_rate; };
    c.set_sample_rate = [](audio_stream* a, uint32_t rate) { return state(a)->config.sample_rate == rate ? 0 : -ENOSYS; };
    c.get_buffer_size = [](const audio_stream* a) { return bufferSize(state(a)); };
    c.get_channels = [](const audio_stream* a) { return state(a)->config.channel_mask; };
    c.get_format = [](const audio_stream* a) { return state(a)->config.format; };
    c.set_format = [](audio_stream* a, audio_format_t format) { return state(a)->config.format == format ? 0 : -ENOSYS; };
    c.standby = standby;
    c.dump = [](const audio_stream*, int fd) { dprintf(fd, "A10VM host PCM stream\n"); return 0; };
    c.get_device = [](const audio_stream* a) { return state(a)->device; };
    c.set_device = [](audio_stream* a, audio_devices_t device) { state(a)->device = device; return 0; };
    c.set_parameters = [](audio_stream*, const char*) { return 0; };
    c.get_parameters = [](const audio_stream*, const char*) { return strdup(""); };
    c.add_audio_effect = [](const audio_stream*, effect_handle_t) { return -ENOSYS; };
    c.remove_audio_effect = c.add_audio_effect;
}
int configuration(audio_config* config, bool capture) {
    if (!config->sample_rate) config->sample_rate = 48000;
    if (!config->channel_mask) config->channel_mask = capture ? AUDIO_CHANNEL_IN_MONO : AUDIO_CHANNEL_OUT_STEREO;
    if (!config->format) config->format = AUDIO_FORMAT_PCM_16_BIT;
    unsigned channels = capture ? audio_channel_count_from_in_mask(config->channel_mask) : audio_channel_count_from_out_mask(config->channel_mask);
    if (channels < 1 || channels > 2 || config->sample_rate < 8000 || config->sample_rate > 192000 ||
            (config->format != AUDIO_FORMAT_PCM_16_BIT && config->format != AUDIO_FORMAT_PCM_FLOAT &&
             config->format != AUDIO_FORMAT_PCM_8_BIT && !hostPcm16Conversion(config->format))) return -EINVAL;
    return 0;
}
int openOutput(audio_hw_device*, audio_io_handle_t, audio_devices_t device, audio_output_flags_t flags,
               audio_config* config, audio_stream_out** output, const char*) {
    if (flags & (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD | AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) return -ENOSYS;
    int result = configuration(config, false); if (result) return result;
    auto* s = new Stream{}; s->config = *config; s->device = device; common(s);
    s->out.get_latency = [](const audio_stream_out*) -> uint32_t { return 40; };
    s->out.write = [](audio_stream_out* a, const void* data, size_t bytes) { return pcm(reinterpret_cast<Stream*>(a), const_cast<void*>(data), bytes); };
    s->out.set_volume = [](audio_stream_out* a, float left, float right) {
        auto* s = reinterpret_cast<Stream*>(a); std::lock_guard<std::mutex> lock(s->mutex);
        int result = connectHost(s); if (result < 0) return result;
        uint32_t l, r; memcpy(&l, &left, 4); memcpy(&r, &right, 4);
        return put(s->socket, 4) && put(s->socket, l) && put(s->socket, r) ? get(s->socket) : -EPIPE;
    };
    s->out.get_render_position = [](const audio_stream_out* a, uint32_t* frames) {
        uint64_t value; timespec time; int result = position(reinterpret_cast<Stream*>(const_cast<audio_stream_out*>(a)), &value, &time);
        if (!result) *frames = value; return result;
    };
    s->out.get_presentation_position = [](const audio_stream_out* a, uint64_t* frames, timespec* time) {
        return position(reinterpret_cast<Stream*>(const_cast<audio_stream_out*>(a)), frames, time, true);
    };
    s->out.get_next_write_timestamp = [](const audio_stream_out*, int64_t*) { return -ENOSYS; };
    *output = &s->out; return 0;
}
int openInput(audio_hw_device*, audio_io_handle_t, audio_devices_t device, audio_config* config,
              audio_stream_in** input, audio_input_flags_t flags, const char*, audio_source_t) {
    if (flags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) return -ENOSYS;
    int result = configuration(config, true); if (result) return result;
    auto* s = new Stream{}; s->config = *config; s->device = device; s->capture = true; common(s);
    s->in.set_gain = [](audio_stream_in*, float) { return -ENOSYS; };
    s->in.read = [](audio_stream_in* a, void* data, size_t bytes) { return pcm(reinterpret_cast<Stream*>(a), data, bytes); };
    s->in.get_input_frames_lost = [](audio_stream_in*) -> uint32_t { return 0; };
    s->in.get_capture_position = [](const audio_stream_in* a, int64_t* frames, int64_t* time) {
        uint64_t value; timespec timestamp; int result = position(reinterpret_cast<Stream*>(const_cast<audio_stream_in*>(a)), &value, &timestamp);
        if (!result) { *frames = value; *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec; } return result;
    };
    *input = &s->in; return 0;
}
int openDevice(const hw_module_t* module, const char* name, hw_device_t** output) {
    if (strcmp(name, AUDIO_HARDWARE_INTERFACE)) return -EINVAL;
    auto* device = new audio_hw_device{};
    device->common.tag = HARDWARE_DEVICE_TAG; device->common.version = AUDIO_DEVICE_API_VERSION_2_0;
    device->common.module = const_cast<hw_module_t*>(module);
    device->common.close = [](hw_device_t* a) { delete reinterpret_cast<audio_hw_device*>(a); return 0; };
    device->init_check = [](const audio_hw_device*) { return getenv("A10VM_STATE_ROOT") ? 0 : -ENODEV; };
    device->set_voice_volume = [](audio_hw_device*, float) { return -ENOSYS; };
    device->set_master_volume = device->set_voice_volume;
    device->get_master_volume = [](audio_hw_device*, float*) { return -ENOSYS; };
    device->set_mode = [](audio_hw_device*, audio_mode_t) { return 0; };
    device->set_mic_mute = [](audio_hw_device*, bool mute) { micMuted.store(mute); return 0; };
    device->get_mic_mute = [](const audio_hw_device*, bool* mute) { *mute = micMuted.load(); return 0; };
    device->set_parameters = [](audio_hw_device*, const char*) { return 0; };
    device->get_parameters = [](const audio_hw_device*, const char*) { return strdup(""); };
    device->get_input_buffer_size = [](const audio_hw_device*, const audio_config* config) {
        Stream s{}; s.config = *config; s.capture = true; return configuration(&s.config, true) ? size_t{0} : bufferSize(&s);
    };
    device->open_output_stream = openOutput; device->open_input_stream = openInput;
    device->close_output_stream = [](audio_hw_device*, audio_stream_out* a) { delete reinterpret_cast<Stream*>(a); };
    device->close_input_stream = [](audio_hw_device*, audio_stream_in* a) { delete reinterpret_cast<Stream*>(a); };
    device->dump = [](const audio_hw_device*, int fd) { dprintf(fd, "A10VM host AudioTrack/AudioRecord\n"); return 0; };
    device->set_master_mute = [](audio_hw_device*, bool) { return -ENOSYS; };
    device->get_master_mute = [](audio_hw_device*, bool*) { return -ENOSYS; };
    *output = &device->common; return 0;
}
hw_module_methods_t methods{openDevice};
}
extern "C" {
audio_module HAL_MODULE_INFO_SYM = {{HARDWARE_MODULE_TAG, AUDIO_MODULE_API_VERSION_0_1, HARDWARE_HAL_API_VERSION,
        AUDIO_HARDWARE_MODULE_ID, "A10VM host PCM audio", "A10VM", &methods, nullptr, {0}}};
}
