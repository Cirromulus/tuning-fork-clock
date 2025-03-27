import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from scipy.signal import savgol_filter

# TODO: Make parameter, obvsly

filename = 'forklog_2025-03-27.csv'
dataframe = pd.read_csv(filename, usecols=[0, 1, 2], names=['Period [us]', 'Frequency [Hz]', 'Temp [0.01 DegC]'])

print (dataframe)

period = dataframe['Period [us]']
freq = dataframe['Frequency [Hz]']
freq_smooth = savgol_filter(freq, 51, 3)
temp = dataframe['Temp [0.01 DegC]'] * 0.01


print (f"Standard deviation of the period: {np.std(period)} us (per {np.mean(period)})")

plt.figure()
plt.plot(freq)
plt.plot(freq_smooth, color='red')
# pltplot(temp)

plt.figure()
period_range = (2200, 2300)
plt.hist2d(period, temp, range=(period_range, (10, 30)), bins=(period_range[1] - period_range[0], 20))
plt.show()