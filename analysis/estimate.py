#!/usr/bin/python

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import sqlite3 as sq
from argparse import ArgumentParser
import data # definitions
from scipy.signal import savgol_filter, argrelextrema, find_peaks


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
            prog='estimate.py',
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

# TODO: THis is useful for only plotting, but we also calculate.
# TODO Split.
# columns = {}
# for colname, desc in data.TABLE_FORMAT.items():
#     columns[colname] = desc.normalize(dataframe[desc.name])

# dumb aliases
period = dataframe[data.TABLE_FORMAT['period'].name]
period_meta = data.TABLE_FORMAT['period']
temp = dataframe[data.TABLE_FORMAT['temperature'].name]
temp_meta = data.TABLE_FORMAT['temperature']

sample_time_us = np.cumsum(np.array(period))
sample_time_s = sample_time_us / 10000000
duration_of_measurement_us = sample_time_us[-1]
avg_duration_of_sample_us = duration_of_measurement_us / len(dataframe)
print (f"Duration of measurement run: {duration_of_measurement_us / 1000000}s (based on reference clock)")

def printStdDev(name, thing, sample_mean = None):
    std, mean = (np.std(thing), np.mean(thing))
    seconds_per_day = 24 * 60 * 60
    if not sample_mean:
        # this is if we apply a difference, which is of course offset to an absolute value
        sample_mean = mean
    print (f"Standard deviation of the {name}: {std} us (mean {mean}) -> {std * seconds_per_day / sample_mean} s / day")
    return std

uncorrected_period_std = printStdDev("period", period_meta.normalize(period))

def legendAllAxes(*axis):
    lines = [line for ax in axis for line in ax.get_lines()]
    labs = [l.get_label() for l in lines if not '_' in l.get_label() ]
    axis[0].legend(lines, labs)

def dampen(factor, xs, time_delta = None):
    rolling_value = xs[0]
    ret = []
    for x, td in zip(xs, time_delta):
        diff = x - rolling_value
        # I am too dumb to get the units correct.
        # Assume that the time delta is somewhat in there.
        # This means, after fitting, having the same sample rate is
        # pretty important to a correct function.
        # Hmm, perhaps 1-(1/(1+x^2))?
        rolling_value += diff * factor
        ret.append(rolling_value)
    return ret

def goodSavgolBecauseILookedAtItHard(x):
    a_smooth_number = 50   # probably "samples"
    return savgol_filter(x, a_smooth_number, 1)

def getExtrema(thing):
    min_expected_peak_distance = 60 # FIXME: Normalize with sample density
    maxima = find_peaks(thing,
                        distance=min_expected_peak_distance,
                        width=min_expected_peak_distance)
    minima = find_peaks(-thing, # negated!
                        distance=min_expected_peak_distance,
                        width=min_expected_peak_distance)
    return (maxima[0], minima[0])

def printExtrema(hansbob, name):
    print (f"Found {len(hansbob[0])}, {len(hansbob[1])} extrema for {name}")
    print (f"  First few maxima: {hansbob[0][:5]}")

def correlateExtrema(left_i, right_i, sample_time, max_diff):
    r_o = 0   # offset from right_i
    l_o = 0
    ret_l = []
    ret_r = []
    ret_diff = []
    while l_o < len(left_i) and r_o < len(right_i):
        l = left_i[l_o]
        r = right_i[r_o]
        # print (f"left[{l}] is at {sample_time[l]}s")
        # print (f"current right[{r}] is at {sample_time[r]}s")
        diff_t = sample_time[r] - sample_time[l]
        # print (f"         diff: {diff_t}s")
        if diff_t < -max_diff:
            # print (f"Difference negative, advancing right. >")
            r_o += 1
        elif diff_t > max_diff:
            # print (f"Difference too big. advancing left.   <")
            l_o += 1
        else:
            # print(f'Difference plausible, taking')
            ret_l.append(l)
            ret_r.append(r)
            ret_diff.append(diff_t)
            r_o += 1
            l_o += 1 # comment in to only take the first match
    return ret_l, ret_r, ret_diff

def correlateMinMax(left, right, time, max_diff_s):
    left_max, right_max, diff_maxes = correlateExtrema(left[0], right[0], time, max_diff_s)
    left_min, right_min, diff_mins = correlateExtrema(left[1], right[1], time, max_diff_s)
    return ((left_max, left_min), (right_max, right_min), diff_maxes + diff_mins)

def getPhaseLatency(left, right, sample_time, window):
    left_extrema = getExtrema(left)
    # print (f"left: {left_extrema}")
    right_extrema = getExtrema(right)
    # print (f"right: {right_extrema}")
    common_temp_extrema, common_period_extrema, common_time_diffs = correlateMinMax(left_extrema, right_extrema, sample_time, window)
    return (np.array(common_time_diffs).mean(), common_temp_extrema, common_period_extrema, common_time_diffs)

temp_smooth = goodSavgolBecauseILookedAtItHard(temp)
period_smooth = goodSavgolBecauseILookedAtItHard(period)
# # for testing, limit to one element:
# temp_extrema = (temp_extrema[0][:1],temp_extrema[1][:1])
# period_extrema = (period_extrema[0][:1],period_extrema[1][:1])

probably_not_slower_than = 80 #s
# I think that if time and period are smoothed by the same amount,
# then the average time difference is not affected by the smoothing?
# Unfortunately, the a smoothed version is necessary for the
# argrelextrema function to work properly
base_latency_s, common_temp_extrema, common_period_extrema, common_time_diffs = getPhaseLatency(temp_smooth, period_smooth, sample_time_s, probably_not_slower_than)
def getAvgPhaseLatencyAgainstPeriod(input_curve):
    return getPhaseLatency(input_curve, period_smooth, sample_time_s, probably_not_slower_than)[0]

# print("Common extrema:")
# printExtrema(common_temp_extrema, "common_temp")
# printExtrema(common_period_extrema, "common_period")
print (f"mean time difference of period reacting on measured period: {base_latency_s}s")

if args.emit_plot:# and False: # this is not too helpful
    plt.figure()
    plt.hist(common_time_diffs, probably_not_slower_than, label="Extrema")
    plt.axvline(base_latency_s, color="red", label="Mean", linestyle="dotted")
    plt.axvline(np.median(common_time_diffs), color="blue", label="Median", linestyle="dotted")
    plt.xlabel("Time difference [s]")
    plt.ylabel("Num occurrences")
    plt.legend()
    plt.title("Distribution of Time-Difference between Extrema")

# plt.show()
# exit()

damped_temperatures = []
steps = 15
scaled_interest_bounds = (.01, .001)   # Hm, less manual please
def factorScaled(f):
    return pow(f, 3)
for i in range(0, steps):
    lin_f = 1 * ((i+1) / steps)
    factor = min(scaled_interest_bounds) + max(scaled_interest_bounds) * factorScaled(lin_f)
    # print (f"Factor {lin_f}: {factor}")
    damped_curve = np.array(dampen(factor, temp, period / 1000000))
    avg_delay = getAvgPhaseLatencyAgainstPeriod(damped_curve)
    damped_temperatures += [(lin_f, factor, damped_curve, avg_delay)]

# Print temp and period, along with the damping-series
if args.emit_plot:
    # First: Just print the data we have.
    fig, ax1 = plt.subplots()
    ax2 = ax1.twinx()
    ax1.set_xlabel('Time [s]')
    ax1.ticklabel_format(style='plain')
    ax1.set_ylabel('Period per cycle [us]')
    ax1.plot(sample_time_s, period_meta.normalize(period), 'orange', alpha=.7, label='Period')
    ax1.plot(sample_time_s, period_meta.normalize(period_smooth), 'orangered', alpha=.7, label='Period (Smooth)')
    ax1.scatter(sample_time_s[common_period_extrema[0]], period_meta.normalize(period_smooth[common_period_extrema[0]]),
                s=100, color="red", marker='1', label="Maxima")
    ax1.scatter(sample_time_s[common_period_extrema[1]], period_meta.normalize(period_smooth[common_period_extrema[1]]),
                s=100, color="red", marker='2', label="Minima")

    ax2.set_ylabel('Scaled Temperature [Celsius]')
    ax2.plot(sample_time_s, temp_meta.normalize(temp), 'firebrick', alpha=.7, label='Temperature')
    ax2.plot(sample_time_s, temp_meta.normalize(temp_smooth), 'darkred', alpha=.7, label='Temperature (Smooth)')
    ax2.scatter(sample_time_s[common_temp_extrema[0]], temp_meta.normalize(temp_smooth[common_temp_extrema[0]]),
                s=100, color="blue", marker='1', label="Maxima")
    ax2.scatter(sample_time_s[common_temp_extrema[1]], temp_meta.normalize(temp_smooth[common_temp_extrema[1]]),
                s=100, color="blue", marker='2', label="Minima")


    for (lin_f, factor, damped_temp, avg_diff) in damped_temperatures:
        if factor > min(scaled_interest_bounds) and factor < max(scaled_interest_bounds):
            ax2.plot(sample_time_s, temp_meta.normalize(damped_temp),
                color=f'#{int(lin_f * 0xFF):02x}{int((1-lin_f) * 0xFF):02x}115A',
                #label=f"{factor}: {avg_diff}"
                # label="Damped Temperature"
                # TODO: Generate only one of these descriptions but with all colors
                )
    ax1.legend(loc="upper right")
    ax2.legend(loc="upper left")
    plt.title("Measurement data")

# plt.show()
# exit()

# Now: Grade the factors by min(abs(diff))
factors = [factor for (_, factor, _, avg_diff) in damped_temperatures]
diffs = [avg_diff for (_, factor, _, avg_diff) in damped_temperatures]
zero_crossings_i = np.where(np.diff(np.signbit(diffs)))[0]

def linearWhereZero(x1, y1, x2, y2):
    slope = (y2 - y1) / (x2 - x1)
    return y1 - x1 * slope

# convert from index to interpolated x value
zero_crossings = []
for i in zero_crossings_i:
    if diffs[i] == 0:
        zero_crossings.append(factors[i])
    # oh noez, wee need to do the math
    # I am Caveman, I make machine do think
    zero_crossings.append(linearWhereZero(diffs[i], factors[i], diffs[i+1], factors[i+1]))

print (f"Interpolated crossing points: factor of {zero_crossings}")

if args.emit_plot:
    plt.figure()
    plt.title(f"Estimation of best damp factor: {zero_crossings[0]}")

    factors = [factor for (_, factor, _, avg_diff) in damped_temperatures]
    diffs = [avg_diff for (_, factor, _, avg_diff) in damped_temperatures]
    plt.plot(factors, diffs, label="Calculated")
    plt.axhline(0, linestyle='dashed', color='lightblue', alpha=.5, label="Ideal zero")
    for zero_crossing in zero_crossings:
        plt.axvline(zero_crossing, color="green", alpha=.75, label="Interpolated Crossing Point")
    plt.legend()
    plt.xlabel("Damp-Factor")
    plt.ylabel("Avg. Extremum Time Difference")

# plt.show()
# exit()
if len(zero_crossings) != 1:
    print ("We did not find a single zero crossing of best factor fit!")
    print ("Can't continue. You need to tune hard-coded values, probably...")
    plt.show()
    exit()

perhaps_best_damp_factor = zero_crossings[0]
perhaps_best_temp = np.array(dampen(perhaps_best_damp_factor, temp, period / 1000000))

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

fit_degree = 2  # We expect a linear relationship, but let's add another degree
print ("On scaled data:")
period_fit = fit(temp, period, fit_degree)
print (f"On damped data ({perhaps_best_damp_factor}):")
period_damped_fit = fit(perhaps_best_temp, period, fit_degree)

# THe scatter-plot. Watch out, it takes some time.
if args.emit_plot:
    def getNormalizedRangeAndBin(thing, thing_meta):
        range = (min(thing_meta.normalize(thing)) - 1, max(thing_meta.normalize(thing)) + 1)
        resolution = 2 # thing_meta.normalize(1) # because we don't need that many bins
        bin = max(1, (range[1] - range[0]) * resolution)
        return (range, bin)

    period_range_n, period_bin = getNormalizedRangeAndBin(period, period_meta)
    temp_range_n, temp_bin = getNormalizedRangeAndBin(temp, temp_meta)
    # print (f"Period range: {period_range} -> bin {period_bin}")
    # print (f"Temp range  : {temp_range} -> bin {temp_bin}")
    valid_period_fit_range_n = np.arange(temp_range_n[0], temp_range_n[1], temp_meta.normalize(1))
    valid_period_fit_range = temp_meta.denormalize(valid_period_fit_range_n)

    # limit scattering to less samples to reduce time overhead
    num_samples_to_scatter = min(5000, len(period)/2)
    ss_step_size = int(len(period) / num_samples_to_scatter)
    period_ss = period[0::ss_step_size]
    # print(f"subsampling for scatterplot to {len(period_ss)} elements")

    plt.figure()
    plt.hist2d(temp_meta.normalize(temp), period_meta.normalize(period),
            range=(temp_range_n, period_range_n),
            bins=(int(temp_bin), int(period_bin)),
            )
    plt.scatter(temp_meta.normalize(temp[0::ss_step_size]), period_meta.normalize(period_ss),
                alpha=.15,
                label="Measured samples", color="blue")
    plt.scatter(temp_meta.normalize(perhaps_best_temp[0::ss_step_size]), period_meta.normalize(period_ss),
                alpha=.15, color="lightgreen", label=f"Damped Temperature")

    plt.plot(valid_period_fit_range_n, period_meta.normalize(period_fit(valid_period_fit_range)),
            'red', label="Best fit")
    plt.plot(valid_period_fit_range_n, period_meta.normalize(period_damped_fit(valid_period_fit_range)),
            'teal', label="Best fit (damped)")

    plt.legend()
    plt.ticklabel_format(style='plain')
    plt.xlabel('Scaled Temperature [Celsius]')
    plt.ylabel('Period per one cycle [us]')
    plt.title("Correlation Data")


# OK, and apply inverse of correlation to try linearize period

def printStats(estimated_period, period):
    difference_period = estimated_period - period
    cumulative_difference = np.cumsum(np.array(difference_period))
    corrected_period_std = printStdDev("Corrected Period", difference_period, sample_mean=np.mean(period))
    improvement_ratio = uncorrected_period_std / corrected_period_std
    print (f"With linear fit for period estimation, we got an improvement factor of {improvement_ratio}.")
    print (f"Hypothetical drift with given correction in this specific dataset: {(cumulative_difference[-1] * avg_duration_of_sample_us) / 1000000} seconds")
    return difference_period

estimated_period_undamped = period_fit(temp)
estimated_period_damped = period_damped_fit(perhaps_best_temp)

print("\nUndamped best fit:")
diff_undamped = printStats(estimated_period_undamped, period)
print("\nDamped best fit:")
diff_damped = printStats(estimated_period_damped, period)

if args.emit_plot:
    fig, ax1 = plt.subplots()
    plt.title("Correction Factor Estimation")
    ax1.set_xlabel('Time [s]')
    ax2 = ax1.twinx()
    ax1.set_ylabel('Period per one Cycle [us]')
    ax1.plot(sample_time_s, period_meta.normalize(period), 'green', label="Measured Period")
    ax1.plot(sample_time_s, period_meta.normalize(estimated_period_undamped), 'darkred', label="Estimated Period")
    ax1.plot(sample_time_s, period_meta.normalize(estimated_period_damped), 'orange', label="Estimated Period (Damped)")

    ax2.set_ylabel('Difference [us]')
    ax2.plot(sample_time_s, period_meta.normalize(diff_undamped),
            'blue', label='Difference')
    ax2.plot(sample_time_s, period_meta.normalize(diff_damped),
            'teal', label='Difference (Damped)')
    ax2.axhline(0, linestyle='dashed', color='lightblue', alpha=.5)
    ax2.fill_between(sample_time_s, period_meta.normalize(diff_damped), 0,
            color='teal', alpha=.5)

    # unten, oben = ax2.get_ylim()
    # yoffs = 0# unten
    # ax2.plot(sample_time_s, abs(difference_period)+yoffs,
    #           color='teal', alpha=.25, label="Difference (abs)")
    # reset ylim to have difference really touching bottom
    # ax2.set_ylim((unten, oben))
    legendAllAxes(ax1, ax2)


# This is TODO...
# It seems that we might correlate the temperature change speed with the correction error!

# temperature_change = np.diff(temp)
# temperature_change_rate = temperature_change / np.array(dataframe[data.TABLE_FORMAT['period'].name])[:1]
# smoothed_temp_change_rate = savgol_filter(temperature_change_rate, 500, 2)

# if args.emit_plot and False:
#     fig, ax1 = plt.subplots()
#     ax2 = ax1.twinx()
#     plt.title("Drift Evaluation")
#     plt.xlabel('Time [s]')
#     ax1.set_ylabel('Difference [us]')
#     ax2.set_ylabel('Temperature change rate [Celsius / s]')
#     ax1.plot(sample_time_s, difference_period,
#             label='Difference from Reference', color='green')
#     ax2.plot(sample_time_s[1:], smoothed_temp_change_rate,
#             label='Temperature change (filtered)', color="red")
#     legendAllAxes(ax1, ax2)


if args.emit_plot:
    plt.show()
