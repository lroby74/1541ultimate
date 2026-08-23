/*
 * tape_map.cc
 *
 * Costruzione della mappa "giro del contanastro -> offset nel file TAP".
 * Le formule sono quelle di C64_MiSTer/rtl/tap_scanner.sv, riga per riga.
 */

#include <stdio.h>
#include <stdlib.h>
#include "tape_map.h"
#include "filemanager.h"

// Il blocco di lettura.  Non e' una questione di memoria - in memoria ci sta
// sempre e solo UN blocco, qualunque sia la dimensione del nastro - ma di
// NUMERO DI LETTURE: ogni lettura e' un giro completo su USB e sul file
// system, e su un nastro da 2,4 MB con blocchi da 4K erano 592 andate e
// ritorno.  A 64K diventano 38.
// Se la memoria non basta si scende, fino a un valore che non puo' fallire.
#define SCAN_BLOCK      65536
#define SCAN_BLOCK_MIN   4096

TapeMap :: TapeMap()
{
    offsets = 0;
    residual = 0;
    mapMax = 0;
    version = 0;
    totalCycles = 0;
}

TapeMap :: ~TapeMap()
{
    clear();
}

void TapeMap :: clear(void)
{
    if (offsets) {
        delete[] offsets;
        offsets = 0;
    }
    if (residual) {
        delete[] residual;
        residual = 0;
    }
    mapMax = 0;
    version = 0;
    totalCycles = 0;
}

uint64_t TapeMap :: clickStartCycles(int click)
{
    if (click <= 0) {
        return 0;
    }
    // somma di (2693 + 6i) ms per i < click, in cicli da 1 us
    uint64_t n  = (uint64_t)click;
    uint64_t ms = (uint64_t)TAPEMAP_THRESH_BASE * n + 3ULL * n * (n - 1ULL);
    return ms * (uint64_t)TAPEMAP_CYCLES_PER_MS;
}

int TapeMap :: clickForCycles(uint64_t cycles)
{
    // Il modello e' quello meccanico: il primo giro dura 2693 ms e ogni giro
    // successivo 6 ms in piu', perche' il perno di raccolta si ingrossa.
    uint64_t acc = 0;
    uint64_t th  = (uint64_t)TAPEMAP_THRESH_BASE * (uint64_t)TAPEMAP_CYCLES_PER_MS;
    int n = 0;
    while (n < TAPEMAP_ABS_MAX) {
        uint64_t next = acc + th;
        if (cycles < next) {
            break;
        }
        acc = next;
        th += (uint64_t)TAPEMAP_THRESH_INC * (uint64_t)TAPEMAP_CYCLES_PER_MS;
        n++;
    }
    return n;
}

uint32_t TapeMap :: getResidual(int click)
{
    if (!isValid() || (click <= 0)) {
        return 0;
    }
    if (click >= mapMax) {
        click = mapMax - 1;
    }
    return residual[click];
}

uint32_t TapeMap :: getOffset(int click)
{
    if (!isValid()) {
        return TAPEMAP_HEADER_SIZE;
    }
    if (click < 0) {
        click = 0;
    }
    if (click >= mapMax) {
        click = mapMax - 1;
    }
    uint32_t off = offsets[click];
    return (off < TAPEMAP_HEADER_SIZE) ? TAPEMAP_HEADER_SIZE : off;
}

int TapeMap :: clickForOffset(uint32_t off)
{
    if (!isValid()) {
        return 0;
    }
    // offsets[] non decresce mai: cerco l'ultimo indice con offsets[i] <= off.
    int lo = 0, hi = mapMax - 1, res = 0;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (offsets[mid] <= off) {
            res = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    // Dentro un impulso molto lungo (un silenzio di parecchi secondi) piu'
    // giri consecutivi ricadono sullo STESSO byte: da un offset non si puo'
    // sapere a quale dei due si e' arrivati.  Si restituisce il primo, cosi'
    // il contatore non si porta mai avanti rispetto al nastro.  Il numero
    // esatto lo tiene comunque il TapeController, che dopo un riavvolgimento
    // riparte dal giro voluto invece di ricavarlo dall'offset.
    while ((res > 0) && (offsets[res - 1] == offsets[res])) {
        res--;
    }
    return res;
}

bool TapeMap :: build(File *f, TapeMapProgress cb, void *ctx)
{
    clear();

    if (!f || !f->isValid()) {
        return false;
    }

    uint32_t fileSize = f->get_size();
    if (fileSize <= TAPEMAP_HEADER_SIZE) {
        return false;
    }

    uint32_t blockSize = SCAN_BLOCK;
    uint8_t *buf = 0;
    while (!buf) {
        buf = new uint8_t[blockSize];
        if (!buf) {
            if (blockSize <= SCAN_BLOCK_MIN) {
                return false;
            }
            blockSize >>= 1;
        }
    }
    offsets  = new uint32_t[TAPEMAP_ABS_MAX + 1];
    residual = new uint32_t[TAPEMAP_ABS_MAX + 1];
    if (!offsets || !residual) {
        delete[] buf;
        clear();
        return false;
    }

    // --- intestazione: il byte 12 e' la versione del TAP -------------------
    uint32_t got = 0;
    if (f->seek(0) != FR_OK) {
        delete[] buf; clear(); return false;
    }
    if ((f->read(buf, TAPEMAP_HEADER_SIZE, &got) != FR_OK) || (got != TAPEMAP_HEADER_SIZE)) {
        delete[] buf; clear(); return false;
    }
    version = (int)(buf[12] & 0x03);

    // --- stato dello scandaglio, identico a tap_scanner.sv -----------------
    uint64_t timeAcc      = 0;                      // cicli da 1 us gia' scorsi
    uint32_t thresholdMs  = TAPEMAP_THRESH_BASE;    // durata del giro corrente
    uint64_t target       = (uint64_t)TAPEMAP_THRESH_BASE * TAPEMAP_CYCLES_PER_MS;

    // Il giro 0 e' l'inizio del nastro, cosi' il riavvolgimento a fondo corsa
    // ricade sempre su un punto sicuro.
    offsets[0]  = 0;
    residual[0] = 0;
    int count = 1;

    uint32_t offset = TAPEMAP_HEADER_SIZE;  // byte in esame
    uint32_t bufBase = 0;                   // offset del primo byte in buf
    uint32_t bufLen = 0;

    if (f->seek(TAPEMAP_HEADER_SIZE) != FR_OK) {
        delete[] buf; clear(); return false;
    }
    bufBase = TAPEMAP_HEADER_SIZE;

    // legge il blocco che contiene 'want'; ritorna false a fine file
    #define ENSURE(want) \
        do { \
            if (((want) < bufBase) || ((want) >= bufBase + bufLen)) { \
                if ((want) != bufBase + bufLen) { \
                    if (f->seek(want) != FR_OK) { goto finished; } \
                } \
                bufBase = (want); \
                if (f->read(buf, blockSize, &bufLen) != FR_OK) { goto finished; } \
                if (bufLen == 0) { goto finished; } \
                if (cb) { cb(ctx, bufBase, fileSize); } \
            } \
        } while(0)

    while (offset < fileSize) {
        uint32_t cycles;

        ENSURE(offset);
        uint8_t b = buf[offset - bufBase];

        if (b != 0) {
            cycles = ((uint32_t)b) * 8;
            offset += 1;
        } else if (version == 0) {
            // In TAP v0 lo zero e' un impulso lungo di durata fissa.
            cycles = TAPEMAP_V0_ZERO_CYCLES;
            offset += 1;
        } else {
            // v1 / v2: 00 seguito da 24 bit little endian.
            uint32_t v = 0;
            bool truncated = false;
            for (int i = 0; i < 3; i++) {
                uint32_t a = offset + 1 + i;
                if (a >= fileSize) { truncated = true; break; }
                ENSURE(a);
                v |= ((uint32_t)buf[a - bufBase]) << (8 * i);
            }
            if (truncated) {
                break;
            }
            cycles = v;
            offset += 4;
        }

        uint32_t pulseEndOffset = offset;
        timeAcc += (uint64_t)cycles;

        // Un solo impulso puo' scavalcare piu' giri: un silenzio da tredici
        // secondi ne contiene cinque, e nel file occupa quattro byte.
        while ((count <= TAPEMAP_ABS_MAX) && (timeAcc >= target)) {
            // Si riparte SEMPRE dall'inizio del token successivo, dopo aver
            // suonato il pezzo di impulso che resta.  Cosi' il punto sul nastro
            // e' esatto anche in mezzo a un silenzio: senza il resto, avvolgere
            // dentro un silenzio porterebbe al suo bordo e poi il player
            // suonerebbe tutto il silenzio da capo, mandando avanti il
            // contatore di parecchi giri.
            offsets[count]  = pulseEndOffset;
            residual[count] = (uint32_t)(timeAcc - target);
            count++;
            thresholdMs += TAPEMAP_THRESH_INC;
            target += (uint64_t)thresholdMs * TAPEMAP_CYCLES_PER_MS;
        }
        if (count > TAPEMAP_ABS_MAX) {
            break;
        }
    }

finished:
    #undef ENSURE
    delete[] buf;

    mapMax = count;
    totalCycles = timeAcc;

    f->seek(TAPEMAP_HEADER_SIZE);

    printf("TapeMap: TAP v%d, %lu byte, %d giri, %lu s di nastro\n",
           version, (unsigned long)fileSize, mapMax,
           (unsigned long)(totalCycles / 1000000ULL));

    return isValid();
}
