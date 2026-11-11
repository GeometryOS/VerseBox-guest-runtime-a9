#include <GLES2/gl2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

void checkLargeTextureUpload() {
    // A 2048-square RGBA image is 16 MiB before the codec header and checksum.
    const GLsizei side = 2048;
    const size_t bytes = static_cast<size_t>(side) * side * 4;
    auto* pixels = static_cast<unsigned char*>(std::malloc(bytes));
    if (!pixels) std::exit(1);
    std::memset(pixels, 0x5a, bytes);
    GLuint texture, framebuffer;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    std::free(pixels);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    unsigned char sample[4]{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sample);
    const unsigned char expected[] = {0x5a, 0x5a, 0x5a, 0x5a};
    if (glGetError() != GL_NO_ERROR || std::memcmp(sample, expected, sizeof(expected))) {
        std::fputs("desktop_egl_error=large texture upload or following readback\n", stderr);
        std::exit(1);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteTextures(1, &texture);
    std::puts("desktop_egl_large_texture_upload=1");
}
