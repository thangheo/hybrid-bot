import pandas as pd
import numpy as np
from statsmodels.tsa.ar_model import AutoReg
from scipy.optimize import minimize
import ccxt
import numpy as np

def safe_returns(profit):
    # Replace zero or near-zero values in profit[:-1] to avoid divide-by-zero errors
    adjusted_profit = np.where(np.abs(profit[:-1]) < 1e-10, 1e-10, profit[:-1])
    
    # Calculate returns with safe division
    returns = np.diff(profit) / adjusted_profit
    
    # Remove any NaN or infinite values in the returns
    returns = np.nan_to_num(returns, nan=0.0, posinf=0.0, neginf=0.0)
    
    return returns

def calculate_sharpe_ratio(profit, risk_free_rate=0):
    # Remove NaN or infinite values from the profit array
    
    # Check if the profit array is too short or has no variation
    if len(profit) < 5 or np.std(profit) == 0:
        return 0
    
    returns = safe_returns(profit)  
    
    # Adjust returns for risk-free rate if needed (Sharpe ratio typically assumes zero risk-free)
    excess_returns = returns - risk_free_rate
    
    # Calculate the average excess daily return and standard deviation of returns
    avg_return = np.mean(excess_returns)
    std_return = np.std(excess_returns)
    
    # Calculate and return the Sharpe ratio
    sharpe_ratio = avg_return / std_return if std_return != 0 else 0
    return sharpe_ratio
#calculate average profit per trade
def calculate_avg_profit_per_trade(profit):
    # Calculate the number of trades
    trades = np.count_nonzero(np.diff(profit))
    
    # Calculate the average profit per trade
    avg_profit_per_trade = profit[-1] / trades if trades > 0 else 0
    return avg_profit_per_trade
#calculate the number of trades
def calculate_number_of_trades(profit):
    # Calculate the number of trades
    trades = np.count_nonzero(np.diff(profit))
    return trades
#calculate the candles per trade
def calculate_candles_per_trade(prices, profit):
    # Calculate the number of trades
    trades = np.count_nonzero(np.diff(profit))
    
    # Calculate the candles per trade
    candles_per_trade = len(prices) / trades if trades > 0 else 0
    return candles_per_trade
#calculate calmar ratio
def calculate_calmar_ratio(profit):
    # Calculate the maximum drawdown
    max_drawdown = np.max(np.maximum.accumulate(profit) - profit)
    
    # Calculate the Calmar ratio
    calmar_ratio = profit[-1] / max_drawdown if max_drawdown > 0 else 0
    return calmar_ratio
    
#test plot 
def plot_positions(prices, long_position, short_position):
    import matplotlib.pyplot as plt
    plt.figure(figsize=(14, 7))
    plt.plot(prices, label='Price')
    plt.plot(np.ma.masked_where(long_position == 0, long_position) * prices, 'g^', markersize=8, label='Long')
    plt.plot(np.ma.masked_where(short_position == 0, short_position) * prices, 'rv', markersize=8, label='Short')
    plt.legend()
    plt.show()
#plot the profit
def plot_profit(profit):
    import matplotlib.pyplot as plt
    plt.figure(figsize=(14, 7))
    plt.plot(profit, label='Profit')
    plt.legend()
    plt.show()
def mix_strategy(current_index,prices, threshold1, threshold2, capital=10000, transaction_cost=0.001,phi=0,drift=0, sigma=0):
    position = 0  # -1 for short, 1 for long, 0 for no position
    # Mean reversion strategy if drift is small enough
    price= prices[current_index]
    position_size = 0
    std_price = sigma
    mean_price = np.mean(prices[current_index - 200:current_index])
    atr = std_price / mean_price
    if phi < threshold1: #mean reversion for negative phi
        #just bypass
        # continue
        if price <= mean_price - 1.5*std_price and price < prices[current_index - 1]  and atr < 0.005 :
            # and price < prices[i - 1]:
            print(f"Mean reversion: Long position (price below mean) at price {price}")
            position = 1 
            position_size = (capital / price) *(1-transaction_cost)
        elif price >= mean_price + 1.5*std_price and price > prices[current_index - 1]  and atr < 0.005:
            # continue
            #and price > prices[i - 1]:
            print(f"Mean reversion: Short position (price above mean) at price {price}")
            position = -1 
            position_size = (capital / price) *(1-transaction_cost)
        #condition to close the position
        else:
            position = 0

    # Momentum strategy for positive phi
    elif phi > threshold2:
        pass
        #just bypass
        if price > mean_price and price > prices[current_index - 1]:
            print(f"Momentum: Long position (price rising) at price {price}")
            position = 1 
        elif price < mean_price and price < prices[current_index - 1]:
            print(f"Momentum: Short position (price falling) at price {price}")
            position = -1  
        #condition to close the position
        else:
            position = 0
    else:
        # print(f"No strong signal at price {price}. Maintaining current position.")
        position = 0  # No position or maintain current one
    
    return position, position_size


def strategy_2(current_index,prices, threshold1, threshold2, capital=10000, transaction_cost=0.001,phi=0,drift=0, sigma=0):
    position = 0  # -1 for short, 1 for long, 0 for no position
    # Mean reversion strategy if drift is small enough
    price= prices[current_index]
    position_size = 0
    std_price = sigma
    mean_price = np.mean(prices[current_index - 200:current_index])
    atr = std_price / mean_price
    if phi < threshold1: #mean reversion for negative phi
        #just bypass
        # continue
        if price < prices[current_index - 1]  and 2*transaction_cost < atr< 0.007 :
            # and price < prices[i - 1]:
            # print(f"Mean reversion: Long position (price below mean) at price {price}")
            position = 1 
            position_size = (capital / price) *(1-transaction_cost)
        elif price > prices[current_index - 1]  and 2*transaction_cost  < atr < 0.007:
            # continue
            #and price > prices[i - 1]:
            # print(f"Mean reversion: Short position (price above mean) at price {price}")
            position = -1 
            position_size = (capital / price) *(1-transaction_cost)
        #condition to close the position
        else:
            position = 0

    # Momentum strategy for positive phi
    elif phi > threshold2:
        pass
        #just bypass
        if price > prices[current_index - 1] and 2*transaction_cost < atr < 0.007 :
            # print(f"Momentum: Long position (price rising) at price {price}")
            position_size = (capital / price) *(1-transaction_cost)
            position = 1 
        elif price < prices[current_index - 1] and 2*transaction_cost < atr < 0.007 :
            # print(f"Momentum: Short position (price falling) at price {price}")
            position_size = (capital / price) *(1-transaction_cost)
            position = -1  
        #condition to close the position
        else:
            position = 0
    else:
        # print(f"No strong signal at price {price}. Maintaining current position.")
        position = 0  # No position or maintain current one
    
    return position, position_size


# Function to calculate the autocorrelation at lag 'lag'
def calculate_autocorrelation(data, lag):
    mean = np.mean(data)
    numerator = np.sum([(data[i] - mean) * (data[i + lag] - mean) for i in range(len(data) - lag)])
    denominator = np.sum([(data[i] - mean) ** 2 for i in range(len(data))])
    
    return numerator / denominator

def backtest_strategy(prices, threshold1, threshold2, initial_capital=10000, position_size_=100, transaction_cost=0.001):
   
    # Initialize variables for tracking positions and profits
    position = 0  # -1 for short, 1 for long, 0 for no position
    capital = initial_capital
    position_price = 0
    n = len(prices)
    #an array to store the profit
    profit = np.zeros(n) + initial_capital
    transition = 0 # 0 for no transition, 1 for transitioning
    #variables to store position to plot later
    long_position = np.zeros(n)
    short_position = np.zeros(n)
    position_size = 0 #calculate position size base on balance
    for i in range(10, n, 1):
        # Skip the first 200 prices, as we need 200 prices to calculate log returns
        if i < 200:
            profit[i] = capital
            continue

        # Calculate log returns for the last 200 prices
        past_prices = prices[i - 200:i]
        returns = np.log(past_prices[1:] / past_prices[:-1])

        # Fit AR(1) model on the calculated returns
        USE_AUTOGEG = False
        if USE_AUTOGEG:    
            model = AutoReg(returns, lags=1, trend='c').fit()
            phi = model.params[1]  # Extract the phi (lag-1 coefficient)
            drift = model.params[0]
        else:
            phi = calculate_autocorrelation(returns, 1)
            drift = 0

        #calculate rolling mean
        mean_price = np.mean(past_prices)
        #calculate rolling std
        std_price = np.std(past_prices)
        #calculate %atr
        atr = std_price / mean_price

        # Close any existing position if transitioning between strategies
        if position != 0:
            if position == 1:  # Long position
                trade_value = (prices[i] - position_price) * position_size
                capital += trade_value
                capital -= abs(trade_value) * transaction_cost  # Apply transaction cost
            elif position == -1:  # Short position
                trade_value = (position_price - prices[i]) * position_size
                capital += trade_value
                capital -= abs(trade_value) * transaction_cost  # Apply transaction cost
            position = 0  # Reset position after closing
            #store the profit
            profit[i] = capital
        # Determine strategy based on phi
        # position,position_size = mix_strategy(i,prices, threshold1, threshold2, capital, transaction_cost,phi,drift,std_price)
        position,position_size = strategy_2(i,prices, threshold1, threshold2, capital, transaction_cost,phi,drift,std_price)
        position_price = prices[i]
        #store the profit
        profit[i] = capital
        
        if position == 1:
            long_position[i] = 1
        elif position == -1:
            short_position[i] = 1
    #end of loop


    # Close the final position at the last price point if needed
    if position != 0:
        if position == 1:  # Close long position
            trade_value = (prices[-1] - position_price) * position_size
            capital += trade_value
            capital -= abs(trade_value) * transaction_cost  # Apply transaction cost
        elif position == -1:  # Close short position
            trade_value = (position_price - prices[-1]) * position_size
            capital += trade_value
            capital -= abs(trade_value) * transaction_cost  # Apply transaction cost

    # Calculate the final profit
    final_profit = capital - initial_capital

    # Calculate profit percentage based on the initial capital
    profit_percentage = (final_profit / initial_capital) * 100
    #calculate sharpe ratio
    sharpes_ratio = calculate_sharpe_ratio(profit)
    #calculate average profit per trade
    avg_profit_per_trade = calculate_avg_profit_per_trade(profit)
    #calculate number of trades
    trades = calculate_number_of_trades(profit)
    #calculate candles per trade
    candles_per_trade = calculate_candles_per_trade(prices, profit)
    #calculate calmar ratio
    calmar_ratio = calculate_calmar_ratio(profit)
    print(f"Final profit: {final_profit:.2f}")
    print(f"Profit percentage: {profit_percentage:.2f}%")
    print(f"Sharpe ratio: {sharpes_ratio:.4f}")
    print(f"Average profit per trade: {avg_profit_per_trade:.2f}")
    print(f"Number of trades: {trades}")
    print(f"Candles per trade: {candles_per_trade:.2f}")
    print(f"Calmar ratio: {calmar_ratio:.4f}")

    # #plot the profit
    plot_profit(profit)
    #plot the position
    plot_positions(prices, long_position, short_position)

    return profit_percentage , sharpes_ratio

# Objective function for optimization
def objective_function(params, prices):
    threshold1, threshold2 = params
    profit_percentage , sharpes_ratio = backtest_strategy(prices, threshold1, threshold2)
    # Since minimize() minimizes the objective, return the negative profit to maximize it
    return -sharpes_ratio

# Load the historical data and run the optimization
def optimize_strategy(csv_file):
    # Load historical price data
    data = pd.read_csv(csv_file)
    prices = data['Close'].values
    
    # Randomly select a portion of the data for optimization
    index = np.random.randint(0, len(prices) - 10000)
    selected_prices = prices[index:index + 10000]

    # Set initial guesses for thresholds
    initial_guess = np.random.uniform(-1, 1, size=2)

    # Use scipy's minimize function to optimize threshold1 and threshold2
    result = minimize(
        objective_function, 
        initial_guess, 
        args=(selected_prices,), 
        bounds=[(-1, 1), (0, 2)],  # Widen the bounds
        method='L-BFGS-B', 
        tol=1e-8,           # Set tolerance for convergence
        options={'disp': True, 'maxiter': 500}  # Increase max iterations
    )

    optimal_threshold1, optimal_threshold2 = result.x
    print(f"Optimal threshold1: {optimal_threshold1}, Optimal threshold2: {optimal_threshold2}")
    #print final profit
    final_profit = -result.fun
    print(f"Final profit: {final_profit:.2f}")
    # Calculate profit percentage based on the initial capital
    profit_percentage = (final_profit / 10000) * 100
    print(f"Profit percentage: {profit_percentage:.2f}%")

#manual optimize with loop
def manual_optimizer(prices):

    sharpe_ratios = []
    profit_percentages = []
    steps = 15
    thresholds1_l = np.linspace(-0.1, -0.01, steps)
    thresholds2_l = np.linspace(0.01, 0.1, steps)
    for threshold1 in thresholds1_l:
        for threshold2 in thresholds2_l:
            profit_percentage, sharpe_ratio = backtest_strategy(prices, threshold1, threshold2)
            profit_percentages.append(profit_percentage)
            sharpe_ratios.append(sharpe_ratio)
            print(f"Threshold1: {threshold1}, Threshold2: {threshold2}, Profit percentage: {profit_percentage:.2f}%, Sharpe ratio: {sharpe_ratio:.4f}")

    max_profit_percentage = max(profit_percentages)
    max_index = profit_percentages.index(max_profit_percentage)
    profit_percentages_optimal_threshold1 = thresholds1_l[max_index // steps]
    profit_percentages_optimal_threshold2 = thresholds2_l[max_index % steps]

    max_sharpe_ratio = max(sharpe_ratios)
    max_index = sharpe_ratios.index(max_sharpe_ratio)
    sharpe_optimal_threshold1 = thresholds1_l[max_index // steps]
    sharpe_optimal_threshold2 = thresholds2_l[max_index % steps]


    print(f"Optimal threshold1 for profit percentage: {profit_percentages_optimal_threshold1}, Optimal threshold2 for profit percentage: {profit_percentages_optimal_threshold2}")
    print(f"Optimal threshold1 for sharpe ratio: {sharpe_optimal_threshold1}, Optimal threshold2 for sharpe ratio: {sharpe_optimal_threshold2}")
    return profit_percentages_optimal_threshold1, profit_percentages_optimal_threshold2 , sharpe_optimal_threshold1, sharpe_optimal_threshold2



#function to get 1000 data 1m from binance
def get_data(symbol,tf='1m'):
    binance = ccxt.binance()
    data = binance.fetch_ohlcv(symbol, tf)
    #convert to pandas dataframe
    data = pd.DataFrame(data, columns=['Timestamp', 'Open', 'High', 'Low', 'Close', 'Volume'])

    return data
if __name__ == '__main__':
    # optimize_strategy('AAVEUSDT_1m.csv')
    # exit()
    #run backtest_strategy SOLUSDT_1m
    selected_prices = []
    BACKTEST = False
    if BACKTEST :
        data = pd.read_csv('AAVEUSDT_1m.csv')
        #test on real data
        prices = data['Close'].values
        # Randomly select a portion of the data for optimization
        datalen= 10000
        index = np.random.randint(0, len(prices) - datalen)
        selected_prices = prices[index:index + datalen]
    else:
        selected_prices = get_data('SUIUSDT', '1m')
        selected_prices = selected_prices['Close'].values
        
   # optimize parasm
    # profit_percentages_optimal_threshold1, profit_percentages_optimal_threshold2 , sharpe_optimal_threshold1, sharpe_optimal_threshold2 = manual_optimizer(selected_prices)

    # profit_percentage, sharpe_ratio = backtest_strategy(selected_prices, profit_percentages_optimal_threshold1, profit_percentages_optimal_threshold2)
    # print(f"Profit percentage: {profit_percentage:.2f}%")
    # print(f"Sharpe ratio: {sharpe_ratio:.4f}")

    # profit_percentage, sharpe_ratio = backtest_strategy(selected_prices, sharpe_optimal_threshold1, sharpe_optimal_threshold2)
    # print(f"Profit percentage: {profit_percentage:.2f}%")
    # print(f"Sharpe ratio: {sharpe_ratio:.4f}")

    #run test on pre value
    profit_percentage, sharpe_ratio = backtest_strategy(selected_prices, -0.01643, 0.02928) #aave
    # profit_percentage, sharpe_ratio = backtest_strategy(selected_prices, -0.0678, 0.0486) #sol
    print(f"Profit percentage: {profit_percentage:.2f}%")
    print(f"Sharpe ratio: {sharpe_ratio:.4f}")
    '''
    SOLUSDT_1m -0.0678, 0.0486)
    AAVEUSDT_1m  -0.01643, 0.02928
    

    '''