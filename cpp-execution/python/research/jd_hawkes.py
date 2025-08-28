import numpy as np
import matplotlib.pyplot as plt

# Parameters
mu = 0.05            # Expected return
sigma = 0.2          # Volatility
S0 = 100             # Initial stock price
lambda_0 = 0.1       # Base intensity for jumps
alpha = 0.5          # Self-exciting parameter (impact of each event)
beta = 1.0           # Decay rate of the self-exciting effect
mu_jump = -0.2       # Expected jump size (mean of jump size in log-normal)
sigma_jump = 0.1     # Volatility of jump size
T = 1                # Time horizon (1 year)
N = 1000             # Number of time steps

# Time step
dt = T / N
time = np.linspace(0, T, N)

# Brownian motion component
W = np.random.standard_normal(size=N) * np.sqrt(dt)

# Initialize arrays
S = np.zeros(N)
S[0] = S0
lambda_t = lambda_0
jump_times = []

# Simulate the combined process
for t in range(1, N):
    # Jump component with Hawkes effect
    if np.random.rand() < lambda_t * dt:
        J = np.random.normal(loc=mu_jump, scale=sigma_jump)
        jump_times.append(t * dt)
        lambda_t += alpha  # Increase intensity due to the jump
    else:
        J = 0
    
    # Update price with Brownian motion and jump
    dS = S[t-1] * (mu * dt + sigma * W[t]) + S[t-1] * (np.exp(J) - 1)
    S[t] = S[t-1] + dS
    
    # Decay the intensity
    lambda_t = lambda_0 + (lambda_t - lambda_0) * np.exp(-beta * dt)

# Plot the simulated price path
plt.plot(time, S, label="Price Process")
plt.scatter(jump_times, [S[int(t / dt)] for t in jump_times], color="red", marker="o", label="Jumps")
plt.title("Jump Diffusion Process with Hawkes Self-Exciting Intensity")
plt.xlabel("Time")
plt.ylabel("Asset Price")
plt.legend()
plt.show()
