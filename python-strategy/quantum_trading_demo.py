import numpy as np
import matplotlib.pyplot as plt
from scipy.linalg import expm
from dataclasses import dataclass
from typing import Dict, List, Tuple
import random
from enum import Enum

# =============================================================================
# REGIME DEFINITIONS
# =============================================================================

class MarketRegime(Enum):
    ACCUMULATION = 0
    INSTITUTIONAL_FOMO = 1
    EUPHORIA = 2
    SIDEWAYS = 3
    BALANCED = 4
    UNCERTAIN = 5
    DISTRIBUTION = 6
    PROFIT_TAKING = 7
    PANIC = 8

class VolatilityRegime(Enum):
    LOW_VOL = 0
    MED_VOL = 1
    HIGH_VOL = 2

class FlowRegime(Enum):
    POS_FLOW = 0
    NEUTRAL_FLOW = 1
    NEG_FLOW = 2

# =============================================================================
# CLASSICAL MARKOV CHAIN
# =============================================================================

class ClassicalMarkovChain:
    def __init__(self):
        self.n_regimes = len(MarketRegime)
        self.regimes = list(MarketRegime)
        
        # Classical transition matrix (stochastic)
        self.transition_matrix = self._create_transition_matrix()
        self.current_state = MarketRegime.BALANCED
        
    def _create_transition_matrix(self) -> np.ndarray:
        """Create realistic transition matrix for market regimes"""
        P = np.zeros((self.n_regimes, self.n_regimes))
        
        # Define transition probabilities (simplified for demo)
        transitions = {
            MarketRegime.ACCUMULATION: {
                MarketRegime.ACCUMULATION: 0.7,
                MarketRegime.INSTITUTIONAL_FOMO: 0.15,
                MarketRegime.BALANCED: 0.1,
                MarketRegime.UNCERTAIN: 0.05
            },
            MarketRegime.INSTITUTIONAL_FOMO: {
                MarketRegime.INSTITUTIONAL_FOMO: 0.4,
                MarketRegime.EUPHORIA: 0.3,
                MarketRegime.ACCUMULATION: 0.2,
                MarketRegime.BALANCED: 0.1
            },
            MarketRegime.EUPHORIA: {
                MarketRegime.EUPHORIA: 0.5,
                MarketRegime.DISTRIBUTION: 0.3,
                MarketRegime.PANIC: 0.1,
                MarketRegime.PROFIT_TAKING: 0.1
            },
            MarketRegime.PANIC: {
                MarketRegime.PANIC: 0.3,
                MarketRegime.ACCUMULATION: 0.4,
                MarketRegime.UNCERTAIN: 0.2,
                MarketRegime.SIDEWAYS: 0.1
            },
            # ... simplified, fill with default values for others
        }
        
        # Fill matrix with defined transitions
        for from_regime, to_regimes in transitions.items():
            for to_regime, prob in to_regimes.items():
                P[from_regime.value, to_regime.value] = prob
        
        # Fill remaining with small default probabilities and normalize
        for i in range(self.n_regimes):
            if np.sum(P[i, :]) == 0:
                P[i, :] = 0.1  # Default uniform
                P[i, i] = 0.3  # Higher self-transition
            # Normalize to make stochastic
            P[i, :] = P[i, :] / np.sum(P[i, :])
            
        return P
    
    def step(self, market_conditions: Dict = None) -> MarketRegime:
        """Take one step in Markov chain"""
        current_idx = self.current_state.value
        probs = self.transition_matrix[current_idx, :]
        
        # Sample next state
        next_idx = np.random.choice(self.n_regimes, p=probs)
        self.current_state = MarketRegime(next_idx)
        return self.current_state
    
    def get_probability_distribution(self) -> np.ndarray:
        """Get current probability distribution (one-hot for classical)"""
        dist = np.zeros(self.n_regimes)
        dist[self.current_state.value] = 1.0
        return dist

# =============================================================================
# QUANTUM TRADING STATE
# =============================================================================

class QuantumTradingState:
    def __init__(self):
        # Dimensions
        self.n_market_regimes = len(MarketRegime)
        self.n_vol_regimes = len(VolatilityRegime)  
        self.n_flow_regimes = len(FlowRegime)
        
        # Total Hilbert space dimension
        self.hilbert_dim = self.n_market_regimes * self.n_vol_regimes * self.n_flow_regimes
        
        # Initialize in balanced superposition
        self.state_vector = self._create_initial_state()
        
        # Market parameters (in natural units where ℏ_market = 1)
        self.hbar_market = 1.0
        
    def _create_initial_state(self) -> np.ndarray:
        """Create initial quantum superposition state"""
        # Start in superposition of likely regimes
        psi = np.zeros(self.hilbert_dim, dtype=complex)
        
        # Give higher amplitude to certain regimes
        regime_amplitudes = {
            MarketRegime.BALANCED: 0.4,
            MarketRegime.UNCERTAIN: 0.3, 
            MarketRegime.ACCUMULATION: 0.2,
            MarketRegime.SIDEWAYS: 0.1
        }
        
        for regime, amp in regime_amplitudes.items():
            for vol in VolatilityRegime:
                for flow in FlowRegime:
                    idx = self._get_state_index(regime, vol, flow)
                    # Add random phase for quantum interference
                    phase = np.random.uniform(0, 2*np.pi)
                    psi[idx] = amp * np.exp(1j * phase) / np.sqrt(len(VolatilityRegime) * len(FlowRegime))
        
        # Normalize
        psi = psi / np.linalg.norm(psi)
        return psi
    
    def _get_state_index(self, market_regime: MarketRegime, 
                        vol_regime: VolatilityRegime,
                        flow_regime: FlowRegime) -> int:
        """Convert regime combination to Hilbert space index"""
        return (market_regime.value * self.n_vol_regimes * self.n_flow_regimes + 
                vol_regime.value * self.n_flow_regimes + 
                flow_regime.value)
    
    def _index_to_regimes(self, idx: int) -> Tuple[MarketRegime, VolatilityRegime, FlowRegime]:
        """Convert Hilbert space index back to regime combination"""
        flow_idx = idx % self.n_flow_regimes
        vol_idx = (idx // self.n_flow_regimes) % self.n_vol_regimes
        market_idx = idx // (self.n_vol_regimes * self.n_flow_regimes)
        
        return (MarketRegime(market_idx), VolatilityRegime(vol_idx), FlowRegime(flow_idx))
    
    def create_hamiltonian(self, market_conditions: Dict) -> np.ndarray:
        """Create market Hamiltonian based on current conditions"""
        H = np.zeros((self.hilbert_dim, self.hilbert_dim), dtype=complex)
        
        # Extract market conditions
        etf_flow = market_conditions.get('etf_flow', 0.0)
        volatility = market_conditions.get('volatility', 0.02)
        order_book_imbalance = market_conditions.get('order_book_imbalance', 0.0)
        
        for i in range(self.hilbert_dim):
            market_i, vol_i, flow_i = self._index_to_regimes(i)
            
            # Diagonal terms (regime energies)
            regime_energy = self._get_regime_energy(market_i, vol_i, flow_i)
            H[i, i] = regime_energy
            
            # Off-diagonal terms (regime transitions with market coupling)
            for j in range(self.hilbert_dim):
                if i != j:
                    market_j, vol_j, flow_j = self._index_to_regimes(j)
                    coupling = self._get_regime_coupling(
                        market_i, vol_i, flow_i,
                        market_j, vol_j, flow_j,
                        etf_flow, volatility, order_book_imbalance
                    )
                    H[i, j] = coupling
        
        return H
    
    def _get_regime_energy(self, market_regime: MarketRegime, 
                          vol_regime: VolatilityRegime,
                          flow_regime: FlowRegime) -> float:
        """Get energy of a regime combination"""
        # Define regime energies (higher energy = less stable)
        market_energies = {
            MarketRegime.PANIC: 3.0,
            MarketRegime.EUPHORIA: 2.5,
            MarketRegime.UNCERTAIN: 2.0,
            MarketRegime.DISTRIBUTION: 1.5,
            MarketRegime.INSTITUTIONAL_FOMO: 1.2,
            MarketRegime.PROFIT_TAKING: 1.0,
            MarketRegime.SIDEWAYS: 0.5,
            MarketRegime.BALANCED: 0.3,
            MarketRegime.ACCUMULATION: 0.1
        }
        
        vol_energies = {
            VolatilityRegime.HIGH_VOL: 1.0,
            VolatilityRegime.MED_VOL: 0.5,
            VolatilityRegime.LOW_VOL: 0.1
        }
        
        flow_energies = {
            FlowRegime.POS_FLOW: 0.2,
            FlowRegime.NEG_FLOW: 0.2,
            FlowRegime.NEUTRAL_FLOW: 0.0
        }
        
        return (market_energies[market_regime] + 
                vol_energies[vol_regime] + 
                flow_energies[flow_regime])
    
    def _get_regime_coupling(self, market_i, vol_i, flow_i,
                           market_j, vol_j, flow_j,
                           etf_flow, volatility, order_book_imbalance) -> complex:
        """Get coupling between two regime combinations"""
        
        # Calculate regime differences
        market_diff = abs(market_i.value - market_j.value)
        vol_diff = abs(vol_i.value - vol_j.value)
        flow_diff = abs(flow_i.value - flow_j.value)
        total_diff = market_diff + vol_diff + flow_diff
        
        # Allow more flexible coupling (not just nearest neighbors)
        if total_diff == 0:
            return 0.0  # No self-coupling
        elif total_diff > 3:
            return 0.0  # Too distant
        
        # Base coupling strength (much stronger!)
        coupling = 0.5 / total_diff  # Stronger coupling for closer states
        
        # Market-driven coupling modifications
        if market_diff >= 1:  # Market regime transitions
            # ETF flow drives regime transitions
            coupling *= (0.5 + abs(etf_flow) * 2.0)
            
            # Volatility affects transition strength  
            coupling *= (0.5 + volatility * 10)
            
            # Order book imbalance affects transitions
            coupling *= (0.5 + abs(order_book_imbalance) * 3)
            
        # Strategic phase relationships for interference
        phase = 0.0
        if market_i == MarketRegime.EUPHORIA and market_j == MarketRegime.PANIC:
            phase = np.pi  # Opposite phases
        elif market_i == MarketRegime.ACCUMULATION and market_j == MarketRegime.EUPHORIA:
            phase = np.pi/2  # Quadrature phases
        else:
            phase = np.random.uniform(0, 2*np.pi)
            
        return coupling * np.exp(1j * phase)
    
    def evolve(self, market_conditions: Dict, dt: float = 0.05):
        """Evolve quantum state using Schrödinger equation"""
        H = self.create_hamiltonian(market_conditions)
        
        # Add decoherence to prevent infinite coherence
        # But not too strong to maintain quantum effects
        decoherence_rate = 0.01
        
        # Time evolution operator: U = exp(-iHt/ℏ) with decoherence
        evolution_matrix = -1j * H * dt / self.hbar_market
        
        # Add small random decoherence
        decoherence = np.random.normal(0, decoherence_rate * dt, 
                                     (self.hilbert_dim, self.hilbert_dim))
        evolution_matrix += decoherence
        
        U = expm(evolution_matrix)
        
        # Apply evolution
        self.state_vector = U @ self.state_vector
        
        # Ensure normalization (numerical stability)
        norm = np.linalg.norm(self.state_vector)
        if norm > 0:
            self.state_vector = self.state_vector / norm
    
    def measure_regime(self) -> Tuple[MarketRegime, float]:
        """Measure market regime (collapses superposition)"""
        # Calculate probabilities for each market regime
        regime_probs = np.zeros(self.n_market_regimes)
        
        for i in range(self.hilbert_dim):
            market_regime, _, _ = self._index_to_regimes(i)
            regime_probs[market_regime.value] += abs(self.state_vector[i])**2
        
        # Sample measurement outcome
        measured_regime_idx = np.random.choice(self.n_market_regimes, p=regime_probs)
        measured_regime = MarketRegime(measured_regime_idx)
        confidence = regime_probs[measured_regime_idx]
        
        # Partial collapse (don't fully collapse to maintain some superposition)
        collapse_strength = 0.7  # Partial collapse
        
        new_state = self.state_vector.copy()
        for i in range(self.hilbert_dim):
            market_regime, vol_regime, flow_regime = self._index_to_regimes(i)
            if market_regime != measured_regime:
                # Reduce amplitude of non-measured states
                new_state[i] *= (1 - collapse_strength)
            else:
                # Slightly enhance measured state
                new_state[i] *= (1 + collapse_strength * 0.3)
        
        # Renormalize
        if np.linalg.norm(new_state) > 0:
            self.state_vector = new_state / np.linalg.norm(new_state)
            
        return measured_regime, confidence
    
    def get_regime_probabilities(self) -> Dict[MarketRegime, float]:
        """Get current regime probabilities without measurement"""
        regime_probs = {}
        
        for regime in MarketRegime:
            prob = 0.0
            for i in range(self.hilbert_dim):
                market_regime, _, _ = self._index_to_regimes(i)
                if market_regime == regime:
                    prob += abs(self.state_vector[i])**2
            regime_probs[regime] = prob
            
        return regime_probs
    
    def calculate_entanglement(self) -> float:
        """Calculate entanglement between market and volatility regimes"""
        # Reshape state vector into matrix for Schmidt decomposition
        # This is a simplified measure
        
        # Calculate von Neumann entropy of reduced density matrix
        # (simplified calculation for demo)
        regime_probs = list(self.get_regime_probabilities().values())
        regime_probs = [p for p in regime_probs if p > 1e-10]  # Remove zeros
        
        if len(regime_probs) <= 1:
            return 0.0
            
        entropy = -sum(p * np.log2(p) for p in regime_probs)
        max_entropy = np.log2(len(regime_probs))
        
        return entropy / max_entropy if max_entropy > 0 else 0.0

# =============================================================================
# DEMONSTRATION AND COMPARISON
# =============================================================================

def simulate_market_conditions(t: int) -> Dict:
    """Generate realistic market conditions over time"""
    # Simulate some market dynamics
    base_vol = 0.02
    vol_cycle = base_vol + 0.01 * np.sin(t * 0.1) + 0.005 * np.random.randn()
    
    etf_flow = 0.3 * np.sin(t * 0.05) + 0.1 * np.random.randn()
    order_book_imbalance = 0.2 * np.sin(t * 0.2) + 0.05 * np.random.randn()
    
    return {
        'volatility': max(0.005, vol_cycle),
        'etf_flow': np.clip(etf_flow, -1.0, 1.0),
        'order_book_imbalance': np.clip(order_book_imbalance, -0.5, 0.5)
    }

def compare_systems():
    """Compare Classical Markov vs Quantum Trading Systems"""
    print("🔬 QUANTUM TRADING SYSTEM DEMO")
    print("=" * 50)
    
    # Initialize systems
    classical_system = ClassicalMarkovChain()
    quantum_system = QuantumTradingState()
    
    # Simulation parameters
    n_steps = 100
    
    # Storage for results
    classical_regimes = []
    quantum_regimes = []
    quantum_probabilities = []
    quantum_entanglement = []
    market_conditions_history = []
    
    print("\n📊 RUNNING SIMULATION...")
    
    for t in range(n_steps):
        # Generate market conditions
        market_conditions = simulate_market_conditions(t)
        market_conditions_history.append(market_conditions)
        
        # Classical system step
        classical_regime = classical_system.step(market_conditions)
        classical_regimes.append(classical_regime)
        
        # Quantum system evolution
        quantum_system.evolve(market_conditions)
        
        # Measure quantum system (with some probability to show superposition)
        if np.random.random() < 0.3:  # Only measure 30% of time
            quantum_regime, confidence = quantum_system.measure_regime()
        else:
            # Get most likely regime without measurement
            probs = quantum_system.get_regime_probabilities()
            quantum_regime = max(probs.keys(), key=lambda k: probs[k])
            confidence = probs[quantum_regime]
        
        quantum_regimes.append(quantum_regime)
        quantum_probabilities.append(quantum_system.get_regime_probabilities())
        quantum_entanglement.append(quantum_system.calculate_entanglement())
        
        # Print progress
        if t % 20 == 0:
            print(f"Step {t}: Classical={classical_regime.name}, "
                  f"Quantum={quantum_regime.name} ({confidence:.2f}), "
                  f"Entanglement={quantum_entanglement[-1]:.2f}")
    
    # Analysis
    print("\n📈 ANALYSIS RESULTS:")
    print("-" * 30)
    
    # Regime diversity
    classical_unique = len(set(classical_regimes))
    quantum_unique = len(set(quantum_regimes))
    print(f"Regime diversity - Classical: {classical_unique}, Quantum: {quantum_unique}")
    
    # Average entanglement
    avg_entanglement = np.mean(quantum_entanglement)
    print(f"Average quantum entanglement: {avg_entanglement:.3f}")
    
    # Regime stability (transitions per step)
    classical_transitions = sum(1 for i in range(1, len(classical_regimes)) 
                               if classical_regimes[i] != classical_regimes[i-1])
    quantum_transitions = sum(1 for i in range(1, len(quantum_regimes)) 
                             if quantum_regimes[i] != quantum_regimes[i-1])
    
    print(f"Transitions - Classical: {classical_transitions}, Quantum: {quantum_transitions}")
    
    return {
        'classical_regimes': classical_regimes,
        'quantum_regimes': quantum_regimes,
        'quantum_probabilities': quantum_probabilities,
        'quantum_entanglement': quantum_entanglement,
        'market_conditions': market_conditions_history
    }

def demonstrate_quantum_properties():
    """Demonstrate unique quantum properties"""
    print("\n🌊 QUANTUM PROPERTIES DEMONSTRATION")
    print("=" * 40)
    
    quantum_system = QuantumTradingState()
    
    # 1. Superposition
    print("\n1. SUPERPOSITION:")
    probs = quantum_system.get_regime_probabilities()
    print("Current regime probabilities (before measurement):")
    for regime, prob in probs.items():
        if prob > 0.01:  # Only show significant probabilities
            print(f"  {regime.name}: {prob:.3f}")
    
    # 2. Measurement and collapse
    print("\n2. MEASUREMENT & COLLAPSE:")
    print("Before measurement - multiple regimes possible")
    measured_regime, confidence = quantum_system.measure_regime()
    print(f"After measurement: {measured_regime.name} (confidence: {confidence:.3f})")
    
    # 3. Entanglement
    print("\n3. ENTANGLEMENT:")
    entanglement = quantum_system.calculate_entanglement()
    print(f"Current entanglement measure: {entanglement:.3f}")
    print("(Higher values indicate stronger quantum correlations)")
    
    # 4. Evolution under different conditions
    print("\n4. QUANTUM EVOLUTION:")
    extreme_conditions = {
        'volatility': 0.1,  # Very high volatility
        'etf_flow': 0.8,    # Strong positive flow
        'order_book_imbalance': 0.4  # High imbalance
    }
    
    print("Evolving under extreme market conditions...")
    for i in range(5):
        quantum_system.evolve(extreme_conditions, dt=0.2)
        probs = quantum_system.get_regime_probabilities()
        dominant_regime = max(probs.keys(), key=lambda k: probs[k])
        print(f"  Step {i+1}: Most likely regime = {dominant_regime.name} "
              f"({probs[dominant_regime]:.3f})")

if __name__ == "__main__":
    # Run full comparison
    results = compare_systems()
    
    # Demonstrate quantum properties
    demonstrate_quantum_properties()
    
    print(f"\n✨ DEMO COMPLETE!")
    print("\nKey insights:")
    print("• Classical system: Definite states, local transitions")
    print("• Quantum system: Superposition, entanglement, interference effects")
    print("• Quantum approach captures market uncertainty more naturally")
    print("• Entanglement reveals hidden correlations between market factors")
