#include "ProbeDecoder.h"
#include "ProbeProtocol.h"
#include "ProcessIdentity.h"
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
class Pbuffer {
public:
    void initialize() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0, count = 0;
        if (!eglInitialize(display_, &major, &minor)) fail("eglInitialize");
        const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        if (!eglChooseConfig(display_, attributes, &config, 1, &count) || count != 1) fail("eglChooseConfig");
        const EGLint size[] = {EGL_WIDTH, probe::width, EGL_HEIGHT, probe::height, EGL_NONE};
        surface_ = eglCreatePbufferSurface(display_, config, size);
        const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, version);
        if (surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT ||
            !eglMakeCurrent(display_, surface_, surface_, context_)) fail("eglMakeCurrent");
        std::printf("HOST_GL egl=%d.%d vendor=%s renderer=%s version=%s\n", major, minor,
                    glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION));
    }
    ~Pbuffer() {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
            eglTerminate(display_);
        }
    }
private:
    [[noreturn]] void fail(const char* operation) {
        throw std::runtime_error(std::string(operation) + " error=" + std::to_string(eglGetError()));
    }
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
};
}

int main(int argc, char** argv) {
    int listener = -1;
    try {
        if (argc < 2 || argc > 3) throw std::invalid_argument("Usage: codec-host SOCKET [NONCE]");
        guardParent();
        Pbuffer target;
        target.initialize();
        listener = listenLocal(argv[1]);
        printIdentity(argc == 3 ? argv[2] : "native", false);
        std::puts("HOST_READY");
        std::fflush(stdout);
        pollfd waiting{listener, POLLIN, 0};
        int ready;
        do { ready = poll(&waiting, 1, 5000); } while (ready < 0 && errno == EINTR);
        if (ready <= 0) throw std::runtime_error("GL client connection timeout");
        SocketStream stream(accept4(listener, nullptr, nullptr, SOCK_CLOEXEC));
        probe::handshake(stream, false);
        ProbeDecoder decoder;
        while (decoder.next(stream)) {}
        if (decoder.frames() != probe::frames) throw std::runtime_error("Incomplete GL frame sequence");
        std::printf("HOST_OK frames=%u packets=%u codec=%s\n", decoder.frames(), decoder.packets(), A10VM_CODEC_ID);
        close(listener);
        unlink(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "HOST_ERROR %s\n", error.what());
        if (listener >= 0) { close(listener); unlink(argv[1]); }
        return 1;
    }
}
