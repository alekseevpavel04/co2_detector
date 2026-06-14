// ============================================================
//  Air Quality Monitor — корпус (v0, параметрический)
// ============================================================
//  Компактный FDM-корпус. Все размеры — переменные ниже.
//  Размеры деталей взяты из даташитов; пометка TODO:verify =
//  проверить калипером на реальной детали.
//
//  F5 — превью (показывает «коробки» деталей внутри: видно, влезает ли).
//  F6 — рендер. Экспорт STL по отдельности: см. RENDER в самом низу.
//
//  Печать: лицевой стенкой ВНИЗ на стол (ровное окно, минимум поддержек).
// ============================================================

$fn = 24;

/* ---------- Допуски / стенки ---------- */
fit     = 0.4;   // зазор посадки деталей (подкрутить под свой принтер)
wall    = 2.0;   // боковые стенки
floor_t = 2.0;   // лицевая стенка (с окном)
lid_t   = 2.0;   // задняя крышка

/* ---------- Батарейный отсек 3xAA с выключателем (доминирует) ----------
   TODO:verify — варианты 68..70 x 47.5..49 x 18..20 мм */
bat_l = 70;
bat_w = 49;
bat_h = 20;

/* ---------- e-Paper Waveshare 2.13" HAT ---------- */
epd_pcb_l = 65;      // длина платы, TODO:verify
epd_pcb_w = 30.2;    // ширина платы, TODO:verify
epd_stack = 6;       // высота со стеклом/FPC/разъёмом, TODO:verify
epd_act_w = 48.55;   // активная (видимая) зона по длине
epd_act_h = 23.71;   // активная зона по ширине
epd_act_off_x = 8;   // смещение активной зоны от края платы по X, TODO:verify
epd_act_off_y = 3;   // смещение активной зоны от края платы по Y, TODO:verify

/* ---------- ESP32-S3 SuperMini ---------- */
esp_l = 22.52;       // длина платы
esp_w = 18;          // ширина платы
esp_h = 6;           // высота с компонентами/USB, TODO:verify
usb_w = 9.5;         // ширина выреза под USB-C, TODO:verify
usb_h = 4;           // высота выреза под USB-C, TODO:verify

/* ---------- SCD41 модуль ---------- TODO:verify свой модуль */
scd_l = 21;
scd_w = 21;
scd_h = 8;           // с металлической крышкой сенсора

/* ---------- Кнопки (тактовые, в передней стенке) ---------- */
btn_hole_d  = 4;     // отверстие под толкатель/палец
btn_spacing = 16;    // расстояние между центрами кнопок

/* ---------- Вентиляция SCD41 ---------- */
vent_d     = 1.8;    // диаметр отверстий решётки
vent_pitch = 3.5;    // шаг отверстий

/* ---------- Крепёжные «ушки» задней крышки ---------- */
ear_r       = 4;     // радиус ушка в углу
ear_hole_d  = 2.2;   // сквозное в крышке (под M2)
ear_pilot_d = 1.6;   // пилот в стойке корпуса (самонарезающий M2)
ear_h       = 6;     // глубина стойки от тыла

// ============================================================
//  Производные габариты
// ============================================================
front_layer = max(epd_stack, scd_h, esp_h) + 1;     // слой электроники
inner_w = max(bat_l, epd_pcb_l) + 2*fit;            // ширина полости
inner_h = max(bat_w, epd_pcb_w + 18) + 2*fit;       // высота (+полоса под кнопки)
inner_d = front_layer + 1 + bat_h;                  // глубина (+зазор перед батареей)

outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;
outer_d = floor_t + inner_d;                        // тыл открыт; крышка отдельно

// --- Позиции деталей (в координатах корпуса; Z=0 — внешняя сторона лица) ---
// e-Paper: верхняя зона, по центру X, прижата к верху.
epd_x = wall + (inner_w - epd_pcb_l)/2;
epd_y = wall + inner_h - epd_pcb_w - fit;
epd_z = floor_t;                                    // прижата к лицу изнутри

// Окно в лицевой стенке = активная зона экрана.
win_x = epd_x + epd_act_off_x;
win_y = epd_y + epd_act_off_y;

// ESP32 — нижний-левый угол, USB-C смотрит в левую стенку.
esp_x = wall + fit;
esp_y = wall + fit;
esp_z = floor_t;

// SCD41 — нижний-правый угол, у вентилируемой правой стенки.
scd_x = wall + inner_w - scd_l - fit;
scd_y = wall + fit;
scd_z = floor_t;

// Кнопки — по центру нижней полосы, между ESP32 и SCD41.
btn_cx = outer_w/2;
btn_y  = wall + (epd_y - wall)/2;                   // середина нижней полосы

// Батарея — задняя полость, по центру, прижата к тылу.
bat_x = wall + (inner_w - bat_l)/2;
bat_y = wall + (inner_h - bat_w)/2;
bat_z = floor_t + front_layer + 1;

// ============================================================
//  Вспомогательные модули
// ============================================================

// Решётка из отверстий в стенке с нормалью по X (правая стенка).
// Отверстия идут вдоль осей Y (ny шт) и Z (nz шт).
module vents_x(x, y0, z0, ny, nz) {
    for (j = [0:ny-1])
        for (k = [0:nz-1])
            translate([x - 1, y0 + j*vent_pitch, z0 + k*vent_pitch])
                rotate([0, 90, 0])
                    cylinder(d = vent_d, h = wall + 2);
}

// Решётка в стенке с нормалью по Y (нижняя стенка).
module vents_y(y, x0, z0, nx, nz) {
    for (i = [0:nx-1])
        for (k = [0:nz-1])
            translate([x0 + i*vent_pitch, y - 1, z0 + k*vent_pitch])
                rotate([-90, 0, 0])
                    cylinder(d = vent_d, h = wall + 2);
}

// 4 сплошных угловых ушка (cylinder в каждом внешнем углу).
module corner_solid(z0, h) {
    pts = [[0,0],[outer_w,0],[0,outer_h],[outer_w,outer_h]];
    for (p = pts)
        translate([p[0], p[1], z0]) cylinder(r = ear_r, h = h);
}

// 4 сквозных отверстия в углах (под винт / пилот).
module corner_drill(z0, h, d) {
    pts = [[0,0],[outer_w,0],[0,outer_h],[outer_w,outer_h]];
    for (p = pts)
        translate([p[0], p[1], z0]) cylinder(d = d, h = h);
}

// ============================================================
//  Корпус (shell)
// ============================================================
module shell() {
    difference() {
        union() {
            cube([outer_w, outer_h, outer_d]);
            // сплошные стойки под винты крышки (в углах, у тыла)
            corner_solid(outer_d - ear_h, ear_h);
        }

        // Внутренняя полость (тыл открыт: +1 по Z за заднюю грань)
        translate([wall, wall, floor_t])
            cube([inner_w, inner_h, inner_d + 1]);

        // Пилотные отверстия в стойках (самонарезающий M2)
        corner_drill(outer_d - ear_h - 1, ear_h + 2, ear_pilot_d);

        // Окно экрана
        translate([win_x, win_y, -1])
            cube([epd_act_w, epd_act_h, floor_t + 2]);

        // Отверстия под кнопки
        translate([btn_cx - btn_spacing/2, btn_y, -1])
            cylinder(d = btn_hole_d, h = floor_t + 2);
        translate([btn_cx + btn_spacing/2, btn_y, -1])
            cylinder(d = btn_hole_d, h = floor_t + 2);

        // Вырез USB-C в левой стенке (напротив ESP32)
        translate([-1, esp_y + esp_w/2 - usb_w/2, floor_t + (esp_h - usb_h)/2])
            cube([wall + 2, usb_w, usb_h]);

        // Решётка SCD41 — правая стенка над сенсором
        vents_x(outer_w, scd_y + 2, floor_t + 1,
                floor(scd_w / vent_pitch) - 1,
                floor(scd_h / vent_pitch));

        // Второй вент для сквозняка — нижняя стенка под SCD41
        vents_y(0, scd_x + 1, floor_t + 1,
                floor(scd_l / vent_pitch) - 1,
                floor(scd_h / vent_pitch));
    }
}

// ============================================================
//  Задняя крышка (lid)
// ============================================================
module lid() {
    difference() {
        union() {
            cube([outer_w, outer_h, lid_t]);
            corner_solid(0, lid_t);          // ушки в тон корпусу
        }
        corner_drill(-1, lid_t + 2, ear_hole_d);  // сквозные под винт
    }
}

// ============================================================
//  Превью деталей (только в F5; полупрозрачные «коробки»)
// ============================================================
module components_preview() {
    // e-Paper
    color("green", 0.35)
        translate([epd_x, epd_y, epd_z]) cube([epd_pcb_l, epd_pcb_w, epd_stack]);
    // ESP32
    color("blue", 0.35)
        translate([esp_x, esp_y, esp_z]) cube([esp_l, esp_w, esp_h]);
    // SCD41
    color("red", 0.35)
        translate([scd_x, scd_y, scd_z]) cube([scd_l, scd_w, scd_h]);
    // Батарея
    color("orange", 0.25)
        translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
}

// ============================================================
//  RENDER — что показать/экспортировать
// ============================================================
// Переключатель детали. В GUI оставь "all" (видишь обе детали + призраки).
// Для экспорта STL рендерю из командной строки:
//   openscad -D 'part="shell"' -o case_shell.stl case.scad
//   openscad -D 'part="lid"'   -o case_lid.stl   case.scad
part = "all";   // "all" | "shell" | "lid"

if (part == "shell") {
    shell();
} else if (part == "lid") {
    lid();
} else {                         // "all" — общий вид
    shell();
    if ($preview) components_preview();
    translate([outer_w + 10, 0, 0]) lid();   // крышка вынесена вбок
}
