#pragma once

#define LP_FILTER_TAPS 121

#define LP_FILTER_100 0
#define LP_FILTER_50  1
#define LP_FILTER_25  2
#define LP_N_FILTERS  3

extern const float g_lowpass_filter_100cps[LP_FILTER_TAPS];
extern const float g_lowpass_filter_50cps[LP_FILTER_TAPS];
extern const float g_lowpass_filter_25cps[LP_FILTER_TAPS];
extern const float *g_lowpass_filters[LP_N_FILTERS];
