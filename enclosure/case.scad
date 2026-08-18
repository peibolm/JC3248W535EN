// ============================================================================
// Carcasa "atril" para detras de la bascula - inventario NFC+bascula
// ============================================================================
// Forma de cuna: pared frontal VERTICAL con el PN532 en su propio brazo/
// caja, sobresaliendo pn532_overhang hacia el usuario a 8cm de altura, para
// que quede flotando SOBRE LA BASCULA y lea mejor el tag de una caja puesta
// encima; techo INCLINADO tilt_deg grados sujetando la pantalla (para
// leerla comodamente desde una mesa baja). El MAX3232 va dentro, con su
// conector DB9 enrasado al suelo interior (para poder pegarlo a la base y
// que los tornillos no carguen solos con la fuerza de enchufar/
// desenchufar el cable) asomando por la pared derecha. El cable USB de
// alimentacion sale por la misma pared derecha, encima del DB9.
//
// Piezas para imprimir:
//   - shell()       cuerpo principal: pared frontal + brazo del PN532 +
//                    techo inclinado + suelo + laterales. Abierta por detras.
//   - back_cover()   tapa trasera DESLIZANTE VERTICAL (sin tornillos):
//                    entra por arriba y baja hasta el suelo, con los 2
//                    cantos metidos en sendas guias en C (una en cada
//                    pared lateral). Las dos Ces abren hacia el CENTRO de
//                    la caja y tienen pestana DELANTERA y TRASERA, asi
//                    que la tapa queda atrapada y solo puede subir/bajar.
//                    Por eso la tapa es ~7mm mas estrecha que la carcasa.
//                    El techo lleva una rendija por la que entra.
//
// Montaje: con la shell() abierta por detras, la PANTALLA se fija con 4
// tornillos PASANTES en diagonal por el techo inclinado, con tuerca por
// dentro (accesible por la trasera abierta) - los tacos alojan ademas el
// resalte circular que trae el propio modulo alrededor de cada agujero.
// El PN532 NO se atornilla: la punta del brazo lleva un ASIENTO rebajado
// del tamano de la placa que la centra, y la placa apoya a tope contra
// una cara frontal MACIZA y fina (pn532_face_t). Se sujeta con cinta de
// transferencia fina (tipo 3M 467MP, 0.05-0.13mm) en las esquinas, fuera
// de la zona de la bobina - NO con cinta de espuma de 1mm, que duplicaria
// la distancia al tag. El MAX3232 no lleva taladros de fijacion
// propios: queda sujeto solo por los 2 tornillos prisioneros de su
// conector DB9, que atraviesan la pared derecha y se fijan con tuerca por
// fuera. Cablea, y cierra deslizando la tapa trasera hacia abajo por sus
// carriles.
//
// Medidas de la pantalla confirmadas por el fabricante (PDF de
// especificaciones + DXF de "3-Structure_Diagram", ambos en este repo).
// Medidas de PN532/MAX3232 medidas por el usuario. Lo marcado "ESTIMADO" no
// viene en ninguna hoja de datos: comprobar con calibre antes de imprimir
// la version definitiva (es parametrico, solo hay que reimprimir la pieza
// afectada).
//
// Requiere OpenSCAD (gratuito, https://openscad.org/).
// ============================================================================

// ---- Que pieza renderizar ----
// "shell" | "shell_print" | "back_cover" | "assembled" | "both".
// "shell_print" es la shell() ya girada en la orientacion recomendada
// para imprimir sin soporte interior (ver el modulo shell_print() mas
// abajo) - es la que hay que exportar a STL. "back_cover" da la tapa ya
// tumbada sobre la cama. "assembled" muestra la tapa METIDA en sus guias
// (para comprobar el encaje); "both" las muestra separadas.
PART = "both";

$fn = 64;

// ============================================================================
// PARAMETROS GENERALES
// ============================================================================
wall         = 1.8;  // pared ligera para una caja de sobremesa sin carga estructural
screw_d      = 3.2;
boss_d       = 7.0;
boss_pilot_d = 2.6;

// ---- Pantalla JC3248W535EN ----
disp_w          = 94.5;
disp_h          = 62.0;
disp_thickness  = 11;    // ESTIMADO - comprobar con calibre
// Ventana: forma EXACTA del DXF "Slot method 1" (ver disp_window_shape()
// mas abajo, en UTILIDADES) - caja contenedora aprox. 82.9 x 58.4mm.
disp_hole_dx    = 42.15;
disp_hole_dy    = 26.15;
disp_seat_depth = 5;   // profundidad, desde la cara EXTERIOR del techo hasta donde
                        // apoya la pantalla (para que quede bien asentada) - pedido
                        // por el usuario, no depende del grosor de pared
disp_standoff_h = disp_seat_depth - wall;
// Agujero de la pantalla ESCALONADO, con un tope justo a disp_seat_depth:
// tramo ANCHO (disp_hole_wide_d, diametro del DXF "Slot method 1" - NO es
// una estimacion) desde la cara exterior hasta el tope, por donde pasan
// libres la cabeza del tornillo y el propio agujero/resalte del modulo;
// tramo ESTRECHO (boss_pilot_d) despues del tope, roscando en un taco
// alargado disp_thread_len mas alla del punto donde asienta la pantalla -
// asi la longitud de rosca no depende de disp_seat_depth.
disp_hole_wide_d = 6.1;   // = diametro del agujero del DXF
disp_thread_len  = 6;     // longitud extra de taco, solo para la rosca

// ---- PN532, en un brazo/caja que sobresale hacia el usuario ----
// El modulo va en su propia caja, en voladizo desde la pared frontal, para
// que el sensor quede flotando SOBRE LA BASCULA y lea mejor (pedido por el
// usuario).
//
// LA VENTANA DE LA ANTENA SE HA ELIMINADO. El campo de 13.56 MHz atraviesa
// el plastico practicamente sin perdida: lo que le hace dano es el METAL y
// la DISTANCIA, no un par de decimas de milimetro de PLA. Un agujero no
// daba alcance, solo dejaba un borde por el que entra polvo y una
// superficie irregular justo donde se apoyan las cajas. Ahora la cara
// frontal del brazo es MACIZA, y en la zona donde asienta la placa se
// adelgaza a pn532_face_t (el rebaje se hace por DENTRO, la cara exterior
// queda lisa y a ras). El rebaje hace ademas de ASIENTO: centra la placa
// en X/Z sin necesidad de tornillos.
//
// Distancia resultante entre la bobina de la placa y la superficie
// exterior = pn532_face_t. Antes, con la placa apoyada en la pared de
// 'wall', eran 1.8mm.
//
// El alojamiento (pn532_arm_inner_w x pn532_arm_inner_h) NO cambia: el
// asiento es un rebaje poco profundo dentro de el, no toca las paredes.
pn532_w               = 40;
pn532_h               = 43;   // corregido por el usuario (antes 40)
pn532_hole_spacing_x  = 25;   // separacion horizontal entre los 2 agujeros (corregido)
pn532_hole_spacing_z  = 28;   // separacion vertical entre los 2 agujeros (corregido)
// Diagonal invertida respecto a como estaba (corregido por el usuario):
// un agujero arriba-izquierda y el otro abajo-derecha, no arriba-derecha/
// abajo-izquierda - por eso el signo de Z va con -sx, no con sx.
//
// OJO: la cota pedida por el usuario (que el brazo vuele por encima de la
// bascula sin tocarla) es la altura hasta la CARA INFERIOR del brazo, NO
// hasta su centro - con el centro a esta altura, la cara de abajo queda
// pn532_arm_outer_h/2 mas baja y tropieza con la bascula. pn532_arm_outer_h
// se calcula justo debajo (depende de pn532_h/pn532_arm_margin/wall, ya
// definidos), y pn532_center_height sale de ahi.
pn532_bottom_clear  = 80;   // altura desde la mesa hasta la cara INFERIOR del brazo (pedido por el usuario)
pn532_overhang      = 30;   // cuanto sobresale el brazo hacia el usuario (pedido por el usuario)
pn532_arm_margin    = 3;    // holgura interior alrededor del canto de la placa dentro del brazo

// Tamano interior/exterior del brazo (se necesita YA para derivar
// pn532_center_height a partir de pn532_bottom_clear, ver arriba).
pn532_arm_inner_w = pn532_w + 2*pn532_arm_margin;
pn532_arm_inner_h = pn532_h + 2*pn532_arm_margin;
pn532_arm_outer_w = pn532_arm_inner_w + 2*wall;
pn532_arm_outer_h = pn532_arm_inner_h + 2*wall;

pn532_center_height = pn532_bottom_clear + pn532_arm_outer_h/2;

// Grosor de la cara frontal SOLO en la zona donde apoya la placa. 0.8 no
// es un numero redondo por casualidad: con boquilla de 0.4 son exactamente
// 2 extrusiones, sin relleno de hueco ni paredes de una sola pasada (que
// el laminador a veces adelgaza o directamente se salta). A 0.2mm de capa
// son 4 capas. Bajar a 0.6 ya es 1.5 extrusiones: el laminador improvisa y
// el grosor real deja de ser fiable. Con boquilla de 0.6, poner 1.2.
pn532_face_t     = 0.8;
pn532_seat_clear = 0.6;  // holgura del asiento respecto a la placa (0.3 por canto)
pn532_seat_r     = 1.5;  // radio de esquina del asiento

// Los 2 taladros pasantes de la punta del brazo. Desactivados: cualquier
// tornillo que los atraviese asoma por la cara que va sobre la bascula, y
// ademas mete acero al lado de la bobina. Ponerlo a true para recuperarlos.
pn532_screw_holes = false;

// ---- MAX3232 + DB9, asomando por la pared derecha ----
// El modulo MAX3232 NO lleva taladros de fijacion propios (es una placa
// sin agujeros de montaje): el unico punto de anclaje son los 2 tornillos
// prisioneros largos del propio conector DB9, que atraviesan la pared
// derecha y se fijan con tuerca por fuera. La placa queda en voladizo,
// sujeta solo por el conector - no hace falta ningun taco para ella.
// OJO: db9_w es solo el cuerpo/carcasa de plastico (la parte que atraviesa
// la pared), NO la distancia entre prisioneros - tiene que ser MENOR que
// db9_jackscrew_spacing para que los 2 taladros de los prisioneros caigan
// en pared solida y no dentro del propio hueco. Comprobar ambas medidas
// con calibre sobre la placa real antes de imprimir.
db9_w                  = 20;    // ESTIMADO: ancho de la carcasa de plastico del DB9 - comprobar
db9_h                  = 11;    // ESTIMADO: alto de la carcasa de plastico del DB9 - comprobar
db9_r                  = 1.5;   // ESTIMADO: radio de esquina de la carcasa del DB9
db9_protrusion         = 9;     // cuanto asoma el conector fuera de la pared
db9_jackscrew_spacing  = 24.99; // estandar DB9/DE9 (MIL-DTL-24308) - NO es una estimacion
db9_jackscrew_d        = 3.4;   // ESTIMADO: diametro de paso de los prisioneros - comprobar
max3232_center_y      = 26;  // profundidad (desde la pared frontal) del centro del conector
// Altura real del centro de los agujeros del DB9 cuando el conector esta
// apoyado sobre una superficie (medida por el usuario, no depende de
// wall/db9_h) - asi el MAX3232 se puede pegar al suelo, y los 2 taladros
// de los prisioneros no cargan solos con la fuerza de conectar/
// desconectar el cable.
// Subido +3mm (pedido por el usuario): las soldaduras de la cara inferior
// de la placa del MAX3232 no dejan que se asiente tan abajo como se midio
// al principio, y los agujeros quedaban demasiado bajos.
max3232_center_z = 13;

// ---- Cable USB de alimentacion, pared derecha (mismo lado que el DB9,
// pedido por el usuario) ----
usbc_cutout_w = 14;  // en profundidad (Y) - +3mm (pedido por el usuario, quedaba muy justo)
usbc_cutout_h = 6;   // en altura (Z)
usbc_center_z = 30;  // por encima del hueco del DB9, sin solaparse con el
usbc_center_y = 26;

// ---- Angulo de la pantalla ----
tilt_deg = 10;

// ---- Margenes de disposicion interior ----
// Recortados al minimo con holgura de seguridad (no afectan al grosor de
// pared -la rigidez de las caras- ni mueven el PN532 o la altura de la
// pantalla, solo acercan los bordes de la caja a lo justo). side_margin en
// particular no sujetaba nada: los tacos de la pantalla quedan muy por
// dentro del propio modulo, era aire de sobra.
front_margin = 8;    // hueco entre la pared frontal y el borde cercano de la pantalla
// rear_margin a 8 dejaba el taco trasero-derecho de la pantalla PEGADO al
// taco de esa esquina de la tapa trasera (6.9mm centro a centro, para una
// suma de radios de 7mm - practicamente solapados, de ahi el revoltijo
// que se veia ahi). Con 14 queda con 10.5mm de sobra entre centros.
rear_margin  = 14;   // hueco tras el borde lejano de la pantalla hasta el final de la cuna
side_margin  = 3;    // margen lateral alrededor de la pantalla (solo holgura de ajuste)

// ============================================================================
// DIMENSIONES DERIVADAS (no tocar a mano, se recalculan solas)
// ============================================================================
pillar_h  = pn532_center_height + pn532_h/2 + 10; // altura de la pared frontal vertical
slope_len = front_margin + disp_h + rear_margin;   // longitud del techo, a lo largo de la pendiente

total_depth = slope_len * cos(tilt_deg);            // huella en planta (profundidad sobre la mesa)
back_h      = pillar_h + slope_len * sin(tilt_deg); // altura de la pared trasera (para la tapa)
total_width = disp_w + 2*side_margin + 2*wall;

// Centro de la pantalla, medido como distancia a lo largo de la pendiente
// desde el borde superior de la pared frontal (P1).
disp_slope_center = front_margin + disp_h/2;

// ---- Tapa trasera DESLIZANTE VERTICAL (entra por arriba, baja hasta el
// suelo) ----
// En cada pared lateral hay un BLOQUE-GUIA con una ranura tallada dentro
// que forma una "C" TUMBADA de verdad, con sus 4 caras:
//   - FONDO (en X): el trozo macizo (slide_back_x) que queda pegado a la
//     pared lateral. Es el lomo de la C.
//   - PESTANA DELANTERA (en Y): impide que la tapa se cuele hacia dentro
//     de la caja.
//   - PESTANA TRASERA (en Y): impide que la tapa se salga hacia fuera por
//     la espalda. ESTA ERA LA QUE FALTABA: sin ella la guia no era una C
//     sino una L, la tapa no quedaba atrapada y no tenia por donde
//     apoyarse al deslizar.
//   - Arriba: ABIERTA, que es por donde entra la tapa deslizando.
// La BOCA de las dos Ces mira hacia el CENTRO de la caja (la izquierda
// hacia +X y la derecha hacia -X). Por eso la tapa tiene que ser MAS
// ESTRECHA que la caja: solo puede ser tan ancha como la distancia entre
// los fondos de las dos ranuras, menos la holgura.
cover_t         = wall;  // grosor de la tapa
cover_clear_y   = 0.4;   // holgura de la tapa dentro de la ranura (en Y)
cover_clear_x   = 0.4;   // holgura total en anchura (0.2mm por canto)
slide_groove_d  = 2.5;   // profundidad de la ranura en X: cuanto muerde cada canto
slide_back_x    = 1.5;   // fondo macizo entre la ranura y la pared lateral
slide_lip_front = 1.5;   // pestana delantera de retencion (en Y)
slide_lip_rear  = 1.2;   // pestana trasera de retencion (en Y)
slide_block_x   = slide_groove_d + slide_back_x; // grosor total del bloque en X

// Posicion en Y (misma logica en las 2 paredes). La ranura queda metida
// hacia DENTRO respecto del borde trasero: los slide_lip_rear ultimos
// milimetros son la pestana trasera, asi que la tapa ya no queda a ras
// del borde, sino ligeramente retranqueada y atrapada por delante y por
// detras.
slide_slot_y1  = total_depth - slide_lip_rear;              // cara trasera de la ranura
slide_slot_y0  = slide_slot_y1 - (cover_t + cover_clear_y); // cara delantera
slide_block_y0 = slide_slot_y0 - slide_lip_front;
slide_block_y1 = total_depth;

// Alto (en Z) del bloque/ranura: desde el suelo hasta el techo, para que
// la tapa pueda entrar deslizando desde arriba del todo hasta abajo.
slide_z0 = wall;
slide_z1 = back_h;

// Franja en X por la que pasa la tapa. Un unico corte con estas medidas
// hace a la vez: (a) la ranura en C de cada bloque-guia y (b) la RENDIJA
// QUE ATRAVIESA EL TECHO INCLINADO. Sin (b) la tapa no podia bajar: el
// techo llega hasta el borde trasero y tapaba por arriba justo la franja
// por la que tiene que entrar (ese era el segundo motivo por el que "no
// deslizaba").
slide_slot_x0 = wall + slide_back_x;
slide_slot_x1 = total_width - wall - slide_back_x;

// ---- Tapa: mas estrecha que la caja ----
// Ancho = distancia entre los fondos de las dos ranuras, menos holgura.
// total_width - cover_w = 2*(wall + slide_back_x) + cover_clear_x, es
// decir la tapa es unos 7mm mas estrecha que la carcasa, y cada canto
// queda metido (slide_groove_d - cover_clear_x/2) dentro de su ranura.
cover_w  = (slide_slot_x1 - slide_slot_x0) - cover_clear_x;
cover_x0 = (total_width - cover_w)/2;
cover_y0 = slide_slot_y0 + cover_clear_y/2;
// Lengueta central que asoma por encima del techo, para poder agarrar la
// tapa y sacarla con los dedos (ponerla a 0 para una tapa a ras).
cover_tab_w = 20;
cover_tab_h = 3;

// ============================================================================
// UTILIDADES
// ============================================================================

// Rectangulo con esquinas redondeadas, centrado en el origen, en el plano XY.
module rounded_rect(w, h, r) {
    hull() {
        for (sx = [-1, 1], sy = [-1, 1])
            translate([sx*(w/2 - r), sy*(h/2 - r)])
                circle(r = r);
    }
}

// ---- Forma EXACTA del hueco de la pantalla, tal cual el DXF "Slot method
// 1" (no es un rectangulo de un solo radio: cada esquina tiene 2 arcos de
// radio distinto, una curva compuesta). Los vertices y "bulges" (formato
// DXF: bulge = tan(angulo_arco/4)) son los del propio archivo. ----
// NOTA: hay 2 centros candidatos (a un lado y otro de la cuerda) para el
// mismo radio; cual es el correcto depende del signo del bulge de una
// forma que NO se puede resolver con una unica formula sin ramificar (un
// primer intento con formula directa acertaba solo con bulge positivo, y
// para los negativos el centro salia reflejado al lado equivocado -> el
// arco se dibujaba hacia el otro lado, creando las puntas que se veian).
// Se prueban los 2 candidatos y se usa el que de verdad lleva de p1 a p2
// al girar theta grados.
function dxf_arc_mid(p1, p2, bulge, segs) =
    let(
        theta = 4*atan(bulge),
        hx = (p2[0]-p1[0])/2, hy = (p2[1]-p1[1])/2,
        half_len = sqrt(hx*hx + hy*hy),
        sagitta = bulge*half_len,
        r = (half_len*half_len + sagitta*sagitta) / (2*abs(sagitta)),
        mx = (p1[0]+p2[0])/2, my = (p1[1]+p2[1])/2,
        apothem = sqrt(max(r*r - half_len*half_len, 0)),
        ux = -hy/half_len, uy = hx/half_len,
        cA = [mx + ux*apothem, my + uy*apothem],
        cB = [mx - ux*apothem, my - uy*apothem],
        a1A = atan2(p1[1]-cA[1], p1[0]-cA[0]),
        pA2 = [cA[0] + r*cos(a1A+theta), cA[1] + r*sin(a1A+theta)],
        use_A = (abs(pA2[0]-p2[0]) < 0.01 && abs(pA2[1]-p2[1]) < 0.01),
        c = use_A ? cA : cB,
        a1 = atan2(p1[1]-c[1], p1[0]-c[0])
    )
    [ for (i = [1:segs-1]) let(a = a1 + theta*i/segs)
        [c[0] + r*cos(a), c[1] + r*sin(a)] ];

function dxf_polygon(pts, bulges, segs=8) =
    [ for (i = [0:len(pts)-1])
        each concat([pts[i]],
            bulges[i] == 0 ? [] : dxf_arc_mid(pts[i], pts[(i+1)%len(pts)], bulges[i], segs)) ];

// Vertices/bulges del DXF, en sus coordenadas locales originales (X,Y).
disp_window_dxf_pts = [
    [27.495555555555562, -37.803509433242318],
    [29.180000000000021, -36.561654985597279],
    [29.180000000000007,  36.561654985597272],
    [27.495555555555555,  37.803509433242318],
    [21.868539968655561,  40.609999999999999],
    [20.64526567398573,   41.469999999999992],
    [-20.645265673985701, 41.469999999999985],
    [-21.868539968655551, 40.609999999999985],
    [-27.495555555555562, 37.803509433242304],
    [-29.18000000000001,  36.561654985597258],
    [-29.18,             -36.561654985597258],
    [-27.495555555555555,-37.803509433242304],
    [-21.868539968655547,-40.609999999999999],
    [-20.645265673985712,-41.469999999999992],
    [20.645265673985719, -41.469999999999992],
    [21.868539968655565, -40.609999999999971],
];
disp_window_dxf_bulges = [
    0.50514240717654058, 0,
    0.50514240717654779, -0.40107493545323053, 0.316339252013559, 0,
    0.31633925201354884, -0.40107493545322986, 0.50514240717654235, 0,
    0.50514240717655101, -0.40107493545323042, 0.31633925201355506, 0,
    0.316339252013564,  -0.40107493545322931,
];

// El DXF esta en orientacion "retrato" (X local recorre los 58.36mm, Y
// local los 82.94mm); nuestra pantalla va en horizontal, así que X local
// del DXF pasa a ser nuestro alto (Z/pendiente) y Y local pasa a ser nuestro
// ancho (X) - mismo intercambio ya usado para disp_hole_dx/dy.
module disp_window_shape() {
    polygon([ for (p = dxf_polygon(disp_window_dxf_pts, disp_window_dxf_bulges))
        [p[1], p[0]] ]);
}

// Coloca a sus hijos SOBRE la pendiente del techo, en coordenadas locales
// donde X sigue siendo el ancho, Y-local = "hacia arriba por la pendiente"
// y Z-local = "hacia fuera" (normal de la superficie). s = distancia a lo
// largo de la pendiente desde P1 (borde superior de la pared frontal).
module on_slope(s, x = 0) {
    translate([x, s*cos(tilt_deg), pillar_h + s*sin(tilt_deg)])
        rotate([tilt_deg, 0, 0])
            children();
}

// Recorta a sus hijos con el MISMO plano inclinado que define el techo
// (el que usa wedge_solid), desplazado z_off en vertical. Sirve para que
// los bloques-guia no asomen por fuera del techo y para que el borde
// superior de la tapa quede a ras de el.
module below_roof(z_off = 0) {
    difference() {
        union() children();
        translate([-1, 0, pillar_h + z_off])
            rotate([tilt_deg, 0, 0])
                cube([total_width + 2, total_depth*4, back_h*4]);
    }
}

// Cuna solida (SIN hueco), usada tanto para el exterior como, escalada/
// retranqueada, para tallar el interior.
module wedge_solid(w, d, ph, bh) {
    difference() {
        cube([w, d, bh]);
        translate([-1, 0, ph])
            rotate([tilt_deg, 0, 0])
                cube([w + 2, d*3, bh*3]);
    }
}

// ============================================================================
// CUERPO PRINCIPAL (abierto por detras)
// ============================================================================
module shell() {
    difference() {
        union() {
            // --- Carcasa hueca: exterior menos interior retranqueado 'wall' ---
            difference() {
                wedge_solid(total_width, total_depth, pillar_h, back_h);

                // Interior: suelo/frontal/laterales retranqueados 'wall' en
                // sus propios ejes (son caras rectas, un translate normal
                // basta). El techo, en cambio, esta inclinado tilt_deg: un
                // retranqueo ingenuo en ejes (wall,wall) NO da un grosor de
                // pared uniforme ahi (da wall*sin(tilt) =~ 0.4mm en vez de
                // wall =~ 2.4mm). Para el corte del techo se retranquea el
                // PIVOTE del plano a lo largo de su normal real
                // (-sin(tilt),cos(tilt)), no en ejes X/Z crudos.
                // Se extiende de sobra en +Y para dejar la trasera abierta.
                difference() {
                    translate([wall, wall, wall])
                        cube([total_width - 2*wall, total_depth*3, back_h*3]);
                    translate([-1, wall*sin(tilt_deg), pillar_h - wall*cos(tilt_deg)])
                        rotate([tilt_deg, 0, 0])
                            cube([total_width + 2, total_depth*6, back_h*6]);
                }
            }

            // --- Tacos para la pantalla, sobre la cara interior del techo.
            // El taco se alarga disp_thread_len MAS ALLA de donde asienta la
            // pantalla (disp_standoff_h), solo para tener longitud de rosca
            // de sobra sin depender de disp_seat_depth - el agujero
            // escalonado (mas abajo) pone el tope justo donde acaba
            // disp_standoff_h, que es donde tiene que apoyar la pantalla ---
            for (sx = [-1, 1], sy = [-1, 1])
                on_slope(disp_slope_center + sy*disp_hole_dy, total_width/2 + sx*disp_hole_dx)
                    translate([0, 0, -wall])
                        rotate([180, 0, 0])
                            cylinder(d = boss_d, h = disp_standoff_h + disp_thread_len);

            // --- Brazo/caja del PN532, sobresaliendo hacia el usuario. Se
            // solapa 1mm con la pared frontal principal (wall+1) para
            // fusionarse bien con ella ---
            translate([total_width/2 - pn532_arm_outer_w/2,
                       -pn532_overhang,
                       pn532_center_height - pn532_arm_outer_h/2])
                cube([pn532_arm_outer_w, pn532_overhang + wall + 1, pn532_arm_outer_h]);

            // --- Bloques-guia de la tapa trasera deslizante: un bloque
            // anadido a cada pared lateral (izquierda/derecha), pegado a
            // la pared en TODA la altura (suelo a techo). La ranura en C
            // se talla dentro, en el difference() de mas abajo. Paredes
            // VERTICALES, sin inclinacion - no tienen el problema de los
            // tacos rectos contra el techo inclinado. Se recortan con el
            // plano del techo (below_roof) para que no asomen por fuera:
            // antes, al ser cubos rectos hasta back_h, sobresalian por la
            // cara inclinada ---
            below_roof() {
                translate([wall, slide_block_y0, slide_z0])
                    cube([slide_block_x, slide_block_y1 - slide_block_y0,
                          slide_z1 - slide_z0 + 2]);
                translate([total_width - wall - slide_block_x, slide_block_y0, slide_z0])
                    cube([slide_block_x, slide_block_y1 - slide_block_y0,
                          slide_z1 - slide_z0 + 2]);
            }
        }

        // --- Ventana de la pantalla, a traves del techo inclinado ---
        // (el translate en Z va DESPUES del extrude: un translate en Z sobre
        // la forma 2D no tiene efecto, linear_extrude siempre arranca en
        // Z-local=0, que es la cara EXTERIOR segun on_slope. Rango con
        // holgura de sobra a cada lado del grosor real del techo, wall.)
        on_slope(disp_slope_center, total_width/2)
            translate([0, 0, -wall - 1])
                linear_extrude(height = wall + 2, center = false)
                    disp_window_shape();

        // --- Hueco interior del brazo del PN532: abierto por detras, se
        // funde con el interior principal Y ademas atraviesa la pared
        // frontal principal en ese punto (para que quede todo comunicado y
        // se pueda acceder a las tuercas por la trasera abierta) ---
        translate([total_width/2 - pn532_arm_inner_w/2,
                   -pn532_overhang + wall,
                   pn532_center_height - pn532_arm_inner_h/2])
            cube([pn532_arm_inner_w, pn532_overhang + wall + 2, pn532_arm_inner_h]);

        // --- Asiento de la placa PN532, rebajado POR DENTRO de la cara
        // frontal del brazo. No atraviesa: deja pn532_face_t de material
        // macizo, asi que la cara exterior queda lisa y continua. El
        // rebaje es del tamano de la placa + holgura, con lo que sus 4
        // paredes centran la placa sin tornillos ---
        translate([total_width/2, -pn532_overhang + pn532_face_t, pn532_center_height])
            rotate([-90, 0, 0])
                linear_extrude(height = wall - pn532_face_t + 0.01)
                    rounded_rect(pn532_w + pn532_seat_clear,
                                 pn532_h + pn532_seat_clear,
                                 pn532_seat_r);

        // --- 2 taladros PASANTES en diagonal para el PN532 (desactivados
        // por defecto, ver pn532_screw_holes). Diagonal invertida: Z va
        // con -sx, no con sx ---
        if (pn532_screw_holes)
            for (sx = [-1, 1])
                translate([total_width/2 + sx*pn532_hole_spacing_x/2,
                           -pn532_overhang - 0.1,
                           pn532_center_height - sx*pn532_hole_spacing_z/2])
                    rotate([-90, 0, 0])
                        cylinder(d = screw_d, h = wall + 0.2);

        // --- Agujero ESCALONADO de la pantalla, con tope justo a
        // disp_seat_depth: tramo ANCHO (disp_hole_wide_d, del DXF) desde la
        // cara exterior hasta el tope, tramo ESTRECHO (boss_pilot_d) desde
        // el tope hasta el fondo del taco alargado, para la rosca del
        // tornillo. El tornillo entra por fuera; su cabeza topa justo donde
        // asienta la pantalla ---
        for (sx = [-1, 1], sy = [-1, 1])
            on_slope(disp_slope_center + sy*disp_hole_dy, total_width/2 + sx*disp_hole_dx) {
                translate([0, 0, -disp_seat_depth])
                    cylinder(d = disp_hole_wide_d, h = disp_seat_depth + 0.1);
                translate([0, 0, -disp_seat_depth - disp_thread_len - 0.1])
                    cylinder(d = boss_pilot_d, h = disp_thread_len + 0.2);
            }

        // --- Hueco en la pared derecha: carcasa del conector DB9, asomando ---
        // (db9_w va en Y, db9_h va en Z; ver comentario de rotate en la
        // ventana de la pantalla para la logica de los ejes tras rotate)
        translate([total_width - wall - 0.1, max3232_center_y, max3232_center_z])
            rotate([0, 90, 0])
                linear_extrude(height = wall + db9_protrusion + 0.2)
                    rounded_rect(db9_h, db9_w, db9_r);

        // --- Taladros para los 2 tornillos prisioneros del propio DB9,
        // que fijan el conector (y con el, todo el MAX3232) a la pared ---
        for (sy = [-1, 1])
            translate([total_width - wall - 0.1,
                       max3232_center_y + sy*db9_jackscrew_spacing/2,
                       max3232_center_z])
                rotate([0, 90, 0])
                    cylinder(d = db9_jackscrew_d, h = wall + 0.2);

        // --- Hueco en la pared derecha: paso del cable USB-C (mismo lado
        // que el DB9, por encima de el, pedido por el usuario) ---
        translate([total_width - wall - 0.1, usbc_center_y - usbc_cutout_w/2, usbc_center_z - usbc_cutout_h/2])
            cube([wall + 0.2, usbc_cutout_w, usbc_cutout_h]);

        // --- Camino de la tapa trasera: UN SOLO corte, que hace a la vez
        // las dos cosas que faltaban ---
        //  (a) talla la ranura en C de cada bloque-guia: se come los
        //      slide_groove_d interiores de cada bloque y deja intactos el
        //      fondo (slide_back_x contra la pared), la pestana DELANTERA
        //      (Y < slide_slot_y0) y la pestana TRASERA (Y >
        //      slide_slot_y1). Las dos Ces quedan abiertas hacia el CENTRO
        //      de la caja, que es de donde llegan los cantos de la tapa.
        //  (b) abre la rendija que ATRAVIESA EL TECHO inclinado, sin la
        //      cual la tapa no podia entrar desde arriba (el techo llega
        //      hasta el borde trasero y cerraba por encima justo esta
        //      franja).
        // Sube hasta muy por encima de back_h para que la rendija quede
        // abierta por arriba pase lo que pase con el angulo del techo.
        translate([slide_slot_x0, slide_slot_y0, slide_z0 - 0.1])
            cube([slide_slot_x1 - slide_slot_x0,
                  slide_slot_y1 - slide_slot_y0,
                  back_h + 20]);
    }
}

// ============================================================================
// ORIENTACION DE IMPRESION
// ============================================================================
// La shell() "de pie" (tal como esta modelada) deja el techo casi
// horizontal (solo tilt_deg=10 grados) colgando sobre toda la cavidad
// hueca - necesitaria soporte por DENTRO de la caja, muy incomodo de
// retirar. Girandola apoyada sobre la CARA DEL BRAZO del PN532, con la
// trasera abierta mirando ARRIBA, el techo pasa a estar a tilt_deg de la
// VERTICAL en vez de la horizontal e imprime limpio, y la caja queda
// "boca arriba" (que es como se imprime cualquier caja: nunca boca
// abajo, o la pared del fondo habria que puentearla entera).
//
// OJO - ESTO ESTABA AL REVES. La version anterior era
//   translate([0, 0, total_depth]) rotate([-90, 0, 0])
// que deja world_Z = -model_Y, es decir la BOCA CONTRA LA CAMA y la punta
// del brazo arriba del todo: la pared frontal quedaba como un techo plano
// de ~104x126mm puenteando sobre la cavidad entera, justo lo que el
// comentario decia que se queria evitar. Con rotate([90,0,0]) la punta
// del brazo queda en Z=0 y la boca en Z=total_depth+pn532_overhang.
//
// Ventaja adicional, y por eso importa aqui: la cara fina del brazo
// (pn532_face_t) pasa a ser la PRIMERA CAPA, plana contra la cama. Es la
// mejor superficie y el grosor mas exacto que se puede conseguir; en la
// orientacion anterior habria tenido que imprimirse como un puente de
// 46x49mm, que con 4 capas se descuelga.
//
// El brazo, al ser un saliente de 30mm, hace de pedestal: la pared
// frontal principal arranca a 30mm de la cama y hay que ponerle soporte
// debajo. Es soporte EXTERIOR, bajo una cara plana y fuera de la pieza,
// facil de quitar. No hay forma de evitarlo con el brazo en voladizo,
// salvo imprimir el brazo como pieza aparte.
module shell_print() {
    translate([0, back_h, pn532_overhang])
        rotate([90, 0, 0])
            shell();
}

// ============================================================================
// TAPA TRASERA (deslizante VERTICAL, sin tornillos)
// ============================================================================
// Entra desde arriba (por la rendija del techo) y se desliza hacia abajo,
// con los 2 cantos metidos en las ranuras en C de la shell (una en cada
// pared lateral). Baja hasta apoyar en el suelo.
//
// back_cover() se modela YA EN SU SITIO, en el mismo sistema de
// coordenadas que shell(), para poder comprobar de un vistazo (PART =
// "assembled") que encaja. Para imprimirla, back_cover_print() la tumba
// sobre la cama.
//
// Es mas ESTRECHA que la caja por narices: cover_w es la distancia entre
// los fondos de las dos ranuras menos cover_clear_x, o sea
// total_width - 2*(wall + slide_back_x) - cover_clear_x. Cada canto queda
// metido 2.3mm dentro de su ranura de 2.5mm.
module back_cover() {
    // Plancha principal, recortada a ras del plano del techo (0.15mm por
    // debajo, para que no roce ni asome).
    below_roof(-0.15)
        translate([cover_x0, cover_y0, wall])
            cube([cover_w, cover_t, back_h - wall + 5]);

    // Lengueta central para agarrarla con los dedos: asoma cover_tab_h
    // por encima del techo, y pasa sin problema por la misma rendija.
    if (cover_tab_h > 0)
        below_roof(cover_tab_h)
            translate([total_width/2 - cover_tab_w/2, cover_y0, wall])
                cube([cover_tab_w, cover_t, back_h - wall + 5 + cover_tab_h]);
}

// La tapa tumbada sobre la cama de impresion (plana, sin soportes).
module back_cover_print() {
    translate([0, 0, cover_y0 + cover_t])
        rotate([-90, 0, 0])
            back_cover();
}

// ============================================================================
// RENDER
// ============================================================================
if (PART == "shell") {
    shell();
} else if (PART == "shell_print") {
    shell_print();
} else if (PART == "back_cover") {
    back_cover_print();
} else if (PART == "assembled") {
    // Comprobacion visual: la tapa en su sitio, dentro de las guias.
    shell();
    back_cover();
} else {
    shell();
    translate([total_width + 20, 0, 0])
        back_cover_print();
}
