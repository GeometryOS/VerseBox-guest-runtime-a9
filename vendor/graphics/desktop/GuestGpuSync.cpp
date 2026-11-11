#include "GuestGpuSync.h"
#include <cstring>

thread_local GLenum GuestGpuSync::error_ = GL_NO_ERROR;

GuestGpuSync::GuestGpuSync(EGLDisplay display) : display_(display) {
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!extensions || !std::strstr(extensions, "EGL_KHR_fence_sync")) return;
    create_ = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
    clientWait_ = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(eglGetProcAddress("eglClientWaitSyncKHR"));
    destroy_ = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
    query_ = reinterpret_cast<PFNEGLGETSYNCATTRIBKHRPROC>(eglGetProcAddress("eglGetSyncAttribKHR"));
    if (!(create_ && clientWait_ && destroy_ && query_)) create_ = nullptr;
    if (std::strstr(extensions, "EGL_KHR_wait_sync"))
        wait_ = reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(eglGetProcAddress("eglWaitSyncKHR"));
}

GuestGpuSync::~GuestGpuSync() {
    for (const auto& item : fences_)
        if (item.second.egl != EGL_NO_SYNC_KHR) destroy_(display_, item.second.egl);
}

void GuestGpuSync::fail(GLenum error) { if (error_ == GL_NO_ERROR) error_ = error; }
GLenum GuestGpuSync::error() { const auto result = error_; error_ = GL_NO_ERROR; return result; }

uint64_t GuestGpuSync::fence(GLenum condition, GLbitfield flags, pid_t process) {
    if (condition != GL_SYNC_GPU_COMMANDS_COMPLETE) { fail(GL_INVALID_ENUM); return 0; }
    if (flags) { fail(GL_INVALID_VALUE); return 0; }
    EGLSyncKHR sync = EGL_NO_SYNC_KHR;
    if (create_) {
        sync = create_(display_, EGL_SYNC_FENCE_KHR, nullptr);
        if (sync == EGL_NO_SYNC_KHR) { fail(GL_INVALID_OPERATION); return 0; }
    } else {
        glFinish();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto handle = next_++;
    fences_.emplace(handle, Fence{sync, process});
    return handle;
}

GLenum GuestGpuSync::clientWait(uint64_t handle, GLbitfield flags, GLuint64 timeout) {
    if (flags & ~GL_SYNC_FLUSH_COMMANDS_BIT) { fail(GL_INVALID_VALUE); return GL_WAIT_FAILED; }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = fences_.find(handle);
    if (found == fences_.end()) { fail(GL_INVALID_VALUE); return GL_WAIT_FAILED; }
    if (found->second.egl == EGL_NO_SYNC_KHR) return GL_ALREADY_SIGNALED;
    EGLint status;
    if (query_(display_, found->second.egl, EGL_SYNC_STATUS_KHR, &status) && status == EGL_SIGNALED_KHR)
        return GL_ALREADY_SIGNALED;
    const EGLint result = clientWait_(display_, found->second.egl,
        flags & GL_SYNC_FLUSH_COMMANDS_BIT ? EGL_SYNC_FLUSH_COMMANDS_BIT_KHR : 0, timeout);
    if (result == EGL_CONDITION_SATISFIED_KHR) return GL_CONDITION_SATISFIED;
    if (result == EGL_TIMEOUT_EXPIRED_KHR) return GL_TIMEOUT_EXPIRED;
    fail(GL_INVALID_OPERATION);
    return GL_WAIT_FAILED;
}

void GuestGpuSync::wait(uint64_t handle, GLbitfield flags, GLuint64 timeout) {
    if (flags || timeout != GL_TIMEOUT_IGNORED) { fail(GL_INVALID_VALUE); return; }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = fences_.find(handle);
    if (found == fences_.end()) { fail(GL_INVALID_VALUE); return; }
    if (found->second.egl == EGL_NO_SYNC_KHR) return;
    if (wait_) {
        if (!wait_(display_, found->second.egl, 0)) fail(GL_INVALID_OPERATION);
    } else if (clientWait_(display_, found->second.egl, 0, EGL_FOREVER_KHR) != EGL_CONDITION_SATISFIED_KHR) {
        fail(GL_INVALID_OPERATION);
    }
}

void GuestGpuSync::destroy(uint64_t handle) {
    if (!handle) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = fences_.find(handle);
    if (found == fences_.end()) { fail(GL_INVALID_VALUE); return; }
    if (found->second.egl != EGL_NO_SYNC_KHR) destroy_(display_, found->second.egl);
    fences_.erase(found);
}

GLboolean GuestGpuSync::contains(uint64_t handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    return fences_.count(handle) ? GL_TRUE : GL_FALSE;
}

void GuestGpuSync::query(uint64_t handle, GLenum name, GLsizei capacity, GLsizei* length, GLint* values) {
    if (length) *length = 0;
    if (capacity < 0) { fail(GL_INVALID_VALUE); return; }
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = fences_.find(handle);
    if (found == fences_.end()) { fail(GL_INVALID_VALUE); return; }
    GLint value;
    switch (name) {
        case GL_OBJECT_TYPE: value = GL_SYNC_FENCE; break;
        case GL_SYNC_CONDITION: value = GL_SYNC_GPU_COMMANDS_COMPLETE; break;
        case GL_SYNC_FLAGS: value = 0; break;
        case GL_SYNC_STATUS: {
            EGLint status = EGL_SIGNALED_KHR;
            if (found->second.egl != EGL_NO_SYNC_KHR &&
                    !query_(display_, found->second.egl, EGL_SYNC_STATUS_KHR, &status)) {
                fail(GL_INVALID_OPERATION); return;
            }
            value = status == EGL_SIGNALED_KHR ? GL_SIGNALED : GL_UNSIGNALED;
            break;
        }
        default: fail(GL_INVALID_ENUM); return;
    }
    if (capacity > 0 && values) { *values = value; if (length) *length = 1; }
}

void GuestGpuSync::releaseProcess(pid_t process) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto item = fences_.begin(); item != fences_.end();) {
        if (item->second.process != process) { ++item; continue; }
        if (item->second.egl != EGL_NO_SYNC_KHR) destroy_(display_, item->second.egl);
        item = fences_.erase(item);
    }
}
