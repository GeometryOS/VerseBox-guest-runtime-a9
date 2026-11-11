#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "desktop_mailbox_error=%s egl=%x gl=%x\n", operation, eglGetError(), glGetError());
        std::exit(1);
    }
}

bool extension(const char* list, const char* name) {
    if (!list) return false;
    const size_t size = std::strlen(name);
    for (const char* match = std::strstr(list, name); match; match = std::strstr(match + size, name)) {
        if ((match == list || match[-1] == ' ') && (match[size] == ' ' || !match[size])) return true;
    }
    return false;
}

struct Mailbox {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    EGLImageKHR image;
    EGLSyncKHR fence;
};

void* consume(void* data) {
    auto& box = *static_cast<Mailbox*>(data);
    require(eglMakeCurrent(box.display, box.surface, box.surface, box.context), "consumer current");
    const auto wait = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(eglGetProcAddress("eglClientWaitSyncKHR"));
    const auto bind = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    require(wait && bind, "consumer entrypoints");
    require(wait(box.display, box.fence, 0, EGL_FOREVER_KHR) == EGL_CONDITION_SATISFIED_KHR, "producer fence");
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    bind(GL_TEXTURE_2D, box.image);
    require(glGetError() == GL_NO_ERROR, "import shared texture");
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "imported framebuffer");
    unsigned char pixel[4]{};
    glReadPixels(1, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(glGetError() == GL_NO_ERROR && !std::memcmp(pixel, "\x20\xa0\x60\xff", 4), "threaded texture pixel");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    require(eglMakeCurrent(box.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "consumer release");
    return nullptr;
}
}

void checkTextureMailbox(EGLDisplay display, EGLConfig config) {
    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    std::printf("desktop_egl_display_extensions=%s\n", extensions ? extensions : "<null>");
    require(extension(extensions, "EGL_KHR_fence_sync") && extension(extensions, "EGL_KHR_image_base") &&
            extension(extensions, "EGL_KHR_gl_texture_2D_image") &&
            extension(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)), "GL_OES_EGL_image"),
            "Chromium threaded texture mailbox extensions");
    const auto create = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    const auto destroy = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    const auto fence = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
    const auto releaseFence = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
    require(create && destroy && fence && releaseFence, "producer entrypoints");
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    const unsigned char pixels[] = {32, 160, 96, 255, 32, 160, 96, 255,
                                    32, 160, 96, 255, 32, 160, 96, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    const EGLint preserved[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    const auto image = create(display, eglGetCurrentContext(), EGL_GL_TEXTURE_2D_KHR,
        reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture)), preserved);
    require(image != EGL_NO_IMAGE_KHR, "texture image creation");
    const auto sync = fence(display, EGL_SYNC_FENCE_KHR, nullptr);
    require(sync != EGL_NO_SYNC_KHR, "texture fence creation");
    glFlush();
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    const EGLint size[] = {EGL_WIDTH, 2, EGL_HEIGHT, 2, EGL_NONE};
    Mailbox box{display, eglCreateContext(display, config, EGL_NO_CONTEXT, version),
        eglCreatePbufferSurface(display, config, size), image, sync};
    require(box.context != EGL_NO_CONTEXT && box.surface != EGL_NO_SURFACE, "consumer objects");
    pthread_t reader;
    require(pthread_create(&reader, nullptr, consume, &box) == 0 && pthread_join(reader, nullptr) == 0,
            "mailbox consumer thread");
    require(destroy(display, image) && releaseFence(display, sync), "mailbox resources release");
    glDeleteTextures(1, &texture);
    require(eglDestroyContext(display, box.context) && eglDestroySurface(display, box.surface), "consumer destroy");
    std::puts("desktop_egl_threaded_texture_mailbox=1");
}
