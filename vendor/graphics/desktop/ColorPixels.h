#pragma once
#include <GLES2/gl2.h>
#include <vector>

class ColorPixels {
public:
    void read(GLint x, GLint y, GLsizei width, GLsizei height,
              GLenum format, GLenum type, void* data);
    void upload(GLint x, GLint y, GLsizei width, GLsizei height,
                GLenum format, GLenum type, const void* data);
private:
    std::vector<unsigned char> rgba_;
    void grow(size_t pixels);
};
