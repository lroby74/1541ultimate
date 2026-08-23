# I banchi di prova

Tutto quello che sta qui dentro serve a **misurare**, non a raccontare.
Nessuno di questi file finisce nel bitstream o nel firmware.

---

## Prima di tutto: l'ambiente

```sh
bash '<repo>/banco/setup-ambiente.sh'   # dentro WSL
export PATH=~/niosbin:$PATH
```

Ricostruisce i wrapper che fanno chiamare a `make` di WSL i compilatori Nios II
di Windows. **La chiave e' `exec -a` con un argv[0] in forma Windows**: senza,
il driver di GCC non trova ne' `cc1` ne' gli header. Sta tutto spiegato dentro
lo script.

ModelSim ASE arriva con Quartus e non serve nessun wrapper:
```sh
export PATH="/c/intelFPGA_lite/17.0/modelsim_ase/win32aloem:$PATH"
```

---

## `tape_map/` — le formule del contanastro, in C

Portage di `C64_MiSTer x 1530 source code/rtl/tap_scanner.sv`.

```sh
cd '<repo>/banco/tape_map'
g++ -O2 -Wall -I. -o tape_map_tb  tape_map_tb.cc  '../../software/io/tape/tape_map.cc'
g++ -O2 -Wall -I. -o turrican_tb  turrican_tb.cc  '../../software/io/tape/tape_map.cc'
g++ -O2 -Wall       -o tape_events tape_events.cc
./tape_map_tb <un file TAP v1 qualsiasi>
./turrican_tb
./tape_events
./costo_montaggio '<repo>/Turrican II.tap'
```

| programma | che cosa misura |
|---|---|
| `tape_map_tb` | **54 controlli**. Confronta la mappa con un modello di riferimento **scritto per conto suo**, su TAP v0/v1/v2 sintetici, silenzi lunghissimi, nastri piu' corti di un giro, e su TAP veri. L'invariante che conta: *ogni giro cade nel punto esatto del nastro, silenzi compresi*. |
| `turrican_tb` | La sequenza vera del game over sul TAP di riferimento, e la simmetria PLAY / REW / FF su tutti i 246 giri. |
| `costo_montaggio` | **Quanto costa montare un TAP**: quante letture e quante ricerche fa la costruzione della mappa. Serve a rispondere a *"2,4 MB non sono troppi?"*. Su Turrican II: era **592 letture e 594 ricerche**, adesso **38 e 3**. In memoria ci sta sempre e solo un blocco: quello che pesava era il numero di giri su USB. |
| `tape_events` | Radiografia di un nastro: dove cadono toni pilota e vuoti, in giri di contanastro, con il margine dal confine del giro. **Serve a tarare.** |

`filemanager.h` qui dentro e' una finta: serve solo a compilare `tape_map.cc`
sul PC senza tirarsi dietro tutto il firmware.

---

## `c2n_sim/` — i registri e l'inerzia, in ModelSim

```sh
cd '<repo>/banco/c2n_sim'
export PATH="/c/intelFPGA_lite/17.0/modelsim_ase/win32aloem:$PATH"
vlib work
vcom -quiet -2008 ../../fpga/ip/busses/vhdl_source/io_bus_pkg.vhd \
                  ../../fpga/ip/sync_fifo/vhdl_source/sync_fifo.vhd \
                  ../../fpga/io/c2n_playback/vhdl_source/tape_speed_control.vhd \
                  ../../fpga/io/c2n_playback/vhdl_source/c2n_playback_io.vhd \
                  c2n_counter_tb.vhd tape_inertia_tb.vhd leadin_tb.vhd
vsim -c -quiet c2n_counter_tb  -do "run -all; quit -f"
vsim -c -quiet tape_inertia_tb -do "run -all; quit -f"
vsim -c -quiet leadin_tb       -do "run -all; quit -f"   # dura qualche minuto
```

| banco | che cosa misura |
|---|---|
| `c2n_counter_tb` | I registri aggiunti a `c2n_playback_io`. Il controllo che conta: **i cicli di nastro avanzano anche quando la coda e' ferma**, cioe' dentro un impulso lungo; e si fermano col C64 congelato dal menu. |
| `leadin_tb` | **Quanto pesa il silenzio di cortesia** che il player infila a ogni PLAY (i byte `00 95 95 25`). Misurato: **2.463.125 cicli, il 91% di un giro**. Non sta nel file TAP, quindi il firmware lo toglie dal conto: senza, PLAY e RIAVVOLGIMENTO davano numeri diversi di quasi un giro. |
| `tape_inertia_tb` | Quanto pesa l'inerzia del motore. **89 ms per ogni ciclo spegni-riaccendi**: su Turrican II (24 blocchi) fa il 79% di un giro. E' il motivo per cui il contanastro conta nastro e non millisecondi. |

Girano al **clock vero della U2+ (62,5 MHz)**: a 10 MHz `tape_speed_control` va
in saturazione e non e' rappresentativo.

---

## `md2_sim/` -- Magic Desk 2 sui 2 MB, in ModelSim

```sh
cd '<repo>/banco/md2_sim'
export PATH="/c/intelFPGA_lite/17.0/modelsim_ase/win32aloem:$PATH"
vlib work
vcom -quiet -2008 ../../fpga/ip/busses/vhdl_source/io_bus_pkg.vhd                   ../../fpga/ip/busses/vhdl_source/slot_bus_pkg.vhd                   ../../fpga/ip/memory/vhdl_source/dpram.vhd                   ../../fpga/devices/vhdl_source/microwire_eeprom.vhd                   ../../fpga/ip/busses/vhdl_source/io_dummy.vhd                   ../../fpga/cart_slot/vhdl_source/all_carts_v5.vhd                   md2_2mb_tb.vhd
vsim -c -quiet md2_2mb_tb -do "run -all; quit -f"
```

| banco | che cosa misura |
|---|---|
| `md2_2mb_tb` | I **128 banchi da 16K** di Magic Desk 2, uno per uno. Il controllo che conta: **il banco 64 non ricade sul banco 0** -- con sei bit di banco ci ricadeva, ed e' il motivo per cui il gioco si rompeva dopo la presentazione. Verifica anche che il bit 7 spegne e riaccende la cartuccia, e che **i mapper vecchi non si sono spostati** di un byte. |

| `md2_reset_tb` | **Che cosa fa il tasto reset della cartuccia al mapper.** Porta il banco a 100, spegne la cartuccia col bit 7, poi preme il tasto e guarda l'indirizzo che esce: torna al **banco 0** e la cartuccia si riaccende in modo 16K. Prova le due strade del reset, il tasto (`RST_in`) e il comando del firmware (`c64_reset`). Serviva a decidere **dove** stava il difetto del "Magic Desk non riparte da zero": non nel VHDL. |

| `crt_md16` | **Un CRT vero, dal file all'indirizzo che legge l'FPGA.** Rifa' i conti del firmware e del VHDL sul file da 2 MB e controlla che il byte che il C64 leggera' sia quello giusto, per tutti i 128 banchi. Dentro c'e' anche la controprova: con sei bit di banco ne sbagliavano 43 su 128, a partire dal 64. Serve a verificare **anche i file**, non solo il codice. |

```sh
g++ -O2 -o crt_md16 crt_md16.cc
./crt_md16 '<repo>/SNK vs CAPCOM Stronger Ed.crt'
```

Il riferimento e' `cartridge.v` del core C64 del MiSTer, riga 744
(`bank_lo <= {data_in[6:0], 1'b0}`) e il commento a riga 818:
*ROM banks are mapped to 0x200000 (2MB max)*.

---

## `lp_filter/` — un vicolo cieco, tenuto perche' e' collaudato

Vedi il suo `LEGGIMI.md`. In breve: in `fpga/io/audio/` ci sono **due** file che
dichiarano la stessa entita' `lp_filter`, e quello che sembrava ottimizzabile
non lo compila nessun target.

---

## Le trappole trovate qui dentro

- **La `sync_fifo` lavora in "fall through"**: tiene un byte gia' pescato
  sull'uscita e non piu' contato in `num_el`. Scrivendone 300 il registro ne
  dichiara 299.
- **Il filtro e' IIR**: nel confronto fra due versioni basta una sola lettura
  dell'ingresso in un istante diverso e divergono per sempre. L'ingresso va
  mosso solo fuori dalla raffica.
- **Confrontare a valori assestati**, mai a ogni ciclo: durante una raffica i
  canali in coda sono legittimamente ancora al campione precedente.


---

## `reset_cart/` -- il tasto reset della cartuccia, in C

```sh
cd '<repo>/banco/reset_cart'
g++ -O2 -o reset_edge_tb reset_edge_tb.cc && ./reset_edge_tb
```

| programma | che cosa misura |
|---|---|
| `reset_edge_tb` | La macchina a stati di `C64::checkResetButton`, **16 controlli**. La regola e' *una pressione, una ripartenza*: tasto tenuto premuto, pressioni ripetute, C64 spento e riacceso, menu aperto, freezer, opzione spenta, nessuna cartuccia. Il controllo che conta e' il terzo: **il nostro stesso reset muove la linea RESET**, e se non si rilegge la linea dopo aver agito si entra in un riavvio a catena. Ha gia' trovato un difetto vero: all'accensione del C64 la linea e' ancora bassa, e passava per una pressione del tasto. |


---

## `tastiera_c64/` -- il tasto C=, in C

```sh
cd '<repo>/banco/tastiera_c64'
g++ -O2 -o c_uguale_tb c_uguale_tb.cc && ./c_uguale_tb
```

| programma | che cosa misura |
|---|---|
| `c_uguale_tb` | La lettura del tasto **C=** (`C64::isCommodoreDown`), **18 controlli**. Il pezzo che si sbaglia facile e' uno solo: **quale bit della porta B e' il C=** quando si tira giu' PA7. Sbagliarlo di uno vuol dire aprire il menu del nastro col CTRL o con lo SPAZIO e non accorgersene mai. Il banco ricostruisce la matrice del C64 **per conto suo**, prova **tutti e 64 i tasti** uno per uno, e poi verifica che quel layout sia lo stesso che usa il firmware **leggendo `modifier_map` dal sorgente `keyboard_c64.cc`**: se qualcuno cambia la tabella, il banco se ne accorge. |


---

## `ui_menu/` -- l'apertura e la chiusura del menu, in C

```sh
cd '<repo>/banco/ui_menu'
g++ -O2 -o run_once_tb run_once_tb.cc && ./run_once_tb
```

| programma | che cosa misura |
|---|---|
| `nota_finestra_tb` | La geometria della finestra di scelta con la **riga di nota** in fondo, **14 controlli**. Il controllo che conta non e' il primo: e' che **senza nota la finestra sia IDENTICA a prima**, perche' `choice()` la usano tutti gli altri menu e spostarli di una riga per una comodita' del nastro sarebbe un difetto. Prova anche una nota piu' lunga dello schermo: il tetto a 36 colonne la tiene dentro le 40 del C64. |

| `run_once_tb` | Il ciclo di vita di `UserInterface::run_once()` con dentro la scorciatoia C=, **11 controlli**. Nasce da un difetto vero trovato sul ferro: dopo la scorciatoia, il menu normale si piantava con la sola cornice disegnata. Causa: **`release_host()` mette `doBreak = true`**, e l'uscita della scorciatoia rientrava senza rimetterlo a false; alla chiamata dopo il giro non partiva e **nessuno chiamava `release_ownership()`**, quindi il C64 restava congelato. Gli invarianti: dopo ogni apertura il C64 non resta congelato, ogni `init` ha il suo `deinit`, la seconda apertura si comporta come la prima. Dentro c'e' anche la **controprova** col difetto rimesso, che deve fallire. |
