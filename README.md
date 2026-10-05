# LIRA-8 für Akai Force / MPC

Repo: mpc-vst-lira8

Drone-Synthesizer nach dem Vorbild des SOMA Lyra-8 als VST2-**Instrument** für den Plugin-Host
von MPC OS (Force, MPC Live/One/X/Key). Nativer C++-Nachbau des Pure-Data-Patches
[LIRA-8](https://github.com/MikeMorenoDSP/LIRA-8) von Mike Moreno (BSD-Lizenz). Kein Pure Data,
kein Camomile, keine Abhängigkeiten.

**Stand:** vollständiges Repo (Klang, Plugin-Hülle, Skin, Build, Workflow). Offline getestet;
der Build über GitHub Actions und der Test auf der Force stehen noch aus.

## Bauen und installieren

GitHub → Actions → „VST release (draft)“ → Run workflow (Version z. B. `0.1.0`, `dry_run` an
für einen reinen Build). Der Workflow baut `lira8.so` für armhf, erzeugt den Skin und lässt
`vst/test.sh` laufen. Installation wie bei Rattler und carp 2000 (ZIP aus dem Lauf, Ordner nach
`/tmp` kopieren, `sh install.sh`). **Das Installationsskript stoppt und startet MPC neu, vorher
Projekt speichern.**

## Aufbau

```
vst/lira_core.h     Klangerzeugung (der Pd-Patch in C++), keine Abhängigkeiten
vst/lira_vst.cpp    Plugin-Hülle: Parameter, MIDI, 32 Speicherplätze, Projekt-Chunk
vst/module.json     Parameterliste = VST-Index-Reihenfolge (nur anhängen!)
vst/layout.conf     Skin: 8 Seiten im Raster von Rattler / carp 2000
vst/vst.json        Name, UID, .so
vst/build.sh        Build (ruft die Werkzeuge von sd88me/mpc-vst-plugins)
vst/test.sh         Host-Test (ASan/UBSan) + CPU-Messung
vst/host_test.cpp   der Test selbst
```

## Seiten

| Seite | Regler 1–4 | Regler 5–8 |
| --- | --- | --- |
| TUNE | Tune 1–4 | Tune 5–8 |
| 1234 | Sharp 12, Mod 12, Source 12, Fast 12 | Sharp 34, Mod 34, Source 34, Fast 34 |
| 5678 | Sharp 56, Mod 56, Source 56, Fast 56 | Sharp 78, Mod 78, Source 78, Fast 78 |
| MAIN | Pitch 1234, Hold 1234, Pitch 5678, Hold 5678 | Switch, Total FB, Vibrato, Quantize |
| LFO | LFO Freq A, LFO Freq B, AND/OR, Link | Drive, Dist Mix, Volume |
| DELAY | Time 1, Time 2, Feedback, Mix | Mod 1, Mod 2, Mod Source, LFO Wave |
| SENSOR | Sensor 1–4 | Sensor 5–8 |
| PRESET | LOAD, Preset, SAVE | |

## Was drin ist

Objekt für Objekt aus dem Patch übertragen (`vst/lira_core.h`):

- **8 Stimmen in 4 Paaren** (12, 34, 56, 78): PolyBLEP-Puls und daraus gefiltertes „Dreieck“,
  SHARP blendet zwischen beiden. Stimmbereiche pro Stimme wie im Patch.
- **Sensoren**: Anschlag/Ausklang 100/100 ms (FAST) oder 200/8000 ms, mit dem kurzen
  „Touch“-Impuls des Originals. HOLD lässt eine Vierergruppe dauerhaft klingen.
- **Kreuz-FM**: jedes Paar wird vom Nachbarpaar (SWITCH tauscht die Quellen), vom Hyper-LFO
  oder – mit TOTAL FB – vom Gesamtausgang moduliert. Auch das 0,1-%-Übersprechen bei OFF ist
  übernommen.
- **Hyper-LFO**: zwei Rechteck-LFOs, AND/OR, LINK (A moduliert B).
- **Doppel-Delay**: 1,45 ms bis 5,9 s, Feedback bis zur Selbstoszillation, Kompressor und
  Expander in der Schleife, Modulation durch LFO (Dreieck/Rechteck) oder sich selbst.
- **Distortion**, Volume, VIBRATO, QUANTIZE (Halbtonraster).

Ausgang wie im Original mono (auf beiden Kanälen).

## Spielen

Noten **C1 bis G1 (36–43)** = Sensoren 1–8, passend zu den Pads. Jede andere Oktave
funktioniert genauso (Note modulo 8). Zusätzlich zum Original gibt es acht Schalter
„Sensor 1–8“ (Parameter 47–54): damit lässt sich ein Drone ohne gehaltene Note einrasten
und im Projekt speichern.

## Abweichungen vom Patch

- Parameter laufen stufenlos statt in 128 Schritten; Glättung statt 23-ms-Rampen.
- Die Rückkopplungen (Kreuz-FM, Total FB) laufen mit 1 Sample Verzögerung statt einem
  64-Sample-Block von Pd. Extreme FM-Einstellungen können dadurch etwas anders kippen.
- Ausgangsschutz oberhalb von -3 dBFS (Decke 0,98) wie bei Rattler.
- Im Plugin-Menü heißt es „LIRA 8“ (Leerzeichen statt Bindestrich, wegen des Skin-Ordnernamens).

## Speicherplätze

32 Plätze mit LOAD/SAVE nach dem Muster von Acid und Rattler: gemeinsam für alle Instanzen und
Projekte, Datei `lira8_presets.txt` neben dem Plugin-Ordner, auch in der Preset-Liste der Force.
Plätze 1–8 haben Werksklänge, solange dort nichts gespeichert ist: Init, Deep Drone, FM Swarm,
Hyper LFO, Ghost Delay, Total Feedback, Organ Cluster, Metal Pulse. Sie sind nach Zahlen gebaut,
nicht nach Gehör abgestimmt.

## Test

`vst/test.sh` (läuft im Workflow nach dem Build) prüft: Stille ohne Sensor, Note und Ausklang
(Fast und 8 s), jede Oktave trifft die Sensoren, Tonhöhe gegen die Formel des Patches, Pitch,
Quantize, samplegenauer Notenstart, Hold, Sensor-Schalter, Sharp, Kreuz-FM, LFO-FM,
Delay-Fahne, alles auf Maximum (bleibt unter der Decke), Wertanzeige, Speicherplätze (Werksklänge,
SAVE/LOAD, zweite Instanz sieht den Platz, leerer Platz ändert nichts), Projekt-Wiederherstellung,
300 Zufallsschritte. Benchmark auf einem PC-Kern: ca. 0,8 % mit allen 8 Stimmen, FM, Delay und
Distortion.

## Plugin-Daten

| | |
| --- | --- |
| Name | LIRA 8 |
| Hersteller | lunarscraper |
| UID | `Lir8` (hex `4c697238`) |
| Typ | Instrument, 0 Ein- / 2 Ausgänge, MIDI in |
| Zustand | Chunk (Text, `schlüssel=wert;`), wird nach Schlüssel wiederhergestellt |

## Parameter

Reihenfolge wie in `vst/module.json`. Nur anhängen, nie umsortieren. 0–46 entsprechen der
Parameterliste des Originals; danach folgen Preset, Load und Save.

| Nr. | Name | Bereich | Standard |
| --- | --- | --- | --- |
| 0 | Fast 12 | OFF / ON | ON |
| 1 | Fast 34 | OFF / ON | ON |
| 2 | Fast 56 | OFF / ON | ON |
| 3 | Fast 78 | OFF / ON | ON |
| 4 | Tune 1 | 0–127 | 32 |
| 5 | Tune 2 | 0–127 | 64 |
| 6 | Tune 3 | 0–127 | 32 |
| 7 | Tune 4 | 0–127 | 64 |
| 8 | Tune 5 | 0–127 | 32 |
| 9 | Tune 6 | 0–127 | 64 |
| 10 | Tune 7 | 0–127 | 32 |
| 11 | Tune 8 | 0–127 | 64 |
| 12 | Sharp 12 | 0–127 | 0 |
| 13 | Sharp 34 | 0–127 | 0 |
| 14 | Sharp 56 | 0–127 | 0 |
| 15 | Sharp 78 | 0–127 | 0 |
| 16 | Mod 12 | 0–127 | 0 |
| 17 | Mod 34 | 0–127 | 0 |
| 18 | Mod 56 | 0–127 | 0 |
| 19 | Mod 78 | 0–127 | 0 |
| 20 | Source 12 | 34 / OFF / LFO | OFF |
| 21 | Source 34 | 12 / OFF / LFO | OFF |
| 22 | Source 56 | 78 / OFF / LFO | OFF |
| 23 | Source 78 | 56 / OFF / LFO | OFF |
| 24 | Pitch 1234 | 0–127 | 64 |
| 25 | Pitch 5678 | 0–127 | 64 |
| 26 | Hold 1234 | 0–127 | 0 |
| 27 | Hold 5678 | 0–127 | 0 |
| 28 | Switch | OFF / ON | OFF |
| 29 | Total FB | OFF / ON | OFF |
| 30 | Vibrato | OFF / ON | OFF |
| 31 | LFO Freq A | 0–127 | 64 |
| 32 | LFO Freq B | 0–127 | 64 |
| 33 | LFO AND/OR | AND / OR | AND |
| 34 | LFO Link | OFF / ON | OFF |
| 35 | Delay Time 1 | 0–127 | 64 |
| 36 | Delay Time 2 | 0–127 | 64 |
| 37 | Delay Feedback | 0–127 | 64 |
| 38 | Delay Mix | 0–127 | 0 |
| 39 | Delay Mod 1 | 0–127 | 0 |
| 40 | Delay Mod 2 | 0–127 | 0 |
| 41 | Delay Mod Source | SELF / OFF / LFO | LFO |
| 42 | Delay LFO Wave | TRI / SQR | TRI |
| 43 | Dist Drive | 0–127 | 64 |
| 44 | Dist Mix | 0–127 | 64 |
| 45 | Volume | 0–127 | 127 |
| 46 | Quantize | OFF / ON | OFF |
| 47 | Sensor 1 | OFF / ON | OFF |
| 48 | Sensor 2 | OFF / ON | OFF |
| 49 | Sensor 3 | OFF / ON | OFF |
| 50 | Sensor 4 | OFF / ON | OFF |
| 51 | Sensor 5 | OFF / ON | OFF |
| 52 | Sensor 6 | OFF / ON | OFF |
| 53 | Sensor 7 | OFF / ON | OFF |
| 54 | Sensor 8 | OFF / ON | OFF |
Anzeige: Tune in Hz (inklusive Pitch und Quantize), LFO in Hz, Delay-Zeiten in ms oder s,
Pitch als Faktor.

## Offen

- Erster Lauf des Workflows und Test auf der Force (Liste → einfügen → spielen → Skin →
  Regler → Projekt speichern und laden).
- Werksklänge nach Gehör nachziehen.

## Lizenz und Dank

BSD-3-Clause, siehe `LICENSE`. Der zugrunde liegende Patch: LIRA-8 © Miguel Moreno
(`LICENSE-LIRA-8-original.txt`). „Lyra-8“ bezeichnet das Instrument von SOMA Laboratory,
dessen Signalfluss nachempfunden ist; keine Verbindung zu SOMA.

Entwickelt mit Unterstützung von Claude (Anthropic)
