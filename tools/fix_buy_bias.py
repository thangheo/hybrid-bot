#!/usr/bin/env python3

"""
Script to fix BUY bias in labeling logic
Analyzes current thresholds and applies symmetric fixes
"""

import json
import re
import os
from typing import Dict, Tuple

def analyze_current_bias():
    """Analyze current threshold configuration"""
    print("🔍 ANALYZING CURRENT BIAS...")
    
    # Read current config
    config_path = "config/strategy_config.json"
    with open(config_path, 'r') as f:
        config = json.load(f)
    
    imbalance_config = config['components']['imbalanceDetector']['parameters']
    
    print(f"📊 Current Configuration:")
    print(f"  • labelingDelayMs: {imbalance_config['labelingDelayMs']}")
    print(f"  • global_sensitivity: {imbalance_config['global_sensitivity']}")
    print(f"  • beta: {imbalance_config.get('beta', 'Not set')}")
    
    return config

def generate_balanced_fixes():
    """Generate code fixes for balanced thresholds"""
    
    fixes = {
        'threshold_defaults': '''
    struct DynamicThresholds {
        double buyThreshold = 0.45;      // ✅ Symmetric: reduced from 0.55
        double sellThreshold = -0.45;    // ✅ Symmetric: more negative from -0.35  
        double minProfitableReturn = 0.003;
    };''',
        
        'percentiles': '''
        // BALANCED percentiles for equal BUY/SELL opportunity
        const double P75 = 0.65; // use 65th percentile for buy (top 35%)
        const double P25 = 0.35; // use 35th percentile for sell (bottom 35%)''',
        
        'threshold_calculation': '''
    // SYMMETRIC threshold calculation with magnitude enforcement
    double buyThresh = std::max(MIN_Z_MAG, p75_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0)));
    double sellThresh = std::min(-MIN_Z_MAG, p25_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0)));
    
    // CRITICAL FIX: Ensure sellThresh magnitude >= MIN_Z_MAG  
    if (std::abs(sellThresh) < MIN_Z_MAG) {
        sellThresh = -MIN_Z_MAG;  // Force minimum symmetric magnitude
    }
    
    out.buyThreshold = buyThresh;
    out.sellThreshold = sellThresh;'''
    }
    
    return fixes

def create_validation_script():
    """Create validation script for threshold symmetry"""
    
    validation_code = '''
// Threshold Symmetry Validator
void validateThresholdSymmetry() {
    StatisticalThresholdAdapter adapter;
    
    // Test multiple regimes and volatilities
    for (int regime = 0; regime < 5; regime++) {
        for (double vol = 0.01; vol <= 0.1; vol += 0.02) {
            auto thresholds = adapter.calculateThresholds(regime, vol);
            
            double buyMag = std::abs(thresholds.buyThreshold);
            double sellMag = std::abs(thresholds.sellThreshold);
            double asymmetry = std::abs(buyMag - sellMag) / std::max(buyMag, sellMag);
            
            if (asymmetry > 0.15) { // More than 15% asymmetry
                LOG_WARN("Threshold asymmetry detected: regime={} vol={:.3f} buy={:.3f} sell={:.3f} asymmetry={:.1f}%", 
                        regime, vol, thresholds.buyThreshold, thresholds.sellThreshold, asymmetry * 100);
            }
        }
    }
    
    LOG_INFO("Threshold symmetry validation completed");
}'''
    
    return validation_code

def main():
    print("🚨 BUY BIAS FIX GENERATOR")
    print("=" * 50)
    
    # Analyze current configuration
    config = analyze_current_bias()
    
    # Generate fixes
    fixes = generate_balanced_fixes()
    
    print("\n🔧 GENERATED FIXES:")
    print("\n1. Symmetric Default Thresholds:")
    print(fixes['threshold_defaults'])
    
    print("\n2. Balanced Percentiles:")
    print(fixes['percentiles'])
    
    print("\n3. Fixed Threshold Calculation:")
    print(fixes['threshold_calculation'])
    
    # Create validation code
    validation = create_validation_script()
    
    print("\n🧪 VALIDATION CODE:")
    print(validation)
    
    # Generate patch summary
    print("\n📋 PATCH SUMMARY:")
    print("✅ Fix 1: Symmetric default thresholds (0.45, -0.45)")
    print("✅ Fix 2: Balanced percentiles (65th, 35th)")
    print("✅ Fix 3: Magnitude enforcement for sell threshold")
    print("✅ Fix 4: Validation functions added")
    
    print("\n🎯 NEXT STEPS:")
    print("1. Apply fixes to imbalance_detection_engine.cpp")
    print("2. Add validation calls to initialization")
    print("3. Monitor label distribution after deployment")
    print("4. Retrain models with balanced labels")
    
    print("\n⚠️  EXPECTED IMPACT:")
    print("• More balanced BUY/SELL signal distribution")
    print("• Higher quality SELL signals")
    print("• Reduced model BUY bias")
    print("• Improved symmetric trading performance")

if __name__ == "__main__":
    main()