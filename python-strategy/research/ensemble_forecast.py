"""
Ensemble Probabilistic Forecasting System for Trading
Combines multiple forecasts with different timeframes and volatilities
Supports Bayesian updates and advanced probability distributions
"""

import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
from scipy.stats import norm, t as student_t, laplace, skewnorm
from dataclasses import dataclass
from typing import List, Callable, Tuple
import seaborn as sns

@dataclass
class Forecast:
    """Represents a single forecast/prediction box"""
    T: float          # Timeframe horizon
    v: float          # Volatility (can be positive or negative for direction)
    p: float          # Probability/confidence
    mu: float = 0.0   # Mean expected change
    dist_type: str = 'normal'  # Distribution type: 'normal', 'student_t', 'laplace', 'skew_normal'
    df: float = 5.0   # Degrees of freedom for Student-t
    alpha: float = 0.0  # Skewness parameter for skew-normal
    t0: float = 0.0   # Creation time (for time decay)
    
    def get_distribution(self) -> stats.rv_continuous:
        """Get the appropriate scipy distribution"""
        sigma = abs(self.v)
        
        if self.dist_type == 'student_t':
            return student_t(df=self.df, loc=self.mu, scale=sigma)
        elif self.dist_type == 'laplace':
            return laplace(loc=self.mu, scale=sigma)
        elif self.dist_type == 'skew_normal':
            return skewnorm(a=self.alpha, loc=self.mu, scale=sigma)
        else:  # default: normal
            return norm(loc=self.mu, scale=sigma)
    
    def pdf(self, v: float) -> float:
        """Probability density at value v"""
        return self.get_distribution().pdf(v)
    
    def is_active(self, t: float) -> bool:
        """Check if forecast is still valid at time t"""
        return t <= self.T


class EnsembleForecastSystem:
    """Main system for combining multiple forecasts"""
    
    def __init__(self, decay_rate: float = 0.1):
        """
        Args:
            decay_rate: Time decay parameter (alpha) for reducing weight of older forecasts
        """
        self.forecasts: List[Forecast] = []
        self.decay_rate = decay_rate
        self.history = []  # For tracking updates
        
    def add_forecast(self, forecast: Forecast):
        """Add a new forecast to the ensemble"""
        self.forecasts.append(forecast)
        
    def get_active_forecasts(self, t: float) -> List[Forecast]:
        """Get forecasts that are still valid at time t"""
        return [f for f in self.forecasts if f.is_active(t)]
    
    def compute_weights(self, t: float, method: str = 'time_decay') -> np.ndarray:
        """
        Compute weights for active forecasts at time t
        
        Args:
            t: Current time
            method: 'equal', 'probability', 'time_decay', 'performance'
        """
        active = self.get_active_forecasts(t)
        if not active:
            return np.array([])
        
        if method == 'equal':
            weights = np.ones(len(active))
        elif method == 'probability':
            weights = np.array([f.p for f in active])
        elif method == 'time_decay':
            weights = np.array([
                f.p * np.exp(-self.decay_rate * (t - f.t0)) 
                for f in active
            ])
        else:  # default to probability
            weights = np.array([f.p for f in active])
        
        # Normalize
        return weights / weights.sum() if weights.sum() > 0 else weights
    
    def P_tv(self, t: float, v: float, method: str = 'time_decay') -> float:
        """
        Combined probability density P(t, v)
        
        Args:
            t: Time point
            v: Volatility/price change value
            method: Weight computation method
        
        Returns:
            Probability density at (t, v)
        """
        active = self.get_active_forecasts(t)
        if not active:
            return 0.0
        
        weights = self.compute_weights(t, method)
        
        # Weighted sum of densities
        prob = sum(w * f.pdf(v) for w, f in zip(weights, active))
        return prob
    
    def P_t(self, t: float, method: str = 'independent') -> float:
        """
        Marginal probability P(t) - probability of event at time t
        
        Args:
            t: Time point
            method: 'independent' or 'weighted'
        """
        active = self.get_active_forecasts(t)
        if not active:
            return 0.0
        
        if method == 'independent':
            # Assume independence: P(A) = 1 - ∏(1 - p_i)
            return 1.0 - np.prod([1 - f.p for f in active])
        else:  # weighted
            weights = self.compute_weights(t, 'time_decay')
            return sum(w * f.p for w, f in zip(weights, active))
    
    def bayesian_update(self, t: float, observed_change: float):
        """
        Update forecast probabilities based on observed price change
        using Bayesian inference
        
        Args:
            t: Current time
            observed_change: Actual observed price change
        """
        active_indices = [i for i, f in enumerate(self.forecasts) if f.is_active(t)]
        if not active_indices:
            return
        
        # Compute likelihoods
        likelihoods = np.array([
            self.forecasts[i].pdf(observed_change) 
            for i in active_indices
        ])
        
        # Prior probabilities
        priors = np.array([self.forecasts[i].p for i in active_indices])
        
        # Posterior = likelihood × prior / evidence
        posterior = likelihoods * priors
        if posterior.sum() > 0:
            posterior /= posterior.sum()
            
            # Update probabilities
            for idx, new_p in zip(active_indices, posterior):
                self.forecasts[idx].p = new_p
        
        # Log for history
        self.history.append({
            'time': t,
            'observed': observed_change,
            'updated_probs': posterior.tolist()
        })
    
    def get_confidence_interval(self, t: float, confidence: float = 0.95) -> Tuple[float, float]:
        """
        Get confidence interval for price change at time t
        
        Args:
            t: Time point
            confidence: Confidence level (0-1)
        
        Returns:
            (lower_bound, upper_bound)
        """
        active = self.get_active_forecasts(t)
        if not active:
            return (0.0, 0.0)
        
        weights = self.compute_weights(t, 'time_decay')
        
        # Compute weighted mean and variance
        weighted_mu = sum(w * f.mu for w, f in zip(weights, active))
        weighted_var = sum(w * (f.v**2 + f.mu**2) for w, f in zip(weights, active)) - weighted_mu**2
        weighted_sigma = np.sqrt(weighted_var)
        
        # Compute confidence interval
        z = norm.ppf((1 + confidence) / 2)
        lower = weighted_mu - z * weighted_sigma
        upper = weighted_mu + z * weighted_sigma
        
        return (lower, upper)
    
    def plot_heatmap(self, t_range: Tuple[float, float], v_range: Tuple[float, float], 
                     resolution: int = 100, method: str = 'time_decay'):
        """
        Plot P(t, v) as a 2D heatmap
        
        Args:
            t_range: (t_min, t_max)
            v_range: (v_min, v_max)
            resolution: Number of points in each dimension
            method: Weight computation method
        """
        t_values = np.linspace(t_range[0], t_range[1], resolution)
        v_values = np.linspace(v_range[0], v_range[1], resolution)
        
        Z = np.zeros((resolution, resolution))
        for i, t in enumerate(t_values):
            for j, v in enumerate(v_values):
                Z[j, i] = self.P_tv(t, v, method)
        
        plt.figure(figsize=(12, 6))
        
        # Main heatmap
        plt.subplot(1, 2, 1)
        im = plt.contourf(t_values, v_values, Z, levels=50, cmap='viridis')
        plt.colorbar(im, label='Probability Density')
        plt.xlabel('Time (t)', fontsize=12)
        plt.ylabel('Volatility / Price Change (v)', fontsize=12)
        plt.title('P(t, v) Heatmap - Probability Density', fontsize=14)
        plt.grid(True, alpha=0.3)
        
        # Add forecast boxes
        for f in self.forecasts:
            if f.T >= t_range[0]:
                rect_t = [0, f.T, f.T, 0, 0]
                rect_v = [f.mu - abs(f.v), f.mu - abs(f.v), 
                         f.mu + abs(f.v), f.mu + abs(f.v), f.mu - abs(f.v)]
                plt.plot(rect_t, rect_v, 'r--', alpha=0.5, linewidth=1)
        
        # Marginal P(t)
        plt.subplot(1, 2, 2)
        p_t_values = [self.P_t(t, 'weighted') for t in t_values]
        plt.plot(t_values, p_t_values, 'b-', linewidth=2)
        plt.xlabel('Time (t)', fontsize=12)
        plt.ylabel('P(t) - Marginal Probability', fontsize=12)
        plt.title('Marginal Probability Over Time', fontsize=14)
        plt.grid(True, alpha=0.3)
        plt.ylim(0, 1)
        
        plt.tight_layout()
        plt.savefig('probability_heatmap.png', dpi=300, bbox_inches='tight')
        plt.show()
        
        return Z
    
    def plot_confidence_bands(self, t_range: Tuple[float, float], 
                             confidence_levels: List[float] = [0.68, 0.95, 0.99]):
        """
        Plot expected trajectory with confidence bands
        
        Args:
            t_range: (t_min, t_max)
            confidence_levels: List of confidence levels to plot
        """
        t_values = np.linspace(t_range[0], t_range[1], 100)
        
        plt.figure(figsize=(12, 6))
        
        # Get mean trajectory
        mean_trajectory = []
        for t in t_values:
            active = self.get_active_forecasts(t)
            if active:
                weights = self.compute_weights(t, 'time_decay')
                mean = sum(w * f.mu for w, f in zip(weights, active))
                mean_trajectory.append(mean)
            else:
                mean_trajectory.append(0)
        
        plt.plot(t_values, mean_trajectory, 'b-', linewidth=2, label='Expected Value')
        
        # Plot confidence bands
        colors = ['lightblue', 'lightcoral', 'lightyellow']
        for conf, color in zip(sorted(confidence_levels, reverse=True), colors):
            lower_band = []
            upper_band = []
            for t in t_values:
                lower, upper = self.get_confidence_interval(t, conf)
                lower_band.append(lower)
                upper_band.append(upper)
            
            plt.fill_between(t_values, lower_band, upper_band, 
                           alpha=0.3, color=color, label=f'{conf*100:.0f}% CI')
        
        plt.xlabel('Time (t)', fontsize=12)
        plt.ylabel('Expected Price Change', fontsize=12)
        plt.title('Expected Trajectory with Confidence Intervals', fontsize=14)
        plt.legend()
        plt.grid(True, alpha=0.3)
        plt.axhline(y=0, color='k', linestyle='--', alpha=0.3)
        
        plt.tight_layout()
        plt.savefig('confidence_bands.png', dpi=300, bbox_inches='tight')
        plt.show()


# Example usage and demonstration
def demo_system():
    """Demonstration of the ensemble forecasting system"""
    
    print("=" * 70)
    print("ENSEMBLE PROBABILISTIC FORECASTING SYSTEM - DEMO")
    print("=" * 70)
    
    # Create system
    system = EnsembleForecastSystem(decay_rate=0.1)
    
    # Add multiple forecasts with different characteristics
    forecasts_config = [
        # Short-term bullish forecast (1h, high volatility)
        Forecast(T=1.0, v=0.02, p=0.7, mu=0.01, dist_type='normal', t0=0.0),
        
        # Medium-term neutral forecast (4h, moderate volatility)
        Forecast(T=4.0, v=0.015, p=0.6, mu=0.0, dist_type='student_t', df=5, t0=0.0),
        
        # Long-term bearish forecast (24h, fat tails)
        Forecast(T=24.0, v=-0.025, p=0.5, mu=-0.005, dist_type='student_t', df=3, t0=0.0),
        
        # Ultra short-term with skew (15min)
        Forecast(T=0.25, v=0.01, p=0.8, mu=0.005, dist_type='skew_normal', alpha=2, t0=0.0),
        
        # Swing trade forecast (12h)
        Forecast(T=12.0, v=0.03, p=0.65, mu=0.008, dist_type='laplace', t0=0.0),
    ]
    
    for f in forecasts_config:
        system.add_forecast(f)
    
    print(f"\n✓ Added {len(forecasts_config)} forecasts to ensemble\n")
    
    # Print forecast summary
    print("Forecast Summary:")
    print("-" * 70)
    print(f"{'T (hrs)':<10} {'v':<10} {'p':<10} {'μ':<10} {'Distribution':<15}")
    print("-" * 70)
    for f in system.forecasts:
        print(f"{f.T:<10.2f} {f.v:<10.4f} {f.p:<10.2f} {f.mu:<10.4f} {f.dist_type:<15}")
    print("-" * 70)
    
    # Test at different time points
    test_times = [0.5, 2.0, 8.0, 16.0]
    print("\n\nProbability Analysis at Different Times:")
    print("-" * 70)
    print(f"{'Time (t)':<12} {'P(t)':<12} {'95% CI':<25} {'Active Forecasts':<15}")
    print("-" * 70)
    
    for t in test_times:
        p_t = system.P_t(t, 'weighted')
        ci = system.get_confidence_interval(t, 0.95)
        n_active = len(system.get_active_forecasts(t))
        print(f"{t:<12.2f} {p_t:<12.4f} [{ci[0]:>6.4f}, {ci[1]:>6.4f}]      {n_active:<15}")
    print("-" * 70)
    
    # Simulate Bayesian updates
    print("\n\nSimulating Bayesian Updates:")
    print("-" * 70)
    observations = [
        (0.5, 0.008),   # At t=0.5h, observed +0.8% change
        (1.5, 0.003),   # At t=1.5h, observed +0.3% change
        (3.0, -0.002),  # At t=3h, observed -0.2% change
    ]
    
    for t, obs in observations:
        print(f"\nTime t={t:.1f}h: Observed change = {obs:+.4f}")
        before_probs = [f.p for f in system.get_active_forecasts(t)]
        system.bayesian_update(t, obs)
        after_probs = [f.p for f in system.get_active_forecasts(t)]
        print(f"  Probabilities updated: {len(before_probs)} active forecasts")
        print(f"  Before: {[f'{p:.3f}' for p in before_probs]}")
        print(f"  After:  {[f'{p:.3f}' for p in after_probs]}")
    
    # Generate visualizations
    print("\n\n" + "=" * 70)
    print("GENERATING VISUALIZATIONS...")
    print("=" * 70)
    
    # Plot 1: Heatmap
    print("\n1. Creating probability heatmap P(t, v)...")
    system.plot_heatmap(t_range=(0, 24), v_range=(-0.04, 0.04), resolution=150)
    
    # Plot 2: Confidence bands
    print("2. Creating confidence band plot...")
    system.plot_confidence_bands(t_range=(0, 24), confidence_levels=[0.68, 0.95, 0.99])
    
    print("\n✓ Visualizations saved to ")
    print("\n" + "=" * 70)
    print("DEMO COMPLETE")
    print("=" * 70)
    
    return system


if __name__ == "__main__":
    system = demo_system()
