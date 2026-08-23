#!/bin/bash
# ============================================================================
# Ricostruisce l'ambiente di compilazione dell'Ultimate-II+ dentro WSL.
#
# Da lanciare DENTRO WSL:   bash <repo>/banco/setup-ambiente.sh
#
# Perche' serve: i pezzi stanno in due mondi.
#   - Git Bash su Windows non ha ne' 'make' ne' un 'gcc' per l'host;
#   - WSL ha make e g++, ma non il compilatore Nios II;
#   - Quartus 17.0 su Windows ha il compilatore Nios II e ModelSim.
# La soluzione e' far girare 'make' dentro WSL e fargli chiamare i compilatori
# di Windows.
#
# >>> LA CHIAVE E' 'exec -a' CON UN argv[0] IN FORMA WINDOWS <<<
# Il driver di GCC ricava DA argv[0] dove stanno cc1, gli header e le librerie.
# Lanciato da WSL riceve un percorso /mnt/c/... che il processo Windows non sa
# interpretare, e non trova piu' niente. Con 'exec -a' gli si passa un argv[0]
# alla Windows e si ritrova tutto da solo.
#
# Passare -B e -isystem a mano NON basta: si arriva fino in fondo e poi si
# sbatte contro un errore assurdo su 'abs' dentro <cstdlib>, perche' si
# mescolano copie diverse degli header di newlib e di libstdc++.
# ============================================================================

set -e

NB="/mnt/c/intelFPGA_lite/17.0/nios2eds/bin/gnu/H-x86_64-mingw32/bin"
WB="C:/intelFPGA_lite/17.0/nios2eds/bin/gnu/H-x86_64-mingw32/bin"
EB="/mnt/c/intelFPGA_lite/17.0/nios2eds/bin"
WE="C:/intelFPGA_lite/17.0/nios2eds/bin"

if [ ! -d "$NB" ]; then
    echo "Non trovo il compilatore Nios II in $NB"
    echo "Serve Quartus Prime Lite 17.0 con nios2eds installato."
    exit 1
fi

mkdir -p ~/niosbin

# --- la catena di compilazione Nios II ---
for t in $(ls "$NB" | sed 's/\.exe$//' | grep '^nios2-elf-'); do
    {
        echo '#!/bin/bash'
        echo "exec -a \"$WB/$t.exe\" \"$NB/$t.exe\" \"\$@\""
    } > ~/niosbin/$t
    chmod +x ~/niosbin/$t
done

# --- gli attrezzi di contorno di nios2eds ---
for t in elf2hex elf2flash bin2flash; do
    if [ -f "$EB/$t.exe" ]; then
        {
            echo '#!/bin/bash'
            echo "exec -a \"$WE/$t.exe\" \"$EB/$t.exe\" \"\$@\""
        } > ~/niosbin/$t
        chmod +x ~/niosbin/$t
    fi
done

# --- i makefile invocano 'python', che su Ubuntu recente non esiste piu' ---
if command -v python3 >/dev/null && ! command -v python >/dev/null; then
    {
        echo '#!/bin/sh'
        echo 'exec python3 "$@"'
    } > ~/niosbin/python
    chmod +x ~/niosbin/python
fi

echo "creati $(ls ~/niosbin | wc -l) wrapper in ~/niosbin"
echo
echo "Adesso:"
echo "  export PATH=~/niosbin:\$PATH"
echo "  cd <repo>"
echo "  make -C tools"
echo "  make -C target/libs/nios2/lwip"
echo "  make -C software/nios_solo_bsp && make -C software/nios_appl_bsp"
echo "  make -C target/u2plus/nios/boot_run      # produce onchip_mem.hex"
echo "  make -C target/u2plus/nios/boot_recovery"
echo "  make -C target/u2plus/nios/ultimate"
echo "  make -C target/u2plus/nios/recovery"
echo "  make -C target/u2plus/nios/updater"
echo "  cp target/u2plus/nios/updater/result/update.app ./update.u2p"
echo
export PATH=~/niosbin:$PATH
nios2-elf-gcc --version | head -1
