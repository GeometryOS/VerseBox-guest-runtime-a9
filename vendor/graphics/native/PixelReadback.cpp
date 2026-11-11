#include "GL2Encoder.h"
#include "GLESTextureUtils.h"

void IOStream::readbackPixels(void* context, int width, int height,
                             unsigned int format, unsigned int type, void* pixels) {
    const auto size = glesv2_enc::pixelDataSize(context, width, height, format, type, 1);
    int startOffset = 0, alignedPixelRowSize = 0, totalRowSize = 0, skipRows = 0;
    static_cast<GL2Encoder*>(context)->state()->getPackingOffsets2D(
        width, height, format, type, &startOffset, &alignedPixelRowSize, &totalRowSize, &skipRows);
    const auto pixelRowSize = GLESTextureUtils::computeTotalImageSize(
        width, 1, 1, format, type, 1, 0, 0, 0, 0, 0);
    if (!size || (startOffset == 0 && pixelRowSize == totalRowSize)) {
        readback(pixels, size);
        return;
    }

    // Consume all wire bytes while preserving the caller's skipped and padding bytes.
    const auto paddingSize = totalRowSize - pixelRowSize;
    std::vector<unsigned char> discarded(std::max(startOffset, paddingSize));
    if (startOffset) readback(discarded.data(), startOffset);
    auto* row = static_cast<unsigned char*>(pixels) + startOffset;
    for (int y = 0; y < height; ++y) {
        readback(row, pixelRowSize);
        if (paddingSize) readback(discarded.data(), paddingSize);
        row += totalRowSize;
    }
}
