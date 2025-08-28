# src/core/perception_interface.py
class PerceptionInterface(LayerInterface):
    """Interface for market perception and sensing"""
    
    @abstractmethod
    async def sense_market(self) -> MarketState:
        """Detect current market regime and conditions"""
        pass
    
    @abstractmethod
    async def analyze_sentiment(self, symbol: str) -> float:
        """Analyze market sentiment (0-1)"""
        pass
    
    @abstractmethod
    async def detect_correlations(self, symbols: List[str]) -> Dict[str, float]:
        """Detect asset correlations"""
        pass
    
    @abstractmethod
    async def score_confidence(self, signal: TradingSignal) -> float:
        """Score confidence for trading signals"""
        pass
    
    @abstractmethod
    async def get_market_data(self, symbol: str, timeframe: str) -> Dict[str, Any]:
        """Get latest market data"""
        pass