# monitoring/tasks/scheduler.py

import logging
import time
import heapq
import threading
import uuid
from datetime import datetime, timedelta
from ..config.settings import settings

logger = logging.getLogger(__name__)

class Task:
    """Represents a scheduled task."""
    
    def __init__(self, func, args=None, kwargs=None, interval=None, 
                 run_at=None, task_id=None, name=None):
        self.func = func
        self.args = args or ()
        self.kwargs = kwargs or {}
        self.interval = interval
        self.run_at = run_at or datetime.utcnow()
        self.task_id = task_id or str(uuid.uuid4())
        self.name = name or func.__name__
        self.last_run = None
        self.next_run = self.run_at
        
    def __lt__(self, other):
        return self.next_run < other.next_run
        
    def run(self):
        """Execute the task."""
        try:
            logger.debug(f"Running task {self.name} ({self.task_id})")
            self.last_run = datetime.utcnow()
            result = self.func(*self.args, **self.kwargs)
            
            # Schedule next run if interval is set
            if self.interval:
                self.next_run = datetime.utcnow() + timedelta(seconds=self.interval)
                return True, result
            return False, result
        except Exception as e:
            logger.error(f"Error running task {self.name}: {e}")
            
            # Still reschedule if interval is set
            if self.interval:
                self.next_run = datetime.utcnow() + timedelta(seconds=self.interval)
                return True, None
            return False, None

class TaskScheduler:
    """Schedule and execute tasks at specified times or intervals."""
    
    def __init__(self):
        self._tasks = []  # Priority queue of tasks
        self._tasks_by_id = {}  # Map of task_id to task
        self._lock = threading.RLock()  # For thread safety
        self._running = False
        self._thread = None
        
    def schedule(self, func, args=None, kwargs=None, interval=None, 
                run_at=None, task_id=None, name=None):
        """
        Schedule a task to run.
        
        Args:
            func: Function to execute
            args: Positional arguments for func
            kwargs: Keyword arguments for func
            interval: Interval in seconds for recurring tasks
            run_at: Datetime when task should first run (defaults to now)
            task_id: Unique ID for the task (generated if not provided)
            name: Name for the task (defaults to func.__name__)
            
        Returns:
            task_id
        """
        task = Task(func, args, kwargs, interval, run_at, task_id, name)
        
        with self._lock:
            # Remove existing task with same ID if it exists
            if task.task_id in self._tasks_by_id:
                self._remove_task(task.task_id)
                
            # Add the new task
            heapq.heappush(self._tasks, task)
            self._tasks_by_id[task.task_id] = task
            
        logger.info(f"Scheduled task {task.name} ({task.task_id}) to run at {task.next_run}")
        return task.task_id
        
    def _remove_task(self, task_id):
        """
        Remove a task by ID.
        
        Note: This method is not thread-safe on its own and should be called
        with self._lock acquired.
        """
        if task_id not in self._tasks_by_id:
            return False
            
        # Remove from task map
        task = self._tasks_by_id.pop(task_id)
        
        # Mark as canceled by setting next_run to None
        task.next_run = None
        
        # Note: We don't remove from self._tasks immediately because that would
        # require rebuilding the heap. Instead, we filter out tasks with next_run=None
        # when popping from the heap.
        
        return True
        
    def cancel(self, task_id):
        """
        Cancel a scheduled task.
        
        Args:
            task_id: ID of the task to cancel
            
        Returns:
            True if cancelled, False if not found
        """
        with self._lock:
            result = self._remove_task(task_id)
            
        if result:
            logger.info(f"Cancelled task {task_id}")
        return result
        
    def _get_next_task(self):
        """
        Get the next task to run.
        
        Returns:
            The next task, or None if no tasks are ready
        """
        with self._lock:
            # Clean up cancelled tasks
            while self._tasks and self._tasks[0].next_run is None:
                heapq.heappop(self._tasks)
                
            # Check if we have any tasks
            if not self._tasks:
                return None
                
            # Check if the next task is ready
            now = datetime.utcnow()
            if self._tasks[0].next_run <= now:
                return heapq.heappop(self._tasks)
                
            return None
            
    def _run_loop(self):
        """Main loop for the scheduler thread."""
        self._running = True
        
        while self._running:
            task = self._get_next_task()
            
            if task:
                # Run the task
                reschedule, _ = task.run()
                
                # Reschedule if needed
                if reschedule:
                    with self._lock:
                        heapq.heappush(self._tasks, task)
            else:
                # Sleep for a bit to avoid busy waiting
                time.sleep(0.1)
                
    def start(self):
        """Start the scheduler."""
        if self._thread and self._thread.is_alive():
            logger.warning("Scheduler already running")
            return False
            
        logger.info("Starting task scheduler")
        self._thread = threading.Thread(target=self._run_loop, daemon=True)
        self._thread.start()
        return True
        
    def stop(self):
        """Stop the scheduler."""
        logger.info("Stopping task scheduler")
        self._running = False
        
        if self._thread:
            self._thread.join(timeout=5.0)
            
        return not (self._thread and self._thread.is_alive())
        
    def get_tasks(self):
        """
        Get all scheduled tasks.
        
        Returns:
            List of task dictionaries
        """
        with self._lock:
            return [
                {
                    "task_id": task.task_id,
                    "name": task.name,
                    "interval": task.interval,
                    "last_run": task.last_run,
                    "next_run": task.next_run
                }
                for task in self._tasks_by_id.values()
                if task.next_run is not None
            ]

# Create singleton instance
scheduler = TaskScheduler()