# src/core/execution_interface.py
class ExecutionInterface(LayerInterface):
    """Interface for trade execution with feedback"""
    
    @abstractmethod
    async def execute_trade(self, signal: TradingSignal) -> Dict[str, Any]:
        """Execute trading signal"""
        pass
    
    @abstractmethod
    async def monitor_latency(self) -> float:
        """Monitor execution latency (ms)"""
        pass
    
    @abstractmethod
    async def check_connection_health(self) -> bool:
        """Check API connection health"""
        pass
    
    @abstractmethod
    async def get_execution_performance(self) -> Dict[str, float]:
        """Get execution performance metrics"""
        pass
    
    @abstractmethod
    async def emergency_stop_all(self) -> bool:
        """Emergency stop all trading activities"""
        pass