# DOSBOX_CTL_PROTOCOL.md — externí řízení DOSBox-X vs. nativní reimplementace

Systém pro automatizované srovnávací testování: `dosbox-x` (upravený,
`engine.cpp`) i nativní reimplementace (`reorion2`) čtou **stejný externí
konfigurační soubor**, který určuje:

- **CO** ukládat/dumpovat a **za jakých podmínek** (`DUMP`)
- **jaké hodnoty v paměti změnit** a za jakých podmínek (`SET`, jen DOSBox)
- **kdy skončit** / zavřít se (`STOP`)

Vše čistě v C/C++, žádný Python. Cílový use-case: externí orchestrátor (tvůj
vlastní kód) spustí `dosbox-x.exe` s konkrétním configem, ten doběhne, sám se
zavře, totéž se spustí pro `reorion2.exe`, a výstupy (trace soubory) se pak
porovnají.

## Soubory

| Soubor | Kam patří | Účel |
|---|---|---|
| `ctl_common.h` | oba projekty | Parser configu + vyhodnocování podmínek/akcí (sdílené, beze změny) |
| `gen_watchtable.cpp` | spouští se ručně/v buildu | C++ nástroj: z `orion_common.h` vygeneruje watch-tabulky pro obě strany |
| `engine.cpp` | DOSBox-X projekt | Náhrada tvého `engine.cpp` — čte config, čte emulovanou paměť |
| `native_ctl.h` | reorion2 projekt | Obdoba pro nativní reimplementaci — čte config, čte C proměnné |
| `trace_dosbox_symbols.gen.cpp` | DOSBox-X projekt | **Generováno** `gen_watchtable.cpp` |
| `trace_native_symbols.gen.cpp` | reorion2 projekt | **Generováno** `gen_watchtable.cpp` |

## Krok 1 — vygeneruj watch-tabulky

```
g++ -std=c++17 -O2 -o gen_watchtable gen_watchtable.cpp
./gen_watchtable cesta/k/orion_common.h vystupni_slozka/
```

(Nebo `cl /EHsc /std:c++17 gen_watchtable.cpp` na Windows.) Vznikne
`trace_dosbox_symbols.gen.cpp` a `trace_native_symbols.gen.cpp` — spusť znovu
pokaždé, když se `orion_common.h` změní (nové funkce, opravené globály).
Momentálně se generují jen skalární proměnné šířky 1/2/4 bajty; pole s
neurčenou velikostí a 8bajtové typy jsou mimo rozsah — viz sekce Omezení.

## Krok 2 — konfigurační soubor

Textový, řádkově orientovaný, žádné závislosti na parseru:

```
# radky s # a prazdne radky se ignoruji
OUTPUT file=dosbox_trace.txt

# dump pri kazdem volani funkce sub_26D0D
DUMP  cond=call:0x26D0D              label=sub_26D0D

# periodicky dump kazdych 50000 cyklu (heartbeat, i bez konkretniho volani)
DUMP  cond=every:50000               label=heartbeat

# dump kdykoliv se zmeni hodnota (napr. skore hrace)
DUMP  cond=changed:0x197F98:4        label=score_changed

# patchni pamet, jakmile beh dosahne dane adresy (napr. preskoc intro)
SET   cond=eip:0x2A51AD              addr=0x2A51AD size=1 value=0x01

# ukonci beh za kterekoliv z nasledujicich podminek
STOP  cond=eip:0x236FE6
STOP  cond=cycle_ge:20000000
STOP  cond=eq:0x197F98:4:0x00000005
```

### Podmínky (`cond=`)

| Zápis | Splněno, když |
|---|---|
| `cycle:N` | počítadlo kroků == N (jednorázově) |
| `cycle_ge:N` | počítadlo kroků >= N |
| `every:N` | počítadlo kroků dělitelné N |
| `eip:0xADDR` | EIP == ADDR (jen DOSBox, kontroluje se každý krok) |
| `call:0xADDR` | cíl volání == ADDR (odpovídá adrese `sub_ADDR`) |
| `call:*` | jakékoliv volání |
| `changed:0xADDR:W` | W-bajtová hodnota na ADDR se od minulého vyhodnocení změnila |
| `eq:0xADDR:W:0xVAL` | W-bajtová hodnota na ADDR == VAL |
| `neq:0xADDR:W:0xVAL` | W-bajtová hodnota na ADDR != VAL |

Víc `cond=` na jednom řádku = **AND** (musí platit všechny zároveň).

### Akce

- **`DUMP`** — zapíše blok `CKPT ... ENDCKPT` se všemi sledovanými symboly
  (nebo jen vybranými přes `fields=jmeno1,jmeno2`) do výstupního souboru.
  Výchozí opakování: `always` (zapisuje pokaždé, kdy podmínka platí).
- **`SET`** — zapíše `value=` (šířky `size=` bajtů) na `addr=`. **Jen
  DOSBox strana** — nativní kód nemá adresovatelnou paměť ke kterékoliv
  proměnné za běhu, takže `SET` tam vypíše varování a nic neudělá.
  Výchozí opakování: `once`.
- **`STOP`** — uzavře výstupní soubor a ukončí proces (`exit(0)`). Výchozí
  opakování: `once`.

Explicitní přepsání opakování: `repeat=once` / `repeat=always`.

## Krok 3 — DOSBox-X strana

1. Zkopíruj `ctl_common.h`, vygenerovaný `trace_dosbox_symbols.gen.cpp` a
   nový `engine.cpp` do projektu (nahraď svůj stávající `engine.cpp`).
2. **Uprav `TURN_ADVANCE_EIP`/`GAME_END_EIP`** v `engine.cpp` — jsou převzaté
   z tvého původního souboru pro konkrétní build hry, ověř že sedí.
3. Konfigurační soubor se hledá takto (pro snadné paralelní spouštění více
   instancí najednou):
   - proměnná prostředí `DOSBOX_CTL_FILE`, pokud je nastavená
   - jinak `dosbox_ctl.cfg` v aktuálním adresáři
4. Přelož a spusť. Po dosažení `STOP` podmínky se proces sám ukončí —
   externí orchestrátor tedy stačí spustit a počkat na ukončení procesu.

## Krok 4 — nativní reimplementace (reorion2)

1. Zkopíruj `ctl_common.h`, vygenerovaný `trace_native_symbols.gen.cpp` a
   `native_ctl.h` do projektu.
2. V `main()`:
   ```cpp
   #include "trace_native_symbols.gen.cpp"   // MUSÍ být po ctl_common.h
   #include "native_ctl.h"

   int main() {
       native_ctl_init();          // nacte stejny config soubor
       while (hra_bezi) {
           herni_tah();
           native_ctl_tick();      // vyhodnoti cycle/every/changed/eq podminky
       }
       native_ctl_shutdown();
   }
   ```
   **Pořadí includů je důležité:** `ctl_common.h` (definuje `WatchEntry`) →
   `trace_native_symbols.gen.cpp` (používá `WatchEntry`) → `native_ctl.h`.
3. Do funkcí `sub_XXXXX`, které chceš sledovat (odpovídá `call:0xADDR`
   podmínkám v configu), přidej na první řádek:
   ```cpp
   void sub_26D0D(int a1) {
       NATIVE_CTL_ON_CALL(sub_26D0D);   // adresu si vytáhne ze jména funkce
       ...
   }
   ```

### Rozdíly oproti DOSBox straně

- "Cyklus" na nativní straně není CPU cyklus, ale **libovolný krok**, který
  definuješ sám voláním `native_ctl_tick()` — typicky jednou za herní
  tah/frame. `cycle`/`cycle_ge`/`every` podmínky se počítají podle toho.
- `eip:` podmínky nemají na nativní straně smysl (žádné EIP) — použij
  `call:` na vstupu instrumentovaných funkcí.
- `changed`/`eq`/`neq` fungují jen pro adresy, které mají odpovídající
  položku ve watch tabulce (tj. jsou to sledované globální proměnné) —
  libovolná "syrová" adresa jako u DOSBoxu tu neexistuje.
- `SET` není podporován (viz výše).

## Krok 5 — porovnání výstupů

Formát trace souboru (stejný na obou stranách):

```
CKPT <label> cycle=<N>
jmeno_promenne=hodnota_hex
jmeno_promenne2=hodnota_hex
...
ENDCKPT
```

Řádkový text — jde otevřít v běžném `diff`/`git diff`, nebo napiš vlastní
porovnávač přímo v reorion2 projektu (načti oba soubory, spáruj bloky podle
pořadí `CKPT` se stejným `label`, porovnej pole). Dá se udělat jako
samostatná utilitka ve stejném projektu — pokud chceš, doplním ji jako další
`.cpp` v tomhle stylu (bez Pythonu).

## Automatický cyklus (externí orchestrátor)

Typický běh:
1. Vygeneruj/uprav `dosbox_ctl.cfg` pro danou testovací scénu.
2. Spusť `dosbox-x.exe` (headless, s daným configem) → čekej na ukončení
   procesu → `dosbox_trace.txt`.
3. Spusť `reorion2.exe` se **stejným** configem (soubor lze sdílet, akce
   `SET` se na nativní straně jen přeskočí) → čekej na ukončení →
   `native_trace.txt`.
4. Porovnej oba soubory.
5. Podle výsledku uprav config (např. přidej `DUMP` na místo první
   divergence s jemnějším `every:`) a zopakuj.

Pro paralelní běhy (více scénářů najednou) použij `DOSBOX_CTL_FILE` env.
proměnnou s unikátní cestou pro každou instanci, ať si nešlapou na
`dosbox_ctl.cfg`.

## Omezení a rozšíření

- **Pole s neurčenou velikostí** (~1076 symbolů) nejsou ve watch tabulce.
  Chceš-li konkrétní pole sledovat, přidej ručně řádek do obou
  `trace_*_symbols.gen.cpp` (nebo řekni které tě zajímá, rozšířím generátor).
- **8bajtové typy** (`_QWORD`, `double`) nejsou zatím podporované — jde
  rozšířit v `gen_watchtable.cpp` (přidat `mem_readd` dvakrát pro DOSBox,
  `%016llX` formát) — ozvi se, pokud je potřebuješ hned.
- **Registry (EAX, ESI, …) a ESP/EBP** nejsou v aktuální verzi součástí
  `DUMP` bloku (jen sledované globální proměnné). Pokud je chceš zahrnout na
  DOSBox straně, přidej vlastní `fprintf` řádky do `CtlEngine::do_dump()` v
  `ctl_common.h`.
- **`saveactstate()`** z předchozí verze `engine.cpp` (těžký kompletní
  16MB snapshot) tu zůstala zachovaná pro ruční hloubkovou analýzu
  konkrétního bodu, kde `DUMP`/porovnání ukáže první rozdíl.
