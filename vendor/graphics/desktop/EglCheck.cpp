#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "codec_defs.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

void checkNativeBufferImages(EGLDisplay display);
void checkTextureMailbox(EGLDisplay display, EGLConfig config);
void checkDanmakuTextures();
void checkSharedGlyphs(EGLDisplay display, EGLConfig config);
void checkLargeTextureUpload();

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "desktop_egl_error=%s egl=%x gl=%x\n", operation, eglGetError(), glGetError());
        std::exit(1);
    }
}
GLuint shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    require(compiled, "shader compilation");
    return shader;
}
}

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint major, minor, count;
    require(eglInitialize(display, &major, &minor), "eglInitialize");
    require(eglGetConfigs(display, nullptr, 0, &count) && count > 0, "configuration count query");
    auto* configs = static_cast<EGLConfig*>(std::malloc(sizeof(EGLConfig) * count));
    require(configs != nullptr, "configuration buffer allocation");
    require(eglGetConfigs(display, configs, count, &count), "configuration enumeration");
    for (int index = 0; index < count; ++index) {
        EGLint id;
        require(configs[index] != nullptr && eglGetConfigAttrib(display, configs[index], EGL_CONFIG_ID, &id),
                "enumerated configurations have usable non-null handles");
    }
    EGLint invalid;
    require(!eglGetConfigAttrib(display, nullptr, EGL_CONFIG_ID, &invalid) && eglGetError() == EGL_BAD_CONFIG,
            "null configuration is invalid");
    std::free(configs);
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
        EGL_OPENGL_ES2_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config;
    require(eglChooseConfig(display, attributes, &config, 1, &count) && count, "eglChooseConfig");
    require(config != nullptr, "chosen configuration is not null");
    require(eglChooseConfig(display, attributes, nullptr, 0, &count) && count > 0,
            "WebView configuration count query");
    const EGLConfig sentinel = config;
    require(eglChooseConfig(display, attributes, &config, 0, &count) && count == 0 && config == sentinel,
            "zero-capacity configuration query preserves the buffer");
    const EGLint unsupported[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_NONE};
    require(eglChooseConfig(display, unsupported, &config, 1, &count) && count == 0,
            "unsupported GLES3 selection returns no configurations");
    require(eglChooseConfig(display, attributes, &config, 1, &count) && config && count,
            "GLES2 selection after WebView GLES3 probe");
    std::puts("desktop_egl_config_non_null_handles=1\ndesktop_egl_config_capabilities=1");
    const EGLint size[] = {EGL_WIDTH, 32, EGL_HEIGHT, 32, EGL_NONE};
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    auto surface = eglCreatePbufferSurface(display, config, size);
    auto context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
    require(surface != EGL_NO_SURFACE && context != EGL_NO_CONTEXT, "GPU objects");
    require(eglMakeCurrent(display, surface, surface, context), "eglMakeCurrent");
    checkLargeTextureUpload();
    std::printf("desktop_egl_version=%d.%d\ndesktop_egl_vendor=%s\ndesktop_egl_renderer=%s\n",
                major, minor, glGetString(GL_VENDOR), glGetString(GL_RENDERER));
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* shaderVersion = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::printf("desktop_egl_gl_version=%s\ndesktop_egl_shader_version=%s\n", glVersion, shaderVersion);
    require(glVersion && std::strstr(glVersion, "OpenGL ES 2.0") && shaderVersion &&
            std::strstr(shaderVersion, "GLSL ES 1.00"), "GLES2 and shader language versions agree");
    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    std::printf("desktop_egl_extensions=%s\n", extensions ? extensions : "<null>");
    require(extensions && std::strstr(extensions, "GL_OES_rgb8_rgba8"),
            "RGBA8 capability advertised to Android 10 Skia");
    GLint maxAttributes = 0;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &maxAttributes);
    std::printf("desktop_egl_max_vertex_attributes=%d\n", maxAttributes);
    require(maxAttributes >= 8 && maxAttributes <= CODEC_MAX_VERTEX_ATTRIBUTES,
            "advertised vertex attributes fit the guest codec state");
    // Skia resets every advertised slot, including slots unused by the shader.
    for (GLint index = 0; index < maxAttributes; ++index) glDisableVertexAttribArray(index);
    require(glGetError() == GL_NO_ERROR, "reset every advertised vertex attribute");
    glDisableVertexAttribArray(maxAttributes);
    require(glGetError() == GL_INVALID_VALUE, "vertex attribute limit enforced");
    GLuint framebuffer, renderbuffer;
    glGenFramebuffers(1, &framebuffer);
    glGenRenderbuffers(1, &renderbuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, 32, 32);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
    require(glGetError() == GL_NO_ERROR && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "RGBA8 render target for Android 10 Skia");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteRenderbuffers(1, &renderbuffer);
    GLuint program = glCreateProgram();
    auto vertex = shader(GL_VERTEX_SHADER, "attribute vec2 position; void main(){gl_Position=vec4(position,0.0,1.0);}");
    auto fragment = shader(GL_FRAGMENT_SHADER, "precision mediump float; void main(){gl_FragColor=vec4(0.0,1.0,0.0,1.0);}");
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position");
    glLinkProgram(program);
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "program link");
    glUseProgram(program);
    glViewport(0, 0, 32, 32);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    const GLfloat vertices[] = {-1, -1, 1, -1, 0, 1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    unsigned char pixel[4]{};
    glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(!std::memcmp(pixel, "\x00\xff\x00\xff", 4), "shader pixel");
    // SurfaceFlinger sends client arrays from an interleaved mesh; the codec
    // packs each attribute separately before the host consumes it.
    const GLfloat mesh[][6] = {
        {-1, -1, 0, 0, 0, 0}, {1, -1, 0, 0, 0, 0},
        {-1, 1, 0, 0, 0, 0}, {1, 1, 0, 0, 0, 0}
    };
    glClear(GL_COLOR_BUFFER_BIT);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(mesh[0]), mesh);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glReadPixels(8, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(!std::memcmp(pixel, "\x00\xff\x00\xff", 4), "packed interleaved mesh pixel");
    glEnable(GL_SCISSOR_TEST);
    glScissor(8, 25, 3, 1);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    unsigned char rows[40];
    std::memset(rows, 0x5a, sizeof(rows));
    glReadPixels(8, 24, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, rows + 4);
    for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 3; ++column) {
            require(!std::memcmp(rows + 4 + row * 16 + column * 4,
                    row ? "\x00\x00\xff\xff" : "\x00\xff\x00\xff", 4),
                    "multiline pixel readback");
        }
        for (int offset = 12; offset < 16; ++offset) {
            require(rows[4 + row * 16 + offset] == 0x5a, "readback row padding preserved");
        }
    }
    for (int offset = 0; offset < 4; ++offset) {
        require(rows[offset] == 0x5a && rows[36 + offset] == 0x5a, "readback buffer guards");
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(8, 24, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    require(!std::memcmp(pixel, "\x00\xff\x00\xff", 4), "readback stream remains synchronized");
    require(eglSwapBuffers(display, surface), "eglSwapBuffers");
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    require(glGetError() == GL_NO_ERROR, "GL errors");
    checkDanmakuTextures();
    checkSharedGlyphs(display, config);
    checkTextureMailbox(display, config);
    checkNativeBufferImages(display);
    require(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "eglRelease");
    eglDestroyContext(display, context);
    eglDestroySurface(display, surface);
    eglTerminate(display);
    std::puts("desktop_egl_vertex_attribute_bounds=1\ndesktop_egl_rgba8_target=1\ndesktop_egl_shader_pixel=1\ndesktop_egl_interleaved_pixel=1\ndesktop_egl_multiline_readback=1\ndesktop_egl_readback_alignment=1\ndesktop_egl_complete=1");
    return 0;
}
