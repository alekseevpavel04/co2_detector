#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

// ============================================================
// Чистая логика проекта — без зависимостей от Arduino/Wire/SPI.
// Компилируется на ESP32 и на хосте (native).
// ============================================================

// ------------------------------------------------------------
// Структура одного измерения (Этап 7)
// ------------------------------------------------------------
// Ровно 10 байт. packed обязателен — без него компилятор добавит
// padding и sizeof(Measurement) станет 12 (КРИТИЧЕСКОЕ #5).
struct __attribute__((packed)) Measurement {
    uint32_t timestamp;     // секунды с старта (uptime). Обнуляется при power-off.
    uint16_t co2;           // ppm (валидный диапазон 0..10000)
    int16_t  temp_x10;      // T × 10 (235 = 23.5°C)
    uint8_t  humidity;      // RH в % (0..100)
    uint8_t  flags;         // резерв
};
static_assert(sizeof(Measurement) == 10, "Measurement must be 10 bytes");

// Валидация: значения вне диапазона — мусор от глюка датчика
// или плохого контакта; такие не сохраняем и не рисуем.
bool is_measurement_valid(const Measurement& m);

// ------------------------------------------------------------
// File numbering (Этап 7)
// ------------------------------------------------------------
// Файлы данных называются /data/measurements_NNN.bin, где NNN —
// трёхзначный номер с ведущими нулями (001..999).

// "/data/measurements_001.bin" → "/data/measurements_001.bin"
//                       ^номер^                   ^номер^
std::string make_filename(int number);

// "/data/measurements_007.bin" → 7
// строка без правильного формата → -1
int extract_file_number(const std::string& path);

// Просто +1, обёртка для семантической ясности.
int next_file_number(int current);

// ------------------------------------------------------------
// Навигация по 9 экранам (Этап 6)
// ------------------------------------------------------------
int cycle_param(int current_screen);
int cycle_period(int current_screen);
