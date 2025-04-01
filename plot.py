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
        f"{data.TABLE_FORMAT['temperature'].name} > -6", # TODO: Better indication of a failed temperature measurement
        f"{data.TABLE_FORMAT['period'].name} > {data.TABLE_FORMAT['period'].denormalize(2250)}",    # Ugly AF. Should do a difference-between-samples instead
    ]
    query = "SELECT * from logdata WHERE " + ' AND '.join(plausibility_filters)
    print (query)
    return pd.read_sql_query(query, con)


parser = ArgumentParser(
            prog='plot.py',
            description='Plots statistics about data of tuning fork')

parser.add_argument('database')
parser.add_argument('--no-emit-plot', default=True, action='store_false',dest ='emit_plot')
args = parser.parse_args()
# print (args)

dataframe = readSqlite(args.database)

print ("Data:")
print (dataframe)
print ("Estimated covariance between columns:")
print (dataframe.cov())
print ()


columns = {}
for colname, desc in data.TABLE_FORMAT.items():
    columns[colname] = desc.normalize(dataframe[desc.name])

# dumb aliases
period = columns['period']
temp = columns['temperature']

sample_time_us = np.cumsum(np.array(dataframe[data.TABLE_FORMAT['period'].name]))
sample_time_s = sample_time_us / 10000000
duration_of_measurement_us = sample_time_us[-1]
avg_duration_of_sample_us = duration_of_measurement_us / len(dataframe)
print (f"Duration of measurement run: {duration_of_measurement_us / 1000000}s (based on reference clock)")

def dampen(factor, xs, time_delta):
    rolling_value = xs[0]
    ret = []
    for x, td in zip(xs, time_delta):
        diff = x - rolling_value
        # I am too dumb to get the units correct.
        # Assume that the time delta is somewhat in there.
        # This means, after fitting, having the same sample rate is
        # pretty important to a correct function.
        rolling_value += diff * factor
        ret.append(rolling_value)
    return ret

# plt.figure()
# n = 5
# delta = .5
# for i in range(0, n):
#     f = i / n
#     delta = np.arange(0, 1, .1)
#     factors = [scaleFactor(f, s) for s in delta]
#     print (delta)
#     print (factors)
#     plt.plot(delta, factors, label=str(f))
# plt.legend()
# plt.show()
# exit()

def printStdDev(name, thing, sample_mean = None):
    std, mean = (np.std(thing), np.mean(thing))
    seconds_per_day = 24 * 60 * 60
    if not sample_mean:
        # this is if we apply a difference, which is of course offset to an absolute value
        sample_mean = mean
    print (f"Standard deviation of the {name}: {std} us (mean {mean}) -> {std * seconds_per_day / sample_mean} s / day")
    return std

uncorrected_period_std = printStdDev("period", period)

def legendAllAxes(*axis):
    lines = [line for ax in axis for line in ax.get_lines()]
    labs = [l.get_label() for l in lines if not '_' in l.get_label() ]
    axis[0].legend(lines, labs)

if args.emit_plot:
    damped_temperatures = []
    steps = 10
    scaled_interest_bounds = (.015, .001)
    def factorScaled(f):
        return pow(f, 3)
    for i in range(0, steps):
        lin_f = 1 * ((i+1) / steps)
        factor = min(scaled_interest_bounds) + max(scaled_interest_bounds) * factorScaled(lin_f)
        print (f"Factor {lin_f}: {factor}")
        damped_temperatures += [(lin_f, factor, dampen(factor, temp, period / 1000000))]

    # First: Just print the data we have.
    fig, ax1 = plt.subplots()
    ax2 = ax1.twinx()
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Period [us]')
    ax1.plot(sample_time_s, period, 'green', label='Period')
    ax2.set_ylabel('Temperature [Celsius]')
    ax2.plot(sample_time_s, temp, 'darkred', label='Temperature')
    for (lin_f, factor, damped_temp) in damped_temperatures:
        if factor > min(scaled_interest_bounds) and factor < max(scaled_interest_bounds):
            ax2.plot(sample_time_s,damped_temp,
                color=f'#{int(lin_f * 0xFF):02x}{int((1-lin_f) * 0xFF):02x}115A', label=str(factor))
    ax1.legend(loc="upper right")
    ax2.legend(loc="upper left")
    plt.ticklabel_format(style='plain')
    plt.title("Measurement data")

# plt.show()
# exit()

# Now: Try to find a correlation
            # from scipy import signal
            # import numpy as np
            # import matplotlib.pyplot as plt

            # def f(x,A,t,mu,sigma):
            #     y1 = A*np.exp(-x/t)
            #     y2 = A*np.exp(-0.5*(x-mu)**2/sigma**2)
            #     return signal.convolve(y1,y2)/ sum(y2)

            # x = np.arange(-10,10,0.01)

            # from scipy.optimize import curve_fit
            # popt, pcov = curve_fit(f, x_data, y_data, 'same')

perhaps_best_damp_factor = .0019
perhaps_best_temp = dampen(perhaps_best_damp_factor, temp, period / 1000000)

def fit(x, y, order):
    fit, cov = np.polyfit(x, y, order, cov=True)
    def asFunction(factors):
        ret = "f(x) = "
        ret += " + ".join([f'{f}x^{i}' for i, f in enumerate(reversed(factors))])
        return ret

    print (f"**\nFactors of best fit: {asFunction(fit)}\n**")
    print ("Covariance of fit:")
    print (cov)
    return np.poly1d(fit)

print ("On scaled data:")
period_fit = fit(temp, period, 2)  # We expect a linear relationship, but let's add a factor
print (f"On damped data ({perhaps_best_damp_factor}):")
period_damped_fit = fit(perhaps_best_temp, period, 2)  # We expect a linear relationship, but let's add a factor
print ("Unscaled data: ")
# TODO: this could be directly separate without the whole plot stuff.
fit(dataframe[data.TABLE_FORMAT['temperature'].name], dataframe[data.TABLE_FORMAT['period'].name], 1)


if args.emit_plot:
    def getRangeAndBin(name):
        range = (min(columns[name]) - 1, max(columns[name]) + 1)
        resolution = data.TABLE_FORMAT[name].denormalize(1)
        bin = max(1, (range[1] - range[0]) * resolution)
        return (range, bin)

    period_range, period_bin = getRangeAndBin('period')
    temp_range, temp_bin = getRangeAndBin('temperature')
    # print (f"Period range: {period_range} -> bin {period_bin}")
    # print (f"Temp range  : {temp_range} -> bin {temp_bin}")
    valid_period_fit_range = np.arange(temp_range[0], temp_range[1], data.TABLE_FORMAT['temperature'].fractional)

    # limit scattering to less samples to reduce time overhead
    num_samples_to_scatter = min(1000, len(period))
    ss_step_size = int(len(period) / num_samples_to_scatter)
    period_ss = period[0::ss_step_size]
    print(f"subsampling for scatterplot to {len(period_ss)} elements")

    plt.figure()
    plt.hist2d(temp, period,
            range=(temp_range, period_range),
            bins=(int(temp_bin), int(period_bin)),
            )
    plt.scatter(temp[0::ss_step_size], period_ss,
                alpha=.15,
                label="Measured samples", color="blue")
    plt.scatter(perhaps_best_temp[0::ss_step_size], period_ss,
                alpha=.15, color="lightgreen", label=f"Damping of {perhaps_best_damp_factor}")

    plt.plot(valid_period_fit_range, period_fit(valid_period_fit_range),
            'red', label="Best fit")
    plt.plot(valid_period_fit_range, period_damped_fit(valid_period_fit_range),
            'teal', label="Best fit (damped)")

    plt.legend()
    plt.ticklabel_format(style='plain')
    plt.xlabel('Temperature [Celsius]')
    plt.ylabel('Cycle Period [us]')
    plt.title("Correlation Data")


# OK, and apply inverse of cerrelation to try linearize period

estimated_period = period_fit(temp)
estimated_period_damped_fit = period_damped_fit(perhaps_best_temp)
difference_period = estimated_period - period
difference_period_damped_fit = estimated_period_damped_fit - period
cumulative_difference = np.cumsum(np.array(difference_period))
corrected_period_std = printStdDev("Corrected Period", difference_period, sample_mean=np.mean(period))

improvement_ratio = uncorrected_period_std / corrected_period_std
print (f"With linear fit for period estimation, we got an improvement factor of {improvement_ratio}.")
print (f"Hypothetical drift with given correction in this specific dataset: {(cumulative_difference[-1] * avg_duration_of_sample_us) / 1000000} seconds")

if args.emit_plot:
    fig, ax1 = plt.subplots()
    plt.title("Correction Factor Estimation")
    ax2 = ax1.twinx()
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Period [us]')
    ax1.plot(sample_time_s, period, 'green', label="Measured Period")
    ax1.plot(sample_time_s, estimated_period, 'darkred', label="Estimated Period")
    ax1.plot(sample_time_s, estimated_period_damped_fit, 'orange', label="Estimated Period (Damped)")

    ax2.set_ylabel('Difference [us]')
    ax2.plot(sample_time_s, difference_period,
            'blue', label='Difference')
    ax2.plot(sample_time_s, difference_period_damped_fit,
            'teal', label='Difference (Damped)')
    ax2.axhline(0, linestyle='dashed', color='lightblue', alpha=.5)
    ax2.fill_between(sample_time_s, difference_period, 0,
            color='blue', alpha=.5)

    # unten, oben = ax2.get_ylim()
    # yoffs = 0# unten
    # ax2.plot(sample_time_s, abs(difference_period)+yoffs,
    #           color='teal', alpha=.25, label="Difference (abs)")
    # reset ylim to have difference really touching bottom
    # ax2.set_ylim((unten, oben))
    legendAllAxes(ax1, ax2)


# This is TODO...
# We can actually correlate the temperature change speed with the correction error!

temperature_change = np.diff(temp)
temperature_change_rate = temperature_change / np.array(dataframe[data.TABLE_FORMAT['period'].name])[:1]
smoothed_temp_change_rate = savgol_filter(temperature_change_rate, 500, 2)

if args.emit_plot and False:
    fig, ax1 = plt.subplots()
    ax2 = ax1.twinx()
    plt.title("Drift Evaluation")
    plt.xlabel('Time [s]')
    ax1.set_ylabel('Difference [us]')
    ax2.set_ylabel('Temperature change rate [Celsius / s]')
    ax1.plot(sample_time_s, difference_period,
            label='Difference from Reference', color='green')
    ax2.plot(sample_time_s[1:], smoothed_temp_change_rate,
            label='Temperature change (filtered)', color="red")
    legendAllAxes(ax1, ax2)


if args.emit_plot:
    plt.show()
