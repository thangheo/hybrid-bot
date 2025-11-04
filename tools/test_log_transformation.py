#!/usr/bin/env python3
"""
Test script to demonstrate log transformation effects on feature 2
"""

import numpy as np
import matplotlib.pyplot as plt
import json

def simulate_feature_2_data():
    """Simulate data similar to feature 2 with large values"""
    # Based on your JSON: mean=546.84, std=3591.09
    np.random.seed(42)
    
    # Generate skewed data similar to your feature 2
    # Using lognormal distribution to create skewed positive data
    base_data = np.random.lognormal(mean=5.5, sigma=1.5, size=1000)
    
    # Scale to match your statistics approximately  
    data = base_data * 100
    
    print(f"Simulated data stats:")
    print(f"  Mean: {np.mean(data):.2f}")
    print(f"  Std:  {np.std(data):.2f}")
    print(f"  Min:  {np.min(data):.2f}")  
    print(f"  Max:  {np.max(data):.2f}")
    
    return data

def apply_log_transform(data):
    """Apply log1p transformation"""
    return np.log1p(data)

def normalize_features(data):
    """Standard Z-score normalization with clipping"""
    mean_val = np.mean(data)
    std_val = np.std(data)
    
    if std_val < 1e-8:
        std_val = 1e-8
        
    normalized = (data - mean_val) / std_val
    
    # Clip to [-3, 3] sigma like in the C++ code
    normalized = np.clip(normalized, -3.0, 3.0)
    
    return normalized, mean_val, std_val

def compare_transformations():
    """Compare original vs log-transformed data"""
    
    # Generate test data
    original_data = simulate_feature_2_data()
    
    # Apply log transformation
    log_data = apply_log_transform(original_data)
    
    print(f"\nAfter log1p transformation:")
    print(f"  Mean: {np.mean(log_data):.2f}")
    print(f"  Std:  {np.std(log_data):.2f}")
    print(f"  Min:  {np.min(log_data):.2f}")
    print(f"  Max:  {np.max(log_data):.2f}")
    
    # Normalize both versions
    norm_original, orig_mean, orig_std = normalize_features(original_data)
    norm_log, log_mean, log_std = normalize_features(log_data)
    
    print(f"\nNormalization stats (original):")
    print(f"  Mean: {orig_mean:.2f}, Std: {orig_std:.2f}")
    print(f"  Normalized range: [{np.min(norm_original):.2f}, {np.max(norm_original):.2f}]")
    
    print(f"\nNormalization stats (log-transformed):")
    print(f"  Mean: {log_mean:.2f}, Std: {log_std:.2f}")  
    print(f"  Normalized range: [{np.min(norm_log):.2f}, {np.max(norm_log):.2f}]")
    
    # Check for numerical issues
    print(f"\nNumerical stability check:")
    print(f"  Original - NaN count: {np.sum(np.isnan(norm_original))}")
    print(f"  Original - Inf count: {np.sum(np.isinf(norm_original))}")
    print(f"  Log-transform - NaN count: {np.sum(np.isnan(norm_log))}")
    print(f"  Log-transform - Inf count: {np.sum(np.isinf(norm_log))}")
    
    return {
        'original_data': original_data,
        'log_data': log_data,
        'norm_original': norm_original,
        'norm_log': norm_log,
        'stats': {
            'original': {'mean': orig_mean, 'std': orig_std},
            'log_transform': {'mean': log_mean, 'std': log_std}
        }
    }

def generate_model_config():
    """Generate example model config with log transformation"""
    config = {
        "enable_log_transform": True,
        "log_transform_indices": [2],
        "log_transform_threshold": 0.0,
        "initial_sigma": 1.0,
        "beta": 0.081,
        "min_probability": 0.6,
        "notes": "Feature 2 log transformation to handle large values (mean=546.84, std=3591)"
    }
    
    with open('/tmp/bayesian_model_config.json', 'w') as f:
        json.dump(config, f, indent=2)
    
    print(f"\nGenerated config file: /tmp/bayesian_model_config.json")
    return config

if __name__ == "__main__":
    print("=== Feature 2 Log Transformation Analysis ===")
    
    results = compare_transformations()
    
    print(f"\n=== Recommendations ===")
    
    orig_std = results['stats']['original']['std']
    log_std = results['stats']['log_transform']['std']
    
    improvement = (orig_std - log_std) / orig_std * 100
    
    print(f"Standard deviation reduction: {improvement:.1f}%")
    print(f"Log transformation is {'RECOMMENDED' if improvement > 50 else 'OPTIONAL'}")
    
    # Generate config
    config = generate_model_config()
    
    print(f"\n=== Next Steps ===")
    print("1. Deploy updated BayesianTradingModel with log transformation")
    print("2. Monitor model performance metrics")  
    print("3. Check for numerical stability improvements")
    print("4. Consider applying to other large-valued features if needed")