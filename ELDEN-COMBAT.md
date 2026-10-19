# OpenGothic – walka w stylu Elden Ring

Fork [OpenGothic](https://github.com/Try/OpenGothic) (Gothic II: Noc Kruka) z walką,
ruchem i kamerą w stylu Elden Ringa: kontroler akcji z buforem, combo, ciężkie/ładowane ataki,
uniki z i-frames, blok, stamina, poise, lock-on, pad, animacje Elden Ringa przeniesione na
szkielet Gothica oraz NPC i potwory walczące na tym samym kontrolerze.

**Licencja:** rdzeń walki (`common/eldencombat/`) i adapter pochodzą z forka OpenMW [openmw-eldenring](https://github.com/Smthri/openmw-eldenring) (GPLv3),
a projekt ELDEN-RING-Combat-Rewrite jest na MIT (`common/eldencombat/LICENSE-MIT.txt`).
Całość jest rozpowszechniana na **GPLv3** (`COPYING`); oryginalny OpenGothic jest na MIT (`LICENSE`).

**Brak danych z gry.** Repozytorium nie zawiera żadnych plików z Elden Ringa ani z Gothica.
Timingi akcji (`common/eldencombat/imported_timings.hpp`) i animacje (`player_anims.bin`)
każdy generuje lokalnie z własnej kopii Elden Ringa narzędziami
[ELDEN-RING-Combat-Rewrite](https://github.com/Funny-Bones/ELDEN-RING-Combat-Rewrite)
(`tools/extract.py`, `tools/bake_anims.py`, skrypt importu z forka OpenMW).
Bez nich gra działa na zastępczych timingach i natywnych animacjach Gothica.

## Uruchamianie

Pad: najlepiej uruchamiać przez Steam (np. jako „grę spoza Steama”), wtedy Steam Input
udostępnia każdy kontroler, także Steam Controller, jako pad XInput.
Parametry: `-eldencombat` (włącza walkę ER), `-eldenanims <ścieżka do player_anims.bin>`,
`-eldendebug` (log akcji w `log.txt`).

```
build-elden.bat      – budowanie (VS 2022 Build Tools, Ninja, Vulkan SDK)
play-elden.bat       – gra z walką Elden Ring (-eldencombat), ignoruje parametry Steama
```

Bez flagi `-eldencombat` gra działa jak vanilla. **F9** w grze przełącza tryb
(Elden ↔ vanilla) w locie.

## Animacje z Elden Ringa

Akcje bojowe (ataki, ładowanie, uniki/przewroty, backstep, blok i trafienie w blok,
reakcje na trafienie, przełamanie gardy) są pokazywane **animacjami z Elden Ringa**
z `player_anims.bin` (Twój lokalny bake, ścieżka w `OPENGOTHIC_ELDEN_ANIMS`, ustawiana
w `play-elden.bat`). `common/game/eldenretarget.*` przenosi je na szkielet Bip01 Gothica
(port retargetingu z forka OpenMW: ramki anatomiczne z pozycji stawów, kalibracja
względem pozy spoczynkowej Gothica, płaszczyzny zgięcia łokci/kolan, skala z długości nóg).
Poza ER jest nakładana na kości (`Pose::setBoneOverride`) z przenikaniem 0,15 s.
Jak w forku OpenMW animacje ER obejmują też ruch: postawę, chód, bieg i skradanie
(pętle kierunkowe, krok zsynchronizowany z faktyczną prędkością) oraz sprint (Gothic nie ma
sprintu – jest pętla sprintu ER i ×1,35–1,5 prędkości). Natywne zostają: skok/upadek, pływanie,
dialogi, interakcje z obiektami, dobywanie broni, łuk/kusza/magia i śmierć.
Przewrót, backstep i sprint działają też bez dobytej broni (bez ataków i bloku). Bez pliku banku gra używa natywnych animacji Gothica z tabeli niżej.

## NPC i humanoidalne potwory

Każdy humanoid dostaje własny kontroler ER. O tym decyduje szkielet (`EldenRetarget::isHumanoid`):
pełny dwunożny Bip01, wyprostowana postawa, stopy pod miednicą, wysokość do 2,6 m.
W praktyce: ludzie, szkielety, orki, gobliny, zombie, kamienni strażnicy, golemy.
Pozostałe potwory (wilk, cieniostwór, kretoszczur, dzik, troll, owady...) mają **animacje
Gothica, ale logikę ER**: kontroler w trybie natywnym bierze czas ataku i okno trafienia z ich
własnej animacji `S_FISTATTACK` (optymalna klatka), pilnuje przerw między seriami, odskok
(MV_JUMPBACK) daje i-frames; ruch i animacje zostają natywne. Decyzja dla każdego modelu jest
w logu (`elden: skeleton '...' -> ...`).

**Poise wrogów** (NPC i potwory, jak w ER): z siły, poziomu, HP i pancerza (`poiseFromStats`,
maks. 45). Dopóki poise trzyma, cios zadaje obrażenia Gothica, ale nie przerywa akcji wroga
(bez natywnego zachwiania); po wyczerpaniu – zachwianie, po czasie poise się odnawia.
Gracz nie ma poise (każde trafienie go zachwiewa, jak w ER bez ciężkiej zbroi).
AI Gothica dalej decyduje, *co* robi (atak, parada, odskok – skrypty, zasięgi, taktyki),
a kontroler ER *wykonuje* to po elden-ringowemu: `MV_ATTACK` → lekki/ciężki atak i combo do 3
ciosów (`AiTactics` z forka OpenMW, przerwa między seriami), `MV_BLOCK` → trzymany blok,
`MV_JUMPBACK` → backstep z i-frames. NPC walczący z graczem czasem reagują na jego cios
unikiem w bok, backstepem albo blokiem. Trafienia NPC liczą się z okien trafień ER, obrażenia
dalej z formuły Gothica; unik/blok gracza i NPC działa w obie strony.
Animacje ER (walka, postawa, chód, bieg) dostają NPC w promieniu 30 m od gracza; gesty,
rutyny dnia, siedzenie itp. zostają natywne.

## Co robi

Gdy gracz ma dobytą broń białą albo pięści, decyzje o akcjach podejmuje kontroler
z Elden Ringa (`EldenCombat::Controller`), a silnik odtwarza **natywne animacje Gothica**
na zegarze tego kontrolera (`common/game/eldenplayer.cpp`):

| Akcja ER | Animacja Gothica |
|---|---|
| lekkie combo 1–6 | kolejne etapy `S_1HATTACK`/`S_2HATTACK`/`S_FISTATTACK` (przy jednym etapie – przeplot z `T_xxATTACKL/R`) |
| ciężki / ładowany | ostatni (najszerszy) etap combo, drugi ciężki – `T_xxATTACKMOVE` |
| atak z biegu/przewrotu/skoku | `T_xxATTACKMOVE` |
| kontra po bloku | `T_xxATTACKL` |
| blok (trzymany) | `T_xxPARADE_0` zatrzymane w oknie obrony; trafienie w blok – `T_xxPARADE_0_A2` |
| backstep | `T_xxPARADEJUMPB` |
| unik w bok / w tył / w przód | `T_xxRUNSTRAFEL/R` / `T_xxJUMPB` / `T_STUMBLEB` (Gothic nie ma przewrotu – etap 2) |
| przełamanie gardy | `T_STUMBLE` |

* Okna wejścia, bufor jednej akcji, anulowania, klatki nietykalności, koszty i regeneracja
  staminy, sprint (przytrzymany unik), blok z opóźnieniem i kontra – z importowanych
  timingów ER (1080 akcji). Optymalna klatka trafienia Gothica jest wyrównana do okna
  trafienia ER, reszta animacji jest rozciągana do długości akcji ER.
* Przemieszczenie przy unikach/atakach – z root motion ER (natywne root motion animacji
  jest wtedy wyłączone), z kolizją.
* **Obrażenia zostają gothicowe**: `DamageCalculator` (siła, talent/szansa krytyczna,
  obrażenia broni, ochrona celu). Kontroler decyduje tylko *kiedy* i *czy* cios trafia.
  Mnożnik: ciężki ×1,5, w pełni naładowany ×1,25 dodatkowo, kontra ×1,3.
* Trafiane są: cel z focusa/lock-onu i NPC, którzy walczą z graczem (bez cięcia przechodniów).
* Obrona gracza: unik (i-frames) i blok rozstrzyga kontroler ER – zablokowany cios nie
  zadaje obrażeń, przy braku staminy gardę łamie; zwykłe trafienie idzie natywną ścieżką
  (obrażenia + natywne zachwianie). Nietykalność uniku działa też na czary.
* Stamina: żółty pasek na dole ekranu; poza walką sprint jest darmowy.
* Kamera jak w Elden Ringu: mysz/prawa gałka obracają tylko kamerę, postać biegnie przodem
  w stronę wskazaną względem kamery (8 i więcej kierunków, „do tyłu” = zawraca i biegnie do kamery).
  Uniki idą tam, gdzie wskazuje gałka na ekranie. Łuk, kusza i magia mają klasyczną kamerę.
* Lock-on: Q / R3 – biała kropka na celu, kamera skupiona na celu, postać zwrócona do celu
  i porusza się klasycznie (przód, tył, strafe). Bieg przodem do kamery tylko bez celu. Ruch myszą/prawą gałką w bok przełącza cel na
  sąsiedniego na ekranie. Zwalnia się przy śmierci/omdleniu celu lub > 25 m.
* Ciosy gracza zakreślają szeroki łuk (±70°) i trafiają kilku przeciwników naraz: cel, każdego,
  kto walczy z graczem, i wrogie potwory w zasięgu broni.
* Chwyt jak w Elden Ringu: każdą broń białą (także dwuręczną) można trzymać w jednej ręce
  albo oburącz (G / Y+RB, tylko poza akcją). Nowa broń startuje w swoim naturalnym chwycie.
  Oburęczny chwyt broni jednoręcznej: obrażenia ×1,25; broń dwuręczna oburącz ×1,0,
  w jednej ręce ×0,75. Chwyt wybiera też zestaw animacji i timingów ER (one-/two-handed).
* Dobijanie ogłuszonych (finishing move) zostaje natywne: LPM/RB na leżącym celu.
* Łuk, kusza i magia działają po staremu.

## Sterowanie

| Akcja | Klawiatura/mysz | Pad (XInput) |
|---|---|---|
| lekki atak | LPM | RB |
| ciężki atak (przytrzymaj = ładowanie) | Shift + LPM | RT |
| blok | PPM (trzymany) | LB (trzymany) |
| unik / przytrzymany sprint | Spacja | B |
| skok | F | A |
| interakcja | E | Y |
| lock-on | Q | R3 |
| chwyt jednoręczny ↔ oburęczny | G | Y + RB |
| skradanie | (klawisz Gothica) | L3 |
| dobycie/schowanie broni | R (oraz 1/2 jak w Gothicu) | X |
| ruch | WASD | lewa gałka |
| kamera / obrót | mysz | prawa gałka |
| ekwipunek / menu | Tab / Esc | Back / Start |
| mikstura HP / many | (klawisze Gothica) | krzyżak góra / dół |
| łuk / broń biała | 2 / 1 | krzyżak lewo / prawo |
| przełącz Elden ↔ vanilla | F9 | – |

W menu, dialogach i ekwipunku pad nawiguje: krzyżak/gałka = strzałki, A = Enter,
B/Start = Esc, X w ekwipunku/skrzyni = „weź”. Pad wysyła wtedy prawdziwe klawisze do okna,
więc działa też w oknach modalnych (nazwa zapisu). Start w grze = dokładnie Esc. W trybie Elden Shift nie przełącza chodu.
W trybie vanilla pad ma prosty układ: RB/RT akcja, LB parada, A skok, Y akcja, X broń.

## Debug

`set OPENGOTHIC_ELDEN_DEBUG=1` – w `build\opengothic\log.txt` każda akcja kontrolera
z wybraną animacją Gothica, klipem ER i odcinkiem czasu; `OPENGOTHIC_ELDEN_TRACE=1` loguje
klatki pozy ER.

## Zmienione pliki

`common/eldencombat/*` (skopiowany rdzeń), `common/game/eldenplayer.*` (adapter),
`common/game/eldenretarget.*` (retargeting ERAN → Bip01),
`common/utils/gamepad.*` (XInput), `common/game/playercontrol.*`, `common/mainwindow.*`,
`common/world/objects/npc.*`, `common/graphics/mdlvisual.*`, `common/graphics/mesh/pose.*`
(animacja na zewnętrznym zegarze), `common/commandline.*`, `CMakeLists.txt`.
