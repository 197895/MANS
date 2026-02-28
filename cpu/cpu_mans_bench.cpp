#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

#include "../mans_timing.h"
#include "mans_file_codec.h"

namespace {

struct BenchStats {
    double comp_ms = 0.0;
    double decomp_ms = 0.0;
    double comp_should_use_adm_ms = 0.0;
    double comp_adm_core_ms = 0.0;
    double comp_entropy_core_ms = 0.0;
    double decomp_entropy_core_ms = 0.0;
    double decomp_adm_core_ms = 0.0;
    std::size_t comp_bytes = 0;
    bool ok = true;
    std::string error;
};

double last_run_sum_ms(std::initializer_list<const char*> labels) {
#ifdef ENABLE_TIMING
    return mans::TimingCollector::instance().last_run_sum_ms(labels);
#else
    (void)labels;
    return 0.0;
#endif
}

bool load_u8_file(const std::string& path, std::vector<std::uint8_t>& data) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) {
        return false;
    }
    const std::streamsize size = in.tellg();
    if (size < 0) {
        return false;
    }
    in.seekg(0, std::ios::beg);
    data.resize(static_cast<std::size_t>(size));
    if (size == 0) {
        return true;
    }
    return static_cast<bool>(in.read(reinterpret_cast<char*>(data.data()), size));
}

BenchStats run_once(const std::string& dtype,
                    const std::vector<std::uint8_t>& input,
                    std::uint32_t threshold,
                    std::uint32_t mode) {
    BenchStats stats{};

    std::vector<std::uint8_t> compressed;
    std::vector<std::uint8_t> recovered;
    {
        MANS_TIMING_RUN_SCOPE();

        const int comp_rc = mans::cpu::mans_compress_bytes(dtype,
                                                           input.data(),
                                                           input.size(),
                                                           compressed,
                                                           threshold,
                                                           mode);
        if (comp_rc != 0) {
            stats.ok = false;
            stats.error = "Compression failed";
            return stats;
        }

        const int decomp_rc = mans::cpu::mans_decompress_bytes(dtype,
                                                               compressed.data(),
                                                               compressed.size(),
                                                               recovered);
        if (decomp_rc != 0) {
            stats.ok = false;
            stats.error = "Decompression failed";
            return stats;
        }
    }

    stats.comp_should_use_adm_ms = last_run_sum_ms({"mans/should_use_adm"});
    stats.comp_adm_core_ms = last_run_sum_ms({"mans/adm_encode_core"});
    stats.comp_entropy_core_ms = last_run_sum_ms({"mans/entropy_encode_core"});
    stats.decomp_entropy_core_ms = last_run_sum_ms({"mans/entropy_decode_core"});
    stats.decomp_adm_core_ms = last_run_sum_ms({"mans/adm_decode_core"});
    stats.comp_ms = stats.comp_should_use_adm_ms +
                    stats.comp_adm_core_ms +
                    stats.comp_entropy_core_ms;
    stats.decomp_ms = stats.decomp_entropy_core_ms +
                      stats.decomp_adm_core_ms;

    if (recovered.size() != input.size() ||
        std::memcmp(recovered.data(), input.data(), input.size()) != 0) {
        stats.ok = false;
        stats.error = "Decompression mismatch";
        return stats;
    }

    stats.comp_bytes = compressed.size();
    return stats;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <-u2|-u4> <input.bin> [--mode p|r] [--threshold 4000]"
                  << " [--csv out.csv]\n";
        return 1;
    }

    const std::string input_type = argv[1];
    const bool is_u2 = (input_type == "-u2" || input_type == "u2");
    const bool is_u4 = (input_type == "-u4" || input_type == "u4");
    if (!is_u2 && !is_u4) {
        std::cerr << "Unknown data type flag: " << input_type << "\nUse: -u2 or -u4\n";
        return 1;
    }

    const std::string input_path = argv[2];
    std::string csv_path;
    std::uint32_t mode = 1; // 0=p, 1=r
    std::uint32_t threshold = 4000;

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) {
            const std::string mode_arg = argv[++i];
            if (mode_arg == "p" || mode_arg == "P") {
                mode = 0;
            } else if (mode_arg == "r" || mode_arg == "R") {
                mode = 1;
            } else {
                std::cerr << "Unknown mode: " << mode_arg << " (use p or r)\n";
                return 1;
            }
        } else if (arg == "--threshold" && i + 1 < argc) {
            threshold = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            return 1;
        }
    }

    std::vector<std::uint8_t> input;
    if (!load_u8_file(input_path, input)) {
        std::cerr << "Failed to load input file: " << input_path << "\n";
        return 1;
    }
    if (input.empty()) {
        std::cerr << "Input file is empty.\n";
        return 1;
    }

    constexpr int kIters = 11;
    double total_comp_ms = 0.0;
    double total_decomp_ms = 0.0;
    double total_comp_bytes = 0.0;
    MANS_TIMING_RESET();

    for (int iter = 0; iter < kIters; ++iter) {
        const BenchStats stats = run_once(input_type, input, threshold, mode);
        if (!stats.ok) {
            std::cerr << stats.error << "\n";
            return 1;
        }

        if (iter == 0) {
            continue; // warm-up
        }
        total_comp_ms += stats.comp_ms;
        total_decomp_ms += stats.decomp_ms;
        total_comp_bytes += static_cast<double>(stats.comp_bytes);
    }

    const double denom = static_cast<double>(kIters - 1);
    const double avg_comp_ms = total_comp_ms / denom;
    const double avg_decomp_ms = total_decomp_ms / denom;
    const double avg_comp_bytes = total_comp_bytes / denom;
    const double ratio_pct = 100.0 * avg_comp_bytes / static_cast<double>(input.size());
    const double comp_mbps = (static_cast<double>(input.size()) / 1e6) / (avg_comp_ms / 1e3);
    const double decomp_mbps =
        (static_cast<double>(input.size()) / 1e6) / (avg_decomp_ms / 1e3);

    std::cout << "Command-line arguments:\n";
    std::cout << "  Input type: " << input_type << "\n";
    std::cout << "  Input file: " << input_path << "\n";
    std::cout << "  Mode: " << (mode == 1 ? "r" : "p") << "\n";
    std::cout << "  Threshold: " << threshold << "\n";
    if (!csv_path.empty()) {
        std::cout << "  CSV: " << csv_path << "\n";
    }
    std::cout << "\n";

    std::cout << std::left << std::setw(8) << "Chunk"
              << " | " << std::setw(9) << "Ratio"
              << " | " << std::setw(13) << "Comp MB/s"
              << " | " << std::setw(13) << "Decomp MB/s"
              << "\n";
    std::cout << std::string(52, '-') << "\n";
    std::cout << std::left << std::setw(8) << "full"
              << " | " << std::setw(8) << std::fixed << std::setprecision(2)
              << ratio_pct << "%"
              << " | " << std::setw(13) << std::fixed << std::setprecision(1)
              << comp_mbps
              << " | " << std::setw(13) << std::fixed << std::setprecision(1)
              << decomp_mbps
              << "\n";

    if (!csv_path.empty()) {
        std::ofstream csv(csv_path);
        if (!csv) {
            std::cerr << "Failed to open CSV output: " << csv_path << "\n";
            return 1;
        }
        csv << "chunk_label,chunk_bytes,ratio_pct,comp_mbps,decomp_mbps\n";
        csv << "full,"
            << input.size() << ","
            << std::fixed << std::setprecision(2) << ratio_pct << ","
            << std::fixed << std::setprecision(1) << comp_mbps << ","
            << std::fixed << std::setprecision(1) << decomp_mbps << "\n";
    }

    return 0;
}
