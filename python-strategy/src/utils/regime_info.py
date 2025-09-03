

import asyncio
import signal
import sys
import os
from datetime import datetime, timedelta
from pathlib import Path
import time

import logging
import yaml
from dotenv import load_dotenv
from apscheduler.schedulers.asyncio import AsyncIOScheduler
from apscheduler.triggers.interval import IntervalTrigger
import pandas as pd
import asyncpg
from typing import Dict, List, Optional, Tuple, Any 
from src.strategy.strategies.markov_strategy_complete import MarkovRegimeInfo , MarketRegime

def _extract_regime_info_for_risk_management(regime_info) -> dict:
    """Extract regime information for risk management from MarkovRegimeInfo"""
    
    # 1. REGIME CONFIDENCE = regime_probability (0-1)
    regime_confidence = getattr(regime_info, 'regime_probability', 0.5)
    
    # 2. REGIME STRENGTH = Composite calculation using multiple factors
    regime_strength = _calculate_regime_strength(regime_info)
    
    # 3. CURRENT REGIME = Direct extraction
    current_regime = getattr(regime_info, 'current_regime', 'balanced')
    
    # Handle enum conversion if needed
    if hasattr(current_regime, 'value'):
        current_regime = current_regime.value
    elif hasattr(current_regime, 'name'):
        current_regime = current_regime.name.lower()
    
    return {
        'current_regime': current_regime,
        'confidence': regime_confidence,
        'strength': regime_strength,
        'transition_confidence': getattr(regime_info, 'transition_confidence', 0.5),
        'volatility_regime': getattr(regime_info, 'volatility_regime', 'medium'),
        'etf_flow_regime': getattr(regime_info, 'etf_flow_regime', 'neutral'),
        'etf_flow_strength': getattr(regime_info, 'etf_flow_strength', 0.5),
        'regime_duration': getattr(regime_info, 'regime_duration', 1),
        'expected_duration': getattr(regime_info, 'expected_duration', 5)
    }

def _calculate_regime_strength(regime_info) -> float:
    """
    Calculate regime strength from multiple factors
    
    Regime strength indicates how pronounced/clear the regime signals are:
    - Higher strength = more confident in regime characteristics
    - Lower strength = regime signals are weak/mixed
    """
    
    factors = []
    
    # 1. ETF Flow Strength (if available) - direct indicator
    etf_flow_strength = getattr(regime_info, 'etf_flow_strength', None)
    if etf_flow_strength is not None:
        factors.append(etf_flow_strength)
    
    # 2. Regime Duration vs Expected Duration
    # If we've been in regime for a while, signals are stronger
    regime_duration = getattr(regime_info, 'regime_duration', 1)
    expected_duration = getattr(regime_info, 'expected_duration', 5)
    
    if expected_duration > 0:
        duration_factor = min(1.0, regime_duration / expected_duration)
        factors.append(duration_factor)
    
    # 3. Transition Confidence (inverse relationship)
    # High transition confidence = current regime is ending = lower strength
    transition_confidence = getattr(regime_info, 'transition_confidence', 0.5)
    stability_factor = 1.0 - transition_confidence  # Invert it
    factors.append(stability_factor)
    
    # 4. Volatility Regime Contribution
    volatility_regime = getattr(regime_info, 'volatility_regime', 'medium')
    if hasattr(volatility_regime, 'value'):
        volatility_regime = volatility_regime.value
    
    # Clear volatility regimes indicate stronger regime signals
    vol_strength_map = {
        'low': 0.8,      # Clear low vol = strong regime signal
        'high': 0.8,     # Clear high vol = strong regime signal  
        'medium': 0.5    # Medium vol = weaker regime signal
    }
    vol_factor = vol_strength_map.get(str(volatility_regime).lower(), 0.5)
    factors.append(vol_factor)
    
    # 5. ETF Flow Regime Clarity
    etf_flow_regime = getattr(regime_info, 'etf_flow_regime', 'neutral')
    if hasattr(etf_flow_regime, 'value'):
        etf_flow_regime = etf_flow_regime.value
    
    flow_strength_map = {
        'positive': 0.8,  # Clear positive flow = strong signal
        'negative': 0.8,  # Clear negative flow = strong signal
        'neutral': 0.3    # Neutral flow = weak signal
    }
    flow_factor = flow_strength_map.get(str(etf_flow_regime).lower(), 0.5)
    factors.append(flow_factor)
    
    # Calculate weighted average
    if factors:
        # Weight the factors (you can adjust these weights)
        weights = [0.3, 0.2, 0.2, 0.15, 0.15]  # Must sum to 1.0
        if len(factors) != len(weights):
            # If we have different number of factors, use simple average
            regime_strength = sum(factors) / len(factors)
        else:
            regime_strength = sum(f * w for f, w in zip(factors, weights))
    else:
        regime_strength = 0.5  # Default
    
    return max(0.0, min(1.0, regime_strength))  # Clamp to [0, 1]

