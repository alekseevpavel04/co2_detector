#include "logic.h"

// ------------------------------------------------------------
// Навигация по 9 экранам (Этап 6)
// ------------------------------------------------------------
// Раскладка экранов в одном целом current_screen [0..8]:
//
//   index  param           period
//   -----  --------------  -------
//   0      CO2             1h
//   1      CO2             24h
//   2      CO2             7d
//   3      Temperature     1h
//   4      Temperature     24h
//   5      Temperature     7d
//   6      Humidity        1h
//   7      Humidity        24h
//   8      Humidity        7d

static const int N_PARAMS  = 3;
static const int N_PERIODS = 3;
static const int N_SCREENS = N_PARAMS * N_PERIODS;

int cycle_param(int current_screen) {
    if (current_screen < 0 || current_screen >= N_SCREENS) return 0;
    int param_idx  = current_screen / N_PERIODS;
    int period_idx = current_screen % N_PERIODS;
    param_idx = (param_idx + 1) % N_PARAMS;
    return param_idx * N_PERIODS + period_idx;
}

int cycle_period(int current_screen) {
    if (current_screen < 0 || current_screen >= N_SCREENS) return 0;
    int param_idx  = current_screen / N_PERIODS;
    int period_idx = current_screen % N_PERIODS;
    period_idx = (period_idx + 1) % N_PERIODS;
    return param_idx * N_PERIODS + period_idx;
}

int logic_stage_check() {
    return 1;
}
