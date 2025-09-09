import pandas as pd
import numpy as np
from datetime import datetime, timedelta
import json
def convert_numpy_types(obj):
    """Convert numpy types to native Python types for JSON serialization"""
    if isinstance(obj, np.integer):
        return int(obj)
    elif isinstance(obj, np.floating):
        return float(obj)
    elif isinstance(obj, np.ndarray):
        return obj.tolist()
    return obj

def convert_to_ptca_format(df):
    """Convert healthcare data to PTCA algorithm format"""
    
    ptca_data = []
    
    for patient_id in df['patient_id'].unique():
        patient_data = df[df['patient_id'] == patient_id].sort_values('day')
        
        for idx, row in patient_data.iterrows():
            # Binary events - convert to Python int
            events = {
                'antibiotic_given': int(row['antibiotic_given']),
                'pain_medication': int(row['pain_medication']),
                'oxygen_therapy': int(row['oxygen_therapy']),
                'physical_therapy': int(row['physical_therapy']),
                'surgery_performed': int(row['surgery_performed']),
                'ventilator_used': int(row['ventilator_used']),
                'dialysis': int(row['dialysis']),
                'blood_transfusion': int(row['blood_transfusion']),
                'infection_detected': int(row['infection_detected']),
                'fever_present': int(row['fever_present']),
            }
            
            # Event scores - convert to Python float
            scores = {
                'antibiotic_given': float(row['antibiotic_given_score']),
                'pain_medication': float(row['pain_medication_score']),
                'oxygen_therapy': float(row['oxygen_therapy_score']),
                'physical_therapy': float(row['physical_therapy_score']),
                'surgery_performed': float(row['surgery_performed_score']),
                'ventilator_used': float(row['ventilator_used_score']),
                'dialysis': float(row['dialysis_score']),
                'blood_transfusion': float(row['blood_transfusion_score']),
                'infection_detected': float(row['infection_detected_score']),
                'fever_present': float(row['fever_present_score']),
            }
            
            # Outcome (health change) - convert to Python float
            outcome = float(row['normalized_outcome'])
            
            ptca_data.append({
                'patient_id': int(patient_id),
                'day': int(row['day']),
                'events': events,
                'scores': scores,
                'outcome': outcome,
                'metadata': {
                    'age': int(row['age']),
                    'gender': str(row['gender']),
                    'admission_severity': str(row['admission_severity']),
                    'current_health': float(row['current_health_score'])
                }
            })
    
    return ptca_data
def create_realistic_healthcare_sample():
    """
    Create realistic healthcare sample data for PTCA testing
    Based on MIMIC-IV structure but completely synthetic
    """
    
    # Set random seed for reproducibility
    np.random.seed(42)
    
    # Sample patients
    n_patients = 50
    n_days_per_patient = 14  # 2 weeks ICU stay average
    
    healthcare_data = []
    
    for patient_id in range(1, n_patients + 1):
        # Patient baseline characteristics
        age = np.random.randint(18, 90)
        gender = np.random.choice(['M', 'F'])
        admission_severity = np.random.choice(['Low', 'Medium', 'High'], p=[0.3, 0.5, 0.2])
        
        # Baseline health score (0-100, higher is better)
        if admission_severity == 'Low':
            baseline_health = np.random.uniform(70, 85)
        elif admission_severity == 'Medium':
            baseline_health = np.random.uniform(50, 70)
        else:  # High severity
            baseline_health = np.random.uniform(20, 50)
        
        current_health = baseline_health
        
        for day in range(n_days_per_patient):
            # Daily events (binary)
            events = {
                'antibiotic_given': np.random.choice([0, 1], p=[0.7, 0.3]),
                'pain_medication': np.random.choice([0, 1], p=[0.6, 0.4]),
                'oxygen_therapy': np.random.choice([0, 1], p=[0.8, 0.2]),
                'physical_therapy': np.random.choice([0, 1], p=[0.85, 0.15]),
                'surgery_performed': np.random.choice([0, 1], p=[0.95, 0.05]),
                'ventilator_used': np.random.choice([0, 1], p=[0.9, 0.1]),
                'dialysis': np.random.choice([0, 1], p=[0.95, 0.05]),
                'blood_transfusion': np.random.choice([0, 1], p=[0.9, 0.1]),
                'infection_detected': np.random.choice([0, 1], p=[0.85, 0.15]),
                'fever_present': np.random.choice([0, 1], p=[0.8, 0.2]),
            }
            
            # Event intensity scores (0-1)
            scores = {}
            for event_name in events.keys():
                if event_name in ['antibiotic_given', 'pain_medication', 'physical_therapy']:
                    # Beneficial treatments
                    scores[event_name] = np.random.uniform(0.6, 1.0)
                elif event_name in ['oxygen_therapy', 'surgery_performed', 'blood_transfusion']:
                    # Critical treatments (high intensity)
                    scores[event_name] = np.random.uniform(0.8, 1.0)
                elif event_name in ['ventilator_used', 'dialysis']:
                    # Life support (very high intensity)
                    scores[event_name] = np.random.uniform(0.9, 1.0)
                else:
                    # Complications (variable intensity)
                    scores[event_name] = np.random.uniform(0.3, 0.8)
            
            # Calculate health outcome for next day based on treatments and complications
            health_change = 0
            
            # Positive treatments
            if events['antibiotic_given']:
                health_change += 3 * scores['antibiotic_given']
            if events['pain_medication']:
                health_change += 2 * scores['pain_medication']
            if events['physical_therapy']:
                health_change += 4 * scores['physical_therapy']
            if events['oxygen_therapy']:
                health_change += 2.5 * scores['oxygen_therapy']
            if events['surgery_performed']:
                health_change += 5 * scores['surgery_performed']  # Major positive impact
            if events['blood_transfusion']:
                health_change += 3.5 * scores['blood_transfusion']
            
            # Life support (neutral to slightly positive)
            if events['ventilator_used']:
                health_change += 1 * scores['ventilator_used']
            if events['dialysis']:
                health_change += 1.5 * scores['dialysis']
            
            # Negative complications
            if events['infection_detected']:
                health_change -= 4 * scores['infection_detected']
            if events['fever_present']:
                health_change -= 2.5 * scores['fever_present']
            
            # Natural recovery/deterioration trend
            if current_health < 40:
                health_change += np.random.uniform(-1, 2)  # Slight upward bias when critical
            elif current_health > 80:
                health_change += np.random.uniform(-0.5, 1)  # Slower improvement when healthy
            else:
                health_change += np.random.uniform(-1, 1.5)  # Normal range
            
            # Add noise
            health_change += np.random.normal(0, 1)
            
            # Update current health (bounded 0-100)
            current_health = max(0, min(100, current_health + health_change))
            
            # Normalize health change for PTCA (-1 to 1 scale)
            normalized_outcome = (health_change) / 10.0  # Scale to reasonable range
            
            # Create data row
            data_row = {
                'patient_id': patient_id,
                'day': day + 1,
                'age': age,
                'gender': gender,
                'admission_severity': admission_severity,
                'current_health_score': round(current_health, 2),
                'health_change': round(health_change, 3),
                'normalized_outcome': round(normalized_outcome, 3),
                **events,  # Add all binary events
                **{f"{k}_score": round(v, 3) for k, v in scores.items()}  # Add all scores
            }
            
            healthcare_data.append(data_row)
    
    return pd.DataFrame(healthcare_data)

def convert_to_ptca_format1(df):
    """Convert healthcare data to PTCA algorithm format"""
    
    ptca_data = []
    
    for patient_id in df['patient_id'].unique():
        patient_data = df[df['patient_id'] == patient_id].sort_values('day')
        
        for idx, row in patient_data.iterrows():
            # Binary events
            events = {
                'antibiotic_given': row['antibiotic_given'],
                'pain_medication': row['pain_medication'],
                'oxygen_therapy': row['oxygen_therapy'],
                'physical_therapy': row['physical_therapy'],
                'surgery_performed': row['surgery_performed'],
                'ventilator_used': row['ventilator_used'],
                'dialysis': row['dialysis'],
                'blood_transfusion': row['blood_transfusion'],
                'infection_detected': row['infection_detected'],
                'fever_present': row['fever_present'],
            }
            
            # Event scores
            scores = {
                'antibiotic_given': row['antibiotic_given_score'],
                'pain_medication': row['pain_medication_score'],
                'oxygen_therapy': row['oxygen_therapy_score'],
                'physical_therapy': row['physical_therapy_score'],
                'surgery_performed': row['surgery_performed_score'],
                'ventilator_used': row['ventilator_used_score'],
                'dialysis': row['dialysis_score'],
                'blood_transfusion': row['blood_transfusion_score'],
                'infection_detected': row['infection_detected_score'],
                'fever_present': row['fever_present_score'],
            }
            
            # Outcome (health change)
            outcome = row['normalized_outcome']
            
            ptca_data.append({
                'patient_id': patient_id,
                'day': row['day'],
                'events': events,
                'scores': scores,
                'outcome': outcome,
                'metadata': {
                    'age': row['age'],
                    'gender': row['gender'],
                    'admission_severity': row['admission_severity'],
                    'current_health': row['current_health_score']
                }
            })
    
    return ptca_data

def save_sample_data():
    """Generate and save sample healthcare data"""
    
    print("🏥 Generating realistic healthcare sample data...")
    
    # Create sample data
    df = create_realistic_healthcare_sample()
    
    # Save CSV for easy viewing
    df.to_csv('healthcare_sample_data.csv', index=False)
    print(f"✅ Saved {len(df)} records to 'healthcare_sample_data.csv'")
    
    # Convert to PTCA format
    ptca_data = convert_to_ptca_format(df)
    
    # Save JSON for PTCA algorithm
    with open('healthcare_ptca_format.json', 'w') as f:
        json.dump(ptca_data, f, indent=2)
    print(f"✅ Saved PTCA format to 'healthcare_ptca_format.json'")
    
    # Print summary statistics
    print("\n📊 DATA SUMMARY:")
    print(f"Patients: {df['patient_id'].nunique()}")
    print(f"Total records: {len(df)}")
    print(f"Days per patient: {df.groupby('patient_id')['day'].count().iloc[0]}")
    print(f"Average health change: {df['health_change'].mean():.2f}")
    print(f"Health change std: {df['health_change'].std():.2f}")
    
    print("\n🎯 EVENT FREQUENCIES:")
    event_cols = [col for col in df.columns if col.endswith('_given') or col.endswith('_performed') or col.endswith('_used') or col.endswith('_detected') or col.endswith('_present') or col.endswith('_therapy')]
    for event in event_cols:
        freq = df[event].mean()
        print(f"  {event}: {freq:.1%}")
    
    print(f"\n📈 SAMPLE DATA FOR PTCA TESTING:")
    print("Events -> Health outcomes with realistic medical relationships")
    print("Ready to test PTCA algorithm! 🚀")
    
    return df, ptca_data

def test_ptca_with_sample():
    """Quick test of PTCA with sample data"""
    
    # Import our PTCA algorithm
    from collections import defaultdict
    import math
    
    class SamplePTCA:
        def __init__(self, n_lookback=3, thresholds={'weak': 0.2, 'strong': 0.5}):
            self.n_lookback = n_lookback
            self.thresholds = thresholds
            self.decay_factor = 0.9
            
            self.accumulated_scores = defaultdict(float)
            self.association_strength = defaultdict(float)
            self.conditioning_count = defaultdict(int)
            
        def _get_reinforcement_strength(self, outcome):
            abs_outcome = abs(outcome)
            if abs_outcome >= self.thresholds['strong']:
                intensity = abs_outcome / self.thresholds['strong']
                return (2.0 * intensity) if outcome >= 0 else (-2.0 * intensity)
            elif abs_outcome >= self.thresholds['weak']:
                progress = (abs_outcome - self.thresholds['weak']) / (self.thresholds['strong'] - self.thresholds['weak'])
                return (1.0 + progress) if outcome >= 0 else (-1.0 - progress)
            return 0.0
        
        def process_patient_data(self, patient_data):
            patient_history = []
            
            for day_data in patient_data:
                events = day_data['events']
                scores = day_data['scores']
                outcome = day_data['outcome']
                
                patient_history.append((events, scores, outcome))
                
                reinforcement = self._get_reinforcement_strength(outcome)
                
                if reinforcement != 0:
                    current_day = len(patient_history) - 1
                    start_day = max(0, current_day - self.n_lookback)
                    
                    for event_name in events:
                        if events[event_name] == 1:  # Event occurred
                            weighted_score = 0
                            for i in range(start_day, current_day + 1):
                                distance = current_day - i
                                weight = self.decay_factor ** distance
                                weighted_score += patient_history[i][1][event_name] * weight
                            
                            score_delta = reinforcement * weighted_score
                            self.accumulated_scores[event_name] += score_delta
                            
                            self.association_strength[event_name] += reinforcement * 0.1
                            self.association_strength[event_name] = max(-1, min(1, self.association_strength[event_name]))
                            
                            self.conditioning_count[event_name] += 1
        
        def get_results(self):
            results = []
            for event in self.accumulated_scores:
                results.append({
                    'event': event,
                    'accumulated_score': self.accumulated_scores[event],
                    'association_strength': self.association_strength[event],
                    'conditioning_count': self.conditioning_count[event]
                })
            return sorted(results, key=lambda x: x['accumulated_score'], reverse=True)
    
    # Load sample data
    print("🧠 Testing PTCA with sample healthcare data...")
    
    df, ptca_data = save_sample_data()
    
    # Group by patient
    patients = {}
    for record in ptca_data:
        pid = record['patient_id']
        if pid not in patients:
            patients[pid] = []
        patients[pid].append(record)
    
    # Test PTCA on sample patients
    ptca = SamplePTCA(n_lookback=3, thresholds={'weak': 0.2, 'strong': 0.5})
    
    # Process first 10 patients
    for pid in list(patients.keys())[:10]:
        ptca.process_patient_data(patients[pid])
    
    # Get results
    results = ptca.get_results()
    
    print(f"\n🏆 PTCA RESULTS ON SAMPLE DATA:")
    print("=" * 60)
    
    for i, result in enumerate(results[:10]):
        event = result['event']
        score = result['accumulated_score']
        association = result['association_strength']
        count = result['conditioning_count']
        
        if score > 0:
            impact = "BENEFICIAL ✅"
        elif score < 0:
            impact = "HARMFUL ⚠️"
        else:
            impact = "NEUTRAL ➖"
        
        print(f"{i+1:2d}. {event:20} | Score: {score:8.3f} | Assoc: {association:6.3f} | Count: {count:3d} | {impact}")
    
    print(f"\n🎯 INTERPRETATION:")
    top_beneficial = [r for r in results if r['accumulated_score'] > 0][:3]
    top_harmful = [r for r in results if r['accumulated_score'] < 0][:3]
    
    if top_beneficial:
        print(f"✅ Most beneficial treatments:")
        for treat in top_beneficial:
            print(f"   - {treat['event']}: Score {treat['accumulated_score']:.3f}")
    
    if top_harmful:
        print(f"⚠️ Most harmful factors:")
        for harm in top_harmful:
            print(f"   - {harm['event']}: Score {harm['accumulated_score']:.3f}")
    
    print(f"\n🚀 READY FOR REAL DATA TESTING!")
    
    return results

if __name__ == "__main__":
    # Generate sample data and test PTCA
    results = test_ptca_with_sample()
    
    print("\n" + "="*60)
    print("📋 FILES CREATED:")
    print("1. healthcare_sample_data.csv - Human readable sample data")
    print("2. healthcare_ptca_format.json - PTCA algorithm ready format")
    print("\n🎯 NEXT STEPS:")
    print("1. Register for MIMIC-IV access at physionet.org")
    print("2. Complete CITI training (takes 2-3 hours)")
    print("3. Apply sample data insights to real clinical data")
    print("4. Publish results & pitch to investors!")