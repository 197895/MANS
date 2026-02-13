// compiler: g++ -std=c++17 -O3 cpu_mans_compress.cpp -o cpu_mans_compress
// exec: OMP_NUM_THREADS=4 ./cpu_mans_compress u2 input.u2 output.bin
//       OMP_NUM_THREADS=4 ./cpu_mans_compress u4 input.u4 output.bin

#include <iostream>
#include <string>

#include "mans_file_codec.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Use: " << argv[0] << " <u2|u4> <input_file> <output_bin_file>\n";
        return 1;
    }

    const std::string dtype = argv[1];
    const std::string input_file = argv[2];
    const std::string output_file = argv[3];
    return mans::cpu::mans_compress_file(dtype, input_file, output_file);
}

