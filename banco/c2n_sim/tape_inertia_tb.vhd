--------------------------------------------------------------------------------
-- Quanto pesa l'inerzia del motore sul contanastro.
--
-- `tape_speed_control` non fa partire e fermare il nastro di colpo: c'e' una
-- rampa di avvio (120 ms) e una di arresto (300 ms), come su un registratore
-- vero.  Durante le rampe il nastro scorre PIU' PIANO, quindi ne passa di meno
-- di quanto direbbe un cronometro.
--
-- Un contanastro che conta MILLISECONDI col motore acceso (come fa
-- tape_counter.sv del MiSTer) sbaglia di quella differenza a ogni avvio e a
-- ogni arresto, e su un multiload l'errore si accumula fino a valere un giro
-- intero.  Un contanastro che conta il NASTRO PASSATO non puo' sbagliare.
--
-- Qui si misura, sul modulo vero e col clock vero della U2+ (62,5 MHz),
-- quanti impulsi di nastro passano davvero in un ciclo accendi/spegni,
-- contro quanti ne conterebbe un cronometro.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity tape_inertia_tb is
end entity;

architecture arch of tape_inertia_tb is
    constant c_clock_freq : natural := 62_500_000;   -- il clock vero della U2+
    constant c_rate_pal   : real    := 985250.0;     -- impulsi al secondo a regime

    signal clock      : std_logic := '0';
    signal reset      : std_logic := '1';
    signal motor      : std_logic := '0';
    signal tick       : std_logic;
    signal clock_stop : boolean := false;

    signal conta      : natural := 0;
    signal conta_on   : boolean := false;
begin
    clock <= not clock after 8 ns when not clock_stop else '0';   -- 62,5 MHz
    reset <= '1', '0' after 200 ns;

    i_dut: entity work.tape_speed_control
    generic map (
        g_simulation => false,
        g_clock_freq => c_clock_freq )
    port map (
        clock     => clock,
        reset     => reset,
        speed_sel => '0',        -- PAL
        motor_en  => motor,
        tick_out  => tick );

    p_conta: process(clock)
    begin
        if rising_edge(clock) then
            if conta_on and tick = '1' then
                conta <= conta + 1;
            end if;
        end if;
    end process;

    p_prova: process
        variable prima, dopo : natural;
        variable ideale      : real;
        variable perso_ms    : real;

        procedure attendi_ms(n : natural) is
        begin
            for k in 1 to n loop
                wait for 1 ms;
            end loop;
        end procedure;
    begin
        wait until reset = '0';
        conta_on <= true;

        -- si lascia salire a regime una prima volta, per misurare la velocita'
        motor <= '1';
        attendi_ms(500);
        prima := conta;
        attendi_ms(1000);          -- un secondo pieno a regime
        dopo := conta;
        report "velocita' a regime: " & integer'image(dopo - prima) &
               " impulsi al secondo (attesi 985250)" severity note;

        -- ora un ciclo spegni/accendi come lo fa un caricatore fra due blocchi
        report "--- un ciclo di spegnimento e riaccensione ---" severity note;
        prima := conta;
        motor <= '0';
        attendi_ms(1000);          -- un secondo di pausa fra due blocchi
        motor <= '1';
        attendi_ms(1000);          -- un secondo di caricamento
        dopo := conta;

        -- quanto avrebbe contato un cronometro: un secondo di motore acceso
        ideale   := c_rate_pal * 1.0;
        perso_ms := (ideale - real(dopo - prima)) / (c_rate_pal / 1000.0);

        report "impulsi passati davvero: " & integer'image(dopo - prima) severity note;
        report "impulsi che conterebbe un cronometro: " & integer'image(integer(ideale)) severity note;
        -- Il segno viene negativo: passa PIU' nastro di quanto conti un
        -- cronometro.  E' la rampa di arresto: a motore gia' spento il nastro
        -- continua a scorrere per un pezzo, e quel pezzo un cronometro non lo
        -- conta.  Rampa di arresto 300 ms (circa 150 ms di nastro) meno rampa
        -- di avvio 120 ms (circa 60 ms persi) = circa +90 ms a ciclo.
        report ">>> nastro passato IN PIU' rispetto a un cronometro: " &
               integer'image(integer(-perso_ms)) & " ms per ogni ciclo <<<" severity note;

        -- Turrican II ha 24 vuoti, cioe' 24 cicli di questi
        report ">>> su Turrican II (24 blocchi) un contatore a cronometro" severity note;
        report "    sbaglierebbe di " & integer'image(integer(-perso_ms * 24.0)) &
               " ms su un giro che ne dura 2693, cioe' lo " &
               integer'image(integer(-perso_ms * 2400.0 / 2693.0)) &
               " per cento di un giro" severity note;
        report "    e su un nastro con il doppio dei blocchi si sfonda il giro intero" severity note;

        clock_stop <= true;
        wait;
    end process;

end architecture;
