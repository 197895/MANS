// pans_utils.h
#ifndef PANS_UTILS_H
#define PANS_UTILS_H

#include <string>
#include <cstdint>

void compressFileWithANS(
    const std::string& inputFilePath,
    const std::string& tempFilePath,
    uint32_t& batchSize,
    uint32_t& compressedSize,
    int precision
);

void decompressFileWithANS(
    const std::string& tempFilePath,
    const std::string& outputFilePath, 
    int precision
);

#endif // PANS_UTILS_H