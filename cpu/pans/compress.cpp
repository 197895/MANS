#include <iostream>
#include <cstdint>
#include "pans_utils.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.file> <output.file>" << std::endl;
        return 1;
    }
    
    uint32_t batchSize = 0;
    uint32_t compressedSize = 0;
    int precision = 10; 
    
    compressFileWithANS(
        argv[1], 
        argv[2],
        batchSize,
        compressedSize,
        precision
    );
        
    // 避免除以0的潜在风险
    if (compressedSize > 0) {
        printf("compress ratio: %f\n", 1.0 * batchSize / compressedSize);
    }
    std::cout << "Compression completed successfully." << std::endl;
    
    return 0;
}