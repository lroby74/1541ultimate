/*
 * filetype_tap.cc
 *
 * Written by
 *    Gideon Zweijtzer <info@1541ultimate.net>
 *    Daniel Kahlin <daniel@kahlin.net>
 *
 *  This file is part of the 1541 Ultimate-II application.
 *  Copyright (C) 200?-2011 Gideon Zweijtzer <info@1541ultimate.net>
 *  Copyright (C) 2011 Daniel Kahlin <daniel@kahlin.net>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "filetype_tap.h"
#include "filemanager.h"
#include "menu.h"
#include "userinterface.h"
#include "c64.h"
#include "tape_controller.h"
#include "endianness.h"

// tester instance
FactoryRegistrator<BrowsableDirEntry *, FileType *> tester_tap(FileType :: getFileTypeFactory(), FileTypeTap :: test_type);

#define TAPFILE_WRITE  0x3111
#define TAPFILE_MOUNT  0x3114   // monta e basta, senza far partire il nastro

/*************************************************************/
/* Tap File Browser Handling                                 */
/*************************************************************/

FileTypeTap :: FileTypeTap(BrowsableDirEntry *node)
{
	this->node = node;
	printf("Creating Tap type from: %s\n", node->getName());
}

FileTypeTap :: ~FileTypeTap()
{
    // >>> Qui NON si smonta il nastro. <<<
    // Questo oggetto e' la voce del browser, non il registratore: viene
    // distrutto ogni volta che l'elenco della cartella si ricostruisce, cioe'
    // ogni volta che si esce dal menu e si rientra.  Smontando qui, bastava
    // uscire al BASIC o riaprire il menu dopo il FOUND per ritrovarsi senza
    // nastro.  Il nastro si toglie quando lo dice l'utente (UNMOUNT TAPE), o
    // quando se ne monta un altro: ci pensa set_file().
    // Se il file sparisce davvero (chiavetta sfilata, file cancellato) se ne
    // accorgono poll() e update_task_items(), che controllano isValid().
}

int FileTypeTap :: fetch_context_items(IndexedList<Action *> &list)
{
    int count = 0;
    uint32_t capabilities = getFpgaCapabilities();
    if(capabilities & CAPAB_C2N_STREAMER) {
        // Una voce sola per far partire un nastro, e porta ai comandi del 1530.
        // Prima ce n'erano tre e si pestavano i piedi: "Play Tape" premeva PLAY
        // e basta, "Run Tape" scriveva LOAD e RUN sul C64 al posto dell'utente.
        // Sul 1530 vero il LOAD lo si scrive da soli (SHIFT + RUN/STOP), ed e'
        // cosi' anche sul core C64 del MiSTer: si monta il nastro e si preme
        // PLAY.  Qui si fa uguale.
        list.append(new Action("Mount TAP", FileTypeTap :: execute_st, TAPFILE_MOUNT, 0 ));
        count++;
        // La registrazione resta: e' l'altra cosa che fa un registratore, e dal
        // menu del nastro non si puo' fare.
        list.append(new Action("Write to Tape", FileTypeTap :: execute_st, TAPFILE_WRITE, 0 ));
        count++;
    }
    return count;
}

FileType *FileTypeTap :: test_type(BrowsableDirEntry *obj)
{
	FileInfo *inf = obj->getInfo();
    if(strcmp(inf->extension, "TAP")==0)
        return new FileTypeTap(obj);
    return NULL;
}

SubsysResultCode_e FileTypeTap :: execute_st(SubsysCommand *cmd)
{
	FRESULT fres;
	uint8_t read_buf[20];
	const char *signature = "C64-TAPE-RAW";
	uint32_t *pul;
	uint32_t bytes_read;
	SubsysCommand *c64_command;

	tape_controller->stop();
	tape_controller->close();

	File *file = 0;
	fres = FileManager :: getFileManager() -> fopen(cmd->path.c_str(), cmd->filename.c_str(), FA_READ, &file);
	if(!file) {
		cmd->user_interface->popup("Can't open TAP file.", BUTTON_OK);
		return SSRET_CANNOT_OPEN_FILE;
	}
	fres = file->read(read_buf, 20, &bytes_read);
	if(fres != FR_OK) {
		cmd->user_interface->popup("Error reading TAP file header.", BUTTON_OK);
		return SSRET_FILE_READ_FAILED;
	}
	if((bytes_read != 20) || (memcmp(read_buf, signature, 12))) {
		cmd->user_interface->popup("TAP file: invalid signature.", BUTTON_OK);
		return SSRET_ERROR_IN_FILE_FORMAT;
	}
	pul = (uint32_t *)&read_buf[16];
	// Il nastro parte sempre dall'inizio: il punto in cui andare si sceglie col
	// contanastro (GO TO nel menu del 1530), non dall'elenco di un file .idx.
	tape_controller->set_file(file, le_to_cpu_32(*pul), int(read_buf[12]), 0, cmd->user_interface);
	file = NULL; // after set file, the tape controller is now owner of the File object :)

	switch(cmd->functionID) {
    case TAPFILE_WRITE:
        c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_C64, C64_DRIVE_LOAD, RUNCODE_TAPE_RECORD, "A", "");
        c64_command->execute();
		tape_controller->start(2);
		break;
    case TAPFILE_MOUNT:
        // Il nastro e' dentro (ce l'ha messo set_file qui sopra) e NON parte:
        // si va dritti ai comandi del 1530, perche' e' li' che si lavora.
        // Cosi' l'utente li trova senza che nessuno debba spiegarglielo.
        printf("Tape mounted, going to the tape menu.\n");
        if (cmd->user_interface) {
            c64_command = new SubsysCommand(cmd->user_interface, SUBSYSID_TAPE_PLAYER,
                                            MENU_C2N_TAPEMENU, 0, "", "");
            c64_command->execute();
        }
        break;
    default:
		break;
	}
	return SSRET_OK;
}
