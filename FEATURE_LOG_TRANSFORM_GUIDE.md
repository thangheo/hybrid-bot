# Feature Log Transformation Guide

## Vấn đề

Feature index 2 trong imbalance detection có giá trị rất lớn:
- Mean: 546.84
- Standard deviation: 3591.09

Điều này có thể gây:
1. **Overflow/underflow** khi normalize
2. **Numerical instability** trong Bayesian model
3. **Skewed distribution** làm giảm hiệu quả model

## Giải pháp: Log Transformation

### Ưu điểm của Log Transform
1. **Compress large values**: log(x+1) biến đổi [0,∞) → [0,∞) nhưng với tốc độ chậm lại
2. **Numerical stability**: Tránh overflow khi x rất lớn
3. **Normalize skewed distributions**: Log giúp phân phối gần normal hơn
4. **Preserve relationships**: Vẫn giữ được thứ tự và tương quan

### Implementation Details

#### 1. Configuration Options
```cpp
struct ModelConfig {
    // Feature preprocessing options
    bool enable_log_transform = true;           // Enable/disable log transform
    std::vector<int> log_transform_indices = {2}; // Which features to transform
    double log_transform_threshold = 0.0;       // Only transform if value > threshold
};
```

#### 2. Transform Function
```cpp
// Uses log1p(x) = log(1+x) for numerical stability
if (should_log_transform && raw_val > threshold) {
    raw_val = std::log1p(raw_val);
}
```

#### 3. Consistent Application
- **Training time**: Transform được apply trong `update_feature_stats()`
- **Prediction time**: Transform được apply trong `normalize_features()`
- **Persistence**: Settings được save/load cùng model

### Configuration Examples

#### Default (Transform feature 2 only)
```json
{
    "enable_log_transform": true,
    "log_transform_indices": [2],
    "log_transform_threshold": 0.0
}
```

#### Transform multiple features
```json
{
    "enable_log_transform": true,
    "log_transform_indices": [2, 5, 8],
    "log_transform_threshold": 1.0
}
```

#### Disable log transform
```json
{
    "enable_log_transform": false,
    "log_transform_indices": [],
    "log_transform_threshold": 0.0
}
```

### Impact Analysis

#### Before Log Transform (Feature 2):
```
Mean: 546.84
Std:  3591.09
Range: [0, very_large_numbers]
Distribution: Highly skewed
```

#### After Log Transform (Feature 2):
```
Expected Mean: ~6.3 (log1p(546.84))
Expected Std:  Much smaller, more reasonable
Range: [0, log(large_numbers)]
Distribution: More normal-like
```

### Backward Compatibility

Existing models sẽ tự động:
1. Default `enable_log_transform = true`
2. Default `log_transform_indices = {2}` 
3. Preserve existing feature normalization cho các features khác

### Monitoring & Debugging

1. **Check model stats** trước/sau transform:
```cpp
auto stats = model.get_stats();
std::cout << "Feature means: ";
for (double mean : stats.feature_means) {
    std::cout << mean << " ";
}
```

2. **Save model** để xem transformed statistics trong JSON
3. **Monitor numerical stability** - không còn NaN/inf values

### Usage trong ImbalanceDetectionEngine

Feature 2 hiện tại là detector result hoặc raw metric. Log transform sẽ:
1. Giảm impact của extreme outliers  
2. Improve model convergence
3. Better numerical stability
4. More balanced feature scaling

### Recommendations

1. **Keep default settings** cho feature 2
2. **Monitor model performance** sau khi deploy
3. **Consider other features** nếu chúng cũng có distribution skewed
4. **Adjust threshold** nếu cần fine-tuning cho specific features