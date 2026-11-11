#pragma once
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/native_window.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

class DesktopPresenter {
public:
    using Window = std::shared_ptr<ANativeWindow>;
    DesktopPresenter(EGLDisplay display, EGLConfig config, EGLContext context,
                     EGLSurface offscreen, int width, int height);
    ~DesktopPresenter();
    int64_t attach(Window window);
    void detach(int64_t generation);
    void draw(GLuint texture);
    void swap();
    void setSwapInterval(EGLint interval);
    int swapInterval() const { return swapInterval_.load(); }
    void blit(GLuint texture, GLuint framebuffer, int width, int height);
    bool changed() const { return generation_ != boundGeneration_; }
    int64_t generation() const { return generation_; }
    int64_t boundGeneration() const { return boundGeneration_; }
    int64_t swaps() const { return swaps_; }
    int64_t presentedGeneration() const { return presentedGeneration_; }
    bool visible() { std::lock_guard<std::mutex> lock(windowMutex_); return requested_ != nullptr; }
private:
    void bind();
    EGLDisplay display_;
    EGLConfig config_;
    EGLContext context_;
    EGLContext swapContext_ = EGL_NO_CONTEXT;
    EGLSurface offscreen_, surface_ = EGL_NO_SURFACE;
    Window requested_, bound_;
    std::mutex windowMutex_;
    GLuint program_ = 0;
    GLint sampler_ = -1;
    int width_, height_;
    std::atomic<int64_t> generation_{0}, boundGeneration_{0}, presentedGeneration_{0}, swaps_{0};
    std::atomic<int> swapInterval_{1};
    int appliedSwapInterval_ = -1;
};
