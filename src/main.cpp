#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <SensirionI2cScd4x.h>
#include <SensirionErrors.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>

#include <vector>
#include <algorithm>
#include <cstring>

#include "logic.h"

// ============================================================
// Этап 9: Первый график (CO2 за 1 час, 12 столбиков)
// ============================================================
// Что нового по сравнению с Этапом 8:
//   - read_last_n_from_file читает последние N измерений из файла
//     через seek — без загрузки всего файла в RAM.
//   - Экран 0 (CO2 для 1 hour) теперь рисует столбики реальных
//     данных вместо placeholder-текста. Высота столбика =
//     (co2 - 400) / (2000 - 400) * graph_height; значения вне
//     диапазона клампятся к шкале.
//   - Экраны 1-8 пока остаются с placeholder — реализация всех 9
//     экранов будет на Этапе 10.
//   - Заполнение справа налево: если данных меньше 12, левая
//     часть остаётся пустой.
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

// --- Хранилище ---
#define MAX_FILE_SIZE  (90 * 1024)   // байт. 9000 measurement ≈ 31 день при 5-мин интервале.
#define MAX_FILES      13            // суммарно 90 КБ × 13 ≈ ~400 дней истории.

// --- Шкалы графиков ---
#define CO2_SCALE_MIN     400
#define CO2_SCALE_MAX     2000
#define TEMP_SCALE_MIN    15
#define TEMP_SCALE_MAX    30
#define HUMIDITY_SCALE_MIN 20
#define HUMIDITY_SCALE_MAX 80

// --- Геометрия экрана (после setRotation(1) — 250×122) ---
#define SCREEN_W       250
#define SCREEN_H       122
#define GRAPH_X        15        // отступ слева под подписи Y
#define GRAPH_Y        28        // под title
#define GRAPH_W        (SCREEN_W - GRAPH_X - 5)   // 230
#define GRAPH_H        68        // высота области графика
#define GRAPH_BOTTOM   (GRAPH_Y + GRAPH_H)        // 96

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
RTC_DATA_ATTR char     cached_file_path[64] = "";

bool sensor_ok = false;
bool fs_ok     = false;

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

static void init_filesystem() {
    // false = не форматировать при ошибке монтирования. Если первый запуск
    // и раздел чистый — это вернёт false, тогда пробуем с format=true.
    if (!LittleFS.begin(false)) {
        Serial.println("LittleFS mount failed, formatting...");
        if (!LittleFS.begin(true)) {
            Serial.println("ERROR: LittleFS format failed too!");
            fs_ok = false;
            return;
        }
        Serial.println("LittleFS formatted and mounted");
    }
    fs_ok = true;

    if (!LittleFS.exists("/data")) {
        LittleFS.mkdir("/data");
        Serial.println("Created /data directory");
    }
    Serial.printf("LittleFS: %u / %u bytes used\n",
                  (unsigned)LittleFS.usedBytes(),
                  (unsigned)LittleFS.totalBytes());
}

// Сканируем /data/, возвращаем номера всех файлов measurements_NNN.bin
// (отсортированы по возрастанию).
static std::vector<int> list_data_file_numbers() {
    std::vector<int> result;
    File dir = LittleFS.open("/data");
    if (!dir || !dir.isDirectory()) return result;

    File entry = dir.openNextFile();
    while (entry) {
        if (!entry.isDirectory()) {
            // entry.name() в разных версиях LittleFS возвращает либо
            // "/data/measurements_001.bin", либо "measurements_001.bin".
            // extract_file_number обрабатывает оба варианта.
            int n = extract_file_number(std::string(entry.name()));
            if (n > 0) result.push_back(n);
        }
        entry.close();
        entry = dir.openNextFile();
    }
    dir.close();
    std::sort(result.begin(), result.end());
    return result;
}

// Возвращает путь к активному (последнему по номеру) файлу.
// Использует RTC-кэш cached_file_path. При первом обращении
// (или если кэш указывает на несуществующий файл) сканирует /data/.
static String get_current_file_path() {
    if (cached_file_path[0] != '\0' && LittleFS.exists(cached_file_path)) {
        return String(cached_file_path);
    }

    auto nums = list_data_file_numbers();
    int n = nums.empty() ? 1 : nums.back();
    std::string path = make_filename(n);
    std::strncpy(cached_file_path, path.c_str(), sizeof(cached_file_path) - 1);
    cached_file_path[sizeof(cached_file_path) - 1] = '\0';
    Serial.printf("Current file resolved: %s\n", cached_file_path);
    return String(cached_file_path);
}

// Создаём следующий файл по номеру и удаляем самый старый,
// если файлов теперь больше MAX_FILES. Обновляем RTC-кэш.
static void rotate_files() {
    auto nums = list_data_file_numbers();
    int current = nums.empty() ? 1 : nums.back();
    int next = next_file_number(current);

    std::string new_path = make_filename(next);
    File f = LittleFS.open(new_path.c_str(), "a", /*create=*/true);
    if (f) f.close();
    Serial.printf("Rotated to %s\n", new_path.c_str());

    std::strncpy(cached_file_path, new_path.c_str(), sizeof(cached_file_path) - 1);
    cached_file_path[sizeof(cached_file_path) - 1] = '\0';

    // Удаляем старейший, если общее число файлов перевалило за MAX_FILES.
    nums = list_data_file_numbers();
    int to_del = oldest_file_to_remove(nums, MAX_FILES);
    if (to_del > 0) {
        std::string p = make_filename(to_del);
        if (LittleFS.remove(p.c_str())) {
            Serial.printf("Deleted oldest: %s\n", p.c_str());
        } else {
            Serial.printf("Failed to delete: %s\n", p.c_str());
        }
    }
}

// Читаем последние n записей из файла. Использует seek чтобы не
// тянуть весь файл в RAM (важно для 7-дневного графика — 2016 записей,
// ~20 КБ; для 1h всё ещё крохотно, но используем единую функцию).
static std::vector<Measurement> read_last_n_from_file(const String& path, int n) {
    std::vector<Measurement> result;
    if (n <= 0) return result;
    if (!LittleFS.exists(path)) return result;

    File f = LittleFS.open(path, "r");
    if (!f) return result;

    int records = static_cast<int>(f.size() / sizeof(Measurement));
    int to_read = std::min(n, records);
    if (to_read <= 0) { f.close(); return result; }

    f.seek(static_cast<size_t>(records - to_read) * sizeof(Measurement));
    Measurement m;
    for (int i = 0; i < to_read; i++) {
        size_t got = f.read(reinterpret_cast<uint8_t*>(&m), sizeof(m));
        if (got != sizeof(m)) break;
        if (is_measurement_valid(m)) result.push_back(m);
    }
    f.close();
    return result;
}

// Аппендим Measurement в текущий файл. Возвращаем true при успехе.
static bool save_measurement(const Measurement& m) {
    if (!fs_ok) {
        Serial.println("save_measurement: FS not mounted");
        return false;
    }
    if (!is_measurement_valid(m)) {
        Serial.printf("save_measurement: invalid (co2=%u t10=%d h=%u), skipping\n",
                      m.co2, m.temp_x10, m.humidity);
        return false;
    }

    String path = get_current_file_path();
    File f = LittleFS.open(path, "a", /*create=*/true);
    if (!f) {
        Serial.printf("save_measurement: cannot open %s\n", path.c_str());
        return false;
    }
    size_t written = f.write(reinterpret_cast<const uint8_t*>(&m), sizeof(m));
    f.flush();
    size_t fsize = f.size();
    f.close();

    if (written != sizeof(m)) {
        Serial.printf("save_measurement: short write %u/%u\n",
                      (unsigned)written, (unsigned)sizeof(m));
        return false;
    }
    int records = static_cast<int>(fsize / sizeof(Measurement));
    Serial.printf("Saved to %s: +%u B, total %u B (%d records)\n",
                  path.c_str(), (unsigned)written, (unsigned)fsize, records);

    // Ротация если файл достиг лимита.
    if (fsize >= MAX_FILE_SIZE) {
        Serial.printf("File reached %u bytes (limit %u), rotating...\n",
                      (unsigned)fsize, (unsigned)MAX_FILE_SIZE);
        rotate_files();
    }
    return true;
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

// Рисуем 12 столбиков CO2 в области графика. Заполняем справа налево:
// если данных меньше 12, левая часть остаётся пустой.
static void draw_co2_bars_1h(const std::vector<Measurement>& ms) {
    const int N_BARS = 12;
    const int slot_w = GRAPH_W / N_BARS;       // 230/12 = 19
    const int bar_w  = slot_w - 3;             // 16 (3 px зазор)
    const int range  = CO2_SCALE_MAX - CO2_SCALE_MIN;

    int items = std::min(static_cast<int>(ms.size()), N_BARS);
    int start_slot = N_BARS - items;           // выравнивание вправо
    int data_start = static_cast<int>(ms.size()) - items;

    for (int i = 0; i < items; i++) {
        int v = ms[data_start + i].co2;
        if (v < CO2_SCALE_MIN) v = CO2_SCALE_MIN;
        if (v > CO2_SCALE_MAX) v = CO2_SCALE_MAX;
        int bh = (v - CO2_SCALE_MIN) * GRAPH_H / range;

        int x = GRAPH_X + (start_slot + i) * slot_w;
        int y_top = GRAPH_Y + GRAPH_H - bh;
        display.fillRect(x, y_top, bar_w, bh, GxEPD_BLACK);
    }

    // Рамка графика
    display.drawRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, GxEPD_BLACK);

    // Подписи шкалы Y (min/max)
    display.setFont(&FreeSans9pt7b);
    display.setCursor(0, GRAPH_Y + 8);
    display.printf("%d", CO2_SCALE_MAX);
    display.setCursor(0, GRAPH_BOTTOM);
    display.printf("%d", CO2_SCALE_MIN);
}

// Подписи оси X для 1-часового графика.
static void draw_x_labels_1h() {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(GRAPH_X, GRAPH_BOTTOM + 12);
    display.print("-1h");
    int now_w = 25;   // ширина "now" примерно
    display.setCursor(GRAPH_X + GRAPH_W - now_w, GRAPH_BOTTOM + 12);
    display.print("now");
}

static void draw_screen(int screen, bool valid,
                         uint16_t co2, float t, float h) {
    if (screen < 0 || screen >= N_SCREENS) screen = 0;
    int param_idx  = screen / 3;
    int period_idx = screen % 3;

    // Подгружаем историю только для тех экранов, где она нужна.
    // На Этапе 9 — только screen 0 (CO2 1h, 12 записей).
    std::vector<Measurement> history;
    if (screen == 0 && fs_ok) {
        history = read_last_n_from_file(get_current_file_path(), 12);
    }

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title — y 16..28
        display.setFont(&FreeSans9pt7b);
        display.setCursor(GRAPH_X, 26);
        display.printf("%s for %s",
                       PARAM_NAMES[param_idx], PERIOD_NAMES[period_idx]);

        if (screen == 0) {
            // Реальный график CO2 1h
            draw_co2_bars_1h(history);
            draw_x_labels_1h();
            // Текущее значение справа сверху (header пока не готов)
            display.setCursor(SCREEN_W - 80, 12);
            if (valid) display.printf("%u ppm", co2);
            else       display.print("--- ppm");
        } else {
            // Placeholder для остальных экранов (реализация — Этап 10)
            display.drawRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, GxEPD_BLACK);
            display.setCursor(GRAPH_X + 10, GRAPH_Y + GRAPH_H / 2);
            display.printf("(screen %d: stage 10)", screen);

            display.setCursor(GRAPH_X, GRAPH_BOTTOM + 12);
            if (valid) display.printf("CO2 %u  T %.1f  H %.0f%%", co2, t, h);
            else       display.print("no data yet");
        }
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
        cached_file_path[0] = '\0';
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
    init_filesystem();

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

                // Сохраняем в LittleFS (Этап 7).
                Measurement rec{};
                rec.timestamp = total_uptime_before_sleep + millis() / 1000;
                rec.co2       = co2;
                rec.temp_x10  = static_cast<int16_t>(t * 10.0f);
                rec.humidity  = static_cast<uint8_t>(h + 0.5f);
                rec.flags     = 0;
                save_measurement(rec);
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
