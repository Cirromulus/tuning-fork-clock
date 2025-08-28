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
    # print (f"{name}: {coldesc}")
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

a_smooth_number = 100 # probably "samples"
actual_period = columns['period'][0]
estimate_period = columns['period_estimate'][0]
estimate_diff = actual_period - estimate_period
estimate_diff_smooth = savgol_filter(estimate_diff, a_smooth_number, 2)
estimate_diff_cum = columns['period'][1].normalize(np.cumsum(estimate_diff))

plt.figure("Precision")
ax1 = plt.subplot(212)
ax1.set_xlabel('Time [s]')
ax2 = ax1.twinx()

ax1.set_ylabel('Difference [us/period]')
ax1.plot(sample_time_s, columns['period'][1].normalize(estimate_diff), #0,
        color='orange', alpha=.5, label='Estimation Error per period')
ax1.plot(sample_time_s, columns['period'][1].normalize(estimate_diff_smooth),
        color='orange', alpha=1, label='Estimation E.p.p. (smoothed)')

# have this in the foreground, thus ax*2*
ax2.set_ylabel('Period per one Cycle [us]')
ax2.plot(sample_time_s, actual_period,
            color='green', alpha=.6, label="Measured Period")
ax2.plot(sample_time_s, estimate_period,
            color='red', alpha=.7, label="Estimated Period")

ax1.legend(loc="upper left")
ax2.legend(loc="upper right")
ax1.grid(visible='major', axis='y')

ax3 = plt.subplot(211, sharex=ax1)
ax3.plot(sample_time_s, estimate_diff_cum, 'teal', label='Cumulative Difference (Drift)')
ax3.tick_params('x', labelbottom=False)
ax3.set_ylabel('Difference [s]')
ax3.legend()
plt.title("Estimation quality")

plt.show()
