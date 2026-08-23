--------------------------------------------------------------------------------
-- Banco di prova: che cosa fa il TASTO RESET della cartuccia al mapper.
--
-- La domanda a cui deve rispondere e' precisa.  Sul ferro si vedeva questo:
--   ricaricando il CRT, il gioco Magic Desk 2 riparte da zero;
--   premendo il tasto reset della cartuccia, invece, riprende da dov'era.
-- Le spiegazioni possibili erano due, e sono opposte:
--   (a) il tasto NON azzera il registro di banco, quindi dopo il reset il C64
--       parte da un banco a meta' del gioco;
--   (b) il tasto azzera tutto nell'FPGA, e a ricordarsi la partita e' la RAM
--       del C64, che un reset non cancella.
-- Se vale (a) si ripara nel VHDL, se vale (b) si ripara nel firmware.  Questo
-- banco decide quale delle due, e lo fa guardando l'indirizzo che il mapper
-- genera subito dopo il reset.
--
-- Le due strade del reset sono distinte e vanno provate tutte e due:
--   RST_in     = il tasto fisico della cartuccia (slot_server_v4, reset_button)
--   c64_reset  = il reset che ordina il firmware (C64_MODE = C64_MODE_RESET)
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.slot_bus_pkg.all;
use work.io_bus_pkg.all;

entity md2_reset_tb is
end entity;

architecture arch of md2_reset_tb is
    constant c_rom_base : std_logic_vector(27 downto 0) := X"2000000";

    constant c_magic_desk2 : std_logic_vector(4 downto 0) := "01111";

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
    signal cart_active : std_logic;
    signal rst_in      : std_logic := '0';
    signal c64_reset   : std_logic := '0';
    signal clock_stop  : boolean := false;

    signal errori      : natural := 0;

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
        RST_in         => rst_in,
        c64_reset      => c64_reset,
        kernal_enable  => '0',
        kernal_area    => '0',
        freeze_trig    => '0',
        freeze_act     => '0',
        freezer_ena    => open,
        unfreeze       => open,
        cart_active    => cart_active,
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
        procedure scrivi_de00(v : std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            slot_req.io_address <= X"0000";
            slot_req.data       <= v;
            slot_req.io_write   <= '1';
            wait until rising_edge(clock);
            slot_req.io_write   <= '0';
            wait until rising_edge(clock);
            wait until rising_edge(clock);
        end procedure;

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

        -- il tasto della cartuccia: si tiene premuto un pezzo, come fa una mano
        procedure premi_tasto_reset is
        begin
            rst_in <= '1';
            wait for 2 us;
            rst_in <= '0';
            wait for 1 us;
        end procedure;

        procedure reset_dal_firmware is
        begin
            c64_reset <= '1';
            wait for 2 us;
            c64_reset <= '0';
            wait for 1 us;
        end procedure;

    begin
        wait until reset = '0';
        wait for 1 us;

        report "--- 1. si va a meta' gioco: banco 100, e cartuccia spenta ---" severity note;
        scrivi_de00(X"64");                       -- banco 100
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(100, 0), "prima del reset si legge il banco 100");

        scrivi_de00(X"E4");                       -- bit 7: la cartuccia si spegne
        wait for 100 ns;
        verifica(game_n = '1' and exrom_n = '1', "prima del reset la cartuccia e' spenta");

        report "--- 2. IL PUNTO: il tasto reset della cartuccia ---" severity note;
        premi_tasto_reset;
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(0, 0),
                 ">>> dopo il tasto reset si legge il BANCO 0 <<<");
        verifica(game_n = '0' and exrom_n = '0',
                 ">>> dopo il tasto reset la cartuccia e' riaccesa in modo 16K <<<");
        verifica(serve_rom = '1', "dopo il tasto reset la ROM viene servita");
        verifica(cart_active = '1', "dopo il tasto reset la cartuccia e' attiva");

        report "--- 3. la stessa cosa col reset ordinato dal firmware ---" severity note;
        scrivi_de00(X"64");
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(100, 0), "si torna al banco 100");
        reset_dal_firmware;
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(0, 0),
                 "anche il reset del firmware riporta al banco 0");

        report "--- 4. il tasto tenuto premuto a lungo non cambia le carte ---" severity note;
        scrivi_de00(X"7F");                       -- banco 127
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(127, 0), "si va al banco 127");
        rst_in <= '1';
        wait for 20 us;                           -- una mano lenta
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(0, 0), "col tasto ancora premuto si e' gia' al banco 0");
        rst_in <= '0';
        wait for 2 us;
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(0, 0), "e al rilascio ci resta");

        report "--- 5. una scrittura dopo il reset funziona ancora ---" severity note;
        scrivi_de00(X"03");
        leggi_a(X"8000");
        verifica(mem_addr = atteso_16k(3, 0), "dopo il reset il registro risponde ancora");

        wait for 1 us;
        report "===============================================================" severity note;
        if errori = 0 then
            report "=== BANCO SUPERATO: il mapper si azzera da solo al reset ===" severity note;
            report "    Quindi la partita NON se la ricorda l'FPGA: se la ricorda" severity note;
            report "    la RAM del C64.  Si ripara nel firmware, non nel VHDL." severity note;
        else
            report "=== BANCO FALLITO ===" severity error;
        end if;
        report "===============================================================" severity note;
        clock_stop <= true;
        wait;
    end process;
end architecture;
