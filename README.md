# MANS: Optimizing ANS Encoding for Multi-Byte Integer Data on NVIDIA GPUs

(C) 2025 by Institute of Computing Technology, Chinese Academy of Sciences. 
- Developer: Wenjing Huang, Jinwu Yang 
- Advisor: Dingwen Tao, Guangming Tan

## Requirements

- CMake ≥ 3.15  
- C++17 compiler
- OpenMP support (e.g., install `libomp-dev`, for CPU parallel compression)  
- Recommended platform: Linux (Ubuntu 22.04)  
- CUDA 12.6 (for NVIDIA GPU)
- ROCm (for AMD GPU)
- Git

## Building

Clone this repo using

```shell
git clone https://github.com/ewTomato/MANS.git
```

Do the standard CMake thing:

```shell
cd MANS; mkdir build; cd build;
cmake -DTARGET_PLATFORM=cpu_nv .. && make (this is for cpu and nvidia platform)
```

<!-- ## Instructions for Use
### For CPU
1. compress

2. decompress -->


```

## License

Multibyte-ANS is licensed with the MIT license, available in the LICENSE file at the top level.
