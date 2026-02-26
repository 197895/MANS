#include <H5PLextern.h>
#include <hdf5.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "H5Z-MANS_config.h"
#include "cpu/mans_file_codec.h"

namespace {

constexpr unsigned int H5Z_FILTER_MANS_ORIGINAL_ID = 32003;
constexpr std::size_t kMansParamsWords = sizeof(mans::MansParams) / sizeof(unsigned int);
constexpr std::size_t kLegacyMansParamsWords = kMansParamsWords - 1;

using mans::h5::safe_malloc;

bool parse_mans_params_from_cd(const unsigned int* cd_values,
                               std::size_t cd_nelmts,
                               mans::MansParams& out) {
    if (!cd_values || cd_nelmts < kLegacyMansParamsWords) {
        return false;
    }

    std::memset(&out, 0, sizeof(out));
    const std::size_t words_to_copy = std::min(cd_nelmts, kMansParamsWords);
    std::memcpy(&out, cd_values, words_to_copy * sizeof(unsigned int));
    if (cd_nelmts < kMansParamsWords) {
        out.mode = mans::Mode::R;
    }
    return true;
}

bool run_mans_codec(bool reverse,
                    const mans::MansParams& params,
                    const void* in_data,
                    std::size_t in_size,
                    std::vector<std::uint8_t>& out_data) {
    const char* dtype = nullptr;
    if (params.dtype == mans::DataType::U16) {
        dtype = "u2";
    } else if (params.dtype == mans::DataType::U32) {
        dtype = "u4";
    } else {
        std::cerr << "[H5Z-MANS-original Error] Unsupported dtype in MansParams: " << params.dtype << "\n";
        return false;
    }
    if (in_size > 0 && in_data == nullptr) {
        std::cerr << "[H5Z-MANS-original Error] Input pointer is null.\n";
        return false;
    }
    if (!reverse && params.mode != mans::Mode::P && params.mode != mans::Mode::R) {
        std::cerr << "[H5Z-MANS-original Error] Unsupported mode in MansParams: "
                  << params.mode << "\n";
        return false;
    }

    const auto* input = static_cast<const std::uint8_t*>(in_data);
    int rc = reverse
        ? mans::cpu::mans_decompress_bytes(dtype, input, in_size, out_data)
        : mans::cpu::mans_compress_bytes(dtype, input, in_size, out_data,
                                         params.adm_threshold, params.mode);
    if (rc != 0) {
        std::cerr << "[H5Z-MANS-original Error] Codec function call failed, ret=" << rc << "\n";
        return false;
    }
    return true;
}

} // namespace

static herr_t H5Z_set_local_mans(hid_t dcpl_id, hid_t type_id, hid_t space_id) {
    (void)type_id;

    const int ndims = H5Sget_simple_extent_ndims(space_id);
    if (ndims <= 0) {
        return 0;
    }
    if (H5Pget_layout(dcpl_id) != H5D_CHUNKED) {
        return 0;
    }

    std::size_t cd_nelmts = 0;
    unsigned int flags = 0;
    if (H5Pget_filter_by_id2(dcpl_id, H5Z_FILTER_MANS_ORIGINAL_ID, &flags,
                             &cd_nelmts, nullptr, 0, nullptr, nullptr) < 0) {
        return 0;
    }

    if (cd_nelmts < kLegacyMansParamsWords) {
        return 0;
    }

    std::vector<unsigned int> cd_values(cd_nelmts, 0);
    if (H5Pget_filter_by_id2(dcpl_id, H5Z_FILTER_MANS_ORIGINAL_ID, &flags,
                             &cd_nelmts, cd_values.data(), 0, nullptr, nullptr) < 0) {
        return 0;
    }
    mans::MansParams params{};
    if (!parse_mans_params_from_cd(cd_values.data(), cd_nelmts, params)) {
        return 0;
    }
    if (params.mode != mans::Mode::P && params.mode != mans::Mode::R) {
        params.mode = mans::Mode::R;
    }

    std::vector<hsize_t> chunk_dims(static_cast<std::size_t>(ndims), 0);
    if (H5Pget_chunk(dcpl_id, ndims, chunk_dims.data()) < 0) {
        return 0;
    }

    std::size_t chunk_elements = 1;
    for (int i = 0; i < ndims; ++i) {
        std::size_t dim = static_cast<std::size_t>(chunk_dims[static_cast<std::size_t>(i)]);
        if (dim == 0) {
            return 0;
        }
        chunk_elements *= dim;
    }
    if (chunk_elements > std::numeric_limits<unsigned int>::max()) {
        std::cerr << "[H5Z-MANS-original Error] chunk_elements exceeds unsigned int: " << chunk_elements << "\n";
        return 0;
    }

    const std::size_t desired_nelmts = std::max(cd_nelmts, kMansParamsWords + 1);
    std::vector<unsigned int> out_values(desired_nelmts, 0);
    std::memcpy(out_values.data(), cd_values.data(),
                std::min(cd_nelmts, desired_nelmts) * sizeof(unsigned int));
    std::memcpy(out_values.data(), &params, sizeof(params));
    out_values[kMansParamsWords] = static_cast<unsigned int>(chunk_elements);

    if (H5Pmodify_filter(dcpl_id, H5Z_FILTER_MANS_ORIGINAL_ID, flags,
                         desired_nelmts, out_values.data()) < 0) {
        return 0;
    }
    return 0;
}

static htri_t H5Z_can_apply_mans(hid_t dcpl_id, hid_t type_id, hid_t space_id) {
    (void)dcpl_id;
    (void)space_id;

    if (H5Tget_class(type_id) != H5T_INTEGER) {
        std::cerr << "[H5Z-MANS-original Warning] Datatype is not INTEGER.\n";
        return 0;
    }
    if (H5Tget_sign(type_id) != H5T_SGN_NONE) {
        std::cerr << "[H5Z-MANS-original Warning] Datatype must be Unsigned (UINT).\n";
        return 0;
    }
    std::size_t size = H5Tget_size(type_id);
    if (size != 2 && size != 4) {
        std::cerr << "[H5Z-MANS-original Warning] Only U16/U32 supported. Current element size: " << size << "\n";
        return 0;
    }
    return 1;
}

static std::size_t H5Z_filter_mans(unsigned int flags,
                                   std::size_t cd_nelmts,
                                   const unsigned int cd_values[],
                                   std::size_t nbytes,
                                   std::size_t* buf_size,
                                   void** buf) {
    if (cd_nelmts < kLegacyMansParamsWords || !cd_values || !buf || !buf_size) {
        std::cerr << "[H5Z-MANS-original Error] Invalid filter arguments.\n";
        return 0;
    }

    mans::MansParams params{};
    if (!parse_mans_params_from_cd(cd_values, cd_nelmts, params)) {
        std::cerr << "[H5Z-MANS-original Error] Failed to parse MansParams.\n";
        return 0;
    }

    std::size_t elem_size = 0;
    if (params.dtype == mans::DataType::U16) {
        elem_size = sizeof(std::uint16_t);
    } else if (params.dtype == mans::DataType::U32) {
        elem_size = sizeof(std::uint32_t);
    } else {
        std::cerr << "[H5Z-MANS-original Error] Unsupported dtype in filter params: " << params.dtype << "\n";
        return 0;
    }

    if (!(flags & H5Z_FLAG_REVERSE) && (nbytes % elem_size != 0)) {
        std::cerr << "[H5Z-MANS-original Error] nbytes is not a multiple of element size.\n";
        return 0;
    }

    std::vector<std::uint8_t> output;
    const bool reverse = ((flags & H5Z_FLAG_REVERSE) != 0);
    if (!run_mans_codec(reverse, params, *buf, nbytes, output)) {
        return 0;
    }

    std::size_t expected_elements = 0;
    if (reverse) {
        if (cd_nelmts > kMansParamsWords) {
            expected_elements = static_cast<std::size_t>(cd_values[kMansParamsWords]);
        } else if (cd_nelmts > kLegacyMansParamsWords) {
            expected_elements = static_cast<std::size_t>(cd_values[kLegacyMansParamsWords]);
        }
    }
    if (expected_elements > 0) {
        std::size_t expected_bytes = expected_elements * elem_size;
        if (output.size() != expected_bytes) {
            std::cerr << "[H5Z-MANS-original Error] Decompressed bytes mismatch. expected="
                      << expected_bytes << ", got=" << output.size() << "\n";
            return 0;
        }
    }

    if (output.empty() && nbytes > 0) {
        std::cerr << "[H5Z-MANS-original Error] Output is empty.\n";
        return 0;
    }

    void* dst_buf = safe_malloc(output.size());
    if (!dst_buf) {
        return 0;
    }
    if (!output.empty()) {
        std::memcpy(dst_buf, output.data(), output.size());
    }

    std::free(*buf);
    *buf = dst_buf;
    *buf_size = output.size();
    return output.size();
}

extern "C" {

const H5Z_class2_t H5Z_MANS_ORIGINAL_CLASS[1] = {{
    H5Z_CLASS_T_VERS,
    H5Z_FILTER_MANS_ORIGINAL_ID,
    1,
    1,
    "H5Z-MANS-original",
    H5Z_can_apply_mans,
    H5Z_set_local_mans,
    H5Z_filter_mans,
}};

H5PL_type_t H5PLget_plugin_type(void) {
    return H5PL_TYPE_FILTER;
}

const void* H5PLget_plugin_info(void) {
    return H5Z_MANS_ORIGINAL_CLASS;
}

} // extern "C"
