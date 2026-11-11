#include "FramebufferCopy.h"

GLuint FramebufferCopy::imageTexture(uint32_t identity, EGLImageKHR image,
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind) {
    if (!texture_) glGenTextures(1, &texture_);
    if (imageIdentity_ != identity) {
        GLint previous;
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
        glBindTexture(GL_TEXTURE_2D, texture_);
        bind(GL_TEXTURE_2D, image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, previous);
        imageIdentity_ = identity;
    }
    return texture_;
}

void FramebufferCopy::reset() {
    glDeleteFramebuffers(1, &framebuffer_);
    glDeleteTextures(1, &texture_);
    framebuffer_ = texture_ = imageIdentity_ = 0;
}

bool FramebufferCopy::copy(uint32_t identity, EGLImageKHR image,
        PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bind, int width, int height, int targetWidth, int targetHeight) {
    const auto texture = imageTexture(identity, image, bind);
    GLint draw, read;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
    const auto scissor = glIsEnabled(GL_SCISSOR_TEST);
    if (!framebuffer_) glGenFramebuffers(1, &framebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (complete) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glDisable(GL_SCISSOR_TEST);
        // GL rows become Android rows without a temporary texture or shader pass.
        glBlitFramebuffer(0, 0, width, height, 0, targetHeight, targetWidth, 0,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    if (scissor) glEnable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
    return complete && glGetError() == GL_NO_ERROR;
}
