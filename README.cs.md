[English](README.md) | [Русский](README.ru.md) | Čeština

# H-Shifter pro Mafia: The City of Lost Heaven

Použití skutečné řadicí páky s H-schématem ve hře, která rozumí pouze sekvenčnímu
řazení nahoru a dolů.

<!-- ![Převod H-schématu na sekvenční řazení](docs/img/banner.png) -->

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

## Před instalací

- **Zazálohujte si uložené pozice.** Tento mód nemění žádné herní soubory. Přesto si
  před změnou nastavení ovládání zazálohujte soubory uložených pozic
  (`<složka hry>\savegame\`).
- **Verze hry.** Testováno na hře Mafia verze 1.3 (vydání GOG, anglická verze). Jiné
  verze a vydání testovány nebyly - použití je na vlastní nebezpečí. Protože se nemění
  žádné herní soubory, pravděpodobným selháním je, že hra neuvidí virtuální ovladač,
  nikoli že se něco poškodí.
- **Jiné módy.** Kompatibilita s jinými módy nebyla testována. Kombinování módů je na
  vlastní nebezpečí.

## Co to je

Převodovka ve hře Mafia má přesně dvě akce: zařadit vyšší a zařadit nižší stupeň.
Fyzická páka s H-schématem takto nefunguje - hlásí absolutní polohu, "páka je na
trojce", a neříká nic o tom, odkud přišla. Tyto dva modely se nepotkávají, a proto
obvykle nelze H-páku k této hře vůbec přiřadit.

Tento skript stojí mezi nimi. Každých 10 ms se dotazuje páky, a jakmile páka dosedne
do polohy, odešle přesně tolik sekvenčních řazení, kolik je potřeba k převedení herní
převodovky ze současného stupně na ten, na který páka ukazuje. Přechod z dvojky na
pětku znamená tři řazení nahoru, ze čtyřky na dvojku dvě řazení dolů.

Zpátečka a neutrál vyžadují víc než počítání. Mezi dvěma polohami páka nehlásí žádný
stupeň a "stojí v neutrálu" je nerozlišitelné od "pomalu se přesouvá do zpátečky" -
stejné hodnoty, dva různé záměry. Skript proto považuje neutrál za časované rozhodnutí
se dvěma různými čekacími okny a do zpátečky i neutrálu se dostává hrubou silou přes
známý stav, místo aby věřil vlastnímu počítadlu. Podrobnosti jsou v
[docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md).

Výstup jde přes vJoy, a nikoli přes klávesnici, a má to konkrétní důvod. Mafia se
zařízení DirectInput dotazuje přímo a syntetické klávesové události ignoruje, takže
odesílání kláves pomocí `Send` nedělá vůbec nic. vJoy vytváří skutečný virtuální
joystick DirectInput, který Windows i hra považují za fyzický ovladač.

Žádný herní soubor není měněn. Do procesu hry se nic nevkládá. Skript je převodník
vstupu, který běží vedle hry.

## Požadavky

| | |
|---|---|
| Hra | Mafia: The City of Lost Heaven, verze 1.3 (vydání GOG, anglická verze) - testované sestavení |
| Hardware | Řadicí páka s H-schématem, kterou Windows vidí jako joystick s jedním tlačítkem na každý stupeň |
| Software | [AutoHotkey v2](https://www.autohotkey.com/), [ovladač vJoy](https://sourceforge.net/projects/vjoystick/) |

Většina H-pák (Logitech, Thrustmaster a jejich obdoby) už hlásí každou polohu jako
samostatné tlačítko, což je přesně to, co skript očekává.

## Instalace

1. Nainstalujte **AutoHotkey v2**. Spouštěč jej ve výchozím nastavení očekává v cestě
   `C:\Program Files\AutoHotkey\v2\AutoHotkey64.exe`.
2. Nainstalujte **ovladač vJoy**, otevřete *Configure vJoy* a ujistěte se, že zařízení
   **1** existuje a má alespoň **2 tlačítka**.
3. Stáhněte tento repozitář (zelené tlačítko *Code*, nebo
   [vydání](../../releases), pokud je zveřejněno).
4. Otevřete `src\HShifter_to_vJoy.ahk` v textovém editoru a zkontrolujte blok
   nastavení na začátku souboru:
   - `PhysicalJoystick` - číslo joysticku vaší páky. Zjistíte je nástrojem *Window
     Spy* z AutoHotkey nebo zkusmo. Výchozí hodnota je `3`.
   - `vJoyDeviceID`, `vJoyUpButton`, `vJoyDownButton` - ponechte `1`, `1`, `2`, pokud
     jste vJoy nenastavili jinak.
   - `vJoyDLL` - cesta k `vJoyInterface.dll`, výchozí
     `C:\Program Files\vJoy\x64\vJoyInterface.dll`.
5. V nastavení ovládání hry Mafia přiřaďte:
   - **Gear Up** na zařízení vJoy 1, **tlačítko 1**
   - **Gear Down** na zařízení vJoy 1, **tlačítko 2**
6. Spusťte `src\HShifter_to_vJoy.ahk` a poté hru. Nebo použijte
   `tools\Launch Mafia with Shifter.ps1`, který spustí skript, potom hru a po ukončení
   hry skript zastaví - nejprve upravte cesty na začátku tohoto souboru.

Odinstalace: zavřete skript. Není co vracet zpět.

## Použití

Tlačítka páky odpovídají převodovým stupňům:

| Tlačítko páky | Výsledek |
|---|---|
| 1 - 6 | Zařadit stupeň 1 až 6 |
| 7 | Tvrdý reset: řazení dolů až do zpátečky, poté jednou nahoru do neutrálu |
| 8 | Zpátečka |
| žádné, po dobu 0,6 s | Automatický neutrál (1,5 s, pokud byl posledním stupněm zpátečka) |

Během běhu skriptu jsou k dispozici dvě klávesové zkratky:

| Klávesa | Výsledek |
|---|---|
| `F9` | Zobrazit stupeň, který skript považuje za zařazený |
| `F10` | Tvrdý reset do neutrálu, stejné jako tlačítko 7 na páce |

Po každé změně zobrazí bublinová nápověda zařazený stupeň. Pokud se představa skriptu
o zařazeném stupni někdy rozejde se hrou, stiskněte `F10` a obojí se vrátí do známého
stavu.

## Kompatibilita

| Verze | Stav |
|---|---|
| Mafia v1.3, vydání GOG (anglická verze) | Testováno |
| Vydání Steam | Netestováno |
| Ostatní jazykové verze, v1.0 - v1.2 | Netestováno |

Protože se ve složce hry nic nemění, je tento mód s ostatními módy kompatibilní už ze
své podstaty. Pouze odesílá stisky tlačítek virtuálnímu ovladači.

Dva související módy níže vznikaly společně s tímto a běží s ním v jedné instalaci.

## Známé problémy

- **Sledování stupně je bez zpětné vazby.** Skript si vede vlastní počítadlo
  zařazeného stupně a nedokáže přečíst skutečný stav herní převodovky. Pokud hra
  řazení odmítne, například při rychlosti, při níž nepodřadí, obě představy se
  rozejdou. Tlačítko 7 na páce nebo `F10` je znovu sjednotí uvedením do známého stavu.
- **Číslo joysticku je nastavení, nikoli automatická detekce.** Pokud vaše páka není
  joystick číslo 3, je nutné skript upravit, jinak neudělá nic.
- **Neutrál je časované rozhodnutí.** Podržení páky mezi dvěma stupni déle než čekací
  okno zařadí neutrál. Je to záměr a okno při výjezdu ze zpátečky je delší, protože
  dráha ze zpátečky na jedničku je dlouhá, ale velmi pomalé řazení je i tak může
  překročit.

## Související módy

Tři módy pro jednu hru, vyvíjené společně. Jsou vzájemně kompatibilní v jedné
instalaci, ale každý řeší jinou úlohu:

- **H-Shifter** - tento repozitář.
- **Force Feedback (Real Driving Mod)** - silová zpětná vazba pro volanty, kterou hra
  sama nikdy neodesílá. Dosud nezveřejněno; odkaz zde přibude po zveřejnění.
- **VR Mod** - stereoskopická virtuální realita se sledováním pohybu hlavy. Dosud
  nezveřejněno.

## Pro vývojáře

Zdrojový kód je otevřený záměrně. Pokud na něm chcete postavit vlastní vstupní mód, je
zde vše a není co dalšího hledat.

- `src\HShifter_to_vJoy.ahk` - celý mód, jeden soubor, AutoHotkey v2.
- `src\test_vjoy_minimal.ahk` - minimální test vJoy: načtení knihovny DLL, získání
  zařízení, impulz tlačítka 1 a zápis všech návratových hodnot do protokolu. Spusťte
  jej jako první, když nic nefunguje: oddělí "vJoy není nastaven" od "logika páky je
  špatně".
- [docs/HOW-IT-WORKS.md](docs/HOW-IT-WORKS.md) - dotazovací smyčka, asymetrické okno
  neutrálu, proč jsou sekvence řazení atomické a jak funguje reset s ohledem na
  zpátečku. V těchto třech věcech spočívá celá obtížnost úlohy; přečtěte si dokument
  dříve, než začnete měnit časování.

Ve vrstvě převodu není nic specifického pro tuto hru kromě dvou výstupních tlačítek,
takže stejný postup lze přenést na jakoukoli hru s výhradně sekvenční převodovkou.

Pull requesty a forky jsou vítány.

## Poděkování

- [vJoy](https://sourceforge.net/projects/vjoystick/) od Shaula Eizikoviche - ovladač
  virtuálního joysticku, díky kterému je to vše možné.
- [AutoHotkey](https://www.autohotkey.com/) - GPL-2.0.

## Licence

MIT - viz [LICENSE](LICENSE). Vztahuje se pouze na vlastní kód tohoto projektu.

Tento mód vyžaduje legální kopii hry Mafia: The City of Lost Heaven. Žádné herní
soubory ani data nejsou součástí módu a nejsou distribuovány.

Tento projekt není spojen se společnostmi Take-Two Interactive, 2K ani Illusion
Softworks a není jimi schválen. Mafia je ochranná známka příslušných vlastníků.
Licence MIT se vztahuje pouze na vlastní kód tohoto projektu; hra a její data zůstávají
majetkem svých vlastníků.
