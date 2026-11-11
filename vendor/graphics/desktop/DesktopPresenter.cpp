#include "DesktopPresenter.h"
#include <stdexcept>
#include <string>
#include <algorithm>
#include <cstdio>

namespace {
void require(bool value, const char* operation) {
    if (!value) throw std::runtime_error(std::string(operation) + " EGL=" + std::to_string(eglGetError()));
}
GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) { glDeleteShader(shader); throw std::runtime_error("Desktop presenter shader"); }
    return shader;
}
}

DesktopPresenter::DesktopPresenter(EGLDisplay display, EGLConfig config, EGLContext context,
        EGLSurface offscreen, int width, int height)
    : display_(display), config_(config), context_(context), offscreen_(offscreen), width_(width), height_(height) {
    EGLint version = 2;
    require(eglQueryContext(display_, context_, EGL_CONTEXT_CLIENT_VERSION, &version), "Desktop context version");
    const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
    swapContext_ = eglCreateContext(display_, config_, context_, attributes);
    require(swapContext_ != EGL_NO_CONTEXT, "Desktop swap context");
    GLuint vertex = compile(GL_VERTEX_SHADER,
        "attribute vec2 position; attribute vec2 uv; varying vec2 texcoord;"
        "void main(){texcoord=uv;gl_Position=vec4(position,0.0,1.0);}");
    GLuint fragment = compile(GL_FRAGMENT_SHADER,
        "precision mediump float; varying vec2 texcoord; uniform sampler2D image;"
        "void main(){gl_FragColor=texture2D(image,texcoord);}");
    program_ = glCreateProgram();
    glAttachShader(program_, vertex);
    glAttachShader(program_, fragment);
    glBindAttribLocation(program_, 0, "position");
    glBindAttribLocation(program_, 1, "uv");
    glLinkProgram(program_);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    require(linked, "Desktop presenter program");
    sampler_ = glGetUniformLocation(program_, "image");
}

DesktopPresenter::~DesktopPresenter() {
    eglMakeCurrent(display_, offscreen_, offscreen_, context_);
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    eglDestroyContext(display_, swapContext_);
    glDeleteProgram(program_);
}

int64_t DesktopPresenter::attach(Window window) {
    std::lock_guard<std::mutex> lock(windowMutex_);
    requested_ = std::move(window);
    return ++generation_;
}

void DesktopPresenter::detach(int64_t generation) {
    std::lock_guard<std::mutex> lock(windowMutex_);
    if (generation != generation_) return;
    requested_.reset();
    ++generation_;
}

void DesktopPresenter::bind() {
    if (!changed()) return;
    Window requested;
    int64_t generation;
    {
        std::lock_guard<std::mutex> lock(windowMutex_);
        requested = requested_;
        generation = generation_;
    }
    require(eglMakeCurrent(display_, offscreen_, offscreen_, context_), "Desktop detach current");
    if (surface_ != EGL_NO_SURFACE) eglDestroySurface(display_, surface_);
    surface_ = EGL_NO_SURFACE;
    bound_ = std::move(requested);
    if (bound_) {
        EGLint format = 0;
        require(eglGetConfigAttrib(display_, config_, EGL_NATIVE_VISUAL_ID, &format), "Desktop native format");
        require(ANativeWindow_setBuffersGeometry(bound_.get(), width_, height_, format) == 0,
                "Desktop window geometry");
        surface_ = eglCreateWindowSurface(display_, config_, bound_.get(), nullptr);
        require(surface_ != EGL_NO_SURFACE, "Desktop window surface");
        require(eglMakeCurrent(display_, surface_, surface_, context_), "Desktop window current");
        const int interval = swapInterval_.load();
        require(eglSwapInterval(display_, interval), "Desktop swap interval");
        appliedSwapInterval_ = interval;
    }
    boundGeneration_ = generation;
}

void DesktopPresenter::draw(GLuint texture) {
    bind();
    auto target = surface_ == EGL_NO_SURFACE ? offscreen_ : surface_;
    require(eglMakeCurrent(display_, target, target, context_), "Desktop present current");
    blit(texture, 0, width_, height_);
    glFlush();
}

void DesktopPresenter::swap() {
    if (surface_ != EGL_NO_SURFACE) {
        auto context = eglGetCurrentContext();
        auto draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
        EGLBoolean result = eglMakeCurrent(display_, surface_, surface_, swapContext_);
        const int interval = swapInterval_.load();
        if (result && interval != appliedSwapInterval_) {
            result = eglSwapInterval(display_, interval);
            if (result) appliedSwapInterval_ = interval;
        }
        if (result) result = eglSwapBuffers(display_, surface_);
        const auto error = result ? EGL_SUCCESS : eglGetError();
        eglMakeCurrent(display_, draw, read, context);
        if (!result) {
            // BufferQueue can disconnect before surfaceDestroyed reaches detach().
            // Retire only this generation; a newer attachment keeps its window.
            if (error == EGL_BAD_SURFACE || error == EGL_BAD_NATIVE_WINDOW) {
                detach(boundGeneration_);
                return;
            }
            throw std::runtime_error("Desktop present swap EGL=" + std::to_string(error));
        }
        ++swaps_;
        presentedGeneration_ = boundGeneration_.load();
    }
}

void DesktopPresenter::setSwapInterval(EGLint interval) {
    const int requested = std::clamp(static_cast<int>(interval), 0, 1);
    const int previous = swapInterval_.exchange(requested);
    if (requested != previous)
        std::printf("desktop_gpu_swap_interval=%d requested=%d\n", requested, interval);
}

void DesktopPresenter::blit(GLuint texture, GLuint framebuffer, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(sampler_, 0);
    const GLfloat vertices[] = {-1,-1, 0,1, 1,-1, 1,1, -1,1, 0,0, 1,1, 1,0};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), vertices + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
        throw std::runtime_error("Desktop present draw GL=" + std::to_string(error));
}
