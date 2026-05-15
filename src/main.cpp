#include <Arduino.h>

// ============================================================
// Этап 1: Hello World
// ============================================================
// Цель: вывод в Serial и мигание встроенного LED каждые 500 мс.
// Подключения: только USB к ESP32-S3 SuperMini.
//
// На большинстве плат SuperMini встроенный LED — это RGB WS2812
// на GPIO 48. Для него нужен neopixelWrite — обычный digitalWrite
// сигнала WS2812 не сформирует.
//
// Если LED не мигает, но Serial выводится — это нормально:
//   - LED может быть на другом GPIO (поменяй LED_PIN ниже)
//   - на некоторых вариантах платы LED отсутствует
// Главный критерий успеха Этапа 1 — Serial вывод.

#define LED_PIN     48   // GPIO встроенного RGB WS2812
#define LED_LEVEL   32   // яркость 0-255 (32 — комфортная, не слепит)

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 1: Hello, ESP32-S3!");
    Serial.println("============================");
    Serial.printf("LED on GPIO %d (assumed RGB WS2812)\n", LED_PIN);
    Serial.println("Blinking every 500 ms...");
}

void loop() {
    static bool state = false;
    static uint32_t counter = 0;
    state = !state;
    counter++;

    // Красный канал; зелёный и синий выключены.
    neopixelWrite(LED_PIN, state ? LED_LEVEL : 0, 0, 0);

    Serial.printf("[%lu] Blink %s\n", counter, state ? "ON " : "OFF");
    delay(500);
}
