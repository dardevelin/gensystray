FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    gcc \
    git \
    make \
    pkg-config \
    libgtk-3-dev \
    valgrind \
    xvfb \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY .. .

RUN git submodule update --init --recursive && make clean && make DEBUG=1
