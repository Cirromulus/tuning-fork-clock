#!/usr/bin/python

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import sqlite3 as sq
from argparse import ArgumentParser
import data # definitions
from scipy.signal import savgol_filter


def readCsv(filename) -> pd.DataFrame:
    return pd.read_csv(filename, usecols=[0, 1, 2], names=['Period [us]', 'Frequency [Hz]', 'Temp [0.01 DegC]'])

def readSqlite(filename) -> pd.DataFrame:
    con = sq.connect(f"file:{filename}?mode=ro", uri=True)
    plausibility_filters = [
        f"{data.TABLE_FORMAT['temperature'].name} != 0", # TODO: Better indication of a failed temperature measurement
        # f"{data.TABLE_FORMAT['period'].name} > "
    ]
    return pd.read_sql_query("SELECT * from logdata WHERE " + ' AND '.join(plausibility_filters), con)


parser = ArgumentParser(
            prog='plot.py',
            description='Plots statistics about data of tuning fork')

parser.add_argument('database')
parser.add_argument('--with_filtered', default=False, action='store_true')
args = parser.parse_args()
print (args)

dataframe = readSqlite(args.database)

print ("Data:")
print (dataframe)
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

period_smooth = None
if args.with_filtered:
    order = 3
    period_smooth = savgol_filter(columns['period'], max(order + 1, int(len(period) / 1000)), order)
    printStdDev("filtered period", period_smooth)

# First: Just print the data we have.
fig, ax1 = plt.subplots()
ax2 = ax1.twinx()
ax1.set_xlabel('Sample')
ax1.set_ylabel('Period [us]', color='green')
ax1.plot(period, 'green')
if period_smooth:
    ax1.plot(period_smooth, 'lightgreen')
ax2.set_ylabel('Temperature [Celsius]', color='red')
ax2.plot(temp, 'darkred')


# Now: Try to find a correlation

def getRangeAndBin(name):
    range = (min(columns[name]) - 1, max(columns[name]) + 1)
    resolution = 1 / data.TABLE_FORMAT[name].fractional
    bin = max(1, (range[1] - range[0]) * resolution)
    return (range, bin)

period_range, period_bin = getRangeAndBin('period')
temp_range, temp_bin = getRangeAndBin('temperature')
print (f"Period range: {period_range} -> bin {period_bin}")
print (f"Temp range  : {temp_range} -> bin {temp_bin}")

def fit(x, y, order):
    fit = np.polyfit(x, y, order)
    print (fit)
    return np.poly1d(fit)

plt.figure()
period_fit = fit(temp, period, 2)  # We expect a linear relationship
valid_period_fit_range = np.arange(temp_range[0], temp_range[1], data.TABLE_FORMAT['temperature'].fractional)

plt.hist2d(temp, period,
           range=(temp_range, period_range),
           bins=(int(temp_bin), int(period_bin)),
           )
plt.scatter(temp, period,
            alpha=.15)
plt.plot(valid_period_fit_range, period_fit(valid_period_fit_range), 'red')

plt.ticklabel_format(style='plain')
plt.xlabel('Temperature [Celsius]')
plt.ylabel('Cycle Period [us]')
plt.show()

# OK, and apply inverse of cerrelation to try linearize period