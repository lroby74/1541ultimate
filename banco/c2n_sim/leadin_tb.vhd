--------------------------------------------------------------------------------
-- Quanto nastro conta il silenzio di cortesia che il player mette all'avvio.
--
-- Il firmware, quando fa partire il nastro, infila davanti i quattro byte
-- 00 95 95 25.  Quel silenzio non sta nel file TAP: e' roba dell'emulatore,
-- serve solo a dare al C64 un attimo di quiete prima dei dati.
--
-- Il punto: l'FPGA conta i cicli di nastro guardando quello che suona, e non
-- sa distinguere un silenzio vero da uno inventato.  Questo banco misura
-- quanti cicli costa, per decidere se togliergli il conto o no.
--
-- Se il numero e' vicino a 2.693.000 (la durata del primo giro di contanastro)
-- allora contarlo vuol dire leggere quasi un giro in piu' su tutto il nastro.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity leadin_tb is
end entity;

architecture arch of leadin_tb is
    constant c_clock_freq : natural := 62_500_000;   -- il clock vero della U2+

    -- il primo giro di contanastro dura 2693 ms nominali, cioe' 2.693.000 cicli
    constant c_primo_giro : natural := 2_693_000;

    signal clock       : std_logic := '0';
    signal reset       : std_logic := '1';
    signal req         : t_io_req := c_io_req_init;
    signal resp        : t_io_resp;
    signal motor_in    : std_logic := '0';
    signal sense_in    : std_logic := '0';
    signal clock_stop  : boolean := false;

    signal errori      : natural := 0;
begin
    clock <= not clock after 8 ns when not clock_stop else '0';
    reset <= '1', '0' after 200 ns;

    i_dut: entity work.c2n_playback_io
    generic map (
        g_clock_freq   => c_clock_freq )
    port map (
        clock          => clock,
        reset          => reset,
        req            => req,
        resp           => resp,
        c64_stopped    => '0',
        generated_tick => open,
        c2n_motor_in   => motor_in,
        c2n_motor_out  => open,
        c2n_sense_in   => sense_in,
        c2n_sense_out  => open,
        c2n_out_en_w   => open,
        c2n_out_en_r   => open,
        c2n_out_r      => open,
        c2n_out_w      => open );

    p_test: process
        variable cicli    : natural;
        variable percento : natural;

        procedure io_wr(a : natural; v : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            req.address <= to_unsigned(a, 24);
            req.data    <= v;
            req.write   <= '1';
            wait until rising_edge(clock);
            req.write   <= '0';
            wait until rising_edge(clock);
        end procedure;

        procedure io_rd(a : natural; v : out std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            req.address <= to_unsigned(a, 24);
            req.read    <= '1';
            wait until rising_edge(clock);
            req.read    <= '0';
            wait for 1 ns;
            v := resp.data;
            wait until rising_edge(clock);
        end procedure;

        procedure leggi_cicli(n : out natural) is
            variable b0, b1, b2, b3 : std_logic_vector(7 downto 0);
        begin
            io_rd(16#004#, b0);
            io_rd(16#005#, b1);
            io_rd(16#006#, b2);
            io_rd(16#007#, b3);
            n := to_integer(unsigned(b3) & unsigned(b2) & unsigned(b1) & unsigned(b0));
        end procedure;

        procedure verifica(cond : boolean; testo : string) is
        begin
            if cond then
                report "  ok  : " & testo severity note;
            else
                report "  NO! : " & testo severity error;
                errori <= errori + 1;
            end if;
        end procedure;
    begin
        wait until reset = '0';
        wait for 1 us;

        report "--- il silenzio d'avvio: 00 95 95 25, in modo v1 ---" severity note;
        io_wr(16#000#, X"04");               -- flush
        io_wr(16#000#, X"00");
        io_wr(16#004#, X"00");               -- azzera i cicli di nastro
        wait for 2 us;

        io_wr(16#800#, X"00");
        io_wr(16#800#, X"95");
        io_wr(16#800#, X"95");
        io_wr(16#800#, X"25");

        motor_in <= '1';
        io_wr(16#000#, X"49");               -- abilita, modo v1, uscita in lettura

        -- si aspetta che la coda si svuoti e il silenzio finisca del tutto
        wait for 3500 ms;

        leggi_cicli(cicli);
        percento := (cicli * 100) / c_primo_giro;

        report "     il silenzio d'avvio vale " & integer'image(cicli) &
               " cicli di nastro" severity note;
        report "     un giro di contanastro ne vale " & integer'image(c_primo_giro) severity note;
        report "     >>> cioe' il " & integer'image(percento) &
               "% di un giro, contato per sbaglio <<<" severity note;

        -- il valore atteso e' 0x259595 = 2.463.637, con uno scarto di pochi
        -- cicli per il ciclo di avvio del motore
        verifica(cicli > 2_400_000 and cicli < 2_520_000,
                 "il silenzio d'avvio pesa quanto dice il token: 0x259595");
        verifica(percento > 80,
                 ">>> vale piu' dell'80% di un giro: va tolto dal conto <<<");

        wait for 1 us;
        if errori = 0 then
            report "=== BANCO SUPERATO: nessun errore ===" severity note;
        else
            report "=== BANCO FALLITO ===" severity failure;
        end if;
        clock_stop <= true;
        wait;
    end process;

end architecture;
