#pragma once
#include "GL2Encoder.h"
#include "GuestTextureContexts.h"

class TextureUploadEncoder : public GL2Encoder {
public:
    TextureUploadEncoder(IOStream* stream, ChecksumCalculator* checksum)
        : GL2Encoder(stream, checksum), image_(glTexImage2D), subImage_(glTexSubImage2D),
          parameteri_(glTexParameteri), parameteriv_(glTexParameteriv),
          parameterf_(glTexParameterf), parameterfv_(glTexParameterfv) {
        gl2_encoder_context_t wire(stream, checksum);
        acknowledged_ = wire.glGetIntegerv;
        glTexImage2D = publish<&TextureUploadEncoder::image_>;
        glTexSubImage2D = publish<&TextureUploadEncoder::subImage_>;
        glTexParameteri = publish<&TextureUploadEncoder::parameteri_>;
        glTexParameteriv = publish<&TextureUploadEncoder::parameteriv_>;
        glTexParameterf = publish<&TextureUploadEncoder::parameterf_>;
        glTexParameterfv = publish<&TextureUploadEncoder::parameterfv_>;
    }
private:
    template<auto Original, typename... Arguments>
    static void publish(void* self, Arguments... arguments) {
        auto& encoder = *static_cast<TextureUploadEncoder*>(self);
        (encoder.*Original)(self, arguments...);
        if (guesttextures::shared(encoder.shared().constPtr())) {
            // A raw protocol query drains and acknowledges CPU commands without
            // consuming GL errors or waiting for the GPU to become idle.
            GLint binding;
            encoder.acknowledged_(self, GL_TEXTURE_BINDING_2D, &binding);
        }
    }
    glGetIntegerv_client_proc_t acknowledged_;
    glTexImage2D_client_proc_t image_;
    glTexSubImage2D_client_proc_t subImage_;
    glTexParameteri_client_proc_t parameteri_;
    glTexParameteriv_client_proc_t parameteriv_;
    glTexParameterf_client_proc_t parameterf_;
    glTexParameterfv_client_proc_t parameterfv_;
};
