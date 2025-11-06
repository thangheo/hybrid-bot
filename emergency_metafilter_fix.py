#!/usr/bin/env python3
"""
Emergency MetaFilter Configuration Fix
Raise thresholds to prevent stuck regime trading
"""

import json
import os
from datetime import datetime

def create_emergency_metafilter_config():
    """Create emergency MetaFilter config to stop stuck regime trading"""
    
    emergency_config = {
        "emergency_metafilter_fix": {
            "timestamp": datetime.now().isoformat(),
            "reason": "DOT stuck in regime=1 with p_trend_up=1.0 contradiction",
            "description": "Raise thresholds to prevent trading on contradictory regime signals"
        },
        
        "metafilter_config": {
            # MUCH HIGHER THRESHOLDS
            "min_p_trend_up": 0.85,           # From 0.3 → 0.85 (85%)
            "min_regime_confidence": 0.70,    # From 0.2 → 0.70 (70%)
            "enable_trend_filter": True,
            
            # STRICTER EMERGENCY EXITS  
            "enable_emergency_exit": True,
            "emergency_p_trend_threshold": 0.20,  # From 0.01 → 0.20 (20%)
            "emergency_regime_threshold": 0.30,   # From 0.01 → 0.30 (30%)
            
            # ADDITIONAL LOGIC CONTRADICTION DETECTION
            "enable_contradiction_check": True,
            "max_regime_trend_contradiction": 0.5,  # If regime=1 (bear) but p_trend_up > 0.5, block
        },
        
        "reasoning": {
            "current_dot_issue": {
                "regime": 1,                    # Bear market
                "p_trend_up": 1.0,             # 100% uptrend (CONTRADICTION!)  
                "old_threshold": 0.3,          # Passed easily
                "new_threshold": 0.85,         # Will block
                "expected_result": "BLOCKED - threshold too high for contradictory signals"
            },
            
            "contradiction_logic": {
                "description": "If regime=1 (bear) but p_trend_up > 0.5, this indicates model confusion",
                "action": "Block all trading for this symbol until regime stabilizes",
                "safety": "Prevent losses from contradictory signals"
            }
        }
    }
    
    config_path = "/home/e8/works/hybrid-bot/config/emergency_metafilter.json"
    
    with open(config_path, 'w') as f:
        json.dump(emergency_config, f, indent=4)
    
    print("🚨 EMERGENCY METAFILTER CONFIG CREATED")
    print("="*60)
    print(f"📁 Config saved to: {config_path}")
    print("\n🎯 NEW THRESHOLDS:")
    print(f"   min_p_trend_up: 0.30 → 0.85 (85%)")
    print(f"   min_regime_confidence: 0.20 → 0.70 (70%)")
    print(f"   emergency_p_trend_threshold: 0.01 → 0.20 (20%)")
    print(f"   emergency_regime_threshold: 0.01 → 0.30 (30%)")
    print("\n🚫 DOT WILL BE BLOCKED:")
    print(f"   Current: p_trend_up=1.0, regime_confidence=0.8")
    print(f"   Status: BOTH thresholds will be met → Still ALLOWED")
    print(f"   Problem: Values are TOO PERFECT → Indicates stuck system")
    print("\n⚡ NEED ADDITIONAL FIX: Contradiction Detection")
    print("="*60)

if __name__ == "__main__":
    create_emergency_metafilter_config()