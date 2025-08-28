# Dockerfile for Order Book Normalization
FROM debian:bullseye-slim as builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    libboost-all-dev \
    libzmq3-dev \
    libtbb-dev \
    nlohmann-json3-dev \
    libssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone and build folly (Facebook's C++ library)
RUN git clone https://github.com/facebook/folly.git && \
    cd folly && \
    cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF . && \
    make -j$(nproc) && \
    make install

# Clone and build prometheus-cpp
RUN git clone https://github.com/jupp0r/prometheus-cpp.git && \
    cd prometheus-cpp && \
    git submodule init && \
    git submodule update && \
    mkdir build && \
    cd build && \
    cmake .. -DBUILD_SHARED_LIBS=ON && \
    make -j$(nproc) && \
    make install

# Copy source code
WORKDIR /app
COPY . .

# Build the application
RUN mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc)

# Create a smaller runtime image
FROM debian:bullseye-slim

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libzmq5 \
    libtbb2 \
    libssl1.1 \
    && rm -rf /var/lib/apt/lists/*

# Copy the built binary and libraries from the builder stage
COPY --from=builder /app/build/order_book_normalizer /usr/local/bin/
COPY --from=builder /usr/local/lib/libfolly.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprometheus-cpp-*.so* /usr/local/lib/

# Update shared library cache
RUN ldconfig

# Create config directory
WORKDIR /app
COPY config /app/config

# Set environment variables
ENV CONFIG_PATH=/app/config/data_ingestion/normalization.yaml
ENV LD_LIBRARY_PATH=/usr/local/lib

# Expose ports
EXPOSE 5555
EXPOSE 8080
# Dockerfile for Order Book Normalization (continued)

# Expose metrics port
EXPOSE 8081

# Set up health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
  CMD curl -f http://localhost:8080/health || exit 1

# Command to run the application
ENTRYPOINT ["/usr/local/bin/order_book_normalizer"]
CMD ["${CONFIG_PATH}"]