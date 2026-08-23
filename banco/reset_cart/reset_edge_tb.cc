// Banco di prova per la macchina a stati del tasto reset della cartuccia
// (C64::checkResetButton, in software/io/c64/c64.cc).
//
// La regola da rispettare e' una sola: UNA pressione, UNA ripartenza.
// I modi di sbagliarla sono tre, e sono tutti stati provati qui:
//   - agire sul livello invece che sul fronte: finche' il tasto e' premuto si
//     riparte in continuazione;
//   - non rileggere la linea dopo aver agito: il nostro stesso reset muove la
//     linea RESET, e al giro dopo si vede un fronte che nessuno ha premuto -
//     da li' non si esce piu';
//   - dimenticare le condizioni di contorno: C64 spento, menu aperto, nessuna
//     cartuccia, opzione disattivata.
//
//   g++ -O2 -o reset_edge_tb reset_edge_tb.cc && ./reset_edge_tb

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
// Il mondo attorno alla funzione: quello che sul ferro sono i registri.
// --------------------------------------------------------------------------
struct Mondo {
    bool linea;          // RESET basso sulla porta cartuccia (status.reset_in)
    bool phi2;           // il C64 e' acceso
    bool disponibile;    // available
    bool congelato;      // isFrozen
    bool fermo;          // is_stopped(), cioe' menu aperto o macchina in pausa
    bool cartuccia;      // c'e' una cartuccia attiva
    bool opzione;        // "Reset Button Clears RAM"

    // il nostro reset tiene la linea bassa mentre lavora: se qualcuno guardasse
    // in quel momento, vedrebbe un fronte che non e' di nessuno
    bool nostroResetTieneLaLinea;

    int  ripartenze;     // quante volte e' stata chiamata start_cartridge
};

// --------------------------------------------------------------------------
// La funzione sotto esame, copiata riga per riga da c64.cc.
// --------------------------------------------------------------------------
struct Macchina {
    bool resetLineSeen;
    Mondo *m;

    Macchina(Mondo *mondo) : resetLineSeen(false), m(mondo) {}

    void start_cartridge(void)
    {
        m->ripartenze++;
        // Il nostro reset muove la linea: la alza e la riabbassa da sola.
        if (m->nostroResetTieneLaLinea) {
            m->linea = true;
        }
    }

    void checkResetButton(void)
    {
        if (!m->disponibile) {
            return;
        }
        if (!m->phi2) {
            resetLineSeen = true;
            return;
        }

        bool adesso = m->linea;
        bool prima  = resetLineSeen;
        resetLineSeen = adesso;

        if (!adesso || prima) {
            return;
        }
        if (m->congelato || m->fermo) {
            return;
        }
        if (!m->cartuccia) {
            return;
        }
        if (!m->opzione) {
            return;
        }

        start_cartridge();
        resetLineSeen = m->linea;
    }
};

// --------------------------------------------------------------------------
static Mondo mondo_normale(void)
{
    Mondo m;
    memset(&m, 0, sizeof(m));
    m.phi2 = true;
    m.disponibile = true;
    m.cartuccia = true;
    m.opzione = true;
    return m;
}

// un giro di orologio del ciclo principale di ultimate.cc (vTaskDelay(3))
static void giri(Macchina &mac, int n)
{
    for (int i = 0; i < n; i++) {
        mac.checkResetButton();
    }
}

int main(void)
{
    printf("--- 1. una pressione normale ---\n");
    {
        Mondo m = mondo_normale();
        Macchina mac(&m);
        giri(mac, 5);                       // a riposo
        verifica(m.ripartenze == 0, "a riposo non succede niente");
        m.linea = true;                     // dito giu'
        giri(mac, 30);                      // tenuto premuto un decimo di secondo
        verifica(m.ripartenze == 1, ">>> tenuto premuto, si riparte UNA volta sola <<<");
        m.linea = false;                    // dito su
        giri(mac, 50);
        verifica(m.ripartenze == 1, "al rilascio non si riparte una seconda volta");
    }

    printf("\n--- 2. due pressioni sono due ripartenze ---\n");
    {
        Mondo m = mondo_normale();
        Macchina mac(&m);
        for (int p = 0; p < 3; p++) {
            m.linea = true;  giri(mac, 20);
            m.linea = false; giri(mac, 20);
        }
        verifica(m.ripartenze == 3, "tre pressioni, tre ripartenze");
    }

    printf("\n--- 3. IL PUNTO: il nostro stesso reset non deve richiamarsi ---\n");
    {
        Mondo m = mondo_normale();
        m.nostroResetTieneLaLinea = true;   // il tasto e' ancora premuto quando agiamo
        Macchina mac(&m);
        m.linea = true;
        giri(mac, 100);
        verifica(m.ripartenze == 1, ">>> nessun riavvio a catena col tasto premuto <<<");
        m.linea = false;
        giri(mac, 100);
        verifica(m.ripartenze == 1, ">>> e nemmeno dopo il rilascio <<<");
    }

    printf("\n--- 4. le condizioni di contorno ---\n");
    {
        Mondo m = mondo_normale();
        m.cartuccia = false;
        Macchina mac(&m);
        m.linea = true; giri(mac, 20); m.linea = false; giri(mac, 20);
        verifica(m.ripartenze == 0, "senza cartuccia il reset resta quello di sempre");
    }
    {
        Mondo m = mondo_normale();
        m.opzione = false;
        Macchina mac(&m);
        m.linea = true; giri(mac, 20); m.linea = false; giri(mac, 20);
        verifica(m.ripartenze == 0, "con l'opzione spenta non si tocca la RAM");
    }
    {
        Mondo m = mondo_normale();
        m.fermo = true;                     // menu della cartuccia aperto
        Macchina mac(&m);
        m.linea = true; giri(mac, 20);
        verifica(m.ripartenze == 0, "col menu aperto non si riparte");
        m.fermo = false;                    // si chiude il menu, linea ancora bassa
        giri(mac, 20);
        verifica(m.ripartenze == 0, "e chiudendo il menu non parte un reset in ritardo");
    }
    {
        Mondo m = mondo_normale();
        m.congelato = true;
        Macchina mac(&m);
        m.linea = true; giri(mac, 20);
        verifica(m.ripartenze == 0, "col freezer attivo non si riparte");
    }
    {
        Mondo m = mondo_normale();
        m.phi2 = false;                     // C64 spento
        Macchina mac(&m);
        m.linea = true; giri(mac, 20);
        verifica(m.ripartenze == 0, "col C64 spento non si riparte");
        m.phi2 = true;                      // si accende con la linea ancora bassa
        giri(mac, 1);
        verifica(m.ripartenze == 0, "accendendolo non si scambia l'accensione per un fronte");
    }
    {
        Mondo m = mondo_normale();
        m.disponibile = false;
        Macchina mac(&m);
        m.linea = true; giri(mac, 20);
        verifica(m.ripartenze == 0, "senza porta cartuccia non si riparte");
    }

    printf("\n--- 5. un contatto ballerino non fa dieci ripartenze ---\n");
    {
        // Il tasto rimbalza: la linea sfarfalla per qualche millisecondo.
        // Non e' un caso da manuale ma non deve fare danni: ogni fronte vero
        // vale una ripartenza, e con l'intervallo del ciclo principale
        // (3 tick) i rimbalzi veri stanno quasi tutti dentro un solo giro.
        Mondo m = mondo_normale();
        m.nostroResetTieneLaLinea = true;
        Macchina mac(&m);
        m.linea = true;  giri(mac, 1);
        verifica(m.ripartenze == 1, "il primo fronte fa ripartire");
        int prima = m.ripartenze;
        m.linea = true;  giri(mac, 30);     // resta premuto
        verifica(m.ripartenze == prima, "e finche' resta premuto non si aggiunge niente");
    }

    printf("\n");
    if (errori == 0) {
        printf("=== BANCO SUPERATO: nessun errore ===\n");
    } else {
        printf("=== BANCO FALLITO: %d errori ===\n", errori);
    }
    return errori ? 1 : 0;
}
