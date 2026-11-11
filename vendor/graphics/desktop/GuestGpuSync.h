#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <map>
#include <mutex>
#include <sys/types.h>

// Backend-owned fences survive a producer's GPU socket disconnect.
class GuestGpuSync {
public:
    explicit GuestGpuSync(EGLDisplay display);
    ~GuestGpuSync();
    uint64_t fence(GLenum condition, GLbitfield flags, pid_t process);
    GLenum clientWait(uint64_t handle, GLbitfield flags, GLuint64 timeout);
    void wait(uint64_t handle, GLbitfield flags, GLuint64 timeout);
    void destroy(uint64_t handle);
    GLboolean contains(uint64_t handle);
    void query(uint64_t handle, GLenum name, GLsizei capacity, GLsizei* length, GLint* values);
    void releaseProcess(pid_t process);
    static GLenum error();
private:
    struct Fence { EGLSyncKHR egl; pid_t process; };
    static void fail(GLenum error);
    EGLDisplay display_;
    PFNEGLCREATESYNCKHRPROC create_ = nullptr;
    PFNEGLCLIENTWAITSYNCKHRPROC clientWait_ = nullptr;
    PFNEGLWAITSYNCKHRPROC wait_ = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroy_ = nullptr;
    PFNEGLGETSYNCATTRIBKHRPROC query_ = nullptr;
    std::map<uint64_t, Fence> fences_;
    std::mutex mutex_;
    uint64_t next_ = 1;
    static thread_local GLenum error_;
};
