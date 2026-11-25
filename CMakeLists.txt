cmake_minimum_required(VERSION 3.16)

project(c01_aes_ctr C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
find_package(OpenMP REQUIRED COMPONENTS C)

add_library(c01_ctr
    src/crypto/ctr_openssl.c
)
target_include_directories(c01_ctr PUBLIC include)
target_link_libraries(c01_ctr PUBLIC OpenSSL::Crypto OpenMP::OpenMP_C)

add_executable(aesctr src/cli/aesctr.c)
target_link_libraries(aesctr PRIVATE c01_ctr)

add_executable(bench_cpu bench/bench_cpu.c)
target_link_libraries(bench_cpu PRIVATE c01_ctr)

option(C01_ENABLE_MPI "Build the MPI file-processing backend" OFF)
if(C01_ENABLE_MPI)
    find_package(MPI REQUIRED COMPONENTS C)
    add_executable(aesctr_mpi src/mpi/aesctr_mpi.c)
    target_link_libraries(aesctr_mpi PRIVATE c01_ctr MPI::MPI_C)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(test_ctr tests/test_ctr.c)
    target_link_libraries(test_ctr PRIVATE c01_ctr)
    add_test(NAME ctr_vectors COMMAND test_ctr)
endif()

option(C01_ENABLE_GPU "Backend GPU (OpenCL)" OFF)
if(C01_ENABLE_GPU)
    find_package(OpenCL REQUIRED)
    add_library(c01_gpu src/backends/gpu_opencl.c)
    target_include_directories(c01_gpu PUBLIC include)
    target_link_libraries(c01_gpu PUBLIC OpenCL::OpenCL)
    target_compile_definitions(c01_gpu PRIVATE
        C01_GPU_KERNEL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/src/backends/aes_ctr.cl")

    target_link_libraries(aesctr PRIVATE c01_gpu)
    target_compile_definitions(aesctr PRIVATE C01_HAVE_GPU)

    add_executable(bench_gpu bench/bench_gpu.c)
    target_link_libraries(bench_gpu PRIVATE c01_gpu c01_ctr)

    if(BUILD_TESTING)
        add_executable(test_gpu tests/test_gpu.c)
        target_link_libraries(test_gpu PRIVATE c01_gpu c01_ctr)
        add_test(NAME gpu_vectors COMMAND test_gpu)
    endif()
endif()
