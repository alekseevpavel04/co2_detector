#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <SensirionI2cScd4x.h>
#include <SensirionErrors.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include "logic.h"

// ============================================================
// Этап 6: Кнопки + навигация по 9 экранам
// ============================================================
// Что нового:
//   - Две кнопки на GPIO 4 («показатель») и GPIO 5 («период»).
//     Один контакт каждой — на GND, второй — на GPIO. Внутренний
//     pull-up через rtc_gpio_pullup_en (КРИТИЧЕСКОЕ #7).
//   - Источников пробуждения теперь два:
//       TIMER → стандартный цикл: измерение → отрисовка → sleep.
//       EXT1  → нажата кнопка: меняем current_screen, перерисовываем
//               текущий экран (датчик не трогаем), sleep.
//   - current_screen в RTC memory переживает sleep.
//   - last_co2/temp/humidity тоже в RTC — чтобы при пробуждении
//     по кнопке было что отрисовать (без нового измерения).
//   - Перед каждым go_to_sleep() ждём отпускания кнопок,
//     иначе ESP-IDF проснётся снова на той же кнопке (бесконечный цикл).
//
// Отрисовка пока заглушка: «Screen N: <param> for <period>» + последние
// значения. Реальные графики появятся с Этапа 9.

// --- Пины ---
#define PIN_SDA            8
#define PIN_SCL            9
#define PIN_BUTTON_PARAM   4    // RTC GPIO 4 — кнопка «показатель»
#define PIN_BUTTON_PERIOD  5    // RTC GPIO 5 — кнопка «период»
#define PIN_EPD_CS         10
#define PIN_EPD_MOSI       11
#define PIN_EPD_SCK        12
#define PIN_EPD_DC         13
#define PIN_EPD_RST        6
#define PIN_EPD_BUSY       7
#define LED_PIN            48
#define LED_LEVEL          32

// --- Интервалы ---
#define MEASUREMENT_DELAY_MS   5000
#define MEASUREMENT_INTERVAL   300        // sleep между замерами, сек
#define BUTTON_RELEASE_TIMEOUT 3000       // мс — сколько ждём отпускания

// --- Драйвер e-Paper ---
#define EPD_DRIVER     GxEPD2_213_B73

#define RTC_MAGIC      0xDEADBEEF
#define N_SCREENS      9

SensirionI2cScd4x scd4x;
GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

// --- RTC memory ---
RTC_DATA_ATTR uint32_t rtc_magic = 0;
RTC_DATA_ATTR uint32_t wake_count = 0;
RTC_DATA_ATTR uint32_t total_uptime_before_sleep = 0;
RTC_DATA_ATTR int      current_screen = 0;
RTC_DATA_ATTR bool     last_valid = false;
RTC_DATA_ATTR uint16_t last_co2  = 0;
RTC_DATA_ATTR float    last_t    = 0.0f;
RTC_DATA_ATTR float    last_h    = 0.0f;

bool sensor_ok = false;

static const char* PARAM_NAMES[3]  = { "CO2", "Temperature", "Humidity" };
static const char* PERIOD_NAMES[3] = { "1 hour", "24 hours", "7 days" };

// ------------------------------------------------------------
// Хелперы (датчик, экран, sleep)
// ------------------------------------------------------------

static void print_scd_error(const char* fn, int16_t err) {
    char msg[64];
    errorToString(static_cast<uint16_t>(err), msg, sizeof(msg));
    Serial.printf("  %s: %d (%s)\n", fn, err, msg);
}

static void init_sensor() {
    Wire.begin(PIN_SDA, PIN_SCL);
    scd4x.begin(Wire, SCD41_I2C_ADDR_62);

    if (wake_count == 1) {
        int16_t err = scd4x.stopPeriodicMeasurement();
        if (err) {
            Serial.println("stopPeriodicMeasurement (ignored on first boot):");
            print_scd_error("stop", err);
        }
        delay(500);
    }

    uint64_t sn = 0;
    int16_t err = scd4x.getSerialNumber(sn);
    if (err) {
        Serial.println("ERROR: SCD41 not responding.");
        print_scd_error("getSerialNumber", err);
        sensor_ok = false;
    } else {
        Serial.printf("SCD41 OK. SN: 0x%012llx\n", (unsigned long long)sn);
        sensor_ok = true;
    }
}

static void init_display() {
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, /*initial=*/true, 10, false);
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);
}

static bool take_measurement(uint16_t& co2, float& t, float& h) {
    neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);

    int16_t err = scd4x.measureSingleShot();
    if (err) {
        Serial.println("measureSingleShot failed:");
        print_scd_error("measureSingleShot", err);
        neopixelWrite(LED_PIN, 0, 0, 0);
        return false;
    }

    delay(MEASUREMENT_DELAY_MS);

    err = scd4x.readMeasurement(co2, t, h);
    neopixelWrite(LED_PIN, 0, 0, 0);

    if (err) {
        Serial.println("readMeasurement failed:");
        print_scd_error("readMeasurement", err);
        return false;
    }
    if (co2 == 0) {
        Serial.println("CO2=0 (sensor not ready)");
        return false;
    }
    return true;
}

static void draw_warmup() {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(15, 55);
        display.print("Warming up sensor...");
        display.setFont(&FreeSans9pt7b);
        display.setCursor(85, 85);
        display.print("Please wait");
    } while (display.nextPage());
}

// Заглушка экранов: показываем индекс, название и последние значения.
// На Этапе 9-10 эта функция превратится в полноценную отрисовку графиков.
static void draw_screen(int screen, bool valid,
                         uint16_t co2, float t, float h) {
    if (screen < 0 || screen >= N_SCREENS) screen = 0;
    int param_idx  = screen / 3;
    int period_idx = screen % 3;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        display.setFont(&FreeSansBold12pt7b);
        display.setCursor(5, 22);
        display.printf("Screen %d", screen);

        display.setFont(&FreeSans9pt7b);
        display.setCursor(5, 45);
        display.printf("%s for %s",
                       PARAM_NAMES[param_idx], PERIOD_NAMES[period_idx]);

        if (valid) {
            display.setCursor(5, 75);
            display.printf("CO2: %u ppm", co2);
            display.setCursor(5, 100);
            display.printf("T: %.1f C   H: %.0f %%", t, h);
        } else {
            display.setCursor(5, 85);
            display.print("(no data yet)");
        }

        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    } while (display.nextPage());
    display.hibernate();
}

static void wait_for_button_release() {
    unsigned long start = millis();
    while (true) {
        bool p1 = digitalRead(PIN_BUTTON_PARAM)  == HIGH;
        bool p2 = digitalRead(PIN_BUTTON_PERIOD) == HIGH;
        if (p1 && p2) break;
        if (millis() - start > BUTTON_RELEASE_TIMEOUT) {
            Serial.println("wait_for_button_release: timeout");
            break;
        }
        delay(10);
    }
    delay(50);  // anti-bounce
}

static void go_to_sleep() {
    if (sensor_ok) {
        int16_t err = scd4x.powerDown();
        if (err) {
            Serial.println("powerDown failed (sleeping anyway):");
            print_scd_error("powerDown", err);
        }
    }

    total_uptime_before_sleep += millis() / 1000;
    Serial.printf("Total uptime: %u sec. Sleeping.\n", total_uptime_before_sleep);

    // --- Timer wake (через MEASUREMENT_INTERVAL сек) ---
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(MEASUREMENT_INTERVAL) * 1000000ULL);

    // --- Button wake (ext1, ANY_LOW) ---
    // GPIO 4 и 5 — RTC GPIO на ESP32-S3, поэтому работают как источник
    // пробуждения. Включаем pull-up через RTC IO модуль — обычный
    // pinMode(INPUT_PULLUP) в sleep не сохраняется.
    rtc_gpio_pullup_en  (static_cast<gpio_num_t>(PIN_BUTTON_PARAM));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BUTTON_PARAM));
    rtc_gpio_pullup_en  (static_cast<gpio_num_t>(PIN_BUTTON_PERIOD));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BUTTON_PERIOD));

    uint64_t button_mask =
        (1ULL << PIN_BUTTON_PARAM) | (1ULL << PIN_BUTTON_PERIOD);
    esp_sleep_enable_ext1_wakeup(button_mask, ESP_EXT1_WAKEUP_ANY_LOW);

    Serial.flush();
    esp_deep_sleep_start();
    // unreachable
}

// ------------------------------------------------------------
// setup() / loop()
// ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 6: Buttons");
    Serial.println("============================");

    // RTC magic + защита от мусора в current_screen.
    if (rtc_magic != RTC_MAGIC) {
        rtc_magic = RTC_MAGIC;
        wake_count = 0;
        total_uptime_before_sleep = 0;
        current_screen = 0;
        last_valid = false;
        last_co2 = 0; last_t = 0.0f; last_h = 0.0f;
        Serial.println("First boot — RTC initialized");
    }
    if (current_screen < 0 || current_screen >= N_SCREENS) current_screen = 0;
    wake_count++;

    // Кнопки: при пробуждении ESP кладёт GPIO в обычный режим — поэтому
    // ставим INPUT_PULLUP, чтобы digitalRead в wait_for_button_release
    // отдавал HIGH при отпущенной кнопке.
    pinMode(PIN_BUTTON_PARAM,  INPUT_PULLUP);
    pinMode(PIN_BUTTON_PERIOD, INPUT_PULLUP);

    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    bool from_button = (reason == ESP_SLEEP_WAKEUP_EXT1);
    Serial.printf("Wake #%u, cause: %d (%s)\n",
                  wake_count, reason,
                  from_button ? "BUTTON" :
                  reason == ESP_SLEEP_WAKEUP_TIMER ? "TIMER" : "OTHER");

    init_display();

    if (from_button) {
        // Кнопка: меняем экран, без измерения.
        uint64_t pin_mask = esp_sleep_get_ext1_wakeup_status();
        if (pin_mask & (1ULL << PIN_BUTTON_PARAM)) {
            int prev = current_screen;
            current_screen = cycle_param(current_screen);
            Serial.printf("PARAM button: screen %d → %d\n", prev, current_screen);
        } else if (pin_mask & (1ULL << PIN_BUTTON_PERIOD)) {
            int prev = current_screen;
            current_screen = cycle_period(current_screen);
            Serial.printf("PERIOD button: screen %d → %d\n", prev, current_screen);
        } else {
            Serial.println("EXT1 wake but no recognized button in mask?");
        }
    } else {
        // Timer / первый запуск: измеряем.
        init_sensor();
        if (wake_count == 1) {
            Serial.println("First wake: showing warmup screen");
            draw_warmup();
        } else if (sensor_ok) {
            int16_t err = scd4x.wakeUp();
            if (err) {
                Serial.println("wakeUp failed:");
                print_scd_error("wakeUp", err);
            }
            delay(30);
        }

        if (sensor_ok) {
            uint16_t co2 = 0;
            float t = 0.0f, h = 0.0f;
            if (take_measurement(co2, t, h)) {
                last_co2 = co2; last_t = t; last_h = h; last_valid = true;
                Serial.printf("CO2: %u ppm, T: %.1f C, H: %.0f %%\n", co2, t, h);
            } else {
                Serial.println("No valid measurement (keeping previous, if any)");
            }
        }
    }

    draw_screen(current_screen, last_valid, last_co2, last_t, last_h);

    if (from_button) {
        wait_for_button_release();
    }
    go_to_sleep();   // не возвращается
}

void loop() {
    go_to_sleep();
}
