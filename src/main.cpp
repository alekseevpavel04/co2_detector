#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SensirionI2cScd4x.h>
#include <SensirionErrors.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

// ============================================================
// Этап 4: SCD41 + E-Paper вместе
// ============================================================
// Цель: каждые ~30 сек измерять CO2/T/H и обновлять экран.
// Подключения: USB + SCD41 (GPIO 8/9) + e-Paper (GPIO 6/7/10/11/12/13).
// Deep sleep ещё нет — устройство работает непрерывно.

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
#define MEASUREMENT_DELAY_MS  5000    // ~5 сек на single-shot
#define CYCLE_INTERVAL_MS     30000   // полный цикл ~30 сек

// --- Драйвер e-Paper (поменять, если экран не показывает: B72/BN/DEPG0213BN) ---
#define EPD_DRIVER     GxEPD2_213_B73

SensirionI2cScd4x scd4x;
GxEPD2_BW<EPD_DRIVER, EPD_DRIVER::HEIGHT> display(
    EPD_DRIVER(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

bool sensor_ok = false;

static void print_scd_error(const char* fn, int16_t err) {
    char msg[64];
    errorToString(static_cast<uint16_t>(err), msg, sizeof(msg));
    Serial.printf("  %s: %d (%s)\n", fn, err, msg);
}

static void init_sensor() {
    Wire.begin(PIN_SDA, PIN_SCL);
    scd4x.begin(Wire, SCD41_I2C_ADDR_62);

    int16_t err = scd4x.stopPeriodicMeasurement();
    if (err) {
        Serial.println("stopPeriodicMeasurement (ignored on first boot):");
        print_scd_error("stop", err);
    }
    delay(500);

    uint64_t sn = 0;
    err = scd4x.getSerialNumber(sn);
    if (err) {
        Serial.println("ERROR: SCD41 not responding.");
        print_scd_error("getSerialNumber", err);
        sensor_ok = false;
    } else {
        Serial.printf("SCD41 OK. Serial: 0x%012llx\n", (unsigned long long)sn);
        sensor_ok = true;
    }
}

static void init_display() {
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, true, 10, false);
    display.setRotation(1);
    display.setTextColor(GxEPD_BLACK);
    Serial.printf("Display: %d x %d\n", display.width(), display.height());
}

// Берём один замер. Возвращает true, если успешно.
static bool take_measurement(uint16_t& co2, float& t, float& h) {
    neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);  // зелёный = идёт замер

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
        Serial.println("CO2=0 (sensor not ready yet)");
        return false;
    }
    return true;
}

// Рисуем три значения. Если valid==false — пишем «No data».
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

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 4: SCD41 + E-Paper");
    Serial.println("============================");

    init_sensor();
    init_display();

    // На старте показываем placeholder — пока не сделан первый замер
    draw_values(false, 0, 0, 0);
}

void loop() {
    static uint32_t counter = 0;
    counter++;
    uint32_t cycle_start = millis();

    uint16_t co2 = 0;
    float t = 0.0f, h = 0.0f;
    bool ok = false;

    if (sensor_ok) {
        ok = take_measurement(co2, t, h);
    }

    if (ok) {
        Serial.printf("[%lu] CO2: %u ppm, T: %.1f C, H: %.0f %%\n",
                      counter, co2, t, h);
    } else {
        Serial.printf("[%lu] No valid measurement\n", counter);
    }

    draw_values(ok, co2, t, h);

    // Досыпаем до полного цикла CYCLE_INTERVAL_MS (с учётом времени измерения
    // и обновления экрана). Если уже превысили — сразу идём дальше.
    uint32_t elapsed = millis() - cycle_start;
    if (elapsed < CYCLE_INTERVAL_MS) {
        delay(CYCLE_INTERVAL_MS - elapsed);
    }
}
