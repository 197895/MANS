cmake_minimum_required(VERSION 3.15)

if(DEFINED HDF5_PREFER_PARALLEL)
    set(HDF5_PREFER_PARALLEL ${HDF5_PREFER_PARALLEL})
endif()

find_package(MPI COMPONENTS C CXX QUIET)
if(HDF5_PREFER_PARALLEL AND NOT MPI_C_FOUND AND NOT MPI_CXX_FOUND)
    message(WARNING "HDF5_PREFER_PARALLEL=ON but MPI C was not found. Falling back to serial HDF5.")
    set(HDF5_PREFER_PARALLEL OFF)
endif()

find_package(HDF5 REQUIRED COMPONENTS C HL)
if(HDF5_PREFER_PARALLEL AND NOT HDF5_IS_PARALLEL)
    message(WARNING "HDF5_PREFER_PARALLEL=ON but the found HDF5 is serial.")
endif()

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}
    ${HDF5_INCLUDE_DIRS}
)

# =========================================================
# Target 1: H5Z-MANS-original (Filter Plugin shared library)
# =========================================================
add_library(H5Z-MANS-original SHARED src/H5Z-MANS.cpp)
target_link_libraries(H5Z-MANS-original PRIVATE mans_file_codec ${HDF5_LIBRARIES})
set_target_properties(H5Z-MANS-original PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/plugins
    OUTPUT_NAME "H5Z-MANS-original"
    PREFIX "lib"
)

if(NOT TARGET MPI::MPI_CXX)
    find_package(MPI REQUIRED COMPONENTS CXX)
endif()

add_executable(mans-original-write test/mans-original-write.cpp)
target_link_libraries(mans-original-write PRIVATE ${HDF5_LIBRARIES} MPI::MPI_CXX)
set_target_properties(mans-original-write PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/h5z-mans
)

add_executable(mans-original-read test/mans-original-read.cpp)
target_link_libraries(mans-original-read PRIVATE ${HDF5_LIBRARIES} MPI::MPI_CXX)
set_target_properties(mans-original-read PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/h5z-mans
)
