// compiler： g++ -std=c++17 -O3 cpu_mans_decompress.cpp -o cpu_mans_decompress
// exec   :  OMP_NUM_THREADS=4 ./cpu_mans_decompress u2 input.bin output.u2
//           OMP_NUM_THREADS=4 ./cpu_mans_decompress u4 input.bin output.u4

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <cstdlib>  // remove

#include "file_utils.h"
#include "adm/adm_utils.h"
#include "pans/pans_utils.h"

struct MansHeader {
    std::uint8_t codec;  // 1 = ADM, 2 = ANS
};
static_assert(sizeof(MansHeader) == 1, "MansHeader must be 1 byte");

// ===== strip_header_and_save =====
// 与 prepend_header_and_save 呼应
inline bool strip_header_and_save(
    const std::string& input_file,
    const std::string& payload_file,
    std::uint8_t& codec)
{
    std::vector<std::uint8_t> all;
    if (!load_u8_file(input_file, all)) {
        std::cerr << "Failed to load input file: " << input_file << "\n";
        return false;
    }
    if (all.size() < sizeof(MansHeader)) {
        std::cerr << "File too small, invalid mans format.\n";
        return false;
    }

    codec = all[0];
    std::vector<std::uint8_t> payload(all.begin() + 1, all.end());

    if (!save_u8_file(payload_file, payload)) {
        std::cerr << "Failed to write payload file: " << payload_file << "\n";
        return false;
    }

    return true;
}


int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Use: " << argv[0]
                  << " <u2|u4> <input_bin_file> <output_u2/u4_file>\n";
        return 1;
    }

    std::string dtype       = argv[1];  // "u2" / "u4"
    std::string input_file  = argv[2];
    std::string output_file = argv[3];

    bool is_u2 = (dtype == "-u2" || dtype == "u2");
    bool is_u4 = (dtype == "-u4" || dtype == "u4");

    if (!is_u2 && !is_u4) {
        std::cerr << "Unknown data type flag: " << dtype
                  << "\nUse: u2 or u4 (or -u2/-u4)\n";
        return 1;
    }

    // 提取 header 和 payload
    std::string tmp_in = input_file + ".tmp_payload";
    std::uint8_t codec;
    if (!strip_header_and_save(input_file, tmp_in, codec)) {
        return 1;
    }

    // PANS 解压：直接调用函数
    std::string tmp_pans_out;
    int precision = 10;

    if (codec == 1) {
        tmp_pans_out = output_file + ".adm";
    } else if (codec == 2) {
        tmp_pans_out = output_file;
    } else {
        std::cerr << "Unknown codec type in mans header: " << int(codec) << "\n";
        return 1;
    }

    decompressFileWithANS(
        tmp_in.c_str(),
        tmp_pans_out.c_str(),
        precision
    );

    // ADM 解压
    if (codec == 1) {
        std::vector<std::uint8_t> adm_data;
        if (!load_u8_file(tmp_pans_out, adm_data)) {
            std::cerr << "Failed to load ADM tmp file: " << tmp_pans_out << "\n";
            return 1;
        }

        if (is_u2) {
            std::vector<std::uint16_t> recovered;
            adm_decompress_and_benchmark(adm_data, recovered);
            if (!save_u16_file(output_file, recovered)) {
                std::cerr << "Failed to write output file: " << output_file << "\n";
                return 1;
            }
        } else {
            std::vector<std::uint32_t> recovered;
            adm_decompress_and_benchmark(adm_data, recovered);
            if (!save_u32_file(output_file, recovered)) {
                std::cerr << "Failed to write output file: " << output_file << "\n";
                return 1;
            }
        }
    }

    std::remove(tmp_in.c_str());

    std::cout << "mans decompress finished! Output: " << output_file << "\n";
    return 0;
}
