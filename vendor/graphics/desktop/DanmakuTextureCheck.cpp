#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "desktop_danmaku_error=%s gl=%x\n", operation, glGetError());
        std::exit(1);
    }
}

GLuint shader(GLenum type, const char* source) {
    GLuint result = glCreateShader(type);
    glShaderSource(result, 1, &source, nullptr);
    glCompileShader(result);
    GLint compiled = 0;
    glGetShaderiv(result, GL_COMPILE_STATUS, &compiled);
    require(compiled, "texture shader");
    return result;
}

void pixel(int x, int y, unsigned char r, unsigned char g, unsigned char b, const char* operation) {
    unsigned char value[4]{};
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, value);
    std::printf("desktop_danmaku_pixel %s=%u,%u,%u,%u\n", operation, value[0], value[1], value[2], value[3]);
    require(std::abs(int(value[0]) - r) <= 2 && std::abs(int(value[1]) - g) <= 2 &&
            std::abs(int(value[2]) - b) <= 2 && value[3] == 255, operation);
}
}

void checkDanmakuTextures() {
    GLuint vertex = shader(GL_VERTEX_SHADER,
        "attribute vec2 position; varying vec2 uv;void main(){uv=(position+1.0)*0.5;gl_Position=vec4(position,0,1);}");
    GLuint fragment = shader(GL_FRAGMENT_SHADER,
        "precision mediump float;varying vec2 uv;uniform sampler2D image;uniform float mask;"
        "void main(){vec4 v=texture2D(image,uv);gl_FragColor=mix(v,vec4(v.a),mask);}");
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position"); glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "texture program");
    glUseProgram(program); glUniform1i(glGetUniformLocation(program, "image"), 0);
    glActiveTexture(GL_TEXTURE0); glBindBuffer(GL_ARRAY_BUFFER, 0);
    const GLfloat vertices[] = {-1,-1, 1,-1, -1,1, 1,1};
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(0); glViewport(0, 0, 30, 20);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
    for (GLenum format : {GL_ALPHA, GL_RGBA, GL_BGRA_EXT}) {
        GLuint texture;
        glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glUniform1f(glGetUniformLocation(program, "mask"), format == GL_ALPHA ? 1.f : 0.f);
        for (GLint alignment : {1, 4, 8}) {
            const int components = format == GL_ALPHA ? 1 : 4;
            const int stride = (3 * components + alignment - 1) / alignment * alignment;
            unsigned char pixels[32]{};
            for (int row = 0; row < 2; ++row) for (int x = 0; x < 3; ++x) {
                const unsigned char alpha = x == 0 ? 0 : (x == 1 ? 255 : 128);
                auto* value = pixels + row * stride + x * components;
                for (int channel = 0; channel < components; ++channel) value[channel] = alpha;
            }
            glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
            glTexImage2D(GL_TEXTURE_2D, 0, format, 3, 2, 0, format, GL_UNSIGNED_BYTE, pixels);
            require(glGetError() == GL_NO_ERROR, "glyph texture upload");
            glClearColor(32.f / 255, 80.f / 255, 160.f / 255, 1.f); glClear(GL_COLOR_BUFFER_BIT);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            pixel(5, 15, 32, 80, 160, "transparent texel preserves video");
            pixel(15, 15, 255, 255, 255, "opaque glyph texel");
            pixel(25, 15, 144, 168, 208, "antialiased glyph texel");
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 3, 2, format, GL_UNSIGNED_BYTE, pixels);
            require(glGetError() == GL_NO_ERROR, "glyph texture update");
        }
        glDeleteTextures(1, &texture);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); glViewport(0, 0, 32, 32);
    glDisable(GL_BLEND); glUseProgram(0);
    glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    require(glGetError() == GL_NO_ERROR, "glyph GL state");
    std::puts("desktop_egl_danmaku_transparency=1");
}
