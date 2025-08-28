
# ================================================================
# FILE 1: src/strategy/exit/__init__.py
# ================================================================

"""
Signal Decay Exit Strategy Module

This module provides sophisticated exit strategies based on signal decay,
inspired by biological half-life concepts.
"""

from .signal_decay_manager import (
    SignalDecayExitManager,
    SignalDecayConfig,
    PositionWithDecay,
    CONSERVATIVE_EXIT_CONFIG,
    AGGRESSIVE_EXIT_CONFIG,
    ADAPTIVE_EXIT_CONFIG
)

__all__ = [
    'SignalDecayExitManager',
    'SignalDecayConfig', 
    'PositionWithDecay',
    'CONSERVATIVE_EXIT_CONFIG',
    'AGGRESSIVE_EXIT_CONFIG',
    'ADAPTIVE_EXIT_CONFIG'
]