FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    make \
    cmake \
    wget \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

ENV CROSS_PREFIX=aarch64-linux-gnu-

# ---- Build FFTW3 (double-precision) for ARM64 ----
# libkeyfinder uses double-precision FFTW (fftw3, not fftw3f)
RUN wget -q http://www.fftw.org/fftw-3.3.10.tar.gz \
    && tar xzf fftw-3.3.10.tar.gz \
    && cd fftw-3.3.10 \
    && ./configure \
        --host=aarch64-linux-gnu \
        --prefix=/opt/arm64 \
        --enable-static \
        --disable-shared \
        --disable-fortran \
        --with-pic \
    && make -j$(nproc) \
    && make install \
    && cd .. && rm -rf fftw-3.3.10 fftw-3.3.10.tar.gz

# ---- Build libkeyfinder for ARM64 ----
ENV PKG_CONFIG_PATH=/opt/arm64/lib/pkgconfig
RUN wget https://github.com/mixxxdj/libkeyfinder/archive/refs/tags/2.2.8.tar.gz -O libkeyfinder.tar.gz \
    && tar xzf libkeyfinder.tar.gz \
    && cd libkeyfinder-2.2.8 \
    && mkdir build && cd build \
    && cmake .. \
        -DCMAKE_SYSTEM_NAME=Linux \
        -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
        -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
        -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
        -DCMAKE_INSTALL_PREFIX=/opt/arm64 \
        -DCMAKE_PREFIX_PATH=/opt/arm64 \
        -DCMAKE_FIND_ROOT_PATH=/opt/arm64 \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=BOTH \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
        -DBUILD_SHARED_LIBS=OFF \
        -DBUILD_TESTING=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    && make -j$(nproc) \
    && make install \
    && cd ../.. && rm -rf libkeyfinder-2.2.8 libkeyfinder.tar.gz

WORKDIR /build
