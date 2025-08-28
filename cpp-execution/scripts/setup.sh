#!/bin/bash
set -e

# Check for Docker
if ! command -v docker &> /dev/null; then
    echo "Docker is not installed. Please install Docker first."
    exit 1
fi

# Check for Docker Compose
if ! command -v docker-compose &> /dev/null; then
    echo "Docker Compose is not installed. Please install Docker Compose first."
    exit 1
fi

# Create necessary directories
directories=(
    "config/zeromq"
    "config/postgres/init"
    "config/influxdb"
    "config/prometheus"
    "config/grafana/provisioning/datasources"
    "config/grafana/provisioning/dashboards"
    "docker/zeromq"
    "include/common"
    "src/common"
    "python/common"
)

for dir in "${directories[@]}"; do
    mkdir -p "$dir"
    echo "Created directory: $dir"
done

# Start the infrastructure
echo "Starting infrastructure services..."
docker-compose up -d

# Wait for services to be fully up
echo "Waiting for services to start..."
sleep 10

# Install C++ dependencies
if command -v apt-get &> /dev/null; then
    echo "Installing C++ dependencies..."
    sudo apt-get update
    sudo apt-get install -y build-essential cmake libzmq3-dev libboost-all-dev
fi

# Install Python dependencies
echo "Installing Python dependencies..."
pip3 install pyzmq numpy pandas matplotlib influxdb-client psycopg2-binary

echo "Infrastructure setup complete!"
echo "Services running:"
docker-compose ps
