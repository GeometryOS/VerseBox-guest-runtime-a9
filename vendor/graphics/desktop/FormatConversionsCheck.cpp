#include "FormatConversions.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <time.h>
#include <utility>
#include <vector>

namespace {
void require(bool value, const char* operation) {
    if (!value) {
        std::fprintf(stderr, "format_conversion_error=%s\n", operation);
        std::exit(1);
    }
}

class GuardedBuffer {
public:
    explicit GuardedBuffer(size_t size) : size_(size) {
        size_t page = sysconf(_SC_PAGESIZE);
        size_t writable = (size + page - 1) / page * page;
        mappingSize_ = writable + 2 * page;
        mapping_ = static_cast<char*>(mmap(nullptr, mappingSize_, PROT_NONE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        require(mapping_ != MAP_FAILED, "guard mmap");
        require(mprotect(mapping_ + page, writable, PROT_READ | PROT_WRITE) == 0, "guard mprotect");
        data_ = mapping_ + page + writable - size;
        std::memset(data_, 0xa5, size_);
    }
    ~GuardedBuffer() { munmap(mapping_, mappingSize_); }
    char* data() { return data_; }
private:
    char* mapping_;
    char* data_;
    size_t mappingSize_, size_;
};

int clamp(int value) { return value < 0 ? 0 : value > 255 ? 255 : value; }

void checkConversion(int width, int height, bool planar, bool rgb565, int inset) {
    std::printf("format_conversion_case=%dx%d:%s:%s:%s\n", width, height,
                planar ? "yuv420p" : "yv12", rgb565 ? "rgb565" : "rgb888", inset ? "crop" : "full");
    int left = inset, top = inset;
    int right = width - inset - 1, bottom = height - inset - 1;
    uint32_t yStride = 0, cStride = 0, size = 0;
    if (planar) get_yuv420p_offsets(width, height, &yStride, &cStride, &size);
    else get_yv12_offsets(width, height, &yStride, &cStride, &size);
    require(yStride == unsigned(planar ? width : (width + 15) & ~15), "y stride");
    require(cStride == unsigned(planar ? width / 2 : (yStride / 2 + 15) & ~15), "chroma stride");
    size_t planeSize = cStride * (height / 2);
    require(size == yStride * height + 2 * planeSize, "planar size");
    size_t uOffset = yStride * height + (planar ? 0 : planeSize);
    size_t vOffset = yStride * height + (planar ? planeSize : 0);
    int bpp = rgb565 ? 2 : 3;
    GuardedBuffer source(width * height * bpp), destination(size);
    std::vector<unsigned char> expected(size, 0xa5);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int r = (x * 11 + y * 7) & 255;
            int g = (x * 3 + y * 19) & 255;
            int b = (x * 23 + y * 5) & 255;
            if (rgb565) {
                reinterpret_cast<uint16_t*>(source.data())[y * width + x] =
                    ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                r = ((r >> 3) * 527 + 23) >> 6;
                g = ((g >> 2) * 259 + 33) >> 6;
                b = ((b >> 3) * 527 + 23) >> 6;
            } else {
                auto* pixel = reinterpret_cast<unsigned char*>(source.data()) + (y * width + x) * 3;
                pixel[0] = r; pixel[1] = g; pixel[2] = b;
            }
            if (x < left || x > right || y < top || y > bottom) continue;
            expected[y * yStride + x] = clamp((77 * r + 150 * g + 29 * b) >> 8);
            if (!(x & 1) && !(y & 1)) {
                size_t chroma = (y / 2) * cStride + x / 2;
                expected[uOffset + chroma] = clamp(((-43 * r - 85 * g + 128 * b) >> 8) + 128);
                expected[vOffset + chroma] = clamp(((128 * r - 107 * g - 21 * b) >> 8) + 128);
            }
        }
    }
    if (rgb565) rgb565_to_yv12(destination.data(), source.data(), width, height, left, top, right, bottom);
    else if (planar) rgb888_to_yuv420p(destination.data(), source.data(), width, height, left, top, right, bottom);
    else rgb888_to_yv12(destination.data(), source.data(), width, height, left, top, right, bottom);
    require(std::memcmp(expected.data(), destination.data(), size) == 0, "rgb to yuv bytes and padding");

    int outWidth = right - left + 1, outHeight = bottom - top + 1;
    GuardedBuffer output(outWidth * outHeight * bpp);
    if (rgb565) yv12_to_rgb565(output.data(), destination.data(), width, height, left, top, right, bottom);
    else if (planar) yuv420p_to_rgb888(output.data(), destination.data(), width, height, left, top, right, bottom);
    else yv12_to_rgb888(output.data(), destination.data(), width, height, left, top, right, bottom);
    for (int y = top; y <= bottom; ++y) {
        for (int x = left; x <= right; ++x) {
            int luma = (expected[y * yStride + x] - 16) * 298;
            int u = expected[uOffset + (y / 2) * cStride + x / 2] - 128;
            int v = expected[vOffset + (y / 2) * cStride + x / 2] - 128;
            int r = clamp((luma + v * 409) / 256);
            int g = clamp((luma - v * 208 - u * 100) / 256);
            int b = clamp((luma + u * 517) / 256);
            int index = (y - top) * outWidth + x - left;
            if (rgb565) {
                require(reinterpret_cast<uint16_t*>(output.data())[index] ==
                        (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)), "yuv to rgb565 bytes");
            } else {
                const auto* pixel = reinterpret_cast<unsigned char*>(output.data()) + index * 3;
                require(pixel[0] == r && pixel[1] == g && pixel[2] == b, "yuv to rgb888 bytes");
            }
        }
    }
}

void checkYuvInput(int width, int height, bool planar, int inset) {
    uint32_t yStride, cStride, size;
    if (planar) get_yuv420p_offsets(width, height, &yStride, &cStride, &size);
    else get_yv12_offsets(width, height, &yStride, &cStride, &size);
    GuardedBuffer source(size);
    uint32_t random = 0x41b27f10;
    for (uint32_t i = 0; i < size; ++i) {
        random = random * 1664525 + 1013904223;
        source.data()[i] = random >> 24;
    }
    const int left = inset, top = inset, right = width - inset - 1, bottom = height - inset - 1;
    const int outWidth = right - left + 1, outHeight = bottom - top + 1;
    const auto* luma = reinterpret_cast<const unsigned char*>(source.data());
    const auto* u = luma + yStride * height + (planar ? 0 : cStride * height / 2);
    const auto* v = luma + yStride * height + (planar ? cStride * height / 2 : 0);
    GuardedBuffer output(outWidth * outHeight * 3);
    if (planar) yuv420p_to_rgb888(output.data(), source.data(), width, height, left, top, right, bottom);
    else yv12_to_rgb888(output.data(), source.data(), width, height, left, top, right, bottom);
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) {
        const int a = (luma[y * yStride + x] - 16) * 298;
        const int cb = u[(y / 2) * cStride + x / 2] - 128, cr = v[(y / 2) * cStride + x / 2] - 128;
        const auto* pixel = reinterpret_cast<const unsigned char*>(output.data()) +
                            ((y - top) * outWidth + x - left) * 3;
        require(pixel[0] == clamp((a + 409 * cr) / 256) &&
                pixel[1] == clamp((a - 100 * cb - 208 * cr) / 256) &&
                pixel[2] == clamp((a + 517 * cb) / 256), "full-range YUV scalar reference");
    }
}

uint64_t cpuNanos() {
    timespec now{};
    require(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0, "benchmark clock");
    return uint64_t(now.tv_sec) * 1000000000 + now.tv_nsec;
}

void benchmarkConversions() {
    constexpr int width = 1080, height = 1920, rounds = 40;
    uint32_t size;
    get_yv12_offsets(width, height, nullptr, nullptr, &size);
    std::vector<char> yuv(size, 128), rgb(width * height * 3);
    for (int i = 0; i < 4; ++i) {
        yv12_to_rgb888(rgb.data(), yuv.data(), width, height, 0, 0, width - 1, height - 1);
        rgb888_to_yv12(yuv.data(), rgb.data(), width, height, 0, 0, width - 1, height - 1);
    }
    uint64_t start = cpuNanos();
    for (int i = 0; i < rounds; ++i)
        yv12_to_rgb888(rgb.data(), yuv.data(), width, height, 0, 0, width - 1, height - 1);
    uint64_t toRgb = (cpuNanos() - start) / rounds;
    start = cpuNanos();
    for (int i = 0; i < rounds; ++i)
        rgb888_to_yv12(yuv.data(), rgb.data(), width, height, 0, 0, width - 1, height - 1);
    uint64_t toYuv = (cpuNanos() - start) / rounds;
    std::printf("{\"width\":%d,\"height\":%d,\"rounds\":%d,\"yuvToRgbCpuNanos\":%llu,"
                "\"rgbToYuvCpuNanos\":%llu,\"roundTripCpuNanos\":%llu,\"maxCpuNanos\":2000000,"
                "\"passed\":%s,\"limitation\":\"CPU pixel conversion only; excludes GPU and transport\"}\n",
                width, height, rounds, (unsigned long long)toRgb, (unsigned long long)toYuv,
                (unsigned long long)(toRgb + toYuv), toRgb + toYuv < 2000000 ? "true" : "false");
    require(toRgb + toYuv < 2000000, "1080p conversion CPU budget");
}
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc == 2 && !std::strcmp(argv[1], "--benchmark")) { benchmarkConversions(); return 0; }
    require(argc == 1, "unexpected argument");
    for (const auto& dimensions : {std::pair<int, int>{2, 2}, {16, 16}, {18, 18}, {30, 18}, {32, 32},
                                  {34, 18}, {66, 32}, {576, 288}}) {
        for (int inset : {0, 1, 2, 3, 5}) {
            if (dimensions.first <= inset * 2 || dimensions.second <= inset * 2) continue;
            checkConversion(dimensions.first, dimensions.second, false, false, inset);
            checkConversion(dimensions.first, dimensions.second, true, false, inset);
            checkConversion(dimensions.first, dimensions.second, false, true, inset);
            checkYuvInput(dimensions.first, dimensions.second, false, inset);
            checkYuvInput(dimensions.first, dimensions.second, true, inset);
        }
    }
    std::puts("desktop_format_conversions=1");
    return 0;
}
