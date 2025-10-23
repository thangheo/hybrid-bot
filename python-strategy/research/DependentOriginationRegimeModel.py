"""
Robust Market Regime Detection via Dependent Origination
Production-ready version with numerical stability fixes
"""

import numpy as np
import warnings
from scipy.stats import multivariate_normal
from sklearn.preprocessing import StandardScaler

warnings.filterwarnings('ignore', category=RuntimeWarning)


class DependentOriginationRegimeModel:
    """
    Numerically stable regime detector based on dependent origination philosophy
    """
    
    def __init__(self, n_regimes=5, n_factors=5):
        self.n_regimes = n_regimes
        self.n_factors = n_factors
        
        self.regime_names = [
            'Uptrend', 'Downtrend', 'Chaos', 
            'Stagnation', 'Emptiness'
        ]
        
        # Regularization parameters
        self.reg_cov = 0.1  # covariance regularization
        self.min_prob = 1e-10  # minimum probability
        # Initialize with stable defaults
        self.regime_params = self._init_stable_params()
        self.transitions = np.ones((n_regimes, n_regimes)) / n_regimes
        self.scaler = StandardScaler()
        
        
    def _init_stable_params(self):
        """Initialize with numerically stable parameters"""
        params = {}
        
        for i in range(self.n_regimes):
            # Small random means
            mean = np.random.randn(self.n_factors) * 0.5
            
            # Well-conditioned covariance
            cov = np.eye(self.n_factors)
            noise = np.random.randn(self.n_factors, self.n_factors) * 0.05
            cov = cov + noise @ noise.T
            cov = (cov + cov.T) / 2  # ensure symmetry
            cov += np.eye(self.n_factors) * self.reg_cov  # regularize
            
            params[i] = {'mean': mean, 'cov': cov}
        
        return params
    
    def extract_factors(self, X_market):
        """
        Extract hidden factors with numerical stability
        
        X_market: (T, d) matrix where d >= 3
                  Column 0: returns
                  Column 1: volume
                  Column 2+: additional features (optional)
        """
        T = X_market.shape[0]
        F = np.zeros((T, self.n_factors))
        
        # Ensure we have at least returns
        returns = X_market[:, 0]
        volume = X_market[:, 1] if X_market.shape[1] > 1 else np.ones(T)
        
        # Factor 1: Liquidity (log volume)
        F[:, 0] = np.log(np.abs(volume) + 1)
        
        # Factor 2: Volatility (rolling std)
        window = min(20, T // 4)
        F[:, 1] = self._rolling_std(returns, window)
        
        # Factor 3: Momentum (rolling mean)
        F[:, 2] = self._rolling_mean(returns, window)
        
        # Factor 4: Trend (exponential moving average)
        F[:, 3] = self._ema(returns, alpha=0.1)
        
        # Factor 5: Autocorrelation
        F[:, 4] = self._rolling_autocorr(returns, window)
        
        # Replace any NaN or inf
        F = np.nan_to_num(F, nan=0.0, posinf=3.0, neginf=-3.0)
        
        # Standardize
        F = self.scaler.fit_transform(F)
        
        # Clip to reasonable range
        F = np.clip(F, -5, 5)
        
        return F
    
    def _rolling_std(self, x, window):
        """Compute rolling standard deviation"""
        result = np.zeros(len(x))
        for i in range(len(x)):
            start = max(0, i - window)
            result[i] = np.std(x[start:i+1]) if i > 0 else 0.01
        return result
    
    def _rolling_mean(self, x, window):
        """Compute rolling mean"""
        result = np.zeros(len(x))
        for i in range(len(x)):
            start = max(0, i - window)
            result[i] = np.mean(x[start:i+1])
        return result
    
    def _ema(self, x, alpha):
        """Exponential moving average"""
        ema = np.zeros(len(x))
        ema[0] = x[0]
        for i in range(1, len(x)):
            ema[i] = alpha * x[i] + (1 - alpha) * ema[i-1]
        return ema
    
    def _rolling_autocorr(self, x, window):
        """Rolling autocorrelation"""
        result = np.zeros(len(x))
        for i in range(window, len(x)):
            segment = x[i-window:i]
            if len(segment) > 1:
                result[i] = np.corrcoef(segment[:-1], segment[1:])[0, 1]
        return np.nan_to_num(result, nan=0.0)
    
    def _safe_mvn_pdf(self, x, mean, cov):
        """Numerically stable multivariate normal PDF"""
        try:
            # Ensure covariance is well-conditioned
            cov_reg = cov + np.eye(len(cov)) * self.reg_cov
            cov_reg = (cov_reg + cov_reg.T) / 2  # ensure symmetry
            
            # Check condition number
            if np.linalg.cond(cov_reg) > 1e10:
                # Fall back to diagonal covariance
                cov_reg = np.diag(np.diag(cov_reg))
            # cov_reg += np.eye(len(cov_reg)) * self.reg_cov  # further regularize
            pdf = multivariate_normal.pdf(x, mean=mean, cov=cov_reg,
                                          allow_singular=True)
            
            # Ensure non-zero
            return max(pdf, self.min_prob)
        
        except:
            # Fallback: simple Gaussian
            dist = np.sum((x - mean) ** 2)
            return max(np.exp(-dist / 2), self.min_prob)
    
    def compute_regime_probs(self, F_t):
        """Compute P(R_i | F_t) with numerical stability"""
        probs = np.zeros(self.n_regimes)
        
        for i in range(self.n_regimes):
            mean = self.regime_params[i]['mean']
            cov = self.regime_params[i]['cov']
            probs[i] = self._safe_mvn_pdf(F_t, mean, cov)
        
        # Normalize
        total = probs.sum()
        if total < self.min_prob:
            # Uniform distribution as fallback
            probs = np.ones(self.n_regimes) / self.n_regimes
        else:
            probs = probs / total
        
        return probs
    
    def fit(self, X_market, n_iterations=30):
        """
        Train model with EM algorithm (numerically stable version)
        """
        F = self.extract_factors(X_market)
        T = F.shape[0]
        
        print(f"Training on {T} time periods with {self.n_factors} factors...")
        
        # Initialize responsibilities with k-means-like clustering
        from sklearn.cluster import KMeans
        kmeans = KMeans(n_clusters=self.n_regimes, random_state=42, n_init=10)
        labels = kmeans.fit_predict(F)
        
        # Initialize parameters from clusters
        for i in range(self.n_regimes):
            mask = labels == i
            if mask.sum() > 0:
                self.regime_params[i]['mean'] = F[mask].mean(axis=0)
                cov = np.cov(F[mask].T)
                if np.isnan(cov).any() or np.isinf(cov).any():
                    cov = np.eye(self.n_factors)
                self.regime_params[i]['cov'] = cov + np.eye(self.n_factors) * self.reg_cov
        
        # EM iterations
        for iteration in range(n_iterations):
            # E-step: compute responsibilities
            gamma = np.zeros((T, self.n_regimes))
            
            for t in range(T):
                gamma[t] = self.compute_regime_probs(F[t])
            
            # M-step: update parameters
            for i in range(self.n_regimes):
                weights = gamma[:, i]
                weight_sum = weights.sum()
                
                if weight_sum < 1e-6:
                    continue
                
                # Update mean
                self.regime_params[i]['mean'] = np.average(F, axis=0, weights=weights)
                
                # Update covariance
                centered = F - self.regime_params[i]['mean']
                weighted_cov = np.zeros((self.n_factors, self.n_factors))
                
                for t in range(T):
                    weighted_cov += weights[t] * np.outer(centered[t], centered[t])
                
                cov = weighted_cov / weight_sum
                cov = (cov + cov.T) / 2  # symmetrize
                cov += np.eye(self.n_factors) * self.reg_cov  # regularize
                
                self.regime_params[i]['cov'] = cov
            
            # Update transitions
            for i in range(self.n_regimes):
                for j in range(self.n_regimes):
                    if T > 1:
                        numerator = np.sum(gamma[:-1, i] * gamma[1:, j])
                        denominator = np.sum(gamma[:-1, i])
                        if denominator > 1e-6:
                            self.transitions[i, j] = numerator / denominator
            
            # Normalize transitions
            row_sums = self.transitions.sum(axis=1, keepdims=True)
            self.transitions = self.transitions / np.maximum(row_sums, 1e-10)
            
            if (iteration + 1) % 10 == 0:
                print(f"  Iteration {iteration + 1}/{n_iterations}")
        
        print("Training complete!")
        return self
    
    def predict(self, X_market):
        """Predict regimes for new data"""
        F = self.extract_factors(X_market)
        T = F.shape[0]
        
        # Simple forward pass (no HMM complexity)
        regime_probs = np.zeros((T, self.n_regimes))
        
        for t in range(T):
            regime_probs[t] = self.compute_regime_probs(F[t])
        
        # Most likely regime
        most_likely = np.argmax(regime_probs, axis=1)
        
        # Compute entropy
        entropy = np.zeros(T)
        for t in range(T):
            p = regime_probs[t]
            p = p[p > 1e-10]
            entropy[t] = -np.sum(p * np.log(p))
        
        # Detect emptiness (high entropy)
        max_entropy = np.log(self.n_regimes)
        emptiness = entropy > (0.8 * max_entropy)
        
        return {
            'regime_probs': regime_probs,
            'most_likely': most_likely,
            'entropy': entropy,
            'emptiness_periods': emptiness,
            'factors': F
        }
    # save model to file
    def save(self, filepath):
        """Save model to file"""
        import pickle
        with open(filepath, 'wb') as f:
            pickle.dump(self, f)
        print(f"Model saved to: {filepath}")

    def visualize(self, results, save_path='./regime_analysis.png'):
        """Create visualization"""
        import matplotlib.pyplot as plt
        
        T = len(results['regime_probs'])
        time = np.arange(T)
        
        fig, axes = plt.subplots(3, 1, figsize=(14, 10))
        
        # Panel 1: Regime probabilities
        ax1 = axes[0]
        colors = ['#2ecc71', '#e74c3c', '#f39c12', '#3498db', '#95a5a6']
        
        for i in range(self.n_regimes):
            ax1.plot(time, results['regime_probs'][:, i], 
                    label=self.regime_names[i], 
                    color=colors[i], alpha=0.7, linewidth=2)
        
        ax1.set_ylabel('Probability', fontsize=11)
        ax1.set_title('Market Regime Probabilities (Arising and Ceasing)', fontsize=12, fontweight='bold')
        ax1.legend(loc='upper right', framealpha=0.9)
        ax1.grid(alpha=0.3)
        ax1.set_ylim([0, 1])
        
        # Panel 2: Entropy
        ax2 = axes[1]
        ax2.fill_between(time, results['entropy'], alpha=0.5, color='purple')
        ax2.axhline(y=np.log(self.n_regimes) * 0.8, 
                   color='red', linestyle='--', linewidth=2,
                   label='Emptiness threshold')
        ax2.set_ylabel('Entropy', fontsize=11)
        ax2.set_title('Regime Uncertainty (Emptiness Detector)', fontsize=12, fontweight='bold')
        ax2.legend()
        ax2.grid(alpha=0.3)
        
        # Panel 3: Dominant regime
        ax3 = axes[2]
        
        for i in range(self.n_regimes):
            mask = results['most_likely'] == i
            if mask.sum() > 0:
                ax3.scatter(time[mask], results['most_likely'][mask], 
                           c=colors[i], label=self.regime_names[i],
                           alpha=0.6, s=30, edgecolors='black', linewidth=0.5)
        
        ax3.set_yticks(range(self.n_regimes))
        ax3.set_yticklabels(self.regime_names)
        ax3.set_xlabel('Time', fontsize=11)
        ax3.set_ylabel('Regime', fontsize=11)
        ax3.set_title('Dominant Regime Over Time', fontsize=12, fontweight='bold')
        ax3.legend(loc='upper right', framealpha=0.9, ncol=5, fontsize=8)
        ax3.grid(alpha=0.3, axis='x')
        
        plt.tight_layout()
        plt.savefig(save_path, dpi=150, bbox_inches='tight')
        print(f"Visualization saved to: {save_path}")
        plt.close()

    # visualize_regime_waves
    def visualize_regime_waves(self, results):
        """
        Visualize arising and ceasing of regimes as probability waves
        """
        import matplotlib.pyplot as plt
        
        T = len(results['regime_probs'])
        time = np.arange(T)
        
        fig, axes = plt.subplots(3, 1, figsize=(14, 10))
        
        # Panel 1: Regime probability waves
        ax1 = axes[0]
        for i in range(self.n_regimes):
            ax1.plot(time, results['regime_probs'][:, i], 
                    label=self.regime_names[i], alpha=0.7, linewidth=2)
        ax1.set_ylabel('P(Regime)')
        ax1.set_title('Arising and Ceasing of Market Regimes (Probability Waves)')
        ax1.legend(loc='upper right')
        ax1.grid(alpha=0.3)
        
        # Panel 2: Entropy (emptiness indicator)
        ax2 = axes[1]
        ax2.fill_between(time, results['entropy'], alpha=0.5, color='purple')
        ax2.axhline(y=np.log(self.n_regimes)/2, color='red', 
                   linestyle='--', label='Emptiness threshold')
        ax2.set_ylabel('Entropy')
        ax2.set_title('Regime Uncertainty (High Entropy = Emptiness State)')
        ax2.legend()
        ax2.grid(alpha=0.3)
        
        # Panel 3: Dominant regime
        ax3 = axes[2]
        regime_colors = ['green', 'red', 'orange', 'blue', 'gray']
        for i in range(self.n_regimes):
            mask = results['most_likely'] == i
            ax3.scatter(time[mask], np.ones(mask.sum())*i, 
                       c=regime_colors[i], label=self.regime_names[i], 
                       alpha=0.6, s=20)
        
        # Highlight emptiness
        emptiness = results['emptiness_periods']
        ax3.scatter(time[emptiness], np.ones(emptiness.sum())*4, 
                   c='black', marker='x', s=50, label='Emptiness')
        
        ax3.set_yticks(range(self.n_regimes))
        ax3.set_yticklabels(self.regime_names)
        ax3.set_xlabel('Time')
        ax3.set_title('Dominant Regime Over Time')
        ax3.legend(loc='upper right')
        ax3.grid(alpha=0.3)
        
        plt.tight_layout()
        plt.savefig('./regime_waves.png', dpi=150)
        plt.close()
        
        return fig
def generate_trading_signals(results, confidence_threshold=0.6):
    """
    Generate actionable trading signals from regime detection
    
    Returns:
        signals: dict with position, confidence, risk_adjustment
    """
    regime_probs = results['regime_probs']
    most_likely = results['most_likely']
    entropy = results['entropy']
    emptiness = results['emptiness_periods']
    
    T = len(regime_probs)
    
    signals = {
        'position': np.zeros(T),  # 1=long, -1=short, 0=neutral
        'confidence': np.zeros(T),
        'risk_adjustment': np.ones(T),
        'regime_name': []
    }
    
    regime_names = ['Uptrend', 'Downtrend', 'Chaos', 'Stagnation', 'Emptiness']
    
    for t in range(T):
        regime = most_likely[t]
        max_prob = regime_probs[t, regime]
        
        signals['regime_name'].append(regime_names[regime])
        
        # Emptiness = stay out
        if emptiness[t]:
            signals['position'][t] = 0
            signals['confidence'][t] = 0
            signals['risk_adjustment'][t] = 0.0
            continue
        
        # High entropy = reduce risk
        max_entropy = np.log(5)
        if entropy[t] > 0.7 * max_entropy:
            signals['risk_adjustment'][t] = 0.5
        
        # Low confidence = reduce risk
        if max_prob < confidence_threshold:
            signals['risk_adjustment'][t] = 0.5
        
        # Generate position based on regime
        if regime == 0:  # Uptrend
            signals['position'][t] = 1
            signals['confidence'][t] = max_prob
        
        elif regime == 1:  # Downtrend
            signals['position'][t] = -1
            signals['confidence'][t] = max_prob
        
        elif regime == 2:  # Chaos
            signals['position'][t] = 0
            signals['confidence'][t] = max_prob
            signals['risk_adjustment'][t] *= 0.3  # very defensive
        
        elif regime == 3:  # Stagnation
            signals['position'][t] = 0  # or mean reversion
            signals['confidence'][t] = max_prob
            signals['risk_adjustment'][t] *= 0.7
    
    return signals


def save_results(results, signals, filepath='./regime_results.csv'):
    """Save results to CSV"""
    import pandas as pd
    
    df = pd.DataFrame({
        'uptrend_prob': results['regime_probs'][:, 0],
        'downtrend_prob': results['regime_probs'][:, 1],
        'chaos_prob': results['regime_probs'][:, 2],
        'stagnation_prob': results['regime_probs'][:, 3],
        'emptiness_prob': results['regime_probs'][:, 4],
        'regime': signals['regime_name'],
        'entropy': results['entropy'],
        'is_emptiness': results['emptiness_periods'],
        'signal': signals['position'],
        'confidence': signals['confidence'],
        'risk_adj': signals['risk_adjustment']
    })
    
    df.to_csv(filepath, index=False)
    print(f"Results saved to: {filepath}")


# ============================================================================
# MAIN WORKFLOW
# ============================================================================

def train_and_detect(X_market, confidence_threshold=0.6):
    """
    Complete workflow: train model and detect regimes
    
    Args:
        X_market: (T, d) array with [returns, volume, ...]
        confidence_threshold: minimum confidence for trading signals
    
    Returns:
        model, results, signals
    """
    
    print("="*70)
    print("REGIME DETECTION VIA DEPENDENT ORIGINATION")
    print("="*70)
    
    # Initialize
    model = DependentOriginationRegimeModel(n_regimes=5, n_factors=5)
    
    # Train
    print("\n[1/4] Training model...")
    model.fit(X_market, n_iterations=30)
    
    # Predict
    print("\n[2/4] Detecting regimes...")
    results = model.predict(X_market)
    
    # Generate signals
    print("\n[3/4] Generating trading signals...")
    signals = generate_trading_signals(results, confidence_threshold)
    
    # Visualize
    print("\n[4/4] Creating visualization...")
    model.visualize(results)
    
    # Save
    save_results(results, signals)
    
    # Print summary
    print("\n" + "="*70)
    print("SUMMARY")
    print("="*70)
    
    for i, name in enumerate(model.regime_names):
        pct = (results['most_likely'] == i).mean() * 100
        avg_prob = results['regime_probs'][:, i].mean()
        print(f"{name:12s}: {pct:5.1f}% of time  (avg prob: {avg_prob:.3f})")
    
    print("\n" + "-"*70)
    
    long_pct = (signals['position'] == 1).mean() * 100
    short_pct = (signals['position'] == -1).mean() * 100
    neutral_pct = (signals['position'] == 0).mean() * 100
    avg_conf = signals['confidence'][signals['confidence'] > 0].mean()
    
    print(f"Long signals:     {long_pct:5.1f}%")
    print(f"Short signals:    {short_pct:5.1f}%")
    print(f"Neutral/wait:     {neutral_pct:5.1f}%")
    print(f"Avg confidence:   {avg_conf:.3f}")
    print(f"Emptiness periods: {results['emptiness_periods'].mean()*100:.1f}%")
    
    print("\n" + "="*70)
    print("✓ Complete! Check outputs:")
    print("  - regime_analysis.png (visualization)")
    print("  - regime_results.csv (detailed data)")
    print("="*70)
    
    return model, results, signals


if __name__ == "__main__":
    # Example with synthetic data
    np.random.seed(42)
    T = 500
    
    # Simulate market with regime changes
    returns = np.zeros(T)
    volume = np.zeros(T)
    
    # Uptrend (0-150)
    returns[0:150] = np.random.normal(0.001, 0.01, 150)
    volume[0:150] = np.random.gamma(3, 1000, 150)
    
    # Chaos (150-250)
    returns[150:250] = np.random.normal(0, 0.03, 100)
    volume[150:250] = np.random.gamma(5, 800, 100)
    
    # Downtrend (250-400)
    returns[250:400] = np.random.normal(-0.001, 0.015, 150)
    volume[250:400] = np.random.gamma(2, 1200, 150)
    
    # Stagnation (400-500)
    returns[400:500] = np.random.normal(0, 0.005, 100)
    volume[400:500] = np.random.gamma(2, 500, 100)
    
    X_market = np.column_stack([returns, volume])
    
    # Run detection
    model, results, signals = train_and_detect(X_market, confidence_threshold=0.6)