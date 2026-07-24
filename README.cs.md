[English](README.md) | [Русский](README.ru.md) | Čeština

# H-Shifter pro Mafia: The City of Lost Heaven

Hrajte Mafii (2002) se skutečnou řadicí pákou s H-schématem. Bez AutoHotkey, bez vJoy,
bez virtuálního ovladače: zásuvný modul uvnitř hry a malý nástroj pro nastavení páky.

<!-- ![banner](docs/img/banner.png) -->

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Před instalací

- **Zazálohujte si uložené pozice.** Před instalací zkopírujte
  `<složka hry>\savegame\`. Mód nemění `Game.exe` ani žádný datový soubor hry, spolehlivá
  je však pouze záloha, kterou máte pod vlastní kontrolou.
- **Verze hry.** Vyvinuto a testováno na GOG vydání hry Mafia verze 1.3 (anglická
  verze). Offsety, ze kterých se čte zařazený stupeň, jsou vázané na konkrétní sestavení,
  takže na jiných verzích a vydáních se uzavřená smyčka nedohledá. Použití je na vlastní
  nebezpečí.
- **Jiné módy.** Kompatibilita s jinými módy nebyla testována. Kombinování módů je na
  vlastní nebezpečí. Mód načítá Ultimate ASI Loader, který spolu s ním načte i jakékoli
  další vaše `.asi` módy; je to záměr, ale takové kombinace testovány nebyly.
- **Antivirus.** `gearbox-setup.exe` je nepodepsaný spustitelný soubor, který zapisuje do
  složky hry, a samotný mód je knihovna načtená do procesu hry. Některé antivirové
  programy mohou označit kterýkoli z nich. Ověřte stažený soubor pomocí kontrolního
  součtu SHA-256 zveřejněného u vydání.

## Co to je

Převodovka ve hře Mafia je sekvenční: hra zná "zařadit vyšší" a "zařadit nižší" a nic
dalšího. Páka s H-schématem hlásí pravý opak - absolutní polohu, "páka je na trojce",
bez jakékoli historie. Kvůli tomuto nesouladu nelze H-páku v nastavení hry jednoduše
přiřadit.

Tento mód rozdíl překlenuje uvnitř hry. Čte vaši páku přes DirectInput, určí, který
stupeň požadujete, a tiskne klávesy řazení přiřazené v samotné hře, dokud převodovka
není na daném stupni. Nic se neemuluje: nevytváří se žádný virtuální joystick a do
Windows se neposílají žádné syntetické stisky.

Podstatné je, že mód po každém kroku **čte zařazený stupeň zpět ze hry**, na adrese
`[car+0x58]+0x5D0` u sestavení GOG. Právě tato uzavřená smyčka zajišťuje spolehlivost.
Žádné vlastní počítadlo, které by se mohlo rozejít, neexistuje, takže odmítnuté řazení
nemůže rozhodit stav, a vůz se dvěma stupni prostě přestane řadit, až mu dojdou: volba
čtyřky na dvoustupňovém nákladním voze jej nechá na dvojce, místo aby poškodila
převodovku. Tabulka převodů pro každý vůz není potřeba a neexistuje.

Mód také čte příznak ruční/automatické převodovky (`[car+0x58]+0x53C`) a nikdy do něj
nezapisuje. Pokud přepnete na automat - volantem, klávesnicí, čímkoli -, je to vaše
rozhodnutí a mód se drží stranou, dokud znovu nepohnete pákou.

Do složky hry přibudou dva soubory a `Game.exe` se nemění. Vypínač v nástroji přejmenuje
zásuvný modul, takže při dalším spuštění je hra zcela původní.

## Požadavky

| | |
|---|---|
| Hra | Mafia: The City of Lost Heaven, vydání GOG verze 1.3 (anglická verze) - jediné testované sestavení |
| Hardware | Řadicí páka s H-schématem, kterou Windows vidí jako zařízení DirectInput. Ovladač režimu převodovky může být na základně volantu; podporována jsou až čtyři zařízení |
| Software | ASI loader ve složce hry - Ultimate ASI Loader jako `dinput8.dll`. Instaluje jej patcher módu Force Feedback; jinak si jej nainstalujte sami |
| Ve hře | Řazení nahoru, dolů a režim převodovky musí být v nastavení ovládání hry Mafia přiřazeny klávesám. Mód tiskne právě je |

## Instalace

1. Ujistěte se, že je přítomen ASI loader: `dinput8.dll` vedle `Game.exe`.
2. Stáhněte archiv vydání a rozbalte složku `gearbox hshifter setup` do složky hry tak,
   aby stála vedle `Game.exe`.
3. Spusťte `gearbox-setup.exe` z této složky. Soubor `Game.exe` najde o úroveň výš,
   takže není třeba nic nastavovat.
4. Přiřaďte své vybavení. Klikněte na řádek a poté stiskněte, co si přejete:
   - řádky pro stupně 1 až 6, zpátečku, neutrál a ovladač režimu převodovky zachytávají
     **tlačítka zařízení**, z libovolného připojeného zařízení;
   - poslední tři řádky zachytávají **klávesy** - řazení nahoru, dolů a režim tak, jak
     jsou přiřazeny uvnitř hry Mafia. Mód tiskne právě je, takže se musí shodovat.
   - Neutrál je výslovná volba: přiřaďte tlačítko, nebo určete, že neutrál je klidová
     poloha vaší páky. Existuje hardware obojího druhu a žádná výchozí hodnota není.
5. Stiskněte **Install**. Nástroj zkopíruje `gearbox_hook.asi` do kořene hry a zapíše
   vaše nastavení do `gearbox hshifter setup\gearbox.ini`.
6. Spusťte hru a jeďte.

Výsledné uspořádání:

```
<složka hry>\
    Game.exe
    dinput8.dll                       ASI loader, není součástí tohoto módu
    gearbox_hook.asi                  mód
    gearbox hshifter setup\
        gearbox-setup.exe             nástroj pro přiřazení
        gearbox.ini                   vaše nastavení
        gearbox_hook.bin              protokol módu, zapisovaný za hry
```

Odinstalace: vypínač v nástroji mód vypne přejmenováním zásuvného modulu a další
spuštění je původní. Pro úplné odstranění smažte obě přidané položky. Ničeho jiného se
mód nedotkl.

## Použití

Pohnete pákou a převodovka ji následuje. Není co držet ani co časovat.

| Ovládání | Chování |
|---|---|
| Poloha stupně | Provede se okamžitě. Mód řadí, dokud hra nehlásí daný stupeň nebo dokud převodovka odmítne pokračovat |
| Klidová poloha (není-li neutrál přiřazen) | Neutrál po 1100 ms. Prodleva existuje proto, že páka klidovou polohou prochází cestou mezi stupni; naměřený přejezd trvá 0,22 až 0,94 s, zatímco záměrný neutrál 1,1 s a více |
| Odjezd ze zpátečky | Prodleva se neuplatní - tento pohyb vede přes neutrál z podstaty věci |
| Ovladač režimu převodovky | Přepíná hru mezi ruční a automatickou převodovkou. Funguje jako tlačítko i jako aretovaný přepínač; nástroj má nastavení, co z toho máte |

Tlačítka, která přiřadíte, jsou před hrou skryta, takže držené tlačítko neutrálu
neovládne obrazovku přiřazení ovládání ve hře a tlačítka stupňů nespouštějí nesouvisející
akce.

Pokud hru přepnete do automatu, mód přestane převodovku řídit, dokud znovu nepohnete
pákou. Pohyb páky vždy znamená "chci tento stupeň", takže vrací ruční režim.

## Kompatibilita

| Verze | Stav |
|---|---|
| Mafia v1.3, vydání GOG (anglická verze) | Vyvinuto a testováno |
| Vydání Steam | Netestováno. Offsety jsou vázané na sestavení a nedohledají se |
| Ostatní jazykové verze, v1.0 - v1.2 | Netestováno |

`Game.exe` se nemění, takže není co obnovovat a kontrola integrity souborů nic
nenajde. Mód je jedním z `.asi`, kolik jich máte; loader načte všechny.

## Známé problémy / FAQ

### Známé problémy

- **Ověřování není dokončeno.** Sedm testovacích jízd určilo podobu řešení a odstranilo
  čtyři chyby; osmá jízda, která znovu prověřuje všechny opravy najednou na skutečném
  hardwaru, zatím nebyla potvrzena. Berte to jako fungující mód ve fázi zkoušek, nikoli
  jako hotový.
- **Uzavřená smyčka se povoluje podle sestavení.** U sestavení, kde offset stupně není
  ověřen, ponechá `closed_loop=0` v souboru ini mód pracovat v otevřené smyčce, zatímco
  protokol zaznamená, zda se řetěz adres dohledal. Spolehlivé chování vyžaduje zapnutou
  smyčku.
- **Vaše herní přiřazení kláves musí být v nástroji uvedena správně.** Mód řadí
  stisknutím kláves přiřazených ve hře. Přiřaďte v Mafii jiné klávesy a zapomeňte
  aktualizovat nástroj - nestane se vůbec nic.
- **Bufferovaný vstup z klávesnice není implementován.** Mód vstupuje do
  `IDirectInputDevice8::GetDeviceState`. Pokud sestavení čte klávesnici přes
  `GetDeviceData`, protokol to ukáže a řazení se nedoručí.
- **Automatický režim si s módem ze své podstaty odporuje.** Je-li hra v automatu a mód
  má zařadit stupeň, protokol to řekne otevřeně, místo aby to skrýval.

### FAQ

**Pro koho je tento mód?**
Pro každého, kdo vlastní skutečnou řadicí páku s H-schématem, nebo jakékoli zařízení
DirectInput s dostatkem tlačítek, a chce, aby převodovka Mafie přímo sledovala polohu
páky - bez virtuálního ovladače, bez skriptovacího jazyka, bez jakéhokoli nastavování
uvnitř hry nad rámec kláves řazení, které už má přiřazené.

**Jak začít?**
Ujistěte se, že máte ASI loader, rozbalte archiv vydání do složky hry, spusťte
`gearbox-setup.exe`, přiřaďte polohy své páky a ověřte klávesy, kterými Mafia sama
řadí nahoru/dolů, stiskněte Install a jeďte. Celý postup je v části
[Instalace](#instalace).

**Na kterých verzích hry to funguje?**
Testováno pouze na GOG vydání Mafia verze 1.3, anglická verze - právě na něm byly
zjištěny zdokumentované offsety paměti. Na jiné verzi nebo vydání se uzavřená smyčka
nedohledá a spolehlivé chování nelze čekat.

**Co mi to dá?**
Skutečné sekvenční řazení podle skutečné polohy páky, uzavřené na vlastní zařazený
stupeň hry tak, že se s ním nelze rozejít, a automatické omezení na skutečný počet
stupňů daného vozu - dvoustupňový nákladní vůz prostě přestane reagovat po dvojce,
místo aby poškodil převodovku. `Game.exe` se přitom nikdy nemění.

**Co mi to NEDÁ?**
- Silovou zpětnou vazbu na volantu - to je samostatný mód, [Force Feedback (Real
  Driving Mod)](#související-módy), zatím nezveřejněný.
- VR - také samostatný mód, [VR Mod](#související-módy).
- Simulaci spojky ani model otáček motoru: mód pouze tiskne klávesy řazení nahoru/dolů,
  které už má hra přiřazené.
- Podporu jiných verzí než GOG v1.3 anglicky - viz výše.
- Hotový, dokončený produkt: osmá ověřovací jízda zatím nebyla potvrzena - viz Známé
  problémy výše.

## Související módy

Tři módy pro jednu hru, vyvíjené společně. Jsou vzájemně kompatibilní v jedné instalaci,
ale každý řeší jinou úlohu:

- **H-Shifter** - tento repozitář.
- **Force Feedback (Real Driving Mod)** - silová zpětná vazba pro volanty, kterou hra
  sama nikdy neodesílá. Jeho patcher zároveň instaluje ASI loader, který tento mód
  potřebuje. Dosud nezveřejněno; odkaz zde přibude po zveřejnění.
- **VR Mod** - stereoskopická virtuální realita se sledováním pohybu hlavy. Dosud
  nezveřejněno.

## Pro vývojáře

Zdrojový kód je otevřený záměrně. Vše, co mód o hře ví, je v tomto repozitáři: offsety,
způsob vstupu do hry i rozhodovací logika.

| Soubor | Co to je |
|---|---|
| `src/gearbox_hook8.c` | mód: hooky DirectInput, vkládání kláves, potlačení tlačítek, uzavřená smyčka |
| `src/gearbox_logic.h` | rozhodovací jádro sdílené s testy, aby testy pokrývaly skutečně dodávaný kód |
| `src/gearbox_gui.c` | `gearbox-setup.exe`: nástroj pro přiřazení, čisté Win32, bez frameworků |
| `tests/test_gearbox.c` | offline simulace logiky řazení |

Sestavení pomocí LLVM-MinGW, 32 bitů:

```
i686-w64-mingw32-clang -O2 -m32 -mwindows -o gearbox-setup.exe gearbox_gui.c -lkernel32 -luser32 -lcomdlg32
```

Jak se řazení doručí: hra čte klávesnici přes DirectInput 8, takže mód vytvoří vlastní
zařízení klávesnice výhradně kvůli přístupu k vtable třídy zařízení, opatchuje
`GetDeviceState` a nastaví bit příslušného scancode v 256bajtovém bufferu, který se hra
chystá přečíst. Klávesnici rozpozná právě velikost bufferu 256 bajtů; vlastní zařízení
módu jsou vyloučena. Nastavení bitu a jeho vynulování je tatáž cesta kódem a na tomtéž
stojí potlačení tlačítek na straně volantu.

Zdokumentované offsety sestavení GOG; vše je pouze pro čtení kromě stavu vstupu:

| Co | Kde |
|---|---|
| aktuální stupeň (povel, nikoli odvozený z rychlosti) | `[car+0x58]+0x5D0` |
| stínová hodnota stupně o cyklus zpět - probíhající řazení | `[car+0x58]+0x5D4` |
| příznak ruční / automatické převodovky, 0 = ruční | `[car+0x58]+0x53C` |
| tabulka akcí pro přiřazení kláves, 63 záznamů `char[16]` + `DWORD` | VA `0x624330..0x624808` |
| identifikátory akcí GEARUP / GEARDOWN | `0x2D` / `0x2F` |

Identifikátory akcí jsou uvedeny proto, že vkládání akce namísto klávesy je plánované
vylepšení: zbavilo by uživatele nutnosti přiřazovat klávesy řazení vůbec. Místo vstupu
pro ně zatím nebylo odvozeno.

Uzavřená smyčka, zpracování odmítnutí a čtyři chyby, které je zformovaly, jsou popsány v
[docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md). Pull requesty a forky jsou vítány.

Chcete postavit podobný closed-loop překladač vstupu pro jinou hru? V
[docs/PORTING.md](docs/PORTING.md) (anglicky) je popsána metoda: technika vkládání, jak
ve své hře hledat obdobu "aktuálního stupně" a jak otestovat rozhodovací logiku dřív, než
sáhnete na samotnou hru - beze ztráty čehokoli z disassembly Mafie.

## Poděkování

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) od ThirteenAG
  - loader, díky kterému jsou `.asi` zásuvné moduly možné. Zde není distribuován.

## Licence

MIT - viz [LICENSE](LICENSE). Vztahuje se pouze na vlastní kód tohoto projektu.

Tento mód vyžaduje legální kopii hry Mafia: The City of Lost Heaven. Žádné herní soubory
ani data nejsou součástí módu a nejsou distribuovány.

Tento projekt není spojen se společnostmi Take-Two Interactive, 2K ani Illusion
Softworks a není jimi schválen. Mafia je ochranná známka příslušných vlastníků. Licence
MIT se vztahuje pouze na vlastní kód tohoto projektu; hra a její data zůstávají majetkem
svých vlastníků.
