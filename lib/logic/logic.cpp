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
// Расчёт средних значений (Этап 13)
// ------------------------------------------------------------
std::vector<float> calculate_window_average(
        const std::vector<float>& history,
        int real_window, int target_size) {
    if (real_window <= 0 || target_size <= 0) return {};
    std::vector<float> result(static_cast<size_t>(target_size), 0.0f);
    if (history.empty()) return result;

    const int history_size = static_cast<int>(history.size());
    const int n_windows = history_size / real_window;
    if (n_windows == 0) return result;   // меньше одного полного окна

    for (int i = 0; i < target_size; i++) {
        float sum = 0.0f;
        int   count = 0;
        for (int j = 0; j < n_windows; j++) {
            int real_start = j * real_window + (i * real_window) / target_size;
            int real_end   = j * real_window + ((i + 1) * real_window) / target_size;
            if (real_end == real_start) real_end = real_start + 1;
            if (real_end > history_size) real_end = history_size;
            for (int k = real_start; k < real_end; k++) {
                sum += history[k];
                count++;
            }
        }
        result[i] = (count > 0) ? sum / static_cast<float>(count) : 0.0f;
    }
    return result;
}

// ------------------------------------------------------------
// Алерт VENTILATE с гистерезисом (Этап 12)
// ------------------------------------------------------------
bool update_alert_state(bool current, int co2) {
    if (current) {
        // Сейчас активен — отключаем когда CO2 опустился ниже 1300.
        return co2 >= 1300;
    }
    // Сейчас неактивен — включаем когда CO2 поднялся выше 1500.
    return co2 > 1500;
}

// ------------------------------------------------------------
// Светофоры (Этап 11)
// ------------------------------------------------------------
int get_co2_traffic_level(int co2) {
    if (co2 < 800)  return 2;   // хорошо
    if (co2 < 1500) return 1;   // терпимо
    return 0;                    // плохо
}

int get_temp_traffic_level(float t) {
    if (t >= 20.0f && t <= 24.0f) return 2;
    if (t >= 18.0f && t <= 26.0f) return 1;
    return 0;
}

int get_humidity_traffic_level(int h) {
    if (h >= 40 && h <= 60) return 2;
    if (h >= 30 && h <= 70) return 1;
    return 0;
}

// ------------------------------------------------------------
// Downsampling (Этап 9)
// ------------------------------------------------------------
std::vector<float> downsample(const std::vector<float>& input, int target_size) {
    if (target_size <= 0) return {};
    std::vector<float> output(static_cast<size_t>(target_size), 0.0f);
    if (input.empty()) return output;

    const size_t in_n = input.size();
    for (int i = 0; i < target_size; i++) {
        size_t start = static_cast<size_t>(i)     * in_n / target_size;
        size_t end   = static_cast<size_t>(i + 1) * in_n / target_size;
        if (end == start) end = start + 1;
        if (end > in_n) end = in_n;

        float sum = 0.0f;
        size_t count = 0;
        for (size_t j = start; j < end; j++) { sum += input[j]; count++; }
        output[i] = (count > 0) ? sum / static_cast<float>(count) : 0.0f;
    }
    return output;
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
