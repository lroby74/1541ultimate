/*
 * tape_map.h
 *
 * Mappa "giro del contanastro -> offset nel file TAP", per l'Ultimate-II+.
 *
 * Portage in C delle formule usate dal C64_MiSTer (rtl/tap_scanner.sv), che a
 * loro volta seguono la base tempi di VICE.  Sull'Ultimate non serve logica
 * nuova nell'FPGA per questo: il conto e' aritmetica pura sui byte del TAP.
 *
 * Modello meccanico del 1530: il nastro passa a velocita' costante, ma il
 * perno di raccolta si ingrossa, quindi ogni giro dura un po' di piu' del
 * precedente.  Da qui le due costanti THRESH_BASE / THRESH_INC.
 */

#ifndef TAPE_MAP_H
#define TAPE_MAP_H

#include <stdint.h>

class File;

// Avviso di avanzamento durante la scansione, cosi' un nastro grosso non
// sembra un blocco.  'fatto' e 'totale' sono in byte.  Puo' essere NULL.
typedef void (*TapeMapProgress)(void *ctx, uint32_t fatto, uint32_t totale);

// Le stesse identiche costanti di rtl/tap_scanner.sv e rtl/tape_counter.sv
#define TAPEMAP_THRESH_BASE     2693    // ms per giro a inizio nastro
#define TAPEMAP_THRESH_INC         6    // ms in piu' a ogni giro
#define TAPEMAP_ABS_MAX         8191    // saturazione, 13 bit come sul MiSTer
#define TAPEMAP_CYCLES_PER_MS   1000    // base tempi TAP: 1 MHz esatto
#define TAPEMAP_V0_ZERO_CYCLES 20000    // in TAP v0 il byte 00 e' un impulso lungo
#define TAPEMAP_HEADER_SIZE       20    // i dati cominciano dopo l'intestazione

class TapeMap
{
    // offsets[n] = byte del TAP da cui si riprende al giro n, cioe' l'inizio
    //              del token SUCCESSIVO a quello in cui cade il confine
    // residual[n] = quanti cicli restano da suonare di quel token, al confine.
    // I due insieme individuano il punto ESATTO del nastro, anche quando il
    // confine cade in mezzo a un silenzio lungo che nel file occupa 4 byte.
    uint32_t *offsets;
    uint32_t *residual;
    int       mapMax;       // quanti giri sono stati mappati
    int       version;      // 0, 1 o 2, letto dal byte 12 dell'intestazione
    uint64_t  totalCycles;  // durata totale del nastro, in cicli da 1 us

public:
    TapeMap();
    ~TapeMap();

    // Scorre l'intero file una volta e costruisce la mappa.
    // Lascia il file riposizionato all'inizio dei dati.
    // La lettura va a blocchi: in memoria non ci sta mai piu' di un blocco,
    // qualunque sia la dimensione del nastro.
    bool build(File *f, TapeMapProgress cb = 0, void *ctx = 0);
    void clear(void);

    bool     isValid(void)     { return (offsets != 0) && (mapMax > 0); }
    int      getMax(void)      { return mapMax; }
    int      getVersion(void)  { return version; }
    uint64_t getTotalCycles(void) { return totalCycles; }

    // Offset nel file da cui far ripartire la riproduzione al giro 'click'.
    uint32_t getOffset(int click);

    // Cicli dell'impulso in corso che restano da suonare a quel confine.
    // Vanno emessi PRIMA di ripartire dall'offset, o si perde un pezzo di
    // nastro (o se ne suona uno di troppo) e il contatore va fuori posto.
    uint32_t getResidual(int click);

    // Istante in cui comincia il giro n, in cicli da 1 us.  E' il modello
    // meccanico: soglia 2693 ms, piu' 6 ms a ogni giro.
    static uint64_t clickStartCycles(int click);

    // Il giro in cui si trova il nastro dopo 'cycles' cicli dall'inizio.
    static int clickForCycles(uint64_t cycles);

    // Giro del contanastro corrispondente a un offset nel file (ricerca binaria).
    int      clickForOffset(uint32_t off);
};

#endif /* TAPE_MAP_H */
