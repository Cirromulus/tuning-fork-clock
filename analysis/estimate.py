#!/bin/env python

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
parser.add_argument('--no-emit-plot', default=True, action='store_false', dest ='emit_plot')
args = parser.parse_args()
# print (args)

dataframe = readSqlite(args.database)

# could be made an option as well
output_file_name = '.'.join(args.database.split('.')[:-1]) + ".calib"
output_file = open(output_file_name, "w")
print (f"Writing results to {output_file}")

# print ("Data:")
# print (dataframe)
# print ("Estimated covariance between columns:")
# print (dataframe.cov())
# print ()

# TODO: This is useful for only plotting, but we also calculate.
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
sample_time_s = sample_time_us / 1000000
duration_of_measurement_us = sample_time_us[-1]
avg_duration_of_sample_us = duration_of_measurement_us / len(dataframe)

print (f"Duration of measurement run: {duration_of_measurement_us / 1000000}s (based on reference clock)")
output_file.write(f"Estimation for {args.database} ({duration_of_measurement_us / 1000000} seconds)\n")

def printDriftPerDay(estimation, reference):
    diff = reference - estimation
    total_drift_s = np.sum(diff) / 1000000
    s_per_day = 60 * 60 * 24
    us_per_day = s_per_day * 1000000
    duration_of_measurement_d = duration_of_measurement_us / us_per_day
    drift_s_per_day = total_drift_s / duration_of_measurement_d
    print (f"Total drift: {total_drift_s}s over {duration_of_measurement_d} days -> {drift_s_per_day}s / day")
    return drift_s_per_day

expected_period_us = data.expected_frequency * period_meta.denormalize(1) * 1000000
print (f"Sample drift with expected period of {expected_period_us}us:")
baseline_drift = printDriftPerDay([expected_period_us] * len(period), period)

def legendAllAxes(*axis):
    lines = [line for ax in axis for line in ax.get_lines()]
    labs = [l.get_label() for l in lines if not '_' in l.get_label() ]
    axis[0].legend(lines, labs)

# No time delta is in there, because we would apply it on the rolling value,
# which would over-apply the time factor. This would need the derivative or something.
# Returns tuple of damped values and difference to actual
def dampenWithDiff(factor, xs):
    rolling_value = xs[0]
    ret = []
    for x in xs:
        diff = x - rolling_value
        rolling_value += diff * factor
        ret.append((rolling_value, diff))
    values, diffs = zip(*ret)
    return (np.array(values), np.array(diffs))

def dampen(*args):
    return dampenWithDiff(*args)[0]

def goodSavgolBecauseILookedAtItHard(x):
    a_smooth_number = 50   # probably "samples"
    return savgol_filter(x, a_smooth_number, 1)

def getExtrema(thing):
    min_expected_peak_distance = 60 # FIXME: Normalize with sample density
    min_prominence = temp_meta.denormalize(.5)
    min_height = temp_meta.denormalize(10)
    maxima = find_peaks(thing,
                        height=min_height,
                        distance=min_expected_peak_distance,
                        prominence=min_prominence,
                        width=min_expected_peak_distance)
    minima = find_peaks(-thing, # negated!
                        height=min_height,
                        distance=min_expected_peak_distance,
                        prominence=min_prominence,
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

probably_not_slower_than = 10 * 60 #s
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

if args.emit_plot: # this is not too helpful
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
steps = 20
scaled_interest_bounds = (.01, .001)   # Hm, less manual please
def factorScaled(f):
    return pow(f, 3)
for i in range(0, steps):
    lin_f = 1 * ((i+1) / steps)
    factor = min(scaled_interest_bounds) + max(scaled_interest_bounds) * factorScaled(lin_f)
    # print (f"Factor {lin_f}: {factor}")
    damped_curve = dampen(factor, temp)
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
perhaps_best_temp_estimate, temp_damp_diff = dampenWithDiff(perhaps_best_damp_factor, temp)
output_file.write(f"Damp factor: {perhaps_best_damp_factor}\n")

# ---------------

def fit(x, y, order):
    fit, cov = np.polyfit(x, y, order, cov=True)
    def asFunction(factors):
        ret = "f(x) = "
        ret += " + ".join([f'{f}x^{i}' for i, f in enumerate(reversed(factors))])
        return ret

    print (f"**\nFactors of best fit: {asFunction(fit)}\n**")
    print ("Covariance of fit:")
    print (cov)
    return (np.poly1d(fit), list(reversed(fit)))

fit_degree = 2  # We expect a linear relationship, but another degree slightly improves it
# Skip first samples: Dirty setting. Usually, the damping functions
# need some time to arrive at the "work range" as the internal error first needs to stack up.
skip_first_samples = min(50000, len(period))    # FIXME: Very hardcoded value
print (f"Skipping first {skip_first_samples} samples (from second {sample_time_s[skip_first_samples]}) to allow for damping effects to settle")

print ("\nOn normal data:")
period_fit, pf_f = fit(temp[skip_first_samples:], period[skip_first_samples:], fit_degree)
print (f"\nOn damped data ({perhaps_best_damp_factor}):")
period_damped_fit, pdf_f = fit(perhaps_best_temp_estimate[skip_first_samples:], period[skip_first_samples:], fit_degree)

estimated_period_undamped = period_fit(temp)
estimated_period_damped = period_damped_fit(perhaps_best_temp_estimate)
error_undamped = period - estimated_period_undamped
error_damped = period - estimated_period_damped

output_file.write(f"Factors for damped period estimation:\n")
np.savetxt(output_file, pdf_f)
# ---------------

def printStats(estimated_period):
    print (f"Hypothetical drift with given correction in this specific dataset:")
    this_drift = printDriftPerDay(estimated_period, period)
    improvement_ratio = abs(baseline_drift) / abs(this_drift)
    print (f"This corresponds to an improvement factor of {improvement_ratio}.")

# Failed test whether temperature changerate can still be somehow fitted.
# makes it worse throughout.
# def applyErrorEstimator(which_temp_gradient, which_error):
#     period_damped_error_deriv_fit = fit(which_temp_gradient, which_error, fit_degree)
#     estimated_period_error_deriv = period_damped_error_deriv_fit(temp)
#     estimated_period_deriv = estimated_period_damped - estimated_period_error_deriv
#     printStats(estimated_period_deriv)
#     return
# applyErrorEstimator(temp_damp_diff, error_damped)
# applyErrorEstimator(temp_damp_diff, error_undamped)

print ("Period error estimation with temp rate change:")
period_damped_error_deriv_fit, pddf_f = fit(temp_damp_diff[skip_first_samples:], error_damped[skip_first_samples:], fit_degree)
def period_deriv_fit(temperature, temp_rate):
    return period_damped_fit(temperature) + period_damped_error_deriv_fit(temp_rate)
estimated_period_deriv = period_deriv_fit(perhaps_best_temp_estimate, temp_damp_diff)
error_damped_deriv = period - estimated_period_deriv


print("\nUndamped best fit:")
printStats(estimated_period_undamped)
print("\nDamped best fit:")
printStats(estimated_period_damped)
print (f"\nOn error of period estimation, with temp gradient from temp damp diff:")
printStats(estimated_period_deriv)
output_file.write(f"Factors for period error estimation based on temp gradient:\n")
np.savetxt(output_file, pddf_f)

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
    valid_temp_fit_range_n = np.arange(temp_range_n[0], temp_range_n[1], temp_meta.normalize(1))
    valid_temp_fit_range = temp_meta.denormalize(valid_temp_fit_range_n)

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
    plt.scatter(temp_meta.normalize(perhaps_best_temp_estimate[0::ss_step_size]), period_meta.normalize(period_ss),
                alpha=.15, color="lightgreen", label=f"Damped Temperature")

    plt.plot(valid_temp_fit_range_n, period_meta.normalize(period_fit(valid_temp_fit_range)),
            'red', label="Best fit")
    plt.plot(valid_temp_fit_range_n, period_meta.normalize(period_damped_fit(valid_temp_fit_range)),
            'teal', label="Best fit (damped)")
    # the estimated_period_deriv is harder to add here because it uses temp change


    plt.legend()
    plt.ticklabel_format(style='plain')
    plt.xlabel('Scaled Temperature [Celsius]')
    plt.ylabel('Period per one cycle [us]')
    plt.title("Correlation Data")

# plt.show()
# exit()

# This is TODO...
# It seems that we might correlate the temperature change speed with the correction error!

# this is actually something like a convolution with a normalized diff pulse
def slidingWindowGradient(xs, ys, windowsize):
    assert windowsize % 2 == 0
    assert len(xs) == len(ys)
    half_ws = int(windowsize / 2)

    gradients = []
    for i in range(len(xs)):
        left_i = max(i - half_ws, 0)
        right_i = min(i + half_ws, len(xs) - 1)
        left = xs[left_i:i+1]
        right = xs[i:right_i+1]

        diff_x = (sum(right) / len(right)) - (sum(left) / len(left))
        diff_y = ys[right_i] - ys[left_i]
        gradients.append(diff_x / diff_y)

    return gradients

# samples, which is near "seconds" (TODO: Calculate from seconds)
window = 64 # Should be big enough to supress noise
temperature_change_rate_centidegree_per_s = np.array(slidingWindowGradient(temp, sample_time_s, window))

# this also works, but I don't Brain enough for modeling that in the clock later:
# temperature_change_rate_centidegree_per_us = np.gradient(temp, period, edge_order=2)
# Probably best would be a savgol, but also: I maek comuter do think, not think

if args.emit_plot:
    fig, ax1 = plt.subplots()
    plt.title("Correction Factor Estimation")
    ax1.set_xlabel('Time [s]')
    ax2 = ax1.twinx()
    ax1.set_ylabel('Period per one Cycle [us]')
    common_alpha = .6
    ax1.plot(sample_time_s, period_meta.normalize(period),
             'green', label="Measured Period", alpha=common_alpha)
    ax1.plot(sample_time_s, period_meta.normalize(period_smooth),
             'darkgreen', label="Measured Period (smoothed)", alpha=common_alpha)
    ax1.plot(sample_time_s, period_meta.normalize(estimated_period_undamped),
             'darkred', label="Estimated Period", alpha=common_alpha)
    ax1.plot(sample_time_s, period_meta.normalize(estimated_period_damped),
             'orange', label="Estimated Period (Damped)", alpha=common_alpha)
    ax1.plot(sample_time_s, period_meta.normalize(estimated_period_deriv),
             'springgreen', label="Estimated Period (Damped & Rate)", alpha=common_alpha)

    ax2.set_ylabel('Difference [us]')
    common_diff_alpha = 0.4
    drift_damped_rate = period_meta.normalize(np.cumsum(error_damped_deriv))
    error_damped_rate = period_meta.normalize(error_damped_deriv)
    scaling_factor_precise = np.max(drift_damped_rate) / np.max(error_damped_rate)
    scaling_factor = int(scaling_factor_precise / 3)

    ax2.plot(sample_time_s, period_meta.normalize(error_undamped) * scaling_factor,
            'blue', label=f'Difference * {scaling_factor}', alpha=common_diff_alpha)
    ax2.plot(sample_time_s, period_meta.normalize(error_damped) * scaling_factor,
             'teal', alpha=common_diff_alpha,
             label=f'Difference (Damped) * {scaling_factor}')
    ax2.plot(sample_time_s, error_damped_rate * scaling_factor,
             'darkgreen', alpha=common_diff_alpha,
             label=f"Difference (Damped & Rate) * {scaling_factor}")

    ax2.plot(sample_time_s, drift_damped_rate,
             'red', alpha=common_alpha,
             label="Drift (Damped & Rate)")

    ax2.axhline(0, linestyle='dashed', color='lightblue', alpha=.5)
    ax2.fill_between(sample_time_s, period_meta.normalize(error_damped), 0,
            color='lightblue', alpha=.5)


    # unten, oben = ax2.get_ylim()
    # yoffs = 0# unten
    # ax2.plot(sample_time_s, abs(difference_period)+yoffs,
    #           color='teal', alpha=.25, label="Difference (abs)")
    # reset ylim to have difference really touching bottom
    # ax2.set_ylim((unten, oben))
    legendAllAxes(ax1, ax2)

plt.show()
exit()

# allright, now we are getting desperate
# damped_damp_diff = dampen(perhaps_best_damp_factor, temp_damp_diff)


if args.emit_plot:
    derive_artige = {
         # not normalizing, because values are small (TODO investigate)
        'sliding window': temperature_change_rate_centidegree_per_s / 10,
        'damp difference': period_meta.normalize(temp_damp_diff),
        # 'damped damp difference': period_meta.normalize(damped_damp_diff),
    }

    fig, ax1 = plt.subplots()
    ax2 = ax1.twinx()
    plt.title("Drift Evaluation")
    plt.xlabel('Time [s]')
    # ax1.set_ylabel('Temperature [degC]')
    ax1.set_ylabel('Error to Reference per Period [us]')
    ax2.set_ylabel('Temperature change rate [Celsius / s]')
    ax1.set_xlabel('Sample time [s]')
    # ax1.plot(sample_time_s, temp_meta.normalize(temp),
    #         label='Measured temperature', color='lightcoral')
    ax1.plot(sample_time_s, period_meta.normalize(error_damped),
             label='Damped & Estimated',
             alpha=0.5)
    ax2.plot([], [])    # force new color, or else we would start at same color as above
    for name, deriv in derive_artige.items():
        ax2.plot(sample_time_s, deriv,
                label=name, alpha=.7)
    legendAllAxes(ax1, ax2)

    # A 3D scatter.
    for name, deriv in derive_artige.items():
        samples_to_take = min(2000, len(period))
        step_size = int(len(period) / samples_to_take)
        plt.figure("Scatter")
        plt.title("Temp Rate change influence.\nSize means abs(error)")
        ax = plt.subplot(projection='3d')
        ax.scatter(period_meta.normalize(period[0::step_size]),
                   temp_meta.normalize(temp[0::step_size]),
                   deriv[0::step_size],
                   label=name, alpha=.5, s=np.abs(error_damped[0::step_size]))
        ax.set_xlabel('Measured Period [us]')
        ax.set_ylabel('Measured Temperature [Celsius]')
        ax.set_zlabel('Change rate [Celsius / s]')
        ax.legend()

    # the correlation scatters
    for name, deriv in derive_artige.items():
        plt.figure()
        plt.title(f"Estimation Error against {name}")
        plt.scatter(period_meta.normalize(error_damped), deriv)
        plt.xlabel("Error of Estimation of Period [us]")
        plt.ylabel("Temperature Change [deg / C]")

if args.emit_plot:
    plt.show()
