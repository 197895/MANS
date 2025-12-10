#include <iostream>
#include "pans_utils.h"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.file> <output.file>" << std::endl;
        return 1;
    }
    
    int precision = 10; 
    
    decompressFileWithANS(
        argv[1],
        argv[2],
        precision
    );
        
    std::cout << "Decompression completed successfully." << std::endl;
    return 0;
}