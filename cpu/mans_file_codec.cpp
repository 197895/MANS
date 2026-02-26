#include "mans_file_codec.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

#include "adm/adm.h"
#include "pans/CpuANSDecode.h"
#include "pans/CpuANSEncode.h"
extern "C" {
#include "fse/include/fse.h"
}

namespace mans {
namespace cpu {
namespace {

constexpr std::uint32_t kDefaultThreshold = 4000;
constexpr std::uint32_t kModeP = 0;
constexpr std::uint32_t kModeR = 1;
constexpr std::uint8_t kCodecAdmPans = 1;
constexpr std::uint8_t kCodecRawPans = 2;
constexpr std::uint8_t kCodecAdmFse = 3;
constexpr std::uint8_t kCodecRawFse = 4;
constexpr std::size_t kFseFrameHeaderSize = 20;
constexpr std::size_t kFseBlockHeaderSize = 9;
constexpr std::uint32_t kFseFrameBlockSize = 32U * 1024U;

struct MansHeader {
    std::uint8_t codec; // 1=ADM+PANS, 2=RAW+PANS, 3=ADM+FSE, 4=RAW+FSE
};
static_assert(sizeof(MansHeader) == 1, "MansHeader must be 1 byte");

std::uint32_t normalize_mode(std::uint32_t mode) {
    return (mode == kModeP) ? kModeP : kModeR;
}

std::uint32_t read_le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t read_le64(const std::uint8_t* p) {
    return static_cast<std::uint64_t>(read_le32(p)) |
           (static_cast<std::uint64_t>(read_le32(p + 4)) << 32);
}

bool parse_fse_frame(const std::uint8_t* compressed_data,
                     std::size_t compressed_len,
                     std::size_t& frame_len,
                     std::size_t& decompressed_len,
                     std::string* error = nullptr) {
    frame_len = 0;
    decompressed_len = 0;

    auto set_error = [&](const char* msg) {
        if (error) {
            *error = msg;
        }
    };

    if (!compressed_data) {
        set_error("compressed_data is null");
        return false;
    }
    if (compressed_len < kFseFrameHeaderSize) {
        set_error("compressed_len too small for FSE frame header");
        return false;
    }
    if (compressed_data[0] != 'M' || compressed_data[1] != 'F' ||
        compressed_data[2] != 'S' || compressed_data[3] != 'E') {
        set_error("invalid FSE magic");
        return false;
    }

    const std::uint32_t block_size = read_le32(compressed_data + 4);
    if (block_size != kFseFrameBlockSize) {
        set_error("unexpected FSE block size");
        return false;
    }

    const std::uint64_t raw_size_u64 = read_le64(compressed_data + 8);
    if (raw_size_u64 > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        set_error("raw size overflows size_t");
        return false;
    }
    const std::size_t raw_size = static_cast<std::size_t>(raw_size_u64);
    const std::uint32_t block_count = read_le32(compressed_data + 16);

    std::size_t offset = kFseFrameHeaderSize;
    std::size_t raw_acc = 0;
    for (std::uint32_t i = 0; i < block_count; ++i) {
        if (offset > compressed_len || compressed_len - offset < kFseBlockHeaderSize) {
            set_error("truncated FSE block header");
            return false;
        }

        const std::uint8_t mode = compressed_data[offset];
        const std::uint32_t block_raw = read_le32(compressed_data + offset + 1);
        const std::uint32_t block_stored = read_le32(compressed_data + offset + 5);
        offset += kFseBlockHeaderSize;

        if (mode > 2) {
            set_error("invalid FSE block mode");
            return false;
        }
        if (block_stored > compressed_len - offset) {
            set_error("truncated FSE block payload");
            return false;
        }
        if (raw_acc > std::numeric_limits<std::size_t>::max() - static_cast<std::size_t>(block_raw)) {
            set_error("raw size accumulation overflow");
            return false;
        }
        raw_acc += static_cast<std::size_t>(block_raw);
        offset += static_cast<std::size_t>(block_stored);
    }

    if (raw_acc != raw_size) {
        set_error("raw size mismatch in FSE frame");
        return false;
    }
    if (offset != compressed_len) {
        set_error("extra bytes after FSE frame");
        return false;
    }

    frame_len = offset;
    decompressed_len = raw_size;
    return true;
}

bool load_u8_file(const std::string& filename, std::vector<std::uint8_t>& data) {
    std::ifstream in(filename, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    std::streamsize size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    return static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), size));
}

bool save_u8_file(const std::string& filename, const std::vector<std::uint8_t>& data) {
    std::ofstream out(filename, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        return false;
    }
    if (!data.empty()) {
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }
    return static_cast<bool>(out);
}

template <typename T>
bool bytes_to_vector(const std::vector<std::uint8_t>& bytes, std::vector<T>& out) {
    if (bytes.size() % sizeof(T) != 0) {
        return false;
    }
    out.resize(bytes.size() / sizeof(T));
    if (!bytes.empty()) {
        std::memcpy(out.data(), bytes.data(), bytes.size());
    }
    return true;
}

template <typename T>
std::vector<std::uint8_t> vector_to_bytes(const std::vector<T>& in) {
    std::vector<std::uint8_t> out(in.size() * sizeof(T));
    if (!out.empty()) {
        std::memcpy(out.data(), in.data(), out.size());
    }
    return out;
}

template <typename T>
bool should_use_adm_t(const std::vector<std::uint8_t>& raw_bytes, std::uint32_t adm_threshold) {
    std::vector<T> data;
    if (!bytes_to_vector(raw_bytes, data)) {
        std::cerr << "Input byte size is not aligned for dtype.\n";
        return false;
    }
    if (data.empty()) {
        std::cerr << "Input file is empty.\n";
        return false;
    }

    const std::size_t block_size = 512;
    std::uint64_t max_block_diff = 0;
    for (std::size_t i = 0; i < data.size(); i += block_size) {
        std::size_t end = std::min(i + block_size, data.size());
        T bmin = std::numeric_limits<T>::max();
        T bmax = std::numeric_limits<T>::min();
        for (std::size_t j = i; j < end; ++j) {
            T v = data[j];
            if (v < bmin) {
                bmin = v;
            }
            if (v > bmax) {
                bmax = v;
            }
        }
        std::uint64_t diff =
            static_cast<std::uint64_t>(bmax) - static_cast<std::uint64_t>(bmin);
        if (diff > max_block_diff) {
            max_block_diff = diff;
        }
    }

    std::cout << "[mans] " << (sizeof(T) == 2 ? "u2" : "u4")
              << " block range (block_size=512): max_diff=" << max_block_diff << "\n";
    return (max_block_diff <= static_cast<std::uint64_t>(adm_threshold));
}

template <typename T>
bool adm_compress_bytes_t(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    std::vector<T> input_data;
    if (!bytes_to_vector(in, input_data)) {
        return false;
    }
    if (input_data.empty()) {
        out.clear();
        return true;
    }

    std::size_t num_elements = input_data.size();
    std::uint64_t gsize =
        (num_elements + adm::cmp_tblock_size * adm::cmp_chunk - 1) /
        (adm::cmp_tblock_size * adm::cmp_chunk);

    std::vector<int> output_lengths(gsize + 1);
    std::vector<std::uint8_t> codes(num_elements);
    std::vector<std::uint8_t> bit_signals;

    std::size_t len2 = 0;
    std::vector<std::uint8_t> centers_bytes;

    if constexpr (std::is_same_v<T, std::uint16_t>) {
        std::vector<std::uint16_t> centers(gsize);
        adm::compress_uint16(input_data, output_lengths, centers, codes, bit_signals);
        centers_bytes = vector_to_bytes(centers);
        len2 = centers_bytes.size();
    } else {
        std::vector<std::uint32_t> centers(gsize);
        adm::compress_uint32(input_data, output_lengths, centers, codes, bit_signals);
        centers_bytes = vector_to_bytes(centers);
        len2 = centers_bytes.size();
    }

    adm::FileHeader header{};
    header.num_elements = static_cast<std::uint64_t>(num_elements);
    header.gsize = gsize;
    header.len1 = output_lengths.size() * sizeof(int);
    header.len2 = len2;
    header.len3 = codes.size() * sizeof(std::uint8_t);
    header.len4 = bit_signals.size();

    const std::size_t total_size = sizeof(header) + header.len1 + header.len2 + header.len3 + header.len4;
    out.resize(total_size);

    std::size_t off = 0;
    std::memcpy(out.data() + off, &header, sizeof(header));
    off += sizeof(header);
    std::memcpy(out.data() + off, output_lengths.data(), header.len1);
    off += header.len1;
    std::memcpy(out.data() + off, centers_bytes.data(), header.len2);
    off += header.len2;
    std::memcpy(out.data() + off, codes.data(), header.len3);
    off += header.len3;
    if (header.len4 > 0) {
        std::memcpy(out.data() + off, bit_signals.data(), header.len4);
    }
    return true;
}

template <typename T>
bool adm_decompress_bytes_t(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    if (in.size() < sizeof(adm::FileHeader)) {
        return false;
    }
    adm::FileHeader header{};
    std::memcpy(&header, in.data(), sizeof(header));

    const std::size_t num_elements = static_cast<std::size_t>(header.num_elements);
    const std::size_t len1 = static_cast<std::size_t>(header.len1);
    const std::size_t len2 = static_cast<std::size_t>(header.len2);
    const std::size_t len3 = static_cast<std::size_t>(header.len3);
    const std::size_t len4 = static_cast<std::size_t>(header.len4);

    if (in.size() < sizeof(header) + len1 + len2 + len3 + len4) {
        return false;
    }

    std::size_t off = sizeof(header);
    std::vector<int> output_lengths(len1 / sizeof(int));
    std::memcpy(output_lengths.data(), in.data() + off, len1);
    off += len1;

    std::vector<std::uint8_t> codes(len3);
    std::vector<std::uint8_t> bit_signals(len4);

    std::vector<T> recovered(num_elements);
    if constexpr (std::is_same_v<T, std::uint16_t>) {
        std::vector<std::uint16_t> centers(len2 / sizeof(std::uint16_t));
        std::memcpy(centers.data(), in.data() + off, len2);
        off += len2;
        std::memcpy(codes.data(), in.data() + off, len3);
        off += len3;
        if (len4 > 0) {
            std::memcpy(bit_signals.data(), in.data() + off, len4);
        }
        adm::decompress_uint16(output_lengths, centers, codes, bit_signals, recovered);
    } else {
        std::vector<std::uint32_t> centers(len2 / sizeof(std::uint32_t));
        std::memcpy(centers.data(), in.data() + off, len2);
        off += len2;
        std::memcpy(codes.data(), in.data() + off, len3);
        off += len3;
        if (len4 > 0) {
            std::memcpy(bit_signals.data(), in.data() + off, len4);
        }
        adm::decompress_uint32(output_lengths, centers, codes, bit_signals, recovered);
    }

    out = vector_to_bytes(recovered);
    return true;
}

void* aligned_alloc_bytes(std::size_t align, std::size_t bytes) {
    if (bytes == 0) {
        return nullptr;
    }
    const std::size_t padded = ((bytes + align - 1) / align) * align;
    return std::aligned_alloc(align, padded);
}

bool pans_compress_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    if (in.empty()) {
        out.clear();
        return true;
    }

    constexpr int precision = 10;
    const std::uint32_t in_size = static_cast<std::uint32_t>(in.size());

    std::uint32_t out_size = 0;
    auto* enc_ptrs = static_cast<std::uint8_t*>(std::malloc(cpu_ans::getMaxCompressedSize(in_size)));
    if (!enc_ptrs) {
        return false;
    }
    auto* header_out = reinterpret_cast<cpu_ans::ANSCoalescedHeader*>(enc_ptrs);

    std::uint32_t max_num_compressed_blocks = 0;
    std::uint32_t uncoalesced_block_stride = cpu_ans::getMaxBlockSizeUnCoalesced(cpu_ans::kDefaultBlockSize);

    auto* table = static_cast<cpu_ans::uint4*>(std::malloc(sizeof(cpu_ans::uint4) * cpu_ans::kNumSymbols));
    auto* temp_histogram = static_cast<std::uint32_t*>(std::malloc(sizeof(std::uint32_t) * cpu_ans::kNumSymbols));
    auto* compressed_blocks_host = static_cast<std::uint8_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint8_t) * in_size * 2));

    std::uint32_t max_blocks_guess =
        (in_size + cpu_ans::kDefaultBlockSize - 1) / cpu_ans::kDefaultBlockSize;
    auto* compressed_words_host = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * max_blocks_guess));
    auto* compressed_words_host_prefix = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * max_blocks_guess));
    auto* compressed_words_prefix_host = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * max_blocks_guess));

    if (!table || !temp_histogram || !compressed_blocks_host || !compressed_words_host ||
        !compressed_words_host_prefix || !compressed_words_prefix_host) {
        std::free(enc_ptrs);
        std::free(table);
        std::free(temp_histogram);
        std::free(compressed_blocks_host);
        std::free(compressed_words_host);
        std::free(compressed_words_host_prefix);
        std::free(compressed_words_prefix_host);
        return false;
    }

    cpu_ans::ansEncode(table, temp_histogram, precision,
                       const_cast<std::uint8_t*>(in.data()), in_size,
                       enc_ptrs, &out_size, header_out,
                       max_num_compressed_blocks, uncoalesced_block_stride,
                       compressed_blocks_host, compressed_words_host,
                       compressed_words_host_prefix, compressed_words_prefix_host);

    auto* block_words_out = header_out->getBlockWords(max_num_compressed_blocks);

    std::uint32_t i = 0;
    for (; i + 1 < max_num_compressed_blocks; ++i) {
        auto* uncoalesced_block = compressed_blocks_host + i * uncoalesced_block_stride;
        auto* warp_state_out = reinterpret_cast<cpu_ans::ANSWarpState*>(uncoalesced_block);
        for (int j = 0; j < cpu_ans::kWarpSize; ++j) {
            header_out->getWarpStates()[i].warpState[j] = warp_state_out->warpState[j];
        }
        block_words_out[i] = cpu_ans::uint2{
            (cpu_ans::kDefaultBlockSize << 16) | compressed_words_host[i],
            compressed_words_prefix_host[i]};
    }
    if (max_num_compressed_blocks > 0) {
        auto* uncoalesced_block = compressed_blocks_host + i * uncoalesced_block_stride;
        auto* warp_state_out = reinterpret_cast<cpu_ans::ANSWarpState*>(uncoalesced_block);
        for (int j = 0; j < cpu_ans::kWarpSize; ++j) {
            header_out->getWarpStates()[i].warpState[j] = warp_state_out->warpState[j];
        }
        std::uint32_t last_block_words = in_size % cpu_ans::kDefaultBlockSize;
        last_block_words = (last_block_words == 0) ? cpu_ans::kDefaultBlockSize : last_block_words;
        block_words_out[i] = cpu_ans::uint2{
            (last_block_words << 16) | compressed_words_host[i],
            compressed_words_prefix_host[i]};
    }

    const std::size_t overhead = header_out->getCompressedOverhead(max_num_compressed_blocks);
    out.clear();
    out.reserve(out_size);
    out.insert(out.end(), enc_ptrs, enc_ptrs + overhead);

    for (std::uint32_t b = 0; b < max_num_compressed_blocks; ++b) {
        auto* uncoalesced_block = compressed_blocks_host + b * uncoalesced_block_stride;
        std::uint32_t num_words = compressed_words_host[b];
        std::uint32_t limit_end =
            cpu_ans::divUp(num_words, cpu_ans::kBlockAlignment / sizeof(cpu_ans::ANSEncodedT));
        auto* in_t = reinterpret_cast<const cpu_ans::uint4*>(uncoalesced_block + sizeof(cpu_ans::ANSWarpState));
        const std::uint8_t* byte_ptr = reinterpret_cast<const std::uint8_t*>(in_t);
        out.insert(out.end(), byte_ptr, byte_ptr + (limit_end << 4));
    }
    if (out.size() != out_size) {
        out.resize(out_size);
    }

    std::free(enc_ptrs);
    std::free(table);
    std::free(temp_histogram);
    std::free(compressed_blocks_host);
    std::free(compressed_words_host);
    std::free(compressed_words_host_prefix);
    std::free(compressed_words_prefix_host);
    return true;
}

bool pans_decompress_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    if (in.size() < sizeof(cpu_ans::ANSCoalescedHeader)) {
        return false;
    }
    auto* header = reinterpret_cast<const cpu_ans::ANSCoalescedHeader*>(in.data());
    const std::uint32_t total_compressed_size = const_cast<cpu_ans::ANSCoalescedHeader*>(header)->getTotalCompressedSize();
    const std::uint32_t batch_size = const_cast<cpu_ans::ANSCoalescedHeader*>(header)->getTotalUncompressedWords();
    if (in.size() < total_compressed_size) {
        return false;
    }

    constexpr int precision = 10;
    auto* symbol = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * (1U << precision)));
    auto* pdf = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * (1U << precision)));
    auto* cdf = static_cast<std::uint32_t*>(
        aligned_alloc_bytes(cpu_ans::kBlockAlignment, sizeof(std::uint32_t) * (1U << precision)));
    if (!symbol || !pdf || !cdf) {
        std::free(symbol);
        std::free(pdf);
        std::free(cdf);
        return false;
    }

    out.resize(batch_size);
    cpu_ans::ansDecode(symbol, pdf, cdf, precision,
                       const_cast<std::uint8_t*>(in.data()), out.data());

    std::free(symbol);
    std::free(pdf);
    std::free(cdf);
    return true;
}

bool fse_compress_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    if (in.empty()) {
        out.clear();
        return true;
    }

    const std::size_t bound = FSE_compressBound(in.size());
    if (bound == 0) {
        return false;
    }

    out.resize(bound);
    const std::size_t csize = FSE_compress(out.data(), bound, in.data(), in.size());
    if (FSE_isError(csize)) {
        return false;
    }
    out.resize(csize);
    return true;
}

bool fse_decompress_bytes(const std::vector<std::uint8_t>& in, std::vector<std::uint8_t>& out) {
    if (in.empty()) {
        out.clear();
        return true;
    }

    std::size_t frame_len = 0;
    std::size_t raw_len = 0;
    std::string parse_error;
    if (!parse_fse_frame(in.data(), in.size(), frame_len, raw_len, &parse_error)) {
        return false;
    }
    if (frame_len != in.size()) {
        return false;
    }

    out.resize(raw_len);
    const std::size_t dsize = FSE_decompress(out.data(), raw_len, in.data(), in.size());
    if (FSE_isError(dsize) || dsize != raw_len) {
        return false;
    }
    return true;
}

} // namespace

int mans_compress_file(const std::string& dtype,
                       const std::string& input_file,
                       const std::string& output_file,
                       const std::string& cpu_bin_dir,
                       std::uint32_t mans_mode) {
    (void)cpu_bin_dir;

    std::vector<std::uint8_t> input_bytes;
    if (!load_u8_file(input_file, input_bytes)) {
        std::cerr << "Failed to load input file: " << input_file << "\n";
        return 1;
    }

    std::vector<std::uint8_t> final_data;
    int rc = mans_compress_bytes(dtype, input_bytes.data(), input_bytes.size(), final_data,
                                 kDefaultThreshold, mans_mode);
    if (rc != 0) {
        return rc;
    }

    if (!save_u8_file(output_file, final_data)) {
        std::cerr << "Failed to write output file: " << output_file << "\n";
        return 1;
    }

    int codec = final_data.empty() ? -1 : static_cast<int>(final_data[0]);
    std::cout << "mans compress finished! Write to " << output_file
              << " (codec=" << codec << ")\n";
    return 0;
}

int mans_decompress_file(const std::string& dtype,
                         const std::string& input_file,
                         const std::string& output_file,
                         const std::string& cpu_bin_dir) {
    (void)cpu_bin_dir;

    std::vector<std::uint8_t> all;
    if (!load_u8_file(input_file, all)) {
        std::cerr << "Failed to load input file: " << input_file << "\n";
        return 1;
    }

    std::vector<std::uint8_t> final_output;
    int rc = mans_decompress_bytes(dtype, all.data(), all.size(), final_output);
    if (rc != 0) {
        return rc;
    }

    if (!save_u8_file(output_file, final_output)) {
        std::cerr << "Failed to write output file: " << output_file << "\n";
        return 1;
    }

    std::cout << "mans decompress finished! Output: " << output_file << "\n";
    return 0;
}

int mans_compress_bytes(const std::string& dtype,
                        const std::uint8_t* input_data,
                        std::size_t input_size,
                        std::vector<std::uint8_t>& output_data,
                        std::uint32_t adm_threshold,
                        std::uint32_t mans_mode) {
    const bool is_u2 = (dtype == "-u2" || dtype == "u2");
    const bool is_u4 = (dtype == "-u4" || dtype == "u4");
    if (!is_u2 && !is_u4) {
        std::cerr << "Unknown data type flag: " << dtype
                  << "\nUse: u2 or u4 (or -u2/-u4)\n";
        return 1;
    }
    if (input_size > 0 && input_data == nullptr) {
        std::cerr << "Input data pointer is null.\n";
        return 1;
    }
    if (input_size == 0) {
        std::cerr << "Input buffer is empty.\n";
        return 1;
    }

    std::vector<std::uint8_t> input_bytes(input_data, input_data + input_size);
    bool use_adm = is_u2 ? should_use_adm_t<std::uint16_t>(input_bytes, adm_threshold)
                         : should_use_adm_t<std::uint32_t>(input_bytes, adm_threshold);

    std::vector<std::uint8_t> codec_input;
    if (use_adm) {
        bool ok = is_u2 ? adm_compress_bytes_t<std::uint16_t>(input_bytes, codec_input)
                        : adm_compress_bytes_t<std::uint32_t>(input_bytes, codec_input);
        if (!ok) {
            std::cerr << "ADM compress failed.\n";
            return 1;
        }
    } else {
        codec_input = std::move(input_bytes);
    }

    const std::uint32_t mode = normalize_mode(mans_mode);
    const bool use_fse = (mode == kModeR);

    std::vector<std::uint8_t> stage2_output;
    if (use_fse) {
        if (!fse_compress_bytes(codec_input, stage2_output)) {
            std::cerr << "FSE compress failed.\n";
            return 1;
        }
    } else {
        if (!pans_compress_bytes(codec_input, stage2_output)) {
            std::cerr << "PANS compress failed.\n";
            return 1;
        }
    }

    output_data.clear();
    output_data.reserve(1 + stage2_output.size());
    const std::uint8_t codec = use_adm
        ? (use_fse ? kCodecAdmFse : kCodecAdmPans)
        : (use_fse ? kCodecRawFse : kCodecRawPans);
    output_data.push_back(codec);
    output_data.insert(output_data.end(), stage2_output.begin(), stage2_output.end());
    return 0;
}

int mans_decompress_bytes(const std::string& dtype,
                          const std::uint8_t* input_data,
                          std::size_t input_size,
                          std::vector<std::uint8_t>& output_data) {
    const bool is_u2 = (dtype == "-u2" || dtype == "u2");
    const bool is_u4 = (dtype == "-u4" || dtype == "u4");
    if (!is_u2 && !is_u4) {
        std::cerr << "Unknown data type flag: " << dtype
                  << "\nUse: u2 or u4 (or -u2/-u4)\n";
        return 1;
    }
    if (input_size > 0 && input_data == nullptr) {
        std::cerr << "Input data pointer is null.\n";
        return 1;
    }
    if (input_size < sizeof(MansHeader)) {
        std::cerr << "File too small, invalid mans format.\n";
        return 1;
    }

    MansHeader mh{};
    mh.codec = input_data[0];

    bool use_adm = false;
    bool use_fse = false;
    if (mh.codec == kCodecAdmPans) {
        use_adm = true;
        use_fse = false;
    } else if (mh.codec == kCodecRawPans) {
        use_adm = false;
        use_fse = false;
    } else if (mh.codec == kCodecAdmFse) {
        use_adm = true;
        use_fse = true;
    } else if (mh.codec == kCodecRawFse) {
        use_adm = false;
        use_fse = true;
    } else {
        std::cerr << "Unknown codec type in mans header: " << int(mh.codec) << "\n";
        return 1;
    }

    std::vector<std::uint8_t> stage2_payload(input_data + 1, input_data + input_size);
    std::vector<std::uint8_t> stage2_decoded;
    if (use_fse) {
        if (!fse_decompress_bytes(stage2_payload, stage2_decoded)) {
            std::cerr << "FSE decompress failed.\n";
            return 1;
        }
    } else {
        if (!pans_decompress_bytes(stage2_payload, stage2_decoded)) {
            std::cerr << "PANS decompress failed.\n";
            return 1;
        }
    }

    output_data.clear();
    if (use_adm) {
        bool ok = is_u2 ? adm_decompress_bytes_t<std::uint16_t>(stage2_decoded, output_data)
                        : adm_decompress_bytes_t<std::uint32_t>(stage2_decoded, output_data);
        if (!ok) {
            std::cerr << "ADM decompress failed.\n";
            return 1;
        }
    } else {
        output_data = std::move(stage2_decoded);
    }

    return 0;
}

} // namespace cpu
} // namespace mans
