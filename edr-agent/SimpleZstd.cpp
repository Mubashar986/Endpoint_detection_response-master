#include "SimpleZstd.hpp"
#include <zstd.h>
#include <iostream>

bool SimpleZstd::compress(const std::string& input, std::vector<BYTE>& output) {
    size_t const inputSize = input.size();
    
    // 1. Calculate maximum compressed size
    size_t const maxDestSize = ZSTD_compressBound(inputSize);
    output.resize(maxDestSize);

    // 2. Compress
    // Level 3 is default, good balance of speed/ratio.
    size_t const cSize = ZSTD_compress(output.data(), maxDestSize, input.data(), inputSize, 3);

    // 3. Check for errors
    if (ZSTD_isError(cSize)) {
        std::cerr << "[Zstd] Compression failed: " << ZSTD_getErrorName(cSize) << std::endl;
        return false;
    }

    // 4. Resize output to actual compressed size
    output.resize(cSize);

    return true;
}
