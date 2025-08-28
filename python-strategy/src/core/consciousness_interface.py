# src/core/consciousness_interface.py
class ConsciousnessInterface(LayerInterface):
    """Interface for meta-intelligence and system oversight"""
    
    @abstractmethod
    async def assess_system_health(self) -> SystemHealth:
        """Comprehensive system health assessment"""
        pass
    
    @abstractmethod
    async def detect_regime_change(self, market_history: List[MarketState]) -> bool:
        """Detect major market regime changes"""
        pass
    
    @abstractmethod
    async def make_meta_decisions(self, system_state: SystemHealth) -> Dict[str, Any]:
        """Make high-level strategic decisions"""
        pass
    
    @abstractmethod
    async def trigger_emergency_protocols(self, crisis_type: str) -> None:
        """Trigger emergency response protocols"""
        pass
    
    @abstractmethod
    async def evaluate_performance_trends(self, 
                                        performance_history: List[PerformanceMetrics]) -> Dict[str, float]:
        """Evaluate long-term performance trends"""
        pass