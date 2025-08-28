import numpy as np
from HawkesPyLib.model import ExpHawkesProcess
from HawkesPyLib.estimation import EM

# Giả sử bạn có dữ liệu sự kiện đã thu thập
# Thời gian các sự kiện xảy ra
event_times = [0.2, 1.5, 3.3, 5.7, 8.4, 12.5, 13.3, 15.2, 16.5, 19.0]

# Khởi tạo mô hình Hawkes với kernel hàm mũ
# Bạn có thể điều chỉnh các tham số decay và amplitude
decay = 0.5  # Tham số phân rã
amplitude = 0.8  # Biên độ ảnh hưởng giữa các sự kiện

# Khởi tạo mô hình ExpHawkesProcess với kernel hàm mũ
hawkes_model = ExpHawkesProcess(mu=0.5, alpha=amplitude, beta=decay)

# Thiết lập thuật toán EM để ước lượng tham số
learner = EM(hawkes_model, max_iter=100)
learner.fit([np.array(event_times)])

# In ra các tham số ước lượng được
print("Baseline intensity (mu):", hawkes_model.mu)
print("Decay parameter (beta):", hawkes_model.beta)
print("Influence parameter (alpha):", hawkes_model.alpha)
