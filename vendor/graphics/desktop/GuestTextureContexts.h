#pragma once

class GLSharedGroup;
namespace guesttextures {
void added(const GLSharedGroup* group);
void removed(const GLSharedGroup* group);
bool shared(const GLSharedGroup* group);
}
