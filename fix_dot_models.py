#!/usr/bin/env python3
"""
Emergency DOT Model Fixes
Fix overfitted Regime 1 model that's causing -$26.44 losses
"""

import json
import os
import shutil
from datetime import datetime

def backup_and_fix_dot_models():
    """Backup and fix problematic DOT models"""
    
    models_dir = "/home/e8/works/hybrid-bot/models/bayesian"
    backup_dir = f"/home/e8/works/hybrid-bot/models/backup_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    
    # Create backup
    os.makedirs(backup_dir, exist_ok=True)
    
    dot_files = [
        "BINANCE_DOT_USDT_regime0.json",
        "BINANCE_DOT_USDT_regime1.json"
    ]
    
    print("🚨 FIXING DOT MODELS - EMERGENCY PATCH")
    print("="*50)
    
    for filename in dot_files:
        filepath = os.path.join(models_dir, filename)
        if not os.path.exists(filepath):
            continue
            
        # Backup original
        backup_path = os.path.join(backup_dir, filename)
        shutil.copy2(filepath, backup_path)
        print(f"✅ Backed up: {filename}")
        
        # Load model
        with open(filepath, 'r') as f:
            model = json.load(f)
        
        original_updates = model.get('num_updates', 0)
        
        # Fix Regime 1 overfitting
        if 'regime1' in filename:
            print(f"🔧 Fixing REGIME 1 (Bear) - Original updates: {original_updates}")
            
            # Reduce extreme weights by 60%
            if 'mu' in model:
                for i in range(len(model['mu'])):
                    for j in range(len(model['mu'][i])):
                        if abs(model['mu'][i][j]) > 0.3:  # Cap extreme weights
                            model['mu'][i][j] *= 0.4  # Reduce by 60%
            
            # Increase forgetting factor (faster adaptation)
            model['config']['forgetting_factor'] = 0.85  # From 0.95
            
            # Increase min probability threshold
            model['config']['min_probability'] = 0.75  # From 0.65
            
            # Reset update count to prevent further overfitting
            model['num_updates'] = min(500, original_updates)  # Cap at 500
            
            print(f"   ✅ Capped weights > 0.3 to 40% of original")
            print(f"   ✅ Forgetting factor: 0.95 → 0.85")
            print(f"   ✅ Min probability: 0.65 → 0.75")
            print(f"   ✅ Updates: {original_updates} → {model['num_updates']}")
        
        elif 'regime0' in filename:
            print(f"🔧 Fine-tuning REGIME 0 (Bull) - Updates: {original_updates}")
            
            # Slightly increase forgetting for better adaptation
            model['config']['forgetting_factor'] = 0.90  # From 0.95
            
            # Keep reasonable threshold
            model['config']['min_probability'] = 0.70  # From 0.65
            
            print(f"   ✅ Forgetting factor: 0.95 → 0.90")  
            print(f"   ✅ Min probability: 0.65 → 0.70")
        
        # Save fixed model
        with open(filepath, 'w') as f:
            json.dump(model, f, indent="\t")
        
        print(f"💾 Saved fixed model: {filename}")
    
    print("="*50)
    print(f"🎯 EMERGENCY DOT FIXES COMPLETE")
    print(f"📁 Backups saved to: {backup_dir}")
    print(f"🚨 Regime 1 (Bear) model de-overfitted")
    print(f"✅ Both models now have better adaptation rates")
    print("="*50)

if __name__ == "__main__":
    backup_and_fix_dot_models()