#include "GL2Encoder.h"
#include "ProbeProtocol.h"
#include "ProcessIdentity.h"
#include "WindowClient.h"
#include "WindowProtocol.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {
class ProbeEncoder final : public GL2Encoder {
public:
    using GL2Encoder::GL2Encoder;
    uint64_t lockAndWriteDma(void*, uint32_t) override {
        throw std::runtime_error("DMA encoding is not enabled for the probe");
    }
};

void checkError(ProbeEncoder& encoder) {
    const auto error = encoder.glGetError(&encoder);
    if (error != GL_NO_ERROR) throw std::runtime_error("Guest GL error " + std::to_string(error));
}

void drawAndRead(ProbeEncoder& encoder, int frame) {
    const std::array<unsigned char, 4> expected{
        static_cast<unsigned char>((frame & 1) ? 255 : 0),
        static_cast<unsigned char>((frame & 2) ? 255 : 0),
        static_cast<unsigned char>((frame & 4) ? 255 : 0), 255};
    encoder.glDisable(&encoder, GL_SCISSOR_TEST);
    encoder.glClearColor(&encoder, expected[0] / 255.f, expected[1] / 255.f,
                        expected[2] / 255.f, 1.f);
    encoder.glClear(&encoder, GL_COLOR_BUFFER_BIT);
    encoder.glEnable(&encoder, GL_SCISSOR_TEST);
    encoder.glScissor(&encoder, 0, 0, 8, 8);
    encoder.glClearColor(&encoder, 1.f, 1.f, 1.f, 1.f);
    encoder.glClear(&encoder, GL_COLOR_BUFFER_BIT);
    std::array<unsigned char, 4> actual{};
    encoder.glReadPixels(&encoder, 32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    if (actual != expected) throw std::runtime_error("Encoded color pixel mismatch at frame " + std::to_string(frame));
    encoder.glReadPixels(&encoder, 4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    if (actual != std::array<unsigned char, 4>{255, 255, 255, 255}) {
        throw std::runtime_error("Encoded scissor pixel mismatch");
    }
    encoder.glFinish(&encoder);
    checkError(encoder);
}

}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    try {
        const bool window = argc == 5 && std::strcmp(argv[1], "--window") == 0;
        if (!window && (argc < 2 || argc > 3)) {
            throw std::invalid_argument("Usage: codec-client SOCKET [NONCE] | --window GL_SOCKET INPUT_SOCKET NONCE");
        }
        guardParent();
        SocketStream stream(connectLocal(argv[window ? 2 : 1]));
        probe::handshake(stream, true);
        std::unique_ptr<SocketStream> control;
        if (window) {
            control = std::make_unique<SocketStream>(connectLocal(argv[3]), 64 * 1024, 0);
            probe::handshake(*control, true);
        }
        ChecksumCalculator checksum;
        if (!checksum.setVersion(1)) throw std::runtime_error("Checksum v1 missing");
        GLClientState state(2, 0);
        ProbeEncoder encoder(&stream, &checksum);
        encoder.setClientState(&state);
        encoder.setSharedGroup(GLSharedGroupPtr(new GLSharedGroup()));
        encoder.setVersion(2, 0, 2, 0);
        GLint attributes = 0;
        encoder.glGetIntegerv(&encoder, GL_MAX_VERTEX_ATTRIBS, &attributes);
        if (attributes < 8) throw std::runtime_error("Host lacks GLES 2 vertex attributes");
        // GLES 2 exposes one draw buffer; GLES 3 indexed-buffer features are disabled.
        state.initFromCaps(0, 0, 0, 0, attributes, 1, 1);
        encoder.setClientStateMakeCurrent(&state, 2, 0, 2, 0);
        encoder.setInitialized();
        const int width = window ? windowprobe::width : probe::width;
        const int height = window ? windowprobe::height : probe::height;
        encoder.glViewport(&encoder, 0, 0, width, height);
        GLint viewport[4]{};
        encoder.glGetIntegerv(&encoder, GL_VIEWPORT, viewport);
        if (viewport[2] != width || viewport[3] != height) {
            throw std::runtime_error("Host GL viewport round-trip mismatch");
        }
        if (window) {
            printIdentity(argv[4]);
            int result = runWindowClient(encoder, *control);
            std::puts("identity_stage=exit");
            printIdentity(argv[4]);
            return result;
        }
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < probe::frames; ++frame) drawAndRead(encoder, frame);
        const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        printIdentity(argc == 3 ? argv[2] : "native");
        std::printf("CLIENT_OK frames=%d pixels=%d elapsed_ms=%.3f codec=%s\n",
                    probe::frames, probe::frames * 2, elapsed, A10VM_CODEC_ID);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "CLIENT_ERROR %s\n", error.what());
        return 1;
    }
}
