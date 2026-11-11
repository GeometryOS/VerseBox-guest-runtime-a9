#include "HostConnection.h"
#include "ThreadInfo.h"
#include "GLEncoder.h"
#include "GL2Encoder.h"
#include "TextureUploadEncoder.h"
#include "SocketStream.h"
#include "DesktopProtocol.h"
#include "gralloc_cb.h"
#include <cstdlib>

namespace {
class PrivateGL2Encoder final : public TextureUploadEncoder {
public:
    using TextureUploadEncoder::TextureUploadEncoder;
    uint64_t lockAndWriteDma(void*, uint32_t) override {
        throw std::runtime_error("Private GPU transport has no DMA command");
    }
};
class PrivateGralloc final : public Gralloc {
public:
    uint32_t getHostHandle(const native_handle_t* handle) override {
        const auto* buffer = static_cast<const cb_handle_t*>(handle);
        return cb_handle_t::validate(buffer) ? buffer->hostHandle : 0;
    }
    int getFormat(const native_handle_t* handle) override {
        const auto* buffer = static_cast<const cb_handle_t*>(handle);
        return cb_handle_t::validate(buffer) ? buffer->format : 0;
    }
} gralloc;
}

HostConnection::HostConnection()
    : m_stream(nullptr), m_glEnc(nullptr), m_gl2Enc(nullptr), m_vkEnc(nullptr),
      m_rcEnc(nullptr), m_grallocHelper(&gralloc), m_processPipe(nullptr),
      m_grallocOnly(true), m_noHostError(false) {}

HostConnection::~HostConnection() {
    delete m_glEnc;
    delete m_gl2Enc;
    delete m_rcEnc;
    delete m_stream;
}

HostConnection* HostConnection::connect(HostConnection* connection) {
    if (!connection) return nullptr;
    try {
        const char* socket = std::getenv("A10VM_GPU_SOCKET");
        if (!socket || !*socket) throw std::runtime_error("Missing A10VM_GPU_SOCKET");
        auto* stream = new SocketStream(connectLocal(socket));
        connection->m_stream = stream;
        desktopgl::handshake(*stream, true);
        connection->m_checksumHelper.setVersion(1);
        return connection;
    } catch (const std::exception& error) {
        ALOGE("Private GPU connection: %s", error.what());
        delete connection;
        return nullptr;
    }
}

HostConnection* HostConnection::get() { return getWithThreadInfo(getEGLThreadInfo()); }

HostConnection* HostConnection::getWithThreadInfo(EGLThreadInfo* info) {
    if (!info->hostConn) info->hostConn = connect(new HostConnection());
    return info->hostConn;
}

HostConnection* HostConnection::createUnique() { return connect(new HostConnection()); }
void HostConnection::teardownUnique(HostConnection* connection) { delete connection; }
void HostConnection::exit() {
    auto* info = getEGLThreadInfo();
    delete info->hostConn;
    info->hostConn = nullptr;
}

GLEncoder* HostConnection::glEncoder() {
    if (!m_glEnc) {
        m_glEnc = new GLEncoder(m_stream, checksumHelper());
    }
    return m_glEnc;
}

GL2Encoder* HostConnection::gl2Encoder() {
    if (!m_gl2Enc) {
        m_gl2Enc = new PrivateGL2Encoder(m_stream, checksumHelper());
        m_gl2Enc->setNoHostError(false);
    }
    return m_gl2Enc;
}

ExtendedRCEncoderContext* HostConnection::rcEncoder() {
    if (!m_rcEnc) {
        m_rcEnc = new ExtendedRCEncoderContext(m_stream, checksumHelper());
        m_rcEnc->setSyncImpl(SYNC_IMPL_NONE);
        m_rcEnc->setDmaImpl(DMA_IMPL_NONE);
        m_rcEnc->setGLESMaxVersion(GLES_MAX_VERSION_2);
        m_rcEnc->setHostComposition(HOST_COMPOSITION_NONE);
    }
    return m_rcEnc;
}

gl_client_context_t* HostConnection::s_getGLContext() { return get()->glEncoder(); }
gl2_client_context_t* HostConnection::s_getGL2Context() { return get()->gl2Encoder(); }

bool processPipeInit(renderControl_encoder_context_t*) { return true; }
