#include "logic.h"
#include <cstdio>
#include <algorithm>

// ------------------------------------------------------------
// Валидация измерения (Этап 7)
// ------------------------------------------------------------
bool is_measurement_valid(const Measurement& m) {
    return m.co2 > 0 && m.co2 < 10000
        && m.temp_x10 > -400 && m.temp_x10 < 500
        && m.humidity <= 100;
}

// ------------------------------------------------------------
// File numbering (Этап 7)
// ------------------------------------------------------------
std::string make_filename(int number) {
    char buf[64];
    // %03d — три цифры с ведущими нулями: 1 → "001", 42 → "042".
    std::snprintf(buf, sizeof(buf), "/data/measurements_%03d.bin", number);
    return std::string(buf);
}

int extract_file_number(const std::string& path) {
    const char* needle = "measurements_";
    size_t pos = path.find(needle);
    if (pos == std::string::npos) return -1;
    pos += 13;   // длина "measurements_"
    if (pos + 3 > path.size()) return -1;

    int num = 0;
    for (int i = 0; i < 3; i++) {
        char c = path[pos + i];
        if (c < '0' || c > '9') return -1;
        num = num * 10 + (c - '0');
    }
    // После трёх цифр должно идти ".bin" (или конец строки —
    // в практике LittleFS у нас всегда .bin).
    if (pos + 3 < path.size() && path.compare(pos + 3, 4, ".bin") != 0) return -1;
    return num;
}

int next_file_number(int current) {
    return current + 1;
}

// ------------------------------------------------------------
// Ротация файлов (Этап 8)
// ------------------------------------------------------------
int oldest_file_to_remove(const std::vector<int>& numbers, int max_files) {
    if (numbers.empty()) return -1;
    if (static_cast<int>(numbers.size()) <= max_files) return -1;
    return *std::min_element(numbers.begin(), numbers.end());
}

// ------------------------------------------------------------
// Навигация по 9 экранам (Этап 6)
// ------------------------------------------------------------
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
