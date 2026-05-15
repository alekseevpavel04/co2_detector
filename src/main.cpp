#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// Этап 3: E-Paper Hello
// ============================================================
// Цель: нарисовать «Hello, e-Paper!» на экране Waveshare 2.13"
// (250×122 px, ч/б).
// Подключения (+ к Этапу 1, SCD41 пока не используется):
//     VCC  → 3V3
//     GND  → GND
//     DIN  → GPIO 11 (MOSI)
//     CLK  → GPIO 12 (SCK)
//     CS   → GPIO 10
//     DC   → GPIO 13
//     RST  → GPIO 6
//     BUSY → GPIO 7
//
// КЛАСС ДРАЙВЕРА: Waveshare 2.13" бывает с разными контроллерами,
// маркировка платы не всегда совпадает. Если экран НИЧЕГО не рисует,
// или рисует мусор/полосы — пробуй другой класс в порядке:
//   GxEPD2_213_B73        (SSD1680, новые партии — по умолчанию)
//   GxEPD2_213_B72        (SSD1675)
//   GxEPD2_213_BN
//   GxEPD2_213_DEPG0213BN
// Для смены — поменяй оба места: тип в шаблоне И параметр конструктора.

#define PIN_EPD_CS     10
#define PIN_EPD_MOSI   11
#define PIN_EPD_SCK    12
#define PIN_EPD_DC     13
#define PIN_EPD_RST    6
#define PIN_EPD_BUSY   7

#define LED_PIN        48
#define LED_LEVEL      32

// Выбор контроллера экрана — меняй здесь:
#define EPD_DRIVER     GxEPD2_213_B73

GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 3: E-Paper Hello");
    Serial.println("============================");

    // SPI на пинах из распиновки. MISO не нужен (экран write-only) — передаём -1.
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);

    // init(serial_diag_bitrate, initial, reset_duration_ms, pulldown_rst_mode)
    display.init(115200, true, 10, false);
    display.setRotation(1);            // 250×122 landscape
    display.setTextColor(GxEPD_BLACK);

    Serial.printf("Display size: %d x %d\n", display.width(), display.height());

    // Full-window обновление: рисуем в каждом «page» (для экранов с
    // частичным буфером — GxEPD2 рисует frame по частям).
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(20, 40);
        display.print("Hello, e-Paper!");

        display.setFont(&FreeSans9pt7b);
        display.setCursor(20, 70);
        display.print("Stage 3 OK");

        display.setCursor(20, 95);
        display.printf("Driver: %s", "B73");

        // Рамка по периметру — визуальный признак что границы экрана видны
        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    } while (display.nextPage());

    display.hibernate();  // выключаем питание контроллера, картинка остаётся
    Serial.println("Display updated. Picture should remain visible.");

    // Индикация что setup завершился: один красный «пых» LED.
    neopixelWrite(LED_PIN, LED_LEVEL, 0, 0);
    delay(500);
    neopixelWrite(LED_PIN, 0, 0, 0);
}

void loop() {
    // На Этапе 3 экран не перерисовывается — просто стоим.
    delay(1000);
}
