#pragma once

#include <EGL/egl.h>
#include <android/native_window.h>
#include <memory>
#include <string>

using WindowHandle = std::shared_ptr<ANativeWindow>;

class WindowEgl {
public:
    ~WindowEgl();
    void initialize();
    void bind(WindowHandle window);
    bool swap();
    const std::string& description() const { return description_; }
private:
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig config_{};
    EGLSurface pbuffer_ = EGL_NO_SURFACE;
    EGLSurface surface_ = EGL_NO_SURFACE;
    WindowHandle window_;
    std::string description_;
};
