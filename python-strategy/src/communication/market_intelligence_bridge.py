import zmq
import time
import threading
import logging
from typing import Dict, List, Optional, Deque
from collections import deque
from dataclasses import dataclass
import sys
import os 

from .trading_messages_pb2 import MarketIntelligence
logger = logging.getLogger(__name__)

@dataclass
class IntelligenceState:
    """Current market intelligence state"""
    liquidity_ratio: float
    volatility_trend: float
    order_book_imbalance: float
    regime_confidence: float
    last_update: float

class MarketIntelligenceBridge:
    def __init__(self, symbols: List[str]):
        self.symbols = symbols
        self.intelligence_cache: Dict[str, IntelligenceState] = {}
        self.intelligence_history: Dict[str, Deque] = {
            symbol: deque(maxlen=100) for symbol in symbols
        }
        self.running = False
        self.context = zmq.Context()
        self.subscriber = None
        self.thread = None
        
    def start(self) -> bool:
        """Start intelligence integration"""
        if self.running:
            return True
            
        try:
            self.subscriber = self.context.socket(zmq.SUB)
            self.subscriber.connect("tcp://localhost:5555")
            self.subscriber.setsockopt(zmq.SUBSCRIBE, b"MARKET_INTEL")
            self.subscriber.setsockopt(zmq.RCVTIMEO, 1000)
            
            self.running = True
            self.thread = threading.Thread(target=self._intelligence_loop)
            self.thread.daemon = True
            self.thread.start()
            
            logger.info("Market intelligence bridge started")
            return True
            
        except Exception as e:
            logger.error(f"Failed to start intelligence bridge: {e}")
            return False
    
    def stop(self):
        """Stop intelligence integration"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2)
        if self.subscriber:
            self.subscriber.close()
    
    def _intelligence_loop(self):
        """Background intelligence collection"""
        while self.running:
            try:
                topic = self.subscriber.recv()
                data = self.subscriber.recv()
                
                intel = MarketIntelligence()
                intel.ParseFromString(data)
                
                # Update cache
                state = IntelligenceState(
                    liquidity_ratio=intel.liquidity_ratio,
                    volatility_trend=intel.realized_volatility_1m,
                    order_book_imbalance=intel.order_book_imbalance,
                    regime_confidence=intel.regime_confidence,
                    last_update=time.time()
                )
                
                self.intelligence_cache[intel.symbol] = state
                self.intelligence_history[intel.symbol].append(state)
                
            except zmq.Again:
                continue
            except Exception as e:
                logger.error(f"Intelligence loop error: {e}")
                time.sleep(1)
    
    def get_intelligence(self, symbol: str) -> Optional[IntelligenceState]:
        """Get current intelligence for symbol"""
        return self.intelligence_cache.get(symbol)
    
    def is_available(self) -> bool:
        """Check if intelligence is available"""
        return self.running and len(self.intelligence_cache) > 0
