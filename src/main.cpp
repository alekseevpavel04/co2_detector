#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include <SensirionErrors.h>

// ============================================================
// Этап 2: SCD41 в Serial
// ============================================================
// Цель: каждые ~5 сек выводить CO2, температуру и влажность.
// Подключения: USB + 4 провода SCD41:
//     VDD → 3V3
//     GND → GND
//     SDA → GPIO 8
//     SCL → GPIO 9
// I2C адрес SCD41: 0x62.
//
// Используется single-shot режим — он совпадает с будущей
// архитектурой deep sleep (Этап 5+). Цикл одного измерения:
//   measureSingleShot()  → команда «начать»
//   delay(5000)          → ждём пока сенсор прогреется и измерит
//   readMeasurement(...) → читаем результат (10 байт по I2C)
//
// Первые ~10 минут температура может быть завышена на ~1°C
// из-за саморазогрева датчика — это нормально, см. datasheet SCD41.

#define LED_PIN     48
#define LED_LEVEL   32
#define PIN_SDA     8
#define PIN_SCL     9

#define MEASUREMENT_DELAY_MS  5000   // single-shot требует ~5 сек

SensirionI2cScd4x scd4x;
bool sensor_ok = false;

static void print_scd_error(const char* fn, int16_t err) {
    char msg[64];
    errorToString(static_cast<uint16_t>(err), msg, sizeof(msg));
    Serial.printf("  %s: %d (%s)\n", fn, err, msg);
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println();
    Serial.println("============================");
    Serial.println("Stage 2: SCD41 readout");
    Serial.println("============================");

    Wire.begin(PIN_SDA, PIN_SCL);
    scd4x.begin(Wire, SCD41_I2C_ADDR_62);

    // Останавливаем периодические измерения на всякий случай —
    // вдруг после soft-reset датчик остался в этом режиме.
    // На «холодном» старте это вернёт ошибку — игнорируем её.
    int16_t err = scd4x.stopPeriodicMeasurement();
    if (err) {
        Serial.println("stopPeriodicMeasurement (ignored on first boot):");
        print_scd_error("stop", err);
    }
    delay(500);

    // Проверка связи: запрашиваем серийный номер датчика.
    uint64_t serial_number = 0;
    err = scd4x.getSerialNumber(serial_number);
    if (err) {
        Serial.println("ERROR: SCD41 not responding.");
        print_scd_error("getSerialNumber", err);
        Serial.println("  Check wiring: SDA=GPIO8, SCL=GPIO9, VDD=3V3, GND=GND.");
        Serial.println("  Expected I2C address: 0x62.");
        sensor_ok = false;
    } else {
        Serial.printf("SCD41 OK. Serial: 0x%012llx\n",
                      (unsigned long long)serial_number);
        sensor_ok = true;
    }
}

void loop() {
    static uint32_t counter = 0;
    counter++;

    // --- Случай: датчик не отвечал в setup() — пробуем заново ---
    if (!sensor_ok) {
        // Жёлтый LED сигнализирует о проблеме с датчиком.
        neopixelWrite(LED_PIN, LED_LEVEL, LED_LEVEL, 0);
        Serial.printf("[%lu] Sensor offline, retrying in 5s\n", counter);
        delay(MEASUREMENT_DELAY_MS);
        uint64_t sn = 0;
        if (scd4x.getSerialNumber(sn) == 0) {
            Serial.printf("[%lu] Sensor back online (SN 0x%012llx)\n",
                          counter, (unsigned long long)sn);
            sensor_ok = true;
        }
        return;
    }

    // --- Обычный цикл: запросили измерение → ждём → читаем ---
    // Зелёный LED — идёт замер.
    neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);

    int16_t err = scd4x.measureSingleShot();
    if (err) {
        Serial.printf("[%lu] measureSingleShot failed:\n", counter);
        print_scd_error("measureSingleShot", err);
        neopixelWrite(LED_PIN, 0, 0, 0);
        delay(MEASUREMENT_DELAY_MS);
        return;
    }

    delay(MEASUREMENT_DELAY_MS);  // нагрев + sampling датчика

    uint16_t co2;
    float temperature, humidity;
    err = scd4x.readMeasurement(co2, temperature, humidity);

    neopixelWrite(LED_PIN, 0, 0, 0);   // LED off после измерения

    if (err) {
        Serial.printf("[%lu] readMeasurement failed:\n", counter);
        print_scd_error("readMeasurement", err);
    } else if (co2 == 0) {
        // Датчик отвечает по I2C, но данных ещё нет — обычно
        // если readMeasurement вызвать слишком рано.
        Serial.printf("[%lu] CO2=0 (sensor not ready, will retry)\n", counter);
    } else {
        Serial.printf("[%lu] CO2: %u ppm, T: %.1f°C, H: %.0f%%\n",
                      counter, co2, temperature, humidity);
    }
    // single-shot уже отнял ~5 сек — дополнительной задержки не нужно.
}
