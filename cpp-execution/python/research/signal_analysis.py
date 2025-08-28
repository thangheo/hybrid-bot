# Converting the C++ code to Python and adding a plotting function

import numpy as np
import matplotlib.pyplot as plt
import pandas as pd
import ccxt
from statsmodels.tsa.ar_model import AutoReg
from scipy.stats import norm

#function to get 1000 data 1m from binance
def get_data(symbol,tf='1m'):
    binance = ccxt.binance()
    data = binance.fetch_ohlcv(symbol, tf)
    #convert to pandas dataframe
    data = pd.DataFrame(data, columns=['Timestamp', 'Open', 'High', 'Low', 'Close', 'Volume'])

    return data
# Function to calculate the Hurst Exponent
def calculate_hurst_exponent(data):
    N = len(data)
    mean_adjusted_data = data - np.mean(data)
    
    # Cumulative sum
    cumulative_sum = np.cumsum(mean_adjusted_data)
    
    # Range (R)
    R = np.max(cumulative_sum) - np.min(cumulative_sum)
    
    # Standard deviation (S)
    S = np.std(mean_adjusted_data)
    
    # Hurst exponent
    return np.log(R / S) / np.log(N)
#function to calculate RSI
def calculate_rsi(data, window=14):
    delta = np.diff(data)
    gain = (delta[delta > 0]).mean()
    loss = (-delta[delta < 0]).mean()
    rs = gain / loss
    return 100 - (100 / (1 + rs))

# Function to calculate the variance ratio
def variance_ratio_test(data, lag):
    N = len(data)
    mean = np.mean(data)
    variance = np.mean([(data[i] - data[i - 1] - mean) ** 2 for i in range(1, N)])
    lag_variance = np.mean([(data[i] - data[i - lag] - mean) ** 2 for i in range(lag, N)])
    
    return lag_variance / variance

# Function to calculate the autocorrelation at lag 'lag'
def calculate_autocorrelation(data, lag):
    mean = np.mean(data)
    numerator = np.sum([(data[i] - mean) * (data[i + lag] - mean) for i in range(len(data) - lag)])
    denominator = np.sum([(data[i] - mean) ** 2 for i in range(len(data))])
    
    return numerator / denominator

def autocorrelation_coefficient(data, lag):
  
    # Compute logarithmic returns
    ret = np.log(data[1:] / data[:-1])

    # Check if lag is valid
    if lag >= len(ret):
        raise ValueError("Lag is too large for the dataset.")

    # Align the returns for the specified lag
    ret_t = ret[lag:]       # Returns at time t
    ret_tlag = ret[:-lag]   # Returns at time t - lag

    # Compute the autocorrelation coefficient phi
    phi = np.corrcoef(ret_t, ret_tlag)[0, 1]

    return phi

# Function to generate the signals and calculate the indicators
def analyze_signals():
    # Generate example signals: mean-reverting, trending, and random noise
    np.random.seed(42)
    n = 1000

    # Mean-reverting signal (Ornstein-Uhlenbeck process)
    theta = 0.1
    mu = 0
    sigma = 0.3
    dt = 0.01
    x = np.zeros(n)
    for t in range(1, n):
        x[t] = x[t-1] + theta * (mu - x[t-1]) * dt + sigma * np.sqrt(dt) * np.random.normal()

    mean_reverting_signal = x

    # Trending signal (Brownian motion with drift)
    drift = -0.03
    trending_signal = np.cumsum(np.random.normal(drift, 0.1, n))

    # Random noise signal (Gaussian white noise)
    random_noise_signal = np.random.normal(0, 1, n)

    # Calculate Hurst Exponent
    hurst_mean_reverting = calculate_hurst_exponent(mean_reverting_signal)
    hurst_trending = calculate_hurst_exponent(trending_signal)
    hurst_random_noise = calculate_hurst_exponent(random_noise_signal)

    # Calculate Variance Ratio Test with lag 2
    variance_ratio_mean_reverting = variance_ratio_test(mean_reverting_signal, 2)
    variance_ratio_trending = variance_ratio_test(trending_signal, 2)
    variance_ratio_random_noise = variance_ratio_test(random_noise_signal, 2)

    # Calculate Autocorrelation at lag 1
    autocorrelation_mean_reverting = calculate_autocorrelation(mean_reverting_signal, 3)
    autocorrelation_trending = calculate_autocorrelation(trending_signal, 3)
    autocorrelation_random_noise = calculate_autocorrelation(random_noise_signal, 3)

    return {
        "mean_reverting": (mean_reverting_signal, hurst_mean_reverting, variance_ratio_mean_reverting, autocorrelation_mean_reverting),
        "trending": (trending_signal, hurst_trending, variance_ratio_trending, autocorrelation_trending),
        "random_noise": (random_noise_signal, hurst_random_noise, variance_ratio_random_noise, autocorrelation_random_noise)
    }
def test_on_real_data(path):
    #read data from csv file
    csv = pd.read_csv('AAVEUSDT_1m.csv')
    data = csv['Close'][:300].values
    # data_next = csv['Close'][1000:2000].values
    # Calculate Hurst Exponent
    hurst = calculate_hurst_exponent(data)
    print(f"Hurst Exponent: {hurst:.4f}")
    # Calculate Variance Ratio Test with lag 2
    variance_ratio = variance_ratio_test(data, 2)
    print(f"Variance Ratio: {variance_ratio:.4f}")
    # Calculate Autocorrelation at lag 1
    autocorrelation = calculate_autocorrelation(data, 1)
    print(f"Autocorrelation (lag 1): {autocorrelation:.4f}")
    #plot price with analysis for data and data_next
    plt.plot(csv['Close'][:500], label='data')
    plt.title(f"Hurst Exponent: {hurst:.4f}, Variance Ratio: {variance_ratio:.4f}, Autocorrelation (lag 1): {autocorrelation:.4f}")
    plt.show()

# Run the function to analyze the signals

# Function to plot the signals and display their properties
def plot_signals_with_analysis():
    analysis = analyze_signals()
    
    signals = ["mean_reverting", "trending", "random_noise"]
    titles = ["Mean-Reverting Signal", "Trending Signal", "Random Noise Signal"]
    
    plt.figure(figsize=(12, 8))

    for i, signal in enumerate(signals):
        #print signal type
        signal_data, hurst, variance_ratio, autocorrelation = analysis[signal]
        print(f"\n{titles[i]}:")
        print(f"Hurst Exponent: {hurst:.4f}")
        print(f"Variance Ratio: {variance_ratio:.4f}")
        print(f"Autocorrelation (lag 1): {autocorrelation:.4f}")
        if hurst < 0.5:
            print("hurst : is mean-reverting")
        elif hurst == 0.5:
            print("hurst : is random walk")
        else:
            print("hurst : is trending")
        if autocorrelation > 0:
            print("ACF: is positively autocorrelated-->trending")
        elif autocorrelation < 0:
            print("ACF: is negatively autocorrelated-->mean-reverting")
        else:
            print("ACF: is random walk")
        if variance_ratio < 1:
            print("VR: is mean-reverting")
        elif variance_ratio == 1:
            print("VR: is random walk")
        else:
            print("VR: is trending")

        plt.subplot(3, 1, i+1)
        plt.plot(signal_data, label=f'{titles[i]}')
        plt.title(f"{titles[i]}\nHurst Exponent: {hurst:.4f}, Variance Ratio: {variance_ratio:.4f}, Autocorrelation (lag 1): {autocorrelation:.4f}")
        plt.legend()

    plt.tight_layout()
    plt.show()

# Run the function to plot signals with analysis
# plot_signals_with_analysis()
# test_on_real_data('AAVEUSDT_1m.csv')
##############
'''
variance ratio = 1 for random walk or trending
variation ratio > 1.5 for mean reverting
autocorrelation  ~= 0 for random walk and ~=1 for trending or mean reverting
Hurst ~= 0.8 for trending or mean reverting and ~=0.5 for random walk

==> Hurst Exponent: 0.8000
==> Variance Ratio: 0.0000
==> Autocorrelation (lag 1): 0.0000
=====================================
Trending Signal:
Hurst Exponent: > 0.8000
Variance Ratio: ~1
acf ~= 1

Mean-Reverting Signal:
Hurst Exponent: > 0.8000
Variance Ratio: > 1.5
acf ~= 1

Random Noise Signal:
Hurst Exponent: ~0.5000
Variance Ratio: ~1
acf ~= 0

'''



if __name__ == "__main__":
    # test_on_real_data('AAVEUSDT_1m.csv')
    symbol = 'BTCUSDT' #'SOLUSDT'
    tf = '1m'
    # data = get_data(symbol)
    data = pd.read_csv('AAVEUSDT_1m.csv')
    price = data['Close'][-800:].values

    # Calculate log returns
    returns = np.log(price[1:] / price[:-1])

    # Fit the AR(1) model
    model = AutoReg(returns, lags=3, trend='c')
    model_fit = model.fit()
    print(model_fit.summary())
    #get trend
    trend = model_fit.params[0]
    print(f"Trend: {trend:.4f}")
    
    # Get the AR(1) coefficient
    ar1 = model_fit.params[1]
    print(f"AR(1) Coefficient: {ar1 :.4f}")
    ar2= model_fit.params[2]
    print(f"AR(2) Coefficient: {ar2 :.4f}")
    ar3= model_fit.params[3]
    print(f"AR(3) Coefficient: {ar3 :.4f}")



    #calculate rsi and hurst exponent and variance ratio and autocorrelation with various lags
    rsi = calculate_rsi(price)
    hurst = calculate_hurst_exponent(price)
    variance_ratio = variance_ratio_test(price, 2)
    # autocorrelation = calculate_autocorrelation(price, 1)
    variance_ratio2 = variance_ratio_test(price, 3)
    # autocorrelation2 = calculate_autocorrelation(price, 2)
    variance_ratio3 = variance_ratio_test(price, 4)
    # autocorrelation3 = calculate_autocorrelation(price, 3)
    #auto correlation coefficient
    autocorrelation1 = autocorrelation_coefficient(price, 1)
    print(f"new Autocorrelation Coefficient (lag 1): {autocorrelation1:.4f}")
    autocorrelation2 = autocorrelation_coefficient(price, 2)
    print(f"new Autocorrelation Coefficient (lag 2): {autocorrelation2:.4f}")
    autocorrelation3 = autocorrelation_coefficient(price, 3)
    print(f"new Autocorrelation Coefficient (lag 3): {autocorrelation3:.4f}")
   
    #print lastest price and rsi and hurst and variance ratio and autocorrelation
    print(f"Symbol: {symbol}")
    print(f"Latest Price: {price[-1]}")
    print(f"RSI: {rsi:.4f}")
    print(f"Hurst Exponent: {hurst:.4f}")
    print(f"Variance Ratio: {variance_ratio:.4f}")
    print(f"Variance Ratio (lag 2): {variance_ratio2:.4f}")
    print(f"Variance Ratio (lag 3): {variance_ratio3:.4f}")
    # print(f"Autocorrelation (lag 1): {autocorrelation:.4f}")
    # print(f"Autocorrelation (lag 2): {autocorrelation2:.4f}")
    # print(f"Autocorrelation (lag 3): {autocorrelation3:.4f}")

    #plot price with analysis
    plt.plot(price, label='Price')
    plt.title(f"Symbol: {symbol}, RSI: {rsi:.4f}, Hurst Exponent: {hurst:.4f}, Variance Ratio: {variance_ratio:.4f}, \
                Variance Ratio (lag 2): {variance_ratio2:.4f}, Variance Ratio (lag 3): {variance_ratio3:.4f},\
             Autocorrelation (lag 1): {autocorrelation1:.4f}")
    plt.show()
