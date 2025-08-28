import asyncio
import time
from typing import Optional
import logging

logger = logging.getLogger(__name__)

class AdaptiveRateLimiter:
    def __init__(self, initial_delay: float = 0.5, max_delay: float = 5.0):
        self.delay = initial_delay
        self.max_delay = max_delay
        self.consecutive_errors = 0
        self.last_request_time = 0
        
    async def wait(self):
        """Wait before making next request"""
        current_time = time.time()
        time_since_last = current_time - self.last_request_time
        
        if time_since_last < self.delay:
            wait_time = self.delay - time_since_last
            await asyncio.sleep(wait_time)
        
        self.last_request_time = time.time()
        
    def on_success(self):
        """Called after successful request"""
        self.consecutive_errors = 0
        # Gradually reduce delay on success
        self.delay = max(0.1, self.delay * 0.9)
        
    def on_rate_limit(self):
        """Called when rate limit hit"""
        self.consecutive_errors += 1
        # Exponential backoff
        multiplier = 1.5 ** min(self.consecutive_errors, 5)
        self.delay = min(self.max_delay, self.delay * multiplier)
        logger.warning(f"Rate limit hit, increasing delay to {self.delay:.2f}s")
        
    def on_error(self):
        """Called on network/exchange errors"""
        self.consecutive_errors += 1
        self.delay = min(self.max_delay, self.delay * 1.2)