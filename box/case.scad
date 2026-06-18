// ============================================================
//  Air Quality Monitor — корпус (v6, стилевая переработка)
// ============================================================
//  Вариант B: чистое лицо — экран по центру + решётка датчика под ним.
//  Кнопка на ЛЕВОМ торце. Корпус сильно скруглён (R8 по вертик. рёбрам),
//  окно экрана со скруглёнными углами + утопленная рамка.
//  Размеры компонентов — замеренные (см. DIMENSIONS.md). Запасы заложены.
//  3 объекта в одном STL (корпус, полка, крышка). Ghosts с пинами+дюпоном.
// ============================================================

$fn = 32;

/* ---------- Стенки / допуски / стиль ---------- */
wall = 2.0; floor_t = 2.0; lid_t = 2.0;
fit  = 0.5;                 // зазор плата↔стенка
r_v  = 10;                  // скругление вертикальных рёбер (сильнее)
chamf = 2.5;                // фаска переднего ребра
r_win = 2.5;                // скругление углов окна экрана
bez_w = 3.5; bez_d = 1.0;   // утопленная рамка вокруг экрана (ширина, глубина)
win_margin = 1.0;           // окно крупнее активной зоны (допуск на смещение)

/* ---------- Запасы на посадки/вырезы ---------- */
btn_clear = 0.5; btn_hole_clr = 1.0;

/* ---------- e-Paper 2.13" ---------- */
epd_l = 65; epd_w = 30.2; epd_t = 1.2; epd_back = 4;
act_w = 48.55; act_h = 23.71;
act_ox = 5; act_oy = 3.25;   // смещение активной зоны от края платы

/* ---------- Кнопка тактовая 12×12 (на лице, справа-снизу) ---------- */
btn = 12; btn_can = 3.3; act_d = 6.55; act_out = 8.25; btn_legs = 3.5;

/* ---------- SCD41 (по центру под экраном, сенсор за решёткой) ---------- */
scd_l = 21.6; scd_w = 13.4; scd_t = 1.6;
can = 8; can_h = 6.5; scd_can_off = 2; scd_hdr_n = 4;

/* ---------- ESP32-S3 SuperMini (справа-снизу) ---------- */
esp_l = 23.5; esp_w = 18; esp_h = 6;
usb_w = 11; usb_h = 5.5;

/* ---------- Дюпон-мама ---------- */
hdr_h = 6; dup_h = 17; dup_w = 2.5;

/* ---------- Глубина отсека электроники (дюпоны 17 + запас) ---------- */
chamber_d = 27;

/* ---------- Батарея 3xAA + отдельный выключатель ---------- */
bat_l = 58; bat_w = 48; bat_h = 17;
sw_slot_w = 7.5; sw_slot_h = 3.5;        // паз под слайдер
// выключатель целиком (корпус+планка+пины) — для проверки места внутри, TODO:verify
sw_body_t = 7;   // вглубь от стенки (корпус+пины)
sw_body_l = 13;  // вдоль стенки (планка с ушками)
sw_body_h = 9;   // высота

/* ---------- Камера SCD41 / решётка ---------- */
cham_wall = 1.5;
grille_n = 9; grille_slot_w = 1.8; grille_pitch = 3.4; grille_slot_h = 12;   // больше/выше — вентиляция

/* ---------- Полка ---------- */
shelf_t = 1.5; shelf_gap = 1.0; ledge_in = 2.0; ledge_t = 1.5;
wire_w = 10; finger_d = 14; batt_rib = 1.5;

/* ---------- Крышка ---------- */
lid_clear = 0.4; lip_in = 2.0; lip_zt = 1.0; lip_clear = 0.4;
tab_w = 9; tab_t = 1.2; tab_arm = 7; barb_out = 1.0; barb_h = 2.5;
bead_in = 1.2; bead_z = 1.5;
post = 2.5; lip = 0.8; lip_h = 1.0; clampc = 0.1;

label = "CO2";

// ============================================================
//  Производная компоновка
// ============================================================
inner_w = 82;                                   // шире — чтобы лицо вышло симметричным
inner_h = max(bat_w, epd_w + 6 + max(esp_w, scd_w)) + 2*fit;   // зазор экран↔низ
outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;

shelf_z = floor_t + chamber_d + shelf_gap;
bat_z   = shelf_z + shelf_t;
inner_d = (bat_z - floor_t) + bat_h;
lid_z0  = floor_t + inner_d;
outer_d = lid_z0 + lid_t;

// Окно (активная зона) — по центру X, в верхней части
win_x = (outer_w - act_w)/2;
win_y = wall + inner_h - act_h - 5;
epd_x = win_x - act_ox;
epd_y = win_y - act_oy;

// Решётка датчика — по центру X, ПОД экраном (ниже, с явным зазором)
grille_cx = outer_w/2;
grille_cy = 13;
can_cx = grille_cx;
can_cy = grille_cy;
scd_x  = can_cx - (scd_l - scd_can_off - can/2);
scd_y  = can_cy - scd_w/2;

// ESP — низ-ЛЕВО, USB в ЛЕВЫЙ торец (освобождает право под кнопку)
esp_x = wall + fit;
esp_y = wall + fit;

// Кнопка — на ЛИЦЕ справа-снизу; надпись — слева-снизу (зеркально, для баланса)
btn_cx   = grille_cx + 24;
btn_cy   = grille_cy;
label_cx = grille_cx - 24;
label_cy = grille_cy;

// Батарея — по центру
bat_x = wall + (inner_w - bat_l)/2;
bat_y = wall + (inner_h - bat_w)/2;

// ============================================================
//  Помощники формы
// ============================================================
module rrect(w, h, r) {
    hull() for (x = [r, w-r]) for (y = [r, h-r]) translate([x, y]) circle(r = r, $fn = 28);
}
module rbody(w, h, d, r) {                      // призма со скруглёнными вертик. рёбрами
    linear_extrude(d) rrect(w, h, r);
}
module outer_body() {                           // корпус: скругление + фаска переднего ребра
    hull() {                                    // фаска у лица z=0..chamf
        linear_extrude(0.01) offset(-chamf) rrect(outer_w, outer_h, r_v);
        translate([0, 0, chamf]) linear_extrude(0.01) rrect(outer_w, outer_h, r_v);
    }
    translate([0, 0, chamf]) rbody(outer_w, outer_h, outer_d - chamf, r_v);
}

// ============================================================
//  Ghosts (тела + пины + дюпон)
// ============================================================
module header_dupont(x0, y0, z0, n) {
    pitch = 2.54;
    for (i = [0:n-1]) translate([x0 + i*pitch, y0, z0]) {
        color("silver") cylinder(d = 0.64, h = hdr_h);
        color("dimgray") translate([-pitch/2, -dup_w/2, 0]) cube([pitch, dup_w, dup_h]);
    }
}
module ghosts(show_bat = true, show_shelf = true) {
    color("green",0.5) translate([epd_x, epd_y, floor_t]) cube([epd_l, epd_w, epd_t]);
    color("dimgray",0.5) translate([epd_x+2, epd_y+2, floor_t+epd_t]) cube([epd_l-4, epd_w-4, epd_back]);
    color("white",0.7) translate([win_x, win_y, -0.1]) cube([act_w, act_h, floor_t+0.2]);
    header_dupont(epd_x+8, epd_y+1.5, floor_t+epd_t, 8);

    color("blue",0.5) translate([esp_x, esp_y, floor_t]) cube([esp_l, esp_w, esp_h]);
    header_dupont(esp_x+2, esp_y+1.5, floor_t+esp_h, 6);
    header_dupont(esp_x+2, esp_y+esp_w-1.5, floor_t+esp_h, 6);

    color("teal",0.5) translate([scd_x, scd_y, floor_t+can_h]) cube([scd_l, scd_w, scd_t]);
    color("silver") translate([can_cx-can/2, can_cy-can/2, floor_t]) cube([can, can, can_h]);
    header_dupont(scd_x+2, scd_y+scd_w/2-(scd_hdr_n*2.54)/2, floor_t+can_h+scd_t, scd_hdr_n);

    // кнопка на ЛИЦЕ: корпус за лицом, актуатор наружу (-Z)
    translate([btn_cx, btn_cy, 0]) {
        color("black",0.6) translate([-btn/2, -btn/2, floor_t]) cube([btn, btn, btn_can]);
        color("dimgray") translate([0,0,-act_out]) cylinder(d=act_d, h=act_out+floor_t+1);
    }

    if (show_shelf) color("gray",0.3) shelf();
    if (show_bat) {
        color("orange",0.18) translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
        // выключатель (слайдер с планкой) у правой стенки, в зоне батареи
        color("dimgray",0.6) translate([wall+inner_w-sw_body_t, wall+inner_h/2-sw_body_l/2, bat_z+bat_h/2-sw_body_h/2])
            cube([sw_body_t, sw_body_l, sw_body_h]);
    }
}

// ============================================================
//  Вырезы лица
// ============================================================
module window_cut() {
    // утопленная рамка
    translate([win_x - win_margin - bez_w, win_y - win_margin - bez_w, -0.01])
        linear_extrude(bez_d + 0.01)
            rrect(act_w + 2*win_margin + 2*bez_w, act_h + 2*win_margin + 2*bez_w, r_win + 1.5);
    // само окно (скруглённое) насквозь
    translate([win_x - win_margin, win_y - win_margin, -1])
        linear_extrude(floor_t + 2)
            rrect(act_w + 2*win_margin, act_h + 2*win_margin, r_win);
}
// Решётка датчика — вертикальные щели со скруглёнными концами, по центру над сенсором
module grille_cut() {
    x0 = grille_cx - (grille_n-1)*grille_pitch/2;
    for (i = [0:grille_n-1])
        translate([x0 + i*grille_pitch, grille_cy, -1])
            linear_extrude(floor_t + 2)
                rrect(grille_slot_w, grille_slot_h, grille_slot_w/2);
}
module scd_port_cut() {   // отверстие под сенсор за решёткой (камера дышит)
    translate([can_cx, can_cy, -1]) cylinder(d = can + 1.5, h = floor_t + 2);
}
module usb_cut() {        // ЛЕВЫЙ торец (ESP слева)
    translate([-1, esp_y + esp_w/2 - usb_w/2, floor_t + (esp_h - usb_h)/2])
        cube([wall + 2, usb_w, usb_h]);
}
module button_hole() {    // на ЛИЦЕ, справа-снизу
    translate([btn_cx, btn_cy, -1]) cylinder(d = act_d + btn_hole_clr, h = floor_t + 2);
}
module switch_slot() {    // правый торец, у батарейного отсека (под слайдер)
    translate([outer_w - wall - 1, wall + inner_h/2 - sw_slot_w/2, bat_z + bat_h/2 - sw_slot_h/2])
        cube([wall + 2, sw_slot_w, sw_slot_h]);
}
module vents_side() {     // сквозняк — щели в нижней стенке у датчика
    for (i = [-1:1])
        translate([grille_cx + i*4 - vents_w()/2, -1, floor_t + 2]) cube([vents_w(), wall + 2, 5]);
}
function vents_w() = 1.6;

// ============================================================
//  Внутренние фичи
// ============================================================
// Простые ЗАСТУПЫ (полочки) под детали: поставил на них и приклеил. Без подгонки.
// Все начинаются чуть в стенке (z0) — чистый union, ничего не висит.
module ledges() {
    ld = 4;    // вылет заступа вглубь (+Z)
    rt = 2;    // толщина заступа (по Y)
    sr = 2;    // боковые рёбра-локаторы (только для экрана — точно к окну)
    z0 = floor_t - 0.5;
    // e-Paper: нижний заступ + 2 боковых ребра (внахлёст с заступом)
    translate([epd_x, epd_y - rt, z0])             cube([epd_l, rt, ld + 0.5]);
    translate([epd_x - sr, epd_y - rt, z0])        cube([sr, epd_w + rt, ld + 0.5]);
    translate([epd_x + epd_l, epd_y - rt, z0])     cube([sr, epd_w + rt, ld + 0.5]);
    // ESP: нижний заступ
    translate([esp_x, esp_y - rt, z0])             cube([esp_l, rt, ld + 0.5]);
    // SCD: веб-стойка от лица до глубины сенсора, плата опирается СВЕРХУ (не висит)
    translate([scd_x, scd_y - rt, z0])             cube([scd_l, rt, can_h + 0.5]);
    // Кнопка: заступ под корпус
    translate([btn_cx - btn/2, btn_cy - btn/2 - rt, z0]) cube([btn, rt, ld + 0.5]);
}
module scd_chamber() {    // перегородки лево/верх + вырез под кабель
    ch_h = shelf_z - floor_t;
    x_in = scd_x - 0.6; y_in = scd_y + scd_w + 0.6;
    difference() {
        union() {
            translate([x_in - cham_wall, wall, floor_t]) cube([cham_wall, (y_in+cham_wall)-wall, ch_h]);
            translate([x_in - cham_wall, y_in, floor_t]) cube([scd_l + 2*cham_wall + 1, cham_wall, ch_h]);
        }
        translate([x_in - cham_wall - 1, y_in - 1 - 6, floor_t + ch_h - 6]) cube([cham_wall+2, 6, 6.5]);
    }
}
module shelf_ledges() {
    translate([wall, wall + inner_h - ledge_in, shelf_z - ledge_t]) cube([inner_w, ledge_in, ledge_t]);
    translate([wall, wall, shelf_z - ledge_t]) cube([scd_x - 2 - wall, ledge_in, ledge_t]);
}
module bottom_lip() {
    translate([wall + 4, wall, outer_d - lip_zt]) cube([inner_w - 8, lip_in, lip_zt]);
}
module top_windows() {   // окошки в верхней стенке под язычки крышки (защёлка)
    for (tx = [outer_w/2 - 16, outer_w/2 + 16])
        translate([tx - tab_w/2 - 0.4, wall + inner_h - 0.5, lid_z0 - tab_arm - 0.4])
            cube([tab_w + 0.8, wall + 1.5, barb_h + 0.8]);
}
module emboss() {         // ВЫПУКЛАЯ надпись СНАРУЖИ лица, слева-снизу (зеркально кнопке)
    translate([label_cx, label_cy, -0.8])
        linear_extrude(1.3) text(label, size = 6, halign = "center", valign = "center");
}

// ============================================================
//  Корпус
// ============================================================
module shell() {
    difference() {
        union() {
            // полая скруглённая оболочка
            difference() {
                outer_body();
                translate([wall, wall, floor_t]) rbody(inner_w, inner_h, outer_d, max(r_v - wall, 1));
            }
            // внутренние фичи ОБРЕЗАНЫ по форме корпуса (не вылезают за скругления)
            intersection() {
                union() {
                    scd_chamber(); ledges();
                    shelf_ledges(); bottom_lip();
                }
                rbody(outer_w, outer_h, outer_d, r_v);
            }
            emboss();   // выпуклая надпись наружу — не обрезаем
        }
        window_cut(); grille_cut();
        usb_cut(); button_hole(); switch_slot(); vents_side();
        top_windows();   // окошки защёлок крышки
    }
}

// ============================================================
//  Полка / крышка (скруглённые под форму)
// ============================================================
module shelf() {
    cl = 0.4;
    difference() {
        intersection() {                          // всё в пределах формы полки
            union() {
                translate([wall + cl, wall + cl, shelf_z]) rbody(inner_w - 2*cl, inner_h - 2*cl, shelf_t, max(r_v-wall-cl,1));
                translate([bat_x + 3, bat_y - batt_rib, shelf_z + shelf_t]) cube([bat_l - 6, batt_rib, 3]);
                translate([bat_x + 3, bat_y + bat_w,    shelf_z + shelf_t]) cube([bat_l - 6, batt_rib, 3]);
                translate([bat_x - batt_rib, bat_y + 3, shelf_z + shelf_t]) cube([batt_rib, bat_w - 6, 3]);
                translate([bat_x + bat_l,    bat_y + 3, shelf_z + shelf_t]) cube([batt_rib, bat_w - 6, 3]);
            }
            translate([wall + cl, wall + cl, shelf_z]) rbody(inner_w - 2*cl, inner_h - 2*cl, shelf_t + 4, max(r_v-wall-cl,1));
        }
        translate([wall + 5, wall + cl - 1, shelf_z - 1]) cube([wire_w, 6, shelf_t + 2]);
        translate([outer_w/2, wall + inner_h/2, shelf_z - 1]) cylinder(d = finger_d, h = shelf_t + 2);
    }
}
module top_tab(tx) {
    y_e = wall + inner_h - lid_clear;       // верхний край плиты
    // плечо: вперёд (-Z) вдоль верхней стенки, соединено с плитой
    translate([tx - tab_w/2, y_e - tab_t, lid_z0 - tab_arm]) cube([tab_w, tab_t, tab_arm]);
    // зацеп: торчит +Y СКВОЗЬ окошко стенки и ловит его передний край
    translate([tx - tab_w/2, y_e, lid_z0 - tab_arm]) cube([tab_w, wall + lid_clear + 0.8, barb_h]);
}
module lid() {
    cl = lid_clear;
    difference() {
        union() {
            translate([wall + cl, wall + lip_clear, lid_z0])
                rbody(inner_w - 2*cl, inner_h - lip_clear - cl, lid_t, max(r_v-wall-cl,1));
            for (tx = [outer_w/2 - 16, outer_w/2 + 16]) top_tab(tx);
        }
        translate([-1, wall - 1, outer_d - lip_zt]) cube([outer_w + 2, (wall+lip_in+lip_clear)-(wall-1), lip_zt+1]);
        translate([outer_w/2, wall + inner_h, outer_d + 0.5]) rotate([90,0,0]) cylinder(d = 12, h = 8, center = true);
    }
}

// ============================================================
//  RENDER
// ============================================================
part = "all";   // all | fit | fit_nb | cut | shell | shelf | lid | print

if (part == "shell") shell();
else if (part == "shelf") shelf();
else if (part == "lid") lid();
else if (part == "fit") { shell(); ghosts(); }
else if (part == "fit_nb") { shell(); ghosts(show_bat=false, show_shelf=false); }
else if (part == "lidon") { shell(); color("plum",0.6) lid(); if ($preview) ghosts(); }  // крышка на месте
else if (part == "cut") {
    intersection() { union() { shell(); ghosts(); } translate([outer_w/2,-5,-5]) cube([outer_w,outer_h+10,outer_d+10]); }
} else if (part == "cutv") {   // вертикальный разрез по центру Y (видно слои/крышку)
    intersection() { union() { shell(); color("plum",0.6) lid(); ghosts(); } translate([-5,wall+inner_h/2,-5]) cube([outer_w+10,outer_h,outer_d+10]); }
} else if (part == "print") {
    shell();
    translate([-(outer_w+8), 0, -shelf_z]) shelf();                 // полка слева, плоско
    translate([outer_w+8, 0, outer_d]) rotate([180,0,0]) lid();     // крышка справа, ПЕРЕВЁРНУТА (язычки вверх)
} else {
    shell(); if ($preview) ghosts(); translate([0,0,18]) lid();
}
