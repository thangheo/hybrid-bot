# Save as order_imbalance_system/docker/data_ingestion.Dockerfile
FROM debian:bullseye-slim

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libzmq3-dev \
    pkg-config \
    && git clone https://github.com/zeromq/cppzmq.git /tmp/cppzmq \
    && cd /tmp/cppzmq \
    && mkdir build && cd build \
    && cmake .. \
    && make -j$(nproc) \
    && make install \
    && rm -rf /tmp/cppzmq \
    && rm -rf /var/lib/apt/lists/*

# Create app directory
WORKDIR /app

# Copy only necessary files
COPY include/ /app/include/
COPY src/ /app/src/
COPY CMakeLists.txt /app/

# Create build script
RUN echo '#!/bin/bash\n\
set -e\n\
rm -rf /app/build\n\
mkdir -p /app/build\n\
cd /app/build\n\
cmake ..\n\
make -j$(nproc)\n\
echo "Build completed successfully"' > /app/build.sh && chmod +x /app/build.sh

# Build the application
RUN /app/build.sh

# Run the data ingestion service
CMD ["/app/build/bin/data_ingestion"]