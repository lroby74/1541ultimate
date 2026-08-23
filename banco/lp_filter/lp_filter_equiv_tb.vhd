--------------------------------------------------------------------------------
-- Banco di equivalenza: sei lp_filter separati contro un lp_filter_shared.
--
-- Il filtro e' IIR: lo stato si porta dietro tutto il passato.  Basta che le due
-- versioni leggano l'ingresso in due istanti diversi una volta sola, e da li' in
-- poi divergono per sempre.  Quindi il confronto va fatto cambiando l'ingresso
-- SOLO quando nessuna delle due versioni sta macinando.
--
-- Il periodo del filtro e' 221 cicli e la raffica occupa i primi 37 (sei canali
-- da sei istruzioni, piu' il ciclo di lancio).  Qui si tiene il passo esatto di
-- 221 cicli e si muove l'ingresso a meta' periodo, lontano dalla raffica.
--
--   PROVA A  ingresso fermo durante la raffica: le due versioni devono dare lo
--            STESSO identico campione, per 400 finestre di fila.
--   PROVA B  ingresso che si muove come un vero segnale audio (aggiornato ogni
--            microsecondo, come il SID): si misura di quanto si discostano, per
--            sapere quanto pesa lo sfalsamento di 30 cicli fra i canali.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library work;
use work.audio_type_pkg.all;

entity lp_filter_equiv_tb is
end entity;

architecture arch of lp_filter_equiv_tb is
    constant c_channels : natural := 6;
    constant c_divider  : natural := 221;

    signal clock      : std_logic := '0';
    signal reset      : std_logic := '1';
    signal clock_stop : boolean := false;

    signal sig_in     : t_audio_array(0 to c_channels-1) := (others => (others => '0'));
    signal lp_ref     : t_audio_array(0 to c_channels-1);
    signal lp_shr     : t_audio_array(0 to c_channels-1);

    signal confronta  : boolean := false;
    signal azzera     : boolean := false;
    signal errori     : natural := 0;
    signal scarto_max : natural := 0;
    signal confronti  : natural := 0;
    type t_nat_array is array(natural range <>) of natural;
    signal scarto_ch  : t_nat_array(0 to c_channels-1) := (others => 0);
begin
    clock <= not clock after 8 ns when not clock_stop else '0';   -- 62,5 MHz
    reset <= '1', '0' after 200 ns;

    -- i sei filtri originali
    g_ref: for i in 0 to c_channels-1 generate
        i_ref: entity work.lp_filter
        generic map ( g_divider => c_divider )
        port map (
            clock     => clock,
            reset     => reset,
            signal_in => sig_in(i),
            high_pass => open,
            band_pass => open,
            low_pass  => lp_ref(i),
            error_out => open,
            valid_out => open );
    end generate;

    -- quello condiviso
    i_shared: entity work.lp_filter_shared
    generic map (
        g_channels => c_channels,
        g_divider  => c_divider )
    port map (
        clock     => clock,
        reset     => reset,
        signal_in => sig_in,
        low_pass  => lp_shr );

    p_check: process(clock)
        variable d : integer;
    begin
        if rising_edge(clock) then
            if azzera then
                errori     <= 0;
                confronti  <= 0;
                scarto_max <= 0;
                scarto_ch  <= (others => 0);
            elsif confronta then
                confronti <= confronti + 1;
                for i in 0 to c_channels-1 loop
                    d := to_integer(lp_ref(i)) - to_integer(lp_shr(i));
                    if d < 0 then
                        d := -d;
                    end if;
                    if d /= 0 then
                        errori <= errori + 1;
                    end if;
                    if d > scarto_max then
                        scarto_max <= d;
                    end if;
                    if d > scarto_ch(i) then
                        scarto_ch(i) <= d;
                    end if;
                end loop;
            end if;
        end if;
    end process;

    p_stim: process
        variable r        : real;
        variable vals     : t_audio_array(0 to c_channels-1);
        variable errori_a : natural := 0;

        procedure attendi(n : natural) is
        begin
            for k in 1 to n loop
                wait until rising_edge(clock);
            end loop;
        end procedure;
    begin
        wait until reset = '0';
        -- ci si porta a meta' periodo, lontano dalla raffica dei primi 37 cicli
        attendi(120);

        ----------------------------------------------------------------
        report "--- PROVA A: ingresso fermo durante la raffica ---" severity note;
        for n in 0 to 399 loop
            for i in 0 to c_channels-1 loop
                -- una sinusoide diversa per canale, cosi' i sei stati divergono
                r := sin(real(n) * 0.031 + real(i) * 0.7);
                vals(i) := to_signed(integer(r * 60000.0), 18);
            end loop;
            sig_in <= vals;
            attendi(218);
            confronta <= true;      -- un solo ciclo di confronto per finestra
            attendi(1);
            confronta <= false;
            attendi(2);             -- in tutto 221 cicli: il passo non slitta mai
        end loop;

        if errori = 0 then
            report "  ok  : " & integer'image(confronti) &
                   " finestre confrontate, nessuno scarto: sono IDENTICHE" severity note;
        else
            report "  NO! : " & integer'image(errori) & " scarti su " &
                   integer'image(confronti) & " finestre, il peggiore vale " &
                   integer'image(scarto_max) severity error;
        end if;

        ----------------------------------------------------------------
        errori_a := errori;   -- si mette da parte l'esito della prova A
        report "--- PROVA B: ingresso che si muove DENTRO la raffica ---" severity note;
        azzera <= true;
        attendi(2);
        azzera <= false;
        attendi(219);           -- in tutto 221: il passo resta agganciato
        -- Qui l'ingresso cambia ogni 62 cicli (un microsecondo, come il SID),
        -- quindi si muove anche mentre il filtro sta macinando: e' il caso che
        -- il congelamento degli ingressi deve rendere innocuo.
        -- Il confronto pero' si fa a fine finestra, quando TUTTI i canali hanno
        -- aggiornato l'uscita: durante la raffica i canali in coda sono
        -- legittimamente ancora al campione precedente.
        for n in 0 to 999 loop
            for m in 0 to 2 loop
                for i in 0 to c_channels-1 loop
                    -- 1 kHz campionato ogni microsecondo
                    r := sin(2.0 * MATH_PI * 1000.0 * real(n*3+m) * 1.0e-6 + real(i) * 0.7);
                    sig_in(i) <= to_signed(integer(r * 60000.0), 18);
                end loop;
                attendi(62);
            end loop;
            attendi(32);        -- 3*62 + 32 = 218
            confronta <= true;
            attendi(1);
            confronta <= false;
            attendi(2);         -- in tutto 221
        end loop;

        report "  " & integer'image(confronti) & " finestre confrontate, scarto massimo " &
               integer'image(scarto_max) & " su 60000 di ampiezza" severity note;
        for i in 0 to c_channels-1 loop
            report "    canale " & integer'image(i) & ": scarto " &
                   integer'image(scarto_ch(i)) severity note;
        end loop;

        if errori_a /= 0 then
            report "=== BANCO FALLITO: la prova A doveva essere identica ===" severity failure;
        elsif scarto_max /= 0 then
            report "=== BANCO FALLITO: nella prova B c'e' uno scarto ===" severity failure;
        else
            report "=== BANCO SUPERATO ===" severity note;
        end if;

        clock_stop <= true;
        wait;
    end process;

end architecture;
