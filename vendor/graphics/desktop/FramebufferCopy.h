#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <cstdint>

// One instance per guest context; reset imports before releasing that context.
class FramebufferCopy {
public:
    GLuint imageTexture(uint32_t identity, EGLImageKHR image, PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind);
    bool copy(uint32_t identity, EGLImageKHR image, PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind,
              int width, int height, int targetWidth, int targetHeight);
    void reset();
private:
    GLuint framebuffer_ = 0;
    GLuint texture_ = 0;
    uint32_t imageIdentity_ = 0;
};
