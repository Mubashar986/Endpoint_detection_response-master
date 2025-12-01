#pragma once
#include <vector>
#include <string>
#include <windows.h>

class SimpleZstd {
public:
    // Compresses data using Zstandard
    // Returns true on success, false on failure.
    static bool compress(const std::string& input, std::vector<BYTE>& output);
};
