FROM nvidia/cuda:12.6.2-devel-ubuntu22.04

# 安装基本构建工具和依赖
RUN apt-get update && apt-get install -y \
    build-essential \
    libomp-dev \
    git \
    vim \
    g++ \
    cmake

# 验证工具版本（可选）
RUN gcc --version && g++ --version && cmake --version && git --version

# 设置工作目录
WORKDIR /workspace

CMD ["/bin/bash"]