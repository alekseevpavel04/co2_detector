// ============================================================
//  Air Quality Monitor — корпус (v2, параметрический)
// ============================================================
//  Компактный FDM-корпус под заказную печать.
//   - крепёж плат: угловые ШТЫРЬКИ с защёлкивающим язычком
//     (плата вставляется и фиксируется, без клея);
//   - SCD41 в отдельной ВЕНТИЛИРУЕМОЙ КАМЕРЕ (перегородка
//     отделяет датчик от внутреннего тёплого воздуха; дырки —
//     прямо в комнату → честный замер CO2/температуры);
//   - задняя крышка — обычный SNAP-FIT (язычки в окошки стенок);
//   - реальные тела модулей (ghosts) для проверки посадки.
//
//  Размеры деталей — из даташитов; TODO:verify = проверить
//  калипером перед заказом печати (от них зависят зазоры посадок).
//
//  Рендер из командной строки:
//    openscad -D 'part="fit_nb"' -o fit.png --preview ...   (посадка плат)
//    openscad -D 'part="print"'  -o case.stl case.scad       (STL обеих деталей)
// ============================================================

$fn = 32;

/* ---------- Допуски / стенки ---------- */
fit     = 0.4;   // общий зазор посадки
wall    = 2.0;   // боковые стенки
floor_t = 2.0;   // лицевая стенка
lid_t   = 2.0;   // крышка

/* ---------- Компоненты (TODO:verify калипером) ---------- */
bat_l = 70;  bat_w = 49;  bat_h = 20;          // батарейный отсек 3xAA
epd_pcb_l = 65;  epd_pcb_w = 30.2;  epd_stack = 6;  epd_pcb_t = 1.6;  // e-Paper HAT
epd_act_w = 48.55; epd_act_h = 23.71;          // активная зона экрана
epd_act_off_x = 8; epd_act_off_y = 3;          // смещение зоны от края платы
esp_l = 22.52; esp_w = 18; esp_h = 6; esp_pcb_t = 1.4;   // ESP32-S3 SuperMini
scd_l = 21; scd_w = 21; scd_h = 8;             // SCD41 модуль
usb_w = 9.5; usb_h = 4;                         // вырез USB-C
btn_hole_d = 4; btn_spacing = 16;              // кнопки

/* ---------- Штырьки-защёлки для плат ---------- */
post_d     = 3.0;   // сторона штырька
post_clear = 0.3;   // зазор штырёк–край платы
lip        = 0.8;   // вылет язычка над платой
lip_h      = 1.0;   // высота язычка

/* ---------- Камера SCD41 ---------- */
cham_wall   = 1.5;  // толщина перегородки
cham_clear  = 0.6;  // зазор перегородка–датчик

/* ---------- Крышка snap-fit ---------- */
lid_clear = 0.3;    // зазор крышки в проёме
tab_w     = 8;      // ширина язычка
tab_t     = 1.6;    // толщина плеча язычка
tab_arm   = 6;      // длина плеча (вперёд по Z)
barb_out  = 1.2;    // вылет зацепа в стенку
barb_h    = 3;      // высота зацепа по Z

/* ---------- Вентиляция ---------- */
vent_w = 1.6;       // ширина щели

/* ---------- Производные ---------- */
front_layer   = 8;                             // слой электроники по Z
epd_lower_gap = 2;                             // зазор экран–нижние платы
inner_w = max(bat_l, epd_pcb_l) + 2*fit;
inner_h = max(bat_w, epd_pcb_w + max(esp_w, scd_w) + epd_lower_gap) + 2*fit;
inner_d = front_layer + bat_h;
outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;
lid_z0  = floor_t + inner_d;                   // фронт крышки (= тыл батареи)
outer_d = lid_z0 + lid_t;                       // крышка заподлицо

// --- Позиции компонентов (Z=0 — внешняя сторона лица) ---
epd_x = wall + (inner_w - epd_pcb_l)/2;
epd_y = wall + inner_h - epd_pcb_w - fit;       // верхняя зона
win_x = epd_x + epd_act_off_x;
win_y = epd_y + epd_act_off_y;

esp_x = wall + fit;                             // нижний-левый
esp_y = wall + fit;

scd_x = wall + inner_w - scd_l - fit;           // нижний-правый (камера)
scd_y = wall + fit;

btn_cx = outer_w/2;
btn_y  = wall + (epd_y - wall)/2;

bat_x = wall + (inner_w - bat_l)/2;
bat_y = wall + (inner_h - bat_w)/2;
bat_z = floor_t + front_layer;

// положения язычков крышки (по X), подальше от вент-зоны SCD41 справа-снизу
tab_x1 = wall + inner_w*0.25;
tab_x2 = wall + inner_w*0.55;

// ============================================================
//  Базовые вырезы
// ============================================================
module cavity_cut() {
    translate([wall, wall, floor_t]) cube([inner_w, inner_h, outer_d]);
}
module window_cut() {
    translate([win_x, win_y, -1]) cube([epd_act_w, epd_act_h, floor_t + 2]);
}
module buttons_cut() {
    translate([btn_cx - btn_spacing/2, btn_y, -1]) cylinder(d = btn_hole_d, h = floor_t + 2);
    translate([btn_cx + btn_spacing/2, btn_y, -1]) cylinder(d = btn_hole_d, h = floor_t + 2);
}
module usb_cut() {
    translate([-1, esp_y + esp_w/2 - usb_w/2, floor_t + (esp_h - usb_h)/2])
        cube([wall + 3, usb_w, usb_h]);
}
// Две вентиляции у SCD41: правая стенка + нижняя стенка.
module vents_cut() {
    for (i = [0:2])
        translate([wall + inner_w - 1, scd_y + 3 + i*5, floor_t + 1.5])
            cube([wall + 2, vent_w, 5]);
    for (i = [0:2])
        translate([scd_x + 3, -1, floor_t + 1.5 + i*2.2])
            cube([scd_l - 6, wall + 2, vent_w]);
}

// ============================================================
//  Штырьки-защёлки (крепёж плат без клея)
// ============================================================
// (cx,cy) — центр штырька; axis 0=язычок вдоль X, 1=вдоль Y;
// dir = в какую сторону смотрит язычок (+1/-1); clamp_t = толщина платы.
module retain_post(cx, cy, axis, dir, clamp_t) {
    h = clamp_t + lip_h;
    translate([cx - post_d/2, cy - post_d/2, floor_t]) cube([post_d, post_d, h]);
    if (axis == 0) {
        lx = (dir > 0) ? cx + post_d/2 : cx - post_d/2 - lip;
        translate([lx, cy - post_d/2, floor_t + clamp_t]) cube([lip, post_d, lip_h]);
    } else {
        ly = (dir > 0) ? cy + post_d/2 : cy - post_d/2 - lip;
        translate([cx - post_d/2, ly, floor_t + clamp_t]) cube([post_d, lip, lip_h]);
    }
}

module posts() {
    // e-Paper: 4 штырька на левом и правом краях (язычки внутрь по X)
    exl = epd_x - post_clear - post_d/2;
    exr = epd_x + epd_pcb_l + post_clear + post_d/2;
    ey1 = epd_y + 4;  ey2 = epd_y + epd_pcb_w - 4;
    retain_post(exl, ey1, 0, +1, epd_pcb_t);
    retain_post(exl, ey2, 0, +1, epd_pcb_t);
    retain_post(exr, ey1, 0, -1, epd_pcb_t);
    retain_post(exr, ey2, 0, -1, epd_pcb_t);

    // ESP32: 4 штырька на верхнем и нижнем краях (язычки по Y; левый край — под USB)
    eyb = esp_y - post_clear - post_d/2;
    eyt = esp_y + esp_w + post_clear + post_d/2;
    ex1 = esp_x + 4;  ex2 = esp_x + esp_l - 4;
    retain_post(ex1, eyb, 1, +1, esp_pcb_t);
    retain_post(ex2, eyb, 1, +1, esp_pcb_t);
    retain_post(ex1, eyt, 1, -1, esp_pcb_t);
    retain_post(ex2, eyt, 1, -1, esp_pcb_t);
}

// ============================================================
//  Камера SCD41 (перегородки слева и сверху; справа/снизу — внешние
//  стенки с вентиляцией). Датчик дышит комнатой, отрезан от тёплого
//  внутреннего воздуха. Удерживается камерой + прижимом батареи.
// ============================================================
module scd_chamber() {
    ch_h = front_layer;                       // высота перегородок по Z
    x_in = scd_x - cham_clear;                // правая грань левой перегородки
    y_in = scd_y + scd_w + cham_clear;        // нижняя грань верхней перегородки
    // левая перегородка
    translate([x_in - cham_wall, wall, floor_t])
        cube([cham_wall, (y_in + cham_wall) - wall, ch_h]);
    // верхняя перегородка
    translate([x_in - cham_wall, y_in, floor_t])
        cube([(wall + inner_w) - (x_in - cham_wall), cham_wall, ch_h]);
    // маленький язычок сверху — придержать датчик до установки батареи
    translate([scd_x + 2, y_in, floor_t + scd_h])
        cube([scd_l - 4, cham_clear + lip, lip_h]);
}

// ============================================================
//  Snap-fit крышки: окошки в верхней и нижней стенках
// ============================================================
module lid_windows() {
    bz0 = lid_z0 - tab_arm;                    // где сидит зацеп
    // верхняя стенка (y high)
    for (tx = [tab_x1, tab_x2])
        translate([tx - tab_w/2 - 0.4, wall + inner_h - 0.2, bz0 - 0.4])
            cube([tab_w + 0.8, wall + 1, barb_h + 0.8]);
    // нижняя стенка (y low)
    for (tx = [tab_x1, tab_x2])
        translate([tx - tab_w/2 - 0.4, -1, bz0 - 0.4])
            cube([tab_w + 0.8, wall + 1, barb_h + 0.8]);
}

// ============================================================
//  Корпус
// ============================================================
module shell() {
    difference() {
        union() {
            difference() {
                cube([outer_w, outer_h, outer_d]);
                cavity_cut();
            }
            posts();
            scd_chamber();
        }
        window_cut();
        buttons_cut();
        usb_cut();
        vents_cut();
        lid_windows();
    }
}

// ============================================================
//  Крышка snap-fit
// ============================================================
module snap_tab(tx, side) {
    // side = +1 (верхняя стенка, зацеп +Y) | -1 (нижняя, зацеп -Y)
    wall_y = (side > 0) ? wall + inner_h : wall;
    arm_y  = (side > 0) ? wall_y - tab_t : wall_y;
    barb_y = (side > 0) ? wall_y : wall_y - barb_out;
    // плечо (вперёд по Z от плиты)
    translate([tx - tab_w/2, arm_y, lid_z0 - tab_arm])
        cube([tab_w, tab_t, tab_arm]);
    // зацеп
    translate([tx - tab_w/2, barb_y, lid_z0 - tab_arm])
        cube([tab_w, barb_out + tab_t, barb_h]);
}

module lid() {
    difference() {
        union() {
            // плита заподлицо в проёме
            translate([wall + lid_clear, wall + lid_clear, lid_z0])
                cube([inner_w - 2*lid_clear, inner_h - 2*lid_clear, lid_t]);
            // язычки
            for (tx = [tab_x1, tab_x2]) snap_tab(tx, +1);
            for (tx = [tab_x1, tab_x2]) snap_tab(tx, -1);
        }
        // палец-выемка на тыльной грани (поддеть для снятия)
        translate([outer_w/2, wall + lid_clear, outer_d + 1])
            rotate([90, 0, 0]) cylinder(d = 12, h = 6, center = true);
    }
}

// ============================================================
//  Тела модулей (для проверки посадки; в STL не идут)
// ============================================================
module ghosts(show_bat = true) {
    color("green", 0.45) translate([epd_x, epd_y, floor_t]) cube([epd_pcb_l, epd_pcb_w, epd_stack]);
    color("blue",  0.45) translate([esp_x, esp_y, floor_t]) cube([esp_l, esp_w, esp_h]);
    color("red",   0.55) translate([scd_x, scd_y, floor_t]) cube([scd_l, scd_w, scd_h]);
    if (show_bat)
        color("orange", 0.18) translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
}

// ============================================================
//  RENDER
// ============================================================
part = "all";   // "all" | "fit" | "fit_nb" | "cut" | "shell" | "lid" | "print"

if (part == "shell") {
    shell();
} else if (part == "lid") {
    lid();
} else if (part == "fit") {
    shell(); ghosts();
} else if (part == "fit_nb") {
    shell(); ghosts(show_bat = false);
} else if (part == "cut") {
    intersection() {
        union() { shell(); ghosts(); }
        translate([outer_w/2, -5, -5]) cube([outer_w, outer_h + 10, outer_d + 10]);
    }
} else if (part == "print") {
    shell();
    translate([0, outer_h + 10, -lid_z0]) lid();   // крышка рядом, на стол
} else {
    shell();
    if ($preview) ghosts();
    translate([0, 0, 16]) lid();                    // explode назад
}
