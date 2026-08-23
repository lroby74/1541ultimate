// Banco di prova per la riga di nota in fondo alla finestra di scelta
// (UIChoiceBox, in software/userinterface/ui_elements.cc).
//
// Due cose da dimostrare, e la seconda vale piu' della prima:
//   1. col la nota la finestra ci sta nello schermo del C64 (40x25) e la nota
//      non viene tagliata;
//   2. >>> SENZA nota, la finestra e' IDENTICA a com'era prima <<< - la stessa
//      choice() la usano tutti gli altri menu, e spostarli di una riga sarebbe
//      un difetto introdotto per una comodita' del nastro.
//
//   g++ -O2 -o nota_finestra_tb nota_finestra_tb.cc && ./nota_finestra_tb

#include <stdio.h>
#include <string.h>

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

#define SCHERMO_X 40
#define SCHERMO_Y 25

struct Finestra {
    int x, y, larghezza, righe;
};

// --------------------------------------------------------------------------
// I conti di UIChoiceBox::init(), com'erano PRIMA della nota.
// --------------------------------------------------------------------------
static Finestra vecchia(const char *titolo, const char **voci, int quante)
{
    int rows = 2 + 2 + quante;
    int max_len = (int)strlen(titolo);
    for (int i = 0; i < quante; i++) {
        int len = (int)strlen(voci[i]);
        if (len > max_len) max_len = len;
    }
    if (max_len > 25) max_len = 25;

    Finestra f;
    f.righe = rows;
    f.larghezza = max_len + 2;
    f.y = (SCHERMO_Y - rows - 2) >> 1;
    f.x = (SCHERMO_X - max_len - 2) >> 1;
    return f;
}

// --------------------------------------------------------------------------
// E come sono adesso.
// --------------------------------------------------------------------------
static Finestra nuova(const char *titolo, const char **voci, int quante, const char *nota)
{
    int rows = 2 + 2 + quante;
    if (nota) rows += 2;

    int max_len = (int)strlen(titolo);
    for (int i = 0; i < quante; i++) {
        int len = (int)strlen(voci[i]);
        if (len > max_len) max_len = len;
    }
    if (max_len > 25) max_len = 25;
    if (nota) {
        int len = (int)strlen(nota);
        if (len > 36) len = 36;
        if (len > max_len) max_len = len;
    }

    Finestra f;
    f.righe = rows;
    f.larghezza = max_len + 2;
    f.y = (SCHERMO_Y - rows - 2) >> 1;
    f.x = (SCHERMO_X - max_len - 2) >> 1;
    return f;
}

static bool uguali(Finestra a, Finestra b)
{
    return a.x == b.x && a.y == b.y && a.larghezza == b.larghezza && a.righe == b.righe;
}

static void stampa(const char *come, Finestra f)
{
    printf("      %-10s x=%d y=%d larga=%d alta=%d (arriva a x=%d, y=%d)\n",
           come, f.x, f.y, f.larghezza, f.righe, f.x + f.larghezza, f.y + f.righe);
}

// --------------------------------------------------------------------------
int main(void)
{
    // il menu del nastro, com'e' davvero
    const char *voci_nastro[] = {
        "PLAY TAPE", "STOP TAPE", "REWIND TAPE", "FF TAPE",
        "GO TO", "RESET COUNTER", "UNMOUNT TAPE" };
    const char *titolo_nastro = "TAPE 067 PLY  MOTOR OFF";
    const char *nota = "PRESS C= + MENU BUTTON TO RETURN";

    printf("--- 1. il menu del nastro con la nota ---\n");
    {
        Finestra f = nuova(titolo_nastro, voci_nastro, 7, nota);
        stampa("con nota", f);
        verifica(f.x >= 0, "non sfora a sinistra");
        verifica(f.x + f.larghezza <= SCHERMO_X, "non sfora a destra");
        verifica(f.y >= 0, "non sfora in alto");
        verifica(f.y + f.righe <= SCHERMO_Y, "non sfora in basso");
        verifica(f.larghezza - 2 >= (int)strlen(nota),
                 ">>> la nota ci sta tutta, non viene tagliata <<<");
        verifica(f.righe == 2 + 2 + 7 + 2, "due righe in piu': una vuota e la nota");
    }

    printf("\n--- 2. IL PUNTO: senza nota non deve cambiare NIENTE ---\n");
    {
        // il menu del nastro
        verifica(uguali(vecchia(titolo_nastro, voci_nastro, 7),
                        nuova(titolo_nastro, voci_nastro, 7, 0)),
                 "menu del nastro senza nota: identico a prima");

        // un menu corto, tipo una domanda si/no
        const char *due[] = { "Yes", "No" };
        verifica(uguali(vecchia("Are you sure?", due, 2),
                        nuova("Are you sure?", due, 2, 0)),
                 "una domanda a due voci: identica a prima");

        // un menu lungo e largo, tipo il menu azioni di un file
        const char *tante[] = {
            "Mount TAP", "Write to Tape", "View", "Hex View",
            "Rename", "Delete", "Run with app", "Copy", "Paste",
            "Something rather long here" };
        verifica(uguali(vecchia("Select an action from this list", tante, 10),
                        nuova("Select an action from this list", tante, 10, 0)),
                 "un menu da dieci voci col titolo lungo: identico a prima");

        // il caso limite del tetto a 25 caratteri
        const char *lunghe[] = { "0123456789012345678901234567890123456789" };
        verifica(uguali(vecchia("0123456789012345678901234567890123456789", lunghe, 1),
                        nuova("0123456789012345678901234567890123456789", lunghe, 1, 0)),
                 "voci piu' lunghe del tetto: identiche a prima");
    }

    printf("\n--- 3. una nota esagerata non manda la finestra fuori schermo ---\n");
    {
        const char *lunghissima =
            "QUESTA NOTA E' MOLTO PIU' LUNGA DELLO SCHERMO DEL C64 E NON CI STA";
        Finestra f = nuova(titolo_nastro, voci_nastro, 7, lunghissima);
        stampa("esagerata", f);
        verifica(f.x >= 0 && f.x + f.larghezza <= SCHERMO_X,
                 "il tetto a 36 la tiene dentro le 40 colonne");
        verifica(f.y >= 0 && f.y + f.righe <= SCHERMO_Y, "e dentro le 25 righe");
    }

    printf("\n--- 4. quanto spazio resta ---\n");
    {
        Finestra f = nuova(titolo_nastro, voci_nastro, 7, nota);
        printf("      colonne libere ai lati: %d\n", SCHERMO_X - f.larghezza);
        printf("      righe libere sopra e sotto: %d\n", SCHERMO_Y - f.righe);
        verifica(SCHERMO_X - f.larghezza >= 2, "resta almeno una colonna per parte");
        verifica(SCHERMO_Y - f.righe >= 2, "e almeno una riga sopra e sotto");
    }

    printf("\n");
    if (errori == 0) {
        printf("=== BANCO SUPERATO: nessun errore ===\n");
    } else {
        printf("=== BANCO FALLITO: %d errori ===\n", errori);
    }
    return errori ? 1 : 0;
}
