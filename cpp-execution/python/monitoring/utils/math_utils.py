# monitoring/utils/math_utils.py

import numpy as np
from scipy import stats
import pandas as pd

def moving_average(data, window):
    """
    Calculate the moving average of a data series.
    
    Args:
        data: List of numeric values
        window: Window size for moving average
        
    Returns:
        List of moving averages (first window-1 elements are None)
    """
    if not data:
        return []
        
    result = []
    for i in range(len(data)):
        if i < window - 1:
            result.append(None)
        else:
            window_values = data[i - window + 1:i + 1]
            result.append(sum(window_values) / window)
            
    return result

def outliers_zscore(data, threshold=3.0):
    """
    Detect outliers using Z-score method.
    
    Args:
        data: List of numeric values
        threshold: Z-score threshold for outlier detection
        
    Returns:
        List of indices of outliers
    """
    if not data:
        return []
        
    z_scores = np.abs(stats.zscore(data))
    return np.where(z_scores > threshold)[0].tolist()

def outliers_iqr(data, k=1.5):
    """
    Detect outliers using IQR method.
    
    Args:
        data: List of numeric values
        k: IQR multiplier for outlier detection
        
    Returns:
        List of indices of outliers
    """
    if not data:
        return []
        
    q1 = np.percentile(data, 25)
    q3 = np.percentile(data, 75)
    iqr = q3 - q1
    
    lower_bound = q1 - k * iqr
    upper_bound = q3 + k * iqr
    
    outliers = []
    for i, value in enumerate(data):
        if value < lower_bound or value > upper_bound:
            outliers.append(i)
            
    return outliers

def linear_regression(x, y):
    """
    Perform linear regression.
    
    Args:
        x: Independent variable values
        y: Dependent variable values
        
    Returns:
        Tuple of (slope, intercept, r_value, p_value, std_err)
    """
    return stats.linregress(x, y)

def exponential_moving_average(data, span):
    """
    Calculate exponential moving average (EMA).
    
    Args:
        data: List or array of numeric values
        span: Span parameter for EMA
        
    Returns:
        Array of EMA values
    """
    return pd.Series(data).ewm(span=span).mean().values

def simple_forecast(data, periods=1):
    """
    Generate a simple forecast based on linear regression.
    
    Args:
        data: Historical data points
        periods: Number of periods to forecast
        
    Returns:
        List of forecasted values
    """
    if len(data) < 2:
        return data * periods  # Just repeat the data if insufficient
        
    x = np.arange(len(data))
    slope, intercept, _, _, _ = linear_regression(x, data)
    
    forecast = []
    for i in range(1, periods + 1):
        next_x = len(data) + i - 1
        next_y = slope * next_x + intercept
        forecast.append(next_y)
        
    return forecast

def calculate_percentiles(data, percentiles=[25, 50, 75, 90, 95, 99]):
    """
    Calculate percentiles for a dataset.
    
    Args:
        data: List or array of numeric values
        percentiles: List of percentiles to calculate
        
    Returns:
        Dictionary mapping percentiles to values
    """
    result = {}
    for p in percentiles:
        result[p] = np.percentile(data, p)
    return result

def calculate_statistics(data):
    """
    Calculate common statistics for a dataset.
    
    Args:
        data: List or array of numeric values
        
    Returns:
        Dictionary of statistics
    """
    return {
        "mean": np.mean(data),
        "median": np.median(data),
        "std": np.std(data),
        "min": np.min(data),
        "max": np.max(data),
        "count": len(data),
        "percentiles": calculate_percentiles(data)
    }