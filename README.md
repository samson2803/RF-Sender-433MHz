# RF-Sender 433 MHz

Ein ESP8266 (NodeMCU) als **433-MHz-Funksender mit Weboberfläche**. Er ersetzt eine
einfache Funkfernbedienung (z. B. für Funksteckdosen) und schaltet ein Gerät
in einem einstellbaren Intervall ein und aus. Codes, Zeiten und Funkparameter
werden bequem im Browser eingestellt, ohne neu zu flashen.

## Funktionen

- **Intervall-Betrieb:** abwechselnd Code AN und Code AUS senden, AN- und AUS-Dauer getrennt einstellbar
- **Start / Stop**, wobei Stop zusätzlich einmal AUS sendet, damit das Gerät sicher aus ist
- **Einzeln senden:** Code AN, Code AUS oder ein beliebiger Code
- **Einstellbar:** Codes (mit Hex-Anzeige), Bits, Protokoll, Pulslänge, Wiederholungen
- **Autostart** des Intervalls nach dem Einschalten bzw. nach einem Stromausfall
- **Live-Status** mit Countdown bis zum nächsten Wechsel
- **Einstellungen bleiben gespeichert** (EEPROM) und überstehen Neustarts
- **WLAN-Einrichtung im Browser:**
  - Ohne eingerichtetes WLAN startet ein **offener Accesspoint** `RF-Sender` mit Captive Portal (http://10.4.33.1)
  - Danach verbindet sich der ESP mit dem Heim-WLAN und ist unter **http://rf-sender.local** erreichbar
  - Ist das WLAN beim Start nicht erreichbar oder länger als 30 s weg, öffnet sich der Accesspoint automatisch wieder
  - **FLASH-Taste 5 s halten** löscht den WLAN-Zugang

## Hardware

- NodeMCU mit ESP8266 (ESP-12E/ESP-12F)
- 433-MHz-Sender **FS1000A**
- Optional zum Auslesen der Codes: 433-MHz-Empfänger **XY-MK-5V**
- 17,3 cm Draht als Antenne (Viertel-Wellenlänge bei 433 MHz)

### Anschluss Sender

| FS1000A | NodeMCU |
|---|---|
| VCC | VIN (5 V vom USB) |
| GND | G |
| DATA (ATAD) | D4 |
| ANT | 17,3 cm Draht |

Der FS1000A darf an 5 V betrieben werden, da sein DATA-Pin nur ein Eingang ist.
Das bringt mehr Reichweite als 3,3 V.

> **Hinweis zu D4:** D4 (GPIO2) ist ein Boot-Pin und steuert die blaue LED,
> die beim Senden flackert. Falls der ESP nicht startet, beim Einschalten kurz
> die DATA-Leitung abziehen oder den Sender auf D1 legen und `TX_PIN` anpassen.

## Installation

1. Arduino IDE mit ESP8266-Boardpaket installieren
   (Boardverwalter-URL: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`)
2. Bibliothek **rc-switch** (von sui77) über den Bibliotheksverwalter installieren
3. Board **„NodeMCU 1.0 (ESP-12E Module)“** wählen
4. `RF_Sender_WebUI.ino` öffnen und hochladen

Oder mit `arduino-cli`:

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 -u -p COM4 RF_Sender_WebUI
```

## Bedienung

1. Mit dem WLAN **RF-Sender** verbinden (offen, kein Passwort)
2. Die Seite öffnet sich meist automatisch, sonst http://10.4.33.1 aufrufen
3. Unter **Einstellungen** die eigenen Codes eintragen und speichern
4. Optional unter **WLAN** das Heim-WLAN auswählen, Passwort eingeben und speichern.
   Der ESP startet neu und ist danach unter http://rf-sender.local bzw. der IP
   aus dem Router erreichbar.

Die im Sketch hinterlegten Codes sind **Beispielwerte**.

## Eigene Codes auslesen

Den Empfänger z. B. an einen Arduino Nano anschließen (VCC → 5V, GND → GND,
DATA → D2) und das Beispiel `ReceiveDemo_Advanced` aus der rc-switch-Bibliothek
hochladen. Beim Drücken der Fernbedienung erscheinen im seriellen Monitor
(9600 Baud) Code, Bitlänge, Protokoll und Pulslänge:

```
Decimal: 1193041 (24Bit) Binary: ... PulseLength: 360 microseconds Protocol: 1
```

Diese Werte in der WebUI eintragen. Bei Fernbedienungen mit EV1527-Chip ist
die letzte Hex-Stelle die Taste (1, 2, 4, 8), der Rest die feste Adresse.

Fernbedienungen mit **Rolling Code** (z. B. KeeLoq/HCS301, Autoschlüssel,
moderne Garagentore) lassen sich damit nicht nachbilden.

## Hinweise

- Der offene Accesspoint ist für jeden in Funkreichweite zugänglich. Das gilt
  auch, wenn er bei fehlendem WLAN automatisch wieder geöffnet wird.
- Auf 433 MHz gilt in der EU eine maximale Sendedauer von etwa 10 % der Zeit.
  Mit den Standardwerten (8 Wiederholungen, Intervalle ab einigen Sekunden)
  bleibt der Sender deutlich darunter.

## Lizenz

[MIT](LICENSE)
