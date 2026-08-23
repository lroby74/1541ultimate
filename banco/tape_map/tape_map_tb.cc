/*
 * Banco di prova per TapeMap (software/io/tape/tape_map.cc).
 *
 * Verifica il portage in C delle formule di C64_MiSTer/rtl/tap_scanner.sv:
 *  - modello meccanico del contanastro (2693 ms + 6 ms a giro);
 *  - decodifica TAP v0 / v1 / v2;
 *  - gli offset della mappa cadono SEMPRE su un inizio di token valido
 *    (mai dentro i 4 byte di un impulso lungo);
 *  - andata e ritorno fra giro e offset.
 *
 * Il modello di riferimento qui sotto e' scritto in modo indipendente da
 * tape_map.cc, cosi' un errore uguale in tutti e due non passa inosservato.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <set>
#include "filemanager.h"
#include "../../software/io/tape/tape_map.h"

static int failures = 0;
static int checks = 0;

static void check(bool cond, const char *what)
{
    checks++;
    if (!cond) {
        printf("  FALLITO: %s\n", what);
        failures++;
    }
}

// --------------------------------------------------------------------------
// Costruzione di TAP di prova
// --------------------------------------------------------------------------
struct Tap {
    std::vector<uint8_t> bytes;
    std::vector<uint32_t> tokenOffsets;   // inizio di ogni token
    std::vector<uint32_t> tokenCycles;    // durata di ogni token, in cicli
    int version;

    void header(int v) {
        version = v;
        const char *sig = "C64-TAPE-RAW";
        for (int i = 0; i < 12; i++) bytes.push_back((uint8_t)sig[i]);
        bytes.push_back((uint8_t)v);
        for (int i = 0; i < 7; i++) bytes.push_back(0);
    }
    void shortPulse(uint8_t b) {          // b diverso da zero
        tokenOffsets.push_back((uint32_t)bytes.size());
        tokenCycles.push_back((uint32_t)b * 8);
        bytes.push_back(b);
    }
    void longPulse(uint32_t cycles) {     // token 00 + 24 bit su v1/v2
        tokenOffsets.push_back((uint32_t)bytes.size());
        tokenCycles.push_back(version == 0 ? 20000 : cycles);
        bytes.push_back(0);
        if (version != 0) {
            bytes.push_back((uint8_t)(cycles & 0xFF));
            bytes.push_back((uint8_t)((cycles >> 8) & 0xFF));
            bytes.push_back((uint8_t)((cycles >> 16) & 0xFF));
        }
    }
    void fixSize(void) {
        uint32_t n = (uint32_t)bytes.size() - 20;
        bytes[16] = (uint8_t)(n & 0xFF);
        bytes[17] = (uint8_t)((n >> 8) & 0xFF);
        bytes[18] = (uint8_t)((n >> 16) & 0xFF);
        bytes[19] = (uint8_t)((n >> 24) & 0xFF);
    }
};

// --------------------------------------------------------------------------
// Modello di riferimento, scritto per conto suo
// --------------------------------------------------------------------------
struct RefMap {
    std::vector<uint32_t> offset;    // byte da cui si riprende
    std::vector<uint32_t> residual;  // cicli dell impulso ancora da suonare
};

static RefMap referenceMap(const Tap &tap, int absMax)
{
    RefMap map;
    map.offset.push_back(0);
    map.residual.push_back(0);

    unsigned long long acc = 0;
    unsigned long long thresholdMs = 2693;
    unsigned long long target = 2693ULL * 1000ULL;

    for (size_t i = 0; i < tap.tokenOffsets.size(); i++) {
        uint32_t endOff = (i + 1 < tap.tokenOffsets.size())
                            ? tap.tokenOffsets[i + 1]
                            : (uint32_t)tap.bytes.size();
        acc += tap.tokenCycles[i];

        while ((int)map.offset.size() <= absMax && acc >= target) {
            map.offset.push_back(endOff);
            map.residual.push_back((uint32_t)(acc - target));
            thresholdMs += 6;
            target += thresholdMs * 1000ULL;
        }
        if ((int)map.offset.size() > absMax) break;
    }
    return map;
}

// somma dei cicli fino a un certo byte, camminando sui token
static unsigned long long cyclesAtOffset(const Tap &tap, uint32_t off)
{
    unsigned long long acc = 0;
    for (size_t i = 0; i < tap.tokenOffsets.size(); i++) {
        if (tap.tokenOffsets[i] >= off) break;
        acc += tap.tokenCycles[i];
    }
    return acc;
}

// istante in cui comincia il giro n, in cicli: 2693 ms piu 6 ms a giro
static unsigned long long clickStart(int n)
{
    if (n <= 0) return 0;
    unsigned long long k = (unsigned long long)n;
    return (2693ULL * k + 3ULL * k * (k - 1ULL)) * 1000ULL;
}

// --------------------------------------------------------------------------
static void runCase(const char *name, Tap &tap)
{
    printf("- %s (TAP v%d, %u byte, %u token)\n", name, tap.version,
           (unsigned)tap.bytes.size(), (unsigned)tap.tokenOffsets.size());
    tap.fixSize();

    File f(&tap.bytes[0], (uint32_t)tap.bytes.size());
    TapeMap m;
    bool ok = m.build(&f);
    check(ok, "build() riuscita");
    if (!ok) return;

    RefMap ref = referenceMap(tap, TAPEMAP_ABS_MAX);

    check(m.getVersion() == tap.version, "versione TAP letta dal byte 12");
    check(m.getMax() == (int)ref.offset.size(), "numero di giri uguale al modello");

    int n = m.getMax() < (int)ref.offset.size() ? m.getMax() : (int)ref.offset.size();
    bool same = true;
    for (int i = 0; i < n; i++) {
        uint32_t expected = (ref.offset[i] < 20) ? 20 : ref.offset[i];
        if (m.getOffset(i) != expected) {
            if (same) printf("    primo scarto al giro %d: %u contro %u\n",
                             i, (unsigned)m.getOffset(i), (unsigned)expected);
            same = false;
        }
    }
    check(same, "ogni offset coincide col modello di riferimento");

    bool sameRes = true;
    for (int i = 0; i < n; i++) {
        if (m.getResidual(i) != ref.residual[i]) sameRes = false;
    }
    check(sameRes, "ogni resto di impulso coincide col modello di riferimento");

    // >>> L'INVARIANTE CHE CONTA <<<
    // Il confine del giro k cade "residual" cicli prima della fine del token
    // che finisce a offsets[k].  Quindi il tempo di nastro al confine deve
    // essere ESATTAMENTE quello che dice il modello meccanico, anche quando il
    // confine cade in mezzo a un silenzio che nel file occupa quattro byte.
    bool esatto = true;
    for (int k = 1; k < m.getMax(); k++) {
        unsigned long long al_confine = cyclesAtOffset(tap, m.getOffset(k))
                                      - (unsigned long long)m.getResidual(k);
        if (al_confine != clickStart(k)) esatto = false;
    }
    check(esatto, ">>> ogni giro cade nel punto esatto del nastro, silenzi compresi <<<");

    bool mono = true;
    for (int i = 1; i < m.getMax(); i++) {
        if (m.getOffset(i) < m.getOffset(i - 1)) mono = false;
    }
    check(mono, "gli offset non tornano indietro");

    // >>> il controllo che conta: mai in mezzo a un token <<<
    std::set<uint32_t> safe;
    safe.insert(20);
    safe.insert((uint32_t)tap.bytes.size());
    for (size_t i = 0; i < tap.tokenOffsets.size(); i++) safe.insert(tap.tokenOffsets[i]);
    bool allSafe = true;
    for (int i = 0; i < m.getMax(); i++) {
        if (safe.find(m.getOffset(i)) == safe.end()) {
            if (allSafe) printf("    giro %d cade a %u, che non e' un inizio di token\n",
                                i, (unsigned)m.getOffset(i));
            allSafe = false;
        }
    }
    check(allSafe, "ogni offset e' un inizio di token valido");

    // clickForOffset() non deve MAI portarsi avanti rispetto al nastro, e deve
    // essere esatta ovunque gli offset siano davvero crescenti.  Dove piu' giri
    // ricadono sullo stesso byte (silenzi lunghissimi) si accetta che torni il
    // primo dei giri appaiati: quello e' il limite dichiarato del metodo.
    bool round = true;
    int tied = 0;
    for (int i = 0; i < m.getMax(); i++) {
        uint32_t off = m.getOffset(i);
        int back = m.clickForOffset(off);
        if (back > i) round = false;
        if (m.getOffset(back) != off) round = false;
        if (back != i) tied++;
    }
    check(round, "clickForOffset() non scavalca mai il giro");
    if (tied) printf("    %d giri su %d ricadono su un byte condiviso\n", tied, m.getMax());
}

// --------------------------------------------------------------------------
// Carica un TAP vero da disco e lo scompone in token, senza usare tape_map.cc
// --------------------------------------------------------------------------
static bool loadTap(const char *path, Tap &t)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 20) { fclose(fp); return false; }
    t.bytes.resize((size_t)sz);
    if (fread(&t.bytes[0], 1, (size_t)sz, fp) != (size_t)sz) { fclose(fp); return false; }
    fclose(fp);

    t.version = t.bytes[12] & 0x03;
    size_t i = 20;
    while (i < t.bytes.size()) {
        uint8_t b = t.bytes[i];
        if (b != 0) {
            t.tokenOffsets.push_back((uint32_t)i);
            t.tokenCycles.push_back((uint32_t)b * 8);
            i += 1;
        } else if (t.version == 0) {
            t.tokenOffsets.push_back((uint32_t)i);
            t.tokenCycles.push_back(20000);
            i += 1;
        } else {
            if (i + 3 >= t.bytes.size()) break;   // token troncato a fine file
            uint32_t v = (uint32_t)t.bytes[i+1] | ((uint32_t)t.bytes[i+2] << 8) | ((uint32_t)t.bytes[i+3] << 16);
            t.tokenOffsets.push_back((uint32_t)i);
            t.tokenCycles.push_back(v);
            i += 4;
        }
    }
    return true;
}

int main(int argc, char **argv)
{
    printf("=== Banco TapeMap ===\n");

    // 1) v1 tipico: impulsi corti uniformi
    {
        Tap t; t.header(1);
        for (int i = 0; i < 60000; i++) t.shortPulse(0x30);   // 384 cicli l'uno
        runCase("v1, impulsi corti uniformi", t);
    }
    // 2) v1 con silenzi lunghi fra i blocchi
    {
        Tap t; t.header(1);
        for (int b = 0; b < 8; b++) {
            for (int i = 0; i < 4000; i++) t.shortPulse((uint8_t)(0x20 + (i & 0x1F)));
            t.longPulse(2000000);   // 2 s di silenzio
        }
        runCase("v1, blocchi separati da silenzi da 2 s", t);
    }
    // 3) v0: lo zero vale 20000 cicli
    {
        Tap t; t.header(0);
        for (int b = 0; b < 20; b++) {
            for (int i = 0; i < 3000; i++) t.shortPulse((uint8_t)(0x28 + (i & 0x0F)));
            t.longPulse(0);
        }
        runCase("v0, zero come impulso lungo fisso", t);
    }
    // 4) v2, mezze onde
    {
        Tap t; t.header(2);
        for (int i = 0; i < 50000; i++) t.shortPulse((uint8_t)(0x18 + (i & 0x07)));
        runCase("v2, mezze onde", t);
    }
    // 5) nastro cortissimo: meno di un giro
    {
        Tap t; t.header(1);
        for (int i = 0; i < 100; i++) t.shortPulse(0x40);
        runCase("nastro piu corto di un giro", t);
    }
    // 6) un impulso enorme che da solo copre decine di giri
    {
        Tap t; t.header(1);
        t.shortPulse(0x30);
        t.longPulse(16000000);   // 16 s, quasi sei giri
        for (int i = 0; i < 1000; i++) t.shortPulse(0x30);
        runCase("un solo impulso da 16 s", t);
    }

    // File TAP veri passati sulla riga di comando
    for (int a = 1; a < argc; a++) {
        Tap t;
        if (!loadTap(argv[a], t)) {
            printf("- %s: NON LEGGIBILE\n", argv[a]);
            failures++;
            continue;
        }
        File f(&t.bytes[0], (uint32_t)t.bytes.size());
        TapeMap m;
        printf("- file vero %s (TAP v%d, %u byte, %u token)\n", argv[a], t.version,
               (unsigned)t.bytes.size(), (unsigned)t.tokenOffsets.size());
        check(m.build(&f), "build() riuscita su file vero");
        if (!m.isValid()) continue;
        RefMap ref = referenceMap(t, TAPEMAP_ABS_MAX);
        check(m.getMax() == (int)ref.offset.size(), "numero di giri uguale al modello");
        bool same = true;
        int n = m.getMax() < (int)ref.offset.size() ? m.getMax() : (int)ref.offset.size();
        for (int i = 0; i < n; i++) {
            uint32_t expected = (ref.offset[i] < 20) ? 20 : ref.offset[i];
            if (m.getOffset(i) != expected) same = false;
        }
        check(same, "ogni offset coincide col modello di riferimento");
    }

    printf("\n%d controlli, %d falliti\n", checks, failures);
    return failures ? 1 : 0;
}
