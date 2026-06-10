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
// Этап 14: Polish + batterу
// ============================================================
// Что доделали:
//   - VERBOSE_LOGGING управляет шумом в Serial: при 0 остаются
//     только важные сообщения (ошибки, состояния алерта, размеры
//     файлов). Для отладки можно временно поднять.
//   - Убрали технический долг — дубликат TARGET_POINTS_FWD удалён,
//     forward-declaration оставлена только для функции
//     load_recent_measurements.
//
// Дополнительно к финальному запуску:
//   - Подключить батарейный бокс (3× AA → 5V, не 3V3!),
//     отключить USB и замерить автономность (см. ТЗ).
//   - Интеграционный тест (test/test_logic/test_integration.cpp)
//     эмулирует месяц данных и прогоняет валидация → averages →
//     downsampling, проверяя что вся pipeline не падает.
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
#define MEASUREMENT_DELAY_MS    5000
#define MEASUREMENT_INTERVAL    300        // sleep между замерами, сек
#define AVERAGE_RECALC_INTERVAL 86400      // пересчёт средних раз в сутки
#define BUTTON_RELEASE_TIMEOUT  3000       // мс — сколько ждём отпускания

// --- Драйвер e-Paper ---
// На нашей панели Waveshare 2.13" работает именно BN (SSD1680).
// С B73 контроллер отрабатывает обновление, но стекло остаётся пустым.
#define EPD_DRIVER     GxEPD2_213_BN

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

// --- Идеальные значения (для линии трендов) ---
#define IDEAL_CO2         600
#define IDEAL_TEMP        22.0f
#define IDEAL_HUMIDITY    50

// --- Пороги «плохо» (для пунктирной линии на графике) ---
#define CO2_THRESHOLD_HI       1500
#define TEMP_THRESHOLD_LO      18.0f
#define TEMP_THRESHOLD_HI      26.0f
#define HUMIDITY_THRESHOLD_LO  30
#define HUMIDITY_THRESHOLD_HI  70

// --- Управление визуализацией трендов ---
#define SHOW_IDEAL_LINE      1
#define SHOW_AVERAGE_LINE    1
#define SHOW_THRESHOLD_BAD   1

// --- Отладка ---
// 1 — подробный лог в Serial (запись измерений, ротация, кэш).
// 0 — только важные сообщения (ошибки, переходы алерта).
#define VERBOSE_LOGGING      1

// Короткие обёртки для verbose-логов.
#if VERBOSE_LOGGING
  #define VLOG(...)   Serial.printf(__VA_ARGS__)
  #define VLOGLN(s)   Serial.println(s)
#else
  #define VLOG(...)   ((void)0)
  #define VLOGLN(s)   ((void)0)
#endif

// --- Пороги алерта (гистерезис, см. logic::update_alert_state) ---
#define ALERT_CO2_ON         1500
#define ALERT_CO2_OFF        1300

// --- UI ---
#define UI_ALERT_TEXT        "VENTILATE"

// --- Average cache (Этап 13) ---
#define CACHE_VERSION        1
#define CACHE_PATH           "/cache/average_cache.bin"
#define CACHE_HISTORY_DAYS   30      // сколько дней истории берём для пересчёта
#define AVG_POINTS_1H        12
#define AVG_POINTS_24H       144
#define AVG_POINTS_7D        168
#define AVG_WINDOW_1H        12      // raw measurement за 1 час
#define AVG_WINDOW_24H       288     // raw за 24 часа
#define AVG_WINDOW_7D        2016    // raw за 7 дней

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
RTC_DATA_ATTR bool     alert_active = false;
RTC_DATA_ATTR uint32_t last_average_recalc_uptime = 0;
RTC_DATA_ATTR char     cached_file_path[64] = "";

// AverageCache: точное хранилище уже отрисовываемых точек.
// uint16 для CO2 (ppm), int16 для T*10 (десятые градуса),
// uint8 для % влажности. Размер ~1.6 КБ.
struct __attribute__((packed)) AverageCache {
    uint32_t version;
    uint8_t  has_data;
    uint8_t  reserved[3];
    uint16_t avg_co2_1h [AVG_POINTS_1H];
    uint16_t avg_co2_24h[AVG_POINTS_24H];
    uint16_t avg_co2_7d [AVG_POINTS_7D];
    int16_t  avg_temp_1h [AVG_POINTS_1H];
    int16_t  avg_temp_24h[AVG_POINTS_24H];
    int16_t  avg_temp_7d [AVG_POINTS_7D];
    uint8_t  avg_humidity_1h [AVG_POINTS_1H];
    uint8_t  avg_humidity_24h[AVG_POINTS_24H];
    uint8_t  avg_humidity_7d [AVG_POINTS_7D];
};
static AverageCache g_cache{};   // сессионная переменная (не RTC)

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
        scd4x.wakeUp();
        delay(30);
        int16_t serr = scd4x.stopPeriodicMeasurement();
        if (serr) {
            Serial.println("stopPeriodicMeasurement (ignored on first boot):");
            print_scd_error("stop", serr);
        }
        delay(500);
    }

    // После powerDown датчик просыпается не мгновенно, и первый wake_up он
    // не ACK'ает. Если сразу дёрнуть getSerialNumber — получим NACK
    // ("not responding") и пропустим замер → "No data" через раз. Поэтому
    // повторяем wakeUp + проверку связи, пока датчик не ответит.
    uint64_t sn = 0;
    int16_t err = 1;
    sensor_ok = false;
    for (int attempt = 1; attempt <= 5; attempt++) {
        scd4x.wakeUp();
        delay(50);
        err = scd4x.getSerialNumber(sn);
        if (!err) {
            sensor_ok = true;
            VLOG("SCD41 OK (attempt %d). SN: 0x%012llx\n",
                 attempt, (unsigned long long)sn);
            break;
        }
        delay(50);
    }
    if (!sensor_ok) {
        Serial.println("ERROR: SCD41 not responding after retries.");
        print_scd_error("getSerialNumber", err);
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
    VLOG("LittleFS: %u / %u bytes used\n",
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
    VLOG("Current file resolved: %s\n", cached_file_path);
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

// Forward decls — реальные определения дальше в файле.
static std::vector<Measurement> load_recent_measurements(int n);

// Точки на графике для каждого периода: 12 / 144 / 168.
static const int TARGET_POINTS[3] = { 12, 144, 168 };

// ------------------------------------------------------------
// Average cache: загрузка / сохранение / пересчёт (Этап 13)
// ------------------------------------------------------------

static bool load_average_cache() {
    if (!fs_ok) return false;
    if (!LittleFS.exists(CACHE_PATH)) return false;
    File f = LittleFS.open(CACHE_PATH, "r");
    if (!f) return false;
    if (f.size() != sizeof(AverageCache)) {
        Serial.printf("Cache size mismatch (%u vs %u), ignoring\n",
                      (unsigned)f.size(), (unsigned)sizeof(AverageCache));
        f.close();
        return false;
    }
    size_t got = f.read(reinterpret_cast<uint8_t*>(&g_cache), sizeof(g_cache));
    f.close();
    if (got != sizeof(g_cache)) return false;
    if (g_cache.version != CACHE_VERSION) {
        Serial.printf("Cache version mismatch (%u vs %u), ignoring\n",
                      g_cache.version, CACHE_VERSION);
        g_cache = AverageCache{};
        return false;
    }
    VLOG("Loaded average cache (has_data=%u)\n", g_cache.has_data);
    return true;
}

static bool save_average_cache() {
    if (!fs_ok) return false;
    if (!LittleFS.exists("/cache")) LittleFS.mkdir("/cache");
    File f = LittleFS.open(CACHE_PATH, "w", /*create=*/true);
    if (!f) {
        Serial.println("save_average_cache: open failed");
        return false;
    }
    size_t w = f.write(reinterpret_cast<const uint8_t*>(&g_cache), sizeof(g_cache));
    f.close();
    VLOG("Saved cache: %u bytes\n", (unsigned)w);
    return w == sizeof(g_cache);
}

static void recalculate_averages() {
    if (!fs_ok) return;

    Serial.println("Recalculating averages...");
    uint32_t t0 = millis();

    // 30 дней истории = 30 × 288 = 8640 записей при 5-мин интервале.
    auto history = load_recent_measurements(CACHE_HISTORY_DAYS * 288);
    if (history.empty()) {
        Serial.println("No history yet, skipping recalc");
        return;
    }
    Serial.printf("Loaded %u records in %u ms\n",
                  (unsigned)history.size(), (unsigned)(millis() - t0));

    std::vector<float> co2_h, t_h, h_h;
    co2_h.reserve(history.size());
    t_h.reserve(history.size());
    h_h.reserve(history.size());
    for (const auto& m : history) {
        co2_h.push_back(static_cast<float>(m.co2));
        t_h.push_back(m.temp_x10 / 10.0f);
        h_h.push_back(static_cast<float>(m.humidity));
    }

    auto co2_1h  = calculate_window_average(co2_h, AVG_WINDOW_1H,  AVG_POINTS_1H);
    auto co2_24h = calculate_window_average(co2_h, AVG_WINDOW_24H, AVG_POINTS_24H);
    auto co2_7d  = calculate_window_average(co2_h, AVG_WINDOW_7D,  AVG_POINTS_7D);
    auto t_1h    = calculate_window_average(t_h,   AVG_WINDOW_1H,  AVG_POINTS_1H);
    auto t_24h   = calculate_window_average(t_h,   AVG_WINDOW_24H, AVG_POINTS_24H);
    auto t_7d    = calculate_window_average(t_h,   AVG_WINDOW_7D,  AVG_POINTS_7D);
    auto h_1h    = calculate_window_average(h_h,   AVG_WINDOW_1H,  AVG_POINTS_1H);
    auto h_24h   = calculate_window_average(h_h,   AVG_WINDOW_24H, AVG_POINTS_24H);
    auto h_7d    = calculate_window_average(h_h,   AVG_WINDOW_7D,  AVG_POINTS_7D);

    g_cache.version  = CACHE_VERSION;
    g_cache.has_data = 1;
    for (int i = 0; i < AVG_POINTS_1H;  i++) g_cache.avg_co2_1h [i] = static_cast<uint16_t>(co2_1h[i]);
    for (int i = 0; i < AVG_POINTS_24H; i++) g_cache.avg_co2_24h[i] = static_cast<uint16_t>(co2_24h[i]);
    for (int i = 0; i < AVG_POINTS_7D;  i++) g_cache.avg_co2_7d [i] = static_cast<uint16_t>(co2_7d[i]);
    for (int i = 0; i < AVG_POINTS_1H;  i++) g_cache.avg_temp_1h [i] = static_cast<int16_t>(t_1h[i] * 10.0f);
    for (int i = 0; i < AVG_POINTS_24H; i++) g_cache.avg_temp_24h[i] = static_cast<int16_t>(t_24h[i] * 10.0f);
    for (int i = 0; i < AVG_POINTS_7D;  i++) g_cache.avg_temp_7d [i] = static_cast<int16_t>(t_7d[i] * 10.0f);
    for (int i = 0; i < AVG_POINTS_1H;  i++) g_cache.avg_humidity_1h [i] = static_cast<uint8_t>(h_1h[i]);
    for (int i = 0; i < AVG_POINTS_24H; i++) g_cache.avg_humidity_24h[i] = static_cast<uint8_t>(h_24h[i]);
    for (int i = 0; i < AVG_POINTS_7D;  i++) g_cache.avg_humidity_7d [i] = static_cast<uint8_t>(h_7d[i]);

    save_average_cache();
    Serial.printf("Average recalc done in %u ms total\n", (unsigned)(millis() - t0));
}

// Возвращает массив средних для текущего (param, period) экрана,
// готовый к отрисовке. Если кэш пуст — пустой vector.
static std::vector<float> get_average_for_screen(int param_idx, int period_idx) {
    if (!g_cache.has_data) return {};
    int target = TARGET_POINTS[period_idx];
    std::vector<float> result(target, 0.0f);

    if (param_idx == 0) {
        const uint16_t* src = (period_idx == 0) ? g_cache.avg_co2_1h :
                              (period_idx == 1) ? g_cache.avg_co2_24h :
                                                  g_cache.avg_co2_7d;
        for (int i = 0; i < target; i++) result[i] = static_cast<float>(src[i]);
    } else if (param_idx == 1) {
        const int16_t* src = (period_idx == 0) ? g_cache.avg_temp_1h :
                             (period_idx == 1) ? g_cache.avg_temp_24h :
                                                 g_cache.avg_temp_7d;
        for (int i = 0; i < target; i++) result[i] = src[i] / 10.0f;
    } else {
        const uint8_t* src = (period_idx == 0) ? g_cache.avg_humidity_1h :
                             (period_idx == 1) ? g_cache.avg_humidity_24h :
                                                 g_cache.avg_humidity_7d;
        for (int i = 0; i < target; i++) result[i] = static_cast<float>(src[i]);
    }
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
    VLOG("Saved to %s: +%u B, total %u B (%d records)\n",
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
    neopixelWrite(LED_PIN, 0, LED_LEVEL, 0);  // зелёный во время замера

    // Первый single-shot после пробуждения датчика часто приходит пустым
    // (CO2=0, «кондиционирующий»). Повторяем до 3 раз, пока не получим
    // валидный замер.
    const int MAX_ATTEMPTS = 3;
    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        int16_t err = scd4x.measureSingleShot();
        if (err) {
            print_scd_error("measureSingleShot", err);
            continue;
        }

        delay(MEASUREMENT_DELAY_MS);

        err = scd4x.readMeasurement(co2, t, h);
        if (err) {
            print_scd_error("readMeasurement", err);
            continue;
        }
        if (co2 == 0) {
            VLOG("CO2=0 (not ready), attempt %d/%d\n", attempt, MAX_ATTEMPTS);
            continue;
        }

        neopixelWrite(LED_PIN, 0, 0, 0);
        return true;
    }

    neopixelWrite(LED_PIN, 0, 0, 0);
    return false;
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

// ------------------------------------------------------------
// Тренды для графика: какие линии накладывать (Этап 11)
// ------------------------------------------------------------
struct ParamLines {
    float ideal;          // желаемое значение
    float threshold_lo;   // граница «плохо» снизу, <0 = нет
    float threshold_hi;   // граница «плохо» сверху, <0 = нет
};

static ParamLines param_lines(int param_idx) {
    switch (param_idx) {
        case 0: return { static_cast<float>(IDEAL_CO2),
                         -1.0f,
                         static_cast<float>(CO2_THRESHOLD_HI) };
        case 1: return { IDEAL_TEMP, TEMP_THRESHOLD_LO, TEMP_THRESHOLD_HI };
        case 2: return { static_cast<float>(IDEAL_HUMIDITY),
                         static_cast<float>(HUMIDITY_THRESHOLD_LO),
                         static_cast<float>(HUMIDITY_THRESHOLD_HI) };
        default: return { -1.0f, -1.0f, -1.0f };
    }
}

// Сколько raw-измерений нужно для каждого периода (при шаге 5 мин).
static const int RECORDS_FOR_PERIOD[3] = { 12, 288, 2016 };
// TARGET_POINTS объявлено выше (нужно cache-функциям тоже).

struct Scale { int lo; int hi; };

static Scale param_scale(int param_idx) {
    switch (param_idx) {
        case 0: return { CO2_SCALE_MIN,      CO2_SCALE_MAX      };
        case 1: return { TEMP_SCALE_MIN,     TEMP_SCALE_MAX     };
        case 2: return { HUMIDITY_SCALE_MIN, HUMIDITY_SCALE_MAX };
        default: return { 0, 100 };
    }
}

static int value_to_y(float v, int lo, int hi) {
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    int bh = static_cast<int>((v - lo) * GRAPH_H / (hi - lo));
    return GRAPH_Y + GRAPH_H - bh;
}

// Достаём из вектора измерений нужный показатель как float.
static std::vector<float> extract_param_values(
        const std::vector<Measurement>& ms, int param_idx) {
    std::vector<float> out;
    out.reserve(ms.size());
    for (const auto& m : ms) {
        switch (param_idx) {
            case 0: out.push_back(static_cast<float>(m.co2)); break;
            case 1: out.push_back(m.temp_x10 / 10.0f); break;
            case 2: out.push_back(static_cast<float>(m.humidity)); break;
            default: out.push_back(0.0f);
        }
    }
    return out;
}

// Тянем N последних записей из всех файлов, от свежего к старому.
// Результат — в хронологическом порядке (старшие сзади).
static std::vector<Measurement> load_recent_measurements(int n) {
    std::vector<Measurement> result;
    if (!fs_ok || n <= 0) return result;

    auto nums = list_data_file_numbers();
    if (nums.empty()) return result;

    for (auto it = nums.rbegin(); it != nums.rend(); ++it) {
        int need = n - static_cast<int>(result.size());
        if (need <= 0) break;
        String path(make_filename(*it).c_str());
        auto chunk = read_last_n_from_file(path, need);
        // chunk уже в хронологическом порядке; вставляем в начало.
        result.insert(result.begin(), chunk.begin(), chunk.end());
    }
    return result;
}

// --- Три отрисовщика, общая семантика «справа налево» ---

static void draw_bars(const std::vector<float>& values, int n_slots,
                       int lo, int hi) {
    if (hi <= lo) return;
    int slot_w = GRAPH_W / n_slots;
    int bar_w  = slot_w - 3;
    if (bar_w < 1) bar_w = 1;

    int items = std::min(static_cast<int>(values.size()), n_slots);
    int start_slot = n_slots - items;
    int data_start = static_cast<int>(values.size()) - items;

    for (int i = 0; i < items; i++) {
        int y_top = value_to_y(values[data_start + i], lo, hi);
        int bh    = GRAPH_Y + GRAPH_H - y_top;
        int x     = GRAPH_X + (start_slot + i) * slot_w;
        if (bh > 0) display.fillRect(x, y_top, bar_w, bh, GxEPD_BLACK);
    }
}

// Filled area: каждая точка — вертикальная линия от низа графика
// до y(value). Выравниваем справа.
static void draw_area(const std::vector<float>& values, int lo, int hi) {
    if (hi <= lo) return;
    int items = std::min(static_cast<int>(values.size()), GRAPH_W);
    if (items <= 0) return;
    int data_start = static_cast<int>(values.size()) - items;
    int x0 = GRAPH_X + GRAPH_W - items;

    for (int i = 0; i < items; i++) {
        int y_top = value_to_y(values[data_start + i], lo, hi);
        int x = x0 + i;
        // Вертикальная линия от baseline (низ графика) до y_top.
        display.drawLine(x, GRAPH_BOTTOM - 1, x, y_top, GxEPD_BLACK);
    }
}

// Точечный пунктир для линии средних: каждый 3-й пиксель.
static void draw_dotted_line(const std::vector<float>& values, int lo, int hi) {
    if (hi <= lo) return;
    int items = std::min(static_cast<int>(values.size()), GRAPH_W);
    if (items < 2) return;
    int data_start = static_cast<int>(values.size()) - items;
    int x0 = GRAPH_X + GRAPH_W - items;

    for (int i = 0; i < items; i += 3) {
        int y = value_to_y(values[data_start + i], lo, hi);
        int x = x0 + i;
        display.drawPixel(x, y, GxEPD_BLACK);
    }
}

// Линейный график: соединяем последовательные точки.
static void draw_line(const std::vector<float>& values, int lo, int hi) {
    if (hi <= lo) return;
    int items = std::min(static_cast<int>(values.size()), GRAPH_W);
    if (items < 2) return;
    int data_start = static_cast<int>(values.size()) - items;
    int x0 = GRAPH_X + GRAPH_W - items;

    int prev_x = -1, prev_y = -1;
    for (int i = 0; i < items; i++) {
        int y = value_to_y(values[data_start + i], lo, hi);
        int x = x0 + i;
        if (prev_x >= 0) display.drawLine(prev_x, prev_y, x, y, GxEPD_BLACK);
        prev_x = x;
        prev_y = y;
    }
}

// Горизонтальная линия на уровне value (если попадает в шкалу).
// dash_len = 0 — сплошная; >0 — штрихи длиной dash_len с равным промежутком.
static void draw_horizontal_value(float v, int lo, int hi, int dash_len) {
    if (v < lo || v > hi) return;
    int y = value_to_y(v, lo, hi);
    if (dash_len <= 0) {
        display.drawLine(GRAPH_X, y, GRAPH_X + GRAPH_W - 1, y, GxEPD_BLACK);
        return;
    }
    int step = dash_len * 2;
    for (int x = GRAPH_X; x < GRAPH_X + GRAPH_W; x += step) {
        int x_end = std::min(x + dash_len - 1, GRAPH_X + GRAPH_W - 1);
        display.drawLine(x, y, x_end, y, GxEPD_BLACK);
    }
}

// Один светофор: 3 точки диаметром 4 px (radius 2), шаг 6.
// level: 0 = плохо (1 закрашена), 1 = терпимо (2), 2 = хорошо (3).
// level = -1 — все пустые (нет валидных данных).
static void draw_traffic_light(int x, int y, int level) {
    int filled = (level < 0) ? 0 : level + 1;
    for (int i = 0; i < 3; i++) {
        int dot_x = x + i * 6;
        if (i < filled) display.fillCircle(dot_x, y, 2, GxEPD_BLACK);
        else            display.drawCircle(dot_x, y, 2, GxEPD_BLACK);
    }
}

// Header (y 0..15): значения CO2 / T / H слева, три светофора справа.
static void draw_header(bool valid, uint16_t co2, float t, float h) {
    display.setFont(&FreeSans9pt7b);

    if (valid) {
        display.setCursor(2, 12);
        display.printf("%u  %.1f  %d", co2, t, static_cast<int>(h + 0.5f));
    } else {
        display.setCursor(2, 12);
        display.print("---  ---  ---");
    }

    int co2_lvl = valid ? get_co2_traffic_level(co2)            : -1;
    int t_lvl   = valid ? get_temp_traffic_level(t)             : -1;
    int h_lvl   = valid ? get_humidity_traffic_level(
                              static_cast<int>(h + 0.5f))       : -1;

    int tl_y = 8;
    int tl_x = SCREEN_W - 70;
    draw_traffic_light(tl_x,      tl_y, co2_lvl);
    draw_traffic_light(tl_x + 24, tl_y, t_lvl);
    draw_traffic_light(tl_x + 48, tl_y, h_lvl);
}

// Подписи Y-оси (min/max) — слева, левее GRAPH_X.
static void draw_y_labels(int lo, int hi) {
    display.setFont(&FreeSans9pt7b);
    display.setCursor(0, GRAPH_Y + 8);
    display.printf("%d", hi);
    display.setCursor(0, GRAPH_BOTTOM);
    display.printf("%d", lo);
}

static void draw_x_labels(int period_idx) {
    // Встроенный шрифт 5×7 px: cursor задаёт ВЕРХ текста (не baseline).
    // Освобождаем y 106+ под алерт.
    display.setFont();
    int y = GRAPH_BOTTOM + 1;          // y 97 (text занимает 97..104)
    const char* left =
        (period_idx == 0) ? "-1h" :
        (period_idx == 1) ? "-24h" : "-7d";
    display.setCursor(GRAPH_X, y);
    display.print(left);
    display.setCursor(GRAPH_X + GRAPH_W - 20, y);
    display.print("now");
}

// Алерт «VENTILATE» в полосе y 106..121, по центру, жирный 12pt.
// Показывается на ВСЕХ 9 экранах когда alert_active == true.
static void draw_alert_if_needed() {
    if (!alert_active) return;
    display.setFont(&FreeSansBold12pt7b);
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(UI_ALERT_TEXT, 0, 0, &x1, &y1, &w, &h);
    int x = (SCREEN_W - static_cast<int>(w)) / 2;
    int y = 119;   // baseline вблизи нижнего края
    display.setCursor(x, y);
    display.print(UI_ALERT_TEXT);
}

// Главная функция — рисует один из 9 экранов.
static void draw_screen(int screen, bool valid,
                         uint16_t co2, float t, float h) {
    if (screen < 0 || screen >= N_SCREENS) screen = 0;
    int param_idx  = screen / 3;
    int period_idx = screen % 3;
    Scale s = param_scale(param_idx);

    // Подгружаем данные нужного объёма и переводим в нужный показатель.
    std::vector<float> raw;
    if (fs_ok) {
        int need = RECORDS_FOR_PERIOD[period_idx];
        auto ms = (need <= 12)
            ? read_last_n_from_file(get_current_file_path(), need)
            : load_recent_measurements(need);
        raw = extract_param_values(ms, param_idx);
    }

    // Downsample только если данных больше, чем целевое число точек.
    // Иначе используем raw как есть (партиальный график справа).
    int target = TARGET_POINTS[period_idx];
    std::vector<float> for_plot =
        (static_cast<int>(raw.size()) > target)
            ? downsample(raw, target)
            : raw;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Header — значения и светофоры
        draw_header(valid, co2, t, h);

        // Title
        display.setFont(&FreeSans9pt7b);
        display.setCursor(GRAPH_X, 26);
        display.printf("%s for %s",
                       PARAM_NAMES[param_idx], PERIOD_NAMES[period_idx]);

        // Граф
        switch (period_idx) {
            case 0: draw_bars(for_plot, TARGET_POINTS[0], s.lo, s.hi); break;
            case 1: draw_area(for_plot, s.lo, s.hi); break;
            case 2: draw_line(for_plot, s.lo, s.hi); break;
        }

        // Линии трендов поверх данных
        ParamLines pl = param_lines(param_idx);
        #if SHOW_IDEAL_LINE
        if (pl.ideal >= s.lo && pl.ideal <= s.hi) {
            draw_horizontal_value(pl.ideal, s.lo, s.hi, 0);   // сплошная
        }
        #endif
        #if SHOW_THRESHOLD_BAD
        if (pl.threshold_hi >= 0.0f) {
            draw_horizontal_value(pl.threshold_hi, s.lo, s.hi, 8);  // длинный пунктир
        }
        if (pl.threshold_lo >= 0.0f) {
            draw_horizontal_value(pl.threshold_lo, s.lo, s.hi, 8);
        }
        #endif

        // Линия среднего из кэша (Этап 13) — точечный пунктир.
        #if SHOW_AVERAGE_LINE
        if (g_cache.has_data) {
            auto avg = get_average_for_screen(param_idx, period_idx);
            if (!avg.empty()) {
                draw_dotted_line(avg, s.lo, s.hi);
            }
        }
        #endif

        display.drawRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, GxEPD_BLACK);
        draw_y_labels(s.lo, s.hi);
        draw_x_labels(period_idx);

        draw_alert_if_needed();
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

    // КРИТИЧНО: по умолчанию домен RTC_PERIPH в deep sleep выключается, и
    // внутренняя подтяжка кнопочных пинов "умирает" → GPIO всплывает, ext1
    // (ANY_LOW) ловит LOW и будит прибор каждые пару секунд без нажатия.
    // Держим домен включённым, чтобы pull-up жил во сне.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

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
        alert_active = false;
        last_average_recalc_uptime = 0;
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
    load_average_cache();   // если файла нет — g_cache остаётся нулевым

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
                Serial.printf("CO2: %u ppm, T: %.1f C, H: %.0f%%\n", co2, t, h);

                // Обновляем алерт с гистерезисом (Этап 12).
                bool prev_alert = alert_active;
                alert_active = update_alert_state(alert_active, co2);
                if (alert_active != prev_alert) {
                    Serial.printf("ALERT state changed: %s -> %s\n",
                                  prev_alert    ? "ON" : "OFF",
                                  alert_active  ? "ON" : "OFF");
                }

                // Сохраняем в LittleFS (Этап 7).
                Measurement rec{};
                uint32_t now_uptime = total_uptime_before_sleep + millis() / 1000;
                rec.timestamp = now_uptime;
                rec.co2       = co2;
                rec.temp_x10  = static_cast<int16_t>(t * 10.0f);
                rec.humidity  = static_cast<uint8_t>(h + 0.5f);
                rec.flags     = 0;
                save_measurement(rec);

                // Пересчёт средних — раз в сутки (Этап 13).
                if (now_uptime - last_average_recalc_uptime >= AVERAGE_RECALC_INTERVAL) {
                    recalculate_averages();
                    last_average_recalc_uptime = now_uptime;
                }
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
