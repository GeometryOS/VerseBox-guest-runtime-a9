#include "DesktopDecoder.h"
#include "GpuBackend.h"
#include "gl2_opcodes.h"
#include "renderControl_opcodes.h"
#include <GLES3/gl3.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>
#include <type_traits>
#include <unistd.h>

namespace {
void require(bool passed, const char* name) {
    if (!passed) throw std::runtime_error(name);
    std::printf("CHECK\t%s\t1\n", name);
}

template<class Result = uint32_t>
Result request(DesktopDecoder& decoder, SocketStream& input, SocketStream& peer,
        ChecksumCalculator& checksum, uint32_t opcode, std::initializer_list<uint32_t> parameters) {
    const auto checksumSize = checksum.checksumByteSize();
    std::vector<uint32_t> packet{opcode, static_cast<uint32_t>((2 + parameters.size()) * 4 + checksumSize)};
    packet.insert(packet.end(), parameters);
    std::vector<unsigned char> bytes(packet[1]);
    std::memcpy(bytes.data(), packet.data(), packet.size() * 4);
    checksum.addBuffer(bytes.data(), packet.size() * 4);
    checksum.writeChecksum(bytes.data() + packet.size() * 4, checksumSize);
    peer.writeFully(bytes.data(), bytes.size());
    require(decoder.next(input), "sync_packet_decoded");
    if constexpr (!std::is_void_v<Result>) {
        Result result;
        peer.readFully(&result, sizeof(result));
        checksum.addBuffer(&result, sizeof(result));
        peer.readFully(bytes.data(), checksumSize);
        require(checksum.validate(bytes.data(), checksumSize), "sync_reply_checksum");
        return result;
    }
}

void check(bool directCopy) {
    GpuBackend backend(8, 8, directCopy);
    GpuBackend::setProcess(getpid());
    DesktopDecoder decoder(backend), consumer(backend);
    int sockets[2];
    require(!socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), "sync_socket_pair");
    SocketStream input(sockets[0], 65536, 0, true), peer(sockets[1], 65536, 0, true);
    ChecksumCalculator checksum, otherChecksum;
    checksum.setVersion(1);
    otherChecksum.setVersion(1);
    const auto context = request(decoder, input, peer, checksum, OP_rcCreateContext, {0, 0, 2});
    const auto surface = request(decoder, input, peer, checksum, OP_rcCreateWindowSurface, {0, 8, 8});
    require(context && surface && request(decoder, input, peer, checksum, OP_rcMakeCurrent,
        {context, surface, surface}), "sync_context_current");
    const auto fence = request<uint64_t>(decoder, input, peer, checksum, OP_glFenceSyncAEMU,
        {GL_SYNC_GPU_COMMANDS_COMPLETE, 0});
    require(fence != 0, "sync_fence_created");
    const auto low = static_cast<uint32_t>(fence), high = static_cast<uint32_t>(fence >> 32);
    require(request<GLboolean>(consumer, input, peer, otherChecksum, OP_glIsSyncAEMU, {low, high}),
        "sync_shared_between_decoders");
    const auto result = request(decoder, input, peer, checksum, OP_glClientWaitSyncAEMU,
        {low, high, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000, 0});
    require(result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED, "sync_gpu_completed");
    const auto query = request<uint64_t>(consumer, input, peer, otherChecksum, OP_glGetSyncivAEMU,
        {low, high, GL_SYNC_STATUS, 1, 4, 4});
    require(static_cast<uint32_t>(query) == 1 && (query >> 32) == GL_SIGNALED, "sync_status_signaled");
    request<void>(consumer, input, peer, otherChecksum, OP_glWaitSyncAEMU, {low, high, 0, UINT32_MAX, UINT32_MAX});
    request<void>(consumer, input, peer, otherChecksum, OP_glDeleteSyncAEMU, {low, high});
    require(!request<GLboolean>(decoder, input, peer, checksum, OP_glIsSyncAEMU, {low, high}),
        "sync_deleted");
    require(request(decoder, input, peer, checksum, OP_glGetError, {}) == GL_NO_ERROR,
        "sync_no_gl_error");
    require(!request<uint64_t>(decoder, input, peer, checksum, OP_glFenceSyncAEMU, {0, 0}) &&
        request(decoder, input, peer, checksum, OP_glGetError, {}) == GL_INVALID_ENUM,
        "sync_invalid_condition_error");
    require(request(decoder, input, peer, checksum, OP_rcMakeCurrent, {0, 0, 0}), "sync_context_released");
    backend.releaseProcess(getpid());
    std::printf("desktop_sync_gles%d=1\n", directCopy ? 3 : 2);
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try { check(true); check(false); }
    catch (const std::exception& error) { std::fprintf(stderr, "desktop_sync_error=%s\n", error.what()); return 1; }
    return 0;
}
