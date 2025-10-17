# Markov Chain Analysis cho Candlestick Data

## Mô tả
Chương trình phân tích dữ liệu nến (candlestick) từ Binance CSV và tạo Markov chain với states 6-bit binary.

## Cách hoạt động
- Mỗi nến → 1 bit: Xanh (close > open) = 1, Đỏ = 0
- 6 nến liên tiếp → 1 state (ví dụ: "001001")
- Sliding window overlap để tạo chuỗi states
- Tính transition probability matrix

## Yêu cầu
```bash
pip install pandas numpy matplotlib seaborn
```

## Cách sử dụng

### Cơ bản
```bash
python markov_candle_analyzer.py <csv_file>
```

### Với custom output prefix
```bash
python markov_candle_analyzer.py BTCUSDT_1h.csv btc_analysis
```

## Format CSV Input
CSV cần có các cột: `open`, `close` (bắt buộc)

Ví dụ Binance CSV:
```
timestamp,open,high,low,close,volume
1234567890,50000,51000,49500,50500,100.5
...
```

## Output Files

Chương trình tạo ra 5 files:

1. **`{prefix}_transition_matrix.csv`**
   - Ma trận 64x64 với tất cả transition probabilities
   - Rows = From State, Columns = To State

2. **`{prefix}_state_frequencies.csv`**
   - Danh sách tất cả states với tần suất xuất hiện
   - Columns: state, count, frequency

3. **`{prefix}_top_transitions.csv`**
   - Danh sách tất cả transitions được sắp xếp theo probability
   - Columns: from_state, to_state, probability, count

4. **`{prefix}_summary.json`**
   - Tổng quan về phân tích
   - Top 10 states phổ biến nhất

5. **`{prefix}_visualization.png`**
   - 4 biểu đồ:
     * Heatmap của transition matrix
     * Top 20 states phổ biến nhất
     * Distribution của transition probabilities
     * Top 15 transitions có xác suất cao nhất

## Ví dụ đầy đủ

```bash
# Download data từ Binance (hoặc dùng CSV có sẵn)
python markov_candle_analyzer.py BTCUSDT_1h.csv btc_markov

# Kết quả:
# - btc_markov_transition_matrix.csv
# - btc_markov_state_frequencies.csv
# - btc_markov_top_transitions.csv
# - btc_markov_summary.json
# - btc_markov_visualization.png
```

## Giải thích States

State "001001" nghĩa là:
- Nến 1: Đỏ (0)
- Nến 2: Đỏ (0)
- Nến 3: Xanh (1)
- Nến 4: Đỏ (0)
- Nến 5: Đỏ (0)
- Nến 6: Xanh (1)

Tổng cộng có 2^6 = 64 states có thể (từ "000000" đến "111111")

## Lưu ý
- Program sử dụng sliding window nên mỗi nến (trừ 5 nến đầu) sẽ xuất hiện trong nhiều states
- States không xuất hiện trong data sẽ có probability = 0
- Cần ít nhất 6 nến để chạy được
