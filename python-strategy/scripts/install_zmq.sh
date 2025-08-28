#!/bin/bash

echo "=== ZeroMQ Installation for Kali Linux ==="

# Update package lists
echo "Updating package lists..."
sudo apt update

# Check current ZMQ status
echo "Current ZMQ status:"
dpkg -l | grep zmq || echo "No ZMQ packages found"
pkg-config --modversion libzmq 2>/dev/null || echo "pkg-config can't find libzmq"

echo ""
echo "=== Method 1: Install from Kali repositories ==="

# Install all ZMQ related packages
echo "Installing ZMQ packages..."

# Core ZMQ library
sudo apt install -y libzmq3-dev libzmq5

# If libzmq5 is not available, try alternatives
if [ $? -ne 0 ]; then
    echo "libzmq5 not available, trying libzmq3..."
    sudo apt install -y libzmq3-dev libzmq3
fi

# Install dependencies that might be missing
echo "Installing ZMQ dependencies..."
sudo apt install -y \
    libbsd0 \
    libc6-dev \
    libgcc-s1 \
    libsodium23 \
    libsodium-dev \
    libnorm1 \
    libnorm-dev \
    libpgm-5.3-0 \
    libpgm-dev \
    libgssapi-krb5-2 \
    libkrb5-dev

# Install build tools
sudo apt install -y \
    build-essential \
    cmake \
    pkg-config \
    git

echo ""
echo "=== Method 2: If repositories fail, build from source ==="

build_from_source() {
    echo "Building ZeroMQ from source..."
    
    cd /tmp
    
    # Install build dependencies
    sudo apt install -y \
        build-essential \
        cmake \
        git \
        libtool \
        autoconf \
        automake \
        uuid-dev \
        libsodium-dev
    
    # Download and build libzmq
    echo "Downloading ZeroMQ source..."
    git clone https://github.com/zeromq/libzmq.git
    cd libzmq
    
    # Use stable version
    git checkout v4.3.4
    
    echo "Configuring build..."
    ./autogen.sh
    ./configure --prefix=/usr/local \
                --enable-static \
                --enable-shared \
                --with-libsodium
    
    echo "Building (this may take a few minutes)..."
    make -j$(nproc)
    
    echo "Installing..."
    sudo make install
    sudo ldconfig
    
    echo "ZeroMQ built and installed from source"
    cd ~
}

# Check if installation was successful
check_installation() {
    echo ""
    echo "=== Checking Installation ==="
    
    # Check pkg-config
    if pkg-config --exists libzmq; then
        echo "✅ pkg-config can find libzmq"
        echo "   Version: $(pkg-config --modversion libzmq)"
        echo "   Libs: $(pkg-config --libs libzmq)"
        echo "   Cflags: $(pkg-config --cflags libzmq)"
    else
        echo "❌ pkg-config cannot find libzmq"
        return 1
    fi
    
    # Check headers
    if [ -f "/usr/include/zmq.h" ] || [ -f "/usr/local/include/zmq.h" ]; then
        echo "✅ ZMQ headers found"
    else
        echo "❌ ZMQ headers not found"
        return 1
    fi
    
    # Check library files
    if ldconfig -p | grep -q libzmq; then
        echo "✅ ZMQ library in system library path"
        ldconfig -p | grep libzmq
    else
        echo "❌ ZMQ library not found in system path"
        return 1
    fi
    
    return 0
}

# Run installation
if ! check_installation; then
    echo "Standard installation may have issues, trying build from source..."
    
    read -p "Build ZeroMQ from source? (y/n): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        build_from_source
        check_installation
    fi
fi

echo ""
echo "=== Testing C++ Compilation ==="

# Create test C++ file
cat > /tmp/zmq_test.cpp << 'EOF'
#include <zmq.h>
#include <iostream>
#include <string>

int main() {
    void* context = zmq_ctx_new();
    if (!context) {
        std::cout << "❌ Failed to create ZMQ context" << std::endl;
        return 1;
    }
    
    void* socket = zmq_socket(context, ZMQ_REQ);
    if (!socket) {
        std::cout << "❌ Failed to create ZMQ socket" << std::endl;
        zmq_ctx_destroy(context);
        return 1;
    }
    
    std::cout << "✅ ZMQ C++ compilation successful!" << std::endl;
    std::cout << "✅ ZMQ context and socket created successfully" << std::endl;
    
    // Get ZMQ version
    int major, minor, patch;
    zmq_version(&major, &minor, &patch);
    std::cout << "✅ ZMQ Version: " << major << "." << minor << "." << patch << std::endl;
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    return 0;
}
EOF

echo "Compiling test program..."
if g++ -std=c++11 -o /tmp/zmq_test /tmp/zmq_test.cpp -lzmq; then
    echo "✅ Compilation successful!"
    echo "Running test..."
    /tmp/zmq_test
else
    echo "❌ Compilation failed!"
    echo "Trying with pkg-config flags..."
    if g++ -std=c++11 $(pkg-config --cflags libzmq) -o /tmp/zmq_test /tmp/zmq_test.cpp $(pkg-config --libs libzmq); then
        echo "✅ Compilation with pkg-config successful!"
        /tmp/zmq_test
    else
        echo "❌ Still failed. Manual debugging needed."
        echo ""
        echo "Debug info:"
        echo "GCC version: $(gcc --version | head -1)"
        echo "Available ZMQ files:"
        find /usr -name "*zmq*" 2>/dev/null | head -10
        find /usr/local -name "*zmq*" 2>/dev/null | head -10
    fi
fi

echo ""
echo "=== Installing Python ZMQ ==="

# Install Python ZMQ
if command -v python3 &> /dev/null; then
    echo "Installing PyZMQ..."
    
    # Try pip first
    if command -v pip3 &> /dev/null; then
        pip3 install pyzmq
    else
        # Install pip if not available
        sudo apt install -y python3-pip
        pip3 install pyzmq
    fi
    
    # Test Python ZMQ
    echo "Testing Python ZMQ..."
    python3 -c "
import zmq
print(f'✅ PyZMQ version: {zmq.pyzmq_version()}')
print(f'✅ ZMQ version: {zmq.zmq_version()}')
context = zmq.Context()
socket = context.socket(zmq.REQ)
print('✅ Python ZMQ working correctly!')
socket.close()
context.term()
" 2>/dev/null || echo "❌ Python ZMQ test failed"

else
    echo "Python3 not found, skipping Python ZMQ installation"
fi

echo ""
echo "=== Installation Summary ==="
echo "If you see ✅ marks above, ZMQ should be working."
echo "If you see ❌ marks, there might be issues that need manual fixing."
echo ""
echo "Common fixes for remaining issues:"
echo "1. sudo ldconfig"
echo "2. export LD_LIBRARY_PATH=/usr/local/lib:\$LD_LIBRARY_PATH"
echo "3. export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:\$PKG_CONFIG_PATH"

# Clean up
rm -f /tmp/zmq_test.cpp /tmp/zmq_test