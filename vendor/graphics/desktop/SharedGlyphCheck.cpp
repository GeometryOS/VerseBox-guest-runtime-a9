#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <pthread.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "desktop_shared_glyph_error=%s egl=%x gl=%x\n",
                     operation, eglGetError(), glGetError());
        std::exit(1);
    }
}

struct Upload {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    GLuint texture = 0;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
    int requested = 0, completed = -1;
};

void* upload(void* data) {
    auto& job = *static_cast<Upload*>(data);
    require(eglMakeCurrent(job.display, job.surface, job.surface, job.context), "worker current");
    glGenTextures(1, &job.texture);
    glBindTexture(GL_TEXTURE_2D, job.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    pthread_mutex_lock(&job.mutex);
    job.completed = 0;
    pthread_cond_broadcast(&job.changed);
    pthread_mutex_unlock(&job.mutex);
    auto* pixels = static_cast<unsigned char*>(std::malloc(2017 * 148 * 4));
    require(pixels != nullptr, "atlas allocation");
    for (int pass = 1; pass <= 3; ++pass) {
        pthread_mutex_lock(&job.mutex);
        while (job.requested < pass) pthread_cond_wait(&job.changed, &job.mutex);
        pthread_mutex_unlock(&job.mutex);
        for (int y = 0; y < 148; ++y) for (int x = 0; x < 2017; ++x) {
            auto* pixel = pixels + (y * 2017 + x) * 4;
            unsigned char alpha = x < 672 ? 0 : (x < 1344 ? 255 : 128);
            pixel[0] = pixel[1] = alpha;
            pixel[2] = pass == 1 ? alpha : 0;
            pixel[3] = alpha;
        }
        if (pass == 1) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2017, 148, 0,
                                   GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2017, 148,
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Publish the handle as soon as the upload API returns, with no query
        // that would incidentally drain the producer's transport buffer.
        pthread_mutex_lock(&job.mutex);
        job.completed = pass;
        pthread_cond_broadcast(&job.changed);
        while (job.requested <= pass) pthread_cond_wait(&job.changed, &job.mutex);
        pthread_mutex_unlock(&job.mutex);
    }
    std::free(pixels);
    require(eglMakeCurrent(job.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), "worker release");
    return nullptr;
}

GLuint shader(GLenum type, const char* source) {
    auto result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint compiled = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);
    require(compiled, "glyph shader");
    return result;
}

void pixel(int x, int pass) {
    unsigned char actual[4]{};
    glReadPixels(x, 15, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
    const unsigned char expected[3][4] = {
        {32, 80, 160, 255}, {255, 255, static_cast<unsigned char>(pass == 1 ? 255 : 0), 255},
        {144, 168, static_cast<unsigned char>(pass == 1 ? 208 : 80), 255}
    };
    const auto* want = expected[x / 10];
    std::printf("desktop_shared_glyph_pixel pass=%d x=%d rgba=%u,%u,%u,%u\n",
                pass, x, actual[0], actual[1], actual[2], actual[3]);
    for (int channel = 0; channel < 4; ++channel)
        require(std::abs(int(actual[channel]) - want[channel]) <= 2, "shared atlas pixels");
}
}

void checkSharedGlyphs(EGLDisplay display, EGLConfig config) {
    const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    const EGLint size[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    Upload job{display, eglCreateContext(display, config, eglGetCurrentContext(), version),
               eglCreatePbufferSurface(display, config, size)};
    require(job.context != EGL_NO_CONTEXT && job.surface != EGL_NO_SURFACE, "shared worker objects");
    pthread_t worker;
    require(pthread_create(&worker, nullptr, upload, &job) == 0, "worker thread");
    auto vertex = shader(GL_VERTEX_SHADER,
        "attribute vec2 position;attribute vec2 texcoord;attribute vec4 color;"
        "varying vec2 uv;varying vec4 tint;void main(){uv=texcoord;tint=color;gl_Position=vec4(position,0,1);}");
    auto fragment = shader(GL_FRAGMENT_SHADER,
        "precision highp float;varying vec2 uv;varying vec4 tint;uniform sampler2D image;"
        "void main(){gl_FragColor=texture2D(image,uv)*tint;}");
    auto program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position"); glBindAttribLocation(program, 1, "texcoord");
    glBindAttribLocation(program, 2, "color"); glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "glyph program");
    glUseProgram(program); glUniform1i(glGetUniformLocation(program, "image"), 0);
    const GLfloat vertices[][8] = {{-1,-1,0,0,1,1,1,1}, {1,-1,1,0,1,1,1,1},
                                   {-1,1,0,1,1,1,1,1}, {1,1,1,1,1,1,1,1}};
    const GLushort indices[] = {0,1,2,2,1,3};
    GLuint buffers[2];
    glGenBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]); glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    for (int index = 0; index < 3; ++index) {
        glVertexAttribPointer(index, index == 2 ? 4 : 2, GL_FLOAT, GL_FALSE, sizeof(vertices[0]),
                              reinterpret_cast<void*>(uintptr_t(index * 2 * sizeof(GLfloat))));
        glEnableVertexAttribArray(index);
    }
    glViewport(0, 0, 30, 20); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND); glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
    pthread_mutex_lock(&job.mutex);
    while (job.completed < 0) pthread_cond_wait(&job.changed, &job.mutex);
    pthread_mutex_unlock(&job.mutex);
    // Bind the incomplete atlas before the worker defines its first storage.
    glBindTexture(GL_TEXTURE_2D, job.texture);
    for (int pass = 1; pass <= 3; ++pass) {
        pthread_mutex_lock(&job.mutex);
        job.requested = pass;
        pthread_cond_broadcast(&job.changed);
        while (job.completed < pass) pthread_cond_wait(&job.changed, &job.mutex);
        pthread_mutex_unlock(&job.mutex);
        // The second draw reuses the binding across a shared upload.
        if (pass != 2) glBindTexture(GL_TEXTURE_2D, job.texture);
        glClearColor(32.f/255, 80.f/255, 160.f/255, 1); glClear(GL_COLOR_BUFFER_BIT);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
        pixel(5, pass); pixel(15, pass); pixel(25, pass);
        require(glGetError() == GL_NO_ERROR, "shared glyph draw");
        pthread_mutex_lock(&job.mutex);
        job.requested = pass + 1;
        pthread_cond_broadcast(&job.changed);
        pthread_mutex_unlock(&job.mutex);
    }
    require(pthread_join(worker, nullptr) == 0, "worker join");
    glDeleteTextures(1, &job.texture); glDeleteBuffers(2, buffers);
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisableVertexAttribArray(1); glDisableVertexAttribArray(2);
    glDisable(GL_BLEND); glUseProgram(0); glViewport(0, 0, 32, 32);
    glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    require(eglDestroyContext(display, job.context) && eglDestroySurface(display, job.surface), "worker cleanup");
    pthread_cond_destroy(&job.changed); pthread_mutex_destroy(&job.mutex);
    std::puts("desktop_egl_shared_glyph_atlas=1");
    std::puts("desktop_egl_shared_glyph_first_frame=1\ndesktop_egl_shared_glyph_without_query=1");
}
