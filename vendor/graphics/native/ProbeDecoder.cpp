#include "ProbeDecoder.h"
#include "gl2_opcodes.h"
#include <GLES2/gl2.h>
#include <cstring>

namespace {
uint32_t word(const std::vector<unsigned char>& packet, size_t offset) {
    uint32_t value;
    std::memcpy(&value, packet.data() + offset, sizeof(value));
    return value;
}

size_t commandSize(uint32_t opcode) {
    switch (opcode) {
        case OP_glViewport: case OP_glScissor: case OP_glClearColor: return 24;
        case OP_glGetIntegerv: return 16;
        case OP_glEnable: case OP_glDisable: case OP_glClear: return 12;
        case OP_glGetError: case OP_glFlush: case OP_glFinishRoundTrip: return 8;
        case OP_glReadPixels: return 36;
        default: throw std::runtime_error("Command outside P1 GLES subset: " + std::to_string(opcode));
    }
}
}

ProbeDecoder::ProbeDecoder(std::function<void()> frameBoundary) {
    if (!checksum_.setVersion(1)) throw std::runtime_error("Checksum v1 missing");
    decoder_.glViewport = ::glViewport;
    decoder_.glScissor = ::glScissor;
    decoder_.glClearColor = ::glClearColor;
    decoder_.glClear = ::glClear;
    decoder_.glEnable = ::glEnable;
    decoder_.glDisable = ::glDisable;
    decoder_.glGetError = ::glGetError;
    decoder_.glGetIntegerv = ::glGetIntegerv;
    decoder_.glReadPixels = ::glReadPixels;
    decoder_.glFlush = ::glFlush;
    decoder_.frameBoundary = std::move(frameBoundary);
    decoder_.glFinishRoundTrip = [](void* context) -> int {
        ::glFinish();
        auto& boundary = static_cast<Context*>(static_cast<gl2_decoder_context_t*>(context))->frameBoundary;
        if (boundary) boundary();
        return 0;
    };
}

bool ProbeDecoder::next(SocketStream& stream) {
    uint32_t header[2]{};
    if (!stream.readOrEof(header, sizeof(header))) return false;
    const auto opcode = header[0];
    const auto size = commandSize(opcode) + checksum_.checksumByteSize();
    if (header[1] != size) throw std::runtime_error("Invalid P1 command length");
    std::vector<unsigned char> packet(size);
    std::memcpy(packet.data(), header, sizeof(header));
    stream.readFully(packet.data() + sizeof(header), size - sizeof(header));
    if (opcode == OP_glGetIntegerv) {
        const auto name = word(packet, 8);
        const auto bytes = word(packet, 12);
        if (!((name == GL_VIEWPORT && bytes == 4 * sizeof(GLint)) ||
              (name == GL_MAX_VERTEX_ATTRIBS && bytes == sizeof(GLint)))) {
            throw std::runtime_error("Query outside P1 GLES subset");
        }
    }
    if (opcode == OP_glReadPixels &&
        (word(packet, 16) != 1 || word(packet, 20) != 1 || word(packet, 24) != GL_RGBA ||
         word(packet, 28) != GL_UNSIGNED_BYTE || word(packet, 32) != 4)) {
        throw std::runtime_error("Readback outside P1 RGBA pixel subset");
    }
    if (decoder_.decode(packet.data(), packet.size(), &stream, &checksum_) != size) {
        throw std::runtime_error("P1 command was not fully decoded");
    }
    ++packets_;
    if (opcode == OP_glFinishRoundTrip) ++frames_;
    return true;
}
