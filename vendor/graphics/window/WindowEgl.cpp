#include "WindowEgl.h"
#include "WindowProtocol.h"
#include <GLES2/gl2.h>
#include <stdexcept>

namespace {
[[noreturn]] void fail(const char* operation) {
    throw std::runtime_error(std::string(operation) + " EGL error=" + std::to_string(eglGetError()));
}
}

void WindowEgl::initialize() {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major = 0, minor = 0, count = 0;
    if (!eglInitialize(display_, &major, &minor)) fail("initialize");
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    if (!eglChooseConfig(display_, attributes, &config_, 1, &count) || count != 1) fail("config");
    const EGLint size[] = {EGL_WIDTH, windowprobe::width, EGL_HEIGHT, windowprobe::height, EGL_NONE};
    pbuffer_ = eglCreatePbufferSurface(display_, config_, size);
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, version);
    if (context_ == EGL_NO_CONTEXT || pbuffer_ == EGL_NO_SURFACE ||
        !eglMakeCurrent(display_, pbuffer_, pbuffer_, context_)) fail("pbuffer");
    description_ = "EGL " + std::to_string(major) + "." + std::to_string(minor) +
        " / " + reinterpret_cast<const char*>(glGetString(GL_VENDOR)) + " / " +
        reinterpret_cast<const char*>(glGetString(GL_RENDERER));
}

void WindowEgl::bind(WindowHandle window) {
    if (!eglMakeCurrent(display_, pbuffer_, pbuffer_, context_)) fail("detach");
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    window_ = std::move(window);
    if (!window_) return;
    EGLint format;
    if (!eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &format)) fail("visual");
    if (ANativeWindow_setBuffersGeometry(window_.get(), windowprobe::width, windowprobe::height, format) != 0) {
        throw std::runtime_error("Window buffer geometry failed");
    }
    surface_ = eglCreateWindowSurface(display_, config_, window_.get(), nullptr);
    if (surface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, surface_, surface_, context_)) fail("window");
    if (!eglSwapInterval(display_, 1)) fail("swap interval");
}

bool WindowEgl::swap() {
    if (surface_ == EGL_NO_SURFACE) return false;
    if (!eglSwapBuffers(display_, surface_)) fail("swap");
    return true;
}

WindowEgl::~WindowEgl() {
    if (display_ == EGL_NO_DISPLAY) return;
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    if (pbuffer_ != EGL_NO_SURFACE) eglDestroySurface(display_, pbuffer_);
    if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
    eglTerminate(display_);
}
