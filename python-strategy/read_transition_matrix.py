import sqlite3
import numpy as np
import pandas as pd
from typing import Dict, List, Optional
from datetime import datetime

def read_transition_matrix_from_db(db_path: str, symbol: str) -> Optional[Dict]:
    """Read transition matrix probabilities from database"""
    
    conn = sqlite3.connect(db_path)
    try:
        # Query transition matrix data
        cursor = conn.execute("""
            SELECT from_regime, to_regime, probability, transition_count, updated_at
            FROM transition_matrix 
            WHERE symbol = ?
            ORDER BY from_regime, to_regime
        """, (symbol,))
        
        results = cursor.fetchall()
        
        if not results:
            print(f"No transition matrix found for {symbol}")
            return None
        
        # Organize data
        transition_data = {}
        matrix_data = []
        
        for from_regime, to_regime, probability, count, updated_at in results:
            if from_regime not in transition_data:
                transition_data[from_regime] = {}
            
            transition_data[from_regime][to_regime] = {
                'probability': probability,
                'count': count,
                'updated_at': updated_at
            }
            
            matrix_data.append({
                'from_regime': from_regime,
                'to_regime': to_regime, 
                'probability': probability,
                'count': count
            })
        
        return {
            'symbol': symbol,
            'transition_probabilities': transition_data,
            'matrix_data': matrix_data,
            'last_updated': results[0][4] if results else None
        }
        
    except Exception as e:
        print(f"Error reading transition matrix: {e}")
        return None
    finally:
        conn.close()

def print_transition_matrix_readable(db_path: str, symbol: str):
    """Print transition matrix in readable format"""
    
    data = read_transition_matrix_from_db(db_path, symbol)
    if not data:
        return
    
    print(f"\n=== TRANSITION MATRIX FOR {symbol} ===")
    print(f"Last Updated: {data['last_updated']}\n")
    
    transitions = data['transition_probabilities']
    
    # Print detailed probabilities
    for from_regime, to_regimes in transitions.items():
        print(f"FROM {from_regime}:")
        for to_regime, info in to_regimes.items():
            prob = info['probability']
            count = info['count']
            print(f"  → {to_regime}: {prob:.3f} ({prob*100:.1f}%) [count: {count:.0f}]")
        print()

def get_regime_transition_probability(db_path: str, symbol: str, 
                                    from_regime: str, to_regime: str) -> float:
    """Get specific transition probability"""
    
    data = read_transition_matrix_from_db(db_path, symbol)
    if not data:
        return 0.0
    
    transitions = data['transition_probabilities']
    
    if from_regime in transitions and to_regime in transitions[from_regime]:
        return transitions[from_regime][to_regime]['probability']
    
    return 0.0

def get_most_likely_next_regime(db_path: str, symbol: str, current_regime: str) -> Dict:
    """Get most likely next regime from current regime"""
    
    data = read_transition_matrix_from_db(db_path, symbol)
    if not data or current_regime not in data['transition_probabilities']:
        return {'regime': None, 'probability': 0.0}
    
    transitions = data['transition_probabilities'][current_regime]
    
    # Find regime with highest probability
    best_regime = max(transitions.items(), key=lambda x: x[1]['probability'])
    
    return {
        'regime': best_regime[0],
        'probability': best_regime[1]['probability'],
        'all_transitions': {k: v['probability'] for k, v in transitions.items()}
    }

def create_transition_matrix_dataframe(db_path: str, symbol: str) -> Optional[pd.DataFrame]:
    """Create pandas DataFrame of transition matrix"""
    
    data = read_transition_matrix_from_db(db_path, symbol)
    if not data:
        return None
    
    # Get all unique regimes
    all_regimes = set()
    for row in data['matrix_data']:
        all_regimes.add(row['from_regime'])
        all_regimes.add(row['to_regime'])
    
    all_regimes = sorted(list(all_regimes))
    
    # Create matrix
    matrix = np.zeros((len(all_regimes), len(all_regimes)))
    
    for row in data['matrix_data']:
        from_idx = all_regimes.index(row['from_regime'])
        to_idx = all_regimes.index(row['to_regime'])
        matrix[from_idx, to_idx] = row['probability']
    
    # Create DataFrame
    df = pd.DataFrame(matrix, index=all_regimes, columns=all_regimes)
    return df

# Example usage
if __name__ == "__main__":
    db_path = "data/markov_persistence.db"  # Your database path
    symbol = "BTCUSDT"
    
    # Print readable transition matrix
    print_transition_matrix_readable(db_path, symbol)
    
    # Get specific transition probability
    prob = get_regime_transition_probability(db_path, symbol, 
                                           "STRONG_UPTREND", "WEAK_UPTREND")
    print(f"Probability STRONG_UPTREND → WEAK_UPTREND: {prob:.3f}")
    
    # Get most likely next regime
    next_regime = get_most_likely_next_regime(db_path, symbol, "MEAN_REVERSION")
    print(f"Most likely after MEAN_REVERSION: {next_regime}")
    
    # Create DataFrame for analysis
    df = create_transition_matrix_dataframe(db_path, symbol)
    if df is not None:
        print("\nTransition Matrix DataFrame:")
        print(df.round(3))
        
        # Save to CSV for external analysis
        df.to_csv(f"{symbol}_transition_matrix.csv")
        print(f"Saved to {symbol}_transition_matrix.csv")