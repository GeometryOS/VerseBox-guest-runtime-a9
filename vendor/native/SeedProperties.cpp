#include "PrivateProperties.h"
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[2]);
    PrivateProperties properties;
    if (!input || !properties.create(argv[1])) return 1;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto split = line.find('=');
        if (split == std::string::npos ||
                !properties.set(line.substr(0, split), line.substr(split + 1))) {
            std::cerr << "Property seed failed: " << line.substr(0, split) << '\n';
            return 1;
        }
    }
    if (input.bad() || properties.get("ro.build.version.sdk") != "29") return 1;
    std::cout << "seed_sdk=29\nseed_count=" << properties.count() << "\nseed_complete=1\n";
    return 0;
}
