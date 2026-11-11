#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <hardware/gralloc.h>
#include <cutils/native_handle.h>
#include <system/window.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <unistd.h>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "desktop_image_error=%s egl=%x gl=%x\n", operation, eglGetError(), glGetError());
        std::exit(1);
    }
}
GLuint shader(GLenum type, const char* text) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &text, nullptr);
    glCompileShader(result);
    GLint compiled = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);
    require(compiled, "native image shader");
    return result;
}
}

void checkNativeBufferImages(EGLDisplay display) {
    const hw_module_t* module = nullptr;
    require(hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module) == 0, "native image gralloc module");
    const auto* gralloc = reinterpret_cast<const gralloc_module_t*>(module);
    alloc_device_t* allocator = nullptr;
    require(gralloc_open(module, &allocator) == 0, "native image allocator");
    const auto create = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    const auto destroy = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    const auto bind = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    require(create && destroy && bind, "native image entrypoints");
    GLuint vertex = shader(GL_VERTEX_SHADER,
        "attribute vec2 position; varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0,1);}");
    GLuint fragment = shader(GL_FRAGMENT_SHADER,
        "#extension GL_OES_EGL_image_external : require\nprecision mediump float;"
        "varying vec2 uv;uniform samplerExternalOES image;void main(){gl_FragColor=texture2D(image,uv);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position"); glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "native image program");
    glUseProgram(program); glUniform1i(glGetUniformLocation(program, "image"), 0);
    glActiveTexture(GL_TEXTURE0);
    const GLfloat vertices[] = {-1,-1, 1,-1, -1,1, 1,1};
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0);
    for (int format : {HAL_PIXEL_FORMAT_RGBA_8888, HAL_PIXEL_FORMAT_YV12, HAL_PIXEL_FORMAT_YCbCr_420_888}) {
        ANativeWindowBuffer buffer{};
        buffer.width = buffer.height = 32; buffer.format = format;
        buffer.usage = GRALLOC_USAGE_SW_WRITE_OFTEN | GRALLOC_USAGE_HW_TEXTURE;
        buffer.common.incRef = buffer.common.decRef = [](android_native_base_t*) {};
        require(allocator->alloc(allocator, 32, 32, format, buffer.usage, &buffer.handle,
                                 &buffer.stride) == 0, "native image allocation");
        EGLImageKHR image = create(display, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, &buffer, nullptr);
        require(image != EGL_NO_IMAGE_KHR, "native buffer EGL image");
        GLuint texture;
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        for (int level : {128, 64}) {
            if (format == HAL_PIXEL_FORMAT_RGBA_8888) {
                void* pixels = nullptr;
                require(gralloc->lock(gralloc, buffer.handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
                                       0, 0, 32, 32, &pixels) == 0, "native image write lock");
                for (int n = 0; n < buffer.stride * 32; ++n) {
                    auto* pixel = static_cast<unsigned char*>(pixels) + n * 4;
                    pixel[0] = pixel[1] = pixel[2] = level; pixel[3] = 255;
                }
            } else {
                android_ycbcr planes{};
                require(gralloc->lock_ycbcr(gralloc, buffer.handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
                                             0, 0, 32, 32, &planes) == 0, "native YUV write lock");
                std::memset(planes.y, level, planes.ystride * 32);
                for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
                    const size_t offset = y * planes.cstride + x * planes.chroma_step;
                    static_cast<unsigned char*>(planes.cb)[offset] = 128;
                    static_cast<unsigned char*>(planes.cr)[offset] = 128;
                }
            }
            require(gralloc->unlock(gralloc, buffer.handle) == 0, "native image upload");
            bind(GL_TEXTURE_EXTERNAL_OES, image);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            unsigned char pixel[4]{};
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            const int expected = format == HAL_PIXEL_FORMAT_RGBA_8888 ? level : (level - 16) * 255 / 219;
            std::printf("desktop_image_pixel format=%x level=%d rgba=%u,%u,%u,%u\n",
                         format, level, pixel[0], pixel[1], pixel[2], pixel[3]);
            require(std::abs(int(pixel[0]) - expected) < 5 &&
                    std::abs(int(pixel[1]) - expected) < 5 &&
                    std::abs(int(pixel[2]) - expected) < 5 && pixel[3] == 255,
                    "external native image pixel");
        }
        bool allocatorReleased = false;
        if (format == HAL_PIXEL_FORMAT_RGBA_8888) {
            auto original = buffer.handle;
            buffer.handle = native_handle_clone(original);
            require(buffer.handle != nullptr, "native image handle clone");
            require(allocator->free(allocator, original) == 0, "native image allocator releases source");
            allocatorReleased = true;
            usleep(1100000);
            buffer_handle_t next = nullptr;
            int nextStride = 0;
            require(allocator->alloc(allocator, 1, 1, format, buffer.usage, &next, &nextStride) == 0,
                    "native image retirement collection");
            bind(GL_TEXTURE_EXTERNAL_OES, image);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            unsigned char pixel[4]{};
            glReadPixels(16, 16, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
            require(pixel[0] == 64 && pixel[1] == 64 && pixel[2] == 64 && pixel[3] == 255,
                    "native image retains pixels after allocator release");
            require(allocator->free(allocator, next) == 0, "native image collection buffer free");
            std::puts("desktop_egl_native_image_lifetime=1");
        }
        glDeleteTextures(1, &texture); require(destroy(display, image), "native image destroy");
        if (allocatorReleased) {
            require(native_handle_close(buffer.handle) == 0, "native image clone close");
            require(native_handle_delete(const_cast<native_handle_t*>(buffer.handle)) == 0, "native image clone delete");
        } else {
            require(allocator->free(allocator, buffer.handle) == 0, "native image free");
        }
    }
    glUseProgram(0); glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    require(gralloc_close(allocator) == 0, "native image allocator close");
    std::puts("desktop_egl_native_buffer_video_pixels=1");
}
