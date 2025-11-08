#!/bin/bash

# Runtime fix for pthread priority assertion error
# This script helps diagnose and fix the "Fatal glibc error: tpp.c:83" issue

echo "=== Pthread Priority Error Diagnostic & Fix Script ==="
echo ""

# Check current system limits
echo "1. Checking system limits..."
echo "Real-time priority limit: $(ulimit -r)"
echo "Scheduling priority limit: $(ulimit -e)"
echo "Max locked memory: $(ulimit -l) KB"

# Check if we're running in a container
if [ -f /.dockerenv ]; then
    echo "Running in Docker container"
elif [ -f /proc/vz/veinfo ]; then
    echo "Running in OpenVZ container"
elif systemd-detect-virt -q; then
    echo "Running in virtualized environment: $(systemd-detect-virt)"
else
    echo "Running on bare metal/VPS"
fi

echo ""
echo "2. Checking pthread capabilities..."

# Test if we can create threads with priority
cat > /tmp/pthread_test.c << 'EOF'
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

int main() {
    int policy = sched_getscheduler(0);
    printf("Current scheduling policy: %d\n", policy);
    
    int max_prio = sched_get_priority_max(SCHED_OTHER);
    int min_prio = sched_get_priority_min(SCHED_OTHER);
    printf("SCHED_OTHER priority range: %d to %d\n", min_prio, max_prio);
    
    max_prio = sched_get_priority_max(SCHED_FIFO);
    min_prio = sched_get_priority_min(SCHED_FIFO);
    printf("SCHED_FIFO priority range: %d to %d\n", min_prio, max_prio);
    
    // Test thread creation with default attributes
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    
    int result = pthread_create(&thread, &attr, NULL, NULL);
    if (result == 0) {
        printf("Basic thread creation: OK\n");
        pthread_cancel(thread);
    } else {
        printf("Basic thread creation failed: %s\n", strerror(result));
    }
    
    pthread_attr_destroy(&attr);
    return 0;
}
EOF

# Compile and run the test
if gcc -o /tmp/pthread_test /tmp/pthread_test.c -lpthread 2>/dev/null; then
    /tmp/pthread_test
    rm -f /tmp/pthread_test /tmp/pthread_test.c
else
    echo "Could not compile pthread test (gcc not available)"
    rm -f /tmp/pthread_test.c
fi

echo ""
echo "3. Recommended fixes:"
echo ""

if [ $(ulimit -r) -eq 0 ]; then
    echo "❌ Real-time priority is disabled"
    echo "✅ Fix 1: Export environment variables to disable RT scheduling:"
    echo "   export ZMQ_THREAD_PRIORITY=-1"
    echo "   export ZMQ_THREAD_SCHED_POLICY=0"
    echo "   export NO_RT_SCHEDULING=1"
    echo ""
    echo "✅ Fix 2: Use the provided script:"
    echo "   source scripts/disable_rt_scheduling.sh"
    echo ""
    echo "✅ Fix 3: Run with modified ulimits (may not work on VPS):"
    echo "   ulimit -r 1 && ./your_program"
    echo ""
else
    echo "✅ Real-time priority is available (limit: $(ulimit -r))"
fi

echo "✅ Fix 4: Code changes have been applied to:"
echo "   - cpp-execution/src/communication/zmq_manager.cpp (disable ZMQ RT scheduling)"
echo "   - cpp-execution/src/main.cpp (system capability check)"
echo "   - docker-compose.yml (environment variables)"

echo ""
echo "4. Testing the fixes:"
echo ""
echo "To test with environment variables:"
echo "  cd /home/e8/works/hybrid-bot"
echo "  source scripts/disable_rt_scheduling.sh"
echo "  # Then run your trading system"
echo ""

echo "To rebuild with the code fixes:"
echo "  cd cpp-execution && mkdir -p build && cd build"
echo "  cmake .. && make -j4"
echo ""

echo "5. Long-term solution for VPS:"
echo "Most VPS providers disable real-time scheduling for security and stability."
echo "The trading system should work fine without RT scheduling - it just uses"
echo "regular thread priorities instead of real-time ones."