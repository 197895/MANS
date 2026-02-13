// compiler: g++ -std=c++17 -O3 cpu_mans_decompress.cpp -o cpu_mans_decompress
// exec: OMP_NUM_THREADS=4 ./cpu_mans_decompress u2 input.bin output.u2
//       OMP_NUM_THREADS=4 ./cpu_mans_decompress u4 input.bin output.u4

#include <iostream>
#include <string>

#include "mans_file_codec.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Use: " << argv[0] << " <u2|u4> <input_bin_file> <output_u2/u4_file>\n";
        return 1;
    }

    const std::string dtype = argv[1];
    const std::string input_file = argv[2];
    const std::string output_file = argv[3];
    return mans::cpu::mans_decompress_file(dtype, input_file, output_file);
}

