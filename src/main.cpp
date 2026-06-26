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
// В корпусе осталась ОДНА кнопка — на GPIO 5. Она листает экраны по кругу.
// GPIO 4 (бывшая вторая кнопка) физически не подключён — не используем.
#define PIN_BUTTON_PARAM   4    // RTC GPIO 4 — не используется (кнопка снята)
#define PIN_BUTTON_PERIOD  5    // RTC GPIO 5 — единственная кнопка (листание)
#define PIN_BUTTON         PIN_BUTTON_PERIOD
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
#define MEASUREMENT_INTERVAL    900        // sleep между замерами, сек (15 мин — экономия батареи)

// --- Калибровка температуры (SCD41) ---
// ВАЖНО: датчик хранит "temperature offset", который ВЫЧИТАЕТСЯ из показания,
// чтобы скомпенсировать саморазогрев. Заводское значение = 4.0 °C и рассчитано
// на НЕПРЕРЫВНЫЙ режим, где чип греется ~на 4°. Мы же меряем в single-shot:
// датчик почти всё время выключен и не греется → вычитать 4° нельзя, иначе
// температура стабильно занижена на ~4° (и «гуляет» из-за разогрева во время
// самого замера). Поэтому для single-shot ставим офсет близкий к нулю.
// setTemperatureOffset() пишет только в RAM датчика (без износа EEPROM), а при
// powerDown настройка сбрасывается — поэтому задаём её КАЖДЫЙ цикл перед замером.
// После сборки сравни показание с отдельным термометром и подкрути это число
// (если прибор показывает на X° меньше реального — УМЕНЬШИ офсет на X).
#define TEMPERATURE_OFFSET      0.0f
#define AVERAGE_RECALC_INTERVAL 86400      // пересчёт средних раз в сутки
#define BUTTON_RELEASE_TIMEOUT  3000       // мс — сколько ждём отпускания

// --- Экономия экрана (e-Paper держит картинку даром; full refresh дорогой) ---
// По таймеру перерисовываем только если значение заметно изменилось, либо
// прошёл FULL_REFRESH_INTERVAL (от «теней»), либо сменился алерт.
#define FULL_REFRESH_INTERVAL   3600       // сек — не реже раза в час полный refresh
#define REDRAW_CO2_DELTA        30         // ppm
#define REDRAW_TEMP_DELTA_X10   3          // 0.3 °C (в десятых)
#define REDRAW_HUM_DELTA        2          // %

// --- Драйвер e-Paper ---
// На нашей панели Waveshare 2.13" работает именно BN (SSD1680).
// С B73 контроллер отрабатывает обновление, но стекло остаётся пустым.
#define EPD_DRIVER     GxEPD2_213_BN

#define RTC_MAGIC      0xDEADBEEF
// Новый UI: 4 экрана, всё про CO2.
//   0 — MAIN (крупное текущее значение + статус)
//   1 — график CO2 за 1 час
//   2 — график CO2 за 24 часа
//   3 — график CO2 за 7 дней
// Одна кнопка (GPIO5) листает экраны по кругу. По таймеру всегда
// возвращаемся на MAIN.
#define N_SCREENS      4
#define SCREEN_MAIN    0

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

// --- Геометрия экрана (после setRotation — 250×122) ---
// Поворот панели. 1 = «обычная» альбомная ориентация, 3 = перевёрнутая на 180°.
// В собранном корпусе экран установлен ВВЕРХ НОГАМИ, поэтому 3 (картинка
// переворачивается, все координаты ниже остаются прежними — GFX сам
// трансформирует). Если соберёшь иначе — поставь 1.
#define EPD_ROTATION   3
#define SCREEN_W       250
#define SCREEN_H       122

// Поля (отступы от краёв) — специально с запасом, чтобы у физического
// края рамки экрана ничего не «срезалось» и смотреть было удобно.
#define PAD_L          26        // слева: место под подписи оси Y
#define PAD_R          12        // справа
#define PAD_T          20        // сверху: строка заголовка
#define PAD_B          16        // снизу: подписи оси X

// Область графика.
#define PLOT_X         PAD_L
#define PLOT_Y         PAD_T
#define PLOT_W         (SCREEN_W - PAD_L - PAD_R)   // 212
#define PLOT_H         (SCREEN_H - PAD_T - PAD_B)   // 86
#define PLOT_B         (PLOT_Y + PLOT_H)            // 106 — низ графика (baseline)

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

// Что сейчас нарисовано на экране — чтобы по таймеру не обновлять зря.
RTC_DATA_ATTR uint32_t last_full_refresh_uptime = 0;
RTC_DATA_ATTR bool     last_shown_valid = false;
RTC_DATA_ATTR uint16_t last_shown_co2   = 0;
RTC_DATA_ATTR float    last_shown_t     = 0.0f;
RTC_DATA_ATTR float    last_shown_h     = 0.0f;
RTC_DATA_ATTR bool     last_shown_alert = false;
RTC_DATA_ATTR int      last_shown_screen = -1;   // какой экран сейчас на стекле

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
    display.setRotation(EPD_ROTATION);
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

        // Ждём готовности замера (~5 сек) в LIGHT SLEEP — ESP ~1-2 мА вместо
        // ~40 мА (главная экономия). Чтобы light sleep не ломал ext1 по кнопке,
        // go_to_sleep перед deep sleep сбрасывает источники пробуждения и
        // снимает возможный hold с кнопочных пинов.
        Serial.flush();
        esp_sleep_enable_timer_wakeup(
            static_cast<uint64_t>(MEASUREMENT_DELAY_MS) * 1000ULL);
        esp_light_sleep_start();

        err = scd4x.readMeasurement(co2, t, h);
        if (err) {
            print_scd_error("readMeasurement", err);
            continue;
        }
        if (co2 == 0) {
            VLOG("CO2=0 (not ready), attempt %d/%d\n", attempt, MAX_ATTEMPTS);
            continue;
        }

        return true;
    }

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
// Данные и шкала для графиков CO2 (новый UI)
// ------------------------------------------------------------

// Окно по времени для каждого периода (сек) и подписи.
static const long  GRAPH_WINDOW_SEC[3] = { 3600L, 86400L, 604800L };
static const char* GRAPH_PERIOD_LBL[3] = { "1h", "24h", "7d" };
static const char* GRAPH_XLEFT_LBL[3]  = { "-1h", "-24h", "-7d" };

// Достаём только CO2 как float, в хронологическом порядке.
static std::vector<float> extract_co2_values(const std::vector<Measurement>& ms) {
    std::vector<float> out;
    out.reserve(ms.size());
    for (const auto& m : ms) out.push_back(static_cast<float>(m.co2));
    return out;
}

// Значение CO2 → координата Y внутри области графика (с насыщением).
static int co2_value_to_y(float v, int lo, int hi) {
    if (hi <= lo) return PLOT_B;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    int bh = static_cast<int>((v - lo) * (long)PLOT_H / (hi - lo));
    return PLOT_B - bh;
}

// Слово-статус для главного экрана.
static const char* co2_status_word(uint16_t co2) {
    switch (get_co2_traffic_level(co2)) {
        case 2:  return "GOOD";
        case 1:  return "FAIR";
        default: return "POOR";
    }
}

// Авто-шкала Y: низ фиксируем на 400 (CO2 в помещении ниже уличного ~400
// почти не падает), верх подстраиваем под максимум данных, округляя вверх
// до сотни, но не ниже 1000. Так кривая занимает всю высоту, а нулевая
// линия (низ) остаётся осмысленной точкой отсчёта.
static void co2_auto_scale(const std::vector<float>& vals, int& lo, int& hi) {
    lo = 400;
    float dmax = 0.0f;
    for (float v : vals) if (v > dmax) dmax = v;
    int top = 1000;
    if (dmax + 50.0f > (float)top) {
        top = (int)((dmax + 50.0f) / 100.0f + 0.999f) * 100;  // округление вверх до 100
    }
    if (top > 5000) top = 5000;
    if (top <= lo + 100) top = lo + 100;
    hi = top;
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

// Печать строки по центру по X на заданной baseline-Y текущим шрифтом/размером.
static void print_centered(const char* s, int baseline_y) {
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    int x = (SCREEN_W - static_cast<int>(w)) / 2 - x1;
    display.setCursor(x, baseline_y);
    display.print(s);
}

// Пунктирная горизонтальная линия-ориентир на уровне value (если в шкале).
static void draw_dashed_h(float v, int lo, int hi, int dash) {
    if (hi <= lo || v < lo || v > hi) return;
    int y = co2_value_to_y(v, lo, hi);
    int step = dash * 2;
    for (int x = PLOT_X; x < PLOT_X + PLOT_W; x += step) {
        int xe = x + dash - 1;
        if (xe > PLOT_X + PLOT_W - 1) xe = PLOT_X + PLOT_W - 1;
        display.drawLine(x, y, xe, y, GxEPD_BLACK);
    }
}

// Кривая CO2. Данные растягиваются на ВСЮ ширину графика, если их хватает
// на полное окно (cap записей). Если меньше — линия прижата к «now» справа,
// слева честно пусто (история ещё копится). Это чинит старый баг, где 144
// точки занимали лишь ~60% ширины и слева всегда оставалась пустота.
static void draw_co2_curve(const std::vector<float>& vals, int cap, int lo, int hi) {
    int m = static_cast<int>(vals.size());
    if (m < 1 || cap < 2) return;
    int x_right = PLOT_X + PLOT_W - 1;
    float px_per_rec = (float)(PLOT_W - 1) / (float)(cap - 1);  // пикселей на запись
    int x_old = x_right - (int)((m - 1) * px_per_rec + 0.5f);
    if (x_old < PLOT_X) x_old = PLOT_X;

    int prev_x = -1, prev_y = -1;
    for (int x = x_old; x <= x_right; x++) {
        // Обратное отображение X → дробный индекс в vals (0 = newest справа).
        float age = (float)(x_right - x) / px_per_rec;
        float idx = (float)(m - 1) - age;
        if (idx < 0.0f) idx = 0.0f;
        if (idx > (float)(m - 1)) idx = (float)(m - 1);
        int i0 = (int)idx;
        int i1 = (i0 + 1 < m) ? i0 + 1 : i0;
        float f = idx - (float)i0;
        float v = vals[i0] * (1.0f - f) + vals[i1] * f;
        int y = co2_value_to_y(v, lo, hi);
        if (prev_x >= 0) display.drawLine(prev_x, prev_y, x, y, GxEPD_BLACK);
        prev_x = x; prev_y = y;
    }
}

// ===== Экран 0: MAIN — крупное текущее значение CO2 =====
static void draw_main(bool valid, uint16_t co2) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // Верхняя строка: "CO2" слева, светофор справа.
        display.setFont(&FreeSans9pt7b);
        display.setTextSize(1);
        display.setCursor(PAD_L, 17);
        display.print("CO2");
        int lvl = valid ? get_co2_traffic_level(co2) : -1;
        draw_traffic_light(SCREEN_W - PAD_R - 14, 11, lvl);

        // Крупное число по центру.
        char num[8];
        if (valid) snprintf(num, sizeof(num), "%u", co2);
        else       snprintf(num, sizeof(num), "--");
        display.setFont(&FreeSansBold12pt7b);
        display.setTextSize(2);
        print_centered(num, 74);

        // "ppm" под числом.
        display.setFont(&FreeSans9pt7b);
        display.setTextSize(1);
        print_centered("ppm", 92);

        // Нижняя строка: статус, либо инверсная плашка VENTILATE при алерте.
        if (alert_active) {
            display.fillRect(0, 100, SCREEN_W, SCREEN_H - 100, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            display.setFont(&FreeSans9pt7b);
            print_centered(UI_ALERT_TEXT, 116);
            display.setTextColor(GxEPD_BLACK);
        } else {
            display.setFont(&FreeSans9pt7b);
            print_centered(valid ? co2_status_word(co2) : "NO DATA", 116);
        }
    } while (display.nextPage());
    display.hibernate();
}

// ===== Экраны 1..3: график CO2 за 1h / 24h / 7d =====
static void draw_graph(int period_idx, bool valid, uint16_t co2) {
    if (period_idx < 0 || period_idx > 2) period_idx = 0;

    // Сколько записей нужно на окно (зависит от реального интервала замера).
    int cap = (int)(GRAPH_WINDOW_SEC[period_idx] / (long)MEASUREMENT_INTERVAL);
    if (cap < 2) cap = 2;

    std::vector<float> vals;
    if (fs_ok) {
        auto ms = load_recent_measurements(cap);
        vals = extract_co2_values(ms);
    }

    int lo, hi;
    co2_auto_scale(vals, lo, hi);

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);

        // --- Заголовок ---
        if (alert_active) {
            // Алерт виден на любом экране: инверсная плашка вверху.
            display.fillRect(0, 0, SCREEN_W, PAD_T - 2, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            display.setFont(&FreeSans9pt7b);
            print_centered(UI_ALERT_TEXT, 14);
            display.setTextColor(GxEPD_BLACK);
        } else {
            display.setFont(&FreeSans9pt7b);
            display.setCursor(PAD_L, 14);
            display.printf("CO2  %s", GRAPH_PERIOD_LBL[period_idx]);
            if (valid) {
                char cur[12];
                snprintf(cur, sizeof(cur), "%u ppm", co2);
                int16_t x1, y1; uint16_t w, h;
                display.getTextBounds(cur, 0, 0, &x1, &y1, &w, &h);
                display.setCursor(SCREEN_W - PAD_R - (int)w - x1, 14);
                display.print(cur);
            }
        }

        // --- Оси (L-образные: слева и снизу) ---
        display.drawLine(PLOT_X, PLOT_Y, PLOT_X, PLOT_B, GxEPD_BLACK);
        display.drawLine(PLOT_X, PLOT_B, PLOT_X + PLOT_W - 1, PLOT_B, GxEPD_BLACK);

        // --- Подписи Y (верх/низ шкалы), мелкий встроенный шрифт ---
        display.setFont();
        display.setCursor(0, PLOT_Y - 2);
        display.printf("%d", hi);
        display.setCursor(0, PLOT_B - 6);
        display.printf("%d", lo);

        // --- Ориентир: порог VENTILATE (если попадает в шкалу) ---
        draw_dashed_h((float)ALERT_CO2_ON, lo, hi, 4);

        // --- Кривая данных ---
        if (!vals.empty()) {
            draw_co2_curve(vals, cap, lo, hi);
        } else {
            display.setFont();
            display.setCursor(PLOT_X + 24, PLOT_Y + PLOT_H / 2);
            display.print("collecting data...");
        }

        // --- Подписи X ---
        display.setFont();
        display.setCursor(PLOT_X, PLOT_B + 4);
        display.print(GRAPH_XLEFT_LBL[period_idx]);
        display.setCursor(PLOT_X + PLOT_W - 18, PLOT_B + 4);
        display.print("now");
    } while (display.nextPage());
    display.hibernate();
}

// Диспетчер: какой из 4 экранов рисовать.
static void draw_current_screen(int screen, bool valid, uint16_t co2) {
    if (screen < 0 || screen >= N_SCREENS) screen = SCREEN_MAIN;
    if (screen == SCREEN_MAIN) draw_main(valid, co2);
    else                       draw_graph(screen - 1, valid, co2);
}

static void wait_for_button_release() {
    // Одна кнопка (GPIO5). Ждём, пока её отпустят, иначе ESP-IDF проснётся
    // снова на той же удерживаемой кнопке — бесконечный цикл.
    unsigned long start = millis();
    while (digitalRead(PIN_BUTTON) == LOW) {
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

    // После light sleep на фазе замера подсистема сна/пины могли остаться в
    // состоянии, в котором ext1 по кнопке не срабатывал. Чистим: сбрасываем
    // все источники пробуждения и снимаем возможный hold с кнопочных пинов
    // перед тем, как заново арм-ить таймер и ext1.
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    rtc_gpio_hold_dis(static_cast<gpio_num_t>(PIN_BUTTON));

    // --- Timer wake (через MEASUREMENT_INTERVAL сек) ---
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(MEASUREMENT_INTERVAL) * 1000000ULL);

    // --- Button wake (ext1, ANY_LOW) — одна кнопка на GPIO 5 ---
    // GPIO 5 — RTC GPIO на ESP32-S3, поэтому работает как источник пробуждения.
    // Включаем pull-up через RTC IO модуль — обычный pinMode(INPUT_PULLUP) в
    // sleep не сохраняется. GPIO 4 (бывшая вторая кнопка) физически снят и в
    // маску НЕ входит — будить не может.
    rtc_gpio_pullup_en  (static_cast<gpio_num_t>(PIN_BUTTON));
    rtc_gpio_pulldown_dis(static_cast<gpio_num_t>(PIN_BUTTON));

    uint64_t button_mask = (1ULL << PIN_BUTTON);
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
    // Снижаем частоту CPU: активная фаза — это в основном ожидание датчика и
    // экрана (I/O), вычислений мало, скорость не нужна — а ток меньше. 80 МГц
    // безопасно для UART/SPI/I2C. delay()/light sleep считаются по реальному
    // времени, поэтому длительность замера не меняется.
    setCpuFrequencyMhz(80);

    Serial.begin(115200);
    delay(100);
    Serial.println();
    Serial.println("============================");
    Serial.println("CO2 Monitor (CO2-only UI, 1 button, 4 screens)");
    Serial.println("============================");
    Serial.printf("CPU @ %d MHz\n", getCpuFrequencyMhz());

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
        last_full_refresh_uptime = 0;
        last_shown_valid = false;
        last_shown_co2 = 0; last_shown_t = 0.0f; last_shown_h = 0.0f;
        last_shown_alert = false;
        last_shown_screen = -1;
        Serial.println("First boot — RTC initialized");
    }
    if (current_screen < 0 || current_screen >= N_SCREENS) current_screen = 0;
    wake_count++;

    // Кнопка: при пробуждении ESP кладёт GPIO в обычный режим — поэтому
    // ставим INPUT_PULLUP, чтобы digitalRead в wait_for_button_release
    // отдавал HIGH при отпущенной кнопке. Кнопка одна (GPIO5).
    pinMode(PIN_BUTTON, INPUT_PULLUP);

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
        // Кнопка: листаем экран по кругу (0→1→2→3→0), без измерения.
        int prev = current_screen;
        current_screen = cycle_screen(current_screen, N_SCREENS);
        Serial.printf("Button: screen %d -> %d\n", prev, current_screen);
    } else {
        // Timer / первый запуск: измеряем и ВСЕГДА возвращаемся на главный
        // экран (так пользователь по таймеру видит свежее значение CO2).
        current_screen = SCREEN_MAIN;
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
            // Офсет температуры под single-shot (см. TEMPERATURE_OFFSET).
            // Задаём каждый цикл: powerDown сбрасывает RAM-настройки датчика.
            int16_t off_err = scd4x.setTemperatureOffset(TEMPERATURE_OFFSET);
            if (off_err) print_scd_error("setTemperatureOffset", off_err);
            #if VERBOSE_LOGGING
            float off_now = -1.0f;
            if (scd4x.getTemperatureOffset(off_now) == 0)
                Serial.printf("Temp offset: set %.2f C, readback %.2f C\n",
                              (float)TEMPERATURE_OFFSET, off_now);
            #endif

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

    // --- Обновляем экран только когда нужно (экономия батареи) ---
    // e-Paper держит картинку без питания, полный refresh + загрузка истории
    // для графика — дорогие. Поэтому:
    //   - кнопка: всегда перерисовываем (пользователь смотрит, листает экраны);
    //   - таймер: только если значения заметно изменились, сменился алерт,
    //     или прошёл час (от «теней»); иначе экран не трогаем вообще.
    uint32_t draw_uptime = total_uptime_before_sleep + millis() / 1000;
    bool screen_changed = (current_screen != last_shown_screen);
    bool values_changed =
        !last_shown_valid ||
        abs((int)last_co2 - (int)last_shown_co2) >= REDRAW_CO2_DELTA;
    bool periodic_full = (draw_uptime - last_full_refresh_uptime) >= FULL_REFRESH_INTERVAL;
    bool alert_changed = (alert_active != last_shown_alert);
    bool need_draw = from_button || screen_changed || periodic_full
                     || values_changed || alert_changed;

    if (need_draw) {
        draw_current_screen(current_screen, last_valid, last_co2);
        last_full_refresh_uptime = draw_uptime;
        last_shown_valid = last_valid;
        last_shown_co2 = last_co2;
        last_shown_t   = last_t;
        last_shown_h   = last_h;
        last_shown_alert = alert_active;
        last_shown_screen = current_screen;
    } else {
        VLOGLN("Screen unchanged — skip refresh (saving power)");
        display.hibernate();   // на всякий случай: контроллер экрана в сон
    }

    if (from_button) {
        wait_for_button_release();
    }
    go_to_sleep();   // не возвращается
}

void loop() {
    go_to_sleep();
}
