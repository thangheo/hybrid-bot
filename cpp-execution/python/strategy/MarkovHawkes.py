import numpy as np
import pandas as pd
import time
from collections import deque
import matplotlib.pyplot as plt
from datetime import datetime, timedelta

class MarkovHawkesTrader:
    def __init__(self, config=None):
        """Khởi tạo mô hình trading kết hợp Markov và Hawkes Process"""
        
        # Cấu hình mặc định cho thị trường crypto
        self.config = {
            'num_states': 20,                # Số lượng trạng thái Markov
            'markov_learning_rate': 0.12,    # Tốc độ học của mô hình Markov
            'hawkes_base_intensity': 15.0,   # Cường độ cơ bản của Hawkes
            'event_window_size': 300,        # Số lượng sự kiện tối đa lưu trữ
            'time_window': 3.0,              # Cửa sổ thời gian (giây)
            'alpha_short': 1.2,              # Hệ số kích thích ngắn hạn
            'beta_short': 10.0,              # Tốc độ suy giảm ngắn hạn
            'alpha_medium': 0.5,             # Hệ số kích thích trung hạn
            'beta_medium': 1.5,              # Tốc độ suy giảm trung hạn
            'alpha_long': 0.15,              # Hệ số kích thích dài hạn
            'beta_long': 0.2,                # Tốc độ suy giảm dài hạn
            'prediction_threshold': 0.65,    # Ngưỡng xác suất để đưa ra quyết định
            'position_size_pct': 0.05,       # Kích thước vị thế (% vốn)
            'stop_loss_pct': 0.2,            # Stop loss (% vị thế)
            'take_profit_pct': 0.4,          # Take profit (% vị thế)
            'market_regime_threshold': 0.3    # Ngưỡng phát hiện thay đổi chế độ
        }
        
        # Ghi đè cấu hình nếu được cung cấp
        if config:
            self.config.update(config)
        
        # Khởi tạo ma trận chuyển tiếp Markov (ban đầu là đều nhau)
        self.transition_matrix = np.ones((self.config['num_states'], 
                                         self.config['num_states'])) / self.config['num_states']
        
        # Khởi tạo ma trận kích thích Hawkes (6x6 cho các loại sự kiện)
        # 0: lệnh mua nhỏ, 1: lệnh bán nhỏ, 2: lệnh mua lớn, 3: lệnh bán lớn, 
        # 4: hủy mua, 5: hủy bán
        self.excitation_matrix = np.array([
            [0.7, 0.4, 0.2, 0.1, 0.5, 0.2],
            [0.4, 0.7, 0.1, 0.2, 0.2, 0.5],
            [0.2, 0.1, 0.5, 0.2, 0.3, 0.1],
            [0.1, 0.2, 0.2, 0.5, 0.1, 0.3],
            [0.6, 0.3, 0.4, 0.2, 0.8, 0.4],
            [0.3, 0.6, 0.2, 0.4, 0.4, 0.8]
        ])
        
        # Buffer lưu trữ sự kiện gần đây (deque hiệu quả hơn list)
        self.event_buffer = deque(maxlen=self.config['event_window_size'])
        
        # Trạng thái hiện tại và trước đó
        self.current_state = None
        self.previous_state = None
        
        # Dữ liệu theo dõi
        self.prediction_errors = deque(maxlen=100)
        self.predictions = deque(maxlen=100)
        self.actual_outcomes = deque(maxlen=100)
        
        # Thông tin tài khoản
        self.balance = 10000.0  # Vốn ban đầu $10,000
        self.positions = []     # Danh sách các vị thế đang mở
        self.trade_history = [] # Lịch sử giao dịch
        
        # Bảng tra cứu cho hàm mũ (tối ưu hiệu suất)
        self._initialize_exp_lookup()
        
    def _initialize_exp_lookup(self):
        """Khởi tạo bảng tra cứu cho hàm mũ để tăng tốc tính toán"""
        # Tạo bảng tra cứu cho 3 giây với độ phân giải 1ms
        max_time = self.config['time_window']
        resolution = 0.001  # 1ms
        table_size = int(max_time / resolution) + 1
        
        self.exp_lookup_short = np.zeros(table_size)
        self.exp_lookup_medium = np.zeros(table_size)
        self.exp_lookup_long = np.zeros(table_size)
        
        for i in range(table_size):
            t = i * resolution
            self.exp_lookup_short[i] = np.exp(-self.config['beta_short'] * t)
            self.exp_lookup_medium[i] = np.exp(-self.config['beta_medium'] * t)
            self.exp_lookup_long[i] = np.exp(-self.config['beta_long'] * t)
    
    def get_decay_factor(self, dt, decay_type='short'):
        """Lấy hệ số suy giảm từ bảng tra cứu"""
        if dt >= self.config['time_window']:
            return 0.0
        
        idx = min(int(dt * 1000), len(self.exp_lookup_short) - 1)
        
        if decay_type == 'short':
            return self.exp_lookup_short[idx]
        elif decay_type == 'medium':
            return self.exp_lookup_medium[idx]
        elif decay_type == 'long':
            return self.exp_lookup_long[idx]
        else:
            return 0.0
    
    def calculate_market_state(self, orderbook, recent_trades):
        """Tính toán trạng thái thị trường dựa trên orderbook và lịch sử giao dịch gần đây"""
        # Trong phiên bản demo, chỉ sử dụng 5 thuộc tính để xác định trạng thái
        # Thực tế sẽ phức tạp hơn nhiều
        
        # 1. Tính toán imbalance orderbook
        bid_volume = sum(level['volume'] for level in orderbook['bids'][:5])
        ask_volume = sum(level['volume'] for level in orderbook['asks'][:5])
        
        if bid_volume > ask_volume * 1.5:
            imbalance = 0  # Áp lực mua mạnh
        elif ask_volume > bid_volume * 1.5:
            imbalance = 2  # Áp lực bán mạnh
        else:
            imbalance = 1  # Cân bằng
        
        # 2. Tính xu hướng giá gần đây
        if len(recent_trades) < 20:
            trend = 1  # Không đủ dữ liệu, giả định sideways
        else:
            prices = [trade['price'] for trade in recent_trades[-20:]]
            first_prices = np.mean(prices[:10])
            last_prices = np.mean(prices[-10:])
            
            if last_prices > first_prices * 1.002:
                trend = 0  # Xu hướng tăng
            elif first_prices > last_prices * 1.002:
                trend = 2  # Xu hướng giảm
            else:
                trend = 1  # Đi ngang
        
        # 3. Tính biến động
        if len(recent_trades) < 30:
            volatility = 0  # Không đủ dữ liệu, giả định thấp
        else:
            prices = np.array([trade['price'] for trade in recent_trades[-30:]])
            returns = np.diff(prices) / prices[:-1]
            vol = np.std(returns) * 100  # Biến động theo %
            
            if vol > 0.1:
                volatility = 2  # Biến động cao
            elif vol > 0.05:
                volatility = 1  # Biến động trung bình
            else:
                volatility = 0  # Biến động thấp
        
        # 4. Độ sâu thị trường
        total_depth = bid_volume + ask_volume
        if total_depth < 100:  # Ngưỡng này phụ thuộc vào cặp tiền
            depth = 1  # Thị trường nông
        else:
            depth = 0  # Thị trường sâu
        
        # 5. Tốc độ giao dịch
        if len(recent_trades) < 20:
            trade_speed = 0 
        else:
            oldest_time = recent_trades[-20]['timestamp']
            newest_time = recent_trades[-1]['timestamp']
            
            if newest_time - oldest_time < 5.0:  # 20 giao dịch trong 5 giây
                trade_speed = 2  # Nhanh
            elif newest_time - oldest_time < 15.0:  # 20 giao dịch trong 15 giây
                trade_speed = 1  # Trung bình
            else:
                trade_speed = 0  # Chậm
        
        # Kết hợp các trạng thái để tạo trạng thái tổng thể
        # Sử dụng phép nhân trọng số để tạo ID trạng thái duy nhất
        state_id = (imbalance * 3**4 + trend * 3**3 + volatility * 3**2 +
                    depth * 3**1 + trade_speed * 3**0)
        
        # Map về khoảng [0, num_states-1]
        return min(state_id, self.config['num_states'] - 1)
    
    def predict_next_state(self, current_state):
        """Dự đoán trạng thái tiếp theo dựa trên ma trận chuyển tiếp"""
        return np.argmax(self.transition_matrix[current_state])
    
    def calculate_hawkes_intensity(self, event_type, current_time):
        """Tính toán cường độ Hawkes cho loại sự kiện được chỉ định"""
        intensity = self.config['hawkes_base_intensity']
        
        if not self.event_buffer:
            return intensity
        
        # Tính toán ảnh hưởng từ các sự kiện trước đó
        for event in self.event_buffer:
            dt = current_time - event['timestamp']
            
            if dt <= 0 or dt >= self.config['time_window']:
                continue
                
            # Tính toán hệ số suy giảm tổng hợp từ ba thành phần
            decay_short = self.get_decay_factor(dt, 'short')
            decay_medium = self.get_decay_factor(dt, 'medium')
            decay_long = self.get_decay_factor(dt, 'long')
            
            # Tính toán yếu tố kích thích
            excitation = (
                self.config['alpha_short'] * decay_short +
                self.config['alpha_medium'] * decay_medium +
                self.config['alpha_long'] * decay_long
            )
            
            # Thêm vào cường độ tổng thể
            intensity += self.excitation_matrix[event['type']][event_type] * excitation
        
        return max(0.0, intensity)  # Đảm bảo không âm
    
    def predict_next_event(self, current_time):
        """Dự đoán loại sự kiện tiếp theo và thời gian của nó"""
        # Tính cường độ cho mỗi loại sự kiện
        intensities = [self.calculate_hawkes_intensity(i, current_time) 
                      for i in range(len(self.excitation_matrix))]
        
        # Tính xác suất cho mỗi loại sự kiện
        total_intensity = sum(intensities)
        if total_intensity > 0:
            probs = [i / total_intensity for i in intensities]
        else:
            probs = [1.0 / len(intensities)] * len(intensities)
        
        # Dự đoán loại sự kiện tiếp theo
        next_event_type = np.argmax(probs)
        
        # Dự đoán thời gian của sự kiện tiếp theo
        # (sử dụng phân phối mũ với cường độ tổng thể)
        next_event_time = current_time + np.random.exponential(1.0 / total_intensity)
        
        return {
            'type': next_event_type,
            'time': next_event_time,
            'probability': probs[next_event_type]
        }
    
    def update_models(self, new_event, orderbook, recent_trades):
        """Cập nhật mô hình Markov và Hawkes dựa trên sự kiện mới nhận được"""
        start_time = time.time()
        
        # 1. Cập nhật buffer sự kiện
        self.event_buffer.append(new_event)
        
        # 2. Xác định trạng thái thị trường hiện tại
        self.previous_state = self.current_state
        self.current_state = self.calculate_market_state(orderbook, recent_trades)
        
        # 3. Cập nhật ma trận chuyển tiếp Markov
        if self.previous_state is not None:
            alpha = self.config['markov_learning_rate']
            self.transition_matrix[self.previous_state, self.current_state] = (
                (1 - alpha) * self.transition_matrix[self.previous_state, self.current_state] + alpha
            )
            
            # Chuẩn hóa hàng để tổng = 1
            row_sum = np.sum(self.transition_matrix[self.previous_state])
            if row_sum > 0:
                self.transition_matrix[self.previous_state] /= row_sum
        
        # 4. Theo dõi hiệu suất dự đoán
        if hasattr(self, 'last_prediction') and self.previous_state is not None:
            predicted_state = self.last_prediction
            actual_state = self.current_state
            
            # Lưu kết quả dự đoán
            self.predictions.append(predicted_state)
            self.actual_outcomes.append(actual_state)
            
            # Tính sai số dự đoán
            error = 1.0 if predicted_state != actual_state else 0.0
            self.prediction_errors.append(error)
            
            # Kiểm tra thay đổi chế độ thị trường
            if len(self.prediction_errors) >= 50:
                recent_errors = list(self.prediction_errors)[-50:]
                error_rate = sum(recent_errors) / len(recent_errors)
                
                if error_rate > self.config['market_regime_threshold']:
                    # Thay đổi chế độ thị trường phát hiện, tăng tốc độ học
                    self.config['markov_learning_rate'] = min(
                        0.5, self.config['markov_learning_rate'] * 2.0)
                    print(f"Phát hiện thay đổi chế độ! Tăng tốc độ học lên {self.config['markov_learning_rate']:.3f}")
                else:
                    # Thị trường ổn định, giảm dần tốc độ học
                    self.config['markov_learning_rate'] = max(
                        0.05, self.config['markov_learning_rate'] * 0.99)
        
        # 5. Dự đoán trạng thái tiếp theo cho lần sau
        self.last_prediction = self.predict_next_state(self.current_state)
        
        # Đo thời gian xử lý
        processing_time = (time.time() - start_time) * 1000  # Chuyển đổi sang ms
        return processing_time
    
    def make_trading_decision(self, current_price, current_time):
        """Đưa ra quyết định giao dịch dựa trên mô hình kết hợp"""
        if self.current_state is None:
            return None
        
        # 1. Dự đoán trạng thái thị trường tiếp theo
        next_state = self.predict_next_state(self.current_state)
        state_confidence = self.transition_matrix[self.current_state, next_state]
        
        # 2. Dự đoán sự kiện orderbook tiếp theo
        # Quan tâm đến sự kiện mua lớn (2) và bán lớn (3)
        buy_intensity = self.calculate_hawkes_intensity(2, current_time)
        sell_intensity = self.calculate_hawkes_intensity(3, current_time)
        
        # 3. Tính toán tín hiệu tổng hợp
        buy_signal = 0
        sell_signal = 0
        
        # Trạng thái thị trường thúc đẩy mua
        if next_state < self.config['num_states'] // 3:  # Giả định trạng thái nhỏ = xu hướng tăng
            buy_signal += 0.6 * state_confidence
        # Trạng thái thị trường thúc đẩy bán
        elif next_state > 2 * self.config['num_states'] // 3:  # Giả định trạng thái lớn = xu hướng giảm
            sell_signal += 0.6 * state_confidence
            
        # Cường độ Hawkes thúc đẩy mua/bán
        total_intensity = buy_intensity + sell_intensity
        if total_intensity > 0:
            buy_signal += 0.4 * (buy_intensity / total_intensity)
            sell_signal += 0.4 * (sell_intensity / total_intensity)
        
        # 4. Đưa ra quyết định giao dịch
        decision = None
        
        if buy_signal > self.config['prediction_threshold']:
            # Tín hiệu mua đủ mạnh
            position_size = self.balance * self.config['position_size_pct']
            stop_loss = current_price * (1 - self.config['stop_loss_pct'])
            take_profit = current_price * (1 + self.config['take_profit_pct'])
            
            decision = {
                'action': 'BUY',
                'price': current_price,
                'size': position_size,
                'stop_loss': stop_loss,
                'take_profit': take_profit,
                'confidence': buy_signal,
                'timestamp': current_time
            }
            
        elif sell_signal > self.config['prediction_threshold']:
            # Tín hiệu bán đủ mạnh
            position_size = self.balance * self.config['position_size_pct']
            stop_loss = current_price * (1 + self.config['stop_loss_pct'])
            take_profit = current_price * (1 - self.config['take_profit_pct'])
            
            decision = {
                'action': 'SELL',
                'price': current_price,
                'size': position_size,
                'stop_loss': stop_loss,
                'take_profit': take_profit,
                'confidence': sell_signal,
                'timestamp': current_time
            }
            
        return decision
    
    def execute_trade(self, decision, fee_rate=0.0006):
        """Thực hiện giao dịch và cập nhật trạng thái tài khoản"""
        if decision is None:
            return
        
        # Tính phí giao dịch
        fee = decision['size'] * fee_rate
        
        # Ghi nhận vị thế mới
        position = {
            'id': len(self.trade_history) + 1,
            'action': decision['action'],
            'entry_price': decision['price'],
            'size': decision['size'],
            'stop_loss': decision['stop_loss'],
            'take_profit': decision['take_profit'],
            'fee': fee,
            'open_time': decision['timestamp'],
            'status': 'OPEN',
            'close_price': None,
            'close_time': None,
            'pnl': None
        }
        
        # Cập nhật số dư
        self.balance -= decision['size'] + fee
        
        # Thêm vào danh sách vị thế đang mở
        self.positions.append(position)
        
        # Thêm vào lịch sử giao dịch
        self.trade_history.append(position)
        
        print(f"Mở vị thế #{position['id']}: {position['action']} tại {position['entry_price']:.2f}, "
              f"Size: ${position['size']:.2f}, SL: {position['stop_loss']:.2f}, TP: {position['take_profit']:.2f}")
        
    def update_positions(self, current_price, current_time, fee_rate=0.0006):
        """Cập nhật trạng thái của các vị thế đang mở"""
        if not self.positions:
            return
        
        positions_to_remove = []
        
        for pos in self.positions:
            # Kiểm tra điều kiện đóng vị thế
            closed = False
            close_reason = None
            
            if pos['action'] == 'BUY':
                # Kiểm tra stop loss
                if current_price <= pos['stop_loss']:
                    closed = True
                    close_reason = 'STOP_LOSS'
                # Kiểm tra take profit
                elif current_price >= pos['take_profit']:
                    closed = True
                    close_reason = 'TAKE_PROFIT'
            else:  # SELL
                # Kiểm tra stop loss
                if current_price >= pos['stop_loss']:
                    closed = True
                    close_reason = 'STOP_LOSS'
                # Kiểm tra take profit
                elif current_price <= pos['take_profit']:
                    closed = True
                    close_reason = 'TAKE_PROFIT'
            
            # Xử lý vị thế đóng
            if closed:
                # Tính lợi nhuận/thua lỗ
                pos_size = pos['size']
                entry_price = pos['entry_price']
                
                if pos['action'] == 'BUY':
                    pnl = pos_size * (current_price - entry_price) / entry_price
                else:  # SELL
                    pnl = pos_size * (entry_price - current_price) / entry_price
                
                # Trừ phí đóng vị thế
                close_fee = pos_size * fee_rate
                pnl -= close_fee
                
                # Cập nhật trạng thái vị thế
                pos['status'] = 'CLOSED'
                pos['close_price'] = current_price
                pos['close_time'] = current_time
                pos['pnl'] = pnl
                pos['close_reason'] = close_reason
                
                # Cập nhật số dư
                self.balance += pos_size + pnl
                
                # Thêm vào danh sách cần xóa
                positions_to_remove.append(pos)
                
                print(f"Đóng vị thế #{pos['id']}: {pos['action']} tại {current_price:.2f}, "
                      f"P&L: ${pnl:.2f} ({close_reason})")
        
        # Xóa các vị thế đã đóng
        for pos in positions_to_remove:
            self.positions.remove(pos)
    
    def simulate_trading(self, market_data, fee_rate=0.0006):
        """Mô phỏng giao dịch với dữ liệu thị trường"""
        results = {
            'timestamps': [],
            'prices': [],
            'balance': [],
            'states': [],
            'processing_times': [],
            'predictions': [],
            'buy_signals': [],
            'sell_signals': []
        }
        
        for t, data in enumerate(market_data):
            current_time = data['timestamp']
            current_price = data['price']
            orderbook = data['orderbook']
            recent_trades = market_data[max(0, t-50):t+1]
            
            # Cập nhật mô hình
            processing_time = self.update_models(data['event'], orderbook, recent_trades)
            
            # Đưa ra quyết định giao dịch
            decision = self.make_trading_decision(current_price, current_time)
            
            # Cập nhật vị thế hiện tại
            self.update_positions(current_price, current_time, fee_rate)
            
            # Thực hiện giao dịch mới (nếu có)
            if decision and len(self.positions) < 3:  # Giới hạn tối đa 3 vị thế cùng lúc
                self.execute_trade(decision, fee_rate)
            
            # Lưu kết quả
            results['timestamps'].append(current_time)
            results['prices'].append(current_price)
            results['balance'].append(self.balance)
            results['states'].append(self.current_state if self.current_state is not None else 0)
            results['processing_times'].append(processing_time)
            results['predictions'].append(self.last_prediction if hasattr(self, 'last_prediction') else 0)
            
            # Lưu tín hiệu mua/bán
            buy_intensity = self.calculate_hawkes_intensity(2, current_time)
            sell_intensity = self.calculate_hawkes_intensity(3, current_time)
            total_intensity = max(buy_intensity + sell_intensity, 1e-6)
            
            results['buy_signals'].append(buy_intensity / total_intensity)
            results['sell_signals'].append(sell_intensity / total_intensity)
            
        return results
    
    def visualize_results(self, results):
        """Trực quan hóa kết quả giao dịch"""
        fig, axes = plt.subplots(4, 1, figsize=(14, 16), sharex=True)
        
        # Chuyển đổi timestamps thành datetime để hiển thị đẹp hơn
        timestamps = [datetime.fromtimestamp(t) for t in results['timestamps']]
        
        # Biểu đồ 1: Giá và các trạng thái
        ax1 = axes[0]
        ax1.plot(timestamps, results['prices'], label='Price', color='blue')
        ax1.set_ylabel('Price')
        ax1.set_title('Price Chart with Trading Signals')
        
        # Thêm trạng thái và dự đoán
        ax1_twin = ax1.twinx()
        ax1_twin.scatter(timestamps, results['states'], 
                         label='Market State', alpha=0.3, color='green', s=10)
        ax1_twin.scatter(timestamps, results['predictions'], 
                         label='Predicted State', alpha=0.3, color='red', s=10)
        ax1_twin.set_ylabel('State ID')
        
        # Biểu đồ 2: Tín hiệu mua/bán
        ax2 = axes[1]
        ax2.plot(timestamps, results['buy_signals'], label='Buy Signal', color='green')
        ax2.plot(timestamps, results['sell_signals'], label='Sell Signal', color='red')
        ax2.set_ylabel('Signal Strength')
        ax2.set_title('Buy/Sell Signals')
        ax2.axhline(y=self.config['prediction_threshold'], linestyle='--', color='gray')
        ax2.legend()
        
        # Biểu đồ 3: Số dư tài khoản
        ax3 = axes[2]
        ax3.plot(timestamps, results['balance'], label='Account Balance', color='purple')
        ax3.set_ylabel('Balance ($)')
        ax3.set_title('Account Balance')
        
        # Biểu đồ 4: Thời gian xử lý
        ax4 = axes[3]
        ax4.plot(timestamps, results['processing_times'], label='Processing Time', color='orange')
        ax4.set_ylabel('Time (ms)')
        ax4.set_xlabel('Time')
        ax4.set_title('Processing Time per Update')
        ax4.axhline(y=10, linestyle='--', color='red', label='10ms Threshold')
        
        # Hiển thị giao dịch trên biểu đồ giá
        for trade in self.trade_history:
            if trade['action'] == 'BUY':
                marker = '^'
                color = 'green'
            else:  # SELL
                marker = 'v'
                color = 'red'
            
            # Vẽ điểm vào lệnh
            entry_time = datetime.fromtimestamp(trade['open_time'])
            ax1.scatter(entry_time, trade['entry_price'], 
                       marker=marker, color=color, s=100)
            
            # Vẽ điểm đóng lệnh nếu đã đóng
            if trade['status'] == 'CLOSED':
                close_time = datetime.fromtimestamp(trade['close_time'])
                if trade['pnl'] > 0:
                    close_color = 'green'
                else:
                    close_color = 'red'
                
                ax1.scatter(close_time, trade['close_price'], 
                           marker='o', color=close_color, s=100)
                
                # Vẽ đường nối giữa mở và đóng lệnh
                ax1.plot([entry_time, close_time], 
                        [trade['entry_price'], trade['close_price']], 
                        linestyle='--', color=color, alpha=0.5)
        
        fig.tight_layout()
        plt.show()

    def get_performance_metrics(self):
        """Tính toán các chỉ số hiệu suất của chiến lược giao dịch"""
        if not self.trade_history or all(t['status'] != 'CLOSED' for t in self.trade_history):
            return {"error": "Không có đủ dữ liệu giao dịch để tính hiệu suất"}
        
        closed_trades = [t for t in self.trade_history if t['status'] == 'CLOSED']
        
        # Số lượng giao dịch
        total_trades = len(closed_trades)
        profitable_trades = sum(1 for t in closed_trades if t['pnl'] > 0)
        
        # Tỷ lệ thắng
        win_rate = profitable_trades / total_trades if total_trades > 0 else 0
        
        # P&L
        total_pnl = sum(t['pnl'] for t in closed_trades)
        average_pnl = total_pnl / total_trades if total_trades > 0 else 0
        
        # Profit factor
        gross_profit = sum(t['pnl'] for t in closed_trades if t['pnl'] > 0)
        gross_loss = abs(sum(t['pnl'] for t in closed_trades if t['pnl'] < 0))
        profit_factor = gross_profit / gross_loss if gross_loss > 0 else float('inf')
        
        # Thời gian trung bình giữ vị thế
        avg_holding_time = np.mean([t['close_time'] - t['open_time'] for t in closed_trades])
        
        # Model performance
        if len(self.predictions) > 0 and len(self.actual_outcomes) > 0:
            model_accuracy = sum(1 for p, a in zip(self.predictions, self.actual_outcomes) 
                                if p == a) / len(self.predictions)
        else:
            model_accuracy = 0
        
        return {
            "total_trades": total_trades,
            "win_rate": win_rate,
            "total_pnl": total_pnl,
            "average_pnl": average_pnl,
            "profit_factor": profit_factor,
            "avg_holding_time": avg_holding_time,
            "model_accuracy": model_accuracy,
            "initial_balance": 10000.0,
            "final_balance": self.balance,
            "return_pct": (self.balance - 10000.0) / 10000.0 * 100
        }

# Hàm tạo dữ liệu mô phỏng
def generate_simulated_data(num_points=1000, volatility=0.002):
    """Tạo dữ liệu mô phỏng cho thị trường crypto"""
    data = []
    
    # Giá ban đầu
    price = 50000.0  # BTC/USDT
    timestamp = time.time()
    
    # Orderbook ban đầu
    def generate_orderbook(current_price, depth=10):
        bids = []
        asks = []
        
        # Tạo các mức giá mua
        for i in range(1, depth+1):
            bid_price = current_price * (1 - 0.0001 * i)
            volume = np.random.exponential(2.0) + 0.5
            bids.append({"price": bid_price, "volume": volume})
        
        # Tạo các mức giá bán
        for i in range(1, depth+1):
            ask_price = current_price * (1 + 0.0001 * i)
            volume = np.random.exponential(2.0) + 0.5
            asks.append({"price": ask_price, "volume": volume})
        
        return {"bids": bids, "asks": asks}
    
    # Tạo dữ liệu
    for i in range(num_points):
        # Cập nhật giá
        price_change = np.random.normal(0, volatility) * price
        price += price_change
        
        # Thêm một xu hướng
        if i > num_points // 3 and i < 2 * num_points // 3:
            price += 0.0001 * price  # Xu hướng tăng nhẹ
        else:
            price -= 0.00005 * price  # Xu hướng giảm nhẹ
        
        # Thêm một số biến động đột ngột
        if np.random.random() < 0.01:  # 1% cơ hội có biến động lớn
            price *= np.random.choice([0.98, 1.02])  # +/- 2%
        
        # Cập nhật timestamp
        timestamp += np.random.exponential(0.5)  # Trung bình 0.5 giây giữa các sự kiện
        
        # Tạo orderbook
        orderbook = generate_orderbook(price)
        
        # Tạo sự kiện (loại 0-5)
        event_type = np.random.choice([0, 1, 2, 3, 4, 5], p=[0.3, 0.3, 0.1, 0.1, 0.1, 0.1])
        
        # Thêm vào dữ liệu
        data.append({
            "timestamp": timestamp,
            "price": price,
            "orderbook": orderbook,
            "event": {
                "type": event_type,
                "timestamp": timestamp
            }
        })
    
    return data

# Demo sử dụng
if __name__ == "__main__":
    # Tạo dữ liệu mô phỏng
    print("Tạo dữ liệu mô phỏng...")
    market_data = generate_simulated_data(num_points=5000, volatility=0.001)
    
    # Khởi tạo mô hình
    print("Khởi tạo mô hình trading...")
    trader = MarkovHawkesTrader()
    
    # Chạy mô phỏng
    print("Bắt đầu mô phỏng giao dịch...")
    results = trader.simulate_trading(market_data, fee_rate=0.0006)
    
    # Hiển thị kết quả
    print("\nKết quả hiệu suất:")
    metrics = trader.get_performance_metrics()
    for key, value in metrics.items():
        print(f"{key}: {value}")
    
    # Trực quan hóa kết quả
    trader.visualize_results(results)