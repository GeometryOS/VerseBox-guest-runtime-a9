#pragma once
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <chrono>
#include <map>
#include <mutex>
#include <vector>
#include <sys/types.h>
#include "DesktopPresenter.h"
#include "FramebufferCopy.h"
#include "SharedTextureSync.h"
#include "ColorPixels.h"
#include "GuestGpuSync.h"
#include "DesktopDisplay.h"

class GpuBackend {
public:
    /**
     * @param width,height 本次会话的帧缓冲尺寸；@param density,refreshHz 上报给客体的值。
     *                     缺省 = DesktopDisplay.h 的编译期默认值（旧调用点行为不变）。
     */
    GpuBackend(int width, int height, bool directCopy = true,
               int density = desktopdisplay::density, int refreshHz = desktopdisplay::refreshHz);
    ~GpuBackend();
    EGLDisplay display() const { return display_; }
    GuestGpuSync& syncs() { return *syncs_; }
    static pid_t process() { return currentProcess_; }
    const std::vector<EGLConfig>& configs() const { return configs_; }
    uint32_t context(uint32_t config, uint32_t share, uint32_t version);
    void destroyContext(uint32_t handle);
    uint32_t surface(uint32_t config, uint32_t width, uint32_t height);
    void destroySurface(uint32_t handle);
    EGLBoolean makeCurrent(uint32_t context, uint32_t draw, uint32_t read);
    void bindFramebuffer(GLenum target, GLuint framebuffer);
    void uploadTexture(GLenum target);
    void bindGuestTexture(GLenum target, GLuint texture);
    void deleteGuestTextures(GLsizei count, const GLuint* textures);
    uint32_t colorBuffer(uint32_t width, uint32_t height, GLenum format);
    void openColorBuffer(uint32_t handle);
    void closeColorBuffer(uint32_t handle);
    void setColorBuffer(uint32_t surface, uint32_t color);
    int flushColorBuffer(uint32_t surface);
    void bindTexture(uint32_t color);
    void bindRenderbuffer(uint32_t color);
    void readColor(uint32_t color, GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, void* data);
    int updateColor(uint32_t color, GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, const void* data);
    int cacheFlush(uint32_t color, EGLint postCount, int read);
    void setSwapInterval(EGLint interval);
    uint32_t clientImage(uint32_t context, EGLenum target, GLuint texture);
    int destroyImage(uint32_t handle);
    EGLImageKHR image(uint32_t handle);
    void post(uint32_t color);
    int64_t attach(DesktopPresenter::Window window);
    void detach(int64_t generation);
    void refreshWindow();
    void readPresentedPixel(int x, int y, unsigned char* rgba);
    bool capture(const std::string& path);
    struct Presentation { uint64_t frames; int64_t swaps, requested, bound, presented; bool visible; };
    Presentation presentation();
    int width() const { return width_; }
    int height() const { return height_; }
    /** 上报给客体的 DPI（rcGetFBParam 3/4）与刷新率（5）：每会话不同。 */
    int density() const { return density_; }
    int refreshHz() const { return refreshHz_; }
    struct CopyStats { uint64_t direct, legacy; };
    CopyStats copyStats();
    struct ResourceCounts { size_t contexts, surfaces, images; };
    ResourceCounts resourceCounts();
    static void setProcess(pid_t process);
    void releaseProcess(pid_t process);
    bool directCopySupported() const { return hostVersion_ == 3 && directCopy_; }
private:
    struct Context {
        EGLContext egl;
        int version;
        FramebufferCopy copy;
        pid_t process;
        std::shared_ptr<SharedTextureSync> textures;
    };
    struct Surface {
        EGLSurface egl;
        int width, height;
        GLenum copyFormat;
        uint32_t color = 0;
        GLuint copyTexture = 0;
        int samples = 0;
        uint32_t config;
        pid_t process;
        EGLImageKHR copyImage = EGL_NO_IMAGE_KHR;
        uint32_t copyIdentity = 0;
    };
    struct Color {
        GLuint texture;
        EGLImageKHR image;
        int width, height;
        unsigned references = 1;
        std::chrono::steady_clock::time_point closedAt{};
        EGLSyncKHR sampled = EGL_NO_SYNC_KHR;
    };
    void collectClosedColors();
    EGLSyncKHR fence();
    void waitFence(EGLSyncKHR sync);
    void sampledColor(Color& color);
    class Current;
    struct Image { EGLImageKHR egl; pid_t process; };
    uint32_t next_ = 1;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext root_ = EGL_NO_CONTEXT;
    EGLSurface output_ = EGL_NO_SURFACE;
    GLuint copyFramebuffer_ = 0;
    std::unique_ptr<DesktopPresenter> presenter_;
    std::unique_ptr<GuestGpuSync> syncs_;
    std::vector<EGLConfig> configs_;
    std::map<uint32_t, Context> contexts_;
    std::map<uint32_t, Surface> surfaces_;
    std::map<uint32_t, Color> colors_;
    std::map<uint32_t, Image> images_;
    std::recursive_mutex mutex_;
    ColorPixels pixelTransfer_;
    std::mutex postMutex_;
    PFNEGLCREATEIMAGEKHRPROC createImage_;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage_;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC textureImage_;
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC renderbufferImage_;
    PFNEGLCREATESYNCKHRPROC createSync_ = nullptr;
    PFNEGLWAITSYNCKHRPROC waitSync_ = nullptr;
    PFNEGLDESTROYSYNCKHRPROC destroySync_ = nullptr;
    int width_, height_;
    int density_ = desktopdisplay::density;
    int refreshHz_ = desktopdisplay::refreshHz;
    int hostVersion_ = 2;
    bool directCopy_;
    uint64_t frames_ = 0, flushes_ = 0;
    uint64_t directFlushes_ = 0, legacyFlushes_ = 0;
    uint64_t cacheFlushes_ = 0;
    uint32_t lastColor_ = 0;
    static thread_local uint32_t currentContext_;
    static thread_local uint32_t currentSurface_;
    static thread_local pid_t currentProcess_;
    static thread_local std::shared_ptr<SharedTextureSync> currentTextures_;
};
