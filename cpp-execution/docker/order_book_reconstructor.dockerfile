# docker/order_book_reconstructor.dockerfile
FROM debian:bullseye-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libzmq3-dev \
    python3 \
    python3-pip \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Python dependencies
RUN pip3 install --no-cache-dir \
    numpy \
    pandas \
    matplotlib \
    pyzmq

# Create working directory
WORKDIR /app

# Copy source code
COPY . /app/

# Build C++ code
RUN mkdir -p build && \
    cd build && \
    cmake .. && \
    make -j$(nproc)

# Expose ZeroMQ port
EXPOSE 5555

# Default command
CMD ["/bin/bash"]