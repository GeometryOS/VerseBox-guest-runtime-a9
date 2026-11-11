#include "FramebufferCopyCheck.h"
#include "GpuBackend.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool value, const char* operation) {
    if (!value) throw std::runtime_error(operation);
}

int config(GpuBackend& backend, int alpha, bool multisample = false) {
    for (unsigned i = 0; i < backend.configs().size(); ++i) {
        EGLint a, r, g, b, samples;
        const auto selected = backend.configs()[i];
        eglGetConfigAttrib(backend.display(), selected, EGL_ALPHA_SIZE, &a);
        eglGetConfigAttrib(backend.display(), selected, EGL_RED_SIZE, &r);
        eglGetConfigAttrib(backend.display(), selected, EGL_GREEN_SIZE, &g);
        eglGetConfigAttrib(backend.display(), selected, EGL_BLUE_SIZE, &b);
        eglGetConfigAttrib(backend.display(), selected, EGL_SAMPLES, &samples);
        if (a == alpha && r == 8 && g == 8 && b == 8 && (samples > 0) == multisample) return i;
    }
    return -1;
}

void checkConfig(GpuBackend& backend, int selected, bool multisample) {
    const auto context = backend.context(selected, 0, 2);
    const auto surface = backend.surface(selected, 4, 4);
    const auto color = backend.colorBuffer(4, 4, GL_RGBA);
    backend.setColorBuffer(surface, color);
    require(context && surface && color && backend.makeCurrent(context, surface, surface), "Copy test context");
    EGLint version, alpha;
    eglQueryContext(backend.display(), eglGetCurrentContext(), EGL_CONTEXT_CLIENT_VERSION, &version);
    eglGetConfigAttrib(backend.display(), backend.configs()[selected], EGL_ALPHA_SIZE, &alpha);
    glClearColor(1.f, 0.f, 0.f, .25f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 4, 2);
    glClearColor(0.f, 0.f, 1.f, .5f);
    glClear(GL_COLOR_BUFFER_BIT);
    GLuint texture, draw, read;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGenFramebuffers(1, &draw);
    glGenFramebuffers(1, &read);
    backend.bindFramebuffer(GL_FRAMEBUFFER, draw);
    if (version >= 3) glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    glViewport(1, 2, 3, 4);
    const auto before = backend.copyStats();
    require(backend.flushColorBuffer(surface) == 0, "Copy test flush");
    const auto after = backend.copyStats();
    const bool direct = backend.directCopySupported() && version >= 3 && !multisample;
    require(after.direct - before.direct == (direct ? 1 : 0) &&
            after.legacy - before.legacy == (direct ? 0 : 1), "Copy path counter");
    GLint drawBinding, readBinding, textureBinding, viewport[4], scissor[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &drawBinding);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &textureBinding);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    const GLint expectedViewport[] = {1, 2, 3, 4}, expectedScissor[] = {0, 0, 4, 2};
    require(static_cast<GLuint>(drawBinding) == draw && static_cast<GLuint>(textureBinding) == texture &&
            glIsEnabled(GL_SCISSOR_TEST) && !std::memcmp(viewport, expectedViewport, sizeof(viewport)) &&
            !std::memcmp(scissor, expectedScissor, sizeof(scissor)), "Copy GL state restoration");
    if (version >= 3) {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readBinding);
        require(static_cast<GLuint>(readBinding) == read, "Copy read framebuffer restoration");
    }
    unsigned char pixel[4];
    backend.readColor(color, 2, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 &&
            pixel[3] == (alpha ? 64 : 255), "Copy top row or alpha");
    backend.readColor(color, 2, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255 &&
            pixel[3] == (alpha ? 128 : 255), "Copy bottom row or alpha");
    backend.bindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &draw);
    glDeleteFramebuffers(1, &read);
    glDeleteTextures(1, &texture);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(1.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    require(!backend.flushColorBuffer(surface), "Copy present initial flush");
    backend.post(color);
    for (int frame = 0; frame < 16; ++frame) {
        const unsigned char value = frame % 2 ? 255 : 0;
        glClearColor(value / 255.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        require(!backend.flushColorBuffer(surface), "Copy present rewrite");
        backend.readPresentedPixel(16, 16, pixel);
        require(pixel[0] == 255 - value && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255,
                "Previous presentation changed during rewrite");
        backend.post(color);
    }
    backend.makeCurrent(0, 0, 0);
    backend.destroySurface(surface);
    backend.destroyContext(context);
    backend.closeColorBuffer(color);
    std::printf("desktop_copy_config_alpha=%d samples=%d direct=%d state=1 rows=1\n", alpha, multisample, direct);
}

void checkResize(GpuBackend& backend, int selected) {
    const auto context = backend.context(selected, 0, 2);
    const auto surface = backend.surface(selected, 4, 4);
    require(context && surface && backend.makeCurrent(context, surface, surface), "Resize test context");
    for (const auto size : {std::pair<int, int>{4, 4}, {8, 6}, {3, 9}, {7, 5}}) {
        const auto color = backend.colorBuffer(size.first, size.second, GL_RGBA);
        backend.setColorBuffer(surface, color);
        EGLint width, height;
        eglQuerySurface(backend.display(), eglGetCurrentSurface(EGL_DRAW), EGL_WIDTH, &width);
        eglQuerySurface(backend.display(), eglGetCurrentSurface(EGL_DRAW), EGL_HEIGHT, &height);
        require(width == size.first && height == size.second, "EGL surface did not follow resized window buffer");
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_SCISSOR_TEST);
        glScissor(size.first - 1, 0, 1, size.second);
        glClearColor(0.f, 0.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        require(!backend.flushColorBuffer(surface), "Resize test flush");
        unsigned char pixel[4];
        backend.readColor(color, size.first - 1, size.second / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        require(pixel[0] == 0 && pixel[2] == 255, "Resized window edge was stretched or clipped");
        backend.readColor(color, size.first - 2, size.second / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
        require(pixel[0] == 255 && pixel[2] == 0, "Resized window edge changed width");
        glDisable(GL_SCISSOR_TEST);
        backend.closeColorBuffer(color);
    }
    backend.makeCurrent(0, 0, 0);
    backend.destroySurface(surface);
    backend.destroyContext(context);
    std::puts("desktop_copy_window_resize=1");
}

void benchmark(bool direct, int alpha) {
    GpuBackend backend(1080, 1920, direct);
    const int selected = config(backend, alpha);
    require(selected >= 0, "Benchmark RGB/RGBA config");
    const auto context = backend.context(selected, 0, 2);
    const auto surface = backend.surface(selected, 1080, 1920);
    const auto color = backend.colorBuffer(1080, 1920, GL_RGBA);
    backend.setColorBuffer(surface, color);
    require(context && surface && color && backend.makeCurrent(context, surface, surface), "Benchmark context");
    auto render = [&](int frame) {
        glClearColor(frame % 2 ? 1.f : 0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        require(!backend.flushColorBuffer(surface), "Benchmark flush");
    };
    unsigned char pixel[4];
    for (int frame = 0; frame < 8; ++frame) render(frame);
    backend.readColor(color, 540, 960, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const auto started = std::chrono::steady_clock::now();
    constexpr int frames = 120;
    for (int frame = 0; frame < frames; ++frame) render(frame);
    // Drain root's dependency and GPU work, rather than timing only submission.
    backend.readColor(color, 540, 960, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    require(pixel[0] == 255 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255, "Benchmark final pixel");
    const auto stats = backend.copyStats();
    require(direct ? stats.direct == frames + 8 : stats.legacy == frames + 8, "Benchmark selected path");
    std::printf("desktop_copy_benchmark mode=%s alpha=%d width=1080 height=1920 frames=%d total_ms=%.3f ms_per_frame=%.3f\n",
                direct ? "blit" : "legacy", alpha, frames, elapsed, elapsed / frames);
    backend.makeCurrent(0, 0, 0);
    backend.destroySurface(surface);
    backend.destroyContext(context);
    backend.closeColorBuffer(color);
}
}

void checkFramebufferCopy(GpuBackend& backend) {
    for (int alpha : {0, 8}) {
        const int selected = config(backend, alpha);
        require(selected >= 0, "Copy test RGB/RGBA config");
        checkConfig(backend, selected, false);
        checkResize(backend, selected);
    }
    const int msaa = config(backend, 8, true);
    if (msaa >= 0) checkConfig(backend, msaa, true);
    std::puts("desktop_copy_state_restored=1\ndesktop_copy_rgb_rgba_alpha=1\ndesktop_copy_present_rewrite_order=1");
}

void benchmarkFramebufferCopy() {
    for (int alpha : {0, 8}) {
        benchmark(false, alpha);
        benchmark(true, alpha);
    }
}
