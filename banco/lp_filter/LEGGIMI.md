# lp_filter_shared — scritto, collaudato, NON collegato

## Che cos'e'

`lp_filter_shared.vhd` e' un filtro passa-basso a variabile di stato che serve
piu' canali a turno con un solo datapath, al posto di N copie separate.

E' **collaudato bit per bit** contro sei istanze di `lp_filter.vhd`:

```
PROVA A  400 finestre, ingresso fermo durante la raffica      -> scarto 0
PROVA B  1000 finestre, ingresso che cambia ogni microsecondo
         anche DENTRO la raffica                              -> scarto 0
```

su tutti e sei i canali.

## Perche' non e' collegato

Nella cartella `fpga/io/audio/` ci sono **due** file che dichiarano la stessa
entita' `lp_filter`:

| file | che cos'e' | chi lo compila |
|---|---|---|
| `lp_filter.vhd` | microprogramma di 6 istruzioni, lanciato una volta ogni 221 cicli | **nessun target** |
| `lp_filter2.vhd` | macina a ogni ciclo di clock, senza microprogramma | tutti i target |

`lp_filter_shared` riproduce il primo, che sull'Ultimate non gira. Il secondo
non ha tempo morto da sfruttare: condividerlo vorrebbe dire farlo girare a 1/6
della frequenza e riscalare il coefficiente (`shift_right(x,10)` diventa
`shift_right(x + shift_right(x,1), 8)`), il che rende la risposta equivalente
in frequenza ma **non identica campione per campione**, e frutta circa 300 LE
invece dei 690 che sembravano.

## Come si fa girare il banco

```
cd banco/lp_filter
export PATH="/c/intelFPGA_lite/17.0/modelsim_ase/win32aloem:$PATH"
vlib work
vcom -quiet -2008 ../../fpga/io/sigma_delta_dac/vhdl_source/my_math_pkg.vhd \
                  ../../fpga/io/audio/audio_type_pkg.vhd \
                  ../../fpga/io/audio/lp_filter.vhd \
                  lp_filter_shared.vhd \
                  lp_filter_equiv_tb.vhd
vsim -c -quiet lp_filter_equiv_tb -do "run -all; quit -f"
```

## La trappola da ricordare

Il confronto fra le due versioni va fatto **a valori assestati**, non a ogni
ciclo: durante la raffica i canali in coda sono legittimamente ancora al
campione precedente. E l'ingresso va mosso solo fuori dalla raffica, oppure
congelato: il filtro e' IIR, quindi basta una sola lettura diversa e le due
versioni divergono per sempre.
