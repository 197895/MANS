#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mans {
namespace cpu {

int mans_compress_bytes(const std::string& dtype,
                        const std::uint8_t* input_data,
                        std::size_t input_size,
                        std::vector<std::uint8_t>& output_data,
                        std::uint32_t adm_threshold = 4000);

int mans_decompress_bytes(const std::string& dtype,
                          const std::uint8_t* input_data,
                          std::size_t input_size,
                          std::vector<std::uint8_t>& output_data);

int mans_compress_file(const std::string& dtype,
                       const std::string& input_file,
                       const std::string& output_file,
                       const std::string& cpu_bin_dir = "");

int mans_decompress_file(const std::string& dtype,
                         const std::string& input_file,
                         const std::string& output_file,
                         const std::string& cpu_bin_dir = "");

} // namespace cpu
} // namespace mans
