#include "GpuBackend.h"
#include "ColorPixels.h"
#include <GLES2/gl2ext.h>
#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char* operation) {
    if (!condition) throw std::runtime_error(std::string(operation) + " EGL=" + std::to_string(eglGetError()));
}

}

thread_local uint32_t GpuBackend::currentContext_ = 0;
thread_local uint32_t GpuBackend::currentSurface_ = 0;
thread_local pid_t GpuBackend::currentProcess_ = 0;
thread_local std::shared_ptr<SharedTextureSync> GpuBackend::currentTextures_;

class GpuBackend::Current {
public:
    explicit Current(GpuBackend& owner) : owner_(owner), context_(eglGetCurrentContext()),
        draw_(eglGetCurrentSurface(EGL_DRAW)), read_(eglGetCurrentSurface(EGL_READ)) {
        require(eglMakeCurrent(owner_.display_, owner_.output_, owner_.output_, owner_.root_), "GPU root current");
    }
    ~Current() { eglMakeCurrent(owner_.display_, draw_, read_, context_); }
private:
    GpuBackend& owner_;
    EGLContext context_;
    EGLSurface draw_, read_;
};

GpuBackend::GpuBackend(int width, int height, bool directCopy, int density, int refreshHz)
    : width_(width), height_(height), density_(density), refreshHz_(refreshHz),
      directCopy_(directCopy) {
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    require(eglInitialize(display_, &major, &minor), "Host EGL initialize");
    require(eglGetConfigs(display_, nullptr, 0, &count), "Host EGL configs");
    std::vector<EGLConfig> all(count);
    require(eglGetConfigs(display_, all.data(), count, &count), "Host EGL configs");
    for (auto config : all) {
        EGLint surfaces, renderable;
        eglGetConfigAttrib(display_, config, EGL_SURFACE_TYPE, &surfaces);
        eglGetConfigAttrib(display_, config, EGL_RENDERABLE_TYPE, &renderable);
        if ((surfaces & EGL_PBUFFER_BIT) && (renderable & EGL_OPENGL_ES2_BIT)) configs_.push_back(config);
    }
    require(!configs_.empty(), "Host GLES2 config");
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT | EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_SAMPLE_BUFFERS, 0, EGL_SAMPLES, 0, EGL_NONE};
    EGLConfig config;
    if (!directCopy_ || !eglChooseConfig(display_, attributes, &config, 1, &count) || !count) {
        attributes[3] = EGL_OPENGL_ES2_BIT;
        eglGetError();
    }
    require(eglChooseConfig(display_, attributes, &config, 1, &count) && count, "Host RGBA config");
    const EGLint size[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    output_ = eglCreatePbufferSurface(display_, config, size);
    if (directCopy_ && attributes[3] == EGL_OPENGL_ES3_BIT_KHR) {
        const EGLint version3[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        root_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, version3);
        if (root_ != EGL_NO_CONTEXT) hostVersion_ = 3;
        else eglGetError();
    }
    if (root_ == EGL_NO_CONTEXT) root_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, version);
    require(output_ != EGL_NO_SURFACE && root_ != EGL_NO_CONTEXT, "Host GPU target");
    require(eglQueryContext(display_, root_, EGL_CONTEXT_CLIENT_VERSION, &hostVersion_), "Host GPU version");
    require(eglMakeCurrent(display_, output_, output_, root_), "Host GPU current");
    createImage_ = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    destroyImage_ = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    textureImage_ = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    renderbufferImage_ = reinterpret_cast<PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC>(eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));
    require(createImage_ && destroyImage_ && textureImage_ && renderbufferImage_, "Host EGL image functions");
    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (extensions && std::strstr(extensions, "EGL_KHR_fence_sync") &&
            std::strstr(extensions, "EGL_KHR_wait_sync")) {
        createSync_ = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
        waitSync_ = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(eglGetProcAddress("eglWaitSyncKHR"));
        destroySync_ = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
        require(createSync_ && waitSync_ && destroySync_, "Host GPU fence functions");
    }
    std::printf("desktop_gpu_server_wait=%d\n", createSync_ != nullptr);
    syncs_ = std::make_unique<GuestGpuSync>(display_);
    std::printf("desktop_gpu_framebuffer_blit=%d\n", directCopySupported());
    presenter_ = std::make_unique<DesktopPresenter>(display_, config, root_, output_, width_, height_);
    glGenFramebuffers(1, &copyFramebuffer_);
    std::printf("desktop_gpu_egl=%d.%d\ndesktop_gpu_vendor=%s\ndesktop_gpu_renderer=%s\n",
                major, minor, glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

GpuBackend::~GpuBackend() {
    eglMakeCurrent(display_, output_, output_, root_);
    syncs_.reset();
    presenter_.reset();
    for (auto& item : colors_) {
        if (item.second.sampled != EGL_NO_SYNC_KHR) destroySync_(display_, item.second.sampled);
        destroyImage_(display_, item.second.image);
        glDeleteTextures(1, &item.second.texture);
    }
    for (auto& item : images_) destroyImage_(display_, item.second.egl);
    for (auto& item : contexts_) eglDestroyContext(display_, item.second.egl);
    currentTextures_.reset();
    contexts_.clear();
    for (auto& item : surfaces_) {
        if (item.second.copyImage != EGL_NO_IMAGE_KHR) destroyImage_(display_, item.second.copyImage);
        glDeleteTextures(1, &item.second.copyTexture);
        eglDestroySurface(display_, item.second.egl);
    }
    glDeleteFramebuffers(1, &copyFramebuffer_);
    eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display_, root_);
    eglDestroySurface(display_, output_);
    eglTerminate(display_);
}

uint32_t GpuBackend::context(uint32_t config, uint32_t share, uint32_t version) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (config >= configs_.size() || version != 2) {
        __android_log_print(ANDROID_LOG_ERROR, "A10VM_GPU", "Rejected context config=%u/%zu share=%u version=%u contexts=%zu",
                            config, configs_.size(), share, version, contexts_.size());
        return 0;
    }
    // Preserve the guest's sharing boundary. Sharing every application with
    // root_ exhausts drivers that limit the number of contexts in one group.
    EGLContext shared = share ? contexts_.at(share).egl : EGL_NO_CONTEXT;
    EGLint renderable = 0;
    eglGetConfigAttrib(display_, configs_[config], EGL_RENDERABLE_TYPE, &renderable);
    int hostVersion = hostVersion_ == 3 && (renderable & EGL_OPENGL_ES3_BIT_KHR) ? 3 : 2;
    EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, hostVersion, EGL_NONE};
    EGLContext context = eglCreateContext(display_, configs_[config], shared, attributes);
    if (context == EGL_NO_CONTEXT && hostVersion == 3) {
        eglGetError();
        attributes[1] = hostVersion = 2;
        context = eglCreateContext(display_, configs_[config], shared, attributes);
    }
    if (context == EGL_NO_CONTEXT) {
        const auto error = eglGetError();
        __android_log_print(ANDROID_LOG_ERROR, "A10VM_GPU", "Context error=0x%x config=%u share=%u version=%d contexts=%zu process=%d",
                            error, config, share, hostVersion, contexts_.size(), currentProcess_);
        std::fprintf(stderr, "desktop_gpu_context_error=0x%x config=%u share=%u version=%d contexts=%zu\n",
                     error, config, share, hostVersion, contexts_.size());
        return 0;
    }
    eglQueryContext(display_, context, EGL_CONTEXT_CLIENT_VERSION, &hostVersion);
    auto handle = next_++;
    auto textures = share ? contexts_.at(share).textures :
        std::make_shared<SharedTextureSync>(display_, createSync_, waitSync_, destroySync_);
    textures->addContext();
    contexts_.emplace(handle, Context{context, hostVersion, {}, currentProcess_, std::move(textures)});
    return handle;
}

GpuBackend::ResourceCounts GpuBackend::resourceCounts() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return {contexts_.size(), surfaces_.size(), images_.size()};
}

void GpuBackend::setProcess(pid_t process) { currentProcess_ = process; }

void GpuBackend::releaseProcess(pid_t process) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    syncs_->releaseProcess(process);
    for (auto item = images_.begin(); item != images_.end();) {
        if (item->second.process != process) { ++item; continue; }
        destroyImage_(display_, item->second.egl);
        item = images_.erase(item);
    }
    for (auto item = surfaces_.begin(); item != surfaces_.end();) {
        if (item->second.process != process) { ++item; continue; }
        const auto handle = (item++)->first;
        destroySurface(handle);
    }
    for (auto item = contexts_.begin(); item != contexts_.end();) {
        if (item->second.process != process) { ++item; continue; }
        item->second.textures->removeContext(item->first);
        eglDestroyContext(display_, item->second.egl);
        item = contexts_.erase(item);
    }
}

void GpuBackend::destroyContext(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (eglGetCurrentContext() == contexts_.at(handle).egl) contexts_.at(handle).copy.reset();
    contexts_.at(handle).textures->removeContext(handle);
    eglDestroyContext(display_, contexts_.at(handle).egl);
    contexts_.erase(handle);
}

uint32_t GpuBackend::surface(uint32_t config, uint32_t width, uint32_t height) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (config >= configs_.size() || !width || !height || width > 4096 || height > 4096) return 0;
    const EGLint size[] = {EGL_WIDTH, static_cast<EGLint>(width), EGL_HEIGHT, static_cast<EGLint>(height), EGL_NONE};
    auto surface = eglCreatePbufferSurface(display_, configs_[config], size);
    if (surface == EGL_NO_SURFACE) return 0;
    EGLint alpha = 0, samples = 0;
    eglGetConfigAttrib(display_, configs_[config], EGL_ALPHA_SIZE, &alpha);
    eglGetConfigAttrib(display_, configs_[config], EGL_SAMPLES, &samples);
    auto handle = next_++;
    surfaces_.emplace(handle, Surface{surface, static_cast<int>(width), static_cast<int>(height),
                                     static_cast<GLenum>(alpha ? GL_RGBA : GL_RGB), 0, 0, samples, config, currentProcess_});
    return handle;
}

void GpuBackend::destroySurface(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Current current(*this);
    if (surfaces_.at(handle).copyImage != EGL_NO_IMAGE_KHR)
        destroyImage_(display_, surfaces_.at(handle).copyImage);
    glDeleteTextures(1, &surfaces_.at(handle).copyTexture);
    eglDestroySurface(display_, surfaces_.at(handle).egl);
    surfaces_.erase(handle);
}

EGLBoolean GpuBackend::makeCurrent(uint32_t context, uint32_t draw, uint32_t read) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto previous = contexts_.find(currentContext_);
    if (context != currentContext_ && previous != contexts_.end() &&
            eglGetCurrentContext() == previous->second.egl) previous->second.copy.reset();
    auto result = eglMakeCurrent(display_, draw ? surfaces_.at(draw).egl : EGL_NO_SURFACE,
        read ? surfaces_.at(read).egl : EGL_NO_SURFACE, context ? contexts_.at(context).egl : EGL_NO_CONTEXT);
    if (result == EGL_FALSE) return result;
    currentContext_ = context;
    currentSurface_ = draw;
    currentTextures_ = context ? contexts_.at(context).textures : nullptr;
    return result;
}

void GpuBackend::uploadTexture(GLenum target) {
    if (currentTextures_) currentTextures_->uploaded(currentContext_, target);
}

void GpuBackend::bindGuestTexture(GLenum target, GLuint texture) {
    if (currentTextures_) currentTextures_->beforeBind(currentContext_, target, texture);
    glBindTexture(target, texture);
}

void GpuBackend::deleteGuestTextures(GLsizei count, const GLuint* textures) {
    if (currentTextures_) currentTextures_->deleted(count, textures);
    glDeleteTextures(count, textures);
}

void GpuBackend::bindFramebuffer(GLenum target, GLuint framebuffer) {
    // Only the calling thread's EGL context is affected; no registry lookup.
    glBindFramebuffer(target, framebuffer);
}

uint32_t GpuBackend::colorBuffer(uint32_t width, uint32_t height, GLenum format) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    collectClosedColors();
    if (!width || !height || width > 4096 || height > 4096) return 0;
    Current current(*this);
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    auto image = createImage_(display_, root_, EGL_GL_TEXTURE_2D_KHR,
                              reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture)), attributes);
    require(image != EGL_NO_IMAGE_KHR, "Color buffer image");
    const auto handle = next_++;
    colors_.emplace(handle, Color{texture, image, static_cast<int>(width), static_cast<int>(height)});
    return handle;
}

void GpuBackend::openColorBuffer(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    collectClosedColors();
    ++colors_.at(handle).references;
}
void GpuBackend::closeColorBuffer(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    collectClosedColors();
    auto found = colors_.find(handle);
    if (found == colors_.end() || !found->second.references || --found->second.references) return;
    // HIDL allocation releases its local handle before the recipient imports it.
    found->second.closedAt = std::chrono::steady_clock::now();
}

void GpuBackend::collectClosedColors() {
    const auto now = std::chrono::steady_clock::now();
    std::unique_ptr<Current> current;
    for (auto item = colors_.begin(); item != colors_.end();) {
        if (item->second.references || now - item->second.closedAt < std::chrono::seconds(1)) {
            ++item;
            continue;
        }
        if (!current) current = std::make_unique<Current>(*this);
        if (item->second.sampled != EGL_NO_SYNC_KHR) destroySync_(display_, item->second.sampled);
        destroyImage_(display_, item->second.image);
        glDeleteTextures(1, &item->second.texture);
        if (lastColor_ == item->first) lastColor_ = 0;
        item = colors_.erase(item);
    }
}

EGLSyncKHR GpuBackend::fence() {
    if (!createSync_) { glFinish(); return EGL_NO_SYNC_KHR; }
    auto sync = createSync_(display_, EGL_SYNC_FENCE_KHR, nullptr);
    require(sync != EGL_NO_SYNC_KHR, "GPU copy fence");
    glFlush();
    return sync;
}

void GpuBackend::waitFence(EGLSyncKHR sync) {
    if (sync == EGL_NO_SYNC_KHR) return;
    const auto result = waitSync_(display_, sync, 0);
    destroySync_(display_, sync);
    require(result == EGL_TRUE, "GPU copy server wait");
}

void GpuBackend::sampledColor(Color& color) {
    if (color.sampled != EGL_NO_SYNC_KHR) {
        waitFence(color.sampled);
        color.sampled = EGL_NO_SYNC_KHR;
    }
    color.sampled = fence();
}

void GpuBackend::setColorBuffer(uint32_t surface, uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& target = surfaces_.at(surface);
    const auto& buffer = colors_.at(color);
    if (target.width != buffer.width || target.height != buffer.height) {
        const EGLint size[] = {EGL_WIDTH, buffer.width, EGL_HEIGHT, buffer.height, EGL_NONE};
        EGLSurface replacement = eglCreatePbufferSurface(display_, configs_.at(target.config), size);
        require(replacement != EGL_NO_SURFACE, "GPU resized window surface");
        EGLSurface draw = eglGetCurrentSurface(EGL_DRAW), read = eglGetCurrentSurface(EGL_READ);
        EGLContext context = eglGetCurrentContext();
        if (draw == target.egl || read == target.egl) {
            if (!eglMakeCurrent(display_, draw == target.egl ? replacement : draw,
                                read == target.egl ? replacement : read, context)) {
                eglDestroySurface(display_, replacement);
                require(false, "GPU resized window current");
            }
        }
        {
            Current current(*this);
            if (target.copyImage != EGL_NO_IMAGE_KHR) destroyImage_(display_, target.copyImage);
            glDeleteTextures(1, &target.copyTexture);
        }
        target.copyImage = EGL_NO_IMAGE_KHR;
        target.copyTexture = 0;
        eglDestroySurface(display_, target.egl);
        target.egl = replacement;
        target.width = buffer.width;
        target.height = buffer.height;
    }
    target.color = color;
}

int GpuBackend::flushColorBuffer(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto& surface = surfaces_.at(handle);
    if (!surface.color) return -1;
    const auto color = colors_.find(surface.color);
    if (color == colors_.end() || eglGetCurrentContext() == EGL_NO_CONTEXT ||
        eglGetCurrentSurface(EGL_DRAW) == EGL_NO_SURFACE ||
        surface.width > color->second.width || surface.height > color->second.height) {
        std::fprintf(stderr, "desktop_gpu_flush_invalid surface=%u color=%u ctx=%p draw=%p size=%dx%d\n",
                     handle, surface.color, eglGetCurrentContext(), eglGetCurrentSurface(EGL_DRAW),
                     surface.width, surface.height);
        return -1;
    }
    auto& context = contexts_.at(currentContext_);
    if (color->second.sampled != EGL_NO_SYNC_KHR) {
        waitFence(color->second.sampled);
        color->second.sampled = EGL_NO_SYNC_KHR;
    }
    if (directCopy_ && context.version == 3 && !surface.samples) {
        if (!context.copy.copy(surface.color, color->second.image, textureImage_, surface.width, surface.height,
                               color->second.width, color->second.height)) {
            std::fprintf(stderr, "desktop_gpu_blit_error surface=%u color=%u\n", handle, surface.color);
            return -1;
        }
        auto copied = fence();
        {
            Current current(*this);
            waitFence(copied);
            glFlush();
        }
        ++directFlushes_;
        ++flushes_;
        return 0;
    }
    GLint texture = 0, framebuffer = 0, readFramebuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    if (context.version == 3) glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!surface.copyTexture) {
        Current current(*this);
        glGenTextures(1, &surface.copyTexture);
        glBindTexture(GL_TEXTURE_2D, surface.copyTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, surface.copyFormat, surface.width, surface.height,
                     0, surface.copyFormat, GL_UNSIGNED_BYTE, nullptr);
        const EGLint preserved[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        surface.copyImage = createImage_(display_, root_, EGL_GL_TEXTURE_2D_KHR,
            reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(surface.copyTexture)), preserved);
        require(surface.copyImage != EGL_NO_IMAGE_KHR, "GPU copy buffer image");
        surface.copyIdentity = next_++;
    }
    const auto imported = context.copy.imageTexture(surface.copyIdentity, surface.copyImage, textureImage_);
    glBindTexture(GL_TEXTURE_2D, imported);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, surface.width, surface.height);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    if (context.version == 3) glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        std::fprintf(stderr, "desktop_gpu_copy_error=0x%x format=0x%x\n", error, surface.copyFormat);
        return -1;
    }
    // Queue GPU dependencies in both directions; CPU threads can continue decoding.
    auto copied = fence();
    EGLSyncKHR sampled;
    {
        Current current(*this);
        waitFence(copied);
        glBindFramebuffer(GL_FRAMEBUFFER, copyFramebuffer_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               color->second.texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return -1;
        // EGL window pixels use the GL origin; native buffers use Android row order.
        presenter_->blit(surface.copyTexture, copyFramebuffer_, color->second.width, color->second.height);
        sampled = fence();
    }
    // A later guest copy must wait until root_ has finished sampling copyTexture.
    waitFence(sampled);
    ++flushes_;
    ++legacyFlushes_;
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

void GpuBackend::bindTexture(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    textureImage_(GL_TEXTURE_2D, colors_.at(color).image);
}
void GpuBackend::bindRenderbuffer(uint32_t color) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    renderbufferImage_(GL_RENDERBUFFER, colors_.at(color).image);
}

void GpuBackend::readColor(uint32_t color, GLint x, GLint y, GLsizei width, GLsizei height,
                           GLenum format, GLenum type, void* data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Current current(*this);
    GLuint framebuffer;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colors_.at(color).texture, 0);
    pixelTransfer_.read(x, y, width, height, format, type, data);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
}
int GpuBackend::updateColor(uint32_t color, GLint x, GLint y, GLsizei width, GLsizei height,
                            GLenum format, GLenum type, const void* data) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Current current(*this);
    glBindTexture(GL_TEXTURE_2D, colors_.at(color).texture);
    pixelTransfer_.upload(x, y, width, height, format, type, data);
    return glGetError() == GL_NO_ERROR ? 0 : -1;
}

int GpuBackend::cacheFlush(uint32_t color, EGLint postCount, int read) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto buffer = colors_.find(color);
    if (buffer == colors_.end()) return -1;
    // CPU readback must be ordered after work from every guest context, not
    // just whatever context happens to be current on the gralloc connection.
    const auto guest = eglGetCurrentContext() == EGL_NO_CONTEXT ? EGL_NO_SYNC_KHR : fence();
    Current current(*this);
    waitFence(guest);
    glFinish();
    ++cacheFlushes_;
    if (cacheFlushes_ == 1 || cacheFlushes_ % 120 == 0)
        std::printf("desktop_gpu_cache_flush=%llu frames=%llu color=%u post_count=%d read=%d\n",
            static_cast<unsigned long long>(cacheFlushes_), static_cast<unsigned long long>(frames_),
            color, postCount, read);
    return 0;
}

void GpuBackend::setSwapInterval(EGLint interval) {
    presenter_->setSwapInterval(interval);
}

uint32_t GpuBackend::clientImage(uint32_t context, EGLenum target, GLuint texture) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    auto image = createImage_(display_, contexts_.at(context).egl, target,
                              reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture)), attributes);
    if (image == EGL_NO_IMAGE_KHR) return 0;
    auto handle = next_++;
    images_.emplace(handle, Image{image, currentProcess_});
    return handle;
}
int GpuBackend::destroyImage(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto result = destroyImage_(display_, images_.at(handle).egl);
    images_.erase(handle);
    return result;
}
EGLImageKHR GpuBackend::image(uint32_t handle) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return images_.at(handle).egl;
}

void GpuBackend::post(uint32_t color) {
    std::lock_guard<std::mutex> posting(postMutex_);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        Current current(*this);
        auto& buffer = colors_.at(color);
        presenter_->draw(buffer.texture);
        sampledColor(buffer);
        lastColor_ = color;
        ++frames_;
        if (frames_ == 1 || frames_ % 120 == 0)
            std::printf("desktop_gpu_frame=%llu color=%u flushes=%llu direct=%llu legacy=%llu cache_flushes=%llu\n",
                        static_cast<unsigned long long>(frames_), color, static_cast<unsigned long long>(flushes_),
                        static_cast<unsigned long long>(directFlushes_), static_cast<unsigned long long>(legacyFlushes_),
                        static_cast<unsigned long long>(cacheFlushes_));
    }
    // root_ is released before the BufferQueue wait. Other guest connections and
    // UI metrics can use the GPU registry while this display waits for its slot.
    presenter_->swap();
}

int64_t GpuBackend::attach(DesktopPresenter::Window window) {
    return presenter_->attach(std::move(window));
}
void GpuBackend::detach(int64_t generation) {
    presenter_->detach(generation);
}
void GpuBackend::refreshWindow() {
    std::lock_guard<std::mutex> posting(postMutex_);
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!presenter_->changed() || !lastColor_ || !colors_.count(lastColor_)) return;
        Current current(*this);
        auto& buffer = colors_.at(lastColor_);
        presenter_->draw(buffer.texture);
        sampledColor(buffer);
    }
    presenter_->swap();
}
void GpuBackend::readPresentedPixel(int x, int y, unsigned char* rgba) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Current current(*this);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}
bool GpuBackend::capture(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!lastColor_ || !colors_.count(lastColor_)) return false;
    const auto& color = colors_.at(lastColor_);
    std::vector<unsigned char> pixels(color.width * color.height * 4);
    readColor(lastColor_, 0, 0, color.width, color.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    const auto temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file << "P6\n" << color.width << ' ' << color.height << "\n255\n";
    for (int y = 0; y < color.height; ++y)
        for (int x = 0; x < color.width; ++x)
            file.write(reinterpret_cast<const char*>(pixels.data() + (y * color.width + x) * 4), 3);
    file.close();
    if (!file) throw std::runtime_error("Desktop frame capture");
    if (std::rename(temporary.c_str(), path.c_str()) != 0)
        throw std::runtime_error("Desktop frame capture publish");
    return true;
}
GpuBackend::Presentation GpuBackend::presentation() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return {frames_, presenter_->swaps(), presenter_->generation(), presenter_->boundGeneration(),
            presenter_->presentedGeneration(), presenter_->visible()};
}

GpuBackend::CopyStats GpuBackend::copyStats() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return {directFlushes_, legacyFlushes_};
}
