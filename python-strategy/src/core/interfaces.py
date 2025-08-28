# src/core/interfaces.py
"""
Bio-Inspired Trading System - Core Interfaces
Foundation layer defining all system interfaces and data models
"""

from abc import ABC, abstractmethod
from typing import Dict, Any, Optional, List, Protocol, Union, Callable
from dataclasses import dataclass, field
from enum import Enum
import asyncio
from datetime import datetime
import uuid


class SystemState(Enum):
    """System health states inspired by biological organism states"""
    HEALTHY = "healthy"
    WARNING = "warning"
    CRITICAL = "critical"
    EMERGENCY = "emergency"
    HIBERNATING = "hibernating"  # Low activity mode
    RECOVERING = "recovering"    # Self-healing mode


class MarketRegime(Enum):
    """Market regime classification for adaptive behavior"""
    TREND = "trend"
    MEAN_REVERSION = "mean_reversion"
    HIGH_VOLATILITY = "high_volatility"
    LOW_VOLATILITY = "low_volatility"
    SIDEWAYS = "sideways"
    CRISIS = "crisis"
    UNKNOWN = "unknown"


class LayerType(Enum):
    """Bio-inspired layer types"""
    CONSCIOUSNESS = "consciousness"
    ADAPTATION = "adaptation"
    HOMEOSTASIS = "homeostasis"
    PERCEPTION = "perception"
    EXECUTION = "execution"


class EventPriority(Enum):
    """Event priority levels for message routing"""
    LOW = 1
    NORMAL = 2
    HIGH = 3
    CRITICAL = 4
    EMERGENCY = 5


@dataclass
class SystemHealth:
    """Comprehensive system health metrics"""
    stability_score: float          # 0-100, overall system stability
    risk_level: float              # 0-1, current risk exposure
    performance_score: float       # 0-100, recent performance quality
    adaptation_score: float        # 0-100, adaptation effectiveness
    state: SystemState
    timestamp: datetime
    details: Dict[str, Any] = field(default_factory=dict)
    alerts: List[str] = field(default_factory=list)
    
    def is_healthy(self) -> bool:
        """Check if system is in healthy state"""
        return self.state in [SystemState.HEALTHY, SystemState.WARNING]
    
    def needs_intervention(self) -> bool:
        """Check if manual intervention is needed"""
        return self.state in [SystemState.CRITICAL, SystemState.EMERGENCY]


@dataclass
class MarketState:
    """Current market conditions and regime information"""
    regime: MarketRegime
    confidence: float              # 0-1, confidence in regime classification
    volatility: float             # Current volatility measure
    trend_strength: float         # -1 to 1, trend direction and strength
    mean_reversion_strength: float # 0-1, mean reversion tendency
    liquidity_score: float        # 0-1, market liquidity assessment
    timestamp: datetime
    symbol: str = ""
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TradingSignal:
    """Trading signal with bio-inspired confidence scoring"""
    signal_id: str = field(default_factory=lambda: str(uuid.uuid4()))
    symbol: str = ""
    action: str = "HOLD"           # BUY, SELL, HOLD
    strength: float = 0.0          # 0-1, signal strength
    confidence: float = 0.0        # 0-1, confidence in signal
    position_size: float = 0.0     # Suggested position size
    regime: MarketRegime = MarketRegime.UNKNOWN
    entry_price: Optional[float] = None
    stop_loss: Optional[float] = None
    take_profit: Optional[float] = None
    timestamp: datetime = field(default_factory=datetime.now)
    metadata: Dict[str, Any] = field(default_factory=dict)
    
    def is_valid(self) -> bool:
        """Validate signal integrity"""
        return (
            self.action in ["BUY", "SELL", "HOLD"] and
            0 <= self.strength <= 1 and
            0 <= self.confidence <= 1 and
            self.position_size >= 0
        )


@dataclass
class PerformanceMetrics:
    """Performance tracking metrics"""
    returns: float                 # Total returns
    sharpe_ratio: float           # Risk-adjusted returns
    max_drawdown: float           # Maximum drawdown
    win_rate: float               # Percentage of winning trades
    total_trades: int             # Number of trades executed
    avg_trade_duration: float     # Average holding period
    volatility: float             # Portfolio volatility
    alpha: float                  # Risk-adjusted alpha
    beta: float                   # Market beta
    timestamp: datetime = field(default_factory=datetime.now)
    period_days: int = 30
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class LayerHealth:
    """Health status for individual layers"""
    layer_type: LayerType
    is_active: bool = True
    cpu_usage: float = 0.0        # 0-100
    memory_usage: float = 0.0     # 0-100
    response_time_ms: float = 0.0
    error_rate: float = 0.0       # 0-1
    last_update: datetime = field(default_factory=datetime.now)
    status_message: str = ""
    metrics: Dict[str, Any] = field(default_factory=dict)


# Protocol definitions for type checking
class EventHandler(Protocol):
    """Protocol for event handling functions"""
    async def __call__(self, event: 'Event') -> None: ...


class LayerInterface(ABC):
    """Base interface for all bio-inspired layers"""
    
    def __init__(self, layer_type: LayerType, config: Dict[str, Any]):
        self.layer_type = layer_type
        self.config = config
        self.is_initialized = False
        self.health_status = LayerHealth(layer_type=layer_type)
    
    @abstractmethod
    async def initialize(self) -> bool:
        """Initialize the layer - like biological system startup"""
        pass
    
    @abstractmethod
    async def process(self, input_data: Any) -> Any:
        """Main processing method - the layer's core function"""
        pass
    
    @abstractmethod
    async def get_health_status(self) -> LayerHealth:
        """Return current health metrics - vital signs"""
        pass
    
    @abstractmethod
    async def adapt(self, feedback: Dict[str, Any]) -> None:
        """Adapt behavior based on feedback - learning mechanism"""
        pass
    
    @abstractmethod
    async def emergency_shutdown(self) -> None:
        """Emergency shutdown procedure - survival mechanism"""
        pass
    
    @abstractmethod
    async def shutdown(self) -> None:
        """Graceful shutdown - planned rest"""
        pass
    
    async def self_diagnose(self) -> Dict[str, Any]:
        """Self-diagnostic routine"""
        return {
            "layer_type": self.layer_type.value,
            "is_initialized": self.is_initialized,
            "health": await self.get_health_status(),
            "timestamp": datetime.now()
        }


class PerceptionInterface(LayerInterface):
    """Interface for market perception and sensing - sensory system"""
    
    @abstractmethod
    async def sense_market(self, symbols: List[str]) -> Dict[str, MarketState]:
        """Detect current market regime and conditions for symbols"""
        pass
    
    @abstractmethod
    async def analyze_sentiment(self, symbol: str) -> float:
        """Analyze market sentiment (0-1 scale)"""
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
    
    @abstractmethod
    async def detect_anomalies(self, data: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Detect market anomalies that might affect trading"""
        pass


class HomeostasisInterface(LayerInterface):
    """Interface for dynamic risk balancing - immune and regulatory system"""
    
    @abstractmethod
    async def calculate_position_size(self, signal: TradingSignal, 
                                    portfolio_health: float) -> float:
        """Calculate optimal position size based on system health"""
        pass
    
    @abstractmethod
    async def adjust_risk_parameters(self, performance: PerformanceMetrics) -> Dict[str, Any]:
        """Dynamically adjust risk parameters based on performance"""
        pass
    
    @abstractmethod
    async def monitor_stability(self) -> float:
        """Monitor system stability (0-100 scale)"""
        pass
    
    @abstractmethod
    async def check_exposure_limits(self, portfolio: Dict[str, float]) -> bool:
        """Check if portfolio exposure is within safe limits"""
        pass
    
    @abstractmethod
    async def emergency_stop_check(self, portfolio: Dict[str, Any]) -> bool:
        """Check if emergency stop should be triggered"""
        pass
    
    @abstractmethod
    async def rebalance_portfolio(self, current_positions: Dict[str, float]) -> Dict[str, float]:
        """Suggest portfolio rebalancing for optimal health"""
        pass


class AdaptationInterface(LayerInterface):
    """Interface for learning and strategy optimization - nervous system"""
    
    @abstractmethod
    async def optimize_strategy_parameters(self, strategy_id: str, 
                                         performance: PerformanceMetrics) -> Dict[str, Any]:
        """Optimize strategy parameters based on performance feedback"""
        pass
    
    @abstractmethod
    async def select_optimal_strategy(self, market_state: MarketState) -> str:
        """Select best strategy for current market regime"""
        pass
    
    @abstractmethod
    async def update_allocations(self, portfolio: Dict[str, float], 
                               performance: PerformanceMetrics) -> Dict[str, float]:
        """Update portfolio allocations based on learning"""
        pass
    
    @abstractmethod
    async def learn_from_performance(self, trades: List[Dict], 
                                   outcomes: List[float]) -> None:
        """Learn from trading performance - memory formation"""
        pass
    
    @abstractmethod
    async def get_adaptation_score(self) -> float:
        """Get current adaptation effectiveness (0-100 scale)"""
        pass
    
    @abstractmethod
    async def evolve_strategies(self, generation_data: Dict[str, Any]) -> List[Dict[str, Any]]:
        """Evolve trading strategies using genetic algorithms"""
        pass


class ConsciousnessInterface(LayerInterface):
    """Interface for meta-intelligence and system oversight - brain"""
    
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
    
    @abstractmethod
    async def coordinate_layers(self, layer_statuses: Dict[str, LayerHealth]) -> Dict[str, Any]:
        """Coordinate activities between all layers"""
        pass


class ExecutionInterface(LayerInterface):
    """Interface for trade execution with feedback - motor system"""
    
    @abstractmethod
    async def execute_trade(self, signal: TradingSignal) -> Dict[str, Any]:
        """Execute trading signal"""
        pass
    
    @abstractmethod
    async def monitor_latency(self) -> float:
        """Monitor execution latency (milliseconds)"""
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
    
    @abstractmethod
    async def get_portfolio_status(self) -> Dict[str, Any]:
        """Get current portfolio status and positions"""
        pass


class DatabaseInterface(ABC):
    """Abstract database interface for the bio-inspired system"""
    
    @abstractmethod
    async def connect(self) -> bool:
        """Establish database connection"""
        pass
    
    @abstractmethod
    async def disconnect(self) -> None:
        """Close database connection"""
        pass
    
    @abstractmethod
    async def execute_query(self, query: str, params: Optional[Dict] = None) -> Any:
        """Execute database query"""
        pass
    
    @abstractmethod
    async def health_check(self) -> bool:
        """Check database health"""
        pass
    
    @abstractmethod
    async def store_system_health(self, health: SystemHealth) -> bool:
        """Store system health metrics"""
        pass
    
    @abstractmethod
    async def store_performance_metrics(self, metrics: PerformanceMetrics) -> bool:
        """Store performance metrics"""
        pass
    
    @abstractmethod
    async def get_historical_performance(self, days: int = 30) -> List[PerformanceMetrics]:
        """Retrieve historical performance data"""
        pass


# Type aliases for convenience
LayerRegistry = Dict[LayerType, LayerInterface]
EventHandlerRegistry = Dict[str, List[EventHandler]]
HealthSnapshot = Dict[LayerType, LayerHealth]