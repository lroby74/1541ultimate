// Banco di prova per l'apertura e la chiusura del menu (UserInterface::run_once)
// con dentro la scorciatoia C= + tasto del menu.
//
// Nasce da un difetto vero, trovato sul ferro il 23 agosto 2026: entrando nel
// menu del nastro con la scorciatoia, uscendo, e riaprendo il menu normale, la
// macchina si piantava con la sola cornice disegnata.
//
// La causa: `release_host()` mette `doBreak = true`, e l'uscita della
// scorciatoia rientrava senza rimetterlo a false.  Alla chiamata successiva il
// giro `while(!doBreak)` non partiva nemmeno: la cornice veniva disegnata,
// il browser mai, e soprattutto **nessuno chiamava release_ownership()**,
// quindi il C64 restava congelato per sempre.
//
// Qui si modella il ciclo di vita - congelato / scongelato, oggetti inizializzati
// e disinizializzati, doBreak - e si controllano gli invarianti che contano:
//   1. dopo ogni apertura il C64 non resta congelato;
//   2. ogni init ha il suo deinit;
//   3. la seconda apertura si comporta come la prima.
//
//   g++ -O2 -o run_once_tb run_once_tb.cc && ./run_once_tb

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

// --------------------------------------------------------------------------
// Il mondo: il C64 e gli oggetti del menu.
// --------------------------------------------------------------------------
struct Mondo {
    bool congelato;       // take_ownership() lo congela, release_ownership() no
    int  init_fatti;      // quante volte gli oggetti sono stati inizializzati
    int  deinit_fatti;
    int  giri_browser;    // quante volte e' girato il giro del browser
    int  menu_nastro;     // quante volte si e' aperto il menu del nastro
    bool cUguale;         // il tasto C= e' premuto adesso?

    Mondo() { memset(this, 0, sizeof(*this)); }
};

// --------------------------------------------------------------------------
// UserInterface, ridotto all'osso ma con la stessa struttura di run_once().
// --------------------------------------------------------------------------
struct Interfaccia {
    bool doBreak;
    bool available;
    Mondo *m;
    bool rimetteDoBreak;   // false = com'era il difetto, true = com'e' adesso

    Interfaccia(Mondo *mondo, bool riparato)
        : doBreak(false), available(false), m(mondo), rimetteDoBreak(riparato) {}

    void take_ownership(void) { m->congelato = true; }
    void release_ownership(void) { m->congelato = false; }
    void appear(void) { m->init_fatti++; }
    void release_host(void) { m->deinit_fatti++; doBreak = true; }
    void set_available(bool v) { available = v; }

    void run_once(void)
    {
        take_ownership();
        appear();
        set_available(true);

        if (m->cUguale) {
            m->menu_nastro++;
            set_available(false);
            release_host();
            release_ownership();
            if (rimetteDoBreak) {
                doBreak = false;
            }
            return;
        }

        while (!doBreak) {
            m->giri_browser++;
            // l'utente esce dal menu: e' il ramo MENU_EXIT di pollFocussed
            set_available(false);
            doBreak = true;
            release_host();
            release_ownership();
            break;
        }
        set_available(false);
        doBreak = false;
    }
};

// --------------------------------------------------------------------------
static void prova(bool riparato, const char *come)
{
    printf("--- %s ---\n", come);

    Mondo m;
    Interfaccia ui(&m, riparato);

    // 1. apertura normale
    m.cUguale = false;
    ui.run_once();
    verifica(!m.congelato, "menu normale: uscendo il C64 e' scongelato");
    verifica(m.giri_browser == 1, "menu normale: il browser e' girato");

    // 2. la scorciatoia
    m.cUguale = true;
    ui.run_once();
    verifica(!m.congelato, "scorciatoia: uscendo il C64 e' scongelato");
    verifica(m.menu_nastro == 1, "scorciatoia: si e' aperto il menu del nastro");
    verifica(m.giri_browser == 1, "scorciatoia: il browser non c'entra");

    // 3. >>> IL PUNTO: subito dopo, il menu normale <<<
    m.cUguale = false;
    int prima = m.giri_browser;
    ui.run_once();
    verifica(m.giri_browser == prima + 1,
             ">>> dopo la scorciatoia il menu normale gira ancora <<<");
    verifica(!m.congelato,
             ">>> e il C64 non resta congelato <<<");

    // 4. gli inviti e i congedi sono in pari
    verifica(m.init_fatti == m.deinit_fatti,
             "ogni init ha il suo deinit");

    // 5. e si puo' andare avanti cosi' quanto si vuole
    for (int i = 0; i < 5; i++) {
        m.cUguale = (i % 2) == 0;
        ui.run_once();
    }
    verifica(!m.congelato, "cinque aperture alternate: mai congelato alla fine");
    verifica(m.init_fatti == m.deinit_fatti, "e sempre in pari");

    printf("\n");
}

int main(void)
{
    prova(true, "com'e' adesso");

    // La controprova: col difetto dentro, il banco deve fallire.  Se non
    // fallisse, vorrebbe dire che non sta misurando niente.
    printf("--- la controprova: com'era col difetto ---\n");
    {
        Mondo m;
        Interfaccia ui(&m, false);
        m.cUguale = true;   ui.run_once();
        m.cUguale = false;  ui.run_once();
        printf("      giri del browser: %d (dovrebbero essere 1)\n", m.giri_browser);
        printf("      C64 congelato   : %s\n", m.congelato ? "SI" : "no");
        verifica(m.giri_browser == 0 && m.congelato,
                 "col difetto il browser non gira e il C64 resta congelato: e' il difetto vero");
    }

    printf("\n");
    if (errori == 0) {
        printf("=== BANCO SUPERATO: nessun errore ===\n");
    } else {
        printf("=== BANCO FALLITO: %d errori ===\n", errori);
    }
    return errori ? 1 : 0;
}
