# Air Quality Monitor — Полное ТЗ

Самодельный батарейный монитор качества воздуха на базе ESP32-S3. Измеряет CO2, температуру и влажность через датчик SCD41, рисует графики на e-Paper экране, работает от 3× AA батареек ~1.5-2.5 месяца без подзарядки.

---

## Контекст разработки

**Пользователь:** программист с CS-образованием, знает Python и высокоуровневые языки. **Не имеет опыта C++ и embedded-разработки.** Использует AI-ассистента (Claude Code) для написания кода итеративно.

**Главный принцип:** реализовывать **поэтапно** (14 этапов ниже), начиная с минимума. После каждого этапа — компиляция, прошивка на железо, проверка. Не пытаться написать всю прошивку сразу.

**Стиль кода:**
- Комментарии в коде на **русском** языке (для пользователя)
- Названия переменных и функций на **английском**
- Понятная структура без излишних оптимизаций
- Магические числа выносить в `#define` или `const`
- Все строки UI на e-Paper — **на английском** (Adafruit GFX не поддерживает кириллицу в стандартных шрифтах)
- На каждом этапе обильное логирование в `Serial` для отладки

**Платформа:** PlatformIO + Arduino framework (не ESP-IDF, не ESPHome).

**Правила работы с Git (для AI-ассистента):**
- Единственный автор всех коммитов — пользователь (`alekseevpavel04 <alekceevpavel@mail.ru>`).
- **НИКОГДА** не добавлять trailer `Co-Authored-By: Claude ...` или любую другую подпись AI в commit message.
- **НИКОГДА** не указывать AI-ассистента как автора (`git commit --author=...`) или соавтора коммита.
- **НИКОГДА** не добавлять AI-ассистента как коллаборатора в репозиторий (через GitHub API, gh CLI и т.д.).
- В commit message не упоминать Claude / AI / "generated with" — сообщения пишутся от лица пользователя.
- Это правило применяется всегда, без исключений и напоминаний.

**Аппаратный bring-up:** все 14 этапов уже реализованы и закоммичены. Когда
пользователь спрашивает про прошивку, отладку на железе, диагностику или
порядок проверки — **открой `BRINGUP.md`** в корне проекта. Там пошаговый
runbook: для каждого этапа — git commit, проводка, команды, ожидаемый
Serial-вывод, дерево диагностики и параметры для подкрутки.

---

## ⚠️ КРИТИЧЕСКИЕ ПРЕДУПРЕЖДЕНИЯ

Эти моменты могут сломать проект или сжечь железо. Прочитать обязательно.

### 1. Питание ESP32-S3

- 3× AA в боксе → к **пину 5V** платы ESP32-S3 SuperMini (НЕ к 3V3!)
- На пин 5V допустимо 3.3-6В, внутренний LDO выдаёт 3.3В на чип
- LDO требует **минимум ~3.6В на входе** для стабильных 3.3В на выходе
- При батарейках ниже 3.6В (≈1.2В на батарейку, ~70% разряда) устройство выключится
- Полный разряд батареи использовать НЕ удастся — это особенность LDO, не баг
- **НИКОГДА не подавать 4.5В на пин 3V3** — это спалит чип

### 2. Кириллица на экране

**Adafruit GFX НЕ поддерживает кириллицу** в стандартных шрифтах — техническое ограничение, не пожелание пользователя.

**Решение для первой версии:** все строки UI на английском: "VENTILATE", "CO2 for 1 hour", "Sensor not found". Hook через `#define UI_LANGUAGE_RU 0`. Строки выбирать через `#if UI_LANGUAGE_RU`. Позже можно добавить кастомные шрифты с кириллицей.

### 3. RTC memory нестабильна при первой загрузке

Переменные `RTC_DATA_ATTR` **не инициализируются автоматически** при первом включении после power-off. Значение **неопределённо** (может быть мусор). **Обязательно** проверять магическим числом:

```cpp
#define RTC_MAGIC 0xDEADBEEF
RTC_DATA_ATTR uint32_t rtc_magic = 0;
RTC_DATA_ATTR uint32_t wake_count = 0;
RTC_DATA_ATTR int current_screen = 0;
RTC_DATA_ATTR bool alert_active = false;
RTC_DATA_ATTR uint32_t total_uptime_before_sleep = 0;
RTC_DATA_ATTR uint32_t last_full_refresh_uptime = 0;
RTC_DATA_ATTR uint32_t last_average_recalc_uptime = 0;

void setup() {
    if (rtc_magic != RTC_MAGIC) {
        rtc_magic = RTC_MAGIC;
        wake_count = 0; current_screen = 0; alert_active = false;
        total_uptime_before_sleep = 0;
        last_full_refresh_uptime = 0;
        last_average_recalc_uptime = 0;
        Serial.println("First boot, RTC initialized");
    }
    if (current_screen < 0 || current_screen >= 9) current_screen = 0;  // защита от мусора
    wake_count++;
}
```

### 4. SCD41 power management

SCD41 в idle потребляет ~0.5 мА — много для бюджета. **Перед deep sleep обязательно `scd4x.powerDown()`** — потребление падает до 0.15 мкА.

**`wake_up()` SCD41 вызывать только перед измерением**, не при каждом старте. Если пробуждение от кнопки (без измерения) — SCD41 не трогать вообще.

### 5. Packed struct для бинарных файлов

Без `__attribute__((packed))` компилятор может добавить padding, и `sizeof(Measurement)` будет 12 байт вместо 10. На разных toolchain поведение разное → файлы несовместимы.

```cpp
struct __attribute__((packed)) Measurement {
    uint32_t timestamp;
    uint16_t co2;
    int16_t temp_x10;
    uint8_t humidity;
    uint8_t flags;
};
static_assert(sizeof(Measurement) == 10, "Measurement must be 10 bytes");
```

### 6. LittleFS partition

Стандартный `huge_app.csv` НЕ содержит SPIFFS/LittleFS раздела. Использовать `default.csv` (~1.5 МБ SPIFFS раздел). LittleFS на ESP32 в Arduino framework монтируется в SPIFFS-раздел физически.

```ini
board_build.partitions = default.csv
board_build.filesystem = littlefs
```

### 7. Кнопки и wake-up

- ESP32-S3 ext1 wakeup поддерживает только **RTC GPIO** (на S3 это GPIO 0-21). GPIO 4 и 5 — RTC GPIO ✅
- Для pull-up при wake-up использовать `rtc_gpio_pullup_en()`, не `pinMode(INPUT_PULLUP)` (обычный pull-up отключается в deep sleep)
- Использовать `ESP_EXT1_WAKEUP_ANY_LOW` (в старых ESP-IDF может быть `ESP_EXT1_WAKEUP_ALL_LOW`)
- **Перед `esp_deep_sleep_start()` гарантированно дождаться отпускания обеих кнопок**, иначе цикл бесконечный

### 8. Прошивка ESP32-S3 SuperMini

Плата иногда не входит в bootloader автоматически. Если PlatformIO выдаёт ошибку загрузки: зажать BOOT → кратко нажать RESET → отпустить BOOT → запустить upload. Перед первой прошивкой полезно `pio run -t erase` для чистого LittleFS.

### 9. Timestamp не переживает power-off

`uptime` (millis()/1000) обнуляется при reset. Данные в файлах остаются, но `timestamp` относится к предыдущему сеансу. Если читать "данные за последние 24 часа по timestamp" — старые данные не попадут в выборку.

**Решение:** читать данные **по позиции в файле** (последние N записей), а не по timestamp. См. раздел "Хранение данных" → "Чтение данных".

---

## Железо

### Распиновка

```
GPIO 4  → Кнопка 1 (показатель)     ← rtc_gpio_pullup_en
GPIO 5  → Кнопка 2 (период)         ← rtc_gpio_pullup_en
GPIO 6  → E-Paper RST
GPIO 7  → E-Paper BUSY              ← input only
GPIO 8  → SCD41 SDA (I2C)
GPIO 9  → SCD41 SCL (I2C)
GPIO 10 → E-Paper CS (SPI)
GPIO 11 → E-Paper MOSI (SPI DIN)
GPIO 12 → E-Paper SCK (SPI CLK)
GPIO 13 → E-Paper DC
GPIO 48 → На некоторых платах встроенный RGB LED, на других — нет. **Не использовать без проверки конкретной платы.**
```

**Не использовать:** GPIO 14, 15 (strapping pins), GPIO 19, 20 (USB).

### Подключения

**SCD41 (4 провода):** VDD→3V3, GND→GND, SDA→GPIO 8, SCL→GPIO 9. I2C адрес: **0x62**.

**Waveshare 2.13" e-Paper HAT (8 проводов через шлейф):** VCC→3V3, GND→GND, DIN→GPIO 11, CLK→GPIO 12, CS→GPIO 10, DC→GPIO 13, RST→GPIO 6, BUSY→GPIO 7.

**Кнопки (по 2 контакта):** один контакт → GND, второй → GPIO (4 или 5). Pull-up внутренний через `rtc_gpio_pullup_en()`. Внешние резисторы не нужны.

**Питание (3× AA бокс):** Красный (+) → пин **5V**, Чёрный (−) → GND. **Полярность критична** — перепутанная сожжёт ESP32. Перед подключением **проверить мультиметром:** +4.5В на красном относительно чёрного.

### Индикатор батареи

**В первой версии не реализуется.** Требует внешнего делителя (2× 100kΩ). При разряде LDO просто перестанет работать, e-Paper сохранит последнее изображение (не теряет картинку без питания). Hook оставить в коде.

### Порядок подключения для теста

1. **Этап 1:** только USB к ESP32.
2. **Этап 2:** + 4 провода SCD41.
3. **Этап 3:** + 8 проводов e-Paper.
4. **Этап 6:** + 2 кнопки. Питание всё ещё через USB.
5. **Этап 14:** подключить батарейный бокс. **USB отключить!** (USB drain мешает измерять реальное потребление).

---

## Дизайн экрана

E-Paper Waveshare 2.13" — **250×122 пикселя**, ч/б, без подсветки.

### Layout

```
y=0   ┌──────────────────────────────────────────────┐
      │ Header: значения + светофоры                  │  16 px
y=16  ├──────────────────────────────────────────────┤
      │ Title: "CO2 for 1 hour"                       │  12 px
y=28  ├──────────────────────────────────────────────┤
      │                                                │
      │ Main graph area                                │  68 px
      │                                                │
y=96  ├──────────────────────────────────────────────┤
      │ X-axis labels                                  │  10 px
y=106 ├──────────────────────────────────────────────┤
      │ "VENTILATE" alert (если активен)              │  16 px
y=122 └──────────────────────────────────────────────┘
```

### Header (y: 0-16)

Шрифт `FreeSans9pt7b` (~12 px). В одну строку: слева 3 значения (CO2 → T → RH), справа группа из 3 светофоров. Ширина текста ~170 px, светофоров ~60 px. Точные координаты подобрать экспериментально.

**Если данных ещё нет:** вместо значений показывать `—`, светофоры — пустые точки `○○○`.

**Альтернативный layout** (если в одну строку не помещается): каждый показатель в отдельной колонке с маленьким светофором над/под значением.

### Светофор

3 круглые точки диаметром 4 px с шагом 2 px:
- ●●● — хорошо
- ●●○ — терпимо
- ●○○ — плохо

```cpp
void draw_traffic_light(int x, int y, int level) {
    // level: 0=плохо, 1=терпимо, 2=хорошо
    int filled = level + 1;
    for (int i = 0; i < 3; i++) {
        int dot_x = x + i * 6;
        if (i < filled) display.fillCircle(dot_x, y, 2, GxEPD_BLACK);
        else            display.drawCircle(dot_x, y, 2, GxEPD_BLACK);
    }
}
```

### Title (y: 16-28)

`FreeSans9pt7b`, x=2: "CO2 for 1 hour", "Temperature for 24 hours", "Humidity for 7 days" и т.д.

### Main graph (y: 28-96)

Высота 68 px, отступ ~15 px слева под подписи Y-оси.

**Шкала Y** — фикс по умолчанию, автомасштаб при выходе за пределы:

| Параметр | Фикс диапазон |
|---|---|
| CO2 | 400-2000 ppm |
| Temperature | 15-30°C |
| Humidity | 20-80% |

**Подписи Y:** 2 значения (min/max) на левом краю, шрифт ~8 px.

**Шкала X / тип отрисовки:**

| Период | Точек | Тип |
|---|---|---|
| 1 hour | 12 (5 мин × 12) | **Bars** — столбики ~16 px |
| 24 hours | ~144 (downsampled из 288) | **Filled area** под кривой |
| 7 days | ~168 (downsampled из 2016) | **Line + daily maxima** |

**Линии трендов** (накладываются поверх):

| Линия | Стиль | Конфиг |
|---|---|---|
| Идеал | Сплошная 1 px | `SHOW_IDEAL_LINE` |
| Среднее | Точечный пунктир | `SHOW_AVERAGE_LINE` |
| Порог "плохо" | Длинный пунктир | `SHOW_THRESHOLD_BAD` |

**Идеальные значения:** CO2 600, T 22°C, RH 50%.
**Пороги "плохо":** CO2 > 1500; T < 18 или > 26; RH < 30 или > 70.

Линия "среднее" отображается только если `average_cache.has_data == true` (после первого пересчёта, ~через 24 часа работы).

### X-axis labels (y: 96-106)

Шрифт **родной 5×7 px** или `Picopixel`:
- 1 hour: "-1h" слева, "now" справа
- 24 hours: "-24h" слева, "now" справа
- 7 days: 7 меток дней недели через равные промежутки

### Alert "VENTILATE" (y: 106-122)

Показывается **на всех 9 экранах** при CO2 > 1500. Появление: `co2 > ALERT_CO2_ON (1500)`. Исчезновение: `co2 < ALERT_CO2_OFF (1300)` — гистерезис. Состояние в `RTC_DATA_ATTR bool alert_active`. Шрифт `FreeSansBold12pt7b` (~16 px), по центру. Если не активен — место пустое.

### Welcome screen (первое включение)

Прогрев датчика **не требуется** — SCD41 single-shot выдаёт приемлемые данные с первого замера (T в первые 10 мин может быть завышена на ~1°C, терпимо).

При `wake_count == 1`:
- Показать welcome screen (текст "Warming up sensor..." + "Please wait" по центру) через **partial refresh** (быстро, не моргает)
- Сразу сделать первое измерение (5 сек)
- Показать данные (даже неточные)
- Уйти в sleep на 5 минут

---

## 9 экранов и навигация

```
Index | Параметр    | Период
------|-------------|----------
0     | CO2         | 1 hour
1     | CO2         | 24 hours
2     | CO2         | 7 days
3     | Temperature | 1 hour
4     | Temperature | 24 hours
5     | Temperature | 7 days
6     | Humidity    | 1 hour
7     | Humidity    | 24 hours
8     | Humidity    | 7 days
```

### Кнопки — pure функции в `lib/logic/`

```cpp
// logic.h
int cycle_param(int current_screen);   // CO2 → T → RH → CO2 (период не меняется)
int cycle_period(int current_screen);  // 1h → 24h → 7d → 1h (параметр не меняется)
```

Реализация: `current_screen / 3` даёт `param_idx`, `current_screen % 3` — `period_idx`, инкрементируется один из них, возвращается `param_idx * 3 + period_idx`.

**Использование в main.cpp:**
```cpp
if (pin_mask & (1ULL << PIN_BUTTON_PARAM))       current_screen = cycle_param(current_screen);
else if (pin_mask & (1ULL << PIN_BUTTON_PERIOD)) current_screen = cycle_period(current_screen);
```

`RTC_DATA_ATTR int current_screen` переживает deep sleep.

### Защита от долгого нажатия и дребезга

`wait_for_button_release()` **обязательно вызывать перед `go_to_sleep()`**, иначе ESP-IDF снова разбудит на той же удерживаемой кнопке — бесконечный цикл.

```cpp
void wait_for_button_release() {
    unsigned long start = millis();
    while (true) {
        bool p1 = (digitalRead(PIN_BUTTON_PARAM) == HIGH);
        bool p2 = (digitalRead(PIN_BUTTON_PERIOD) == HIGH);
        if (p1 && p2) break;
        if (millis() - start > 3000) break;  // timeout 3 сек
        delay(10);
    }
    delay(50);  // anti-bounce
}
```

---

## Хранение данных

### Структура измерения

```cpp
struct __attribute__((packed)) Measurement {
    uint32_t timestamp;     // секунды с старта (uptime). Обнуляется при reset.
    uint16_t co2;           // ppm (валидный диапазон 0-10000)
    int16_t temp_x10;       // T × 10 (235 = 23.5°C)
    uint8_t humidity;       // RH в % (0-100)
    uint8_t flags;          // резерв
};
static_assert(sizeof(Measurement) == 10, "Measurement struct must be 10 bytes");
```

### Файловая структура

```
/data/
    measurements_001.bin    ← текущий, растёт до 90 КБ
    measurements_002.bin    ← следующий
    ...
    measurements_013.bin    ← когда станет 14-й, удаляется самый старый
/cache/
    average_cache.bin       ← пересчитывается раз в сутки
```

### Ротация (по размеру файла, не по времени)

```cpp
#define MAX_FILE_SIZE  (90 * 1024)  // 90 КБ ≈ 9000 измерений ≈ 31 день
#define MAX_FILES      13
```

- Каждое измерение — append в текущий файл
- При достижении 90 КБ → создать следующий по номеру (`_001` → `_002`)
- Если файлов больше MAX_FILES → удалить самый старый (имя с минимальным номером)
- При reset: сканировать `/data/`, файл с наибольшим номером = текущий. Если пусто → создать `_001`.

### Кэширование current file path

`get_current_file_path()` сканирует `/data/` через LittleFS — ~50-100 мс. При 288 measurement/день это потеря 5-10 мА·ч/день. **Решение:** кэш в RTC memory.

```cpp
RTC_DATA_ATTR char cached_file_path[64] = "";
// если cached_file_path[0] != '\0' и LittleFS.exists(cached_file_path) → использовать
// иначе → list_data_files_sorted(), взять последний, записать в кэш
// при rotate_files() — обновить кэш на новый
```

При reset кэш обнуляется (мусор), но `LittleFS.exists()` приведёт к пересканированию.

### Запись измерения

`save_measurement(m)`: открыть текущий файл в режиме "a", записать `sizeof(Measurement)` байт, `flush()`, прочитать `f.size()`, закрыть. Если размер ≥ MAX_FILE_SIZE → `rotate_files()`. Обязательно логировать в Serial размер файла и неполные записи.

### Чтение данных за период (по позиции, не по timestamp!)

Timestamp обнуляется при reset → **читать последние N измерений по позиции в файле**, не по времени.

**КРИТИЧНО:** не загружать все файлы в RAM. У ESP32-S3 SuperMini только 320 КБ RAM, 13 файлов × 90 КБ = 1.2 МБ — не помещается. Использовать `seek()`:

```cpp
// Последние N записей из конкретного файла
std::vector<Measurement> read_last_n_from_file(const String &path, int n) {
    File f = LittleFS.open(path, "r");
    if (!f) return {};
    int records = f.size() / sizeof(Measurement);
    int to_read = std::min(n, records);
    f.seek((records - to_read) * sizeof(Measurement));
    std::vector<Measurement> result;
    Measurement m;
    for (int i = 0; i < to_read; i++) {
        if (f.read((uint8_t*)&m, sizeof(m)) == sizeof(m) && is_measurement_valid(m))
            result.push_back(m);
    }
    f.close();
    return result;
}

// Последние N из всех файлов: идём от свежего файла к старому, добавляем в начало vector
// Удобные обёртки:
//   load_recent_measurements(12)   — для 1h графика
//   load_recent_measurements(288)  — для 24h
//   load_recent_measurements(2016) — для 7d
```

`vector::insert(begin, ...)` для 2016 элементов — ~50 мс на ESP32-S3, приемлемо. Если будут проблемы, заменить на push_back + reverse.

### Защита от corruption

При чтении файла: если `file_size % sizeof(Measurement) != 0` — обрезать до кратного, логировать warning. Битые записи (`is_measurement_valid` returns false) пропускать.

### File management — контракты функций

```cpp
std::vector<String> list_data_files_sorted();  // все /data/measurements_*.bin, отсортированы по имени
String get_current_file_path();                // последний по номеру, или "/data/measurements_001.bin" если пусто
int get_file_number(const String &path);       // "/data/measurements_001.bin" → 1
void rotate_files();                            // создать следующий, удалить самый старый если файлов >= MAX_FILES
```

**Замечание про `list_data_files_sorted`:** `entry.name()` в LittleFS может возвращать либо `/data/measurements_001.bin`, либо `measurements_001.bin` — нормализовать вручную перед сортировкой.

### Average cache

Хранит **готовые к отрисовке** средние — 12 точек для 1h, 144 для 24h, 168 для 7d. Эти числа соответствуют ширине графиков после downsampling.

```cpp
#define CACHE_VERSION 1

struct __attribute__((packed)) AverageCache {
    uint32_t version;
    uint8_t has_data;
    uint8_t reserved[3];
    uint16_t avg_co2_1h[12];     uint16_t avg_co2_24h[144];     uint16_t avg_co2_7d[168];
    int16_t  avg_temp_1h[12];    int16_t  avg_temp_24h[144];    int16_t  avg_temp_7d[168];
    uint8_t  avg_humidity_1h[12]; uint8_t avg_humidity_24h[144]; uint8_t avg_humidity_7d[168];
};
// Размер ~1620 байт
```

**Версионирование:** при загрузке проверять `cache.version == CACHE_VERSION`. Если не совпадает (после обновления прошивки структура изменилась) — кэш игнорировать, ждать следующего пересчёта.

Пересчёт раз в сутки. В `handle_measurement()`:
```cpp
uint32_t uptime = total_uptime_before_sleep + millis() / 1000;
if (uptime - last_average_recalc_uptime >= AVERAGE_RECALC_INTERVAL) {
    recalculate_averages();
    last_average_recalc_uptime = uptime;
}
```

### Алгоритм пересчёта средних

**Идея:** для каждого окна (1h, 24h, 7d) взять историю последних 30 дней, разбить на множество окон такого же размера, для каждой позиции внутри окна — среднее по всем таким окнам.

| Окно | Точек в окне (реальных) | Целевой размер (для отрисовки) |
|---|---|---|
| 1h | 12 | 12 |
| 24h | 288 | 144 |
| 7d | 2016 | 168 |

Для 1h всё просто (12=12). Для 24h и 7d — усреднять внутри окна до целевого размера.

```
для каждого периода (1h, 24h, 7d):
    real_window = 12 | 288 | 2016
    target = 12 | 144 | 168
    n_windows = len(history) / real_window
    для i в (0..target):
        sum = 0; count = 0
        для j в (0..n_windows):
            real_start = j*real_window + (i*real_window/target)
            real_end   = j*real_window + ((i+1)*real_window/target)
            для k в (real_start..real_end):
                sum += history[k].co2; count++
        cache.avg_co2_[период][i] = sum / count
```

Edge cases: мало данных, нецелое деление, пустая история — обработать.

**Что это даёт:** линия "среднее" показывает обычный паттерн (например, «среднее CO2 в момент 55 мин назад / 50 / ... / 0 мин назад, по последним 30 дням»). Видно, выше или ниже текущая динамика.

### Downsampling

```cpp
std::vector<float> downsample(const std::vector<float> &input, int target_size) {
    std::vector<float> output(target_size);
    if (input.empty()) return output;  // все нули
    for (int i = 0; i < target_size; i++) {
        size_t start = ((size_t)i * input.size()) / target_size;
        size_t end = ((size_t)(i + 1) * input.size()) / target_size;
        if (end == start) end = start + 1;
        if (end > input.size()) end = input.size();
        float sum = 0;
        for (size_t j = start; j < end; j++) sum += input[j];
        output[i] = sum / (end - start);
    }
    return output;
}
```

Простое усреднение. На длинных периодах острые пики сглаживаются — приемлемо. Если нужно лучше — позже заменить на LTTB.

### Битые измерения

```cpp
bool is_measurement_valid(const Measurement &m) {
    return m.co2 > 0 && m.co2 < 10000
        && m.temp_x10 > -400 && m.temp_x10 < 500
        && m.humidity <= 100;
}
```

Не сохранять, не показывать, пропустить цикл.

### Когда данных мало

Если для "1 hour" есть только 3 точки (15 мин работы) — показать имеющиеся в правой части графика, левая часть пустая. То же для 24h и 7d. График заполняется справа налево по мере накопления.

---

## Время в системе (без NTP)

ESP32 без Wi-Fi не знает реального времени. Используем **uptime** (millis()/1000), накопленный через deep sleep:

```cpp
RTC_DATA_ATTR uint32_t total_uptime_before_sleep = 0;
// в go_to_sleep(): total_uptime_before_sleep += millis() / 1000;
// в setup() после wake: uint32_t real_uptime = total_uptime_before_sleep + millis() / 1000;
```

Переживает deep sleep, **но НЕ power-off**. После замены батарей счётчик обнуляется. **Поэтому чтение данных — по позиции в файле, не по timestamp.**

Через год можно добавить NTP через Wi-Fi.

---

## Энергопотребление

| Операция | Ток | Время | Расход |
|---|---|---|---|
| Deep sleep (ESP + SCD41 powered down) | 30 мкА | 99.5% | 0.7 мА·ч |
| Wake + измерение SCD41 (6 сек) | 50 мА | 288 раз/день | 24 мА·ч |
| Partial refresh e-Paper (0.5 сек) | 30 мА | 288 раз/день | 1.2 мА·ч |
| Full refresh e-Paper (2 сек, раз в час) | 30 мА | 24 раза/день | 0.4 мА·ч |
| Кнопки | пренебрежимо | ~5 раз/день | — |

**Итого: ~26 мА·ч/день** на чипе. С КПД LDO (~73% при 4.5В): **~36 мА·ч/день от батарей**.

**Реалистичная автономность:**
- Щелочные Duracell Basic (~1500 мА·ч полезной ёмкости до 3.6В): **~42 дня**
- Литиевые Energizer Ultimate (~2400 мА·ч): **~67 дней**

**Итог: 1.5-2.5 месяца.** Чтобы дольше — увеличить `MEASUREMENT_INTERVAL` до 10 мин (2× к автономности).

---

## Конфигурация

В начале `main.cpp`:

```cpp
// --- Аппаратные пины ---
#define PIN_SDA              8
#define PIN_SCL              9
#define PIN_BUTTON_PARAM     4
#define PIN_BUTTON_PERIOD    5
#define PIN_EPD_CS           10
#define PIN_EPD_MOSI         11
#define PIN_EPD_SCK          12
#define PIN_EPD_DC           13
#define PIN_EPD_RST          6
#define PIN_EPD_BUSY         7

// --- Wi-Fi / локализация / отладка ---
#define ENABLE_WIFI          0
#define UI_LANGUAGE_RU       0  // 0=EN (default), 1=RU (требует custom fonts)
#define VERBOSE_LOGGING      1

// --- Тренды на графиках ---
#define SHOW_IDEAL_LINE      1
#define SHOW_AVERAGE_LINE    1
#define SHOW_THRESHOLD_BAD   1

// --- Калибровка температуры ---
// Положительное = насколько занизить (датчик греется от ESP).
// После сборки сравнить с термометром и подкорректировать.
#define TEMPERATURE_OFFSET   2.0

// --- Идеальные значения ---
#define IDEAL_CO2            600
#define IDEAL_TEMP           22.0
#define IDEAL_HUMIDITY       50

// --- Пороги светофора ---
// CO2 (ppm): хорошо < GOOD_MAX, терпимо < OK_MAX, иначе плохо
#define CO2_GOOD_MAX         800
#define CO2_OK_MAX           1500
// Temperature (°C): хорошо GOOD_MIN-GOOD_MAX, терпимо OK_MIN-OK_MAX, иначе плохо
#define TEMP_GOOD_MIN        20.0
#define TEMP_GOOD_MAX        24.0
#define TEMP_OK_MIN          18.0
#define TEMP_OK_MAX          26.0
// Humidity (%)
#define HUMIDITY_GOOD_MIN    40
#define HUMIDITY_GOOD_MAX    60
#define HUMIDITY_OK_MIN      30
#define HUMIDITY_OK_MAX      70

// --- Alert CO2 (гистерезис) ---
#define ALERT_CO2_ON         1500
#define ALERT_CO2_OFF        1300

// --- Интервалы (секунды) ---
#define MEASUREMENT_INTERVAL    300    // 5 минут
#define FULL_REFRESH_INTERVAL   3600   // 1 час
#define AVERAGE_RECALC_INTERVAL 86400  // 24 часа

// --- Шкалы графиков ---
#define CO2_SCALE_MIN        400
#define CO2_SCALE_MAX        2000
#define TEMP_SCALE_MIN       15
#define TEMP_SCALE_MAX       30
#define HUMIDITY_SCALE_MIN   20
#define HUMIDITY_SCALE_MAX   80

// --- Хранилище ---
#define MAX_FILE_SIZE        (90 * 1024)
#define MAX_FILES            13

// --- RTC magic ---
#define RTC_MAGIC            0xDEADBEEF

// --- UI строки (для UI_LANGUAGE_RU == 0) ---
#define UI_ALERT_TEXT        "VENTILATE"
#define UI_WARMUP_TITLE      "Warming up sensor..."
#define UI_WARMUP_SUBTITLE   "Please wait"
#define UI_ERROR_SENSOR      "Sensor not found"
#define UI_PARAM_CO2         "CO2"
#define UI_PARAM_TEMP        "Temperature"
#define UI_PARAM_HUMIDITY    "Humidity"
#define UI_PERIOD_1H         "1 hour"
#define UI_PERIOD_24H        "24 hours"
#define UI_PERIOD_7D         "7 days"
```

---

## platformio.ini

```ini
[env:esp32-s3-supermini]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino
monitor_speed = 115200
upload_speed = 921600
board_build.partitions = default.csv
board_build.filesystem = littlefs

build_flags =
    -DARDUINO_USB_MODE=1
    -DARDUINO_USB_CDC_ON_BOOT=1

; Лучше GitHub URL для надёжности
lib_deps =
    https://github.com/Sensirion/arduino-i2c-scd4x.git
    https://github.com/ZinggJM/GxEPD2.git
    https://github.com/adafruit/Adafruit-GFX-Library.git
    https://github.com/adafruit/Adafruit_BusIO.git

[env:native]
platform = native
test_framework = unity
build_flags =
    -std=gnu++17
    -DUNITY_INCLUDE_DOUBLE
```

Если registry-имена (`sensirion/Sensirion I2C SCD4x` и т.п.) не находятся — использовать GitHub URL.

---

## Архитектура проекта и тестирование

Проект разделён на две части для тестируемости **до появления железа**:

- **Чистая логика** — `lib/logic/logic.h` + `logic.cpp`. Без зависимостей от Arduino/Wire/SPI/LittleFS. Тестируется на хосте через `pio test -e native`.
- **Arduino-специфика** — `src/main.cpp`. Взаимодействие с железом. Проверяется на плате (или в Wokwi).

### Структура файлов

```
project/
├── platformio.ini          ← два environments: esp32 и native
├── src/
│   └── main.cpp            ← Arduino-специфика
├── lib/
│   └── logic/
│       ├── logic.h         ← объявления чистых функций
│       └── logic.cpp       ← реализация
└── test/
    └── test_logic/
        └── test_logic.cpp  ← Unity-тесты
```

### Что выносится в `lib/logic/`

Функции без зависимостей от железа:
- Светофоры: `get_co2_traffic_level()`, `get_temp_traffic_level()`, `get_humidity_traffic_level()`
- Алерт с гистерезисом: `update_alert_state(bool current, int co2)`
- Навигация: `cycle_param(int)`, `cycle_period(int)`
- Валидация: `is_measurement_valid(const Measurement&)`
- Downsampling: `downsample(input, target_size)`
- File numbering: `extract_file_number()`, `make_filename()`, `next_file_number()`
- Расчёт средних: `calculate_averages()` (vector → vector, без I/O)

**Важно:** в logic использовать `std::string`, не Arduino `String` (которого нет на native). В main.cpp конвертировать через `String(s.c_str())`.

### Что остаётся в `src/main.cpp`

Всё, что взаимодействует с железом (не покрывается юнит-тестами):
- `setup()`, `loop()`, `init_hardware()`, `go_to_sleep()`
- `take_measurement()` — SCD41
- `save_measurement()`, `load_*()` — LittleFS
- `draw_*()` — GxEPD2
- `handle_button_wakeup()`, `handle_measurement()` — оркестрация

Эти функции **вызывают чистую логику из logic.h**, но сами проверяются на железе через Serial и визуально.

### Цикл разработки на каждом этапе

1. **Реализовать** функции этапа (логику — в `lib/logic/`, железо — в `src/main.cpp`)
2. **Написать тесты** для чистой логики этапа в `test/test_logic/test_logic.cpp`
3. **Запустить** `pio test -e native` — всё зелёное
4. Если ошибки — исправить **логику**, не подгонять тесты под результат
5. Только потом — следующий этап

Команды:
- `pio test -e native` — юнит-тесты на хосте
- `pio run -e esp32-s3-supermini -t upload` — прошивка

### Что тестировать на каждом этапе

| Этап | Тесты |
|---|---|
| 1-5 | Юнит-тесты не нужны (только железо) |
| 6 (Кнопки) | `cycle_param`, `cycle_period` — все 9 переходов |
| 7 (LittleFS) | `extract_file_number`, `make_filename`, `next_file_number` |
| 8 (Ротация) | Выбор файла для удаления, лимит MAX_FILES |
| 9 (График) | `downsample` — edge cases (пустой input, target > input) |
| 10 (9 экранов) | Полный цикл навигации |
| 11 (Тренды) | `get_*_traffic_level` для всех порогов |
| 12 (Алерт) | `update_alert_state` — вход в зону, гистерезис, выход |
| 13 (Averages) | `calculate_averages` — корректность |
| 14 (Финал) | **Интеграционный тест** — синтетический месяц работы |

### Финальный интеграционный тест (Этап 14)

`test/test_logic/test_integration.cpp`:
- Сгенерировать синтетические данные на месяц (~8640 измерений с реалистичным суточным паттерном CO2)
- Прогнать через цепочку: валидация → расчёт средних → downsampling
- Проверить полный цикл навигации по всем 9 экранам
- Проверить алерт на колебаниях CO2 вокруг порогов

### Wokwi (опционально)

Если захочется глазами увидеть UI до железа — https://wokwi.com (бесплатный браузерный симулятор Arduino/ESP32, поддерживает SCD41 и e-Paper 2.13"). Опционально — юнит-тесты + проверка на железе достаточны.

---

## Скелет main.cpp

Полная реализация пишется поэтапно. Базовый каркас:

```cpp
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SensirionI2CScd4x.h>
#include <GxEPD2_BW.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <vector>
#include "logic.h"  // pure функции из lib/logic/

// КОНФИГУРАЦИЯ (#define блок из раздела выше)
// ТИПЫ ДАННЫХ (Measurement, AverageCache)

SensirionI2CScd4x scd4x;

// Класс GxEPD2 — подобрать экспериментально: B73 → B72 → BN → DEPG0213BN.
// Второй template param (HEIGHT vs MAX_HEIGHT) зависит от версии GxEPD2.
GxEPD2_BW<GxEPD2_213_B73, GxEPD2_213_B73::HEIGHT> display(
    GxEPD2_213_B73(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)
);

// RTC memory (см. КРИТИЧЕСКОЕ #3)
RTC_DATA_ATTR uint32_t rtc_magic, wake_count;
RTC_DATA_ATTR int current_screen;
RTC_DATA_ATTR bool alert_active;
RTC_DATA_ATTR uint32_t total_uptime_before_sleep;
RTC_DATA_ATTR uint32_t last_full_refresh_uptime, last_average_recalc_uptime;

// Сессионные переменные
bool sensor_available = false;
Measurement current_measurement;
bool has_current_measurement = false;

void setup() {
    Serial.begin(115200); delay(100);

    // Инициализация RTC при первой загрузке (см. КРИТИЧЕСКОЕ #3)
    // Защита current_screen от мусора, wake_count++

    // LittleFS.begin(false), при ошибке — LittleFS.begin(true)
    // init_i2c(), init_epaper()

    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    if (reason == ESP_SLEEP_WAKEUP_EXT1) {
        // Wake from button: не трогаем SCD41,
        // читаем последнее измерение из файла для header,
        // вызываем handle_button_wakeup()
    } else {
        // Wake by timer ИЛИ первый запуск:
        // check_sensor(), при успехе — handle_measurement(),
        // иначе — show_error_screen()
    }

    go_to_sleep();  // не возвращается
}

void loop() { /* не используется */ }

// Основные блоки:
// - init_i2c(), init_epaper(), check_sensor()
// - handle_measurement(): warmup screen при wake_count==1, wakeUp SCD41,
//   set offset, take_measurement(), save, update alert, recalc averages раз в сутки, draw
// - handle_button_wakeup(): pin_mask, cycle_param/period, draw, wait_for_button_release
// - take_measurement(m): measureSingleShot, delay 5s, readMeasurement, fill struct
// - go_to_sleep(): scd4x.powerDown(), accumulate uptime,
//   esp_sleep_enable_timer_wakeup, rtc_gpio_pullup_en для кнопок,
//   esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW),
//   esp_deep_sleep_start()
// - save_measurement, load_recent_measurements, rotate_files,
//   recalculate_averages, load/save_average_cache
// - draw_full_screen, draw_header, draw_traffic_light, draw_title,
//   draw_graph_bars_1h/area_24h/line_7d, draw_trend_lines,
//   draw_x_axis_labels, draw_alert_if_needed
//
// Точные сигнатуры — по мере реализации этапов.
```

**Класс GxEPD2** для первой сборки — попробовать `GxEPD2_213_B73` (SSD1680), потом `B72` (SSD1675), `BN`, `DEPG0213BN`. Если экран ничего не показывает — менять класс. **Это нормальная часть отладки, не баг.**

---

## 14 этапов реализации

Реализация **строго итеративная**. Каждый этап — отдельный коммит и проверка на железе.

### Этап 1: Hello world
- **Что:** мигание встроенного LED, вывод в Serial.
- **Подключения:** только USB.
- **Успех:** в Serial "Hello!", LED мигает.

Также на этом этапе — **создать полную файловую структуру**: `platformio.ini` с двумя environments, `src/main.cpp` (минимальный), `lib/logic/logic.h` + `logic.cpp` (скелеты), `test/test_logic/test_logic.cpp` (один тест-заглушка).

### Этап 2: SCD41 в Serial
- **Что:** читать SCD41 в цикле, вывод в Serial.
- **Подключения:** + 4 провода SCD41.
- **Успех:** каждые 5 сек "CO2: 645 ppm, T: 23.4°C, H: 45%". Значения: CO2 400-2000, T 18-28, RH 30-70.
- **Проблемы:** "not found" → проверить адрес 0x62, провода. Дикие значения → плохой контакт.

### Этап 3: E-Paper Hello
- **Что:** нарисовать "Hello, e-Paper!".
- **Подключения:** + 8 проводов e-Paper.
- **Успех:** виден текст.
- **Если не работает:** менять класс GxEPD2 (B73 → B72 → BN → DEPG0213BN).

### Этап 4: SCD41 + E-Paper
- **Что:** измерять и выводить на экран.
- **Успех:** 3 значения, обновляются каждые 30 сек (пока без sleep).

### Этап 5: Deep sleep
- **Что:** уходить в deep sleep на 5 мин. Использовать `RTC_DATA_ATTR` для счётчика. `scd4x.powerDown()` перед sleep. **Добавить warmup screen** при `wake_count == 1` через partial refresh.
- **Успех:** цикл wake → measure → display → sleep работает. В Serial wake count растёт. Если есть мультиметр: ток в sleep < 100 мкА. При первом включении 5 сек "Warming up..."

### Этап 6: Кнопки
- **Что:** wake from buttons через ext1. Cycle экранов (пока текст "Screen N"). Пишутся юнит-тесты на `cycle_param`, `cycle_period`.
- **Подключения:** + 2 кнопки на GPIO 4 и 5.
- **Успех:** нажатие → wake → переключение → sleep. Удержание не вызывает бесконечный цикл.

### Этап 7: LittleFS — сохранение измерений
- **Что:** сохранять каждое измерение в `/data/measurements_001.bin`, логировать размер.
- **Успех:** файл растёт, переживает reset, данные корректны.

### Этап 8: Ротация файлов
- **Что:** при 90 КБ → новый файл, при >13 файлов → удалить старейший.
- **Успех:** программно симулировать 9000+ измерений, видеть `_002.bin`, `_003.bin`... При >13 — старейший удаляется.

### Этап 9: Первый график
- **Что:** график CO2 за 1 час (12 столбиков).
- **Успех:** столбики видны, высота соответствует значениям, график заполняется справа налево.

### Этап 10: Все 9 экранов и навигация
- **Что:** `draw_graph_area_24h`, `draw_graph_line_7d`. Cycle через кнопки.
- **Успех:** все 9 экранов, переключение правильное, current_screen переживает sleep.

### Этап 11: Линии трендов
- **Что:** идеальная линия, порог "плохо". Конфиг через `#define`.
- **Успех:** линии видны, `SHOW_IDEAL_LINE = 0` → пропадает.

### Этап 12: Алерт VENTILATE
- **Что:** при CO2 > 1500 на всех экранах. Гистерезис.
- **Успех:** > 1500 → видно, < 1300 → пропадает, между 1300-1500 — сохраняется.

### Этап 13: Average cache
- **Что:** раз в сутки пересчитывать средние, линия "среднее" на графиках.
- **Успех:** через сутки линия среднего появляется. Пересчёт < 5 сек.

### Этап 14: Полировка и батарея
- **Что:** финальные шрифты, уменьшить `VERBOSE_LOGGING`, подключить батарейный бокс, **отключить USB**. Замерить автономность за неделю. Написать **интеграционный тест** (синтетический месяц данных).
- **Успех:** работает от батарей, ток в sleep < 100 мкА, прогноз ~1.5-2 месяца.

---

## Известные проблемы и решения

**E-Paper не показывает картинку:**
1. Менять класс GxEPD2 (B73 → B72 → BN → DEPG0213BN)
2. Проверить пины (особенно DC и BUSY)
3. Убедиться, что `display.init(115200)` вызван до отрисовки
4. Попробовать `display.setRotation(0/1/2/3)`

**SCD41 ошибки коммуникации:**
1. I2C-сканер — видит устройство 0x62?
2. Проверить питание SCD41 (3.3В)
3. Проверить контакты модуля (SDA/SCL могут быть подписаны иначе)

**Глюки при разряде батарей:** LDO не справляется при <3.6В на входе. Это особенность, не баг. Замена батарей.

**LittleFS corruption** (power-off во время записи): если `LittleFS.begin(false)` упал → `LittleFS.begin(true)` отформатирует. Данные потеряются, устройство продолжит работать.

**Wake from sleep слишком часто:** утечка на GPIO кнопки (грязь, флюс, длинный провод как антенна), внешние помехи, неисправная кнопка.

**Кнопки не работают после долгого нажатия:** в `wait_for_button_release()` сработал timeout — кнопка залипла или удерживается. Программно нормально (выход через 3 сек), физически — проверить.

---

## FAQ

**Q: Класс GxEPD2_213_B73 не подходит?**
A: Попробовать B72, BN, DEPG0213BN. Модель Waveshare 2.13 не всегда совпадает с маркировкой.

**Q: Изменить интервал измерений?**
A: `MEASUREMENT_INTERVAL`: 600 (10 мин) → 2× автономности, 1800 (30 мин) → 6×.

**Q: Экран показывает призраков?**
A: Уменьшить `FULL_REFRESH_INTERVAL` (например, 1800 = раз в 30 мин).

**Q: SCD41 показывает несоответствующие значения?**
A: Подождать 5-10 мин стабилизации; подкорректировать `TEMPERATURE_OFFSET`; проветрить комнату до улицы для ABC-калибровки.

**Q: SCD41 не подключен?**
A: `check_sensor()` вернёт false → "Sensor not found" на экране, sleep 5 мин, повтор. Расход ~4 мА·ч/день — лучше выключить до починки.

**Q: ESP32-C3 SuperMini вместо S3?**
A: Технически да, но GPIO только 11 — нужно очень аккуратно перераспределить, может не хватить.

**Q: Зачем кэш средних, если можно считать на лету?**
A: При нажатии кнопки экран обновляется за 0.2 сек, а не 1-2 сек. Пересчёт раз в день — фоновый.

**Q: Почему не ESPHome?**
A: ESPHome — когда не хочется писать код. Здесь хотим полный контроль и оптимальное энергопотребление.

---

## Финальный чек-лист

Перед первой прошивкой:
- [ ] `platformio.ini`: правильные `lib_deps`, `partitions = default.csv`, `filesystem = littlefs`
- [ ] Все пины в `#define` соответствуют схеме
- [ ] Класс GxEPD2 выбран (хотя бы догадка — `B73`)
- [ ] `RTC_MAGIC`, `ENABLE_WIFI 0`, `UI_LANGUAGE_RU 0`
- [ ] `static_assert(sizeof(Measurement) == 10, ...)` присутствует
- [ ] `pio run` без ошибок
- [ ] `pio run -t erase` (очистка flash)
- [ ] `pio run -t upload`, Serial Monitor открыт

После прошивки:
- [ ] В Serial: "First boot, RTC initialized" → "Wake count: 1"
- [ ] `check_sensor()` returns true
- [ ] Первое измерение в Serial
- [ ] E-Paper показывает что-то осмысленное

---

**Удачи с проектом!**
