#!/bin/bash

# Trading System Manager
# Usage: ./trading_manager.sh [start|stop|restart|status|logs]

# Configuration
CPP_BUILD_DIR="cpp-execution/build"
CPP_EXEC_PATH="cpp-execution/bin/tactical"
PYTHON_SCRIPT_PATH="python-strategy/main.py"
LOG_DIR="logs"
CPP_LOG_FILE="$LOG_DIR/cpp_bridge.log"
PYTHON_LOG_FILE="/tmp/python_strategy.log" 
BUILD_LOG_FILE="/tmp/cpp_build.log"
PID_DIR="pids"
CPP_PID_FILE="$PID_DIR/cpp_bridge.pid"
PYTHON_PID_FILE="$PID_DIR/python_strategy.pid"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Create directories if they don't exist
mkdir -p "$LOG_DIR" "$PID_DIR"

# Function to print colored output
print_status() {
    local color=$1
    local message=$2
    echo -e "${color}[$(date '+%Y-%m-%d %H:%M:%S')] ${message}${NC}"
}

#source environment
source venv/bin/activate

# Function to check if a process is running
is_running() {
    local pid_file=$1
    if [[ -f "$pid_file" ]]; then
        local pid=$(cat "$pid_file")
        if ps -p "$pid" > /dev/null 2>&1; then
            return 0  # Running
        else
            rm -f "$pid_file"  # Clean up stale PID file
            return 1  # Not running
        fi
    fi
    return 1  # PID file doesn't exist
}

# Function to build C++ project
build_cpp() {
    print_status "$BLUE" "Building C++ project..."
    
    # Check if build directory exists
    if [[ ! -d "$CPP_BUILD_DIR" ]]; then
        print_status "$RED" "ERROR: Build directory not found: $CPP_BUILD_DIR"
        return 1
    fi
    
    # Save current directory
    local current_dir=$(pwd)
    
    # Change to build directory and run make
    if cd "$CPP_BUILD_DIR"; then
        print_status "$BLUE" "Changed to build directory: $(pwd)"
        
        # Run make and capture output
        if make > "$BUILD_LOG_FILE" 2>&1; then
            print_status "$GREEN" "C++ build completed successfully"
            # Return to original directory
            cd "$current_dir"
            return 0
        else
            print_status "$RED" "ERROR: C++ build failed. Check logs: $BUILD_LOG_FILE"
            # Return to original directory even on failure
            cd "$current_dir"
            return 1
        fi
    else
        print_status "$RED" "ERROR: Failed to change to build directory: $CPP_BUILD_DIR"
        return 1
    fi
}

# Function to start C++ bridge
start_cpp() {
    if is_running "$CPP_PID_FILE"; then
        print_status "$YELLOW" "C++ bridge is already running (PID: $(cat $CPP_PID_FILE))"
        return 1
    fi

    # Build C++ project first
    if ! build_cpp; then
        print_status "$RED" "ERROR: Failed to build C++ project. Cannot start bridge."
        return 1
    fi

    if [[ ! -x "$CPP_EXEC_PATH" ]]; then
        print_status "$RED" "ERROR: C++ executable not found or not executable: $CPP_EXEC_PATH"
        return 1
    fi

    print_status "$BLUE" "Starting C++ bridge..."
    
    # Start C++ process with nohup
    nohup "$CPP_EXEC_PATH" > "$CPP_LOG_FILE" 2>&1 &
    local cpp_pid=$!
    
    # Save PID
    echo "$cpp_pid" > "$CPP_PID_FILE"
    
    # Wait a moment and check if it's still running
    sleep 2
    if is_running "$CPP_PID_FILE"; then
        print_status "$GREEN" "C++ bridge started successfully (PID: $cpp_pid)"
        return 0
    else
        print_status "$RED" "ERROR: C++ bridge failed to start. Check logs: $CPP_LOG_FILE"
        return 1
    fi
}

# Function to start Python strategy
start_python() {
    if is_running "$PYTHON_PID_FILE"; then
        print_status "$YELLOW" "Python strategy is already running (PID: $(cat $PYTHON_PID_FILE))"
        return 1
    fi
    
    if [[ ! -f "$PYTHON_SCRIPT_PATH" ]]; then
        print_status "$RED" "ERROR: Python script not found: $PYTHON_SCRIPT_PATH"
        return 1
    fi
    
    # Check if config file exists
    local config_path="python-strategy/config/trading_bot.yaml"
    if [[ ! -f "$config_path" ]]; then
        print_status "$RED" "ERROR: Config file not found: $config_path"
        return 1
    fi
    
    print_status "$BLUE" "Starting Python strategy with config: $config_path"
    
    # Start Python process with nohup and config parameter
    cd "$(dirname "$PYTHON_SCRIPT_PATH")" || exit 1
    nohup python3 "$(basename "$PYTHON_SCRIPT_PATH")" --config "../$config_path" > "$PYTHON_LOG_FILE" 2>&1 &
    local python_pid=$!
    
    # Save PID (adjust path since we changed directory)
    cd - > /dev/null
    echo "$python_pid" > "$PYTHON_PID_FILE"
    
    # Wait a moment and check if it's still running
    sleep 3
    if is_running "$PYTHON_PID_FILE"; then
        print_status "$GREEN" "Python strategy started successfully (PID: $python_pid)"
        print_status "$GREEN" "Using config: $config_path"
        return 0
    else
        print_status "$RED" "ERROR: Python strategy failed to start. Check logs: $PYTHON_LOG_FILE"
        return 1
    fi
}

# Function to stop a process
stop_process() {
    local pid_file=$1
    local name=$2
    
    if ! is_running "$pid_file"; then
        print_status "$YELLOW" "$name is not running"
        return 1
    fi
    
    local pid=$(cat "$pid_file")
    print_status "$BLUE" "Stopping $name (PID: $pid)..."
    
    # Try graceful shutdown first
    kill -TERM "$pid" 2>/dev/null
    
    # Wait up to 10 seconds for graceful shutdown
    local count=0
    while ps -p "$pid" > /dev/null 2>&1 && [ $count -lt 10 ]; do
        sleep 1
        ((count++))
    done
    
    # Force kill if still running
    if ps -p "$pid" > /dev/null 2>&1; then
        print_status "$YELLOW" "Force killing $name..."
        kill -KILL "$pid" 2>/dev/null
        sleep 1
    fi
    
    # Clean up PID file
    rm -f "$pid_file"
    print_status "$GREEN" "$name stopped successfully"
    return 0
}

# Function to show status
show_status() {
    print_status "$BLUE" "=== Trading System Status ==="
    
    if is_running "$CPP_PID_FILE"; then
        print_status "$GREEN" "C++ Bridge: RUNNING (PID: $(cat $CPP_PID_FILE))"
    else
        print_status "$RED" "C++ Bridge: STOPPED"
    fi
    
    if is_running "$PYTHON_PID_FILE"; then
        print_status "$GREEN" "Python Strategy: RUNNING (PID: $(cat $PYTHON_PID_FILE))"
    else
        print_status "$RED" "Python Strategy: STOPPED"
    fi
    
    echo ""
    print_status "$BLUE" "Log files:"
    print_status "$NC" "  C++ Build: $BUILD_LOG_FILE"
    print_status "$NC" "  C++ Runtime: $CPP_LOG_FILE"
    print_status "$NC" "  Python: $PYTHON_LOG_FILE"
}

# Function to show logs
show_logs() {
    local service=$1
    local lines=${2:-50}
    
    case "$service" in
        "cpp"|"c++")
            if [[ -f "$CPP_LOG_FILE" ]]; then
                print_status "$BLUE" "=== C++ Bridge Logs (last $lines lines) ==="
                tail -n "$lines" "$CPP_LOG_FILE"
            else
                print_status "$RED" "C++ runtime log file not found: $CPP_LOG_FILE"
            fi
            ;;
        "build")
            if [[ -f "$BUILD_LOG_FILE" ]]; then
                print_status "$BLUE" "=== C++ Build Logs (last $lines lines) ==="
                tail -n "$lines" "$BUILD_LOG_FILE"
            else
                print_status "$RED" "C++ build log file not found: $BUILD_LOG_FILE"
            fi
            ;;
        "python"|"py")
            if [[ -f "$PYTHON_LOG_FILE" ]]; then
                print_status "$BLUE" "=== Python Strategy Logs (last $lines lines) ==="
                tail -n "$lines" "$PYTHON_LOG_FILE"
            else
                print_status "$RED" "Python log file not found: $PYTHON_LOG_FILE"
            fi
            ;;
        "all"|"")
            show_logs "build" "$lines"
            echo ""
            show_logs "cpp" "$lines"
            echo ""
            show_logs "python" "$lines"
            ;;
        *)
            print_status "$RED" "Unknown service: $service. Use 'cpp', 'python', 'build', or 'all'"
            ;;
    esac
}

# Main script logic
case "$1" in
    "start")
        print_status "$BLUE" "=== Starting Trading System ==="
        
        # Start C++ first (includes build step, Python depends on it)
        if start_cpp; then
            print_status "$BLUE" "Waiting 5 seconds for C++ to initialize..."
            sleep 5
            
            # Start Python
            start_python
        else
            print_status "$RED" "Failed to start C++ bridge. Aborting."
            exit 1
        fi
        
        echo ""
        show_status
        ;;
        
    "stop")
        print_status "$BLUE" "=== Stopping Trading System ==="
        
        # Stop Python first (graceful shutdown)
        stop_process "$PYTHON_PID_FILE" "Python strategy"
        
        # Then stop C++
        stop_process "$CPP_PID_FILE" "C++ bridge"
        
        echo ""
        show_status
        ;;
        
    "restart")
        print_status "$BLUE" "=== Restarting Trading System ==="
        "$0" stop
        sleep 3
        "$0" start
        ;;
        
    "build")
        print_status "$BLUE" "=== Building C++ Project Only ==="
        if build_cpp; then
            print_status "$GREEN" "Build completed successfully"
        else
            print_status "$RED" "Build failed"
            exit 1
        fi
        ;;
        
    "status")
        show_status
        ;;
        
    "logs")
        show_logs "$2" "$3"
        ;;
        
    "follow")
        # Follow logs in real-time
        service=${2:-"all"}
        case "$service" in
            "cpp"|"c++")
                tail -f "$CPP_LOG_FILE"
                ;;
            "build")
                tail -f "$BUILD_LOG_FILE"
                ;;
            "python"|"py")
                tail -f "$PYTHON_LOG_FILE"
                ;;
            "all"|"")
                print_status "$BLUE" "Following all logs (Ctrl+C to stop)..."
                tail -f "$BUILD_LOG_FILE" "$CPP_LOG_FILE" "$PYTHON_LOG_FILE"
                ;;
        esac
        ;;
        
    *)
        echo "Usage: $0 {start|stop|restart|build|status|logs|follow} [options]"
        echo ""
        echo "Commands:"
        echo "  start     - Build C++ project, then start both C++ bridge and Python strategy"
        echo "  stop      - Stop both processes gracefully"
        echo "  restart   - Stop and start both processes (includes rebuild)"
        echo "  build     - Build C++ project only (without starting services)"
        echo "  status    - Show current status of both processes"
        echo "  logs      - Show recent logs"
        echo "            Usage: $0 logs [cpp|python|build|all] [number_of_lines]"
        echo "  follow    - Follow logs in real-time"
        echo "            Usage: $0 follow [cpp|python|build|all]"
        echo ""
        echo "Log Management:"
        echo "  - Build logs: $BUILD_LOG_FILE"
        echo "  - C++ runtime logs: $CPP_LOG_FILE"
        echo "  - Python logs: $PYTHON_LOG_FILE"
        echo ""
        echo "Examples:"
        echo "  $0 start"
        echo "  $0 build"
        echo "  $0 logs build 100"
        echo "  $0 follow cpp"
        exit 1
        ;;
esac