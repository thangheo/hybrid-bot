import numpy as np
import matplotlib.pyplot as plt
from tick.hawkes import SimuHawkes, HawkesKernelExp

# Thiết lập các tham số cho mô hình Hawkes
baseline_intensity = 0.5  # Cường độ nền (base intensity)
decay = 0.5               # Tham số phân rã của kernel (decay parameter)
influence = 0.8           # Mức độ ảnh hưởng giữa các sự kiện

# Thiết lập mô hình Hawkes với một dimension duy nhất (1 loại sự kiện)
hawkes = SimuHawkes(baseline=[baseline_intensity], end_time=60, verbose=False)

# Sử dụng kernel hàm mũ cho quá trình tự kích thích
kernel = HawkesKernelExp(decay, influence)
hawkes.set_kernel(0, 0, kernel)

# Chạy mô phỏng
hawkes.simulate()

# Lấy thời gian các sự kiện đã xảy ra
event_times = hawkes.timestamps[0]

# Tạo biểu đồ cho kết quả mô phỏng
plt.figure(figsize=(12, 6))
plt.hist(event_times, bins=60, color='blue', alpha=0.7)
plt.xlabel('Thời gian (phút)')
plt.ylabel('Số lượng lệnh mua chủ động')
plt.title('Mô phỏng số lượng lệnh mua chủ động sử dụng Hawkes Process')
plt.grid(axis='y', linestyle='--', alpha=0.7)
plt.show()
