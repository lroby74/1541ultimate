// Banco di prova per la lettura del tasto C= (C64::isCommodoreDown).
//
// Il pezzo che si sbaglia facile e' UNO SOLO: quale bit della porta B e' il C=
// quando si tira giu' PA7.  Sbagliarlo di uno vuol dire aprire il menu del
// nastro tenendo premuto CTRL, o lo SPAZIO, e non accorgersene mai al banco.
//
// Qui la matrice viene ricostruita dal layout vero del C64 - scritto per conto
// suo, tasto per tasto - e poi si controlla anche che quel layout sia lo stesso
// che usa il firmware, leggendo la tabella `modifier_map` direttamente dal
// sorgente `keyboard_c64.cc`.  Se un giorno qualcuno cambia la tabella, il
// banco se ne accorge.
//
//   g++ -O2 -o c_uguale_tb c_uguale_tb.cc && ./c_uguale_tb

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

static int errori = 0;

static void verifica(bool cond, const char *testo)
{
    if (cond) {
        printf("  ok  : %s\n", testo);
    } else {
        printf("  NO! : %s\n", testo);
        errori++;
    }
}

// --------------------------------------------------------------------------
// La matrice del C64, riga per riga.  PA e' la selezione (attiva bassa), PB e'
// quello che si legge (un tasto premuto porta il suo bit a zero).
// --------------------------------------------------------------------------
static const char *matrice[8][8] = {
 /* PA0 */ { "INST/DEL", "RETURN", "CRSR-DX", "F7", "F1", "F3", "F5", "CRSR-GIU" },
 /* PA1 */ { "3", "W", "A", "4", "Z", "S", "E", "SHIFT-SX" },
 /* PA2 */ { "5", "R", "D", "6", "C", "F", "T", "X" },
 /* PA3 */ { "7", "Y", "G", "8", "B", "H", "U", "V" },
 /* PA4 */ { "9", "I", "J", "0", "M", "K", "O", "N" },
 /* PA5 */ { "+", "P", "L", "-", ".", ":", "@", "," },
 /* PA6 */ { "STERLINA", "*", ";", "CLR/HOME", "SHIFT-DX", "=", "freccia-su", "/" },
 /* PA7 */ { "1", "freccia-sx", "CTRL", "2", "SPAZIO", "C=", "Q", "RUN/STOP" },
};

// --------------------------------------------------------------------------
// Un CIA finto: si dice quali tasti sono premuti, e lui risponde come il vero.
// --------------------------------------------------------------------------
struct CiaFinto {
    bool premuto[8][8];
    uint8_t pa;
    int  ballerino;      // quante letture sbagliate prima di stare fermo

    CiaFinto() { memset(premuto, 0, sizeof(premuto)); pa = 0xFF; ballerino = 0; }

    void premi(const char *nome)
    {
        for (int r = 0; r < 8; r++)
            for (int c = 0; c < 8; c++)
                if (!strcmp(matrice[r][c], nome)) premuto[r][c] = true;
    }

    void scriviPA(uint8_t v) { pa = v; }

    uint8_t leggiPB(void)
    {
        if (ballerino > 0) {
            // Il bus non si e' ancora assestato.  Deve dare valori DIVERSI a
            // ogni lettura: un valore sbagliato ma fermo non lo distingue da
            // uno buono nessun algoritmo, ne' il nostro ne' quello del
            // firmware, e chiederglielo sarebbe chiedere l'impossibile.
            ballerino--;
            return (uint8_t)(0xA5 ^ (ballerino << 3));
        }
        uint8_t pb = 0xFF;
        for (int r = 0; r < 8; r++) {
            if (pa & (1 << r)) continue;         // riga non selezionata
            for (int c = 0; c < 8; c++) {
                if (premuto[r][c]) pb &= ~(1 << c);
            }
        }
        return pb;
    }
};

// --------------------------------------------------------------------------
// La funzione sotto esame, copiata riga per riga da c64.cc.
// --------------------------------------------------------------------------
static bool isCommodoreDown(CiaFinto &cia)
{
    cia.scriviPA(0x7F);
    cia.scriviPA(0x7F);
    uint8_t riga = cia.leggiPB();
    for (int i = 0; i < 8; i++) {
        cia.scriviPA(0x7F);
        uint8_t ancora = cia.leggiPB();
        if (ancora == riga) break;
        riga = ancora;
    }
    cia.scriviPA(0xFF);

    return (riga & 0x20) == 0;
}

// --------------------------------------------------------------------------
// La tabella dei modificatori, letta dal sorgente vero del firmware.
// --------------------------------------------------------------------------
static bool leggi_modifier_map(const char *percorso, int valori[64])
{
    FILE *fp = fopen(percorso, "rb");
    if (!fp) return false;
    static char testo[200000];
    size_t n = fread(testo, 1, sizeof(testo) - 1, fp);
    testo[n] = 0;
    fclose(fp);

    char *p = strstr(testo, "modifier_map[]");
    if (!p) return false;
    p = strchr(p, '{');
    if (!p) return false;
    p++;

    int quanti = 0;
    while (*p && *p != '}' && quanti < 64) {
        if (p[0] == '/' && p[1] == '/') {            // via i commenti a fine riga
            while (*p && *p != '\n') p++;
            continue;
        }
        if (p[0] == '0' && p[1] == 'x') {
            valori[quanti++] = (int)strtol(p, &p, 16);
            continue;
        }
        p++;
    }
    return quanti == 64;
}

int main(int argc, char **argv)
{
    const char *sorgente = (argc > 1) ? argv[1]
        : "../../software/io/c64/keyboard_c64.cc";

    printf("--- 1. il C= e nessun altro ---\n");
    {
        CiaFinto cia;
        verifica(!isCommodoreDown(cia), "a tastiera libera: no");
    }
    {
        CiaFinto cia; cia.premi("C=");
        verifica(isCommodoreDown(cia), ">>> col solo C= premuto: si <<<");
    }
    // i vicini di casa nella stessa riga: sono loro che si prendono per sbaglio
    const char *vicini[] = { "CTRL", "SPAZIO", "Q", "RUN/STOP", "1", "2", "freccia-sx" };
    for (unsigned i = 0; i < sizeof(vicini)/sizeof(vicini[0]); i++) {
        CiaFinto cia; cia.premi(vicini[i]);
        char t[64];
        sprintf(t, "col %s premuto: no", vicini[i]);
        verifica(!isCommodoreDown(cia), t);
    }

    printf("\n--- 2. tutti i 64 tasti, uno per uno ---\n");
    {
        int sbagliati = 0;
        for (int r = 0; r < 8; r++) {
            for (int c = 0; c < 8; c++) {
                CiaFinto cia; cia.premi(matrice[r][c]);
                bool atteso = !strcmp(matrice[r][c], "C=");
                if (isCommodoreDown(cia) != atteso) {
                    printf("        sbagliato su %s\n", matrice[r][c]);
                    sbagliati++;
                }
            }
        }
        verifica(sbagliati == 0, "un tasto solo dei 64 fa scattare la scorciatoia");
    }

    printf("\n--- 3. il C= insieme a qualcos'altro ---\n");
    {
        CiaFinto cia; cia.premi("C="); cia.premi("SHIFT-SX"); cia.premi("SPAZIO");
        verifica(isCommodoreDown(cia), "C= piu' shift piu' spazio: si");
    }
    {
        CiaFinto cia; cia.premi("C="); cia.premi("CTRL");
        verifica(isCommodoreDown(cia), "C= piu' CTRL: si");
    }

    printf("\n--- 4. il bus che non si e' ancora assestato ---\n");
    {
        CiaFinto cia; cia.premi("C="); cia.ballerino = 2;
        verifica(isCommodoreDown(cia), "due letture ballerine: le scavalca e vede il C=");
    }
    {
        CiaFinto cia; cia.ballerino = 2;   // nessun tasto premuto
        verifica(!isCommodoreDown(cia), "e senza tasti premuti non se ne inventa uno");
    }

    printf("\n--- 5. il layout e' lo stesso che usa il firmware ---\n");
    {
        int mm[64];
        if (!leggi_modifier_map(sorgente, mm)) {
            printf("  NO! : non riesco a leggere modifier_map da %s\n", sorgente);
            errori++;
        } else {
            // Nel firmware: 0x01 = shift, 0x02 = C=, 0x04 = CTRL.
            // La posizione del C= dev'essere PA7 (riga 7), bit 5 -> indice 61.
            verifica(mm[61] == 0x02, "modifier_map dice C= all'indice 61 (PA7, bit 5)");
            verifica(mm[58] == 0x04, "e CTRL all'indice 58: sono due tasti diversi");
            verifica(mm[15] == 0x01 && mm[52] == 0x01, "i due shift al loro posto");
            int quanti_c = 0;
            for (int i = 0; i < 64; i++) if (mm[i] == 0x02) quanti_c++;
            verifica(quanti_c == 1, "e di C= ce n'e' uno solo in tutta la matrice");
        }
    }

    printf("\n");
    if (errori == 0) {
        printf("=== BANCO SUPERATO: nessun errore ===\n");
    } else {
        printf("=== BANCO FALLITO: %d errori ===\n", errori);
    }
    return errori ? 1 : 0;
}
