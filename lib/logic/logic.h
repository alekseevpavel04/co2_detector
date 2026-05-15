#pragma once

// ============================================================
// Чистая логика проекта — без зависимостей от Arduino/Wire/SPI.
// Компилируется на ESP32 и на хосте (native) для юнит-тестов.
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

// ------------------------------------------------------------
// Навигация (Этап 6)
// ------------------------------------------------------------
// Экраны: всего 9, индекс 0..8.
//   param_idx  = screen / 3   (0=CO2, 1=Temperature, 2=Humidity)
//   period_idx = screen % 3   (0=1h, 1=24h, 2=7d)
//
// cycle_param  — кнопка «показатель»: меняет param_idx по кругу,
//                period_idx остаётся прежним.
// cycle_period — кнопка «период»:     меняет period_idx по кругу,
//                param_idx остаётся прежним.
//
// Если current_screen вне диапазона 0..8 — возвращают 0 (защита
// от мусора в RTC memory при первом включении).
int cycle_param(int current_screen);
int cycle_period(int current_screen);

// Заглушка для test-инфраструктуры (с Этапа 1). Удалим, когда
// в logic.cpp появится достаточно реального содержимого.
int logic_stage_check();
