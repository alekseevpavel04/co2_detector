#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <SensirionI2cScd4x.h>
#include <SensirionErrors.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// Этап 5: Deep sleep + RTC memory
// ============================================================
// Цель: цикл wake → measure → display → sleep (5 минут).
//
// Что нового:
//   - Перед sleep вызываем scd4x.powerDown() — потребление падает
//     с ~0.5 мА до ~0.15 мкА (КРИТИЧЕСКОЕ #4).
//   - Используем RTC_DATA_ATTR для счётчика пробуждений и
//     накопленного uptime. Магическое число RTC_MAGIC защищает
//     от мусора при первом включении (КРИТИЧЕСКОЕ #3).
//   - При wake_count == 1 показываем welcome screen («Warming up
//     sensor...») через partial refresh — быстро и без мерцания.
//
// Архитектурно loop() здесь не используется: всё происходит
// в setup(), и в конце уходим в deep sleep. После пробуждения
// процессор начинает заново с setup().

// --- Пины ---
#define PIN_SDA        8
#define PIN_SCL        9
#define PIN_EPD_CS     10
#define PIN_EPD_MOSI   11
#define PIN_EPD_SCK    12
#define PIN_EPD_DC     13
#define PIN_EPD_RST    6
#define PIN_EPD_BUSY   7
#define LED_PIN        48
#define LED_LEVEL      32

// --- Интервалы ---
#define MEASUREMENT_DELAY_MS   5000   // ~5 сек на single-shot
#define MEASUREMENT_INTERVAL   300    // спим 5 минут (в секундах)

// --- Драйвер e-Paper ---
#define EPD_DRIVER     GxEPD2_213_B73

// --- RTC magic ---
#define RTC_MAGIC      0xDEADBEEF

SensirionI2cScd4x scd4x;
GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

// --- Переменные в RTC memory: переживают deep sleep ---
RTC_DATA_ATTR uint32_t rtc_magic = 0;
RTC_DATA_ATTR uint32_t wake_count = 0;
RTC_DATA_ATTR uint32_t total_uptime_before_sleep = 0;

// --- Сессионные переменные (не переживают sleep, очищаются заново) ---
bool sensor_ok = false;

// ------------------------------------------------------------
// Хелперы
// ------------------------------------------------------------

static void print_scd_error(const char* fn, int16_t err) {
    char msg[64];
    errorToString(static_cast<uint16_t>(err), msg, sizeof(msg));
    Serial.printf("  %s: %d (%s)\n", fn, err, msg);
}

static void init_sensor() {
    Wire.begin(PIN_SDA, PIN_SCL);
    scd4x.begin(Wire, SCD41_I2C_ADDR_62);

    // Не пытаемся stopPeriodicMeasurement при wake — после powerDown
    // сенсор и так не в periodic. На первом boot — попробуем, мало ли.
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
    neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);  // зелёный во время замера

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

// Welcome screen — показывается ОДИН РАЗ на первом включении
// пока идёт первое измерение (5 сек). Partial refresh — без мерцания.
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

static void draw_values(bool valid, uint16_t co2, float t, float h) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        if (!valid) {
            display.setFont(&FreeSansBold12pt7b);
            display.setCursor(40, 65);
            display.print("No data");
        } else {
            display.setFont(&FreeSansBold12pt7b);
            display.setCursor(5, 30);
            display.printf("CO2: %u ppm", co2);

            display.setFont(&FreeSans9pt7b);
            display.setCursor(5, 65);
            display.printf("T: %.1f C", t);
            display.setCursor(5, 95);
            display.printf("H: %.0f %%", h);
        }
        display.drawRect(0, 0, display.width(), display.height(), GxEPD_BLACK);
    } while (display.nextPage());
    display.hibernate();
}

static void go_to_sleep() {
    // Гасим сенсор — без этого в sleep он съест ~0.5 мА (КРИТИЧЕСКОЕ #4).
    if (sensor_ok) {
        int16_t err = scd4x.powerDown();
        if (err) {
            Serial.println("powerDown failed (sleeping anyway):");
            print_scd_error("powerDown", err);
        }
    }

    // Накапливаем общий uptime через циклы sleep.
    total_uptime_before_sleep += millis() / 1000;

    Serial.printf("Total uptime so far: %u sec. Sleeping for %d sec.\n",
                  total_uptime_before_sleep, MEASUREMENT_INTERVAL);
    Serial.flush();

    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(MEASUREMENT_INTERVAL) * 1000000ULL);
    esp_deep_sleep_start();
    // сюда не возвращаемся
}

// ------------------------------------------------------------
// setup() / loop()
// ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 5: Deep sleep");
    Serial.println("============================");

    // Инициализация RTC memory при первом power-on (или после замены батарей):
    // содержимое RTC_DATA_ATTR может быть мусором — проверяем магией.
    if (rtc_magic != RTC_MAGIC) {
        rtc_magic = RTC_MAGIC;
        wake_count = 0;
        total_uptime_before_sleep = 0;
        Serial.println("First boot — RTC memory initialized");
    }
    wake_count++;

    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    Serial.printf("Wake #%u, cause: %d, prior uptime: %u sec\n",
                  wake_count, reason, total_uptime_before_sleep);

    init_display();
    init_sensor();

    // На первом запуске — показываем «warming up» через partial refresh.
    // На последующих пробуждениях — будим сенсор после powerDown.
    if (wake_count == 1) {
        Serial.println("First wake: showing warmup screen");
        draw_warmup();
    } else {
        if (sensor_ok) {
            int16_t err = scd4x.wakeUp();
            if (err) {
                Serial.println("wakeUp failed:");
                print_scd_error("wakeUp", err);
            }
            delay(30);  // SCD41 нужно ~20 мс на пробуждение
        }
    }

    // Один замер за пробуждение.
    uint16_t co2 = 0;
    float t = 0.0f, h = 0.0f;
    bool ok = false;
    if (sensor_ok) {
        ok = take_measurement(co2, t, h);
    }

    if (ok) {
        Serial.printf("CO2: %u ppm, T: %.1f C, H: %.0f %%\n", co2, t, h);
    } else {
        Serial.println("No valid measurement");
    }

    draw_values(ok, co2, t, h);

    go_to_sleep();   // не возвращается
}

void loop() {
    // Не должны сюда попадать — но если попали, всё равно в sleep.
    go_to_sleep();
}
