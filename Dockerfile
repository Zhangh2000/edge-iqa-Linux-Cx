FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get -o Acquire::Retries=5 update \
    && apt-get -o Acquire::Retries=5 install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libopencv-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["bash"]
