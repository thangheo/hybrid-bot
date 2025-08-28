import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.stats import norm
# import tick.hawkes as hk
import numpy as np
from scipy.optimize import minimize

def hawkes_fit(events):
    """
    Estimate the parameters of an exponential Hawkes process given event times.

    Parameters:
    events (array-like): An array of event times.

    Returns:
    lambda_0_est (float): Estimated baseline intensity λ₀.
    alpha_est (float): Estimated excitation parameter α.
    beta_est (float): Estimated decay parameter β.
    """
    events = np.asarray(events)
    N = len(events)
    T = events[-1]  # Observation window up to the last event time

    def negative_log_likelihood(params):
        lambda_0, alpha, beta = params

        # Ensure parameters are positive to maintain model validity
        if lambda_0 <= 0 or alpha <= 0 or beta <= 0:
            return np.inf

        # Initialize variables
        lambda_t = np.zeros(N)
        compensator = 0.0
        s_prev = 0.0

        for i in range(N):
            if i == 0:
                lambda_t[i] = lambda_0
            else:
                dt = events[i] - events[i - 1]
                s_prev = np.exp(-beta * dt) * (s_prev + 1)
                lambda_t[i] = lambda_0 + alpha * s_prev

            # Check for non-positive intensities
            if lambda_t[i] <= 0:
                return np.inf

        # Log-likelihood components
        log_likelihood = np.sum(np.log(lambda_t))
        compensator = lambda_0 * T + (alpha / beta) * (N - np.sum(np.exp(-beta * (T - events))))

        neg_log_likelihood = - (log_likelihood - compensator)
        return neg_log_likelihood

    # Initial parameter guesses
    lambda_0_init = N / T * 0.8
    alpha_init = N / T * 0.2
    beta_init = 1.0 / (events[-1] / N + 1e-5)

    initial_params = [lambda_0_init, alpha_init, beta_init]
    bounds = [(1e-8, None), (1e-8, None), (1e-8, None)]  # Parameters must be positive

    # Optimize the negative log-likelihood
    result = minimize(negative_log_likelihood, initial_params, bounds=bounds, method='L-BFGS-B')

    if result.success:
        lambda_0_est, alpha_est, beta_est = result.x
        return lambda_0_est, alpha_est, beta_est
    else:
        raise RuntimeError('Optimization failed: ' + result.message)

# def detect_jumps(price_series, window=20):
#     log_returns = np.log(price_series).diff().dropna()
#     abs_log_returns = np.abs(log_returns)

#     # Calculate realized volatility (RV) and bipower variation (BV)
#     rv = (log_returns ** 2).rolling(window=window).sum()
#     bv = (abs_log_returns * abs_log_returns.shift(1)).rolling(window=window).sum() * (np.pi / 2)

#     # Compute test statistic
#     jump_statistic = (rv - bv) / (np.sqrt((np.pi / 2) * (bv ** 2) / window))
#     p_values = 1 - norm.cdf(jump_statistic)

#     # Identify jumps (e.g., p-value < 0.05)
#     jump_indices = p_values[p_values < 0.05].index

#     return jump_indices, log_returns.loc[jump_indices]




def detect_jumps(price_series, window=20):
    log_returns = np.log(price_series).diff().dropna()
    abs_log_returns = np.abs(log_returns)

    # Calculate realized volatility (RV) and bipower variation (BV)
    rv = (log_returns ** 2).rolling(window=window).sum()
    bv = (abs_log_returns * abs_log_returns.shift(1)).rolling(window=window).sum() * (np.pi / 2)

    # Compute test statistic
    jump_statistic = (rv - bv) / (np.sqrt((np.pi / 2) * (bv ** 2) / window))
    p_values = 1 - norm.cdf(jump_statistic)

    # Convert p-values to a Series to retain the index
    p_values_series = pd.Series(p_values, index=jump_statistic.index)

    # Identify jumps (e.g., p-value < 0.05)
    jump_indices = p_values_series[p_values_series < 0.05].index

    return jump_indices, log_returns.loc[jump_indices]

def estimate_hawkes_parameters(jump_times):
    # Convert jump times to list of arrays (as required by tick library)
    timestamps = [np.array(jump_times)]

    # Create and fit the Hawkes model
    # hawkes_model = hk.HawkesExpKern(decays=1.0, baseline=0.1, max_iter=1000)
    # hawkes_model.fit(timestamps)

    # lambda_0_est = hawkes_model.baseline[0]
    # alpha_est = hawkes_model.adjacency[0, 0]
    # beta_est = 1.0 / hawkes_model.decays[0]  # Decay rate is inverse of the decay parameter in tick


    lambda_0_est, alpha_est, beta_est = hawkes_fit(jump_times)

    return lambda_0_est, alpha_est, beta_est

    # # Example usage:
    # jump_times_list = jump_indices.astype(float).values  # Ensure jump times are in float
    # lambda_0_est, alpha_est, beta_est = estimate_hawkes_parameters(jump_times_list)

    # print(f"Estimated lambda_0: {lambda_0_est}")
    # print(f"Estimated alpha: {alpha_est}")
    # print(f"Estimated beta: {beta_est}")

def estimate_jump_size_parameters(jump_returns):
    # Since jump sizes are in log returns, we can use them directly
    mu_jump_est = np.mean(jump_returns)
    sigma_jump_est = np.std(jump_returns)

    return mu_jump_est, sigma_jump_est

    # # Example usage:
    # mu_jump_est, sigma_jump_est = estimate_jump_size_parameters(jump_returns)

    # print(f"Estimated mu_jump: {mu_jump_est}")
    # print(f"Estimated sigma_jump: {sigma_jump_est}")

def estimate_diffusion_parameters(log_returns, jump_indices):
    # Remove jumps from returns
    non_jump_returns = log_returns.drop(index=jump_indices)

    mu_est = np.mean(non_jump_returns) / dt
    sigma_est = np.std(non_jump_returns) / np.sqrt(dt)

    return mu_est, sigma_est

    # Example usage:
    # dt = T / N  # Time step
    # log_returns = np.log(price_series).diff().dropna()
    # mu_est, sigma_est = estimate_diffusion_parameters(log_returns, jump_indices)

    # print(f"Estimated mu: {mu_est}")
    # print(f"Estimated sigma: {sigma_est}")

if __name__ == "__main__":
    # Example usage:
    S = np.cumsum(np.random.normal(0, 1, 1000))
    time = pd.date_range('2021-01-01', periods=1000)

    price_series = pd.Series(S, index=time)
    jump_indices, jump_returns = detect_jumps(price_series)
    # Convert jump indices (DatetimeIndex) to UNIX timestamps as float
    jump_times_list = jump_indices.astype('int64') / 1e9  # Convert to seconds

    # # Plot detected jumps
    # plt.figure(figsize=(12, 6))
    # plt.plot(price_series.index, price_series, label='Price Series')
    # plt.scatter(price_series.index.intersection(jump_indices), price_series.loc[jump_indices], color='red', label='Detected Jumps')
    # plt.legend()
    # plt.show()


    # Estimate Hawkes parameters
    # jump_times_list = jump_indices.astype(float).values  # Ensure jump times are in float
    lambda_0_est, alpha_est, beta_est = estimate_hawkes_parameters(jump_times_list)
    print(f"Estimated lambda_0: {lambda_0_est}")
    print(f"Estimated alpha: {alpha_est}")
    print(f"Estimated beta: {beta_est}")
