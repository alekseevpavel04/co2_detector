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
post_d      = 2.5;   // сторона штырька
post_clear  = 0.25;  // зазор штырёк–край платы (входит без люфта, без зажима)
lip         = 0.8;   // вылет язычка над платой
lip_h       = 1.0;   // высота язычка
clamp_clear = 0.1;   // зазор язычка над платой (держит, но не пережимает)

/* ---------- Кнопки (тактовые, гнездо на внутренней стороне лица) ---------- */
btn_body    = 6.0;   // сторона корпуса кнопки, TODO:verify
btn_body_h  = 3.5;   // высота корпуса кнопки за лицом, TODO:verify
btn_wall    = 1.2;   // стенка гнезда

/* ---------- Камера SCD41 ---------- */
cham_wall   = 1.5;  // толщина перегородки
cham_clear  = 0.6;  // зазор перегородка–датчик
scd_wire_w  = 6;    // вырез под кабель датчика I2C к ESP

/* ---------- Крышка: низ под губу + верх на защёлки ---------- */
lid_clear = 0.3;    // зазор крышки в проёме
lip_in    = 2.0;    // нависание нижней губы над краем крышки
lip_zt    = 1.0;    // толщина губы по Z (= глубина четверти на крышке)
lip_clear = 0.3;    // зазор заведения под губу
tab_w     = 9;      // ширина язычка-защёлки
tab_t     = 1.2;    // толщина плеча (тонкое → мягко гнётся, прощает допуск)
tab_arm   = 7;      // длина плеча (вперёд по Z)
barb_out  = 1.0;    // вылет зацепа язычка
barb_h    = 2.5;    // высота зацепа по Z
bead_in   = 1.2;    // вылет бусины-зацепа на стенке
bead_z    = 1.5;    // толщина бусины по Z

/* ---------- Полка-перегородка (двухуровневая компоновка) ---------- */
shelf_gap = 0.5;    // зазор полки над самой высокой платой (SCD41)
shelf_t   = 1.5;    // толщина полки
ledge_in  = 2.0;    // вылет опорной полочки внутрь (верх/низ стенки)
ledge_t   = 1.5;    // высота опорной полочки
wire_w    = 8;      // ширина выреза в полке под провода батареи
finger_d  = 14;     // палец-отверстие в полке (вынуть полку)
batt_rib  = 1.5;    // рёбра-держатели батарейного отсека по Y

/* ---------- Вентиляция ---------- */
vent_w = 1.6;       // ширина щели

/* ---------- Производные ---------- */
front_layer   = 8;                             // отсек электроники по Z
epd_lower_gap = 2;                             // зазор экран–нижние платы
inner_w = max(bat_l, epd_pcb_l) + 2*fit;
inner_h = max(bat_w, epd_pcb_w + max(esp_w, scd_w) + epd_lower_gap) + 2*fit;
shelf_z = floor_t + front_layer + shelf_gap;   // низ полки-перегородки
bat_z   = shelf_z + shelf_t;                   // низ батарейного отсека (= верх полки)
inner_d = (bat_z - floor_t) + bat_h;           // полная глубина полости
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
// bat_z задан выше в производных

// положения язычков крышки (по X) — симметрично, выемка-палец по центру между ними
tab_x1 = outer_w/2 - 16;
tab_x2 = outer_w/2 + 16;

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
    z_lip = floor_t + clamp_t + clamp_clear;   // язычок чуть выше платы → не пережимает
    h = z_lip + lip_h - floor_t;
    translate([cx - post_d/2, cy - post_d/2, floor_t]) cube([post_d, post_d, h]);
    if (axis == 0) {
        lx = (dir > 0) ? cx + post_d/2 : cx - post_d/2 - lip;
        translate([lx, cy - post_d/2, z_lip]) cube([lip, post_d, lip_h]);
    } else {
        ly = (dir > 0) ? cy + post_d/2 : cy - post_d/2 - lip;
        translate([cx - post_d/2, ly, z_lip]) cube([post_d, lip, lip_h]);
    }
}

module posts() {
    // e-Paper: по 2 штырька в ЗАЗОРАХ до левой и правой стенок (по центру зазора,
    // чтобы не вылезать за габарит). Язычки внутрь по X. Верх держит верхняя стенка.
    exl = (wall + epd_x) / 2;                              // центр левого зазора
    exr = (epd_x + epd_pcb_l + wall + inner_w) / 2;        // центр правого зазора
    ey1 = epd_y + 5;  ey2 = epd_y + epd_pcb_w - 5;
    retain_post(exl, ey1, 0, +1, epd_pcb_t);
    retain_post(exl, ey2, 0, +1, epd_pcb_t);
    retain_post(exr, ey1, 0, -1, epd_pcb_t);
    retain_post(exr, ey2, 0, -1, epd_pcb_t);

    // ESP32: штырьки ТОЛЬКО на обращённых в полость краях (верх + право).
    // Низ и лево у стенок (места нет) — там фиксируют стенки + полка сверху.
    eyt = esp_y + esp_w + post_clear + post_d/2;           // верхний край (в полость)
    ex1 = esp_x + 4;  ex2 = esp_x + esp_l - 4;
    retain_post(ex1, eyt, 1, -1, esp_pcb_t);
    retain_post(ex2, eyt, 1, -1, esp_pcb_t);
}

// ============================================================
//  Камера SCD41 (перегородки слева и сверху; справа/снизу — внешние
//  стенки с вентиляцией). Датчик дышит комнатой, отрезан от тёплого
//  внутреннего воздуха. Удерживается камерой + прижимом батареи.
// ============================================================
module scd_chamber() {
    ch_h = shelf_z - floor_t;                 // до низа полки (заодно опора полки)
    x_in = scd_x - cham_clear;                // правая грань левой перегородки
    y_in = scd_y + scd_w + cham_clear;        // нижняя грань верхней перегородки
    difference() {
        union() {
            // левая перегородка
            translate([x_in - cham_wall, wall, floor_t])
                cube([cham_wall, (y_in + cham_wall) - wall, ch_h]);
            // верхняя перегородка
            translate([x_in - cham_wall, y_in, floor_t])
                cube([(wall + inner_w) - (x_in - cham_wall), cham_wall, ch_h]);
        }
        // ВЫРЕЗ под кабель датчика (I2C к ESP) — в левой перегородке, у верха.
        // Без него камера запечатана и провода датчика не выйдут.
        translate([x_in - cham_wall - 1, y_in - 1 - scd_wire_w, floor_t + ch_h - 5])
            cube([cham_wall + 2, scd_wire_w, 5.5]);
    }
}

// Гнёзда под тактовые кнопки на внутренней стороне лица (рамка-локатор).
// Кнопка вставляется актуатором к отверстию, корпус упирается в лицо.
module button_mounts() {
    for (bx = [btn_cx - btn_spacing/2, btn_cx + btn_spacing/2])
        translate([bx, btn_y, floor_t])
            difference() {
                translate([-(btn_body/2 + btn_wall), -(btn_body/2 + btn_wall), 0])
                    cube([btn_body + 2*btn_wall, btn_body + 2*btn_wall, btn_body_h]);
                translate([-btn_body/2, -btn_body/2, -1])
                    cube([btn_body, btn_body, btn_body_h + 2]);
            }
}

// ============================================================
//  Удержание крышки (добавляемые элементы на корпусе)
// ============================================================
// Нижняя сплошная губа: под неё внахлёст заводится нижний край крышки.
module bottom_lip() {
    translate([wall + 4, wall, outer_d - lip_zt])
        cube([inner_w - 8, lip_in, lip_zt]);
}
// Верхние бусины-зацепы (внутренние, без наружных дырок) — за них щёлкают язычки.
module top_beads() {
    bz = lid_z0 - tab_arm + barb_h;            // нижняя грань бусины ловит верх зацепа
    for (tx = [tab_x1, tab_x2])
        translate([tx - tab_w/2 - 0.5, wall + inner_h - bead_in, bz])
            cube([tab_w + 1, bead_in, bead_z]);
}

// Опорные полочки под полку-перегородку (на верхней и нижней стенках).
// Только в зонах, где платы их не задевают (над экраном / над ESP, мимо SCD).
module shelf_ledges() {
    // верхняя стенка — во всю ширину (над экраном, Z экрана ниже)
    translate([wall, wall + inner_h - ledge_in, shelf_z - ledge_t])
        cube([inner_w, ledge_in, ledge_t]);
    // нижняя стенка — слева до зоны SCD (над ESP); справа полку держит камера SCD41
    translate([wall, wall, shelf_z - ledge_t])
        cube([scd_x - 2 - wall, ledge_in, ledge_t]);
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
            // button_mounts();  // TODO: переделать под выбранный тип кнопок (рамка не держала)
            shelf_ledges();
            bottom_lip();
            top_beads();
        }
        window_cut();
        buttons_cut();
        usb_cut();
        vents_cut();
    }
}

// ============================================================
//  Полка-перегородка (уровень 2): дно батарейного отсека,
//  закрывает электронику. Лежит на полочках, вынимается за палец.
// ============================================================
module shelf() {
    cl = 0.4;
    difference() {
        union() {
            translate([wall + cl, wall + cl, shelf_z])
                cube([inner_w - 2*cl, inner_h - 2*cl, shelf_t]);
            // рёбра-держатели батарейного отсека по Y (центрируют его).
            // ВНУТРИ плиты (не выходят за края, иначе полка упрётся в стенки).
            translate([bat_x + 3, bat_y - batt_rib, shelf_z + shelf_t])
                cube([bat_l - 6, batt_rib, 3]);
            translate([bat_x + 3, bat_y + bat_w, shelf_z + shelf_t])
                cube([bat_l - 6, batt_rib, 3]);
        }
        // вырез под провода батареи (нижний-левый, вниз к ESP)
        translate([wall + 5, wall + cl - 1, shelf_z - 1])
            cube([wire_w, 6, shelf_t + 2]);
        // палец-отверстие — поддеть и вынуть полку
        translate([outer_w/2, wall + inner_h/2, shelf_z - 1])
            cylinder(d = finger_d, h = shelf_t + 2);
    }
}

// ============================================================
//  Крышка: низ под губу (без гибки) + верх на 2 язычка-защёлки
// ============================================================
// Верхний язычок: плечо вперёд по Z + зацеп наружу (+Y) под бусину стенки.
module top_tab(tx) {
    y_edge = wall + inner_h - lid_clear;       // верхний край плиты
    translate([tx - tab_w/2, y_edge - tab_t, lid_z0 - tab_arm])
        cube([tab_w, tab_t, tab_arm]);                       // плечо
    translate([tx - tab_w/2, y_edge, lid_z0 - tab_arm])
        cube([tab_w, barb_out, barb_h]);                     // зацеп наружу
}

module lid() {
    notch_d = 12;
    difference() {
        union() {
            // плита: низ доведён почти до стенки (под губу), верх с зазором
            translate([wall + lid_clear, wall + lip_clear, lid_z0])
                cube([inner_w - 2*lid_clear,
                      inner_h - lip_clear - lid_clear, lid_t]);
            for (tx = [tab_x1, tab_x2]) top_tab(tx);
        }
        // четверть на нижнем крае — под нижнюю губу (получается заподлицо)
        translate([-1, wall - 1, outer_d - lip_zt])
            cube([outer_w + 2, (wall + lip_in + lip_clear) - (wall - 1), lip_zt + 1]);
        // палец-выемка на верхнем крае — поддеть для снятия
        translate([outer_w/2, wall + inner_h, outer_d + 0.5])
            rotate([90, 0, 0]) cylinder(d = notch_d, h = 8, center = true);
    }
}

// ============================================================
//  Тела модулей (для проверки посадки; в STL не идут)
// ============================================================
module ghosts(show_bat = true, show_shelf = true) {
    color("green", 0.45) translate([epd_x, epd_y, floor_t]) cube([epd_pcb_l, epd_pcb_w, epd_stack]);
    color("blue",  0.45) translate([esp_x, esp_y, floor_t]) cube([esp_l, esp_w, esp_h]);
    color("red",   0.55) translate([scd_x, scd_y, floor_t]) cube([scd_l, scd_w, scd_h]);
    if (show_shelf) color("gray", 0.30) shelf();
    if (show_bat)
        color("orange", 0.18) translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
}

// ============================================================
//  RENDER
// ============================================================
part = "all";   // "all" | "fit" | "fit_nb" | "cut" | "shell" | "shelf" | "lid" | "print"

if (part == "shell") {
    shell();
} else if (part == "shelf") {
    shelf();
} else if (part == "lid") {
    lid();
} else if (part == "fit") {
    shell(); ghosts();
} else if (part == "fit_nb") {
    shell(); ghosts(show_bat = false, show_shelf = false);   // видна электроника
} else if (part == "cut") {
    intersection() {
        union() { shell(); ghosts(); }
        translate([outer_w/2, -5, -5]) cube([outer_w, outer_h + 10, outer_d + 10]);
    }
} else if (part == "print") {
    // три детали на плоскости для заказа STL
    shell();
    translate([0, -(outer_h + 10), -shelf_z]) shelf();
    translate([0,  (outer_h + 10), -lid_z0])  lid();
} else {
    shell();
    if ($preview) ghosts();
    translate([0, 0, 16]) lid();                    // explode назад
}
