#include "GpuBackend.h"
#include "FramebufferCopyCheck.h"
#include "ContextGroupsCheck.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <thread>
#include <time.h>

static std::atomic<size_t> largePixelAllocations{0};
void* operator new(size_t bytes) {
    if (bytes == 1080 * 1920 * 4) ++largePixelAllocations;
    void* memory = std::malloc(bytes ? bytes : 1);
    if (!memory) throw std::bad_alloc();
    return memory;
}
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }

namespace {
uint64_t cpuNanos() {
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now)) throw std::runtime_error("Pixel benchmark clock");
    return uint64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

int benchmarkColorTransfers() {
    constexpr int width = 1080, height = 1920, rounds = 20;
    GpuBackend backend(32, 32);
    const auto color = backend.colorBuffer(width, height, GL_RGB);
    std::vector<unsigned char> rgb(width * height * 3, 128);
    for (int i = 0; i < 4; ++i) {
        if (backend.updateColor(color, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data())) return 1;
        backend.readColor(color, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    }
    const size_t before = largePixelAllocations;
    const uint64_t started = cpuNanos();
    for (int i = 0; i < rounds; ++i) {
        rgb[0] = i;
        if (backend.updateColor(color, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data())) return 2;
        rgb[0] = 255;
        backend.readColor(color, 0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        if (rgb[0] != i || rgb.back() != 128) return 3;
    }
    const uint64_t elapsed = (cpuNanos() - started) / rounds;
    const size_t allocations = largePixelAllocations - before;
    std::printf("{\"width\":%d,\"height\":%d,\"rounds\":%d,\"cpuNanosPerRoundTrip\":%llu,"
                "\"rgbaScratchAllocationsAfterWarmup\":%zu,\"maxScratchAllocations\":0,\"pixelsVerified\":true,"
                "\"passed\":%s,\"limitation\":\"Actual host upload/readback; excludes guest and IPC\"}\n",
                width, height, rounds, (unsigned long long)elapsed, allocations, allocations ? "false" : "true");
    backend.closeColorBuffer(color);
    return allocations ? 4 : 0;
}
}

int main(int argc, char** argv) {
    try {
        const bool legacy = argc == 2 && !std::strcmp(argv[1], "--legacy-copy");
        const bool benchmark = argc == 2 && !std::strcmp(argv[1], "--benchmark");
        const bool pixelBenchmark = argc == 2 && !std::strcmp(argv[1], "--benchmark-pixels");
        if (argc > 2 || (argc == 2 && !legacy && !benchmark && !pixelBenchmark)) return 64;
        if (benchmark) { benchmarkFramebufferCopy(); return 0; }
        if (pixelBenchmark) return benchmarkColorTransfers();
        GpuBackend backend(32, 32, !legacy);
        checkContextGroups(backend);
        checkFramebufferCopy(backend);
        auto color = backend.colorBuffer(1, 1, GL_RGBA);
        backend.closeColorBuffer(color);
        backend.openColorBuffer(color);
        const unsigned char expected[] = {19, 177, 233, 255};
        if (backend.updateColor(color, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, expected) != 0) return 1;
        if (backend.cacheFlush(color, 0, 1) != 0) return 29;
        std::puts("desktop_cpu_cache_flush_without_context=1");
        backend.post(color);
        unsigned char actual[4]{};
        backend.readPresentedPixel(16, 16, actual);
        if (std::memcmp(expected, actual, 4)) return 2;

        unsigned char rgb[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
        backend.readColor(color, 0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb + 1);
        backend.post(color);
        if (rgb[0] != 0xa5 || rgb[4] != 0xa5 || std::memcmp(expected, rgb + 1, 3)) return 20;
        unsigned char rgb565[] = {0xa5, 0xa5, 0xa5, 0xa5};
        backend.readColor(color, 0, 0, 1, 1, GL_RGB565, GL_UNSIGNED_SHORT_5_6_5, rgb565 + 1);
        backend.post(color);
        if (rgb565[0] != 0xa5 || rgb565[3] != 0xa5 || rgb565[1] != 0x9d || rgb565[2] != 0x15) return 21;
        std::puts("desktop_color_rgb_readback=1\ndesktop_color_rgb565_readback=1");

        const auto oddWidth = backend.colorBuffer(3, 2, GL_RGB);
        const unsigned char rgbRows[] = {255,0,0, 0,255,0, 0,0,255, 19,177,233, 233,19,177, 177,233,19};
        if (backend.updateColor(oddWidth, 0, 0, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, rgbRows)) return 22;
        unsigned char tightRows[20];
        std::memset(tightRows, 0xa5, sizeof(tightRows));
        backend.readColor(oddWidth, 0, 0, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, tightRows + 1);
        backend.post(oddWidth);
        if (tightRows[0] != 0xa5 || tightRows[19] != 0xa5 ||
                std::memcmp(tightRows + 1, rgbRows, sizeof(rgbRows))) return 23;
        const uint16_t packedRows[] = {0xf800, 0x07e0, 0x001f, 0x159d, 0xffff, 0};
        if (backend.updateColor(oddWidth, 0, 0, 3, 2, GL_RGB565, GL_UNSIGNED_SHORT_5_6_5, packedRows)) return 24;
        unsigned char tightPacked[14];
        std::memset(tightPacked, 0xa5, sizeof(tightPacked));
        backend.readColor(oddWidth, 0, 0, 3, 2, GL_RGB565, GL_UNSIGNED_SHORT_5_6_5, tightPacked + 1);
        backend.post(oddWidth);
        if (tightPacked[0] != 0xa5 || tightPacked[13] != 0xa5 ||
                std::memcmp(tightPacked + 1, packedRows, sizeof(packedRows))) return 25;
        backend.closeColorBuffer(oddWidth);
        std::puts("desktop_color_tight_rows=1\ndesktop_color_rgb_upload=1\ndesktop_color_rgb565_upload=1");

        for (const auto size : {std::pair<int, int>{18, 4}, {34, 2}, {7, 3}, {48, 2}}) {
            const auto buffer = backend.colorBuffer(size.first, size.second, GL_RGB);
            const size_t bytes = size.first * size.second * 3;
            std::vector<unsigned char> source(bytes), result(bytes + 2, 0xa5);
            for (size_t i = 0; i < bytes; ++i) source[i] = (i * 127 + 19) & 255;
            if (backend.updateColor(buffer, 0, 0, size.first, size.second, GL_RGB, GL_UNSIGNED_BYTE, source.data())) return 32;
            backend.readColor(buffer, 0, 0, size.first, size.second, GL_RGB, GL_UNSIGNED_BYTE, result.data() + 1);
            if (result.front() != 0xa5 || result.back() != 0xa5 ||
                std::memcmp(source.data(), result.data() + 1, bytes)) return 33;
            std::vector<unsigned char> rgba(size.first * size.second * 4);
            backend.readColor(buffer, 0, 0, size.first, size.second, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            for (size_t i = 0; i < bytes / 3; ++i) {
                if (std::memcmp(rgba.data() + i * 4, source.data() + i * 3, 3) || rgba[i * 4 + 3] != 255) return 34;
            }
            backend.closeColorBuffer(buffer);
        }
        std::puts("desktop_color_vector_rows_and_tail=1\ndesktop_color_vector_alpha=1");

        const auto rows = backend.colorBuffer(2, 2, GL_RGBA);
        const unsigned char androidRows[] = {255,0,0,255, 255,0,0,255, 0,0,255,255, 0,0,255,255};
        if (backend.updateColor(rows, 0, 0, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, androidRows) != 0) return 13;
        backend.post(rows);
        backend.readPresentedPixel(16, 28, actual);
        if (actual[0] < 240 || actual[2] > 15) return 14;
        backend.readPresentedPixel(16, 3, actual);
        if (actual[2] < 240 || actual[0] > 15) return 15;
        std::puts("desktop_android_row_orientation=1");
        backend.closeColorBuffer(rows);

        const auto context = backend.context(0, 0, 2);
        const auto surface = backend.surface(0, 4, 4);
        const auto guestColor = backend.colorBuffer(4, 4, GL_RGBA);
        backend.setColorBuffer(surface, guestColor);
        if (!context || !surface || !guestColor || !backend.makeCurrent(context, surface, surface)) return 4;
        glViewport(0, 0, 4, 4);
        glClearColor(1.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        unsigned char source[4]{};
        glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, source);
        const unsigned char red[] = {255, 0, 0, 255};
        if (std::memcmp(red, source, 4)) return 8;
        if (backend.flushColorBuffer(surface) != 0) return 5;
        unsigned char copied[4]{};
        backend.readColor(guestColor, 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copied);
        if (std::memcmp(red, copied, 4)) return 6;
        glEnable(GL_SCISSOR_TEST);
        glScissor(0, 0, 4, 2);
        glClearColor(0.f, 0.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        if (backend.flushColorBuffer(surface) != 0) return 16;
        backend.readColor(guestColor, 2, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copied);
        if (std::memcmp(red, copied, 4)) return 17;
        backend.post(guestColor);
        backend.readPresentedPixel(16, 28, actual);
        if (actual[0] < 240 || actual[2] > 15) return 18;
        backend.readPresentedPixel(16, 3, actual);
        if (actual[2] < 240 || actual[0] > 15) return 19;
        std::puts("desktop_guest_gl_row_orientation=1");
        GLuint imported, target;
        glGenRenderbuffers(1, &imported);
        glBindRenderbuffer(GL_RENDERBUFFER, imported);
        backend.bindRenderbuffer(guestColor);
        glGenFramebuffers(1, &target);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, imported);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 9;
        glClearColor(0.f, 1.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (backend.cacheFlush(guestColor, 0, 1) != 0) return 30;
        const unsigned char green[] = {0, 255, 0, 255};
        backend.readColor(guestColor, 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copied);
        if (std::memcmp(green, copied, 4)) return 10;
        GLuint importedTexture;
        glGenTextures(1, &importedTexture);
        glBindTexture(GL_TEXTURE_2D, importedTexture);
        backend.bindTexture(guestColor);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, importedTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return 11;
        glClearColor(0.f, 0.f, 1.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        if (backend.cacheFlush(guestColor, 0, 1) != 0) return 31;
        const unsigned char blue[] = {0, 0, 255, 255};
        backend.readColor(guestColor, 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, copied);
        if (std::memcmp(blue, copied, 4)) return 12;
        glDeleteTextures(1, &importedTexture);
        backend.bindFramebuffer(GL_FRAMEBUFFER, 0);
        glDeleteFramebuffers(1, &target);
        glDeleteRenderbuffers(1, &imported);
        std::puts("desktop_composer_image_write=1");
        std::puts("desktop_composer_texture_write=1");
        if (!backend.makeCurrent(0, 0, 0)) return 7;
        backend.destroySurface(surface);
        backend.destroyContext(context);
        backend.closeColorBuffer(guestColor);
        std::puts("desktop_guest_surface_flush=1");

        std::atomic<bool> concurrentPassed{true};
        auto render = [&](int channel) {
            try {
                const auto ctx = backend.context(0, 0, 2);
                const auto target = backend.surface(0, 16, 16);
                const auto buffer = backend.colorBuffer(16, 16, GL_RGBA);
                backend.setColorBuffer(target, buffer);
                if (!backend.makeCurrent(ctx, target, target)) throw std::runtime_error("Concurrent context");
                for (int frame = 0; frame < 64; ++frame) {
                    const unsigned char value = frame % 2 ? 255 : 0;
                    glClearColor(channel == 0 ? value / 255.f : 0.f, 0.f,
                                 channel == 2 ? value / 255.f : 0.f, 1.f);
                    glClear(GL_COLOR_BUFFER_BIT);
                    if (backend.flushColorBuffer(target)) throw std::runtime_error("Concurrent flush");
                    unsigned char pixel[4]{};
                    backend.readColor(buffer, 8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
                    if (pixel[channel] != value || pixel[1] != 0 || pixel[3] != 255)
                        throw std::runtime_error("Concurrent copy ordering");
                }
                backend.makeCurrent(0, 0, 0);
                backend.destroySurface(target);
                backend.destroyContext(ctx);
                backend.closeColorBuffer(buffer);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "desktop_concurrent_error=%s\n", error.what());
                concurrentPassed = false;
            }
        };
        std::thread first(render, 0), second(render, 2);
        first.join();
        second.join();
        if (!concurrentPassed) return 26;
        std::puts("desktop_concurrent_copy_ordering=1");
        const auto copyStats = backend.copyStats();
        if (!legacy && backend.directCopySupported() && !copyStats.direct) return 27;
        if (legacy && (copyStats.direct || !copyStats.legacy)) return 28;
        std::printf("desktop_copy_direct=%llu\ndesktop_copy_legacy=%llu\n",
                    static_cast<unsigned long long>(copyStats.direct),
                    static_cast<unsigned long long>(copyStats.legacy));

        backend.openColorBuffer(color);
        backend.closeColorBuffer(color);
        backend.post(color);
        backend.closeColorBuffer(color);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        auto replacement = backend.colorBuffer(1, 1, GL_RGBA);
        try {
            backend.openColorBuffer(color);
            return 3;
        } catch (const std::out_of_range&) { }
        backend.closeColorBuffer(replacement);
        std::puts("desktop_buffer_import_lifetime=1\ndesktop_buffer_retired=1");
        std::puts("desktop_presented_pixel=1");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "desktop_present_error=%s\n", error.what());
        return 1;
    }
}
