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
# for name, (column, coldesc) in columns.items():
#     if name == 'period':
#         continue
#     print (f"{name}: {coldesc}")
#     # Three integers (nrows, ncols, index)
#     current_axis = plt.subplot(rows, cols, i, sharex=first_axis)
#     if not first_axis:
#         first_axis = current_axis
#         plt.xlabel("Time [s]")
#     else:
#         plt.tick_params('x', labelbottom=False)
#     plt.plot(sample_time_s, column, label=name)
#     current_axis.set_ylabel(f"{coldesc.name} [{coldesc.unit}]")
#     plt.ticklabel_format(style='plain')

#     i -= 1
# plt.title("All available Data")

## --------------------

actual_period = columns['period'][0]
estimate_period = columns['period_estimate'][0]
initial_diff = actual_period[0] - estimate_period[0]
print (f"Initial difference: {initial_diff}")
estimate_period_norm = estimate_period - initial_diff

fig, ax1 = plt.subplots()
ax1.set_xlabel('Time [s]')
ax2 = ax1.twinx()
ax1.set_ylabel('Period per one Cycle [us]')
ax1.plot(sample_time_s, actual_period, 'green', label="Measured Period")
ax1.plot(sample_time_s, estimate_period_norm, 'red', label="Estimated Period")

ax2.set_ylabel('Difference [us]')
ax2.plot(sample_time_s, estimate_period_norm, 'teal', label='Difference')
ax2.fill_between(sample_time_s, estimate_period_norm, 0,
        color='teal', alpha=.5)
plt.title("Estimation quality")
ax1.legend()

ax2.legend()
plt.show()
