/* Finta filemanager.h: serve solo al banco di prova su PC, per compilare
   tape_map.cc senza tirarsi dietro tutto il firmware dell'Ultimate. */
#ifndef BANCO_FILEMANAGER_H
#define BANCO_FILEMANAGER_H
#include <stdint.h>
#include <string.h>

typedef enum { FR_OK = 0, FR_NO_FILE, FR_DISK_ERR } FRESULT;

class File
{
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
public:
    int seekCount;
    int readCount;
    File(const uint8_t *d, uint32_t s) : data(d), size(s), pos(0), seekCount(0), readCount(0) { }
    bool isValid(void) { return data != 0; }
    uint32_t get_size(void) { return size; }
    uint32_t get_pos(void) { return pos; }
    FRESULT seek(uint32_t p) { seekCount++; if (p > size) return FR_DISK_ERR; pos = p; return FR_OK; }
    FRESULT read(void *buf, uint32_t len, uint32_t *got) {
        readCount++;
        uint32_t n = (pos + len > size) ? (size - pos) : len;
        memcpy(buf, data + pos, n);
        pos += n;
        *got = n;
        return FR_OK;
    }
};
#endif
