// ============================================================
//  Air Quality Monitor — корпус (v5, переработка с нуля)
// ============================================================
//  Горизонтальный корпус. Экран слева, 1 кнопка справа.
//  Датчик SCD41 сенсором в отверстие лица (мембрана дышит комнатой).
//  Доступ к выключателю батарей — внутри отсека (за крышкой).
//  В модели — РЕАЛЬНЫЕ тела плат С ПИНАМИ И ДЮПОН-МАМА (проверка
//  влезаемости). Рельефная надпись на верхней стенке.
//
//  ⚠️ Дюпон-мама на пинах ESP/SCD торчит ~13 мм → отсек электроники
//  получается ГЛУБОКИМ. Это видно на рендере (part="fit"/"cut").
//  Хочешь тоньше — угловые (right-angle) пины или пайка проводов
//  напрямую; тогда уменьшить chamber_d.
//
//  Размеры с TODO:verify — проверить калипером.
//  Рендер: openscad -D 'part="fit"' -o fit.png --preview ...
//          openscad -D 'part="print"' -o case.stl case.scad   (3 объекта)
// ============================================================

$fn = 32;

/* ---------- Стенки / допуски ---------- */
wall    = 2.0;
floor_t = 2.0;
lid_t   = 2.0;
fit     = 0.4;

/* ---------- e-Paper 2.13" (TODO:verify) ---------- */
epd_l = 65; epd_w = 30.2; epd_t = 1.2; epd_back = 4;   // плата + компоненты сзади
act_w = 48.55; act_h = 23.71;          // активная зона
act_ox = 8; act_oy = 3;                // смещение зоны от края платы
// шлейф тонкий — глубокого места не требует

/* ---------- Кнопка тактовая 12×12, актуатор Ø7 (TODO:verify) ---------- */
btn      = 12;       // корпус
btn_can  = 5;        // высота корпуса за лицом (без актуатора), TODO:verify
act_d    = 7;        // актуатор
btn_legs = 5;        // ноги + провода за корпусом

/* ---------- SCD41 модуль (TODO:verify) ---------- */
scd_l = 22; scd_w = 15; scd_t = 1.2;   // плата
can   = 10; can_h = 7;                  // металлический сенсор (мембрана сверху)
scd_hdr_n = 4;                          // линейка 1×4 на краю

/* ---------- ESP32-S3 SuperMini (TODO:verify) ---------- */
esp_l = 22.52; esp_w = 18; esp_h = 6;
usb_w = 9.5; usb_h = 4;

/* ---------- Разъёмы: дюпон-мама на пинах ---------- */
hdr_h   = 6;     // высота пина над платой
dup_h   = 13;    // длина дюпон-мама вдоль пина (торчит от платы)
dup_w   = 2.6;   // толщина корпуса дюпона

/* ---------- Глубина отсека электроники (платы + дюпоны) ---------- */
// Главный потребитель глубины — вертикальные дюпоны на ESP/SCD.
chamber_d = 20;  // TODO: уменьшить, если пины угловые/пайка напрямую

/* ---------- Батарейный отсек 3xAA + выключатель (TODO:verify) ---------- */
bat_l = 70; bat_w = 49; bat_h = 20;
sw_l = 10; sw_w = 7; sw_h = 4;          // бугорок выключателя на боксе

/* ---------- Крепёж плат (штырьки-защёлки) ---------- */
post = 2.5; pclear = 0.25; lip = 0.8; lip_h = 1.0; clampc = 0.1;

/* ---------- Камера SCD41 ---------- */
cham_wall = 1.5;

/* ---------- Полка-перегородка ---------- */
shelf_t = 1.5; shelf_gap = 0.5; ledge_in = 2.0; ledge_t = 1.5;
wire_w = 8; finger_d = 14; batt_rib = 1.5;

/* ---------- Крышка (низ под губу + верх на защёлки) ---------- */
lid_clear = 0.3; lip_in = 2.0; lip_zt = 1.0; lip_clear = 0.3;
tab_w = 9; tab_t = 1.2; tab_arm = 7; barb_out = 1.0; barb_h = 2.5;
bead_in = 1.2; bead_z = 1.5;

/* ---------- Вентиляция / надпись ---------- */
vent_w = 1.6;
label  = "CO2";

// ============================================================
//  Производная компоновка (лицо: X — ширина, Y — высота, Z — глубина)
// ============================================================
gap = 3;                                 // зазор экран–кнопка
// верхняя полоса: экран + кнопка; нижняя: ESP + SCD
top_band = max(epd_w, btn);
bot_band = max(esp_w, scd_w);
inner_w  = max(bat_l, epd_l + gap + btn + 2) + 2*fit;
inner_h  = max(bat_w, top_band + 2 + bot_band) + 2*fit;
outer_w  = inner_w + 2*wall;
outer_h  = inner_h + 2*wall;

shelf_z = floor_t + chamber_d + shelf_gap;
bat_z   = shelf_z + shelf_t;
inner_d = (bat_z - floor_t) + bat_h;
lid_z0  = floor_t + inner_d;
outer_d = lid_z0 + lid_t;

// --- Позиции (Z=0 — внешняя сторона лица) ---
// e-Paper: верх-лево, прижата к лицу (экран в окно)
epd_x = wall + fit;
epd_y = wall + inner_h - epd_w - fit;
win_x = epd_x + act_ox;
win_y = epd_y + act_oy;

// Кнопка: верх-право, по центру верхней полосы
btn_cx = wall + epd_l + gap + btn/2 + fit;
btn_cy = epd_y + epd_w/2;

// ESP: низ-лево
esp_x = wall + fit;
esp_y = wall + fit;

// SCD: низ-право, сенсор в отверстие лица
scd_x = wall + inner_w - scd_l - fit;
scd_y = wall + fit;
can_cx = scd_x + scd_l/2;                // центр сенсора (предположительно центр платы)
can_cy = scd_y + scd_w/2;

// Батарея: по центру; выключатель к тыльной грани (к крышке)
bat_x = wall + (inner_w - bat_l)/2;
bat_y = wall + (inner_h - bat_w)/2;

// ============================================================
//  ТЕЛА МОДУЛЕЙ + ПИНЫ + ДЮПОН (ghosts, в STL не идут)
// ============================================================
// Линейка пинов с дюпон-мама, торчит в +Z от платы, начиная с z0.
module header_dupont(x0, y0, z0, n) {
    pitch = 2.54;
    for (i = [0:n-1])
        translate([x0 + i*pitch, y0, z0]) {
            color("silver") cylinder(d = 0.64, h = hdr_h);          // пин
            color("dimgray") translate([-pitch/2, -dup_w/2, hdr_h - dup_h + 2])
                cube([pitch, dup_w, dup_h]);                         // дюпон-мама
        }
}

module ghosts(show_bat = true, show_shelf = true) {
    // e-Paper (плата + экран + компоненты сзади + тонкий шлейф)
    color("green", 0.5) translate([epd_x, epd_y, floor_t]) cube([epd_l, epd_w, epd_t]);
    color("dimgray", 0.5) translate([epd_x+2, epd_y+2, floor_t+epd_t]) cube([epd_l-4, epd_w-4, epd_back]);
    color("white", 0.6) translate([win_x, win_y, -0.1]) cube([act_w, act_h, floor_t+0.2]);

    // ESP32 + дюпоны на двух длинных краях
    color("blue", 0.5) translate([esp_x, esp_y, floor_t]) cube([esp_l, esp_w, esp_h]);
    header_dupont(esp_x + 2, esp_y + 1.5,        floor_t + esp_h, 6);
    header_dupont(esp_x + 2, esp_y + esp_w - 1.5, floor_t + esp_h, 6);

    // SCD41: плата на глубине can_h, сенсор смотрит в лицо (-Z), пины 1×4 в +Z
    color("teal", 0.5) translate([scd_x, scd_y, floor_t + can_h]) cube([scd_l, scd_w, scd_t]);
    color("silver") translate([can_cx - can/2, can_cy - can/2, floor_t]) cube([can, can, can_h]); // сенсор
    header_dupont(scd_x + scd_l - 2, scd_y + scd_w/2 - (scd_hdr_n*2.54)/2, floor_t + can_h + scd_t, scd_hdr_n);

    // Кнопка: корпус + актуатор (в лицо) + ноги/провода сзади
    color("black", 0.6) translate([btn_cx - btn/2, btn_cy - btn/2, floor_t]) cube([btn, btn, btn_can]);
    color("dimgray") translate([btn_cx, btn_cy, -3]) cylinder(d = act_d, h = btn_can + 3);
    color("dimgray", 0.6) translate([btn_cx - btn/2, btn_cy - btn/2, floor_t + btn_can]) cube([btn, btn, btn_legs]);

    // Полка
    if (show_shelf) color("gray", 0.3) shelf();
    // Батарея + бугорок выключателя у тыла (к крышке)
    if (show_bat) {
        color("orange", 0.18) translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
        color("red", 0.4) translate([bat_x + bat_l/2 - sw_l/2, bat_y + 2, bat_z + bat_h])
            cube([sw_l, sw_w, sw_h]);   // выключатель смотрит к крышке
    }
}

// ============================================================
//  Вырезы и фичи корпуса
// ============================================================
module cavity_cut() {
    translate([wall, wall, floor_t]) cube([inner_w, inner_h, outer_d]);
}
module window_cut() {
    translate([win_x, win_y, -1]) cube([act_w, act_h, floor_t + 2]);
}
module button_hole() {
    translate([btn_cx, btn_cy, -1]) cylinder(d = act_d + 0.6, h = floor_t + 2);
}
module scd_port() {
    translate([can_cx, can_cy, -1]) cylinder(d = can + 1, h = floor_t + 2);  // сенсор в лицо
}
module usb_cut() {
    translate([-1, esp_y + esp_w/2 - usb_w/2, floor_t + (esp_h - usb_h)/2])
        cube([wall + 3, usb_w, usb_h]);
}
// Вторая вентиляция (сквозняк) — щели в правой стенке у датчика
module vents_cut() {
    for (i = [0:2])
        translate([wall + inner_w - 1, scd_y + 3 + i*4, floor_t + 2])
            cube([wall + 2, vent_w, 6]);
}

// Гнездо кнопки: рамка 12×12 + 2 язычка прижимают корпус к лицу
module button_mount() {
    h = btn_can + clampc + lip_h;
    translate([btn_cx, btn_cy, floor_t]) {
        difference() {
            translate([-(btn/2+wall), -(btn/2+wall), 0]) cube([btn+2*wall, btn+2*wall, h]);
            translate([-btn/2, -btn/2, -1]) cube([btn, btn, h+2]);
        }
        translate([-btn/2, btn/2 - lip, btn_can+clampc]) cube([btn, lip, lip_h]);
        translate([-btn/2, -btn/2,      btn_can+clampc]) cube([btn, lip, lip_h]);
    }
}

// Сиденье SCD41: плата опирается на САМ СЕНСОР (он стоит в отверстии лица как
// стойка высотой can_h). Камера локализует плату по бокам; 2 язычка от верхней
// перегородки камеры прижимают плату, чтобы сенсор не вышел из отверстия.
module scd_seat() {
    zt  = floor_t + can_h + scd_t + clampc;   // чуть выше платы
    y_w = scd_y + scd_w + 0.6;                // внутренняя грань верхней перегородки
    for (x = [scd_x + 3, scd_x + scd_l - 3])
        translate([x - 2, scd_y + scd_w - lip, zt])
            cube([4, (y_w + 0.5) - (scd_y + scd_w - lip), lip_h]);
}

// Камера SCD41: перегородки (лево/верх) + вырез под кабель к ESP
module scd_chamber() {
    ch_h = shelf_z - floor_t;
    x_in = scd_x - 0.6;
    y_in = scd_y + scd_w + 0.6;
    difference() {
        union() {
            translate([x_in - cham_wall, wall, floor_t]) cube([cham_wall, (y_in+cham_wall)-wall, ch_h]);
            translate([x_in - cham_wall, y_in, floor_t]) cube([(wall+inner_w)-(x_in-cham_wall), cham_wall, ch_h]);
        }
        translate([x_in - cham_wall - 1, y_in - 1 - 6, floor_t + ch_h - 6]) cube([cham_wall+2, 6, 6.5]);
    }
}

// Опоры полки (верхняя стенка во всю ширину; нижняя — слева, мимо камеры SCD)
module shelf_ledges() {
    translate([wall, wall + inner_h - ledge_in, shelf_z - ledge_t]) cube([inner_w, ledge_in, ledge_t]);
    translate([wall, wall, shelf_z - ledge_t]) cube([scd_x - 2 - wall, ledge_in, ledge_t]);
}
module bottom_lip() {
    translate([wall + 4, wall, outer_d - lip_zt]) cube([inner_w - 8, lip_in, lip_zt]);
}
module top_beads() {
    bz = lid_z0 - tab_arm + barb_h;
    for (tx = [outer_w/2 - 16, outer_w/2 + 16])
        translate([tx - tab_w/2 - 0.5, wall + inner_h - bead_in, bz]) cube([tab_w+1, bead_in, bead_z]);
}

// Рельефная надпись на верхней стенке (выпуклая, вросшая в стенку → один объект)
module emboss() {
    translate([outer_w/2, outer_h - 0.5, outer_d * 0.45])
        rotate([-90, 0, 0])
            linear_extrude(1.1)        // от y=outer_h-0.5 (внутри) до +0.6 наружу
                text(label, size = 9, halign = "center", valign = "center");
}

// ============================================================
//  Корпус
// ============================================================
module shell() {
    union() {
        difference() {
            union() {
                difference() {
                    cube([outer_w, outer_h, outer_d]);
                    cavity_cut();
                }
                scd_chamber();
                button_mount();
                scd_seat();
                shelf_ledges();
                bottom_lip();
                top_beads();
            }
            window_cut();
            button_hole();
            scd_port();
            usb_cut();
            vents_cut();
        }
        // надпись выпуклая — добавляем поверх (не вычитаем)
        emboss();
    }
}

// ============================================================
//  Полка-перегородка
// ============================================================
module shelf() {
    cl = 0.4;
    difference() {
        union() {
            translate([wall + cl, wall + cl, shelf_z]) cube([inner_w - 2*cl, inner_h - 2*cl, shelf_t]);
            // рёбра-держатели батареи (по Y), внутри плиты
            translate([bat_x + 3, bat_y - batt_rib, shelf_z + shelf_t]) cube([bat_l - 6, batt_rib, 3]);
            translate([bat_x + 3, bat_y + bat_w,    shelf_z + shelf_t]) cube([bat_l - 6, batt_rib, 3]);
            // рёбра-держатели батареи (по X) — т.к. корпус шире батареи
            translate([bat_x - batt_rib, bat_y + 3, shelf_z + shelf_t]) cube([batt_rib, bat_w - 6, 3]);
            translate([bat_x + bat_l,    bat_y + 3, shelf_z + shelf_t]) cube([batt_rib, bat_w - 6, 3]);
        }
        translate([wall + 5, wall + cl - 1, shelf_z - 1]) cube([wire_w, 6, shelf_t + 2]);   // провода
        translate([outer_w/2, wall + inner_h/2, shelf_z - 1]) cylinder(d = finger_d, h = shelf_t + 2);
    }
}

// ============================================================
//  Крышка (низ под губу + 2 язычка-защёлки сверху)
// ============================================================
module top_tab(tx) {
    y_edge = wall + inner_h - lid_clear;
    translate([tx - tab_w/2, y_edge - tab_t, lid_z0 - tab_arm]) cube([tab_w, tab_t, tab_arm]);
    translate([tx - tab_w/2, y_edge, lid_z0 - tab_arm]) cube([tab_w, barb_out, barb_h]);
}
module lid() {
    difference() {
        union() {
            translate([wall + lid_clear, wall + lip_clear, lid_z0])
                cube([inner_w - 2*lid_clear, inner_h - lip_clear - lid_clear, lid_t]);
            for (tx = [outer_w/2 - 16, outer_w/2 + 16]) top_tab(tx);
        }
        translate([-1, wall - 1, outer_d - lip_zt])
            cube([outer_w + 2, (wall + lip_in + lip_clear) - (wall - 1), lip_zt + 1]);
        translate([outer_w/2, wall + inner_h, outer_d + 0.5]) rotate([90,0,0]) cylinder(d = 12, h = 8, center = true);
    }
}

// ============================================================
//  RENDER
// ============================================================
part = "all";   // "all" | "fit" | "fit_nb" | "cut" | "shell" | "shelf" | "lid" | "print"

if (part == "shell") shell();
else if (part == "shelf") shelf();
else if (part == "lid") lid();
else if (part == "fit") { shell(); ghosts(); }
else if (part == "fit_nb") { shell(); ghosts(show_bat=false, show_shelf=false); }
else if (part == "cut") {
    intersection() {
        union() { shell(); ghosts(); }
        translate([outer_w/2, -5, -5]) cube([outer_w, outer_h+10, outer_d+10]);
    }
} else if (part == "print") {
    shell();
    translate([0, -(outer_h + 8), -shelf_z]) shelf();
    translate([0,  (outer_h + 8), -lid_z0])  lid();
} else {
    shell();
    if ($preview) ghosts();
    translate([0, 0, 18]) lid();
}
