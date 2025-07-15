cmake_minimum_required(VERSION 3.15)
project(MANS_ROOT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------
# 平台控制选项
# -------------------------------
set(VALID_PLATFORMS "cpu" "nv" "amd" "all" "cpu_nv" "cpu_amd")
set(TARGET_PLATFORM "cpu" CACHE STRING "Target platform to build: cpu / nv / amd / all / cpu_nv / cpu_amd")
if (NOT TARGET_PLATFORM IN_LIST VALID_PLATFORMS)
  message(FATAL_ERROR "Invalid TARGET_PLATFORM='${TARGET_PLATFORM}'. Must be one of: ${VALID_PLATFORMS}.")
endif()

# -------------------------------
# 平台布尔标志
# -------------------------------
set(BUILD_CPU FALSE)
set(BUILD_NV FALSE)
set(BUILD_AMD FALSE)

if(TARGET_PLATFORM STREQUAL "cpu" OR TARGET_PLATFORM STREQUAL "cpu_nv" OR TARGET_PLATFORM STREQUAL "cpu_amd" OR TARGET_PLATFORM STREQUAL "all")
  set(BUILD_CPU TRUE)
endif()
if(TARGET_PLATFORM STREQUAL "nv" OR TARGET_PLATFORM STREQUAL "cpu_nv" OR TARGET_PLATFORM STREQUAL "all")
  set(BUILD_NV TRUE)
endif()
if(TARGET_PLATFORM STREQUAL "amd" OR TARGET_PLATFORM STREQUAL "cpu_amd" OR TARGET_PLATFORM STREQUAL "all")
  set(BUILD_AMD TRUE)
endif()

# -------------------------------
# OpenMP（用于 CPU）
# -------------------------------
if(BUILD_CPU)
  find_package(OpenMP REQUIRED)
endif()

# -------------------------------
# 编译 mans/cpu
# -------------------------------
if(BUILD_CPU)
  set(cpu_adm_binary_dir ${CMAKE_BINARY_DIR}/cpu/adm)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/cpu)

  add_executable(mappingcpu_uint32 cpu/adm/mappingcpu_uint32.cpp)
  set_target_properties(mappingcpu_uint32 PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
      ARCHIVE_OUTPUT_DIRECTORY ${cpu_adm_binary_dir}
      LIBRARY_OUTPUT_DIRECTORY ${cpu_adm_binary_dir}
  )
  target_compile_options(mappingcpu_uint32 PRIVATE -mavx512f -fopenmp -march=native -O3)
  target_link_libraries(mappingcpu_uint32 PRIVATE OpenMP::OpenMP_CXX)

  add_executable(mappingcpu_uint16 cpu/adm/mappingcpu_uint16.cpp)
  set_target_properties(mappingcpu_uint16 PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
      ARCHIVE_OUTPUT_DIRECTORY ${cpu_adm_binary_dir}
      LIBRARY_OUTPUT_DIRECTORY ${cpu_adm_binary_dir}
  )
  target_compile_options(mappingcpu_uint16 PRIVATE -mavx512f -fopenmp -march=native -O3)
  target_link_libraries(mappingcpu_uint16 PRIVATE OpenMP::OpenMP_CXX)

  # CPU/pans 子模块
  set(cpu_pans_binary_dir ${CMAKE_BINARY_DIR}/cpu/pans)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${cpu_pans_binary_dir})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${cpu_pans_binary_dir})
  add_subdirectory(cpu/pans)

endif()

# -------------------------------
# 编译 mans/nv
# -------------------------------
if(BUILD_NV)
  enable_language(CUDA)
  
  # 自动探测 GPU 架构
  include(FindCUDA/select_compute_arch)
  CUDA_DETECT_INSTALLED_GPUS(INSTALLED_GPU_CCS)
  string(STRIP "${INSTALLED_GPU_CCS}" INSTALLED_GPU_CCS_STRIPPED)
  string(REPLACE " " ";" INSTALLED_GPU_CCS_LIST "${INSTALLED_GPU_CCS_STRIPPED}")
  string(REPLACE "." "" DETECTED_CUDA_ARCH_LIST "${INSTALLED_GPU_CCS_LIST}")
  message(STATUS "Auto-detected CUDA architectures: ${DETECTED_CUDA_ARCH_LIST}")

  set(nv_adm_binary_dir ${CMAKE_BINARY_DIR}/nv/adm)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/nv)

  add_executable(nv_mapping_uint16 nv/adm/mapping_uint16.cu)
  set_target_properties(nv_mapping_uint16 PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
      ARCHIVE_OUTPUT_DIRECTORY ${nv_adm_binary_dir}
      LIBRARY_OUTPUT_DIRECTORY ${nv_adm_binary_dir}
      CUDA_SEPARABLE_COMPILATION ON
      CUDA_ARCHITECTURES "${DETECTED_CUDA_ARCH_LIST}"
  )
  target_compile_options(nv_mapping_uint16 PRIVATE -O3)

  add_executable(nv_mapping_uint32 nv/adm/mapping_uint32.cu)
  set_target_properties(nv_mapping_uint32 PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
      ARCHIVE_OUTPUT_DIRECTORY ${nv_adm_binary_dir}
      LIBRARY_OUTPUT_DIRECTORY ${nv_adm_binary_dir}
      CUDA_SEPARABLE_COMPILATION ON
      CUDA_ARCHITECTURES "${DETECTED_CUDA_ARCH_LIST}"
  )
  target_compile_options(nv_mapping_uint32 PRIVATE -O3)

  # nv/ans 子模块
  set(nv_ans_binary_dir ${CMAKE_BINARY_DIR}/nv/ans)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${nv_ans_binary_dir})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${nv_ans_binary_dir})
  add_subdirectory(nv/ans)

endif()

# -------------------------------
# 编译 mans/amd （使用 hipcc）
# -------------------------------
if(BUILD_AMD)

  find_program(HIPCC_EXECUTABLE NAMES hipcc REQUIRED)
  if(NOT HIPCC_EXECUTABLE)
    message(FATAL_ERROR "hipcc not found. Please ensure ROCm is installed and hipcc is in PATH.")
  endif()

  # ========== amd/adm ==========

  set(AMD_ADM_BIN_DIR ${CMAKE_BINARY_DIR}/bin/amd)

  foreach(fname IN ITEMS mapping_uint16 mapping_uint32)
    set(src_file ${CMAKE_SOURCE_DIR}/amd/adm/${fname}.cpp)
    set(target_name amd_${fname})
    set(out_file ${AMD_ADM_BIN_DIR}/${target_name})

    add_custom_command(
      OUTPUT ${out_file}
      COMMAND ${HIPCC_EXECUTABLE} ${src_file}
              -DHIP_ENABLE_WARP_SYNC_BUILTINS
              -Wno-unused-result -Wno-format -O3
              -o ${out_file}
      DEPENDS ${src_file}
      COMMENT "Building AMD ADM: ${fname}.cpp"
      VERBATIM
    )

    add_custom_target(amd_adm_${fname} ALL
      DEPENDS ${out_file}
    )
  endforeach()

  # ========== amd/ans 子项目 ==========

  set(amd_ans_binary_dir ${CMAKE_BINARY_DIR}/amd/ans)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/amd)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${amd_ans_binary_dir})
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${amd_ans_binary_dir})
  add_subdirectory(amd/ans)

endif()

