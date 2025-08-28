# monitoring/tasks/processor.py

import logging
import multiprocessing
import queue
import threading
import uuid
import time
from concurrent.futures import ThreadPoolExecutor, ProcessPoolExecutor
from ..config.settings import settings

logger = logging.getLogger(__name__)

class Task:
    """Represents a task to be processed."""
    
    def __init__(self, func, args=None, kwargs=None, task_id=None, name=None):
        self.func = func
        self.args = args or ()
        self.kwargs = kwargs or {}
        self.task_id = task_id or str(uuid.uuid4())
        self.name = name or func.__name__
        
    def run(self):
        """Execute the task."""
        try:
            logger.debug(f"Running task {self.name} ({self.task_id})")
            return self.func(*self.args, **self.kwargs)
        except Exception as e:
            logger.error(f"Error running task {self.name}: {e}")
            raise

class TaskProcessor:
    """Process tasks asynchronously."""
    
    def __init__(self, max_workers=None, use_processes=False):
        """
        Initialize the task processor.
        
        Args:
            max_workers: Maximum number of workers (default: CPU count)
            use_processes: Whether to use processes instead of threads
        """
        self.max_workers = max_workers or multiprocessing.cpu_count()
        self.use_processes = use_processes
        self._queue = queue.Queue()
        self._results = {}
        self._lock = threading.RLock()
        self._running = False
        self._worker_thread = None
        
        # Create executor
        if use_processes:
            self._executor = ProcessPoolExecutor(max_workers=self.max_workers)
        else:
            self._executor = ThreadPoolExecutor(max_workers=self.max_workers)
            
    def submit(self, func, args=None, kwargs=None, task_id=None, name=None):
        """
        Submit a task for processing.
        
        Args:
            func: Function to execute
            args: Positional arguments for func
            kwargs: Keyword arguments for func
            task_id: Unique ID for the task (generated if not provided)
            name: Name for the task (defaults to func.__name__)
            
        Returns:
            task_id
        """
        task = Task(func, args, kwargs, task_id, name)
        
        with self._lock:
            self._queue.put(task)
            self._results[task.task_id] = {"status": "queued"}
            
        logger.debug(f"Submitted task {task.name} ({task.task_id})")
        return task.task_id
        
    def _process_task(self, task):
        """
        Process a single task.
        
        Args:
            task: Task to process
        """
        with self._lock:
            self._results[task.task_id] = {"status": "running"}
            
        try:
            future = self._executor.submit(task.run)
            result = future.result()
            
            with self._lock:
                self._results[task.task_id] = {
                    "status": "completed",
                    "result": result,
                    "completed_at": time.time()
                }
                
            logger.debug(f"Completed task {task.name} ({task.task_id})")
        except Exception as e:
            with self._lock:
                self._results[task.task_id] = {
                    "status": "failed",
                    "error": str(e),
                    "completed_at": time.time()
                }
                
            logger.error(f"Task {task.name} ({task.task_id}) failed: {e}")
            
    def _worker_loop(self):
        """Main loop for the worker thread."""
        self._running = True
        
        while self._running:
            try:
                # Get task with timeout to allow checking _running periodically
                task = self._queue.get(timeout=1.0)
                self._process_task(task)
                self._queue.task_done()
            except queue.Empty:
                continue
            except Exception as e:
                logger.error(f"Error in worker loop: {e}")
                
    def start(self):
        """Start the task processor."""
        if self._worker_thread and self._worker_thread.is_alive():
            logger.warning("Task processor already running")
            return False
            
        logger.info("Starting task processor")
        self._worker_thread = threading.Thread(target=self._worker_loop, daemon=True)
        self._worker_thread.start()
        return True
        
    # monitoring/tasks/processor.py (continued)

    def stop(self):
        """Stop the task processor."""
        logger.info("Stopping task processor")
        self._running = False
        
        if self._worker_thread:
            self._worker_thread.join(timeout=5.0)
            
        self._executor.shutdown(wait=False)
        return not (self._worker_thread and self._worker_thread.is_alive())
        
    def get_result(self, task_id):
        """
        Get the result of a task.
        
        Args:
            task_id: ID of the task
            
        Returns:
            Dictionary with task status and result (if available)
        """
        with self._lock:
            return self._results.get(task_id, {"status": "unknown"})
            
    def get_queue_size(self):
        """Get the number of tasks in the queue."""
        return self._queue.qsize()
        
    def get_active_tasks(self):
        """
        Get the number of tasks currently being processed.
        
        Returns:
            Dictionary with counts by status
        """
        counts = {"queued": 0, "running": 0, "completed": 0, "failed": 0}
        
        with self._lock:
            for result in self._results.values():
                status = result.get("status")
                if status in counts:
                    counts[status] += 1
                    
        return counts
        
    def clean_completed(self, max_age=3600):
        """
        Clean up completed task results older than max_age seconds.
        
        Args:
            max_age: Maximum age in seconds for completed task results
            
        Returns:
            Number of task results cleaned up
        """
        now = time.time()
        count = 0
        
        with self._lock:
            to_remove = []
            
            for task_id, result in self._results.items():
                if result.get("status") in ("completed", "failed"):
                    completed_at = result.get("completed_at", 0)
                    if now - completed_at > max_age:
                        to_remove.append(task_id)
                        
            for task_id in to_remove:
                del self._results[task_id]
                count += 1
                
        return count
        
    def reset(self):
        """Reset the task processor, clearing all tasks and results."""
        with self._lock:
            # Empty the queue
            while not self._queue.empty():
                try:
                    self._queue.get_nowait()
                    self._queue.task_done()
                except queue.Empty:
                    break
                    
            # Clear results
            self._results.clear()
            
        return True

# Create singleton instance
task_processor = TaskProcessor()