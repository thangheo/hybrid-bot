# src/core/adaptation_interface.py
class AdaptationInterface(LayerInterface):
    """Interface for learning and strategy optimization"""
    
    @abstractmethod
    async def optimize_strategy_parameters(self, strategy_id: str, 
                                         performance: PerformanceMetrics) -> Dict[str, Any]:
        """Optimize strategy parameters based on performance"""
        pass
    
    @abstractmethod
    async def select_optimal_strategy(self, market_state: MarketState) -> str:
        """Select best strategy for current market regime"""
        pass
    
    @abstractmethod
    async def update_allocations(self, portfolio: Dict[str, float], 
                               performance: PerformanceMetrics) -> Dict[str, float]:
        """Update portfolio allocations"""
        pass
    
    @abstractmethod
    async def learn_from_performance(self, trades: List[Dict], 
                                   outcomes: List[float]) -> None:
        """Learn from trading performance"""
        pass
    
    @abstractmethod
    async def get_adaptation_score(self) -> float:
        """Get current adaptation effectiveness (0-100)"""
        pass