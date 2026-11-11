#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <atomic>
#include <map>
#include <mutex>
#include <set>

class SharedTextureSync {
public:
    SharedTextureSync(EGLDisplay display, PFNEGLCREATESYNCKHRPROC create,
                      PFNEGLWAITSYNCKHRPROC wait, PFNEGLDESTROYSYNCKHRPROC destroy);
    ~SharedTextureSync();
    void addContext();
    void removeContext(uint32_t context);
    void uploaded(uint32_t context, GLenum target);
    void beforeBind(uint32_t context, GLenum target, GLuint texture);
    void deleted(GLsizei count, const GLuint* textures);
private:
    struct Update {
        EGLSyncKHR fence;
        uint32_t writer;
        std::set<uint32_t> readers;
    };
    EGLDisplay display_;
    PFNEGLCREATESYNCKHRPROC create_;
    PFNEGLWAITSYNCKHRPROC wait_;
    PFNEGLDESTROYSYNCKHRPROC destroy_;
    std::atomic<unsigned> contexts_{0};
    std::mutex mutex_;
    std::map<GLuint, Update> updates_;
};
