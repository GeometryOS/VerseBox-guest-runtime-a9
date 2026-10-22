#pragma once
#include "PrivateProperties.h"
#include <cstdio>
#include <stdexcept>
#include <string>

inline void require(bool passed, const char* name) {
    if (!passed) throw std::runtime_error(name);
    std::printf("CHECK\t%s\t1\n", name);
}

int mappingCount(const std::string& directory, const char* permissions = nullptr);
void crossProcess(PrivateProperties& writer, const std::string& directory,
                  const char* program, const char* linker);
int readerMain(const char* directory, int channel);
