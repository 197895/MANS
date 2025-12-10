// compiler： g++ -std=c++17 -O3 cpu_mans_compress.cpp -o cpu_mans_compress
// exec   :  OMP_NUM_THREADS=4 ./cpu_mans_compress u2 input.u2 output.bin
//           OMP_NUM_THREADS=4 ./cpu_mans_compress u4 input.u4 output.bin

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <limits>
#include <cstdlib>

#include "file_utils.h"
#include "adm/adm_utils.h"
#include "pans/pans_utils.h"

const int threshold = 4000;

struct MansHeader {
    std::uint8_t codec;  // 1 = ADM, 2 = ANS
};
static_assert(sizeof(MansHeader) == 1, "MansHeader must be 1 byte");

// ===== decide_use_adm =====
template<typename T>
bool decide_use_adm(const std::vector<T>& data, const std::string& dtype) {
    const std::size_t block_size = 512;
    std::uint64_t max_block_diff = 0;

    for (std::size_t i = 0; i < data.size(); i += block_size) {
        std::size_t end = std::min(i + block_size, data.size());

        T bmin = std::numeric_limits<T>::max();
        T bmax = std::numeric_limits<T>::min();

        for (std::size_t j = i; j < end; ++j) {
            T v = data[j];
            if (v < bmin) bmin = v;
            if (v > bmax) bmax = v;
        }

        std::uint64_t diff = static_cast<std::uint64_t>(bmax) - static_cast<std::uint64_t>(bmin);
        if (diff > max_block_diff) {
            max_block_diff = diff;
        }
    }

    std::cout << "[mans] " << dtype << " block range (block_size=512): max_diff="
              << max_block_diff << "\n";

    return (max_block_diff <= threshold);
}

inline bool prepend_header_and_save(
    const std::string& payload_file,
    const std::string& output_file,
    std::uint8_t codec)
{
    std::vector<std::uint8_t> payload;
    if (!load_u8_file(payload_file, payload)) {
        std::cerr << "Failed to load payload file: " << payload_file << "\n";
        return false;
    }

    std::vector<std::uint8_t> final_data;
    final_data.reserve(1 + payload.size());
    final_data.push_back(codec);
    final_data.insert(final_data.end(), payload.begin(), payload.end());

    if (!save_u8_file(output_file, final_data)) {
        std::cerr << "Failed to write output file: " << output_file << "\n";
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Use: " << argv[0]
                  << " <u2|u4> <input_file> <output_bin_file>\n";
        return 1;
    }

    std::string dtype       = argv[1];
    std::string input_file  = argv[2];
    std::string output_file = argv[3];

    bool is_u2 = (dtype == "-u2" || dtype == "u2");
    bool is_u4 = (dtype == "-u4" || dtype == "u4");

    if (!is_u2 && !is_u4) {
        std::cerr << "Unknown data type flag: " << dtype
                  << "\nUse: u2 or u4 (or -u2/-u4)\n";
        return 1;
    }

    // 1. 读取数据 & 按 512 块计算 max-min
    std::vector<std::uint16_t> data_u16;
    std::vector<std::uint32_t> data_u32;
    bool use_adm = false;

    if (is_u2) {
        if (!load_u16_file(input_file, data_u16)) {
            std::cerr << "Failed to load input file: " << input_file << "\n";
            return 1;
        }
        if (data_u16.empty()) {
            std::cerr << "Input file is empty.\n";
            return 1;
        }
        use_adm = decide_use_adm(data_u16, dtype);
    } else {
        if (!load_u32_file(input_file, data_u32)) {
            std::cerr << "Failed to load input file: " << input_file << "\n";
            return 1;
        }
        if (data_u32.empty()) {
            std::cerr << "Input file is empty.\n";
            return 1;
        }
        use_adm = decide_use_adm(data_u32, dtype);
    }

    std::string tmp_out = output_file + ".adm";
    std::string tmp_pans_in;

    std::vector<std::uint8_t> adm_output;

    MansHeader mh{};
    if (use_adm) {
        mh.codec = 1; // ADM
        if (is_u2) {
            adm_compress_and_benchmark(data_u16, adm_output);
        } else {
            adm_compress_and_benchmark(data_u32, adm_output);
        }
        if (!save_u8_file(tmp_out, adm_output)) {
            std::cerr << "Failed to write ADM output: " << tmp_out << "\n";
            return 1;
        }
        tmp_pans_in = tmp_out;
    } else {
        mh.codec = 2; // without ADM
        tmp_pans_in = input_file;
    }

    // PANS 压缩：直接调用函数
    std::string tmp_pans_out = output_file;
    uint32_t batchSize = 0;
    uint32_t compressedSize = 0;
    int precision = 10;

    compressFileWithANS(
        tmp_pans_in.c_str(),
        tmp_pans_out.c_str(),
        batchSize,
        compressedSize,
        precision
    );

    if (compressedSize > 0) {
        std::printf("[pans] compress ratio: %f\n", 1.0 * batchSize / compressedSize);
    }

    // 封装后的调用
    if (!prepend_header_and_save(tmp_pans_out, output_file, mh.codec)) {
        return 1;
    }

    std::cout << "mans compress finished! Write to " << output_file
              << " (codec=" << int(mh.codec) << ")\n";
    return 0;
}
