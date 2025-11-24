#pragma once
#include <vector>
#include <string>
#include <windows.h>

class SimpleGzip {
public:
    // Compresses data using Gzip format (currently Store-Only / No Compression)
    // Returns true on success, false on failure.
    static bool compress(const std::string& input, std::vector<BYTE>& output);

private:
    static void appendData(std::vector<BYTE>& output, const void* data, size_t size);
    static void appendByte(std::vector<BYTE>& output, BYTE b);
    static void appendU16(std::vector<BYTE>& output, UINT16 val);
    static void appendU32(std::vector<BYTE>& output, UINT32 val);
    static UINT32 calculateCRC32(const std::string& data);
};
