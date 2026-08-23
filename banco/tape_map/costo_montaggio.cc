// Quanto costa montare un TAP: quante letture e quante ricerche fa la
// costruzione della mappa del contanastro, e quanto tempo ci mette la parte
// di calcolo.
//
// Serve a rispondere alla domanda giusta: "2,4 MB non sono troppi per
// la U2+?".  La riproduzione non c'entra (legge 512 byte alla volta come ha
// sempre fatto): il carico e' tutto nel momento del montaggio.
//
//   g++ -O2 -I. -o costo_montaggio costo_montaggio.cc ../../software/io/tape/tape_map.cc
//   ./costo_montaggio <un TAP grosso>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "filemanager.h"
#include "../../software/io/tape/tape_map.h"

int main(int argc, char **argv)
{
    const char *nome = (argc > 1) ? argv[1] : "Turrican II.tap";

    FILE *fp = fopen(nome, "rb");
    if (!fp) {
        printf("non trovo %s\n", nome);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = new uint8_t[n];
    if (fread(d, 1, n, fp) != (size_t)n) {
        printf("lettura incompleta\n");
        return 1;
    }
    fclose(fp);

    File f(d, (uint32_t)n);
    TapeMap m;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    bool ok = m.build(&f);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    printf("file            : %s\n", nome);
    printf("dimensione      : %ld byte (%.2f MB)\n", n, n / 1048576.0);
    printf("mappa valida    : %s, %d giri\n", ok ? "si" : "NO", m.getMax());
    printf("letture         : %d\n", f.readCount);
    printf("ricerche (seek) : %d\n", f.seekCount);
    printf("calcolo su PC   : %.1f ms\n", ms);
    printf("\n");
    printf(">>> sulla U2+ il costo e' quasi tutto nelle %d letture da USB <<<\n", f.readCount);

    delete[] d;
    return 0;
}
