#pragma once

#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <sched.h>
#include <algorithm>
#include <cstdio>

namespace trading {
namespace system {

/**
 * System capabilities checker to prevent pthread priority errors
 */
class SystemCapabilities {
public:
    /**
     * Check if real-time scheduling is available
     * @return true if RT scheduling is available, false otherwise
     */
    static bool hasRealTimeScheduling() {
        // Check if we can get real-time priority limits
        int fifo_max = sched_get_priority_max(SCHED_FIFO);
        int fifo_min = sched_get_priority_min(SCHED_FIFO);
        
        return (fifo_max > fifo_min && fifo_max > 0);
    }
    
    /**
     * Check if we can set thread priorities
     * @return true if thread priority setting is available
     */
    static bool canSetThreadPriority() {
        pthread_t current_thread = pthread_self();
        int policy;
        struct sched_param param;
        
        // Try to get current thread scheduling parameters
        int result = pthread_getschedparam(current_thread, &policy, &param);
        return (result == 0);
    }
    
    /**
     * Get maximum allowed priority for the current scheduling policy
     * @return maximum priority, or -1 if not available
     */
    static int getMaxAllowedPriority() {
        int policy = sched_getscheduler(0);
        if (policy == -1) return -1;
        
        return sched_get_priority_max(policy);
    }
    
    /**
     * Get minimum allowed priority for the current scheduling policy
     * @return minimum priority, or -1 if not available
     */
    static int getMinAllowedPriority() {
        int policy = sched_getscheduler(0);
        if (policy == -1) return -1;
        
        return sched_get_priority_min(policy);
    }
    
    /**
     * Safe thread priority setting that won't cause pthread assertion failures
     * @param thread_id Thread to set priority for
     * @param priority Desired priority (will be clamped to valid range)
     * @return true if successful, false if not supported
     */
    static bool safeSetThreadPriority(pthread_t thread_id, int priority) {
        if (!canSetThreadPriority()) {
            return false; // Not supported on this system
        }
        
        int min_prio = getMinAllowedPriority();
        int max_prio = getMaxAllowedPriority();
        
        if (min_prio == -1 || max_prio == -1) {
            return false; // Can't determine valid range
        }
        
        // Clamp priority to valid range
        int safe_priority = std::max(min_prio, std::min(max_prio, priority));
        
        struct sched_param param;
        param.sched_priority = safe_priority;
        
        int policy = sched_getscheduler(0);
        if (policy == -1) policy = SCHED_OTHER; // Fallback to default
        
        return pthread_setschedparam(thread_id, policy, &param) == 0;
    }
    
    /**
     * Initialize system capabilities and log warnings if RT features are unavailable
     */
    static void initializeAndLog() {
        bool has_rt = hasRealTimeScheduling();
        bool can_set_priority = canSetThreadPriority();
        
        if (!has_rt) {
            // Log warning but don't fail
            fprintf(stderr, "[SYSTEM] Real-time scheduling not available (this is normal on VPS/shared systems)\n");
        }
        
        if (!can_set_priority) {
            fprintf(stderr, "[SYSTEM] Thread priority setting not available (using default scheduling)\n");
        }
        
        if (has_rt && can_set_priority) {
            int max_prio = getMaxAllowedPriority();
            int min_prio = getMinAllowedPriority();
            fprintf(stderr, "[SYSTEM] Priority range available: %d to %d\n", min_prio, max_prio);
        }
    }
};

} // namespace system
} // namespace trading