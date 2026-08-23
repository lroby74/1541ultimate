--------------------------------------------------------------------------------
-- Banco di prova per i registri aggiunti a c2n_playback_io per il contanastro.
--
-- Gira al clock VERO della U2+ (62,5 MHz): a 10 MHz il controllo di velocita'
-- del nastro va in saturazione e non e' rappresentativo.
--
-- Controlla:
--   1. il riempimento della FIFO si legge bene anche sopra i 255 byte;
--   2. il registro di stato vecchio, a +0x000, e' rimasto quello di prima;
--   3. lo stato del trasporto a +0x001 segue motore e sense;
--   4. >>> il contatore dei cicli di nastro avanza ANCHE DENTRO UN IMPULSO
--      LUNGO, quando la FIFO e' ferma e i byte non si muovono <<<;
--   5. il contatore si ferma quando il C64 e' congelato dal menu;
--   6. si azzera scrivendo a +0x004.
--
-- Il punto 4 e' quello che conta: un giro del contanastro dentro un silenzio
-- da tredici secondi non si puo' distinguere guardando i byte, perche' quel
-- silenzio nel file TAP occupa quattro byte in tutto.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity c2n_counter_tb is
end entity;

architecture arch of c2n_counter_tb is
    constant c_clock_freq : natural := 62_500_000;   -- il clock vero della U2+

    signal clock       : std_logic := '0';
    signal reset       : std_logic := '1';
    signal req         : t_io_req := c_io_req_init;
    signal resp        : t_io_resp;
    signal motor_in    : std_logic := '0';
    signal sense_in    : std_logic := '0';
    signal c64_stopped : std_logic := '0';
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
        c64_stopped    => c64_stopped,
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
        variable d         : std_logic_vector(7 downto 0);
        variable lo, hi    : std_logic_vector(7 downto 0);
        variable conteggio : natural;
        variable cicli_a   : natural;
        variable cicli_b   : natural;

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
            wait until rising_edge(clock);   -- qui il modulo campiona la lettura
            req.read    <= '0';
            wait for 1 ns;                   -- resp.data e' appena stato scritto
            v := resp.data;
            wait until rising_edge(clock);
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

        -- Byte ancora da suonare.  PRIMA il byte basso, che congela gli alti.
        -- La sync_fifo lavora in "fall through": un byte e' gia' sull'uscita e
        -- non e' piu' contato in num_el, pero' il C64 non l'ha ancora sentito.
        procedure leggi_coda(n : out natural) is
            variable l, h, st : std_logic_vector(7 downto 0);
            variable v : natural;
        begin
            io_rd(16#002#, l);
            io_rd(16#003#, h);
            io_rd(16#000#, st);
            v := to_integer(unsigned(h(3 downto 0)) & unsigned(l));
            if st(7) = '0' then
                v := v + 1;
            end if;
            n := v;
        end procedure;

        -- Cicli di nastro passati.  PRIMA il byte piu' basso, che congela gli altri.
        procedure leggi_cicli(n : out natural) is
            variable b0, b1, b2, b3 : std_logic_vector(7 downto 0);
        begin
            io_rd(16#004#, b0);
            io_rd(16#005#, b1);
            io_rd(16#006#, b2);
            io_rd(16#007#, b3);
            n := to_integer(unsigned(b3) & unsigned(b2) & unsigned(b1) & unsigned(b0));
        end procedure;

    begin
        wait until reset = '0';
        wait for 1 us;

        report "--- 1. coda vuota all'inizio ---" severity note;
        leggi_coda(conteggio);
        verifica(conteggio = 0, "la coda parte vuota");
        io_rd(16#000#, d);
        verifica(d(7) = '1', "il vecchio registro di stato dice ancora fifo_empty");
        leggi_cicli(cicli_a);
        verifica(cicli_a = 0, "il contatore dei cicli di nastro parte da zero");

        report "--- 2. trecento byte nella coda: si legge sopra i 255? ---" severity note;
        for i in 0 to 299 loop
            io_wr(16#800#, std_logic_vector(to_unsigned(16#30# + (i mod 16), 8)));
        end loop;
        wait for 1 us;
        leggi_coda(conteggio);
        report "     coda: " & integer'image(conteggio) & " byte" severity note;
        verifica(conteggio = 300, "il conteggio vale 300, quindi i bit alti passano");
        io_rd(16#002#, lo);
        io_rd(16#003#, hi);
        verifica(to_integer(unsigned(hi(3 downto 0)) & unsigned(lo)) = 299,
                 "il registro grezzo ne dichiara 299: il byte in fall-through");
        io_rd(16#000#, d);
        verifica(d(7) = '0', "il vecchio registro di stato dice che la coda non e' vuota");

        report "--- 3. lo stato del trasporto segue motore e sense ---" severity note;
        motor_in <= '0'; sense_in <= '0';
        wait for 1 us;
        io_rd(16#001#, d);
        verifica(d(0) = '0', "motore fermo letto come 0");
        verifica(d(1) = '0', "sense a 0 letto come 0");
        motor_in <= '1'; sense_in <= '1';
        wait for 1 us;
        io_rd(16#001#, d);
        verifica(d(0) = '1', "motore acceso letto come 1");
        verifica(d(1) = '1', "sense a 1 letto come 1");

        report "--- 4. si svuota la coda e si prepara un IMPULSO LUNGO ---" severity note;
        io_wr(16#000#, X"04");               -- flush
        io_wr(16#000#, X"00");
        io_wr(16#004#, X"00");               -- azzera i cicli di nastro
        wait for 2 us;
        leggi_coda(conteggio);
        verifica(conteggio = 0, "coda vuota");
        leggi_cicli(cicli_a);
        verifica(cicli_a = 0, "cicli azzerati dalla scrittura a +0x004");

        -- un impulso lungo da 400.000 cicli: nel file sono QUATTRO byte
        io_wr(16#800#, X"00");
        io_wr(16#800#, X"80");               -- 0x061A80 = 400.000
        io_wr(16#800#, X"1A");
        io_wr(16#800#, X"06");
        io_wr(16#000#, X"49");               -- abilita, modo v1, uscita in lettura
        wait for 200 ms;                     -- il motore sale a regime e il nastro scorre

        leggi_coda(conteggio);
        leggi_cicli(cicli_a);
        report "     dopo 200 ms: coda " & integer'image(conteggio) &
               " byte, cicli di nastro " & integer'image(cicli_a) severity note;
        verifica(conteggio = 0, "la coda e' ferma a zero: i byte non si muovono piu'");
        verifica(cicli_a > 1000, ">>> ma i cicli di nastro avanzano lo stesso <<<");

        wait for 100 ms;
        leggi_cicli(cicli_b);
        report "     dopo altri 100 ms: cicli di nastro " & integer'image(cicli_b) severity note;
        verifica(cicli_b > cicli_a, "e continuano ad avanzare dentro l'impulso lungo");

        report "--- 5. col C64 congelato il nastro si deve fermare ---" severity note;
        c64_stopped <= '1';
        wait for 1 ms;
        leggi_cicli(cicli_a);
        wait for 20 ms;
        leggi_cicli(cicli_b);
        verifica(cicli_b = cicli_a, "col C64 fermo i cicli non avanzano");
        c64_stopped <= '0';

        report "--- 6. col motore spento il nastro rallenta e si ferma ---" severity note;
        motor_in <= '0';
        wait for 500 ms;                     -- la rampa di arresto dura 300 ms
        leggi_cicli(cicli_a);
        wait for 20 ms;
        leggi_cicli(cicli_b);
        verifica(cicli_b = cicli_a, "a motore spento e rampa esaurita i cicli si fermano");

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
