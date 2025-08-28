import numpy as np
from scipy.stats import norm
import pandas as pd

# Step 1: Create a range of x values from -5 to 5 with small steps (e.g., 0.01)
x_values = np.arange(-5, 5, 0.01)

# Step 2: Calculate the corresponding Phi(x) values using scipy's norm.cdf function
phi_values = norm.cdf(x_values)

# Step 3: Save the x_values and phi_values to a CSV file to use as a lookup table later
lookup_table = pd.DataFrame({'x': x_values, 'Phi(x)': phi_values})
lookup_table.to_csv('phi_lookup_table.csv', index=False)

# Output the file path
# '/mnt/data/phi_lookup_table.csv'
