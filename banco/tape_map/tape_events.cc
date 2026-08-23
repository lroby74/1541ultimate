/*
 * Radiografia di un TAP: dove cadono gli eventi del nastro, letti in giri di
 * contanastro secondo il modello meccanico del 1530.
 *
 * Serve a tarare: se il modello e' giusto, i punti che si vedono qui devono
 * corrispondere ai numeri letti sul contanastro vero.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <vector>

#define THRESH_BASE 2693
#define THRESH_INC     6
#define CYC_PER_MS  1000

// istante in cui comincia il giro n, in cicli da 1 us
static unsigned long long clickStart(int n)
{
    // somma di (2693 + 6i) ms per i < n, in cicli
    unsigned long long ms = (unsigned long long)THRESH_BASE * (unsigned long long)n
                          + 3ULL * (unsigned long long)n * (unsigned long long)(n - 1);
    return ms * CYC_PER_MS;
}

// giro del contanastro corrispondente a un tempo in cicli da 1 us
static int clickAt(unsigned long long cycles)
{
    unsigned long long acc = 0;
    unsigned long long th = THRESH_BASE;
    int n = 0;
    while (n < 8191) {
        unsigned long long next = acc + th * CYC_PER_MS;
        if (cycles < next) break;
        acc = next;
        th += THRESH_INC;
        n++;
    }
    return n;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1]
                     : "Turrican II.tap";
    // soglia per considerare una pausa "un vuoto", in millisecondi
    int gapMs = (argc > 2) ? atoi(argv[2]) : 20;

    FILE *fp = fopen(path, "rb");
    if (!fp) { printf("non trovo %s\n", path); return 2; }
    fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc((size_t)sz);
    if (fread(d, 1, (size_t)sz, fp) != (size_t)sz) return 2;
    fclose(fp);

    int version = d[12] & 3;
    printf("=== %s ===\nTAP v%d, %ld byte\n\n", path, version, sz);

    unsigned long long acc = 0;
    long i = 20;

    // per riconoscere i toni pilota: corse di impulsi tutti uguali
    uint32_t runValue = 0;
    long runCount = 0;
    unsigned long long runStart = 0;

    printf("%-8s %-9s %-10s %s\n", "giro", "secondi", "byte", "evento");
    printf("-------- --------- ---------- ------------------------------------\n");

    while (i < sz) {
        long tokenStart = i;
        uint32_t cyc;
        uint8_t b = d[i];
        if (b != 0) { cyc = (uint32_t)b * 8; i += 1; }
        else if (version == 0) { cyc = 20000; i += 1; }
        else {
            if (i + 3 >= sz) break;
            cyc = (uint32_t)d[i+1] | ((uint32_t)d[i+2] << 8) | ((uint32_t)d[i+3] << 16);
            i += 4;
        }

        // fine di una corsa di impulsi uguali?
        if (cyc != runValue) {
            if (runCount >= 1500) {
                printf("%-8d %-9.1f %-10ld tono pilota: %ld impulsi da %u us\n",
                       clickAt(runStart), (double)runStart / 1e6,
                       (long)0, runCount, runValue);
            }
            runValue = cyc; runCount = 1; runStart = acc;
        } else {
            runCount++;
        }

        if (cyc >= (uint32_t)gapMs * 1000) {
            int k = clickAt(acc);
            double dall_inizio = (double)(acc - clickStart(k)) / 1e6;
            double alla_fine   = (double)(clickStart(k+1) - acc) / 1e6;
            printf("%-8d %-9.1f %-10ld VUOTO di %.2f s   [nel giro da %.2f s, ne mancano %.2f]\n",
                   k, (double)acc / 1e6, tokenStart, (double)cyc / 1e6,
                   dall_inizio, alla_fine);
        }
        acc += cyc;
    }
    if (runCount >= 1500) {
        printf("%-8d %-9.1f %-10ld tono pilota: %ld impulsi da %u us\n",
               clickAt(runStart), (double)runStart / 1e6, (long)0, runCount, runValue);
    }

    printf("\ntotale: %.1f s, %d giri\n", (double)acc / 1e6, clickAt(acc));
    free(d);
    return 0;
}
