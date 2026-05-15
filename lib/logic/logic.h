#pragma once

// ============================================================
// Чистая логика проекта — без зависимостей от Arduino/Wire/SPI.
// Компилируется и на ESP32, и на хосте (native) для юнит-тестов.
//
// Функции добавляются по мере реализации этапов:
//   Этап 6:  cycle_param, cycle_period
//   Этап 7:  extract_file_number, make_filename, next_file_number
//   Этап 9:  downsample
//   Этап 11: get_co2_traffic_level, get_temp_traffic_level,
//            get_humidity_traffic_level
//   Этап 12: update_alert_state
//   Этап 13: calculate_averages
// ============================================================

// Заглушка для Этапа 1 — нужна только чтобы test-инфраструктура
// и связка lib/logic <-> test/test_logic собирались.
int logic_stage_check();
