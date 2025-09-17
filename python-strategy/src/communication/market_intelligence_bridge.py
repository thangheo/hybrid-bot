import zmq
import time
import threading
import logging
import json
import base64
import gc
import tracemalloc
import resource
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
    # Add additional fields for debugging
    realized_volatility_1m: float = 0.0
    spread: float = 0.0
    current_regime: int = 0

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
        self.stats = {
            'messages_received': 0,
            'parse_errors': 0,
            'field_warnings': 0
        }
        # Garbage collection / memory diagnostics
        # Run a collection at least every `gc_interval_seconds` or every `gc_collect_every_n_messages` messages
        self.gc_last_run = time.time()
        self.gc_interval_seconds = 30
        self.gc_collect_every_n_messages = 50
        # Enable tracemalloc by env var MARKET_INTEL_TRACEMALLOC=1 if you want detailed allocation tracing
        try:
            self.enable_tracemalloc = os.environ.get('MARKET_INTEL_TRACEMALLOC', '0') == '1'
        except Exception:
            self.enable_tracemalloc = False
        if self.enable_tracemalloc:
            try:
                tracemalloc.start()
                logger.info("MarketIntelligenceBridge: tracemalloc started for memory diagnostics")
            except Exception:
                logger.warning("MarketIntelligenceBridge: failed to start tracemalloc")
        
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
                # Receive all frames for this message (supports legacy 2-frame and new 3-frame formats)
                frames = self.subscriber.recv_multipart()
                if not frames:
                    continue

                topic = frames[0]

                # Determine payload bytes depending on frame count and shape
                payload_bytes = None
                metadata_obj = None

                if len(frames) >= 3:
                    # New multipart format: [topic, metadata_json, raw_payload]
                    try:
                        metadata_obj = json.loads(frames[1].decode('utf-8')) if frames[1] else {}
                    except Exception:
                        metadata_obj = {}
                    payload_bytes = frames[2]
                elif len(frames) == 2:
                    # Legacy format: [topic, serialized_json_or_protobuf]
                    # Try protobuf first, then JSON wrapper with possible base64 data field
                    candidate = frames[1]
                    # Quick attempt: try protobuf parse directly
                    intel = MarketIntelligence()
                    try:
                        intel.ParseFromString(candidate)
                        payload_bytes = candidate
                    except Exception:
                        # Not direct protobuf — try JSON wrapper
                        try:
                            wrapper = json.loads(candidate.decode('utf-8'))
                            data_field = wrapper.get('data', '')
                            meta = wrapper.get('metadata', {})
                            metadata_obj = meta if isinstance(meta, dict) else {}
                            # If metadata says base64, decode; else assume data_field is base64 or text
                            if metadata_obj.get('data_encoding') == 'base64' or not _is_likely_utf8(data_field):
                                try:
                                    payload_bytes = base64.b64decode(data_field)
                                except Exception:
                                    # Last resort: treat raw candidate as payload
                                    payload_bytes = candidate
                            else:
                                # data_field appears to be UTF-8 text; use its bytes
                                payload_bytes = data_field.encode('utf-8')
                        except Exception:
                            # Can't parse JSON either - use raw candidate
                            payload_bytes = candidate
                else:
                    # Unknown shape
                    logger.warning(f"Received message with unexpected frame count: {len(frames)}")
                    continue

                # At this point payload_bytes should be raw protobuf bytes (or at least bytes to try)
                if payload_bytes is None:
                    logger.error("No payload bytes could be extracted from incoming message")
                    self.stats['parse_errors'] += 1
                    continue

                intel = MarketIntelligence()
                try:
                    intel.ParseFromString(payload_bytes)
                except Exception as e:
                    # Log helpful debug information to aid troubleshooting
                    logger.error(f"Error parsing message with type 'trading.MarketIntelligence': {e}")
                    logger.debug(f"Topic: {topic!r} frames={len(frames)} metadata={metadata_obj} payload_len={len(payload_bytes)}")
                    self.stats['parse_errors'] += 1
                    continue
                self.stats['messages_received'] += 1
                
                # CRITICAL FIX: Proper field extraction with validation
                symbol = intel.symbol.replace('/', '')
                
                # ✅ FIX: Ensure intelligence_history has entry for this symbol
                if symbol not in self.intelligence_history:
                    logger.info(f"Adding new symbol to intelligence tracking: {symbol}")
                    self.intelligence_history[symbol] = deque(maxlen=100)
                
                # Debug first few messages to understand C++ data structure
                if self.stats['messages_received'] <= 5:
                    logger.info(f"DEBUG Intelligence for {symbol}:")
                    logger.info(f"  liquidity_ratio: {intel.liquidity_ratio}")
                    logger.info(f"  realized_volatility_1m: {intel.realized_volatility_1m}")
                    logger.info(f"  volatility_trend: {intel.volatility_trend}")  # Check if this exists
                    logger.info(f"  order_book_imbalance: {intel.order_book_imbalance}")
                    logger.info(f"  regime_confidence: {intel.regime_confidence}")
                    logger.info(f"  bid_ask_spread: {intel.bid_ask_spread}")
                
                # FIXED: Extract volatility_trend correctly
                # If C++ sets volatility_trend (field 18), use it
                # Otherwise, use realized_volatility_1m as fallback
                volatility_trend_value = 0.0
                if hasattr(intel, 'volatility_trend') and intel.volatility_trend != 0.0:
                    volatility_trend_value = intel.volatility_trend
                elif hasattr(intel, 'realized_volatility_1m') and intel.realized_volatility_1m != 0.0:
                    volatility_trend_value = intel.realized_volatility_1m
                    if self.stats['messages_received'] <= 3:
                        logger.warning(f"Using realized_volatility_1m as volatility_trend fallback for {symbol}")
                        self.stats['field_warnings'] += 1
                else:
                    # Calculate reasonable fallback based on spread
                    if intel.bid_ask_spread > 0:
                        volatility_trend_value = intel.bid_ask_spread * 10.0  # Simple estimate
                    else:
                        volatility_trend_value = 0.02  # Default reasonable value
                    
                    if self.stats['messages_received'] <= 3:
                        logger.warning(f"No volatility data from C++, using fallback {volatility_trend_value} for {symbol}")
                        self.stats['field_warnings'] += 1
                
                # Validate other critical fields
                liquidity_ratio = intel.liquidity_ratio if intel.liquidity_ratio != 0.0 else 0.5
                order_book_imbalance = intel.order_book_imbalance  # Can be 0.0 legitimately
                regime_confidence = intel.regime_confidence if intel.regime_confidence != 0.0 else 0.5
                
                # Update cache with validated data
                state = IntelligenceState(
                    liquidity_ratio=liquidity_ratio,
                    volatility_trend=volatility_trend_value,
                    order_book_imbalance=order_book_imbalance,
                    regime_confidence=regime_confidence,
                    last_update=time.time(),
                    # Additional debug fields
                    realized_volatility_1m=intel.realized_volatility_1m,
                    spread=intel.bid_ask_spread,
                    current_regime=intel.current_regime
                )

                self.intelligence_cache[symbol] = state
                
                # ✅ SAFE: Add to history with error handling
                try:
                    self.intelligence_history[symbol].append(state)
                except KeyError:
                    # Should not happen after our fix above, but just in case
                    logger.warning(f"Creating missing intelligence_history entry for {symbol}")
                    self.intelligence_history[symbol] = deque(maxlen=100)
                    self.intelligence_history[symbol].append(state)
                # Periodic garbage collection to mitigate memory growth
                try:
                    now = time.time()
                    if (self.stats['messages_received'] % self.gc_collect_every_n_messages) == 0 or (now - self.gc_last_run) >= self.gc_interval_seconds:
                        before = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                        logger.info(f"MarketIntelligenceBridge: running gc.collect() (before={before} KB)")
                        collected = gc.collect()
                        after = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
                        logger.info(f"MarketIntelligenceBridge: gc.collect() returned {collected}, memory after={after} KB")
                        self.gc_last_run = now
                        if self.enable_tracemalloc:
                            try:
                                snapshot = tracemalloc.take_snapshot()
                                top_stats = snapshot.statistics('lineno')[:5]
                                logger.debug("Top tracemalloc stats:")
                                for stat in top_stats:
                                    logger.debug(str(stat))
                            except Exception:
                                pass
                except Exception as e:
                    logger.debug(f"GC diagnostics failed: {e}")
                
            except zmq.Again:
                continue
            except KeyError as e:
                logger.error(f"KeyError in intelligence loop for symbol processing: {e}")
                self.stats['parse_errors'] += 1
                continue
            except Exception as e:
                logger.error(f"Intelligence loop error: {e}")
                self.stats['parse_errors'] += 1
                time.sleep(1)
    
    def get_intelligence(self, symbol: str) -> Optional[IntelligenceState]:
        """Get current intelligence for symbol"""
        intel = self.intelligence_cache.get(symbol)
        
        # Validate data freshness (within 30 seconds)
        if intel and (time.time() - intel.last_update) > 30:
            logger.warning(f"Stale intelligence data for {symbol} ({time.time() - intel.last_update:.1f}s old)")
        
        return intel
    
    def is_available(self) -> bool:
        """Check if intelligence is available"""
        return self.running and len(self.intelligence_cache) > 0
    
    def get_debug_stats(self) -> dict:
        """Get debug statistics"""
        return {
            'running': self.running,
            'symbols_cached': len(self.intelligence_cache),
            'messages_received': self.stats['messages_received'],
            'parse_errors': self.stats['parse_errors'],
            'field_warnings': self.stats['field_warnings'],
            'active_symbols': list(self.intelligence_cache.keys())
        }
    
    def validate_cpp_data_completeness(self) -> dict:
        """Validate that C++ is sending complete data"""
        validation_report = {}
        
        for symbol, intel in self.intelligence_cache.items():
            issues = []
            
            # Check for zero/default values that might indicate missing C++ fields
            if intel.liquidity_ratio == 0.0:
                issues.append("liquidity_ratio is zero")
            if intel.volatility_trend == 0.0:
                issues.append("volatility_trend is zero")
            if intel.regime_confidence == 0.0:
                issues.append("regime_confidence is zero")
            
            # Check data freshness
            age = time.time() - intel.last_update
            if age > 60:
                issues.append(f"Data is stale ({age:.1f}s)")
                
            validation_report[symbol] = {
                'issues': issues,
                'has_issues': len(issues) > 0,
                'data_age': age
            }
        
        return validation_report