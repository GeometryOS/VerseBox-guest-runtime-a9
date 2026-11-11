#include "ChecksumCalculatorThreadInfo.h"
#include "emugl/common/logging.h"

#include <cstdarg>
#include <cstdio>
#include <stdexcept>

namespace {
thread_local ChecksumCalculatorThreadInfo* current = nullptr;

void logMessage(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}
}

namespace emugl {
Logger emugl_logger = logMessage;
Logger emugl_cxt_logger = logMessage;
}

ChecksumCalculatorThreadInfo::ChecksumCalculatorThreadInfo() {
    if (current) throw std::logic_error("Nested checksum thread context");
    current = this;
}

ChecksumCalculatorThreadInfo::~ChecksumCalculatorThreadInfo() { current = nullptr; }
ChecksumCalculator& ChecksumCalculatorThreadInfo::get() { return m_protocol; }
uint32_t ChecksumCalculatorThreadInfo::getMaxVersion() { return ChecksumCalculator::getMaxVersion(); }

bool ChecksumCalculatorThreadInfo::setVersion(uint32_t version) {
    if (!current) throw std::logic_error("Missing checksum thread context");
    return current->m_protocol.setVersion(version);
}

bool ChecksumCalculatorThreadInfo::writeChecksum(ChecksumCalculator* calc, void* data,
        size_t size, void* checksum, size_t checksumSize) {
    calc->addBuffer(data, size);
    return calc->writeChecksum(checksum, checksumSize);
}

bool ChecksumCalculatorThreadInfo::validate(ChecksumCalculator* calc, void* data,
        size_t size, void* checksum, size_t checksumSize) {
    calc->addBuffer(data, size);
    return calc->validate(checksum, checksumSize);
}

void ChecksumCalculatorThreadInfo::validOrDie(ChecksumCalculator* calc, void* data,
        size_t size, void* checksum, size_t checksumSize, const char* message) {
    if (!validate(calc, data, size, checksum, checksumSize)) {
        throw std::runtime_error(message);
    }
}
