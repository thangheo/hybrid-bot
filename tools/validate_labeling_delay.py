#!/usr/bin/env python3
"""
Validation script để kiểm tra tính phù hợp của labelingDelayMs: 30000
"""

import json
import sys
from typing import Dict, List, Tuple

def load_config(file_path: str) -> Dict:
    """Load strategy configuration"""
    with open(file_path, 'r') as f:
        return json.load(f)

def analyze_labeling_delay(config: Dict) -> Dict:
    """Phân tích tính phù hợp của labelingDelayMs"""
    
    # Extract key timing parameters
    imbalance_params = config['components']['imbalanceDetector']['parameters']
    momentum_params = config['components']['momentumCorrelator']['parameters'] 
    signal_params = config['components']['signalGenerator']['parameters']
    
    labeling_delay = imbalance_params.get('labelingDelayMs', 0)
    detection_window = imbalance_params.get('detectionWindowMs', 0)
    prediction_horizon = momentum_params.get('predictionHorizonMs', 0)
    signal_ttl = signal_params.get('signalTtlMs', 0)
    correlation_window = momentum_params.get('correlationWindowMs', 0)
    
    # Time windows for analysis
    time_windows = config['parameters']['array'].get('time_windows', [])
    
    analysis = {
        'labeling_delay_ms': labeling_delay,
        'timing_alignment': {},
        'consistency_scores': {},
        'recommendations': [],
        'warnings': [],
        'overall_score': 0
    }
    
    # 1. Check timing alignment
    analysis['timing_alignment'] = {
        'detection_window': detection_window,
        'prediction_horizon': prediction_horizon, 
        'signal_ttl': signal_ttl,
        'correlation_window': correlation_window,
        'time_windows': time_windows
    }
    
    # 2. Consistency scoring
    scores = []
    
    # Perfect alignment with prediction horizon
    if labeling_delay == prediction_horizon:
        scores.append(('prediction_horizon_match', 10, "Perfect alignment với prediction horizon"))
        analysis['consistency_scores']['prediction_horizon'] = 10
    else:
        diff_pct = abs(labeling_delay - prediction_horizon) / max(labeling_delay, prediction_horizon) * 100
        score = max(0, 10 - diff_pct/10)
        scores.append(('prediction_horizon_mismatch', score, f"Mismatch với prediction horizon: {diff_pct:.1f}%"))
        analysis['consistency_scores']['prediction_horizon'] = score
    
    # Detection window alignment  
    if labeling_delay == detection_window:
        scores.append(('detection_window_match', 10, "Perfect alignment với detection window"))
        analysis['consistency_scores']['detection_window'] = 10
    else:
        diff_pct = abs(labeling_delay - detection_window) / max(labeling_delay, detection_window) * 100
        score = max(0, 10 - diff_pct/10)
        scores.append(('detection_window_mismatch', score, f"Mismatch với detection window: {diff_pct:.1f}%"))
        analysis['consistency_scores']['detection_window'] = score
    
    # Signal TTL alignment
    if labeling_delay == signal_ttl:
        scores.append(('signal_ttl_match', 8, "Good alignment với signal TTL"))
        analysis['consistency_scores']['signal_ttl'] = 8
    else:
        diff_pct = abs(labeling_delay - signal_ttl) / max(labeling_delay, signal_ttl) * 100
        score = max(0, 8 - diff_pct/12)
        scores.append(('signal_ttl_mismatch', score, f"Mismatch với signal TTL: {diff_pct:.1f}%"))
        analysis['consistency_scores']['signal_ttl'] = score
        
    # Time windows inclusion
    if labeling_delay in time_windows:
        scores.append(('time_windows_included', 5, "Labeling delay có trong time_windows"))
        analysis['consistency_scores']['time_windows'] = 5
    else:
        closest = min(time_windows, key=lambda x: abs(x - labeling_delay))
        diff_pct = abs(labeling_delay - closest) / labeling_delay * 100
        score = max(0, 5 - diff_pct/20)
        scores.append(('time_windows_missing', score, f"Không có trong time_windows, gần nhất: {closest}ms"))
        analysis['consistency_scores']['time_windows'] = score
    
    # 3. Market suitability analysis
    trading_pairs = config.get('tradingPairs', [])
    pair_categories = {}
    for pair in trading_pairs:
        category = pair.get('category', 'unknown')
        if category not in pair_categories:
            pair_categories[category] = []
        pair_categories[category].append(pair['symbol'])
    
    # Category-based recommendations
    volatile_categories = ['meme_utility', 'meme_ecosystem', 'emerging_l1']
    stable_categories = ['blue_chip', 'defi_infrastructure', 'rwa']
    
    volatile_pairs = []
    stable_pairs = []
    
    for category, pairs in pair_categories.items():
        if category in volatile_categories:
            volatile_pairs.extend(pairs)
        elif category in stable_categories:
            stable_pairs.extend(pairs)
    
    # 4. Generate recommendations
    if len(stable_pairs) > len(volatile_pairs):
        analysis['recommendations'].append(f"✅ 30s phù hợp: {len(stable_pairs)} stable pairs vs {len(volatile_pairs)} volatile pairs")
        scores.append(('pair_suitability', 7, "Majority stable pairs"))
    else:
        analysis['warnings'].append(f"⚠️ Cân nhắc giảm delay: {len(volatile_pairs)} volatile pairs vs {len(stable_pairs)} stable pairs")
        scores.append(('pair_suitability', 4, "Majority volatile pairs"))
    
    # 5. Model training considerations
    bayes_min_updates = imbalance_params.get('bayesMinUpdatesToTrade', 5000)
    require_ready = imbalance_params.get('requireModelReadyUpdates', 100)
    
    if labeling_delay >= 30000:
        if bayes_min_updates >= 3000:
            analysis['warnings'].append("⚠️ 30s delay + high min updates có thể làm chậm initial trading")
        else:
            analysis['recommendations'].append("✅ Min updates reasonable cho 30s delay")
    
    # 6. Calculate overall score
    total_score = sum(score for _, score, _ in scores)
    max_possible = 10 + 10 + 8 + 5 + 7  # max scores for each component
    analysis['overall_score'] = (total_score / max_possible) * 100
    
    # 7. Final recommendations based on score
    if analysis['overall_score'] >= 80:
        analysis['recommendations'].append("🎯 EXCELLENT: labelingDelayMs: 30000 rất phù hợp")
    elif analysis['overall_score'] >= 70:
        analysis['recommendations'].append("✅ GOOD: labelingDelayMs: 30000 phù hợp, có thể optimize nhẹ")  
    elif analysis['overall_score'] >= 60:
        analysis['recommendations'].append("⚠️ FAIR: Cân nhắc điều chỉnh một số thông số")
    else:
        analysis['recommendations'].append("❌ POOR: Nên xem xét thay đổi labelingDelayMs")
    
    return analysis

def print_analysis_report(analysis: Dict):
    """Print detailed analysis report"""
    
    print("=" * 80)
    print(f"📊 PHÂN TÍCH LABELING DELAY: {analysis['labeling_delay_ms']}ms")
    print("=" * 80)
    
    print(f"\n🎯 OVERALL SCORE: {analysis['overall_score']:.1f}/100")
    
    print(f"\n⏱️  TIMING ALIGNMENT:")
    timing = analysis['timing_alignment']
    print(f"  • Detection Window:    {timing['detection_window']}ms")
    print(f"  • Prediction Horizon:  {timing['prediction_horizon']}ms") 
    print(f"  • Signal TTL:          {timing['signal_ttl']}ms")
    print(f"  • Correlation Window:  {timing['correlation_window']}ms")
    print(f"  • Time Windows:        {timing['time_windows']}")
    
    print(f"\n📈 CONSISTENCY SCORES:")
    for metric, score in analysis['consistency_scores'].items():
        print(f"  • {metric:20} {score:4.1f}/10")
    
    if analysis['recommendations']:
        print(f"\n✅ RECOMMENDATIONS:")
        for rec in analysis['recommendations']:
            print(f"  {rec}")
    
    if analysis['warnings']:
        print(f"\n⚠️  WARNINGS:")
        for warning in analysis['warnings']:
            print(f"  {warning}")

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 validate_labeling_delay.py <config_file>")
        sys.exit(1)
    
    config_file = sys.argv[1]
    
    try:
        config = load_config(config_file)
        analysis = analyze_labeling_delay(config)
        print_analysis_report(analysis)
        
        # Return appropriate exit code
        if analysis['overall_score'] >= 70:
            sys.exit(0)  # Success
        elif analysis['overall_score'] >= 50: 
            sys.exit(1)  # Warning
        else:
            sys.exit(2)  # Error
            
    except Exception as e:
        print(f"❌ Error analyzing config: {e}")
        sys.exit(3)

if __name__ == "__main__":
    main()