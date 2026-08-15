[English](README.md) | [Русский](README.ru.md) | Čeština

# ALXGtech Mafia 1 Mods

Silová zpětná vazba volantu, kamera z pohledu řidiče a H-řadička pro GOG vydání hry Mafia: The
City of Lost Heaven. Jeden nástroj, který to vše nainstaluje i odinstaluje.

<!-- docs/img/banner.png - not in this release yet -->

## 🚀 Rychlý start

Tři módy pro **Mafia: The City of Lost Heaven** - tu první - v jednom malém programu.
Příliš dlouhé na čtení? To je všechno:

1. Stáhněte `.zip` ze sekce **[Releases](../../releases)**.
2. Rozbalte jej.
3. Zkopírujte `ALXGtech Mafia 1 Mods.exe` do složky s Mafií, vedle `Game.exe`.
4. Spusťte jej.
5. V programu zapněte módy, které chcete, a hrajte.

| | mód | v tomto vydání |
|:--:|---|---|
| 🎮 | **Silová zpětná vazba** - kompletní sada sil pro direct drive volant | ✅ hotovo |
| 👁️ | **Kamera z pohledu řidiče** - místo řidiče a zorné pole odpovídající 16:9 | ✅ hotovo |
| 🕹️ | **H-řadička** - skutečná kulisa ovládá vlastní převody hry | ✅ hotovo |
| 🥽 | **VR** | 🚧 rozpracováno - záložka je v okně, zatím nic neinstaluje |

Vše níže jsou podrobnosti: co jednotlivé módy dělají, co zapisují do složky hry a jak je vrátit
zpět.

## Než začnete instalovat

- **Zazálohujte si uložené pozice.** Jsou v `<game>\savegame\`. Instalátor zaznamenává každý
  soubor, který zapíše, a umí vše vrátit zpět, ale vaše vlastní záloha je ta, kterou máte pod
  kontrolou.
- **Verze.** Postaveno pro GOG vydání Mafia v1.3 (build 16073). Všech osm jazykových vydání má
  identický `Game.exe`, takže poslouží kterékoli; hrálo se s anglickým. Nástroj váš `Game.exe`
  ověří a řekne, pokud to není sestavení, na kterém bylo vše měřeno. Jakákoli jiná verze nebo
  vydání z jiného obchodu je na vlastní riziko.
- **Ostatní módy.** Kompatibilita s jinými módy nebyla testována. Přibalený ASI loader načte i
  vaše ostatní `.asi` módy, což je záměr, ale neověřený.
- **Antivirus.** `ALXGtech Mafia 1 Mods.exe` je nepodepsaný spustitelný soubor, který zapisuje do
  složky hry, takže jej některé antiviry mohou označit. Ověřte stažený soubor proti kontrolním
  součtům SHA-256 zveřejněným u vydání.

## Co to je

Čtyři módy v jednom okně, každý se zapíná a vypíná na vlastní záložce. Tři fungují už dnes;
čtvrtý poctivě říká, že zatím ne.

**🎮 Silová zpětná vazba.** Kompletní sada sil DirectInput8 řízená živým stavem vozu: dostředivá
síla podle rychlosti, tlumič při stání, odlehčení při ztrátě přilnavosti, obrubníky, tramvajové
koleje, houpání mimo silnici a nárazy. Je stavěná pro dnešní direct drive volanty lidmi, kteří
na nich jezdí, protože originál se nechová tak, jak by se volant v roce 2026 chovat měl.
Původní hra volantem při srážce skutečně trhne; tohle to nahrazuje plným modelem sil. Nastavení
se načítá za běhu, takže změna na záložce Force Feedback je cítit v další zatáčce, ne až po
restartu.

**👁️ Kamera z pohledu řidiče.** Kamera sedí na místě řidiče, ne za vozem. Chůze zůstává beze
změny. Posazení je nastavené tam, kde bylo vyladěno za volantem, a lze s ním hýbat klávesami
přímo za jízdy; kde je necháte, tam zůstane. Zapnutí tohoto módu zároveň rozšíří zorné pole
hry ze 70 na 86 stupňů, což odpovídá obrazovce 16:9 - a 70 vypadá nejhůř právě z místa řidiče.
Úhel se nastavuje posuvníkem na téže záložce a vypnutí módu vrátí původní bajty.

**🕹️ H-řadička.** Skutečná kulisa ovládá vlastní převody Mafie, takže kulisa znamená převod. Módul
čte řadičku přes DirectInput, skryje hře přiřazená tlačítka a po každém zařazení si ověří
převod přímo ve hře, takže se nemůže rozejít se skutečností a sám se omezí na skutečný počet
převodů daného vozu bez tabulky pro každý z nich.

Čtvrtý mód, **🥽 VR**, je **🚧 již brzy**. Jeho záložka v okně je proto, aby nikdo nemusel přemýšlet,
zda se na něj nezapomnělo, a říká totéž: není hotový, zatím nic neinstaluje.

## Požadavky

- Windows, 32 nebo 64 bitů.
- Mafia: The City of Lost Heaven, vydání GOG, v1.3 build 16073 (`Game.exe`, 2 355 200 bajtů,
  md5 `b500437f340b8a2f1e847e10bb974a06`). Libovolná jazyková verze.
- Silová zpětná vazba: volant s DirectInput force feedback. Vyvíjeno a testováno na direct
  drive základně.
- H-řadička: kulisová řadička, kterou Windows vidí jako herní ovladač.
- Nic dalšího. Žádné AutoHotkey, žádné vJoy, žádný virtuální ovladač, žádný .NET.

## Instalace

1. Stáhněte `.zip` ze sekce [Releases](../../releases) a rozbalte jej.
2. Zkopírujte `ALXGtech Mafia 1 Mods.exe` do složky s Mafií, vedle `Game.exe`.
3. Spusťte jej. V záhlaví je vidět, se kterou složkou pracuje a jaké sestavení našel; pokud je
   složka špatná, použijte `Choose...`.
4. Otevřete záložku a stiskněte `Enable this mod`. Přepínač je instalace; žádné tlačítko uložit
   neexistuje a nic dalšího mačkat netřeba.
5. Spusťte hru.

Mód odeberete stisknutím `Disable this mod` na jeho záložce. ASI loader odchází s posledním
módem, který jej potřeboval, a s ním i vlastní složka nástroje.

### Co zapnutí módu přidá do složky hry

| mód | soubory |
|---|---|
| každý mód | `dinput8.dll` (Ultimate ASI Loader) |
| Silová zpětná vazba | `mafia_ffb.asi` |
| Pohled řidiče | `mafia_fp.asi`, `mafia_fp.ini` a 12 bajtů uvnitř `Game.exe` (zorné pole) |
| H-řadička | `gearbox_hook.asi`, `ALXG mods\gearbox hshifter setup\gearbox.ini` |

Zorné pole je jediná věc, která se zde dotýká `Game.exe`, a jsou to tři čtyřbajtové floaty,
stejné délky, nic se neposouvá. `Game.exe.bak` vznikne před prvním zápisem, původní bajty se
zaznamenají a vypnutí kamery je zapíše zpět. Herních dat se to nedotýká vůbec: žádný archiv
`.dta`, žádné `tables\`, žádné `sounds\`, nic lokalizovaného.

Vše naše je v jediné složce, `<game>\ALXG mods\` - záznam instalace, nastavení řadičky i
silové zpětné vazby. V kořeni hry zůstávají jen tři soubory `.asi` a ASI loader, protože
žádnou jinou složku loader nečte.

Každý zápis se nejprve zaznamená do `<game>\ALXG mods\install.log` a to, co vytlačil, se uloží
vedle do `original\`. Odinstalace jde podle tohoto záznamu, řádek po řádku, takže vrátí přesně
to, co tam bylo. Soubor, který jste si sami upravili, je rozpoznán jako váš, je to řečeno
nahlas a zůstane nedotčen.

### Ruční instalace

Pokud nástroj spouštět nechcete, tytéž soubory jsou v `manual-install\`. Zkopírujte je do
složky hry v rozložení, které tam uvidíte. Pak nemáte žádný záznam ani odinstalaci - soubory
smažete ručně. Oba soubory `.ini` jsou nastavení, kopírujte je jen tehdy, pokud vlastní ještě
nemáte.

## Používání

Za jízdy se zapnutou kamerou z pohledu řidiče:

| klávesa | co dělá |
|---|---|
| F1 / F2 | oči výš / níž, 2 cm na stisk |
| F3 / F4 | oči doleva / doprava |
| F5 / F6 | oči dopředu / dozadu |
| F9 / F10 | bližší ořezová rovina dál / blíž |

Posazení, na kterém se ustálíte, se zapíše zpět do `mafia_fp.ini` a přežije restart. Tytéž
hodnoty jsou na záložce First person spolu s bližší ořezovou rovinou, zámkem horizontu,
náklonem kamery a zorným polem. Kteroukoli z těchto kláves tam lze přemapovat a v `mafia_fp.ini` jsou další,
zatím nepřiřazené.

H-řadička se nastavuje na vlastní záložce: stiskněte tlačítko odpovídající každé kulise a pak
spusťte hru. Změny řadičky se projeví až po restartu Mafie, což záložka na obrazovce říká.

## Kompatibilita

| testováno | výsledek |
|---|---|
| GOG v1.3 build 16073, anglicky | testováno, na tomto sestavení bylo vše měřeno |
| GOG v1.3, ruská verze | nainstalováno a spuštěno na čisté instalaci: bajtově shodný `Game.exe`, všechny tři módy se načetly |
| GOG v1.3, ostatních šest jazyků | identický `Game.exe`, takže se očekává funkčnost, ve hře netestováno |
| Steam a další vydání | netestováno. Nástroj řekne, že sestavení nepoznává |
| ostatní módy | netestováno |

Ve složce hry může být jen jeden `dinput8.dll`. Pokud tam už ASI loader máte, ten stávající se
před nahrazením zazálohuje a při odinstalaci vrátí.

## Známé problémy

- Náraz zezadu je cítit jen stěží. Je to změřené, ne odhadnuté: žádný z našich kanálů jej zatím
  nevidí a povolení prahu nárazu vrací falešné rázy, které byly horší. Je to známá chyba a v
  tomto vydání opravena není.
- Počítadlo poškození v tomto sestavení hry nedává nic použitelného, takže zásahy střelbou přes
  volant cítit nejsou.
- Klávesy kamery jsou v této verzi pouze klávesnicové. Tlačítko volantu na ně zatím přiřadit
  nelze.
- Posuvník zorného pole se projeví až při dalším spuštění Mafie, na rozdíl od všeho ostatního
  na této záložce, co se do běžící hry dostane asi za sekundu.
- Záložka VR nic neinstaluje - ten mód není hotový.
- Nástroj zatím nemá vlastní ikonu.

## Pro vývojáře

Zdrojové kódy jsou otevřené, jak samotné módy, tak instalátor. Sestaveno pomocí LLVM-MinGW pro
32bitové Windows; build skripty jsou v `tools\`. Módy jsou ASI pluginy: `LS3DF.dll` hry
importuje `DINPUT8.dll`, Windows jej vyhledá ve složce hry a přibalený Ultimate ASI Loader
načte každé `.asi` vedle sebe. Odtud každý mód čte vlastní stav vozu na známých adresách
enginu a buď posílá síly do volantu, hýbe kamerou, nebo ovládá převody.

Pull requesty a forky jsou vítány, včetně přenesení postupu na úplně jinou hru.

## Poděkování

- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) od ThirteenAG,
  přibalen jako `dinput8.dll`. Viz `THIRD-PARTY.md`.
- Vše ostatní je vlastní práce tohoto projektu.

## Licence a právní informace

Vlastní kód tohoto projektu je pod licencí MIT. Viz `LICENSE`.

Tento mód vyžaduje legální kopii hry Mafia: The City of Lost Heaven. Žádné herní soubory ani
data nejsou přiložena ani šířena.

Tento projekt není spojen s Take-Two Interactive, 2K ani bývalou Illusion Softworks a není jimi
schválen. Mafia je ochranná známka svých vlastníků. Licence MIT pokrývá pouze vlastní kód
tohoto projektu; hra a její data zůstávají majetkem svých vlastníků.
