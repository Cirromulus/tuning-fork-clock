#!/usr/bin/python

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import sqlite3 as sq
import data

from scipy.signal import savgol_filter


def readCsv(filename) -> pd.DataFrame:
    return pd.read_csv(filename, usecols=[0, 1, 2], names=['Period [us]', 'Frequency [Hz]', 'Temp [0.01 DegC]'])

def readSqlite(filename) -> pd.DataFrame:
    con = sq.connect(filename)
    plausibility_filters = [
        f"{data.TABLE_FORMAT['temperature'].name} != 0" # TODO: Better indication of a failed temperature measurement
    ]
    return pd.read_sql_query("SELECT * from logdata WHERE " + ' AND '.join(plausibility_filters), con)

# TODO: Make parameter, obvsly
dataframe = readSqlite('2025-03-29_12-00-05_sensor_log.db')
# print (dataframe)

print ("Estimated covariance between columns:")
print (dataframe.cov())
print ("Variance:")
print (dataframe.var())


columns = {}
for colname, desc in data.TABLE_FORMAT.items():
    columns[colname] = desc.normalize(dataframe[desc.name])

# dumb aliases
period = columns['period']
temp = columns['temperature']

def printStdDev(name, thing):
    std, mean = (np.std(thing), np.mean(thing))
    seconds_per_day = 24 * 60 * 60
    print (f"Standard deviation of the {name}: {std} us (per {mean}) -> {std * seconds_per_day / mean} s / day")

printStdDev("period", period)

period_smooth = savgol_filter(columns['period'], int(len(period) / 1000), 3)
printStdDev("filtered period", period_smooth)
fig, ax1 = plt.subplots()
ax2 = ax1.twinx()
ax1.set_xlabel('Sample')
ax1.set_ylabel('Period [us]', color='green')
ax1.plot(period, 'green')
ax1.plot(period_smooth, 'lightgreen')
ax2.set_ylabel('Temperature [Celsius]', color='red')
ax2.plot(temp, 'darkred')

plt.figure()
period_range = (min(period), max(period))
period_resolution = 1
temp_range = (min(temp), max(temp))
temp_resolution = 10
period_bin = (period_range[1] - period_range[0]) * period_resolution
temp_bin = (temp_range[1] - temp_range[0]) * temp_resolution

plt.hist2d(period, temp, range=(period_range, temp_range), bins=(int(period_bin), int(temp_bin)))
plt.show()