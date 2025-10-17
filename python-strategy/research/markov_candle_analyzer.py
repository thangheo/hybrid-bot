import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from collections import defaultdict, Counter
import json

class CandleMarkovAnalyzer:
    def __init__(self, csv_path):
        """
        Khởi tạo analyzer với file CSV từ Binance
        """
        self.csv_path = csv_path
        self.df = None
        self.states = []
        self.transition_counts = defaultdict(lambda: defaultdict(int))
        self.transition_probs = {}
        self.state_frequencies = Counter()
        
    def load_data(self):
        """
        Đọc dữ liệu từ Binance CSV
        Format: timestamp, open, high, low, close, volume, ...
        """
        print(f"Đang đọc dữ liệu từ {self.csv_path}...")
        self.df = pd.read_csv(self.csv_path)
        
        # Kiểm tra các cột cần thiết
        required_cols = ['open', 'close']
        for col in required_cols:
            if col not in self.df.columns:
                raise ValueError(f"Thiếu cột '{col}' trong CSV")
        
        print(f"Đã load {len(self.df)} nến")
        return self
    
    def candle_to_binary(self, open_price, close_price):
        """
        Chuyển đổi nến thành binary
        Xanh (open < close) -> 1
        Đỏ (open >= close) -> 0
        """
        return '1' if close_price > open_price else '0'
    
    def create_state(self, candles):
        """
        Tạo state từ 6 nến liên tiếp
        Input: DataFrame với 6 rows
        Output: string binary 6 ký tự, ví dụ "001001"
        """
        state = ''
        for idx, row in candles.iterrows():
            state += self.candle_to_binary(row['open'], row['close'])
        return state
    
    def extract_states(self, window_size=6):
        """
        Trích xuất tất cả states bằng sliding window
        """
        print(f"Đang trích xuất states với window size = {window_size}...")
        
        if len(self.df) < window_size:
            raise ValueError(f"Không đủ dữ liệu. Cần ít nhất {window_size} nến")
        
        self.states = []
        
        # Sliding window với overlap
        for i in range(len(self.df) - window_size + 1):
            window = self.df.iloc[i:i+window_size]
            state = self.create_state(window)
            self.states.append(state)
            self.state_frequencies[state] += 1
        
        print(f"Đã tạo {len(self.states)} states")
        print(f"Số states unique: {len(self.state_frequencies)}")
        return self
    
    def calculate_transitions(self):
        """
        Tính transition counts và probabilities
        """
        print("Đang tính transition probabilities...")
        
        # Đếm transitions
        for i in range(len(self.states) - 1):
            current_state = self.states[i]
            next_state = self.states[i + 1]
            self.transition_counts[current_state][next_state] += 1
        
        # Tính probabilities
        self.transition_probs = {}
        for current_state, next_states in self.transition_counts.items():
            total = sum(next_states.values())
            self.transition_probs[current_state] = {
                next_state: count / total 
                for next_state, count in next_states.items()
            }
        
        print(f"Đã tính transition cho {len(self.transition_probs)} states")
        return self
    
    def get_transition_matrix(self):
        """
        Tạo full transition matrix 64x64
        """
        # Tất cả states có thể (2^6 = 64)
        all_possible_states = [format(i, '06b') for i in range(64)]
        
        # Khởi tạo matrix
        matrix = np.zeros((64, 64))
        
        for i, state_from in enumerate(all_possible_states):
            if state_from in self.transition_probs:
                for state_to, prob in self.transition_probs[state_from].items():
                    j = int(state_to, 2)  # Convert binary string to index
                    matrix[i][j] = prob
        
        return matrix, all_possible_states
    
    def save_results(self, output_prefix="markov_results"):
        """
        Lưu kết quả ra files
        """
        print(f"\nĐang lưu kết quả với prefix '{output_prefix}'...")
        
        # 1. Lưu transition matrix dạng CSV
        matrix, states = self.get_transition_matrix()
        df_matrix = pd.DataFrame(matrix, index=states, columns=states)
        csv_file = f"{output_prefix}_transition_matrix.csv"
        df_matrix.to_csv(csv_file)
        print(f"✓ Đã lưu transition matrix: {csv_file}")
        
        # 2. Lưu state frequencies
        freq_file = f"{output_prefix}_state_frequencies.csv"
        df_freq = pd.DataFrame([
            {'state': state, 'count': count, 'frequency': count/len(self.states)}
            for state, count in self.state_frequencies.most_common()
        ])
        df_freq.to_csv(freq_file, index=False)
        print(f"✓ Đã lưu state frequencies: {freq_file}")
        
        # 3. Lưu top transitions
        top_file = f"{output_prefix}_top_transitions.csv"
        top_transitions = []
        for state_from, next_states in self.transition_probs.items():
            for state_to, prob in next_states.items():
                count = self.transition_counts[state_from][state_to]
                top_transitions.append({
                    'from_state': state_from,
                    'to_state': state_to,
                    'probability': prob,
                    'count': count
                })
        
        df_top = pd.DataFrame(top_transitions)
        df_top = df_top.sort_values('probability', ascending=False)
        df_top.to_csv(top_file, index=False)
        print(f"✓ Đã lưu top transitions: {top_file}")
        
        # 4. Lưu summary JSON
        summary = {
            'total_candles': len(self.df),
            'total_states': len(self.states),
            'unique_states': len(self.state_frequencies),
            'total_transitions': sum(sum(d.values()) for d in self.transition_counts.values()),
            'top_10_states': [
                {'state': state, 'count': count} 
                for state, count in self.state_frequencies.most_common(10)
            ]
        }
        json_file = f"{output_prefix}_summary.json"
        with open(json_file, 'w', encoding='utf-8') as f:
            json.dump(summary, f, indent=2, ensure_ascii=False)
        print(f"✓ Đã lưu summary: {json_file}")
        
        return self
    
    def visualize(self, output_prefix="markov_results"):
        """
        Tạo visualizations
        """
        print("\nĐang tạo visualizations...")
        
        # Tạo figure với multiple subplots
        fig = plt.figure(figsize=(20, 12))
        
        # 1. Heatmap của transition matrix (chỉ các states có data)
        ax1 = plt.subplot(2, 2, 1)
        active_states = sorted(self.transition_probs.keys())
        if len(active_states) > 0:
            # Tạo matrix cho active states only
            matrix_size = len(active_states)
            active_matrix = np.zeros((matrix_size, matrix_size))
            
            for i, state_from in enumerate(active_states):
                for j, state_to in enumerate(active_states):
                    if state_to in self.transition_probs[state_from]:
                        active_matrix[i][j] = self.transition_probs[state_from][state_to]
            
            # Giới hạn số states hiển thị nếu quá nhiều
            display_limit = 40
            if matrix_size <= display_limit:
                sns.heatmap(active_matrix, xticklabels=active_states, 
                           yticklabels=active_states, cmap='YlOrRd', 
                           ax=ax1, cbar_kws={'label': 'Probability'})
                ax1.set_title(f'Transition Probability Heatmap\n({matrix_size} active states)', 
                             fontsize=12, fontweight='bold')
            else:
                # Nếu quá nhiều states, chỉ hiển thị top states
                top_states = [s for s, _ in self.state_frequencies.most_common(display_limit)]
                state_to_idx = {s: i for i, s in enumerate(top_states)}
                
                top_matrix = np.zeros((display_limit, display_limit))
                for i, state_from in enumerate(top_states):
                    if state_from in self.transition_probs:
                        for state_to, prob in self.transition_probs[state_from].items():
                            if state_to in state_to_idx:
                                j = state_to_idx[state_to]
                                top_matrix[i][j] = prob
                
                sns.heatmap(top_matrix, xticklabels=top_states, 
                           yticklabels=top_states, cmap='YlOrRd', 
                           ax=ax1, cbar_kws={'label': 'Probability'})
                ax1.set_title(f'Transition Probability Heatmap\n(Top {display_limit} states)', 
                             fontsize=12, fontweight='bold')
            
            ax1.set_xlabel('To State', fontweight='bold')
            ax1.set_ylabel('From State', fontweight='bold')
            plt.setp(ax1.get_xticklabels(), rotation=45, ha='right', fontsize=8)
            plt.setp(ax1.get_yticklabels(), rotation=0, fontsize=8)
        
        # 2. Top 20 state frequencies
        ax2 = plt.subplot(2, 2, 2)
        top_20_states = self.state_frequencies.most_common(20)
        states_list = [s[0] for s in top_20_states]
        counts_list = [s[1] for s in top_20_states]
        
        bars = ax2.barh(range(len(states_list)), counts_list, color='steelblue')
        ax2.set_yticks(range(len(states_list)))
        ax2.set_yticklabels(states_list, fontsize=9)
        ax2.set_xlabel('Frequency', fontweight='bold')
        ax2.set_title('Top 20 Most Frequent States', fontsize=12, fontweight='bold')
        ax2.invert_yaxis()
        
        # Thêm values trên bars
        for i, (bar, count) in enumerate(zip(bars, counts_list)):
            ax2.text(count, i, f' {count}', va='center', fontsize=8)
        
        # 3. Distribution of transition probabilities
        ax3 = plt.subplot(2, 2, 3)
        all_probs = []
        for next_states in self.transition_probs.values():
            all_probs.extend(next_states.values())
        
        if all_probs:
            ax3.hist(all_probs, bins=50, color='coral', edgecolor='black', alpha=0.7)
            ax3.set_xlabel('Transition Probability', fontweight='bold')
            ax3.set_ylabel('Count', fontweight='bold')
            ax3.set_title('Distribution of Transition Probabilities', 
                         fontsize=12, fontweight='bold')
            ax3.axvline(np.mean(all_probs), color='red', linestyle='--', 
                       linewidth=2, label=f'Mean: {np.mean(all_probs):.4f}')
            ax3.legend()
        
        # 4. Top 15 transitions
        ax4 = plt.subplot(2, 2, 4)
        top_transitions = []
        for state_from, next_states in self.transition_probs.items():
            for state_to, prob in next_states.items():
                top_transitions.append((f"{state_from}→{state_to}", prob))
        
        top_transitions.sort(key=lambda x: x[1], reverse=True)
        top_15 = top_transitions[:15]
        
        trans_labels = [t[0] for t in top_15]
        trans_probs = [t[1] for t in top_15]
        
        bars = ax4.barh(range(len(trans_labels)), trans_probs, color='mediumseagreen')
        ax4.set_yticks(range(len(trans_labels)))
        ax4.set_yticklabels(trans_labels, fontsize=8)
        ax4.set_xlabel('Probability', fontweight='bold')
        ax4.set_title('Top 15 Highest Probability Transitions', 
                     fontsize=12, fontweight='bold')
        ax4.invert_yaxis()
        
        # Thêm values trên bars
        for i, (bar, prob) in enumerate(zip(bars, trans_probs)):
            ax4.text(prob, i, f' {prob:.3f}', va='center', fontsize=8)
        
        plt.tight_layout()
        
        # Lưu figure
        viz_file = f"{output_prefix}_visualization.png"
        plt.savefig(viz_file, dpi=300, bbox_inches='tight')
        print(f"✓ Đã lưu visualization: {viz_file}")
        
        plt.show()
        
        return self
    
    def print_summary(self):
        """
        In summary ra console
        """
        print("\n" + "="*60)
        print("SUMMARY - MARKOV CHAIN ANALYSIS")
        print("="*60)
        print(f"Tổng số nến: {len(self.df)}")
        print(f"Tổng số states: {len(self.states)}")
        print(f"Số states unique: {len(self.state_frequencies)}")
        print(f"Tổng số transitions: {sum(sum(d.values()) for d in self.transition_counts.values())}")
        
        print("\n--- Top 10 States phổ biến nhất ---")
        for i, (state, count) in enumerate(self.state_frequencies.most_common(10), 1):
            freq = count / len(self.states) * 100
            print(f"{i:2d}. {state}: {count:5d} lần ({freq:5.2f}%)")
        
        print("\n--- Top 10 Transitions có xác suất cao nhất ---")
        all_trans = []
        for state_from, next_states in self.transition_probs.items():
            for state_to, prob in next_states.items():
                all_trans.append((state_from, state_to, prob))
        
        all_trans.sort(key=lambda x: x[2], reverse=True)
        for i, (s_from, s_to, prob) in enumerate(all_trans[:10], 1):
            print(f"{i:2d}. {s_from} → {s_to}: {prob:.4f}")
        
        print("="*60 + "\n")
        
        return self


def main():
    """
    Hàm main để chạy chương trình
    """
    import sys
    
    # Kiểm tra argument
    if len(sys.argv) < 2:
        print("Usage: python markov_candle_analyzer.py <csv_file_path> [output_prefix]")
        print("\nExample:")
        print("  python markov_candle_analyzer.py BTCUSDT_1h.csv")
        print("  python markov_candle_analyzer.py BTCUSDT_1h.csv btc_analysis")
        sys.exit(1)
    
    csv_path = sys.argv[1]
    output_prefix = sys.argv[2] if len(sys.argv) > 2 else "markov_results"
    
    try:
        # Tạo analyzer và chạy pipeline
        analyzer = CandleMarkovAnalyzer(csv_path)
        
        analyzer.load_data() \
                .extract_states(window_size=6) \
                .calculate_transitions() \
                .print_summary() \
                .save_results(output_prefix) \
                .visualize(output_prefix)
        
        print("\n✅ Hoàn thành! Đã tạo tất cả files và visualizations.")
        
    except Exception as e:
        print(f"\n❌ Lỗi: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
