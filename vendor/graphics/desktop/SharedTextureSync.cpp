#include "SharedTextureSync.h"
#include <stdexcept>

SharedTextureSync::SharedTextureSync(EGLDisplay display, PFNEGLCREATESYNCKHRPROC create,
        PFNEGLWAITSYNCKHRPROC wait, PFNEGLDESTROYSYNCKHRPROC destroy)
    : display_(display), create_(create), wait_(wait), destroy_(destroy) { }

SharedTextureSync::~SharedTextureSync() {
    for (auto& item : updates_) destroy_(display_, item.second.fence);
}

void SharedTextureSync::addContext() { ++contexts_; }

void SharedTextureSync::removeContext(uint32_t context) {
    --contexts_;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : updates_) item.second.readers.erase(context);
}

void SharedTextureSync::uploaded(uint32_t context, GLenum target) {
    if (contexts_.load(std::memory_order_relaxed) < 2) return;
    GLenum binding;
    if (target == GL_TEXTURE_2D) binding = GL_TEXTURE_BINDING_2D;
    else if (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X && target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
        binding = GL_TEXTURE_BINDING_CUBE_MAP;
    else return;
    GLint texture = 0;
    glGetIntegerv(binding, &texture);
    if (!texture) return;
    // The transport round trip only submits CPU commands. Publish GPU writes
    // before another decoder thread samples the same shared texture.
    if (!create_) { glFinish(); return; }
    auto fence = create_(display_, EGL_SYNC_FENCE_KHR, nullptr);
    if (fence == EGL_NO_SYNC_KHR) throw std::runtime_error("Shared texture publish fence");
    glFlush();
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = updates_.find(texture);
    if (found != updates_.end()) destroy_(display_, found->second.fence);
    updates_.insert_or_assign(texture, Update{fence, context, {}});
}

void SharedTextureSync::beforeBind(uint32_t context, GLenum target, GLuint texture) {
    if (!texture || contexts_.load(std::memory_order_relaxed) < 2 ||
            (target != GL_TEXTURE_2D && target != GL_TEXTURE_CUBE_MAP)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = updates_.find(texture);
    if (found == updates_.end() || found->second.writer == context) return;
    auto& update = found->second;
    if (update.readers.count(context)) return;
    if (wait_(display_, update.fence, 0) != EGL_TRUE)
        throw std::runtime_error("Shared texture consumer wait");
    update.readers.insert(context);
}

void SharedTextureSync::deleted(GLsizei count, const GLuint* textures) {
    if (!create_) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (GLsizei index = 0; index < count; ++index) {
        auto found = updates_.find(textures[index]);
        if (found == updates_.end()) continue;
        destroy_(display_, found->second.fence);
        updates_.erase(found);
    }
}
