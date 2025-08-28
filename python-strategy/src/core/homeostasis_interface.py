# src/core/homeostasis_interface.py
class HomeostasisInterface(LayerInterface):
    """Interface for dynamic risk balancing"""
    
    @abstractmethod
    async def calculate_position_size(self, signal: TradingSignal, 
                                    portfolio_health: float) -> float:
        """Calculate optimal position size"""
        pass
    
    @abstractmethod
    async def adjust_risk_parameters(self, performance: PerformanceMetrics) -> Dict[str, Any]:
        """Dynamically adjust risk parameters"""
        pass
    
    @abstractmethod
    async def monitor_stability(self) -> float:
        """Monitor system stability (0-100)"""
        pass
    
    @abstractmethod
    async def check_exposure_limits(self, portfolio: Dict[str, float]) -> bool:
        """Check if portfolio exposure is within limits"""
        pass
    
    @abstractmethod
    async def emergency_stop_check(self, portfolio: Dict[str, Any]) -> bool:
        """Check if emergency stop should be triggered"""
        pass