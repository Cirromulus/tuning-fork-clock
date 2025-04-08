#!/usr/bin/python

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import sqlite3 as sq
from argparse import ArgumentParser
import data # definitions
from scipy.signal import savgol_filter, argrelextrema, find_peaks

def readSqlite(filename) -> pd.DataFrame:
    con = sq.connect(f"file:{filename}?mode=ro", uri=True)
    return pd.read_sql_query("SELECT * from logdata", con)

parser = ArgumentParser(
            prog='plot.py',
            description='Plots statistics about data of tuning fork')

parser.add_argument('database')
parser.add_argument('--no-emit-plot', default=True, action='store_false',dest ='emit_plot')
args = parser.parse_args()

dataframe = readSqlite(args.database)
print (dataframe)

columns = {}
for colname, desc in data.TABLE_FORMAT.items():
    columns[colname] = (desc.normalize(dataframe[desc.name]), desc)
sample_time_s = np.cumsum(dataframe[data.TABLE_FORMAT['period'].name] / 1000000) # in seconds

cols = 1
rows = len(columns) - 1 # without period
first_axis = None
i = rows
plt.figure("All available data")
for name, (column, coldesc) in columns.items():
    if name == 'period':
        continue
    print (f"{name}: {coldesc}")
    # Three integers (nrows, ncols, index)
    current_axis = plt.subplot(rows, cols, i, sharex=first_axis)
    if not first_axis:
        first_axis = current_axis
        plt.xlabel("Time [s]")
    else:
        plt.tick_params('x', labelbottom=False)
    plt.plot(sample_time_s, column, label=name)
    current_axis.set_ylabel(f"{coldesc.name} [{coldesc.unit}]")
    plt.ticklabel_format(style='plain')

    i -= 1
# plt.title("All available Data")

## --------------------

actual_period = columns['period'][0]
estimate_period = columns['period_estimate'][0]
constant_diff = estimate_period[0] - actual_period[0]
estimate_period_norm = estimate_period - constant_diff
estimate_diff_norm = estimate_period_norm - actual_period
estimate_diff_cum = columns['period'][1].normalize(np.cumsum(estimate_diff_norm))


print (actual_period)
print (estimate_period)
print (f"Initial difference: {constant_diff}")
print (estimate_period_norm)
print (estimate_diff_norm)

plt.figure("Precision")
ax1 = plt.subplot(212)
ax1.set_xlabel('Time [s]')
ax2 = ax1.twinx()
ax1.set_ylabel('Period per one Cycle [us]')
ax1.plot(sample_time_s, actual_period, 'green', label="Measured Period")
ax1.plot(sample_time_s, estimate_period_norm, 'red', label="Estimated Period")
ax1.legend(loc="upper left")

ax2.set_ylabel('Difference [us]')
ax2.fill_between(sample_time_s, estimate_diff_norm, 0,
        color='orange', alpha=.5, label='Difference per period')

ax2.legend(loc="upper right")

ax3 = plt.subplot(211, sharex=ax1)
ax3.plot(sample_time_s, estimate_diff_cum, 'teal', label='Cumulative Difference (Drift)')
ax3.tick_params('x', labelbottom=False)
ax3.set_ylabel('Difference [s]')
ax3.legend()
plt.title("Estimation quality")

plt.show()
