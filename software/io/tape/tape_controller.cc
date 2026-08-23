/*************************************************************/
/* Tape Emulator Control                                     */
/*************************************************************/
#include "tape_controller.h"
#include "tape_map.h"
#include "menu.h"
#include "filemanager.h"
#include "userinterface.h"
#include "c64.h"
#include "init_function.h"
#include <string.h>

TapeController *tape_controller = NULL; // globally static
static void init_tape_controller(void *_a, void *_b)
{
    if(getFpgaCapabilities() & CAPAB_C2N_STREAMER)
	    tape_controller = new TapeController;
}
InitFunction tape_playback_init("Tape Playback Controller", init_tape_controller, NULL, NULL, 60);

#define MENU_C2N_PAUSE         0x3201
#define MENU_C2N_RESUME        0x3202
#define MENU_C2N_STATUS        0x3203
#define MENU_C2N_STOP          0x3204
#define MENU_C2N_UNMOUNT       0x3206

const char *tick_rates[] = { "0.98 MHz (PAL)", "1.02 MHz (NTSC)" };
const char *no_yes[] = { "No", "Yes" };

#define CFG_TAPE_RATE        1
#define CFG_TAPE_AUTOUNLOAD  2

// Il player, quando parte, infila davanti un silenzio di cortesia: i quattro
// byte 00 95 95 25.  Quel silenzio NON sta nel file TAP, e' roba nostra, e
// quindi non va contato come nastro passato: sarebbero 2.463.637 cicli, cioe'
// il 91% di un giro di contanastro.  Contarlo vorrebbe dire leggere un giro in
// piu' per tutto il resto del nastro - proprio lo scarto da cui siamo partiti.
// Su un TAP v0 lo zero vale 2048 cicli fissi e i tre byte valgono byte*8.
#define C2N_LEADIN_CYCLES_V1  0x259595
#define C2N_LEADIN_CYCLES_V0  (2048 + (0x95 * 8) + (0x95 * 8) + (0x25 * 8))

struct t_cfg_definition tape_config[] = {
    { CFG_TAPE_RATE,       CFG_TYPE_ENUM, "Tape Playback Rate", "%s", tick_rates, 0,  1, 0 },
    // Come la voce "Tape Auto Unload" dell'OSD del core C64 del MiSTer.
    // Qui parte da No: il nastro resta dentro, e con lui il contanastro.
    { CFG_TAPE_AUTOUNLOAD, CFG_TYPE_ENUM, "Unload Tape at End", "%s", no_yes,     0,  1, 0 },
    { CFG_TYPE_END,        CFG_TYPE_END,  "", "", NULL, 0, 0, 0 }
};

TapeController :: TapeController() : SubSystem(SUBSYSID_TAPE_PLAYER)
{
    fm = FileManager :: getFileManager();
    register_store(0x54415045, "Tape Settings", tape_config);
    cfg->set_sort_order(SORT_ORDER_CFG_TAPE);
    
    file = NULL;
	menuItem = 0;
	transport = TRANSPORT_STP;
	paused = 0;
	recording = 0;
	controlByte = 0;
	tapeMap = NULL;
	cycleBase = 0;
	pendingResidual = 0;
	absCount = 0;
	zeroAt = 0;
	blockBuffer = new uint8_t[512];
	stop();
	taskHandle = 0;
	if (getFpgaCapabilities() & CAPAB_C2N_STREAMER) {
		xTaskCreate( TapeController :: poll_static, "TapePlayer", configMINIMAL_STACK_SIZE, this, PRIO_REALTIME, &taskHandle );
}
	}

TapeController :: ~TapeController()
{
	if (taskHandle) {
		vTaskDelete(taskHandle);
	}
	if (tapeMap) {
		delete tapeMap;
	}
	delete blockBuffer;
}

void TapeController :: poll_static(void *a)
{
	TapeController *tc = (TapeController *)a;
	while(1) {
		// >>> Il lucchetto va RISPETTATO. <<<
		// lock() aspetta 5 secondi e poi rinuncia, ma il valore di ritorno
		// veniva buttato via: passati i 5 secondi questo compito entrava lo
		// stesso mentre il menu stava lavorando sul nastro, e i due finivano a
		// leggere lo stesso file con lo stesso buffer - e a restituire un
		// lucchetto mai preso.  Bastava tenere aperta la finestra del
		// contanastro per qualche secondo per bloccare tutto.
		// Se il lucchetto e' di qualcun altro si salta il giro: mentre si
		// avvolge il nastro non deve avanzare comunque.
		if (tc->lock("tape poll")) {
			tc->poll();
			tc->unlock();
		}
		if (PLAYBACK_STATUS & C2N_STAT_ENABLED) {
			vTaskDelay(5);
		} else {
			vTaskDelay(250);
		}
	}
}

void TapeController :: create_task_items(void)
{
    TaskCategory *cat = TasksCollection :: getCategory("Tape", SORT_ORDER_TAPE);

    // Una voce sola, che apre lo STESSO menu di F8.  Due interfacce per la
    // stessa cosa si contraddicono, e qui dentro ogni voce si esegue una volta
    // sola e poi il menu chiude: inutilizzabile per avvolgere il nastro.
    tapeMenuAction = new Action("Tape Menu (F8)", SUBSYSID_TAPE_PLAYER, MENU_C2N_TAPEMENU);
    cat->append(tapeMenuAction);
}

void TapeController :: update_task_items(bool writablePath)
{
	if (file && !file->isValid()) {
	    close();
	}
	// La voce c'e' sempre: e' il menu stesso a dire se un nastro c'e' o no.
}

void TapeController :: stop()
{
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0; // also clears sense pin and disables output
}

void TapeController :: close()
{
	if(file) {
		printf("Closing tape file..\n");
        fm->fclose(file);
	}
	file = NULL;
}
	
void TapeController :: start(int playout_pin) // pin = 1: read, pin = 2: write
{
	stop();

	printf("Start Tape.. Status = %b. [", PLAYBACK_STATUS);
	PLAYBACK_CONTROL = C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = 0;
	paused = 0;
	
    // insert 2.5 (was one) second pause to start with
    *PLAYBACK_DATA = 0x00;
    *PLAYBACK_DATA = 0x95; //0xA2;
    *PLAYBACK_DATA = 0x95; //0x08;
    *PLAYBACK_DATA = 0x25; //0x0F; 25

    // L'FPGA conta tutto quello che suona, silenzio di cortesia compreso.
    // Quel silenzio non e' nastro, quindi lo si toglie dal conto: cosi' far
    // partire il nastro e riprenderlo dopo un riavvolgimento danno lo stesso
    // numero, che e' quello che deve succedere.
    cycleBase -= (int64_t)((mode != 0) ? C2N_LEADIN_CYCLES_V1 : C2N_LEADIN_CYCLES_V0);

    // Un riavvolgimento a nastro fermo aveva lasciato in coda il resto
    // dell'impulso; la partenza ha appena svuotato la coda, quindi va rimesso.
    if (pendingResidual && (mode != 0)) {
        *PLAYBACK_DATA = 0x00;
        *PLAYBACK_DATA = (uint8_t)(pendingResidual & 0xFF);
        *PLAYBACK_DATA = (uint8_t)((pendingResidual >> 8) & 0xFF);
        *PLAYBACK_DATA = (uint8_t)((pendingResidual >> 16) & 0xFF);
    }
    pendingResidual = 0;

	// Il bit di modo dell'FPGA vale 0 (TAP v0: lo zero e' un impulso corto
	// fisso) oppure 1 (v1 e v2: lo zero apre un token da 24 bit).  Qui dentro
	// pero' "mode" e' il byte 12 dell'intestazione, che per un v2 vale 2:
	// spostato di tre finirebbe sul bit 0x10, che e' il SENSE.
	uint8_t modeBit = (mode != 0) ? C2N_MODE_SELECT : 0;
	controlByte = C2N_ENABLE | modeBit | uint8_t(playout_pin << 6);
	if (playout_pin == 1) { // normal playback: WE control the Sense PIN
		controlByte |= C2N_SENSE;
	}
	if (cfg->get_value(CFG_TAPE_RATE)) {
	    controlByte |= C2N_RATE;
	}

	// preload some blocks
	for(int i=0;i<16;i++) {
		if(PLAYBACK_STATUS & C2N_STAT_FIFO_AF)
			break;

		read_block();
	}

	PLAYBACK_CONTROL = controlByte;
    recording = (playout_pin == 2);
	printf("%b] Status = %b.\n", controlByte, PLAYBACK_STATUS);
	state = 0;
}
	
void TapeController :: read_block()
{
	if(!file) {
		state = 1;
		return;
	}
	if(!file->isValid()) {
		state = 1;
    	close();
        return;
    }

	uint32_t bytes_read;

	if(block > length)
		block = length;

	if(block <= 0) {
		state = 1;
		return;
	}	

	file->read(blockBuffer, block, &bytes_read);
	for(int i=0;i<bytes_read;i++)// not sure if memcpy copies the bytes in the right order.
		*PLAYBACK_DATA = blockBuffer[i];

	if(bytes_read != block) {
		printf("[%d of %d]", bytes_read, block);
	}

	printf(".");
	length -= block;
	block = 512;
}
	
void TapeController :: poll()
{
	if(!file)
		return;

	if(!file->isValid()) {
    	close();
        return;
    }

	uint8_t st = PLAYBACK_STATUS;
	if(st & C2N_STAT_ENABLED) { // we are enabled
		if(!(st & C2N_STAT_FIFO_AF)) {
			switch(state) {
			case 0:
				read_block();
				break;
			case 1:
				*PLAYBACK_DATA = 123;
				state = 2;
				break;
			case 2:
				if (PLAYBACK_STATUS & C2N_STAT_FIFO_EMPTY) {
					state = 3;
					if (recording) {
						// In registrazione il file va chiuso, o quello che si
						// e' inciso non arriva sul disco.
						close();
						stop();
						C64::getMachine()->setButtonPushed();
					} else {
						// Nastro finito: si ferma.  Se resta dentro o no lo
						// decide l'utente, come sul MiSTer.  Di suo resta, cosi'
						// il contanastro non sparisce e si puo' riavvolgere.
						transport = TRANSPORT_STP;
						stop();
						if (cfg->get_value(CFG_TAPE_AUTOUNLOAD)) {
							close();
						}
					}
				}
				break;
			default:
				break;
			}
		}
	}
}
	
SubsysResultCode_e TapeController :: executeCommand(SubsysCommand *cmd)
{
	switch(cmd->functionID) {
		case MENU_C2N_PAUSE:
			PLAYBACK_CONTROL = (controlByte & ~C2N_ENABLE);
			paused = 1;
			break;
		case MENU_C2N_RESUME:
			PLAYBACK_CONTROL = controlByte;
			paused = 0;
			break;
		case MENU_C2N_STATUS:
			printf("Tape: stato %b, trasporto %b, coda %d byte, cicli di nastro %lu, giro %d\n",
			       PLAYBACK_STATUS, PLAYBACK_STATUS2, getFifoCount(),
			       (unsigned long)getTapeCycles(), absCount);
			break;
		case MENU_C2N_STOP:
			stopTransport();   // il nastro resta dentro: smontare e' un'altra cosa
			break;
		case MENU_C2N_UNMOUNT:
			close();
			stop();
			break;
		case MENU_C2N_TAPEMENU:
			tapeMenu(cmd);
			break;
		default:
			break;
	}
	return SSRET_OK;
}

// Avanzamento della scansione del nastro.  Serve solo a far vedere che sta
// lavorando: su un TAP da 2,4 MB la scansione e' 38 letture da USB, poco, ma
// senza un segno a video sembrerebbe comunque un blocco.
struct t_scan_progress {
	UserInterface *ui;
	int shown;
};

static void tape_scan_progress(void *ctx, uint32_t fatto, uint32_t totale)
{
	t_scan_progress *sp = (t_scan_progress *)ctx;
	if (!sp || !sp->ui || !totale) {
		return;
	}
	int pct = (int)(((uint64_t)fatto * 100) / totale);
	if (pct > sp->shown) {
		sp->ui->update_progress(0, pct - sp->shown);
		sp->shown = pct;
	}
}

void TapeController :: set_file(File *f, uint32_t len, int m, int offset, UserInterface *ui)
{
	close();
	file = f;
	mode = m;

	if (offset < 20) {
	    offset = 20;
	}
	// length va calcolata DOPO aver portato offset a 20, altrimenti si finisce
	// per leggere oltre la fine del file quando offset arriva minore di 20.
	length = f->get_size() - offset;
	file->seek((uint32_t)offset);
	block = 512 - (offset & 0x1FF);

	// Contanastro: si scandaglia il TAP una volta sola, qui.
	if (!tapeMap) {
	    tapeMap = new TapeMap();
	}
	t_scan_progress sp;
	sp.ui = ui;
	sp.shown = 0;
	if (ui) {
	    ui->show_progress("Scanning tape..", 100);
	}
	tapeMap->build(f, ui ? tape_scan_progress : 0, &sp);
	if (ui) {
	    ui->hide_progress();
	}

	file->seek((uint32_t)offset);
	absCount = tapeMap->clickForOffset((uint32_t)offset);
	zeroAt = absCount;

	PLAYBACK_CYCLES_CLEAR = 0;
	cycleBase = (int64_t)TapeMap::clickStartCycles(absCount);
	pendingResidual = 0;
}

// ---------------------------------------------------------------------------
// Contanastro
// ---------------------------------------------------------------------------
bool TapeController :: hasCounter(void)
{
	return (file != NULL) && (tapeMap != NULL) && tapeMap->isValid();
}

int TapeController :: getFifoCount(void)
{
	// il byte basso va letto per primo: la lettura congela i bit alti
	uint8_t lo = PLAYBACK_FIFO_LO;
	uint8_t hi = PLAYBACK_FIFO_HI;
	int n = (((int)(hi & 0x0F)) << 8) | (int)lo;

	// La sync_fifo lavora in "fall through": un byte e' gia' stato pescato
	// fuori e sta sull'uscita, quindi NON e' piu' contato in num_el anche se
	// il C64 non l'ha ancora sentito.  Misurato sul banco ModelSim: scrivendo
	// 300 byte il registro ne dichiara 299.  Va rimesso, o la posizione sul
	// nastro sarebbe sbagliata di un byte.
	if ((PLAYBACK_STATUS & C2N_STAT_FIFO_EMPTY) == 0) {
	    n++;
	}
	return n;
}

uint32_t TapeController :: getTapeCycles(void)
{
	// il byte piu' basso va letto per primo: la lettura congela gli altri tre
	uint32_t b0 = (uint32_t)PLAYBACK_CYCLES0;
	uint32_t b1 = (uint32_t)PLAYBACK_CYCLES1;
	uint32_t b2 = (uint32_t)PLAYBACK_CYCLES2;
	uint32_t b3 = (uint32_t)PLAYBACK_CYCLES3;
	return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

uint64_t TapeController :: getTotalCycles(void)
{
	int64_t t = cycleBase + (int64_t)getTapeCycles();
	if (t < 0) {
	    t = 0;   // siamo ancora dentro il silenzio d'avvio
	}
	return (uint64_t)t;
}

void TapeController :: updateCounter(void)
{
	if (!hasCounter()) {
	    return;
	}
	// Il contatore segue il NASTRO PASSATO, non l'orologio.  E' l'FPGA a
	// contare, con la stessa condizione che fa avanzare il nastro: cosi' le
	// rampe del motore e i silenzi lunghi sono gia' dentro il conto.
	uint64_t total = getTotalCycles();
	while ((absCount < TAPEMAP_ABS_MAX) &&
	       (total >= TapeMap::clickStartCycles(absCount + 1))) {
	    absCount++;
	}
}

int TapeController :: getCounterValue(void)
{
	int v = (absCount - zeroAt) % 1000;
	if (v < 0) {
	    v += 1000;   // il contatore meccanico gira all'indietro da 000 a 999
	}
	return v;
}

void TapeController :: seekToClick(int click)
{
	if (!hasCounter()) {
	    return;
	}
	if (click < 0) {
	    click = 0;
	}
	if (click > tapeMap->getMax() - 1) {
	    click = tapeMap->getMax() - 1;
	}

	bool wasRunning = ((PLAYBACK_STATUS & C2N_STAT_ENABLED) != 0);

	// I byte gia' in coda appartengono a un altro punto del nastro: si buttano.
	// Si toglie solo il bit di abilitazione: il SENSE deve restare com'era,
	// altrimenti per il C64 e' come se si fosse alzato il tasto PLAY.
	uint8_t idle = (uint8_t)(controlByte & ~C2N_ENABLE);
	PLAYBACK_CONTROL = idle;
	PLAYBACK_CONTROL = idle | C2N_CLEAR_ERROR | C2N_FLUSH_FIFO;
	PLAYBACK_CONTROL = idle;

	uint32_t off = tapeMap->getOffset(click);
	file->seek(off);
	length = file->get_size() - off;
	block = 512 - (off & 0x1FF);
	if (block == 0) {
	    block = 512;
	}
	state = 0;
	absCount = click;

	// Il contatore riparte dal punto esatto in cui comincia questo giro.
	PLAYBACK_CYCLES_CLEAR = 0;
	cycleBase = (int64_t)TapeMap::clickStartCycles(click);

	// Il confine del giro cade in mezzo a un impulso: se ne suona il pezzo che
	// resta, poi si riprende dal token successivo.  Senza questo, avvolgere
	// dentro un silenzio lungo porterebbe al suo bordo e il player lo
	// suonerebbe tutto da capo, mandando avanti il contatore di parecchi giri.
	uint32_t rest = tapeMap->getResidual(click);
	if (wasRunning) {
	    // Il nastro sta girando: il resto si infila subito in coda.
	    if (rest && (mode != 0)) {
	        *PLAYBACK_DATA = 0x00;
	        *PLAYBACK_DATA = (uint8_t)(rest & 0xFF);
	        *PLAYBACK_DATA = (uint8_t)((rest >> 8) & 0xFF);
	        *PLAYBACK_DATA = (uint8_t)((rest >> 16) & 0xFF);
	    }
	    pendingResidual = 0;
	} else {
	    // Nastro fermo: metterlo in coda adesso non servirebbe, perche' la
	    // partenza svuota la coda.  Lo emettera' start().
	    pendingResidual = rest;
	}

	if (wasRunning) {
	    for (int i = 0; i < 16; i++) {
	        if (PLAYBACK_STATUS & C2N_STAT_FIFO_AF) {
	            break;
	        }
	        read_block();
	    }
	    PLAYBACK_CONTROL = controlByte;
	}
}

// ---------------------------------------------------------------------------
// I tasti del 1530
// ---------------------------------------------------------------------------

// I tre caratteri che dicono che cosa sta facendo il nastro, come sul VICE.
// Lo spazio in coda a "FF " serve a coprire la terza lettera di quello di prima.
const char *TapeController :: transportText(void)
{
	if (!file) {
	    return "STP";
	}
	switch (transport) {
	case TRANSPORT_FF:
	    return "FF ";
	case TRANSPORT_REW:
	    return "REW";
	case TRANSPORT_PLY:
	    // Il tasto PLAY puo' essersi rialzato da solo a fine nastro.
	    return (PLAYBACK_STATUS & C2N_STAT_ENABLED) ? "PLY" : "STP";
	default:
	    return "STP";
	}
}

// STOP: alza il tasto, e il nastro si ferma a prescindere dal motore.
// Il nastro resta DENTRO, e con lui il contanastro: smontarlo e' un'altra cosa.
void TapeController :: stopTransport(void)
{
	transport = TRANSPORT_STP;
	if (!file) {
	    stop();
	    return;
	}

	// I byte ancora in coda sono stati letti dal file ma non suonati: il
	// nastro non ci e' passato sopra.  Se non si torna indietro di altrettanto,
	// alla prossima partenza quel pezzo di nastro sparisce - fino a mezzo giro
	// di contanastro, e senza che il conto se ne accorga.
	int inFifo = getFifoCount();
	uint32_t pos = file->get_size() - length;
	if (inFifo > (int)pos) {
	    inFifo = (int)pos;
	}
	if (inFifo > 0) {
	    uint32_t back = pos - (uint32_t)inFifo;
	    if (file->seek(back) == FR_OK) {
	        length += (uint32_t)inFifo;
	        block = 512 - (back & 0x1FF);
	        if (block == 0) {
	            block = 512;
	        }
	        state = 0;
	    }
	}

	stop();
}

// PLAY: abbassa il tasto.  Da qui in poi e' il motore della porta Datassette a
// decidere se il nastro scorre o no, esattamente come sul 1530 vero.
void TapeController :: doPlay(UserInterface *ui)
{
	if (!file) {
		if (ui) ui->popup("No tape mounted.", BUTTON_OK);
		return;
	}
	if (paused) {
		PLAYBACK_CONTROL = controlByte;
		paused = 0;
		transport = TRANSPORT_PLY;
		return;
	}
	transport = TRANSPORT_PLY;
	if (PLAYBACK_STATUS & C2N_STAT_ENABLED) {
		return;                              // il tasto e' gia' abbassato
	}
	start(1);
}

// RESET COUNTER: azzera le cifre e basta.  Il nastro NON si muove, come sul
// 1530 vero: e' proprio questo che fa funzionare la sequenza del game over.
void TapeController :: doResetCounter(UserInterface *ui)
{
	if (!hasCounter()) {
		if (ui) ui->popup("No tape mounted.", BUTTON_OK);
		return;
	}
	updateCounter();
	zeroAt = absCount;
}

// GO TO: si scrive il numero e ci si va, per la strada piu' corta.
void TapeController :: doGoto(UserInterface *ui)
{
	if (!hasCounter()) {
		if (ui) ui->popup("No tape mounted.", BUTTON_OK);
		return;
	}
	char buf[8];
	buf[0] = 0;
	if (ui->string_box("Go to counter:", buf, 3) <= 0) {
		return;
	}
	if ((buf[0] < 48) || (buf[0] > 57)) {
		return;
	}
	int target = 0;
	for (int i = 0; (i < 3) && (buf[i] >= 48) && (buf[i] <= 57); i++) {
		target = target * 10 + (buf[i] - 48);
	}

	// La strada piu' corta, come col nastro vero: da 000 il numero 980 vuol
	// dire venti giri indietro, non novecentottanta avanti.
	updateCounter();
	int delta = (target - getCounterValue()) % 1000;
	if (delta < 0) {
		delta += 1000;
	}
	if (delta > 500) {
		delta -= 1000;
	}
	seekToClick(absCount + delta);
}

// Un giro avanti o indietro.  Niente finestra: si torna subito al menu, che
// rimane sulla stessa voce e mostra il numero nuovo.
void TapeController :: windOne(UserInterface *ui, int direction)
{
	if (!hasCounter()) {
		if (ui) ui->popup("No tape mounted.", BUTTON_OK);
		return;
	}
	transport = (direction < 0) ? TRANSPORT_REW : TRANSPORT_FF;
	updateCounter();
	seekToClick(absCount + direction);
}

// ---------------------------------------------------------------------------
// Il menu del nastro: una schermata sola, col contanastro sempre in vista.
// Battendo INVIO si ripete lo stesso comando, perche' la voce accesa resta
// quella: e' cosi' che si fanno venti giri indietro senza impazzire.
// ---------------------------------------------------------------------------
void TapeController :: tapeMenu(SubsysCommand *cmd)
{
	UserInterface *ui = cmd->user_interface;
	if (!ui) {
		return;
	}

	const char *voci[7];
	const int n_voci = 7;
	voci[0] = "PLAY TAPE";
	voci[1] = "STOP TAPE";
	voci[2] = "REWIND TAPE";
	voci[3] = "FF TAPE";
	voci[4] = "GO TO";
	voci[5] = "RESET COUNTER";
	voci[6] = "UNMOUNT TAPE";

	while (1) {
	    // La finestra di scelta si ferma a 25 caratteri: questo ne fa 23.
	    char titolo[40];
	    if (hasCounter()) {
	        updateCounter();
	        uint8_t st2 = PLAYBACK_STATUS2;
	        sprintf(titolo, "TAPE %03d %s  %s",
	                getCounterValue(),
	                transportText(),
	                (st2 & C2N_STAT2_MOTOR) ? "MOTOR ON " : "MOTOR OFF");
	    } else {
	        sprintf(titolo, "TAPE  ---  NO TAPE");
	    }

	    // La scorciatoia per tornare qui, scritta dove serve: dentro il menu
	    // stesso.  E' una riga di nota, non una voce: non si seleziona.
	    int sel = ui->choice(titolo, voci, n_voci, &menuItem,
	                         "PRESS C= + MENU BUTTON TO RETURN");
	    if (sel < 0) {
	        break;                           // annullato
	    }
	    switch (sel) {
	    case 0:
	        doPlay(ui);
	        break;
	    case 1:
	        stopTransport();
	        break;
	    case 2:
	        windOne(ui, -1);
	        break;
	    case 3:
	        windOne(ui, +1);
	        break;
	    case 4:
	        doGoto(ui);
	        break;
	    case 5:
	        doResetCounter(ui);
	        break;
	    case 6:
	        close();
	        stop();
	        transport = TRANSPORT_STP;
	        break;
	    default:
	        break;
	    }
	}
}
