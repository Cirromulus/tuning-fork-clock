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
        f"{data.TABLE_FORMAT['period'].name} > 2250",    # Ugly AF. Should do a difference-between-samples instead
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

def printStdDev(name, thing, sample_mean = None):
    std, mean = (np.std(thing), np.mean(thing))
    seconds_per_day = 24 * 60 * 60
    if not sample_mean:
        # this is if we apply a difference, which is of course offset to an absolute value
        sample_mean = mean
    print (f"Standard deviation of the {name}: {std} us (mean {mean}) -> {std * seconds_per_day / sample_mean} s / day")
    return std

uncorrected_period_std = printStdDev("period", period)

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
plt.ticklabel_format(style='plain')
plt.title("Raw data")


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
    fit, cov = np.polyfit(x, y, order, cov=True)
    def asFunction(factors):
        ret = "f(x) = "
        ret += " + ".join([f'{f}x^{i}' for i, f in enumerate(reversed(factors))])
        return ret

    print (f"Factors of best fit: {asFunction(fit)}")
    print ("Covariance:")
    print (cov)
    return np.poly1d(fit)

period_fit = fit(temp, period, 1)  # We expect a linear relationship
valid_period_fit_range = np.arange(temp_range[0], temp_range[1], data.TABLE_FORMAT['temperature'].fractional)

plt.figure()
plt.hist2d(temp, period,
           range=(temp_range, period_range),
           bins=(int(temp_bin), int(period_bin)),
           )
plt.scatter(temp, period,
            alpha=.15,
            label="Measured samples")
plt.plot(valid_period_fit_range, period_fit(valid_period_fit_range),
         'red', label="Best fit")

plt.legend()
plt.ticklabel_format(style='plain')
plt.xlabel('Temperature [Celsius]')
plt.ylabel('Cycle Period [us]')
plt.title("Correlation Data")

# OK, and apply inverse of cerrelation to try linearize period

estimated_period = period_fit(temp)
difference_period = estimated_period - period
corrected_period_std = printStdDev("Corrected Period", difference_period, sample_mean=np.mean(period))

improvement_ratio = uncorrected_period_std / corrected_period_std
print (f"With linear fit for period estimation, we got an improvement factor of {improvement_ratio}.")

fig, ax1 = plt.subplots()
ax2 = ax1.twinx()
ax1.set_xlabel('Sample Number')
ax1.set_ylabel('Period [us]', color='green')
ax1.plot(period, 'green', label="Measured")
ax1.plot(estimated_period, 'red', label="Estimated")
ax1.legend(loc='upper right')

ax2.set_ylabel('Difference [us]')
ax2.plot(difference_period, 'blue', label='Difference')
ax2.axhline(0, linestyle='dashed', color='lightblue', alpha=.5)
ax2.legend(loc='upper center')
plt.title("Correction Factor Estimation")
plt.show()

