/*************************************************************/
/* Tape Emulator / Recorder Control                          */
/*************************************************************/
#include <stdio.h>
#include "menu.h"
#include "iomap.h"
#include "filemanager.h"
#include "subsys.h"
#include "config.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#define PLAYBACK_STATUS  *((volatile uint8_t *)(C2N_PLAY_BASE + 0x000))
#define PLAYBACK_CONTROL *((volatile uint8_t *)(C2N_PLAY_BASE + 0x000))
#define PLAYBACK_DATA     ((volatile uint8_t *)(C2N_PLAY_BASE + 0x800))

#define C2N_ENABLE      0x01
#define C2N_CLEAR_ERROR 0x02
#define C2N_FLUSH_FIFO  0x04
#define C2N_MODE_SELECT 0x08
#define C2N_SENSE		0x10
#define C2N_RATE        0x20

#define C2N_OUT_READ    0x00
#define C2N_OUT_WRITE   0x40
#define C2N_OUT_WRITE_N 0x80
#define C2N_OUT_TOGGLE  0xC0

// Registri aggiunti per il contanastro (vedi c2n_playback_io.vhd)
#define PLAYBACK_STATUS2 *((volatile uint8_t *)(C2N_PLAY_BASE + 0x001))
#define PLAYBACK_FIFO_LO *((volatile uint8_t *)(C2N_PLAY_BASE + 0x002))
#define PLAYBACK_FIFO_HI *((volatile uint8_t *)(C2N_PLAY_BASE + 0x003))

// Cicli di nastro davvero passati: quattro byte, il piu' basso per primo
// (la lettura del piu' basso congela gli altri tre).  Una scrittura a +0x004
// li azzera.  Contano il NASTRO, non il tempo: tengono conto delle rampe di
// avvio e arresto del motore e avanzano anche dentro un impulso lungo.
#define PLAYBACK_CYCLES0 *((volatile uint8_t *)(C2N_PLAY_BASE + 0x004))
#define PLAYBACK_CYCLES1 *((volatile uint8_t *)(C2N_PLAY_BASE + 0x005))
#define PLAYBACK_CYCLES2 *((volatile uint8_t *)(C2N_PLAY_BASE + 0x006))
#define PLAYBACK_CYCLES3 *((volatile uint8_t *)(C2N_PLAY_BASE + 0x007))
#define PLAYBACK_CYCLES_CLEAR PLAYBACK_CYCLES0

#define C2N_STAT2_MOTOR     0x01
#define C2N_STAT2_SENSE     0x02
#define C2N_STAT2_STREAM_EN 0x04
#define C2N_STAT2_ERROR     0x08

#define C2N_STAT_ENABLED    0x01
#define C2N_STAT_ERROR      0x02
#define C2N_STAT_FIFO_FULL  0x04
#define C2N_STAT_FIFO_AF    0x08
#define C2N_STAT_STREAM_EN  0x40
#define C2N_STAT_FIFO_EMPTY 0x80

class TapeMap;

class TapeController : public SubSystem, ObjectWithMenu, ConfigurableObject
{
	FileManager *fm;
	File *file;
	uint32_t length;
	int   state;
	int   block;
    int   mode;
	int   paused;
	bool  recording;
	uint8_t  controlByte;
	uint8_t  *blockBuffer;
	TaskHandle_t taskHandle;

	// --- contanastro -------------------------------------------------------
	// La mappa "giro -> byte del TAP" e' costruita all'apertura del file con le
	// formule di VICE (vedi tape_map.cc).  La posizione esatta si ricava dai
	// byte spinti nella FIFO meno quelli che vi sono ancora dentro.
	TapeMap *tapeMap;
	int64_t  cycleBase;      // cicli di nastro gia' passati prima dell'ultimo salto
	                         // (puo' essere negativo: vedi il silenzio d'avvio)
	uint32_t pendingResidual;// resto di impulso da emettere alla prossima partenza
	int   absCount;          // giro assoluto sul nastro
	int   zeroAt;            // giro in cui il contatore e' stato azzerato

	// Che cosa sta facendo il nastro, alla maniera del VICE: tre caratteri.
	enum { TRANSPORT_STP = 0, TRANSPORT_PLY, TRANSPORT_FF, TRANSPORT_REW };
	int     transport;

	Action *tapeMenuAction;
	int     menuItem;        // la voce accesa nel menu, si ricorda fra un'apertura e l'altra

	void read_block();
	static void poll_static(void *a);

	int  getFifoCount(void);
	uint32_t getTapeCycles(void);
	uint64_t getTotalCycles(void);
	void updateCounter(void);
	void seekToClick(int click);

	// --- i tasti del 1530 -------------------------------------------------
	void stopTransport(void);
	void doPlay(UserInterface *ui);
	void doResetCounter(UserInterface *ui);
	void doGoto(UserInterface *ui);
	void windOne(UserInterface *ui, int direction);
	const char *transportText(void);
	void tapeMenu(SubsysCommand *cmd);
public:
	TapeController();
	virtual ~TapeController();
	
    void create_task_items(void);
    void update_task_items(bool writablePath);
    const char *identify(void) { return "Tape Player"; }
	SubsysResultCode_e executeCommand(SubsysCommand *cmd);
	
	void close();
	void stop();
	void start(int);
	void poll();
	void set_file(File *f, uint32_t, int, int, UserInterface *ui = 0);

	// Valore mostrato dal contanastro, 000..999 come sul 1530 vero.
	int  getCounterValue(void);
	bool hasCounter(void);
};

extern TapeController *tape_controller;
