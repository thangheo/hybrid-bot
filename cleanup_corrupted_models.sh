#!/bin/bash

# Bayesian Model Cleanup Script
# This script backs up and removes corrupted Bayesian model files to allow fresh training

echo "🧹 Cleaning up corrupted Bayesian models..."

# Create backup directory with timestamp
BACKUP_DIR="/home/e8/works/hybrid-bot/models/bayesian_backup_$(date +%Y%m%d_%H%M%S)"
MODELS_DIR="/home/e8/works/hybrid-bot/models/bayesian"

if [ -d "$MODELS_DIR" ] && [ "$(ls -A $MODELS_DIR)" ]; then
    echo "📦 Creating backup at: $BACKUP_DIR"
    mkdir -p "$BACKUP_DIR"
    cp -r "$MODELS_DIR"/* "$BACKUP_DIR"/
    
    echo "🗑️  Removing corrupted model files..."
    rm -f "$MODELS_DIR"/*.json
    
    echo "✅ Cleanup completed!"
    echo "📁 Backup saved to: $BACKUP_DIR"
    echo "🆕 Models directory ready for fresh training"
else
    echo "⚠️  No models found in $MODELS_DIR"
fi

echo ""
echo "📊 Model directory status:"
ls -la "$MODELS_DIR" 2>/dev/null || echo "Directory empty or doesn't exist"