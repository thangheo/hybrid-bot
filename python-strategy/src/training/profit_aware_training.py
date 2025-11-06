"""
Profit-aware training utilities for market prediction models
Chỉ tạo BUY/SELL labels khi expected profit > trading fees (0.1%)
"""

import numpy as np
import pandas as pd
from typing import Dict, List, Tuple, Optional
from sklearn.preprocessing import StandardScaler
from sklearn.ensemble import RandomForestClassifier
from sklearn.linear_model import LogisticRegression
import warnings

warnings.filterwarnings('ignore')

class ProfitAwareTraining:
    """
    Training system that only labels signals when expected profit > trading costs
    """
    
    def __init__(self, 
                 trading_fee=0.001,  # 0.1% trading fee
                 min_expected_profit=0.002,  # 0.2% minimum expected profit  
                 confidence_threshold=0.65,
                 lookforward_periods=5):
        
        self.trading_fee = trading_fee
        self.min_expected_profit = min_expected_profit
        self.confidence_threshold = confidence_threshold
        self.lookforward_periods = lookforward_periods
        
        self.historical_outcomes = []  # Store (features, signal_strength, realized_profit)
        self.profit_model = None  # Model to predict expected profit
        self.scaler = StandardScaler()
        
    def calculate_future_returns(self, prices: np.ndarray, periods: int = 5) -> np.ndarray:
        """
        Calculate forward-looking returns over multiple periods
        """
        returns = []
        for i in range(len(prices) - periods):
            # Max return over next 'periods' periods
            future_prices = prices[i+1:i+1+periods]
            if len(future_prices) > 0:
                max_gain = np.max(future_prices) / prices[i] - 1
                max_loss = np.min(future_prices) / prices[i] - 1
                # Use max absolute return (best case scenario)
                returns.append(max_gain if abs(max_gain) > abs(max_loss) else max_loss)
            else:
                returns.append(0.0)
                
        # Pad with zeros for remaining periods
        returns.extend([0.0] * periods)
        return np.array(returns)
    
    def estimate_profit_probability(self, 
                                  features: np.ndarray, 
                                  signal_strength: float,
                                  regime: int = 0) -> Tuple[float, float]:
        """
        Estimate expected profit and probability of profit > threshold
        Based on historical similar signals
        """
        if len(self.historical_outcomes) < 50:
            # Not enough data, use simple heuristic
            expected_profit = abs(signal_strength) * 0.5 - self.trading_fee
            prob_profit = 0.6 if abs(signal_strength) > 1.0 else 0.4
            return expected_profit, prob_profit
            
        # Find similar historical signals
        similar_signals = []
        tolerance = 0.3  # 30% tolerance for similarity
        
        for hist_features, hist_strength, hist_profit in self.historical_outcomes[-200:]:  # Last 200 signals
            # Feature similarity (cosine similarity)
            if len(hist_features) == len(features):
                feature_sim = np.dot(features, hist_features) / (
                    np.linalg.norm(features) * np.linalg.norm(hist_features) + 1e-8)
                strength_sim = 1 - abs(signal_strength - hist_strength) / (abs(signal_strength) + abs(hist_strength) + 1e-8)
                
                if feature_sim > 0.7 and strength_sim > (1 - tolerance):
                    similar_signals.append(hist_profit)
        
        if len(similar_signals) < 5:
            # Fallback to regime-based estimation
            regime_signals = [profit for _, _, profit in self.historical_outcomes[-100:]]
            if len(regime_signals) > 10:
                similar_signals = regime_signals
            else:
                # Ultimate fallback
                expected_profit = abs(signal_strength) * 0.3 - self.trading_fee
                prob_profit = 0.55
                return expected_profit, prob_profit
        
        # Calculate statistics from similar signals
        similar_signals = np.array(similar_signals)
        expected_profit = np.mean(similar_signals)
        prob_profit = np.mean(similar_signals > self.trading_fee)
        
        return expected_profit, prob_profit
    
    def generate_profit_aware_labels(self, 
                                   features: np.ndarray,
                                   prices: np.ndarray, 
                                   signal_strengths: np.ndarray,
                                   regimes: Optional[np.ndarray] = None) -> Tuple[np.ndarray, np.ndarray]:
        """
        Generate labels only when expected profit > minimum threshold
        
        Returns:
            labels: 0=SELL, 1=NEUTRAL, 2=BUY
            confidences: confidence scores for each label
        """
        n_samples = len(features)
        labels = np.ones(n_samples)  # Default to NEUTRAL
        confidences = np.zeros(n_samples)
        
        # Calculate actual future returns for training
        future_returns = self.calculate_future_returns(prices, self.lookforward_periods)
        
        if regimes is None:
            regimes = np.zeros(n_samples)
            
        for i in range(n_samples - self.lookforward_periods):
            # Estimate expected profit for this signal
            expected_profit, prob_profit = self.estimate_profit_probability(
                features[i], signal_strengths[i], int(regimes[i])
            )
            
            # Calculate realized profit for updating historical data
            realized_return = future_returns[i]
            realized_profit = abs(realized_return) - self.trading_fee
            
            # Update historical outcomes
            self.historical_outcomes.append((
                features[i].copy(), signal_strengths[i], realized_profit
            ))
            
            # Only label if expected profit exceeds threshold AND confidence is high
            min_profit_threshold = self.min_expected_profit
            
            if (expected_profit > min_profit_threshold and 
                prob_profit > self.confidence_threshold):
                
                if signal_strengths[i] > 0:
                    labels[i] = 2  # BUY
                elif signal_strengths[i] < 0:
                    labels[i] = 0  # SELL
                    
                confidences[i] = prob_profit
            else:
                labels[i] = 1  # NEUTRAL
                confidences[i] = 0.5
                
        # Keep only recent historical outcomes to prevent memory bloat
        if len(self.historical_outcomes) > 1000:
            self.historical_outcomes = self.historical_outcomes[-800:]
            
        return labels, confidences
    
    def train_profit_model(self, features: np.ndarray, profits: np.ndarray):
        """
        Train a model to directly predict expected profit
        """
        # Remove outliers
        profit_std = np.std(profits)
        mask = np.abs(profits) < 3 * profit_std
        
        X_clean = features[mask]
        y_clean = profits[mask]
        
        if len(X_clean) < 50:
            print("Warning: Not enough clean data to train profit model")
            return
            
        # Scale features
        X_scaled = self.scaler.fit_transform(X_clean)
        
        # Train ensemble model
        self.profit_model = RandomForestClassifier(
            n_estimators=100,
            max_depth=10,
            min_samples_split=20,
            random_state=42
        )
        
        # Convert to classification: profitable vs not profitable
        y_binary = (y_clean > self.trading_fee).astype(int)
        
        self.profit_model.fit(X_scaled, y_binary)
        
        # Print training metrics
        train_accuracy = self.profit_model.score(X_scaled, y_binary)
        print(f"Profit model training accuracy: {train_accuracy:.3f}")
        
    def predict_profitability(self, features: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """
        Predict probability of profitability using trained model
        """
        if self.profit_model is None:
            # Fallback to simple heuristic
            return np.full(len(features), 0.5), np.full(len(features), 0.1)
            
        X_scaled = self.scaler.transform(features)
        prob_profitable = self.profit_model.predict_proba(X_scaled)[:, 1]
        
        # Also get feature importance for expected profit magnitude
        feature_importance = np.sum(np.abs(features), axis=1)
        feature_importance = feature_importance / (np.max(feature_importance) + 1e-8)
        expected_profit = prob_profitable * feature_importance * 0.005  # Scale to reasonable profit range
        
        return prob_profitable, expected_profit
    
    def get_statistics(self) -> Dict:
        """
        Get training statistics
        """
        if len(self.historical_outcomes) == 0:
            return {"total_signals": 0}
            
        profits = [outcome[2] for outcome in self.historical_outcomes]
        
        stats = {
            "total_signals": len(profits),
            "profitable_signals": sum(1 for p in profits if p > 0),
            "win_rate": sum(1 for p in profits if p > 0) / len(profits),
            "average_profit": np.mean(profits),
            "average_win": np.mean([p for p in profits if p > 0]) if any(p > 0 for p in profits) else 0,
            "average_loss": np.mean([p for p in profits if p < 0]) if any(p < 0 for p in profits) else 0,
            "profit_factor": (sum(p for p in profits if p > 0) / 
                            abs(sum(p for p in profits if p < 0))) if any(p < 0 for p in profits) else float('inf')
        }
        
        return stats
    
    def save_model(self, filepath: str):
        """Save the profit model"""
        import joblib
        if self.profit_model is not None:
            joblib.dump({
                'model': self.profit_model,
                'scaler': self.scaler,
                'historical_outcomes': self.historical_outcomes[-500:],  # Save last 500 outcomes
                'config': {
                    'trading_fee': self.trading_fee,
                    'min_expected_profit': self.min_expected_profit,
                    'confidence_threshold': self.confidence_threshold
                }
            }, filepath)
            print(f"Profit-aware model saved to {filepath}")
    
    def load_model(self, filepath: str):
        """Load the profit model"""
        import joblib
        try:
            data = joblib.load(filepath)
            self.profit_model = data['model']
            self.scaler = data['scaler']
            self.historical_outcomes = data['historical_outcomes']
            config = data['config']
            
            self.trading_fee = config['trading_fee']
            self.min_expected_profit = config['min_expected_profit'] 
            self.confidence_threshold = config['confidence_threshold']
            
            print(f"Profit-aware model loaded from {filepath}")
            print(f"Loaded {len(self.historical_outcomes)} historical outcomes")
        except Exception as e:
            print(f"Error loading model: {e}")


def apply_profit_aware_training_to_regime_model(X_market: np.ndarray, 
                                              regime_results: Dict,
                                              prices: np.ndarray,
                                              min_profit_pct: float = 0.002) -> Dict:
    """
    Apply profit-aware training to existing regime detection results
    
    Args:
        X_market: Market features [returns, volume, ...]
        regime_results: Results from regime detection model
        prices: Price series for calculating future returns
        min_profit_pct: Minimum profit percentage threshold
    
    Returns:
        Enhanced signals with profit-aware labeling
    """
    print("Applying profit-aware training...")
    
    # Initialize profit-aware trainer
    trainer = ProfitAwareTraining(
        trading_fee=0.001,
        min_expected_profit=min_profit_pct,
        confidence_threshold=0.65,
        lookforward_periods=5
    )
    
    # Extract features and create signal strengths from regime probabilities
    features = X_market[:, :5]  # Use first 5 features
    regime_probs = regime_results['regime_probs']
    
    # Create signal strength from regime transition probabilities
    signal_strengths = np.zeros(len(regime_probs))
    for i in range(len(signal_strengths)):
        # Uptrend regime (0) -> positive signal
        # Downtrend regime (1) -> negative signal  
        # Other regimes -> neutral
        signal_strengths[i] = (regime_probs[i, 0] - regime_probs[i, 1]) * 2  # Scale to [-2, 2]
    
    # Generate profit-aware labels
    labels, confidences = trainer.generate_profit_aware_labels(
        features, prices, signal_strengths, regime_results['most_likely']
    )
    
    # Train profit prediction model
    if len(trainer.historical_outcomes) > 50:
        outcomes_features = np.array([outcome[0] for outcome in trainer.historical_outcomes])
        outcomes_profits = np.array([outcome[2] for outcome in trainer.historical_outcomes])
        trainer.train_profit_model(outcomes_features, outcomes_profits)
    
    # Create enhanced signals
    enhanced_signals = {
        'position': labels - 1,  # Convert to [-1, 0, 1] format
        'confidence': confidences,
        'expected_profit': np.zeros(len(labels)),
        'profit_probability': np.zeros(len(labels)),
        'regime_name': [f"Regime_{r}" for r in regime_results['most_likely']],
        'signal_strength': signal_strengths,
        'risk_adjustment': np.ones(len(labels))
    }
    
    # Add profit predictions if model is trained
    if trainer.profit_model is not None:
        prob_profitable, expected_profits = trainer.predict_profitability(features)
        enhanced_signals['profit_probability'] = prob_profitable
        enhanced_signals['expected_profit'] = expected_profits
    
    # Print statistics
    stats = trainer.get_statistics()
    print("\nProfit-Aware Training Statistics:")
    print(f"Total signals analyzed: {stats['total_signals']}")
    if stats['total_signals'] > 0:
        print(f"Win rate: {stats['win_rate']:.1%}")
        print(f"Average profit per trade: {stats['average_profit']:.4f} ({stats['average_profit']*100:.2f}%)")
        print(f"Profit factor: {stats['profit_factor']:.2f}")
        
        # Count final labels
        buy_signals = np.sum(labels == 2)
        sell_signals = np.sum(labels == 0)  
        neutral_signals = np.sum(labels == 1)
        
        print(f"\nFinal Signal Distribution:")
        print(f"BUY signals: {buy_signals} ({buy_signals/len(labels)*100:.1f}%)")
        print(f"SELL signals: {sell_signals} ({sell_signals/len(labels)*100:.1f}%)")
        print(f"NEUTRAL signals: {neutral_signals} ({neutral_signals/len(labels)*100:.1f}%)")
    
    return enhanced_signals, trainer


# Example usage and integration with existing training
if __name__ == "__main__":
    # Test với synthetic data
    np.random.seed(42)
    T = 1000
    
    # Simulate price series with trend
    prices = np.cumsum(np.random.randn(T) * 0.01) + 100
    
    # Create features
    features = np.random.randn(T, 5)
    
    # Create mock regime results
    regime_results = {
        'regime_probs': np.random.dirichlet([1, 1, 1, 1, 1], T),
        'most_likely': np.random.randint(0, 5, T),
        'entropy': np.random.rand(T),
        'emptiness_periods': np.random.rand(T) > 0.8
    }
    
    X_market = np.column_stack([
        np.diff(np.log(prices), prepend=np.log(prices[0])),  # returns
        np.random.lognormal(0, 1, T),  # volume
        features
    ])
    
    # Apply profit-aware training
    enhanced_signals, trainer = apply_profit_aware_training_to_regime_model(
        X_market, regime_results, prices, min_profit_pct=0.002
    )
    
    print("Enhanced signals generated successfully!")
    print(f"Signal confidence range: {enhanced_signals['confidence'].min():.3f} - {enhanced_signals['confidence'].max():.3f}")