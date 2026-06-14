// ============================================================
//  Air Quality Monitor — корпус (v1, параметрический)
// ============================================================
//  Компактный FDM-корпус под заказную печать.
//  - посадочные гнёзда-направляющие для e-Paper / ESP32 / SCD41
//    (детали вдвигаются, при желании — на клей);
//  - сдвижная задняя крышка с защёлкой (без винтов, заподлицо);
//  - две рабочие вентиляции у датчика (правая + нижняя стенки);
//  - реальные тела модулей (ghosts) для проверки посадки.
//
//  Размеры деталей — из даташитов; пометка TODO:verify = проверить
//  калипером перед заказом печати (от этого зависят зазоры посадок).
//
//  Рендер из командной строки:
//    openscad -D 'part="all"'   -o all.png   --preview ...   (общий вид)
//    openscad -D 'part="fit"'   -o fit.png   --preview ...   (посадка деталей)
//    openscad -D 'part="print"' -o case.stl  case.scad       (STL: обе детали)
// ============================================================

$fn = 32;

/* ---------- Допуски / стенки ---------- */
fit      = 0.4;   // общий зазор посадки
wall     = 2.0;   // боковые стенки
floor_t  = 2.0;   // лицевая стенка
lid_t    = 2.0;   // крышка

/* ---------- Компоненты (TODO:verify калипером) ---------- */
bat_l = 70;  bat_w = 49;  bat_h = 20;          // батарейный отсек 3xAA
epd_pcb_l = 65;  epd_pcb_w = 30.2;  epd_stack = 6;   // e-Paper HAT
epd_act_w = 48.55; epd_act_h = 23.71;          // активная зона экрана
epd_act_off_x = 8; epd_act_off_y = 3;          // смещение зоны от края платы
esp_l = 22.52; esp_w = 18; esp_h = 6;          // ESP32-S3 SuperMini
scd_l = 21; scd_w = 21; scd_h = 8;             // SCD41 модуль
usb_w = 9.5; usb_h = 4;                         // вырез USB-C
btn_hole_d = 4; btn_spacing = 16;              // кнопки

/* ---------- Гнёзда-направляющие ---------- */
seat_rib    = 1.2;   // толщина ребра-направляющей
seat_clear  = 0.4;   // зазор гнезда (плата легко входит)
seat_h_epd  = 3;     // высота ребра вокруг экрана
seat_h_small= 3;     // высота ребра для ESP32 / SCD41

/* ---------- Сдвижная крышка ---------- */
rail_d      = 1.5;   // глубина паза в стенке (заезд язычка)
slide_clear = 0.4;   // зазор скольжения по X
lid_y_clear = 0.6;   // зазор по Y (чтобы вдвигалась)
back_lip    = 1.0;   // задний бортик: держит крышку в Z (она утоплена на эту величину)
detent_d    = 1.2;   // диаметр бугорка-защёлки

/* ---------- Вентиляция ---------- */
vent_w   = 1.6;   // ширина щели

/* ---------- Производные ---------- */
front_layer = 8;                               // слой электроники по Z
epd_lower_gap = 2;                             // зазор между экраном и нижними платами
inner_w = max(bat_l, epd_pcb_l) + 2*fit;
// высота вмещает: экран сверху + нижнюю полосу под самую высокую плату (SCD41/ESP32)
inner_h = max(bat_w, epd_pcb_w + max(esp_w, scd_w) + epd_lower_gap) + 2*fit;
inner_d = front_layer + bat_h;                 // полость до тыльной грани батареи
outer_w = inner_w + 2*wall;
outer_h = inner_h + 2*wall;
lid_z0  = floor_t + inner_d;                   // фронт крышки по Z
lid_z1  = lid_z0 + lid_t;                       // тыл плиты крышки
outer_d = lid_z1 + back_lip;                    // общий габарит по Z (крышка утоплена)

// --- Позиции компонентов (Z=0 — внешняя сторона лица) ---
epd_x = wall + (inner_w - epd_pcb_l)/2;
epd_y = wall + inner_h - epd_pcb_w - fit;       // верхняя зона
win_x = epd_x + epd_act_off_x;
win_y = epd_y + epd_act_off_y;

esp_x = wall + fit;                             // нижний-левый угол
esp_y = wall + fit;

scd_x = wall + inner_w - scd_l - fit;           // нижний-правый угол (у вентиляции)
scd_y = wall + fit;

btn_cx = outer_w/2;
btn_y  = wall + (epd_y - wall)/2;               // середина нижней полосы

bat_x = wall + (inner_w - bat_l)/2;
bat_y = wall + (inner_h - bat_w)/2;
bat_z = floor_t + front_layer;                  // батарея за электроникой

// ============================================================
//  Вырезы и фичи (модули)
// ============================================================

// Большая внутренняя полость, тыл открыт.
module cavity_cut() {
    translate([wall, wall, floor_t])
        cube([inner_w, inner_h, outer_d]);     // выходит за тыл — открыто
}

// Рамка-гнездо вокруг footprint (4 ребра).
module seat_frame(px, py, fw, fh, rh) {
    translate([px, py, floor_t])
        difference() {
            translate([-seat_rib, -seat_rib, 0])
                cube([fw + 2*seat_rib, fh + 2*seat_rib, rh]);
            translate([0, 0, -1]) cube([fw, fh, rh + 2]);
        }
}

// Гнёзда всех трёх плат.
module seats() {
    // e-Paper — полная рамка (экран ляжет к окну изнутри)
    seat_frame(epd_x - seat_clear, epd_y - seat_clear,
               epd_pcb_l + 2*seat_clear, epd_pcb_w + 2*seat_clear, seat_h_epd);

    // ESP32 — полная рамка (вырез USB прорежет левое ребро)
    seat_frame(esp_x - seat_clear, esp_y - seat_clear,
               esp_l + 2*seat_clear, esp_w + 2*seat_clear, seat_h_small);

    // SCD41 — только ДВА ребра (слева и сверху): датчик прижат в угол к
    // правой и нижней стенкам, где вентиляция — рёбра её не перекрывают.
    translate([scd_x - seat_clear - seat_rib, scd_y - seat_clear, floor_t])
        cube([seat_rib, scd_w + 2*seat_clear, seat_h_small]);            // левое ребро
    translate([scd_x - seat_clear - seat_rib, scd_y + scd_w + seat_clear, floor_t])
        cube([scd_l + 2*seat_clear + seat_rib, seat_rib, seat_h_small]); // верхнее ребро
}

module window_cut() {
    translate([win_x, win_y, -1]) cube([epd_act_w, epd_act_h, floor_t + 2]);
}

module buttons_cut() {
    translate([btn_cx - btn_spacing/2, btn_y, -1]) cylinder(d = btn_hole_d, h = floor_t + 2);
    translate([btn_cx + btn_spacing/2, btn_y, -1]) cylinder(d = btn_hole_d, h = floor_t + 2);
}

// Вырез USB-C в левой стенке (режет и стенку, и левое ребро гнезда ESP32).
module usb_cut() {
    translate([-1, esp_y + esp_w/2 - usb_w/2, floor_t + (esp_h - usb_h)/2])
        cube([wall + seat_rib + 2, usb_w, usb_h]);
}

// Две вентиляции у SCD41: щели в правой стенке + щели в нижней стенке.
module vents_cut() {
    // правая стенка (нормаль X): 3 вертикальные щели
    for (i = [0:2])
        translate([wall + inner_w - 1, scd_y + 3 + i*5, floor_t + 1.5])
            cube([wall + 2, vent_w, 5]);
    // нижняя стенка (нормаль Y): 3 горизонтальные щели
    for (i = [0:2])
        translate([scd_x + 3, -1, floor_t + 1.5 + i*2.2])
            cube([scd_l - 6, wall + 2, vent_w]);
}

// Пазы под сдвижную крышку: в левой и правой стенках + слот сверху для въезда.
module lid_slots() {
    // левый паз (в левую стенку на rail_d), открыт сверху
    translate([wall - rail_d, wall, lid_z0])
        cube([rail_d, inner_h + wall + 1, lid_t]);
    // правый паз
    translate([wall + inner_w, wall, lid_z0])
        cube([rail_d, inner_h + wall + 1, lid_t]);
    // слот в верхней стенке (въезд крышки сверху)
    translate([wall - rail_d, wall + inner_h, lid_z0])
        cube([inner_w + 2*rail_d, wall + 1, lid_t]);
}

// Лунка-защёлка в правом пазу у дна (бугорок на крышке туда щёлкает).
module detent_pocket() {
    translate([wall + inner_w + rail_d/2, wall + 3, lid_z0 + lid_t/2])
        rotate([0, 90, 0]) cylinder(d = detent_d, h = rail_d + 1, center = true);
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
                lid_slots();
                detent_pocket();
            }
            seats();
        }
        // финальные вырезы (режут стенки и рёбра)
        window_cut();
        buttons_cut();
        usb_cut();
        vents_cut();
    }
}

// ============================================================
//  Сдвижная крышка
// ============================================================
module lid() {
    notch_d = 10;
    union() {
        difference() {
            // плита с язычками (заходят в пазы)
            translate([wall - rail_d + slide_clear,
                       wall + lid_y_clear, lid_z0])
                cube([inner_w + 2*rail_d - 2*slide_clear,
                      inner_h - 2*lid_y_clear, lid_t]);
            // палец-вырез на въездной (верхней) кромке
            translate([outer_w/2, wall + inner_h, lid_z0 - 1])
                cylinder(d = notch_d, h = lid_t + 2);
        }
        // бугорок-защёлка на правом язычке у дна
        translate([wall + inner_w + rail_d/2, wall + lid_y_clear + 3, lid_z0 + lid_t/2])
            rotate([0, 90, 0]) cylinder(d = detent_d, h = rail_d, center = true);
    }
}

// ============================================================
//  Тела модулей (для проверки посадки; в STL не идут)
// ============================================================
module ghosts(show_bat = true) {
    color("green", 0.45)  translate([epd_x, epd_y, floor_t]) cube([epd_pcb_l, epd_pcb_w, epd_stack]);
    color("blue",  0.45)  translate([esp_x, esp_y, floor_t]) cube([esp_l, esp_w, esp_h]);
    color("red",   0.55)  translate([scd_x, scd_y, floor_t]) cube([scd_l, scd_w, scd_h]);
    if (show_bat)
        color("orange",0.20) translate([bat_x, bat_y, bat_z]) cube([bat_l, bat_w, bat_h]);
}

// ============================================================
//  RENDER
// ============================================================
part = "all";   // "all" | "fit" | "shell" | "lid" | "print"

if (part == "shell") {
    shell();
} else if (part == "lid") {
    lid();
} else if (part == "fit") {
    // корпус + детали внутри (без крышки) — видно посадку
    shell();
    ghosts();
} else if (part == "fit_nb") {
    // то же, но без батареи — видно гнёзда и плату электроники
    shell();
    ghosts(show_bat = false);
} else if (part == "cut") {
    // продольный разрез по центру X — видно слои по глубине
    intersection() {
        union() { shell(); ghosts(); }
        translate([outer_w/2, -5, -5]) cube([outer_w, outer_h + 10, outer_d + 10]);
    }
} else if (part == "print") {
    // обе детали на плоскости для заказа STL
    shell();
    translate([0, outer_h + 10, -lid_z0]) lid();   // крышка рядом, опущена на стол
} else {
    // общий вид: корпус + призраки + крышка «выехала» назад (explode)
    shell();
    if ($preview) ghosts();
    translate([0, 0, 14]) lid();
}
