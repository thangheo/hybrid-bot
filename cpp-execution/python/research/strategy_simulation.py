import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from statsmodels.tsa.ar_model import AutoReg
from scipy.stats import norm

# Bước 1: Đọc dữ liệu giá từ tệp CSV
# Giả sử tệp CSV có cột 'Date' và 'Close'
data = pd.read_csv('AAVEUSDT_1m.csv')
print(data.tail())
prices = data['Close']
#get the last 1000 data
prices = prices.tail(2000)


# Bước 2: Tính toán lợi suất log
returns = np.log(prices / prices.shift(1)).dropna()

# Bước 3: Ước lượng các tham số mu, phi, sigma
# Sử dụng mô hình AR(1) với drift
model = AutoReg(returns, lags=1, trend='c')
model_fit = model.fit()
print(model_fit.params)  # Print the actual available keys in the model's parameters

phi = model_fit.params['Close.L1']
mu = model_fit.params['const'] / (1 - phi)
sigma = np.sqrt(model_fit.sigma2)

print(f"Estimated parameters:")
print(f"mu (drift) = {mu:.6f}")
print(f"phi (autocorrelation coefficient) = {phi:.6f}")
print(f"sigma (standard deviation) = {sigma:.6f}")

# Bước 4: Xây dựng mô hình AR(1) với drift cho mô phỏng
def simulate_returns(mu, phi, sigma, n_steps, n_simulations):
    simulations = np.zeros((n_steps, n_simulations))
    for i in range(n_simulations):
        r_t = returns.iloc[-1]  # Khởi tạo với giá trị lợi suất cuối cùng
        for t in range(n_steps):
            epsilon = np.random.normal(0, sigma)
            r_t = mu + phi * r_t + epsilon
            simulations[t, i] = r_t
    return simulations

# Bước 5: Sử dụng Monte Carlo để mô phỏng giá trong tương lai
n_steps = 500  # Số ngày giao dịch trong một năm
n_simulations = 1000  # Số lần mô phỏng

simulated_returns = simulate_returns(mu, phi, sigma, n_steps, n_simulations)

# Tính toán giá từ lợi suất mô phỏng
last_price = prices.iloc[-1]
simulated_prices = last_price * np.exp(np.cumsum(simulated_returns, axis=0))

# Bước 6: Đánh giá hiệu suất chiến lược
# Giả sử chiến lược là mua khi phi > 0 (momentum) và bán khi phi < 0 (mean reversion)
# Phí giao dịch là 0.1% cho mỗi giao dịch

# Tính lợi nhuận kỳ vọng của chiến lược
# Với mỗi kịch bản mô phỏng, tính lợi nhuận chiến lược

strategy_profits = np.zeros(n_simulations)

for i in range(n_simulations):
    r_sim = simulated_returns[:, i]
    signal = np.zeros_like(r_sim)
    profit = 0
    position = 0  # 1 nếu mua, -1 nếu bán, 0 nếu không có vị thế

    for t in range(1, len(r_sim)):
        # Sử dụng lợi suất tại thời điểm t-1 để đưa ra tín hiệu tại thời điểm t
        if phi > 0:
            # Chiến lược momentum
            signal[t] = np.sign(r_sim[t - 1])
        elif phi < 0:
            # Chiến lược mean reversion
            signal[t] = -np.sign(r_sim[t - 1])
        else:
            signal[t] = 0  # Không giao dịch nếu phi = 0

        # Thực hiện giao dịch
        if signal[t] != position:
            # Tính phí giao dịch
            transaction_cost = 0.001 * last_price * np.exp(np.sum(r_sim[:t]))
            profit -= transaction_cost
            position = signal[t]

        # Tính lợi nhuận hàng ngày
        profit += position * r_sim[t] * last_price * np.exp(np.sum(r_sim[:t]))

    strategy_profits[i] = profit

# Phân tích kết quả
mean_profit = np.mean(strategy_profits)
std_profit = np.std(strategy_profits)

print(f"\nStrategy Performance:")
print(f"Average Profit: {mean_profit:.2f}")
print(f"Standard Deviation of Profit: {std_profit:.2f}")

# Vẽ biểu đồ phân phối lợi nhuận
plt.hist(strategy_profits, bins=50, edgecolor='k')
plt.title('Distribution of Strategy Profits')
plt.xlabel('Profit')
plt.ylabel('Frequency')
plt.show()
