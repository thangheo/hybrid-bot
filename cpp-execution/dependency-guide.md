# Debian/Ubuntu Dependency Installation Guide

This guide provides detailed instructions for installing all dependencies required by the Order Imbalance Trading System on Debian-based systems.

## Core Dependencies

Install the basic build tools and core libraries:

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config
```

## ZeroMQ and cppzmq

Install ZeroMQ library and development files:

```bash
sudo apt install -y libzmq3-dev
```

For cppzmq, it's a header-only library. Our project already includes it in `include/external/cppzmq`, but if you want to install it system-wide:

```bash
sudo apt install -y libcppzmq-dev
```
```

sudo apt update
sudo apt install libjsoncpp-dev

```
```
sudo apt update
sudo apt install libeigen3-dev

```
## Boost Libraries

Install Boost development libraries:

```bash
sudo apt install -y libboost-all-dev
```

Or if you want to install only the required components:

```bash
sudo apt install -y libboost-system-dev libboost-program-options-dev libboost-filesystem-dev libboost-thread-dev
```

## Protocol Buffers and gRPC

Install Protocol Buffers:

```bash
sudo apt install -y libprotobuf-dev protobuf-compiler
```

Install gRPC:

```bash
sudo apt install -y libgrpc++-dev protobuf-compiler-grpc
```

## Google Test

Install Google Test for C++ unit testing:

```bash
sudo apt install -y libgtest-dev
```

In some versions of Debian/Ubuntu, you might need to build it from source:

```bash
cd /usr/src/gtest
sudo cmake .
sudo make
sudo cp lib/*.a /usr/lib/
```

Or on newer versions:

```bash
sudo apt install -y googletest
```

## Python Dependencies

Install Python and development tools:

```bash
sudo apt install -y python3 python3-dev python3-pip
```

Install pybind11 for C++/Python bindings:

```bash
sudo apt install -y pybind11-dev
```

## Database dependencies

For InfluxDB client:

```bash
sudo apt install -y libcurl4-openssl-dev
```

## Troubleshooting Common Issues

### Missing CMake Find Modules

If you get errors about missing Find*.cmake files, ensure you've properly set your CMAKE_MODULE_PATH. Our project includes custom modules in `cmake/modules/`.

### ZeroMQ Not Found

If CMake cannot find ZeroMQ, you can specify the path manually:

```bash
cmake .. -DZeroMQ_ROOT=/usr/local
```

### Protocol Buffers or gRPC Issues

If you're having issues with Protocol Buffers or gRPC, you can check their installation:

```bash
# Check if protoc is installed
which protoc
protoc --version

# Check if grpc_cpp_plugin is installed
which grpc_cpp_plugin
```

You might need to specify their locations manually:

```bash
cmake .. -DPROTOBUF_PROTOC_EXECUTABLE=/path/to/protoc -DGRPC_CPP_PLUGIN_EXECUTABLE=/path/to/grpc_cpp_plugin
```

### Google Test Issues

If you're having issues with Google Test:

```bash
# See which gtest library files are installed
find /usr -name "libgtest*.a" -o -name "libgtest*.so"
```

You can specify the path manually if needed:

```bash
cmake .. -DGTEST_ROOT=/path/to/gtest
```

## Verifying Installation

You can verify that the dependencies are correctly installed using:

```bash
# ZeroMQ
pkg-config --modversion libzmq

# Boost
dpkg -s libboost-dev | grep Version

# Protobuf
protoc --version

# gRPC
pkg-config --modversion grpc++

# Python
python3 --version
pip3 --version
```
# Install prerequisites
sudo apt-get install -y build-essential autoconf libtool pkg-config cmake

# Clone the gRPC repo
git clone -b v1.50.0 https://github.com/grpc/grpc
cd grpc
git submodule update --init

# Build and install
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=/usr \
      ../..
make -j$(nproc)
sudo make install