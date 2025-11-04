#!/usr/bin/env python3
"""
Demo Hedging Integration với Regime Publisher
Chạy để test hedging logic với mock data
"""

import sys
import json
from pathlib import Path

# Add paths
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
sys.path.insert(0, str(SRC_DIR))
sys.path.insert(0, str(HERE))

def demo_hedging_logic():
    """Demo hedging logic với mock regime data"""
    
    print("🚀 DEMO HEDGING INTEGRATION")
    print("="*50)
    
    # Mock BTC regime data scenarios
    scenarios = [
        {
            'name': 'Market Dump - Should Open Short',
            'data': {
                'regime': 2,
                'probs': [0.1, 0.2, 0.6, 0.1],
                'p_trend_up': 0.005,  # Very low - market dump
                'last_ts': '2024-01-01T12:00:00Z'
            }
        },
        {
            'name': 'Market Recovery - Should Close Short',
            'data': {
                'regime': 0,
                'probs': [0.7, 0.2, 0.1, 0.0],
                'p_trend_up': 0.95,  # Very high - market recovery
                'last_ts': '2024-01-01T12:05:00Z'
            }
        },
        {
            'name': 'Neutral Zone - Should Wait',
            'data': {
                'regime': 1,
                'probs': [0.3, 0.4, 0.2, 0.1],
                'p_trend_up': 0.5,  # Neutral
                'last_ts': '2024-01-01T12:10:00Z'
            }
        }
    ]
    
    # Mock hedging config
    mock_config = {
        'enabled': True,
        'api_key': 'mock_key',
        'api_secret': 'mock_secret',
        'testnet': True,
        'emergency_p_trend_threshold': 0.01,
        'close_p_trend_threshold': 0.9,
        'hedge_amount': 0.01,
        'leverage': 10
    }
    
    print(f"📋 Hedging Config:")
    print(f"   Emergency threshold: {mock_config['emergency_p_trend_threshold']}")
    print(f"   Close threshold: {mock_config['close_p_trend_threshold']}")
    print(f"   Hedge amount: {mock_config['hedge_amount']} BTC")
    print(f"   Leverage: {mock_config['leverage']}x")
    print()
    
    # Mock hedging manager (without actual trading)
    class MockHedgingManager:
        def __init__(self, config):
            self.config = config
            self.emergency_p_trend_threshold = config['emergency_p_trend_threshold']
            self.close_p_trend_threshold = config['close_p_trend_threshold']
            self.is_hedged = False
            
        def process_regime_signal(self, regime_data):
            p_trend_up = regime_data.get('p_trend_up')
            
            result = {
                'action': 'none',
                'reason': '',
                'success': True,
                'is_hedged': self.is_hedged,
                'timestamp': '2024-01-01T12:00:00Z'
            }
            
            if p_trend_up is None:
                result['reason'] = 'p_trend_up not available'
                return result
            
            if p_trend_up <= self.emergency_p_trend_threshold:
                if not self.is_hedged:
                    result['action'] = 'open_short'
                    result['reason'] = f'Market dump: p_trend_up={p_trend_up:.4f} <= {self.emergency_p_trend_threshold}'
                    self.is_hedged = True
                else:
                    result['action'] = 'hold_short'
                    result['reason'] = f'Already hedged, market dumping: p_trend_up={p_trend_up:.4f}'
                    
            elif p_trend_up >= self.close_p_trend_threshold:
                if self.is_hedged:
                    result['action'] = 'close_short'
                    result['reason'] = f'Market recovery: p_trend_up={p_trend_up:.4f} >= {self.close_p_trend_threshold}'
                    self.is_hedged = False
                else:
                    result['action'] = 'no_position'
                    result['reason'] = f'No position, market up: p_trend_up={p_trend_up:.4f}'
            else:
                if self.is_hedged:
                    result['action'] = 'hold_short'
                    result['reason'] = f'Neutral zone, holding: p_trend_up={p_trend_up:.4f}'
                else:
                    result['action'] = 'wait'
                    result['reason'] = f'Neutral zone, waiting: p_trend_up={p_trend_up:.4f}'
            
            result['is_hedged'] = self.is_hedged
            return result
    
    # Test scenarios
    hedging_manager = MockHedgingManager(mock_config)
    
    for i, scenario in enumerate(scenarios):
        print(f"📊 Scenario {i+1}: {scenario['name']}")
        print(f"   Input: regime={scenario['data']['regime']}, p_trend_up={scenario['data']['p_trend_up']}")
        
        result = hedging_manager.process_regime_signal(scenario['data'])
        
        action_emoji = {
            'open_short': '🔴',
            'close_short': '✅',
            'hold_short': '⏳',
            'wait': '⏸️',
            'no_position': '💰',
            'none': '❓'
        }
        
        print(f"   {action_emoji.get(result['action'], '❓')} Action: {result['action']}")
        print(f"   📝 Reason: {result['reason']}")
        print(f"   🛡️  Is Hedged: {result['is_hedged']}")
        print()
    
    print("✅ Demo completed!")
    print()
    print("🔧 Integration Steps:")
    print("1. Set BINANCE_API_KEY and BINANCE_API_SECRET environment variables")
    print("2. Update hedging_config.json with your settings")
    print("3. Install ccxt: pip install ccxt")
    print("4. Run: python regime_publisher.py")
    print()
    print("⚠️  Important:")
    print("- Test on testnet first (testnet: true in config)")
    print("- Monitor logs for hedging actions")
    print("- BTC regime p_trend_up will trigger hedging automatically")


if __name__ == '__main__':
    demo_hedging_logic()