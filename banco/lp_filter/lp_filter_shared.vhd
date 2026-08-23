-------------------------------------------------------------------------------
--
-- Filtro passa-basso a variabile di stato, condiviso fra piu' canali.
--
-- Stessa matematica di lp_filter.vhd (Gideon Zweijtzer), stesso microprogramma
-- di sei istruzioni, stessi limitatori: quello che cambia e' che qui il
-- datapath e' UNO SOLO e serve g_channels canali a turno.
--
-- Perche' si puo' fare: il microprogramma dura sei cicli e viene lanciato una
-- volta ogni g_divider (221) cicli di clock.  Un filtro singolo quindi sta
-- fermo per il 97% del tempo.  Sei canali in fila occupano 36 cicli su 221:
-- ci stanno comodi, e si risparmiano cinque copie di sommatori e multiplexer.
--
-- Il canale 0 esegue le sue istruzioni negli stessi identici cicli del filtro
-- originale; i canali successivi sono sfalsati di sei cicli l'uno dall'altro,
-- cioe' al massimo 30 cicli (0,48 us a 62,5 MHz) rispetto all'originale.
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.my_math_pkg.all;
use work.audio_type_pkg.all;

entity lp_filter_shared is
generic (
    g_channels  : natural := 6;
    g_divider   : natural := 221 );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    signal_in   : in  t_audio_array(0 to g_channels-1);
    low_pass    : out t_audio_array(0 to g_channels-1) );
end entity;

architecture dsvf of lp_filter_shared is
    type t_byte_array is array(natural range <>) of std_logic_vector(7 downto 0);
    constant c_program  : t_byte_array := (X"80", X"12", X"81", X"4C", X"82", X"20");
    constant c_steps    : natural := c_program'length;

    signal filter_q     : signed(17 downto 0);
    signal filter_f     : signed(17 downto 0);

    -- lo stato del filtro, uno per canale
    signal bp_arr       : t_audio_array(0 to g_channels-1) := (others => (others => '0'));
    signal lp_arr       : t_audio_array(0 to g_channels-1) := (others => (others => '0'));
    signal hp_arr       : t_audio_array(0 to g_channels-1) := (others => (others => '0'));

    -- i registri di lavoro, condivisi: nascono e muoiono dentro i sei cicli
    -- di un singolo canale, quindi non serve tenerne una copia per canale
    signal x_reg        : signed(17 downto 0) := (others => '0');
    signal temp_reg     : signed(17 downto 0) := (others => '0');

    -- Il microprogramma legge l'ingresso nel ciclo della terza istruzione (0x81).
    -- Nell'originale i sei filtri lo fanno tutti nello stesso istante; qui i
    -- canali sono in fila, quindi si congela l'ingresso di TUTTI nel momento in
    -- cui lo legge il canale 0, che e' lo stesso istante dell'originale.
    signal in_lat       : t_audio_array(1 to g_channels-1) := (others => (others => '0'));
    signal lat_now      : std_logic := '0';

    signal bp_cur       : signed(17 downto 0);
    signal lp_cur       : signed(17 downto 0);
    signal hp_cur       : signed(17 downto 0);
    signal input_cur    : signed(17 downto 0);

    signal xa           : signed(17 downto 0);
    signal xb           : signed(17 downto 0);
    signal sum_b        : signed(17 downto 0);
    signal sub_a        : signed(17 downto 0);
    signal sub_b        : signed(17 downto 0);

    signal instruction  : std_logic_vector(7 downto 0) := (others => '0');
    signal ch_sel       : integer range 0 to g_channels-1 := 0;
    signal ch_cnt       : integer range 0 to g_channels-1 := 0;
    signal step         : integer range 0 to c_steps-1 := 0;
    signal busy         : std_logic := '1';
    signal divider      : integer range 0 to g_divider-1 := 0;

    alias  xa_select    : std_logic is instruction(0);
    alias  xb_select    : std_logic is instruction(1);
    alias  sub_a_sel    : std_logic is instruction(2);
    alias  sub_b_sel    : std_logic is instruction(3);
    alias  sum_to_lp    : std_logic is instruction(4);
    alias  sum_to_bp    : std_logic is instruction(5);
    alias  sub_to_hp    : std_logic is instruction(6);
    alias  mult_enable  : std_logic is instruction(7);

begin
    assert g_divider > g_channels * c_steps + 2
        report "lp_filter_shared: non c'e' tempo per tutti i canali dentro un periodo"
        severity failure;

    filter_q <= to_signed(65536, filter_q'length);
    filter_f <= to_signed(16384, filter_f'length);

    bp_cur    <= bp_arr(ch_sel);
    lp_cur    <= lp_arr(ch_sel);
    hp_cur    <= hp_arr(ch_sel);
    input_cur <= signal_in(0) when ch_sel = 0 else in_lat(ch_sel);

    xa    <= filter_f  when xa_select='0' else filter_q;
    xb    <= bp_cur    when xb_select='0' else hp_cur;
    sum_b <= bp_cur    when xb_select='0' else lp_cur;
    sub_a <= input_cur when sub_a_sel='0' else temp_reg;
    sub_b <= lp_cur    when sub_b_sel='0' else x_reg;

    process(clock)
        variable x_result   : signed(35 downto 0);
        variable sum_result : signed(17 downto 0);
        variable sub_result : signed(17 downto 0);
    begin
        if rising_edge(clock) then
            x_result := xa * xb;
            if mult_enable='1' then
                x_reg <= x_result(33 downto 16);
            end if;

            if lat_now = '1' then
                for i in 1 to g_channels-1 loop
                    in_lat(i) <= signal_in(i);
                end loop;
            end if;

            sum_result := sum_limit(x_reg, sum_b);
            sub_result := sub_limit(sub_a, sub_b);

            -- nell'originale temp_reg riceve prima la somma e poi la
            -- sottrazione: vince sempre la seconda, quindi si scrive solo quella
            temp_reg <= sub_result;

            if sum_to_lp='1' then
                lp_arr(ch_sel) <= sum_result;
            end if;
            if sum_to_bp='1' then
                bp_arr(ch_sel) <= sum_result;
            end if;
            if sub_to_hp='1' then
                hp_arr(ch_sel) <= sub_result;
            end if;

            -- parte di controllo
            instruction <= (others => '0');
            if reset='1' then
                for i in 0 to g_channels-1 loop
                    hp_arr(i) <= (others => '0');
                    lp_arr(i) <= (others => '0');
                    bp_arr(i) <= (others => '0');
                end loop;
                divider <= 0;
                step    <= 0;
                ch_cnt  <= 0;
                -- come i sei filtri originali: la prima raffica parte subito
                -- dopo il reset, senza saltare un periodo
                busy    <= '1';
                lat_now <= '0';
            elsif divider = g_divider-1 then
                divider <= 0;
                step    <= 0;
                ch_cnt  <= 0;
                busy    <= '1';   -- si riparte dal canale 0
            else
                divider <= divider + 1;
                lat_now <= '0';
                if busy = '1' then
                    instruction <= c_program(step);
                    ch_sel      <= ch_cnt;
                    -- la terza istruzione del canale 0: e' li' che si congela
                    if (ch_cnt = 0) and (step = 2) then
                        lat_now <= '1';
                    end if;
                    if step = c_steps-1 then
                        step <= 0;
                        if ch_cnt = g_channels-1 then
                            busy <= '0';
                        else
                            ch_cnt <= ch_cnt + 1;
                        end if;
                    else
                        step <= step + 1;
                    end if;
                end if;
            end if;
        end if;
    end process;

    g_out: for i in 0 to g_channels-1 generate
        low_pass(i) <= lp_arr(i);
    end generate;

end dsvf;
