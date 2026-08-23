// Prova un CRT Magic Desk 16K vero, dal file fino all'indirizzo che l'FPGA
// andra' a leggere.  Rifa' gli stessi conti del firmware e del VHDL, e poi
// controlla che il byte che il C64 leggera' sia davvero quello del file.
//
// I tre pezzi che devono essere d'accordo:
//   1. il file CRT              - pacchetti CHIP, banco, indirizzo, dimensione
//   2. il firmware (c64_crt.cc) - dove mette ogni pezzo nella finestra ROM
//   3. l'FPGA (all_carts_v5)    - dove va a leggere quando il gioco cambia banco
// Se uno dei tre e' fuori posto, il gioco parte e poi si rompe nei banchi alti:
// e' esattamente quello che succedeva con sei bit di banco invece di sette.
//
//   g++ -O2 -o crt_md16 crt_md16.cc
//   ./crt_md16 <un CRT Magic Desk 16K>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// --- quello che dice il firmware (software/io/c64/c64.h) -------------------
#define CART_ROM_SIZE   (2*1024*1024)

// --- quello che dice il VHDL (ultimate_logic_32.vhd / all_carts_v5.vhd) ---
#define ROM_BASE        0x2000000u      // g_rom_base_cart sulla II+
#define BANK_BITS       7               // io_wdata(6 downto 0), 128 banchi
#define BANK_SIZE       16384u          // rom_mode "01"

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

static uint16_t be16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }
static uint32_t be32(const uint8_t *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("uso: crt_md16 <file.crt>\n");
        return 1;
    }

    FILE *fp = fopen(argv[1], "rb");
    if (!fp) {
        printf("non trovo %s\n", argv[1]);
        return 1;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *d = (uint8_t *)malloc(n);
    if (fread(d, 1, n, fp) != (size_t)n) { printf("lettura incompleta\n"); return 1; }
    fclose(fp);

    printf("file: %s\n", argv[1]);
    printf("      %ld byte\n\n", n);

    // --- 1. l'intestazione ------------------------------------------------
    printf("--- 1. l'intestazione ---\n");
    verifica(memcmp(d, "C64 CARTRIDGE   ", 16) == 0, "firma C64 CARTRIDGE");
    uint32_t hdrlen = be32(d + 0x10);
    uint16_t tipo   = be16(d + 0x16);
    printf("      tipo CRT = %d, EXROM=%d GAME=%d, nome \"%.32s\"\n",
           tipo, d[0x18], d[0x19], d + 0x20);
    verifica(tipo == 85, "tipo 85 = Magic Desk 16K, quello che il firmware riconosce");
    verifica(d[0x18] == 0 && d[0x19] == 0, "EXROM e GAME a zero: modo 16K all'accensione");

    // --- 2. la finestra ROM del firmware ----------------------------------
    printf("\n--- 2. dove il firmware mette i dati ---\n");
    uint8_t *rom = (uint8_t *)malloc(CART_ROM_SIZE);
    memset(rom, 0xFF, CART_ROM_SIZE);       // come clear_cart_mem()

    uint32_t off = hdrlen;
    int chips = 0, highest = 0;
    uint32_t bank_multiplier = 16384;       // il valore di partenza nel firmware
    bool troppo_grande = false;

    while (off + 16 <= (uint32_t)n) {
        if (memcmp(d + off, "CHIP", 4) != 0) {
            printf("      a 0x%X non c'e' CHIP\n", off);
            errori++;
            break;
        }
        uint32_t pktlen = be32(d + off + 4);
        uint16_t ctype  = be16(d + off + 8);
        uint16_t bank   = be16(d + off + 10);
        uint16_t load   = be16(d + off + 12);
        uint16_t size   = be16(d + off + 14);

        // la trappola del C128: se scattasse, i banchi diventerebbero da 32K
        if ((load == 0xC000) || (size == 0x8000)) {
            bank_multiplier = 32768;
        }

        uint32_t dest = (uint32_t)bank * bank_multiplier + (load & 0x2000);
        if (dest + size > CART_ROM_SIZE) {
            troppo_grande = true;           // il controllo che prima non scattava mai
        } else if ((ctype == 0) || (ctype == 2)) {
            memcpy(rom + dest, d + off + 16, size);
        }
        if (bank > highest) highest = bank;
        chips++;
        off += pktlen ? pktlen : (16 + size);
    }

    printf("      %d pacchetti CHIP, banco piu' alto %d\n", chips, highest);
    verifica(chips == 128, "128 pacchetti, uno per banco");
    verifica(highest == 127, "i banchi arrivano al 127");
    verifica(bank_multiplier == 16384, "banchi da 16K: la trappola del C128 non scatta");
    verifica(!troppo_grande, "tutto entra nella finestra ROM da 2 MB");

    // --- 3. l'indirizzo che genera l'FPGA ---------------------------------
    printf("\n--- 3. quello che l'FPGA andra' a leggere ---\n");

    // rom_addr <= g_rom_base; rom_addr(20 downto 13) <= bank_bits;
    // rom_mode "01" -> il bit 13 viene da A13, quindi il banco sta in 20..14
    int fuori = 0, primo_fuori = -1;
    for (int banco = 0; banco < 128; banco++) {
        uint8_t reg = (uint8_t)banco;                 // scrittura in $DE00
        uint32_t bank_bits = (uint32_t)(reg & 0x7F);  // io_wdata(6 downto 0)

        for (int meta = 0; meta < 2; meta++) {        // $8000 e $A000
            for (int prova = 0; prova < 4; prova++) {
                uint32_t dentro = (uint32_t)((prova * 2039) % 8192) + (meta ? 8192 : 0);

                // indirizzo generato dall'FPGA
                uint32_t fpga = ROM_BASE + (bank_bits * BANK_SIZE) + dentro;
                // indirizzo dentro la finestra, che e' quello che vede il firmware
                uint32_t nella_finestra = fpga - ROM_BASE;

                // byte che il gioco si aspetta: sta nel pacchetto CHIP di quel banco
                uint32_t nel_file = hdrlen + (uint32_t)banco * (16 + 16384) + 16 + dentro;

                if (nella_finestra >= CART_ROM_SIZE) {
                    fuori++;
                    if (primo_fuori < 0) primo_fuori = banco;
                    continue;
                }
                if (rom[nella_finestra] != d[nel_file]) {
                    if (primo_fuori < 0) primo_fuori = banco;
                    fuori++;
                }
            }
        }
    }
    char testo[128];
    sprintf(testo, ">>> tutti i 128 banchi leggono il byte giusto (%d controlli) <<<", 128*2*4);
    verifica(fuori == 0, testo);
    if (fuori) {
        printf("      primo banco sbagliato: %d\n", primo_fuori);
    }

    // --- 4. la controprova: con sei bit si rompeva -------------------------
    printf("\n--- 4. la controprova: com'era con sei bit di banco ---\n");
    int rotti = 0, primo_rotto = -1;
    for (int banco = 0; banco < 128; banco++) {
        uint32_t bank_bits = (uint32_t)(banco & 0x3F);   // sei bit, come prima
        uint32_t nella_finestra = bank_bits * BANK_SIZE;
        uint32_t nel_file = hdrlen + (uint32_t)banco * (16 + 16384) + 16;
        if (rom[nella_finestra] != d[nel_file]) {
            rotti++;
            if (primo_rotto < 0) primo_rotto = banco;
        }
    }
    printf("      con sei bit sbagliavano %d banchi su 128, a partire dal %d\n",
           rotti, primo_rotto);
    verifica(rotti > 0, "con sei bit i banchi alti ricadevano sui bassi: era quello il difetto");

    printf("\n");
    if (errori == 0) {
        printf("=== BANCO SUPERATO: nessun errore ===\n");
    } else {
        printf("=== BANCO FALLITO: %d errori ===\n", errori);
    }
    free(rom);
    free(d);
    return errori ? 1 : 0;
}
