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
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/Picopixel.h>
#include "main_screen.h"
#include "graph24_shell.h"
#include "graph7d_shell.h"

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

// Калибровка влажности. У SCD41 НЕТ аппаратного регистра офсета для RH (только
// для температуры), поэтому правим программно: это число (в % RH) прибавляется
// к показанию датчика — и на экране, и в файле, чтобы история была единообразной.
// RH в общем корпусе с тёплым ESP/экраном обычно ЗАНИЖЕН (тёплый воздух → ниже
// относительная влажность). После сборки сравни с отдельным гигрометром и
// подкрути: прибор показывает на X% МЕНЬШЕ реального → УВЕЛИЧЬ офсет на X.
#define HUMIDITY_OFFSET         0.0f

// --- Режим НАСТРОЙКИ (для итеративной доводки UI по USB) ---
// 1 = плата НЕ уходит в сон вообще, CPU держим на 240 МГц → USB-CDC порт
//     стабильно поднят и не пропадает. Это позволяет перепрошивать прибор
//     сколько угодно раз без «ловли» окна пробуждения. На экране — "TUNING
//     MODE" + живые значения.
//     ТЕПЕРЬ режим переключается НА ЛЕТУ: удерживай кнопку ~10 c — прибор
//     уйдёт в SETUP (без сна, USB живой); удержишь ещё раз — вернётся в
//     нормальный. Флаг живёт в RTC-памяти (переживает deep sleep, сбрасывается
//     при полном обесточивании). Значение ниже — лишь ДЕФОЛТ при первой
//     загрузке / после снятия батарей (0 = нормальный режим).
#define TUNING_MODE             0
#define LONG_PRESS_MS           10000      // мс — удержание для смены режима (~10 c)
#define AVERAGE_RECALC_INTERVAL 86400      // пересчёт средних раз в сутки
#define BUTTON_RELEASE_TIMEOUT  3000       // мс — сколько ждём отпускания

// --- Тестовая синтетика для проверки графиков ---
// 1 = при старте (если ещё не засеяно) затереть /data и записать 7 суток
// синтетических данных (часовой цикл + многосуточный тренд). Меняешь паттерн —
// подними SEED_VERSION, чтобы пересеять. ПЕРЕД боевым использованием → 0.
#define SEED_SYNTHETIC          0
#define SEED_VERSION            3

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

#define RTC_MAGIC      0xDEADBEF1   // bump при изменении набора RTC-переменных (форсит чистый init)
// Новый UI: 4 экрана, всё про CO2.
//   0 — MAIN (крупное текущее значение + статус)
//   1 — график CO2 за 1 час
//   2 — график CO2 за 24 часа
//   3 — график CO2 за 7 дней
// Одна кнопка (GPIO5) листает экраны по кругу. По таймеру всегда
// возвращаемся на MAIN. Экраны: 0=MAIN, 1=график 24ч, 2=график 7д.
// (График за 1ч убран — при 15-мин интервале там всего 4 точки.)
#define N_SCREENS      3
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
RTC_DATA_ATTR uint32_t tuning_mode = 0;          // 0=норм, 1=SETUP (без сна); смена долгим нажатием

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

#if SEED_SYNTHETIC
// Тестовая засевка: затираем /data и пишем 7 суток синтетики (часовой цикл +
// плавный многосуточный тренд). Срабатывает ОДИН раз на версию (маркер-файл),
// чтобы не пересевать каждую загрузку. Нужна только для проверки графиков.
static void seed_synthetic_once() {
    char marker[24];
    snprintf(marker, sizeof(marker), "/seed_v%d", SEED_VERSION);
    if (LittleFS.exists(marker)) { Serial.println("Synthetic: already seeded"); return; }
    Serial.println("Synthetic: seeding 7 days of data...");

    for (int n : list_data_file_numbers()) {          // затираем старое
        std::string p = make_filename(n);
        LittleFS.remove(p.c_str());
    }
    cached_file_path[0] = '\0';

    const int TOTAL = (7 * 24 * 3600) / MEASUREMENT_INTERVAL;   // 672 записи (7 сут)
    const int DAY_RECS  = (24 * 3600) / MEASUREMENT_INTERVAL;   // 96 записей = суточный цикл
    const float P2 = 6.2831853f;                                 // 2π (TWO_PI занят Arduino)

    std::string path = make_filename(1);
    File f = LittleFS.open(path.c_str(), "w", /*create=*/true);
    if (!f) { Serial.println("Synthetic: open failed"); return; }

    for (int i = 0; i < TOTAL; i++) {
        float daily = 250.0f * sinf(P2 * (float)i / (float)DAY_RECS);    // ±250 за сутки (1 цикл/день)
        float trend = (float)i * (180.0f / (float)TOTAL);                // +180 за 7 сут (небольшой тренд)
        int co2 = 600 + (int)(daily + trend);
        if (co2 < 400) co2 = 400;

        Measurement m{};
        m.timestamp = (uint32_t)i * MEASUREMENT_INTERVAL;
        m.co2 = (uint16_t)co2;
        m.temp_x10 = (int16_t)(230 + 30.0f * sinf(P2 * (float)i / (float)DAY_RECS));
        m.humidity = (uint8_t)(45 + (int)(10.0f * sinf(P2 * (float)i / (float)DAY_RECS)));
        m.flags = 0;
        f.write(reinterpret_cast<uint8_t*>(&m), sizeof(m));
    }
    f.close();

    File mk = LittleFS.open(marker, "w", /*create=*/true);
    if (mk) mk.close();
    Serial.printf("Synthetic: wrote %d records to %s\n", TOTAL, path.c_str());
}
#endif

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

        // Программная калибровка влажности (см. HUMIDITY_OFFSET).
        h += HUMIDITY_OFFSET;
        if (h < 0.0f)   h = 0.0f;
        if (h > 100.0f) h = 100.0f;

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

// Крупное число CO2 (размер 3) пиксельным шрифтом из шаблона. Цифры рисуются
// моноширинно справа налево: правый край последней цифры — у x_right, верх y_top.
// drawBitmap с одним цветом прозрачен по нулевым битам — фон оболочки не затирается.
static void draw_num_big(uint16_t v, int x_right, int y_top) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", v);
    for (int i = static_cast<int>(strlen(buf)) - 1; i >= 0; i--) {
        int d = buf[i] - '0';
        int w = DIG3_W[d];
        display.drawBitmap(x_right - w + 1, y_top, DIG3[d], w, DIG3_H, GxEPD_BLACK);
        x_right -= DIG3_PITCH;
    }
}

// Мелкое число (размер 2) для температуры/влажности. Поддерживает цифры, знак
// '+'/'-' (по центру ячейки) и точку '.' (у базовой линии) — как в макете.
static void draw_num_small(const char* s, int x_right, int y_top) {
    for (int i = static_cast<int>(strlen(s)) - 1; i >= 0; i--) {
        char c = s[i];
        if (c >= '0' && c <= '9') {
            int d = c - '0';
            int w = DIG2_W[d];
            display.drawBitmap(x_right - w + 1, y_top, DIG2[d], w, DIG2_H, GxEPD_BLACK);
        } else if (c == '.') {
            display.drawBitmap(x_right - 6, y_top + 12, DOT2,   DOT2_W,   DOT2_H,   GxEPD_BLACK);
        } else if (c == '+') {
            display.drawBitmap(x_right - 9, y_top + 3,  PLUS2,  PLUS2_W,  PLUS2_H,  GxEPD_BLACK);
        } else if (c == '-') {
            display.drawBitmap(x_right - 9, y_top + 6,  MINUS2, MINUS2_W, MINUS2_H, GxEPD_BLACK);
        }
        x_right -= DIG2_PITCH;
    }
}

// Постоянный индикатор режима: если прибор в SETUP (без сна) — рамка по
// периметру на ЛЮБОМ экране + (опционально) бейдж "SETUP" в углу. В нормальном
// режиме ничего не рисуем (чистый экран). Так режим виден всегда, а не только
// в момент переключения. Вызывать внутри firstPage/nextPage перед закрытием.
static void draw_mode_overlay(bool with_badge) {
    if (!tuning_mode) return;
    display.drawRect(0, 0, SCREEN_W, SCREEN_H, GxEPD_BLACK);
    display.drawRect(1, 1, SCREEN_W - 2, SCREEN_H - 2, GxEPD_BLACK);
    if (with_badge) {
        display.setFont(&Picopixel);
        int16_t bx, by; uint16_t bw, bh;
        display.getTextBounds("SETUP", 0, 0, &bx, &by, &bw, &bh);
        int w = static_cast<int>(bw) + 6, x = SCREEN_W - 5 - w, y = 4;
        display.fillRect(x, y, w, 9, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(x + 3, y + 7);
        display.print("SETUP");
        display.setTextColor(GxEPD_BLACK);
    }
}

// ===== Экран 0: MAIN — маскот слева + крупное CO2 + T/RH снизу =====
// Раскладка: отступ сверху ~16px (рамка корпуса режет край), без рубящей
// линии-разделителя; внизу — мелкие капс-подписи TEMP/HUMIDITY (watch-стиль).
static void draw_main(bool valid, uint16_t co2, float t, float h) {
    // Уровень → оболочка (своя картинка кота + статичные подписи/юниты, в bad
    // ещё и плашка NEED AIR). Пороги: good < 700, norm < 1200, иначе bad.
    // MAIN_Y_* — верх ячеек {CO2, темп, влажность} именно этой оболочки: блок
    // чисел в каждом макете может стоять на своей высоте, числа привязаны к ней.
    const unsigned char* shell = MAIN_SHELL_FINE;
    const int* ys = MAIN_Y_FINE;
    if (valid) {
        if (co2 < 700)       { shell = MAIN_SHELL_GOOD; ys = MAIN_Y_GOOD; }
        else if (co2 < 1200) { shell = MAIN_SHELL_FINE; ys = MAIN_Y_FINE; }
        else                 { shell = MAIN_SHELL_BAD;  ys = MAIN_Y_BAD;  }
    }

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        // Оболочка целиком (1:1 из макета пользователя)
        display.drawBitmap(0, 0, shell, MAINSCR_W, MAINSCR_H, GxEPD_BLACK);

        if (valid) {
            // Живые числа поверх оболочки, выровнены вправо к x=202 (юниты с x=206).
            draw_num_big(co2, 202, ys[0]);                    // CO2 (размер 3)

            char tbuf[12];
            snprintf(tbuf, sizeof(tbuf), "%+.1f", t);         // напр. "+24.3" / "-5.0"
            draw_num_small(tbuf, 202, ys[1]);                 // температура (размер 2)

            char hbuf[8];
            snprintf(hbuf, sizeof(hbuf), "%d", static_cast<int>(h + 0.5f));
            draw_num_small(hbuf, 202, ys[2]);                 // влажность (размер 2)
        }

        draw_mode_overlay(true);   // рамка + бейдж "SETUP", если в режиме настройки
    } while (display.nextPage());
    display.hibernate();
}

// ===== Экраны 1..3: график CO2 — тонкие бары, ПОЗИЦИОННАЯ ось времени =====
// Берём последние N записей ПО ПОЗИЦИИ в файле, а НЕ по timestamp: uptime
// сбрасывается при смене батарей, поэтому метки времени между сессиями
// ненадёжны. Замеры идут с фиксированным шагом MEASUREMENT_INTERVAL, значит
// N последних записей ≈ окно N*interval. Свежая запись — справа ("now"), дальше
// по одной влево. Это устойчиво к сбросу времени; плотных «фейковых» точек нет
// (в SETUP мы замеры больше не сохраняем). Стиль watch-UI: уровни слева, бары.
static void draw_graph(int period_idx, bool valid, uint16_t co2, float t, float h) {
    (void)valid; (void)co2; (void)t; (void)h;
    if (period_idx < 0 || period_idx > 2) period_idx = 0;

    // Сколько записей укладывается в окно при штатном интервале замеров.
    int n_target = (int)(GRAPH_WINDOW_SEC[period_idx] / MEASUREMENT_INTERVAL);
    if (n_target < 2) n_target = 2;

    std::vector<Measurement> ms;
    if (fs_ok) ms = load_recent_measurements(n_target);

    // Геометрия (увеличенный верхний отступ — край режется корпусом).
    const int PX = 30, PR = 8, PY = 33, PB = 100;
    const int PW = SCREEN_W - PX - PR, PH = PB - PY;
    // Столбцов на экране: мало записей → шире бары, много → тоньше (с
    // агрегацией). Шаг ЦЕЛОЧИСЛЕННЫЙ — иначе бары встают неравномерно и в
    // гребёнке появляются «проплешины».
    int nb = (n_target < PW / 3) ? n_target : PW / 3;
    if (nb < 1) nb = 1;
    int pitch = PW / nb;                               // целый шаг (ровная гребёнка)

    // Раскладываем по столбцам ПО ПОЗИЦИИ: свежая запись (последняя) — в правый
    // край, остальные левее пропорционально их «возрасту в записях». Если
    // записей меньше окна — слева остаётся пусто (история ещё копится), честно.
    std::vector<float> ssum(nb, 0.0f);
    std::vector<int>   cnt(nb, 0);
    int lo = 400, hi = 1000;
    int m = static_cast<int>(ms.size());
    if (m > 0) {
        float dmax = 0.0f;
        for (int j = 0; j < m; j++) {
            int age_recs = (m - 1) - j;                       // 0 = самая свежая
            float frac = (n_target > 1) ? (float)age_recs / (float)(n_target - 1) : 0.0f;
            if (frac > 1.0f) frac = 1.0f;
            int col = (nb - 1) - (int)(frac * (nb - 1) + 0.5f);
            if (col < 0) col = 0;
            if (col >= nb) col = nb - 1;
            ssum[col] += ms[j].co2; cnt[col]++;
            if (ms[j].co2 > dmax) dmax = ms[j].co2;
        }
        hi = (int)((dmax + 100.0f) / 200.0f + 1.0f) * 200;   // кратно 200, с запасом
        if (hi < 1000) hi = 1000;
        if (hi > 5000) hi = 5000;
    }
    auto vy = [&](float v) -> int {
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        return PB - (int)((v - lo) * (long)PH / (hi - lo));
    };

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);

        // Шапка: "CO2" слева, период справа (период — только здесь), линейка
        display.setFont(&FreeSansBold9pt7b);
        display.setCursor(8, 20);
        display.print("CO2");
        const char* pl = GRAPH_PERIOD_LBL[period_idx];
        display.setFont(&FreeSans9pt7b);
        int16_t lx, ly; uint16_t lw, lh;
        display.getTextBounds(pl, 0, 0, &lx, &ly, &lw, &lh);
        display.setCursor(242 - static_cast<int>(lw), 20);
        display.print(pl);
        display.drawLine(8, 25, SCREEN_W - 9, 25, GxEPD_BLACK);

        // Уровни (4 полосы) — пунктир + мелкие подписи слева (Picopixel)
        int step = (hi - lo) / 4;
        display.setFont(&Picopixel);
        for (int k = 0; k <= 4; k++) {
            int lvl = lo + step * k;
            int yy = vy((float)lvl);
            for (int x = PX; x < PX + PW; x += 5) display.drawPixel(x, yy, GxEPD_BLACK);
            char lab[8]; snprintf(lab, sizeof(lab), "%d", lvl);
            int16_t bx, byy; uint16_t bw, bh;
            display.getTextBounds(lab, 0, 0, &bx, &byy, &bw, &bh);
            display.setCursor(PX - 3 - static_cast<int>(bw), yy + 2);
            display.print(lab);
        }
        // Порог VENTILATE (1500) — заметнее остальных линий
        if (lo < ALERT_CO2_ON && ALERT_CO2_ON < hi) {
            int yy = vy((float)ALERT_CO2_ON);
            for (int x = PX; x < PX + PW; x += 3) {
                display.drawPixel(x, yy, GxEPD_BLACK);
                display.drawPixel(x + 1, yy, GxEPD_BLACK);
            }
        }

        // Бары по столбцам (ширина = шаг−1px, ровный зазор)
        int drawn = 0;
        int bw = pitch - 1;
        if (bw < 1) bw = 1;
        for (int col = 0; col < nb; col++) {
            if (cnt[col] == 0) continue;
            int x = PX + col * pitch;
            int y = vy(ssum[col] / cnt[col]);
            display.fillRect(x, y, bw, PB - y, GxEPD_BLACK);
            drawn++;
        }
        // Базовая линия
        display.drawLine(PX, PB, PX + PW - 1, PB, GxEPD_BLACK);
        if (drawn == 0) {
            display.setFont(&FreeSans9pt7b);
            display.setCursor(PX + 24, PY + PH / 2);
            display.print("collecting data...");
        }

        // Ось X: только "now" справа (период уже в шапке)
        display.setFont(&FreeSans9pt7b);
        int16_t ax, ay; uint16_t aw, ah;
        display.getTextBounds("now", 0, 0, &ax, &ay, &aw, &ah);
        display.setCursor(PX + PW - static_cast<int>(aw), PB + 13);
        display.print("now");

        draw_mode_overlay(false);  // только рамка (бейдж не лепим — в шапке период)
    } while (display.nextPage());
    display.hibernate();
}

// ===== Графики 24ч/7д — пиксельная оболочка из шаблона + заливка под кривой =====
// Оболочка (рамка, заголовок, подписи осей, сетка) вшита 1:1 битмапой. Поверх —
// заливка по данным ПО ПОЗИЦИИ (свежее справа). Шкала ФИКСИРОВАННАЯ под шаблон:
// 400..2000 ppm, база y=99, верх y=39. n_target = окно / интервал замеров.
static void draw_graph_template(const unsigned char* shell, int sw, int sh, int n_target) {
    const int PXL = 33, PXR = 239, BASE = 99, TOPY = 39;
    const int VMIN = 400, VMAX = 2000;
    if (n_target < 2) n_target = 2;

    std::vector<Measurement> ms;
    if (fs_ok) ms = load_recent_measurements(n_target);
    int m = static_cast<int>(ms.size());

    auto vy = [&](float v) -> int {
        if (v < VMIN) v = VMIN;
        if (v > VMAX) v = VMAX;
        return BASE - (int)((v - VMIN) * (long)(BASE - TOPY) / (VMAX - VMIN) + 0.5f);
    };

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        // Оболочка 1:1 (чёрные пиксели шаблона; серая «слепая» зона не рисуется)
        display.drawBitmap(0, 0, shell, sw, sh, GxEPD_BLACK);
        // Заливка под кривой по позиции. Линейная интерполяция между записями.
        if (m > 0) {
            for (int x = PXL; x <= PXR; x++) {
                float f = (float)(x - PXL) / (float)(PXR - PXL);
                float pos = f * (n_target - 1);
                float idx = pos - (float)(n_target - m);   // сдвиг, если истории мало
                if (idx < 0.0f) continue;                  // слева ещё пусто — честно
                int i0 = (int)idx; if (i0 > m - 1) i0 = m - 1;
                int i1 = (i0 + 1 < m) ? i0 + 1 : i0;
                float fr = idx - (float)i0;
                float val = ms[i0].co2 * (1.0f - fr) + ms[i1].co2 * fr;
                int y = vy(val);
                display.fillRect(x, y, 1, BASE - y + 1, GxEPD_BLACK);
            }
        }
        draw_mode_overlay(false);
    } while (display.nextPage());
    display.hibernate();
}

// Диспетчер: какой из 3 экранов рисовать.
// screen 1 → график 24ч, screen 2 → график 7д (оба — пиксельные шаблоны).
static void draw_current_screen(int screen, bool valid, uint16_t co2, float t, float h) {
    if (screen < 0 || screen >= N_SCREENS) screen = SCREEN_MAIN;
    if (screen == SCREEN_MAIN)
        draw_main(valid, co2, t, h);
    else if (screen == 1)
        draw_graph_template(GRAPH24_SHELL, GRAPH24_W, GRAPH24_H,
                            (int)(GRAPH_WINDOW_SEC[1] / MEASUREMENT_INTERVAL));
    else
        draw_graph_template(GRAPH7D_SHELL, GRAPH7D_W, GRAPH7D_H,
                            (int)(GRAPH_WINDOW_SEC[2] / MEASUREMENT_INTERVAL));
}

// Подтверждение смены режима: две строки по центру (показываем при долгом нажатии).
static void draw_mode_message(const char* line1, const char* line2) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(1);
        int16_t bx, by; uint16_t bw, bh;
        display.setFont(&FreeSansBold12pt7b);
        display.getTextBounds(line1, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((SCREEN_W - static_cast<int>(bw)) / 2, 56);
        display.print(line1);
        display.setFont(&FreeSans9pt7b);
        display.getTextBounds(line2, 0, 0, &bx, &by, &bw, &bh);
        display.setCursor((SCREEN_W - static_cast<int>(bw)) / 2, 80);
        display.print(line2);
    } while (display.nextPage());
    display.hibernate();
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
// Режим НАСТРОЙКИ (SETUP): не спим, USB живой, показываем живые
// значения. Замер делаем через delay() (НЕ light sleep!), иначе
// USB-порт пропадёт на время измерения. Включается долгим нажатием.
// ------------------------------------------------------------
static uint32_t g_tuning_last_ms = 0;

static void measure_awake(uint16_t& co2, float& t, float& h) {
    co2 = 0; t = 0.0f; h = 0.0f;
    for (int a = 0; a < 3; a++) {
        if (scd4x.measureSingleShot()) { delay(100); continue; }
        delay(5200);                       // ожидание готовности БЕЗ light sleep
        if (scd4x.readMeasurement(co2, t, h)) { co2 = 0; continue; }
        if (co2 > 0) return;
    }
}

// Сессионные значения для режима настройки.
static uint16_t g_tune_co2 = 0;
static float    g_tune_t = 0.0f, g_tune_h = 0.0f;
static bool     g_tune_valid = false;
static int      g_btn_prev = HIGH;

static void tuning_measure_and_store() {
    if (!sensor_ok) return;
    uint16_t co2 = 0; float t = 0.0f, h = 0.0f;
    measure_awake(co2, t, h);
    if (co2 > 0) {
        g_tune_co2 = co2; g_tune_t = t; g_tune_h = h; g_tune_valid = true;
        alert_active = update_alert_state(alert_active, co2);
        Serial.printf("TUNE CO2 %u ppm, T %.1f C, RH %.0f%%  (alert=%d)\n",
                      co2, t, h, alert_active ? 1 : 0);
        // НЕ сохраняем: в SETUP-режиме данные искажены саморазогревом (T завышена,
        // RH занижена) — это «грязь», нельзя пускать её в историю.
    }
}

static void tuning_setup() {
    Serial.println();
    Serial.println("=== SETUP MODE: never sleep, USB stays up, CPU 240 MHz ===");
    Serial.println("Hold button ~10s to return to NORMAL mode.");
    Serial.printf("CPU @ %d MHz\n", getCpuFrequencyMhz());

    pinMode(PIN_BUTTON, INPUT_PULLUP);
    init_display();
    init_filesystem();
#if SEED_SYNTHETIC
    if (fs_ok) seed_synthetic_once();
#endif

    wake_count = 1;                 // чистая инициализация датчика
    init_sensor();
    if (sensor_ok) {
        scd4x.setTemperatureOffset(TEMPERATURE_OFFSET);
        scd4x.wakeUp(); delay(30);
    }

    current_screen = SCREEN_MAIN;
    tuning_measure_and_store();
    draw_current_screen(current_screen, g_tune_valid, g_tune_co2, g_tune_t, g_tune_h);
    g_tuning_last_ms = millis();
    g_btn_prev = digitalRead(PIN_BUTTON);
    Serial.println("SETUP ready: real UI on screen, button cycles screens. Reflash freely.");
}

// ------------------------------------------------------------
// setup() / loop()
// ------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(100);

    // RTC magic + защита от мусора. Делаем В САМОМ НАЧАЛЕ — до выбора режима,
    // потому что сам режим (tuning_mode) тоже живёт в RTC и должен быть валиден.
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
        tuning_mode = TUNING_MODE;   // дефолт режима при первой загрузке / после обесточивания
        Serial.println("First boot — RTC initialized");
    }
    if (current_screen < 0 || current_screen >= N_SCREENS) current_screen = 0;

    // --- Режим SETUP (без сна): НЕ трогаем 240 МГц ради надёжного USB ---
    if (tuning_mode) {
        tuning_setup();
        return;
    }

    // --- Нормальный режим ---
    // Снижаем частоту CPU: активная фаза — это в основном ожидание датчика и
    // экрана (I/O), вычислений мало, скорость не нужна — а ток меньше. 80 МГц
    // безопасно для UART/SPI/I2C. delay()/light sleep считаются по реальному
    // времени, поэтому длительность замера не меняется.
    setCpuFrequencyMhz(80);
    Serial.println();
    Serial.println("============================");
    Serial.println("CO2 Monitor (CO2-only UI, 1 button, 4 screens)");
    Serial.println("============================");
    Serial.printf("CPU @ %d MHz\n", getCpuFrequencyMhz());
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
#if SEED_SYNTHETIC
    if (fs_ok) seed_synthetic_once();
#endif
    load_average_cache();   // если файла нет — g_cache остаётся нулевым

    if (from_button) {
        // Долгое удержание (~10 c) → переключение в SETUP-режим (без сна).
        // millis() здесь ≈ время с момента пробуждения = с момента нажатия.
        while (digitalRead(PIN_BUTTON) == LOW && millis() < LONG_PRESS_MS) delay(10);
        if (digitalRead(PIN_BUTTON) == LOW) {
            Serial.println("Long press -> switching to SETUP mode");
            tuning_mode = 1;
            draw_mode_message("SETUP MODE", "USB on - reflash freely");
            while (digitalRead(PIN_BUTTON) == LOW) delay(10);   // дождаться отпускания
            delay(50);
            // Входим в режим без сна ПРЯМО СЕЙЧАС, без перезагрузки.
            // Возвращаем 240 МГц (как в штатном SETUP — ради надёжного USB).
            setCpuFrequencyMhz(240);
            tuning_setup();
            return;          // дальше прибор крутит loop() в SETUP-режиме
        }
        // Короткое нажатие: листаем экран по кругу (0→1→2→3→0), без измерения.
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
#if SEED_SYNTHETIC
                // Пока включена синтетика — НЕ дописываем реальные замеры, иначе
                // они «прилипают» к концу графика и портят чистую тестовую кривую.
                Serial.println("SEED_SYNTHETIC: real measurement NOT saved");
#else
                save_measurement(rec);

                // Пересчёт средних — раз в сутки (Этап 13).
                if (now_uptime - last_average_recalc_uptime >= AVERAGE_RECALC_INTERVAL) {
                    recalculate_averages();
                    last_average_recalc_uptime = now_uptime;
                }
#endif
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
        draw_current_screen(current_screen, last_valid, last_co2, last_t, last_h);
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
    // В нормальном режиме setup() уходит в deep sleep и сюда не возвращается.
    // Этот цикл крутится только в SETUP-режиме (без сна). Подстраховка:
    if (!tuning_mode) { go_to_sleep(); return; }

    // Опрос кнопки: короткое нажатие — листаем экраны, долгое (~10 c) — выходим
    // обратно в нормальный режим. Плюс периодический замер с обновлением.
    int btn = digitalRead(PIN_BUTTON);
    if (g_btn_prev == HIGH && btn == LOW) {        // фронт нажатия
        delay(30);                                 // антидребезг
        if (digitalRead(PIN_BUTTON) == LOW) {
            unsigned long t0 = millis();
            while (digitalRead(PIN_BUTTON) == LOW && millis() - t0 < LONG_PRESS_MS) delay(10);
            if (digitalRead(PIN_BUTTON) == LOW) {
                Serial.println("Long press -> returning to NORMAL mode");
                tuning_mode = 0;
                draw_mode_message("NORMAL MODE", "sleep enabled");
                while (digitalRead(PIN_BUTTON) == LOW) delay(10);
                delay(50);
                // Сразу в нормальный режим (сон). Следующее пробуждение
                // (таймер/кнопка) пойдёт обычным путём. Без перезагрузки.
                go_to_sleep();   // не возвращается
            }
            // Короткое нажатие: листаем экран по кругу.
            current_screen = cycle_screen(current_screen, N_SCREENS);
            Serial.printf("Button: screen -> %d\n", current_screen);
            draw_current_screen(current_screen, g_tune_valid,
                                g_tune_co2, g_tune_t, g_tune_h);
        }
    }
    g_btn_prev = btn;

    if (millis() - g_tuning_last_ms > 20000) {
        uint16_t prev = g_tune_co2;
        tuning_measure_and_store();
        // Перерисовываем главный только при заметном изменении CO2 —
        // чтобы e-paper не «моргал» полным обновлением каждые 20 сек.
        if (current_screen == SCREEN_MAIN &&
            abs((int)g_tune_co2 - (int)prev) >= 30)
            draw_current_screen(current_screen, g_tune_valid,
                                g_tune_co2, g_tune_t, g_tune_h);
        g_tuning_last_ms = millis();
    }
    delay(20);
}
