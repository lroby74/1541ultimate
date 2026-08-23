--------------------------------------------------------------------------------
-- Banco di prova per Magic Desk 2 sulla finestra ROM da 2 MB.
--
-- Quello che deve dimostrare:
--   1. i 128 banchi da 16K esistono davvero, e il banco 64 NON ricade sul
--      banco 0 (era questo il difetto: sei bit di banco invece di sette);
--   2. ogni banco cade nel punto giusto dei 2 MB a partire dalla base;
--   3. il bit 7 del registro spegne la cartuccia e una scrittura successiva
--      la riaccende (se si spegnesse anche la decodifica, non si tornerebbe
--      piu' indietro);
--   4. dentro il banco l'indirizzo segue A0..A13, cioe' banchi da 16K;
--   5. i mapper vecchi non si sono spostati: il bit 20 resta a zero e
--      l'indirizzo e' quello di prima.
--
-- Riferimento: cartridge.v del core C64 del MiSTer, riga 744:
--   bank_lo <= {data_in[6:0], 1'b0};  -- fino a 128 banchi da 16K
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;
use work.io_bus_pkg.all;

entity md2_2mb_tb is
end entity;

architecture arch of md2_2mb_tb is
    constant c_rom_base : std_logic_vector(27 downto 0) := X"2000000";

    -- i codici dei mapper, come in all_carts_v5
    constant c_magic_desk2 : std_logic_vector(4 downto 0) := "01111";
    constant c_ocean_16K   : std_logic_vector(4 downto 0) := "01001";

    signal clock       : std_logic := '0';
    signal reset       : std_logic := '1';
    signal slot_req    : t_slot_req := c_slot_req_init;
    signal slot_resp   : t_slot_resp;
    signal cart_logic  : std_logic_vector(4 downto 0) := c_magic_desk2;
    signal variant     : std_logic_vector(2 downto 0) := "000";
    signal mem_addr    : unsigned(25 downto 0);
    signal exrom_n     : std_logic;
    signal game_n      : std_logic;
    signal serve_rom   : std_logic;
    signal c64_reset   : std_logic := '0';
    signal clock_stop  : boolean := false;

    signal errori      : natural := 0;

    -- l'indirizzo che dovrebbe uscire per (banco da 16K, offset dentro il banco)
    function atteso_16k(banco : natural; offs : natural) return unsigned is
        variable v : unsigned(25 downto 0);
    begin
        v := unsigned(c_rom_base(25 downto 0));
        v := v + to_unsigned(banco * 16384 + offs, 26);
        return v;
    end function;

begin
    clock <= not clock after 8 ns when not clock_stop else '0';
    reset <= '1', '0' after 200 ns;

    i_dut: entity work.all_carts_v5
    generic map (
        g_eeprom    => false,
        g_rom_base  => c_rom_base )
    port map (
        clock          => clock,
        reset          => reset,
        io_req_eeprom  => c_io_req_init,
        io_resp_eeprom => open,
        RST_in         => '0',
        c64_reset      => c64_reset,
        kernal_enable  => '0',
        kernal_area    => '0',
        freeze_trig    => '0',
        freeze_act     => '0',
        freezer_ena    => open,
        unfreeze       => open,
        cart_active    => open,
        cart_kill      => '0',
        cart_logic     => cart_logic,
        cart_variant   => variant,
        cart_force     => '0',
        slot_req       => slot_req,
        slot_resp      => slot_resp,
        epyx_timeout   => '0',
        serve_enable   => open,
        serve_vic      => open,
        serve_128      => open,
        serve_rom      => serve_rom,
        serve_io1      => open,
        serve_io2      => open,
        allow_write    => open,
        mem_req        => '0',
        mem_addr       => mem_addr,
        phi2           => '0',
        irq_n          => open,
        nmi_n          => open,
        exrom_n        => exrom_n,
        game_n         => game_n,
        CART_LEDn      => open );

    p_test: process
        -- una scrittura del C64 in DE00
        procedure scrivi_de00(v : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            slot_req.io_address <= X"0000";      -- DE00: il bit 8 e' a zero
            slot_req.data       <= v;
            slot_req.io_write   <= '1';
            wait until rising_edge(clock);
            slot_req.io_write   <= '0';
            wait until rising_edge(clock);
            wait until rising_edge(clock);
        end procedure;

        -- il C64 legge un indirizzo nella finestra della cartuccia
        procedure leggi_a(a : std_logic_vector(15 downto 0)) is
        begin
            wait until rising_edge(clock);
            slot_req.bus_address <= unsigned(a);
            wait until rising_edge(clock);
            wait for 1 ns;
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

        procedure prova_banco(banco : natural) is
        begin
            scrivi_de00(std_logic_vector(to_unsigned(banco, 8)));
            leggi_a(X"8000");                  -- prima meta' del banco
            verifica(mem_addr = atteso_16k(banco, 0),
                     "banco " & integer'image(banco) & " a 8000");
            leggi_a(X"A000");                  -- seconda meta' dello stesso banco
            verifica(mem_addr = atteso_16k(banco, 16#2000#),
                     "banco " & integer'image(banco) & " a A000, cioe' +8K nello stesso banco");
        end procedure;

        variable a1, a2 : unsigned(25 downto 0);
    begin
        wait until reset = '0';
        wait for 1 us;

        report "--- 1. i primi banchi, quelli che ci stavano gia' ---" severity note;
        prova_banco(0);
        prova_banco(1);
        prova_banco(63);

        report "--- 2. IL PUNTO: i banchi oltre il 63 ---" severity note;
        scrivi_de00(X"00");
        leggi_a(X"8000");
        a1 := mem_addr;
        scrivi_de00(X"40");                    -- banco 64
        leggi_a(X"8000");
        a2 := mem_addr;
        report "     banco 0  -> " & integer'image(to_integer(a1)) severity note;
        report "     banco 64 -> " & integer'image(to_integer(a2)) severity note;
        verifica(a1 /= a2, ">>> il banco 64 NON ricade sul banco 0 <<<");
        verifica(a2 = atteso_16k(64, 0), "il banco 64 cade a base + 1 MB esatto");

        prova_banco(64);
        prova_banco(100);
        prova_banco(127);

        report "--- 3. tutti i 128 banchi, uno per uno ---" severity note;
        for b in 0 to 127 loop
            scrivi_de00(std_logic_vector(to_unsigned(b, 8)));
            leggi_a(X"8000");
            if mem_addr /= atteso_16k(b, 0) then
                report "  NO! : banco " & integer'image(b) & " fuori posto" severity error;
                errori <= errori + 1;
            end if;
        end loop;
        verifica(true, "i 128 banchi cadono tutti nel punto giusto dei 2 MB");

        report "--- 4. il bit 7 spegne, e si deve poter riaccendere ---" severity note;
        scrivi_de00(X"05");
        wait for 100 ns;
        verifica(game_n = '0' and exrom_n = '0', "acceso: modo 16K, game e exrom bassi");
        verifica(serve_rom = '1', "acceso: la ROM viene servita");

        scrivi_de00(X"85");                    -- bit 7 = 1
        wait for 100 ns;
        verifica(game_n = '1' and exrom_n = '1', "spento: game e exrom alti");
        verifica(serve_rom = '0', "spento: la ROM non viene piu' servita");

        scrivi_de00(X"07");                    -- bit 7 = 0
        wait for 100 ns;
        verifica(game_n = '0' and exrom_n = '0', ">>> si riaccende dopo essere stato spento <<<");
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(7, 0), "e riparte dal banco 7, quello appena scritto");

        report "--- 5. il reset del C64 riporta al banco 0 ---" severity note;
        scrivi_de00(X"7F");
        c64_reset <= '1';
        wait for 200 ns;
        c64_reset <= '0';
        wait for 200 ns;
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(0, 0), "dopo il reset si riparte dal banco 0");

        report "--- 6. i mapper vecchi non si sono spostati ---" severity note;
        cart_logic <= c_ocean_16K;
        variant    <= "111";                   -- 32 banchi
        c64_reset  <= '1';
        wait for 200 ns;
        c64_reset  <= '0';
        wait for 200 ns;
        scrivi_de00(X"05");                    -- Ocean: banco 5 da 16K
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(5, 0),
                 "Ocean 16K banco 5 sta dove stava, il bit 20 resta a zero");
        scrivi_de00(X"1F");                    -- banco 31, l'ultimo
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(31, 0), "Ocean 16K banco 31 idem");

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
