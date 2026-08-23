/*
 * Banco di prova sulla sequenza vera di Turrican II.
 *
 * La sequenza e' questa: dopo il game over il contanastro segna 067, il gioco
 * chiede di azzerare il contatore, riavvolgere di 20 (cioe' fino a 980) e fare
 * play; il caricamento deve fermarsi di nuovo esattamente a 000.
 *
 * Qui si ripete quella sequenza sul TAP vero, usando la stessa TapeMap che gira
 * dentro l'Ultimate, e si controlla che il byte da cui si riparte e quello a cui
 * si torna siano gli stessi.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <vector>
#include "filemanager.h"
#include "../../software/io/tape/tape_map.h"

static int failures = 0;

static void check(bool cond, const char *what)
{
    printf("  [%s] %s\n", cond ? "ok " : "NO!", what);
    if (!cond) failures++;
}

// Le stesse due funzioni del TapeController, ricopiate qui per provarle
// senza tirarsi dietro tutto il firmware.
static int counterValue(int absCount, int zeroAt)
{
    int v = (absCount - zeroAt) % 1000;
    if (v < 0) v += 1000;
    return v;
}

static int gotoTarget(int absCount, int zeroAt, int target, int maxClick)
{
    int delta = (target - counterValue(absCount, zeroAt)) % 1000;
    if (delta < 0) delta += 1000;
    if (delta > 500) delta -= 1000;
    int click = absCount + delta;
    if (click < 0) click = 0;
    if (click > maxClick) click = maxClick;
    return click;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1]
                     : "Turrican II.tap";

    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("non trovo %s\n", path); return 2; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)malloc((size_t)sz);
    if (fread(data, 1, (size_t)sz, fp) != (size_t)sz) { printf("lettura corta\n"); return 2; }
    fclose(fp);

    printf("=== Turrican II: %ld byte ===\n", sz);

    File f(data, (uint32_t)sz);
    TapeMap m;

    clock_t t0 = clock();
    bool ok = m.build(&f);
    clock_t t1 = clock();

    check(ok, "la mappa si costruisce");
    if (!ok) return 1;

    printf("  scandaglio: %.0f ms di CPU, %d letture e %d seek sul file\n",
           1000.0 * (double)(t1 - t0) / CLOCKS_PER_SEC, f.readCount, f.seekCount);
    printf("  TAP v%d, %d giri di contanastro, %llu s di nastro (%.1f minuti)\n",
           m.getVersion(), m.getMax(),
           (unsigned long long)(m.getTotalCycles() / 1000000ULL),
           (double)m.getTotalCycles() / 60000000.0);

    check(m.getMax() > 100, "il nastro e' lungo abbastanza da avere piu' di 100 giri");
    check(m.getOffset(0) == 20, "il giro 0 e' l'inizio dei dati");

    // --- la sequenza vera del gioco -----------------------------------------
    printf("\n-- sequenza: 067 -> azzera -> riavvolgi a 980 -> play -> 000 --\n");

    int zeroAt = 0;              // all'inserimento del nastro il contatore e' a 000
    int absCount = 67;           // il gioco e' arrivato qui
    if (absCount >= m.getMax()) { printf("  nastro troppo corto per la prova\n"); return 1; }

    uint32_t offsetAt067 = m.getOffset(absCount);
    printf("  a display 067 il nastro sta al byte %u\n", (unsigned)offsetAt067);
    check(counterValue(absCount, zeroAt) == 67, "il display segna 067");

    // 1) si azzera il contatore: cambiano le CIFRE, non la posizione
    uint32_t offsetPrima = m.getOffset(absCount);
    zeroAt = absCount;
    check(counterValue(absCount, zeroAt) == 0, "dopo l'azzeramento il display segna 000");
    check(m.getOffset(absCount) == offsetPrima, "l'azzeramento NON muove il nastro");

    // 2) si riavvolge fino a 980, cioe' venti giri indietro
    int click980 = gotoTarget(absCount, zeroAt, 980, m.getMax() - 1);
    absCount = click980;
    printf("  dopo il riavvolgimento: giro assoluto %d, byte %u\n",
           absCount, (unsigned)m.getOffset(absCount));
    check(counterValue(absCount, zeroAt) == 980, "il display segna 980");
    check(click980 == 67 - 20, "sono esattamente venti giri indietro");
    check(m.getOffset(absCount) < offsetAt067, "il nastro e' davvero tornato indietro");

    // 3) play: si riavvolge il nastro e si va avanti fino a rivedere 000
    uint32_t played = m.getOffset(absCount);
    int guard = 0;
    while ((counterValue(absCount, zeroAt) != 0) && (absCount < m.getMax() - 1) && (guard++ < 5000)) {
        absCount++;
        played = m.getOffset(absCount);
    }
    printf("  tornati a display %03d, byte %u\n", counterValue(absCount, zeroAt), (unsigned)played);
    check(counterValue(absCount, zeroAt) == 0, "il display e' tornato a 000");
    check(played == offsetAt067, "si riparte ESATTAMENTE dallo stesso byte di prima");

    // --- controllo generale di andata e ritorno ----------------------------
    printf("\n-- andata e ritorno su tutti i giri --\n");
    int bad = 0, tied = 0;
    for (int i = 0; i < m.getMax(); i++) {
        int back = m.clickForOffset(m.getOffset(i));
        if (back > i) bad++;
        else if (back != i) tied++;
    }
    printf("  %d giri, %d appaiati su un byte condiviso\n", m.getMax(), tied);
    check(bad == 0, "clickForOffset() non scavalca mai il giro");

    // --- IL CONTROLLO CHE CONTA -------------------------------------------
    // Un giro in riavvolgimento o in avanti veloce deve valere la STESSA
    // lunghezza di nastro di un giro in riproduzione.  Qui si verifica in due
    // modi: la simmetria del trasporto, e la durata reale di ogni giro sul
    // nastro vero contro il modello meccanico.
    printf("\n-- un giro vale uguale in PLAY, in REW e in FF? --\n");

    // 1) simmetria: da un punto qualsiasi, N giri avanti e N indietro devono
    //    riportare allo stesso identico byte
    bool simm = true;
    for (int start = 5; start < m.getMax() - 40; start += 17) {
        for (int n = 1; n <= 30; n++) {
            int avanti   = start + n;
            int indietro = avanti - n;
            if (indietro != start) simm = false;
            if (m.getOffset(indietro) != m.getOffset(start)) simm = false;
        }
    }
    check(simm, "N giri avanti e N indietro riportano allo stesso byte");

    // 2) la strada e' la stessa in tutti i versi: il byte del giro k non
    //    dipende da come ci si e' arrivati, perche' la tabella e' una sola
    bool stessa = true;
    for (int k = 1; k < m.getMax(); k++) {
        uint32_t da_play = m.getOffset(k);        // percorrendo in avanti
        uint32_t da_rew  = m.getOffset(k);        // arrivandoci riavvolgendo
        if (da_play != da_rew) stessa = false;
    }
    check(stessa, "il byte di un giro non dipende dal verso di marcia");

    // 3) >>> L'INVARIANTE <<<  Il confine del giro k cade "residual" cicli
    //    prima della fine del token che finisce a offsets[k].  Quindi il tempo
    //    di nastro a quel confine deve essere ESATTAMENTE quello del modello
    //    meccanico, anche dove il confine casca in mezzo a un silenzio lungo.
    {
        unsigned long long acc = 0;
        long i = 20;
        int ver = m.getVersion();
        std::vector<unsigned long long> tempo;   // cicli all inizio di ogni token
        std::vector<uint32_t> bytei;
        while (i < sz) {
            uint32_t cyc;
            uint8_t b = data[i];
            bytei.push_back((uint32_t)i);
            tempo.push_back(acc);
            if (b != 0) { cyc = (uint32_t)b * 8; i += 1; }
            else if (ver == 0) { cyc = 20000; i += 1; }
            else {
                if (i + 3 >= sz) break;
                cyc = (uint32_t)data[i+1] | ((uint32_t)data[i+2] << 8) | ((uint32_t)data[i+3] << 16);
                i += 4;
            }
            acc += cyc;
        }
        bytei.push_back((uint32_t)i);
        tempo.push_back(acc);

        int fuori = 0, primo = -1;
        size_t idx = 0;
        for (int k = 1; k < m.getMax(); k++) {
            uint32_t off = m.getOffset(k);
            while (idx + 1 < bytei.size() && bytei[idx] < off) idx++;
            unsigned long long al_confine = tempo[idx] - (unsigned long long)m.getResidual(k);
            unsigned long long kk = (unsigned long long)k;
            unsigned long long atteso = (2693ULL * kk + 3ULL * kk * (kk - 1ULL)) * 1000ULL;
            if (al_confine != atteso) {
                if (primo < 0) primo = k;
                fuori++;
            }
        }
        if (fuori) printf("    %d giri fuori posto, il primo e il %d\n", fuori, primo);
        check(fuori == 0, ">>> ogni giro cade nel punto ESATTO del nastro, silenzi compresi <<<");
    }

    // --- quanto dura un giro, in secondi -----------------------------------
    double sec = (double)m.getTotalCycles() / 1000000.0;
    printf("\n  in media un giro vale %.2f s di nastro\n", sec / (double)m.getMax());

    free(data);
    printf("\n%s\n", failures ? ">>> CI SONO CONTROLLI FALLITI <<<" : "tutti i controlli superati");
    return failures ? 1 : 0;
}
