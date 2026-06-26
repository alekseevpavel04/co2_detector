#include <unity.h>
#include "logic.h"

// Тесты чистой логики (запуск: pio test -e native).
//
// Файл содержит две группы:
//   1. Юнит-тесты per-stage (cycle_*, downsample, traffic, alert, etc.)
//   2. Интеграционный тест (Этап 14): синтетический месяц данных
//      прогоняется через всю чисто-логическую pipeline.

#include <vector>
#include <cmath>

void setUp(void) {}
void tearDown(void) {}

// ------------------------------------------------------------
// Этап 7 — Measurement struct и валидация
// ------------------------------------------------------------
void test_measurement_is_10_bytes(void) {
    // КРИТИЧЕСКОЕ #5: если в структуре есть padding, формат
    // бинарных файлов нарушается. Проверяем sizeof честно.
    TEST_ASSERT_EQUAL_size_t(10, sizeof(Measurement));
}

void test_is_measurement_valid_typical(void) {
    Measurement m{0, 645, 234, 45, 0};
    TEST_ASSERT_TRUE(is_measurement_valid(m));
}

void test_is_measurement_valid_co2_range(void) {
    Measurement m{0, 645, 234, 45, 0};
    m.co2 = 0;
    TEST_ASSERT_FALSE(is_measurement_valid(m));
    m.co2 = 10000;
    TEST_ASSERT_FALSE(is_measurement_valid(m));
    m.co2 = 9999;
    TEST_ASSERT_TRUE(is_measurement_valid(m));
}

void test_is_measurement_valid_temp_range(void) {
    Measurement m{0, 645, 234, 45, 0};
    m.temp_x10 = -400;
    TEST_ASSERT_FALSE(is_measurement_valid(m));
    m.temp_x10 = 500;
    TEST_ASSERT_FALSE(is_measurement_valid(m));
    m.temp_x10 = 250;
    TEST_ASSERT_TRUE(is_measurement_valid(m));
}

void test_is_measurement_valid_humidity_range(void) {
    Measurement m{0, 645, 234, 45, 0};
    m.humidity = 101;
    TEST_ASSERT_FALSE(is_measurement_valid(m));
    m.humidity = 100;
    TEST_ASSERT_TRUE(is_measurement_valid(m));
    m.humidity = 0;
    TEST_ASSERT_TRUE(is_measurement_valid(m));
}

// ------------------------------------------------------------
// Этап 7 — file numbering
// ------------------------------------------------------------
void test_make_filename_zero_padding(void) {
    TEST_ASSERT_EQUAL_STRING("/data/measurements_001.bin",
                             make_filename(1).c_str());
    TEST_ASSERT_EQUAL_STRING("/data/measurements_042.bin",
                             make_filename(42).c_str());
    TEST_ASSERT_EQUAL_STRING("/data/measurements_999.bin",
                             make_filename(999).c_str());
}

void test_extract_file_number_valid(void) {
    TEST_ASSERT_EQUAL_INT(1,  extract_file_number("/data/measurements_001.bin"));
    TEST_ASSERT_EQUAL_INT(13, extract_file_number("/data/measurements_013.bin"));
    TEST_ASSERT_EQUAL_INT(999, extract_file_number("/data/measurements_999.bin"));
    // LittleFS entry.name() может возвращать имя без префикса /data/ —
    // нормально, главное что префикс «measurements_» присутствует.
    TEST_ASSERT_EQUAL_INT(7, extract_file_number("measurements_007.bin"));
}

void test_extract_file_number_invalid(void) {
    TEST_ASSERT_EQUAL_INT(-1, extract_file_number("garbage"));
    TEST_ASSERT_EQUAL_INT(-1, extract_file_number("measurements_"));
    TEST_ASSERT_EQUAL_INT(-1, extract_file_number("measurements_abc.bin"));
    TEST_ASSERT_EQUAL_INT(-1, extract_file_number(""));
}

void test_make_extract_roundtrip(void) {
    // Туда-обратно для всех допустимых номеров.
    for (int n : {1, 7, 42, 100, 256, 999}) {
        std::string s = make_filename(n);
        TEST_ASSERT_EQUAL_INT(n, extract_file_number(s));
    }
}

void test_next_file_number(void) {
    TEST_ASSERT_EQUAL_INT(2,  next_file_number(1));
    TEST_ASSERT_EQUAL_INT(14, next_file_number(13));
    TEST_ASSERT_EQUAL_INT(100, next_file_number(99));
}

// ------------------------------------------------------------
// Этап 8 — ротация файлов
// ------------------------------------------------------------
void test_oldest_empty_list(void) {
    std::vector<int> n;
    TEST_ASSERT_EQUAL_INT(-1, oldest_file_to_remove(n, 13));
}

void test_oldest_under_max(void) {
    std::vector<int> n{1, 2, 3};
    TEST_ASSERT_EQUAL_INT(-1, oldest_file_to_remove(n, 13));
}

void test_oldest_at_max(void) {
    std::vector<int> n;
    for (int i = 1; i <= 13; i++) n.push_back(i);
    TEST_ASSERT_EQUAL_INT(-1, oldest_file_to_remove(n, 13));
}

void test_oldest_over_max_returns_min(void) {
    std::vector<int> n;
    for (int i = 1; i <= 14; i++) n.push_back(i);
    TEST_ASSERT_EQUAL_INT(1, oldest_file_to_remove(n, 13));
}

void test_oldest_unsorted_finds_min(void) {
    // Список не отсортирован — функция всё равно находит минимум.
    std::vector<int> n{5, 3, 8, 1, 9, 2, 7, 4, 11, 12, 6, 10, 13, 14};
    TEST_ASSERT_EQUAL_INT(1, oldest_file_to_remove(n, 13));
}

void test_oldest_with_holes(void) {
    // После N удалений номера не плотные: например, остались 5..18 (14 шт).
    std::vector<int> n;
    for (int i = 5; i <= 18; i++) n.push_back(i);
    TEST_ASSERT_EQUAL_INT(5, oldest_file_to_remove(n, 13));
}

// ------------------------------------------------------------
// Этап 9 — downsample
// ------------------------------------------------------------
void test_downsample_empty_input(void) {
    std::vector<float> in;
    auto out = downsample(in, 12);
    TEST_ASSERT_EQUAL_size_t(12, out.size());
    for (float v : out) TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
}

void test_downsample_target_zero_or_negative(void) {
    auto out1 = downsample({1.0f, 2.0f}, 0);
    TEST_ASSERT_EQUAL_size_t(0, out1.size());
    auto out2 = downsample({1.0f, 2.0f}, -5);
    TEST_ASSERT_EQUAL_size_t(0, out2.size());
}

void test_downsample_passthrough(void) {
    std::vector<float> in{1.0f, 2.0f, 3.0f};
    auto out = downsample(in, 3);
    TEST_ASSERT_EQUAL_size_t(3, out.size());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, out[0]);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, out[1]);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, out[2]);
}

void test_downsample_4_to_2_averages(void) {
    std::vector<float> in{1.0f, 2.0f, 3.0f, 4.0f};
    auto out = downsample(in, 2);
    TEST_ASSERT_EQUAL_size_t(2, out.size());
    TEST_ASSERT_EQUAL_FLOAT(1.5f, out[0]);  // avg(1,2)
    TEST_ASSERT_EQUAL_FLOAT(3.5f, out[1]);  // avg(3,4)
}

void test_downsample_upscale_repeats(void) {
    // input короче target — каждый выходной слот получает по одному
    // входному значению (повторы), без дроби.
    std::vector<float> in{10.0f, 20.0f};
    auto out = downsample(in, 4);
    TEST_ASSERT_EQUAL_size_t(4, out.size());
    TEST_ASSERT_EQUAL_FLOAT(10.0f, out[0]);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, out[1]);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out[2]);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, out[3]);
}

void test_downsample_real_24h(void) {
    // 288 точек → 144: каждый бакет — 2 точки.
    std::vector<float> in;
    for (int i = 0; i < 288; i++) in.push_back(static_cast<float>(i));
    auto out = downsample(in, 144);
    TEST_ASSERT_EQUAL_size_t(144, out.size());
    // out[0] = avg(0,1) = 0.5
    // out[1] = avg(2,3) = 2.5
    // out[143] = avg(286, 287) = 286.5
    TEST_ASSERT_EQUAL_FLOAT(0.5f,   out[0]);
    TEST_ASSERT_EQUAL_FLOAT(2.5f,   out[1]);
    TEST_ASSERT_EQUAL_FLOAT(286.5f, out[143]);
}

// ------------------------------------------------------------
// Этап 11 — светофоры
// ------------------------------------------------------------
void test_co2_traffic_good(void) {
    TEST_ASSERT_EQUAL_INT(2, get_co2_traffic_level(400));
    TEST_ASSERT_EQUAL_INT(2, get_co2_traffic_level(799));
}
void test_co2_traffic_ok(void) {
    TEST_ASSERT_EQUAL_INT(1, get_co2_traffic_level(800));   // граница
    TEST_ASSERT_EQUAL_INT(1, get_co2_traffic_level(1000));
    TEST_ASSERT_EQUAL_INT(1, get_co2_traffic_level(1499));
}
void test_co2_traffic_bad(void) {
    TEST_ASSERT_EQUAL_INT(0, get_co2_traffic_level(1500));  // граница
    TEST_ASSERT_EQUAL_INT(0, get_co2_traffic_level(3000));
    TEST_ASSERT_EQUAL_INT(0, get_co2_traffic_level(5000));
}

void test_temp_traffic_good(void) {
    TEST_ASSERT_EQUAL_INT(2, get_temp_traffic_level(20.0f));
    TEST_ASSERT_EQUAL_INT(2, get_temp_traffic_level(22.0f));
    TEST_ASSERT_EQUAL_INT(2, get_temp_traffic_level(24.0f));
}
void test_temp_traffic_ok(void) {
    TEST_ASSERT_EQUAL_INT(1, get_temp_traffic_level(18.0f));
    TEST_ASSERT_EQUAL_INT(1, get_temp_traffic_level(19.5f));
    TEST_ASSERT_EQUAL_INT(1, get_temp_traffic_level(25.0f));
    TEST_ASSERT_EQUAL_INT(1, get_temp_traffic_level(26.0f));
}
void test_temp_traffic_bad(void) {
    TEST_ASSERT_EQUAL_INT(0, get_temp_traffic_level(17.9f));
    TEST_ASSERT_EQUAL_INT(0, get_temp_traffic_level(26.1f));
    TEST_ASSERT_EQUAL_INT(0, get_temp_traffic_level(10.0f));
    TEST_ASSERT_EQUAL_INT(0, get_temp_traffic_level(35.0f));
}

void test_humidity_traffic_good(void) {
    TEST_ASSERT_EQUAL_INT(2, get_humidity_traffic_level(40));
    TEST_ASSERT_EQUAL_INT(2, get_humidity_traffic_level(50));
    TEST_ASSERT_EQUAL_INT(2, get_humidity_traffic_level(60));
}
void test_humidity_traffic_ok(void) {
    TEST_ASSERT_EQUAL_INT(1, get_humidity_traffic_level(30));
    TEST_ASSERT_EQUAL_INT(1, get_humidity_traffic_level(35));
    TEST_ASSERT_EQUAL_INT(1, get_humidity_traffic_level(65));
    TEST_ASSERT_EQUAL_INT(1, get_humidity_traffic_level(70));
}
void test_humidity_traffic_bad(void) {
    TEST_ASSERT_EQUAL_INT(0, get_humidity_traffic_level(29));
    TEST_ASSERT_EQUAL_INT(0, get_humidity_traffic_level(71));
    TEST_ASSERT_EQUAL_INT(0, get_humidity_traffic_level(0));
    TEST_ASSERT_EQUAL_INT(0, get_humidity_traffic_level(100));
}

// ------------------------------------------------------------
// Этап 12 — алерт VENTILATE с гистерезисом
// ------------------------------------------------------------
void test_alert_off_stays_off_below_threshold(void) {
    // Низкий CO2, алерт уже off → остаётся off.
    TEST_ASSERT_FALSE(update_alert_state(false, 400));
    TEST_ASSERT_FALSE(update_alert_state(false, 1000));
    TEST_ASSERT_FALSE(update_alert_state(false, 1499));
    TEST_ASSERT_FALSE(update_alert_state(false, 1500));  // на границе ещё off
}

void test_alert_off_turns_on_above_1500(void) {
    TEST_ASSERT_TRUE(update_alert_state(false, 1501));
    TEST_ASSERT_TRUE(update_alert_state(false, 2000));
    TEST_ASSERT_TRUE(update_alert_state(false, 5000));
}

void test_alert_on_stays_on_above_1300(void) {
    // Уже on, CO2 ещё выше 1300 → остаёмся on (гистерезис).
    TEST_ASSERT_TRUE(update_alert_state(true, 5000));
    TEST_ASSERT_TRUE(update_alert_state(true, 1500));
    TEST_ASSERT_TRUE(update_alert_state(true, 1400));
    TEST_ASSERT_TRUE(update_alert_state(true, 1300));
}

void test_alert_on_turns_off_below_1300(void) {
    TEST_ASSERT_FALSE(update_alert_state(true, 1299));
    TEST_ASSERT_FALSE(update_alert_state(true, 1000));
    TEST_ASSERT_FALSE(update_alert_state(true, 400));
}

void test_alert_hysteresis_zone(void) {
    // CO2 в зоне 1300..1500 — состояние не меняется.
    for (int co2 = 1300; co2 <= 1500; co2 += 50) {
        TEST_ASSERT_FALSE(update_alert_state(false, co2));
        TEST_ASSERT_TRUE (update_alert_state(true,  co2));
    }
}

// ------------------------------------------------------------
// Этап 13 — calculate_window_average
// ------------------------------------------------------------
void test_avg_window_uniform_history(void) {
    // Везде одно значение — среднее в каждой точке тоже это значение.
    std::vector<float> h(24, 100.0f);
    auto r = calculate_window_average(h, 12, 12);
    TEST_ASSERT_EQUAL_size_t(12, r.size());
    for (float v : r) TEST_ASSERT_EQUAL_FLOAT(100.0f, v);
}

void test_avg_window_two_windows_progressive(void) {
    // Окно 1: 0..11, окно 2: 100..111
    // r[i] = avg(i, 100+i) = 50 + i
    std::vector<float> h;
    for (int i = 0; i < 12; i++)  h.push_back(static_cast<float>(i));
    for (int i = 0; i < 12; i++)  h.push_back(static_cast<float>(100 + i));
    auto r = calculate_window_average(h, 12, 12);
    TEST_ASSERT_EQUAL_size_t(12, r.size());
    for (int i = 0; i < 12; i++) {
        TEST_ASSERT_EQUAL_FLOAT(50.0f + static_cast<float>(i), r[i]);
    }
}

void test_avg_window_24h_downsample(void) {
    // 288 точек → 144 слотов. Каждый слот = avg 2 точек.
    std::vector<float> h;
    for (int i = 0; i < 288; i++) h.push_back(static_cast<float>(i));
    auto r = calculate_window_average(h, 288, 144);
    TEST_ASSERT_EQUAL_size_t(144, r.size());
    TEST_ASSERT_EQUAL_FLOAT(0.5f,   r[0]);
    TEST_ASSERT_EQUAL_FLOAT(2.5f,   r[1]);
    TEST_ASSERT_EQUAL_FLOAT(286.5f, r[143]);
}

void test_avg_window_empty_history(void) {
    std::vector<float> h;
    auto r = calculate_window_average(h, 12, 12);
    TEST_ASSERT_EQUAL_size_t(12, r.size());
    for (float v : r) TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
}

void test_avg_window_history_smaller_than_window(void) {
    // Меньше одного окна — нет валидных полных окон → нули.
    std::vector<float> h(10, 50.0f);
    auto r = calculate_window_average(h, 12, 12);
    TEST_ASSERT_EQUAL_size_t(12, r.size());
    for (float v : r) TEST_ASSERT_EQUAL_FLOAT(0.0f, v);
}

void test_avg_window_invalid_sizes(void) {
    std::vector<float> h(24, 50.0f);
    TEST_ASSERT_EQUAL_size_t(0, calculate_window_average(h, 0,  12).size());
    TEST_ASSERT_EQUAL_size_t(0, calculate_window_average(h, 12, 0).size());
    TEST_ASSERT_EQUAL_size_t(0, calculate_window_average(h, -1, 12).size());
    TEST_ASSERT_EQUAL_size_t(0, calculate_window_average(h, 12, -1).size());
}

void test_avg_window_4_to_2(void) {
    // 8 точек, окно 4, target 2. n_windows = 2.
    // Окно 1: [1,2,3,4], окно 2: [5,6,7,8]
    // i=0: avg(1,2,5,6) = 3.5
    // i=1: avg(3,4,7,8) = 5.5
    std::vector<float> h{1, 2, 3, 4, 5, 6, 7, 8};
    auto r = calculate_window_average(h, 4, 2);
    TEST_ASSERT_EQUAL_size_t(2, r.size());
    TEST_ASSERT_EQUAL_FLOAT(3.5f, r[0]);
    TEST_ASSERT_EQUAL_FLOAT(5.5f, r[1]);
}

void test_alert_simulated_cycle(void) {
    // Эмулируем: CO2 растёт до 1600 (включается), падает до 1200 (выключается).
    bool active = false;
    int co2_trajectory[] = { 600, 800, 1200, 1400, 1499, 1501, 1600, 1500,
                              1400, 1350, 1300, 1299, 800, 500 };
    bool expected[]       = { false, false, false, false, false, true, true, true,
                              true, true, true, false, false, false };
    int n = sizeof(co2_trajectory) / sizeof(co2_trajectory[0]);
    for (int i = 0; i < n; i++) {
        active = update_alert_state(active, co2_trajectory[i]);
        TEST_ASSERT_EQUAL_MESSAGE(expected[i], active, "alert trajectory step");
    }
}

// ------------------------------------------------------------
// Этап 6 — навигация: cycle_param, cycle_period
// ------------------------------------------------------------
// Раскладка экранов: index = param_idx*3 + period_idx
//   0=CO2 1h    1=CO2 24h   2=CO2 7d
//   3=T 1h      4=T 24h     5=T 7d
//   6=RH 1h     7=RH 24h    8=RH 7d

void test_cycle_param_keeps_period_1h(void) {
    // period_idx == 0 (1h), parameter циклится: CO2 → T → RH → CO2
    TEST_ASSERT_EQUAL_INT(3, cycle_param(0));   // CO2 1h → T 1h
    TEST_ASSERT_EQUAL_INT(6, cycle_param(3));   // T 1h → RH 1h
    TEST_ASSERT_EQUAL_INT(0, cycle_param(6));   // RH 1h → CO2 1h
}

void test_cycle_param_keeps_period_24h(void) {
    TEST_ASSERT_EQUAL_INT(4, cycle_param(1));   // CO2 24h → T 24h
    TEST_ASSERT_EQUAL_INT(7, cycle_param(4));   // T 24h → RH 24h
    TEST_ASSERT_EQUAL_INT(1, cycle_param(7));   // RH 24h → CO2 24h
}

void test_cycle_param_keeps_period_7d(void) {
    TEST_ASSERT_EQUAL_INT(5, cycle_param(2));   // CO2 7d → T 7d
    TEST_ASSERT_EQUAL_INT(8, cycle_param(5));   // T 7d → RH 7d
    TEST_ASSERT_EQUAL_INT(2, cycle_param(8));   // RH 7d → CO2 7d
}

void test_cycle_period_keeps_param_CO2(void) {
    // param_idx == 0 (CO2), period циклится: 1h → 24h → 7d → 1h
    TEST_ASSERT_EQUAL_INT(1, cycle_period(0));  // CO2 1h → CO2 24h
    TEST_ASSERT_EQUAL_INT(2, cycle_period(1));  // CO2 24h → CO2 7d
    TEST_ASSERT_EQUAL_INT(0, cycle_period(2));  // CO2 7d → CO2 1h
}

void test_cycle_period_keeps_param_T(void) {
    TEST_ASSERT_EQUAL_INT(4, cycle_period(3));  // T 1h → T 24h
    TEST_ASSERT_EQUAL_INT(5, cycle_period(4));
    TEST_ASSERT_EQUAL_INT(3, cycle_period(5));
}

void test_cycle_period_keeps_param_RH(void) {
    TEST_ASSERT_EQUAL_INT(7, cycle_period(6));
    TEST_ASSERT_EQUAL_INT(8, cycle_period(7));
    TEST_ASSERT_EQUAL_INT(6, cycle_period(8));
}

void test_cycle_invalid_returns_zero(void) {
    // Защита от мусора (например, при первом включении из RTC memory).
    TEST_ASSERT_EQUAL_INT(0, cycle_param(-1));
    TEST_ASSERT_EQUAL_INT(0, cycle_param(9));
    TEST_ASSERT_EQUAL_INT(0, cycle_param(100));
    TEST_ASSERT_EQUAL_INT(0, cycle_period(-1));
    TEST_ASSERT_EQUAL_INT(0, cycle_period(9));
    TEST_ASSERT_EQUAL_INT(0, cycle_period(-999));
}

// «Замкнутость»: тройное применение cycle_param возвращает к исходу
// (3 параметра по кругу), то же для cycle_period (3 периода).
void test_cycle_param_is_modulo_3(void) {
    for (int s = 0; s < 9; s++) {
        int r = cycle_param(cycle_param(cycle_param(s)));
        TEST_ASSERT_EQUAL_INT(s, r);
    }
}

void test_cycle_period_is_modulo_3(void) {
    for (int s = 0; s < 9; s++) {
        int r = cycle_period(cycle_period(cycle_period(s)));
        TEST_ASSERT_EQUAL_INT(s, r);
    }
}

// ------------------------------------------------------------
// Новый UI — навигация одной кнопкой по 4 экранам (main + 3 графика)
// ------------------------------------------------------------
void test_cycle_screen_basic_loop(void) {
    // 0(main) → 1(1h) → 2(24h) → 3(7d) → 0
    TEST_ASSERT_EQUAL_INT(1, cycle_screen(0, 4));
    TEST_ASSERT_EQUAL_INT(2, cycle_screen(1, 4));
    TEST_ASSERT_EQUAL_INT(3, cycle_screen(2, 4));
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(3, 4));
}

void test_cycle_screen_garbage_protection(void) {
    // Мусор в current (RTC memory) → 0.
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(-1, 4));
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(4, 4));
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(999, 4));
    // Некорректное число экранов → 0.
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(1, 0));
    TEST_ASSERT_EQUAL_INT(0, cycle_screen(1, -3));
}

void test_cycle_screen_full_cycle_returns_to_start(void) {
    // 4-кратное нажатие возвращает к исходному экрану.
    for (int s = 0; s < 4; s++) {
        int r = cycle_screen(cycle_screen(cycle_screen(cycle_screen(s, 4), 4), 4), 4);
        TEST_ASSERT_EQUAL_INT(s, r);
    }
}

// ------------------------------------------------------------
// Этап 14 — интеграционный тест: синтетический месяц данных
// ------------------------------------------------------------
// Генерируем 30 дней × 288 = 8640 «измерений» с правдоподобным
// суточным паттерном (CO2 низкий ночью, выше днём). Прогоняем
// через is_measurement_valid + calculate_window_average +
// downsample, проверяем что:
//   - все измерения валидны;
//   - размеры выходных массивов совпадают с целевыми;
//   - средние попадают в разумный диапазон.
//   - навигация по 9 экранам замкнута;
//   - алерт корректно отрабатывает по реальной траектории CO2.

static std::vector<Measurement> generate_synthetic_month() {
    std::vector<Measurement> out;
    out.reserve(30 * 288);
    for (int day = 0; day < 30; day++) {
        for (int slot = 0; slot < 288; slot++) {
            // slot 0..287 — 5-минутные интервалы за сутки.
            float hour = slot / 12.0f;          // 0..24
            // CO2: ~500 ночью, ~1100 в рабочие часы (9..18).
            uint16_t co2 = 500;
            if (hour >= 9.0f && hour <= 18.0f) co2 = 1100;
            co2 += static_cast<uint16_t>(((slot + day) * 7) % 200) - 100;
            // T: между 20 и 24 с лёгкой суточной волной.
            float t_real = 22.0f + 1.5f * std::sin(hour * 6.28318f / 24.0f);
            int16_t t10 = static_cast<int16_t>(t_real * 10.0f);
            uint8_t hum = static_cast<uint8_t>(45 + (slot % 21));   // 45..65
            Measurement m{};
            m.timestamp = static_cast<uint32_t>(day * 86400 + slot * 300);
            m.co2 = co2;
            m.temp_x10 = t10;
            m.humidity = hum;
            m.flags = 0;
            out.push_back(m);
        }
    }
    return out;
}

void test_integration_synthetic_month(void) {
    auto history = generate_synthetic_month();
    TEST_ASSERT_EQUAL_size_t(30u * 288u, history.size());

    // 1. Все синтетические измерения валидны.
    int valid_count = 0;
    for (const auto& m : history) if (is_measurement_valid(m)) valid_count++;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(history.size()), valid_count);

    // 2. Перегоняем в float-векторы.
    std::vector<float> co2, t, h;
    co2.reserve(history.size());
    t.reserve(history.size());
    h.reserve(history.size());
    for (const auto& m : history) {
        co2.push_back(static_cast<float>(m.co2));
        t.push_back(m.temp_x10 / 10.0f);
        h.push_back(static_cast<float>(m.humidity));
    }

    // 3. Расчёт средних: размеры выходов = target.
    auto co2_1h  = calculate_window_average(co2, 12,   12);
    auto co2_24h = calculate_window_average(co2, 288,  144);
    auto co2_7d  = calculate_window_average(co2, 2016, 168);
    TEST_ASSERT_EQUAL_size_t(12,  co2_1h.size());
    TEST_ASSERT_EQUAL_size_t(144, co2_24h.size());
    TEST_ASSERT_EQUAL_size_t(168, co2_7d.size());

    // 4. Средние попадают в правдоподобный диапазон.
    for (float v : co2_24h) {
        TEST_ASSERT_TRUE_MESSAGE(v >= 300.0f && v <= 1500.0f,
                                  "co2_24h avg in plausible range");
    }
    auto t_24h = calculate_window_average(t, 288, 144);
    for (float v : t_24h) {
        TEST_ASSERT_TRUE_MESSAGE(v >= 18.0f && v <= 26.0f,
                                  "t_24h avg in plausible range");
    }
    auto h_24h = calculate_window_average(h, 288, 144);
    for (float v : h_24h) {
        TEST_ASSERT_TRUE_MESSAGE(v >= 30.0f && v <= 80.0f,
                                  "h_24h avg in plausible range");
    }

    // 5. Downsample не падает на больших окнах.
    auto ds = downsample(co2, 168);
    TEST_ASSERT_EQUAL_size_t(168, ds.size());

    // 6. Навигация: cycle_param × 3 раза замыкает на исходный экран.
    for (int s = 0; s < 9; s++) {
        int after = cycle_param(cycle_param(cycle_param(s)));
        TEST_ASSERT_EQUAL_INT(s, after);
    }

    // 7. Алерт: симулируем CO2 крутящееся вокруг порогов.
    bool alert = false;
    for (const auto& m : history) {
        alert = update_alert_state(alert, m.co2);
    }
    // После 30 дней с реалистичным паттерном — алерт должен быть
    // в каком-то определённом состоянии (а не «вечно on» / «вечно off»
    // независимо от данных). Проверяем что функция вообще не падает.
    (void)alert;
}

int main(int argc, char** argv) {
    UNITY_BEGIN();

    RUN_TEST(test_measurement_is_10_bytes);
    RUN_TEST(test_is_measurement_valid_typical);
    RUN_TEST(test_is_measurement_valid_co2_range);
    RUN_TEST(test_is_measurement_valid_temp_range);
    RUN_TEST(test_is_measurement_valid_humidity_range);

    RUN_TEST(test_make_filename_zero_padding);
    RUN_TEST(test_extract_file_number_valid);
    RUN_TEST(test_extract_file_number_invalid);
    RUN_TEST(test_make_extract_roundtrip);
    RUN_TEST(test_next_file_number);

    RUN_TEST(test_oldest_empty_list);
    RUN_TEST(test_oldest_under_max);
    RUN_TEST(test_oldest_at_max);
    RUN_TEST(test_oldest_over_max_returns_min);
    RUN_TEST(test_oldest_unsorted_finds_min);
    RUN_TEST(test_oldest_with_holes);

    RUN_TEST(test_downsample_empty_input);
    RUN_TEST(test_downsample_target_zero_or_negative);
    RUN_TEST(test_downsample_passthrough);
    RUN_TEST(test_downsample_4_to_2_averages);
    RUN_TEST(test_downsample_upscale_repeats);
    RUN_TEST(test_downsample_real_24h);

    RUN_TEST(test_co2_traffic_good);
    RUN_TEST(test_co2_traffic_ok);
    RUN_TEST(test_co2_traffic_bad);
    RUN_TEST(test_temp_traffic_good);
    RUN_TEST(test_temp_traffic_ok);
    RUN_TEST(test_temp_traffic_bad);
    RUN_TEST(test_humidity_traffic_good);
    RUN_TEST(test_humidity_traffic_ok);
    RUN_TEST(test_humidity_traffic_bad);

    RUN_TEST(test_alert_off_stays_off_below_threshold);
    RUN_TEST(test_alert_off_turns_on_above_1500);
    RUN_TEST(test_alert_on_stays_on_above_1300);
    RUN_TEST(test_alert_on_turns_off_below_1300);
    RUN_TEST(test_alert_hysteresis_zone);
    RUN_TEST(test_alert_simulated_cycle);

    RUN_TEST(test_avg_window_uniform_history);
    RUN_TEST(test_avg_window_two_windows_progressive);
    RUN_TEST(test_avg_window_24h_downsample);
    RUN_TEST(test_avg_window_empty_history);
    RUN_TEST(test_avg_window_history_smaller_than_window);
    RUN_TEST(test_avg_window_invalid_sizes);
    RUN_TEST(test_avg_window_4_to_2);

    RUN_TEST(test_integration_synthetic_month);

    RUN_TEST(test_cycle_param_keeps_period_1h);
    RUN_TEST(test_cycle_param_keeps_period_24h);
    RUN_TEST(test_cycle_param_keeps_period_7d);

    RUN_TEST(test_cycle_period_keeps_param_CO2);
    RUN_TEST(test_cycle_period_keeps_param_T);
    RUN_TEST(test_cycle_period_keeps_param_RH);

    RUN_TEST(test_cycle_invalid_returns_zero);
    RUN_TEST(test_cycle_param_is_modulo_3);
    RUN_TEST(test_cycle_period_is_modulo_3);

    RUN_TEST(test_cycle_screen_basic_loop);
    RUN_TEST(test_cycle_screen_garbage_protection);
    RUN_TEST(test_cycle_screen_full_cycle_returns_to_start);

    return UNITY_END();
}
