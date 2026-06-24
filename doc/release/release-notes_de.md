## Release 0.10.3 - 2026-06-24
Änderungen seit der letzten öffentlichen Version 0.8.2.

### Fehlerbehebungen
- Allgemeine Stabilitätsverbesserungen und automatische Bereinigung hängender Client-Verbindungen.
- Unterstützung für IR-Codes mit dem Datenwert `0` korrigiert, z. B. für die Ziffer „0“ bei Philips-Fernsehern mit RC6-Protokoll ([bug-tracker#721](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/721)).
- Zuverlässigkeit der Ethernet-Verbindung verbessert ([#89](https://github.com/unfoldedcircle/ucd3-firmware/pull/89)).
- UART-Einstellungen können jetzt ohne Neustart des Geräts geändert werden ([#84](https://github.com/unfoldedcircle/ucd3-firmware/pull/84)).

### Neue Funktionen
- Integrierte Web-Verwaltungsoberfläche hinzugefügt, mit der das Gerät über einen Browser konfiguriert und verwaltet werden kann ([#76](https://github.com/unfoldedcircle/ucd3-firmware/pull/76)).
- Unterstützung für statische Netzwerkkonfiguration hinzugefügt, um IP-Adressen manuell festlegen zu können ([#81](https://github.com/unfoldedcircle/ucd3-firmware/pull/81)).
- RS232-Unterstützung mit optionaler TCP-Serial-Bridge hinzugefügt ([#72](https://github.com/unfoldedcircle/ucd3-firmware/pull/72)).
  - Bei aktuellen Geräten ist der RS232-Modus auf Port 2 beschränkt. Port 1 kann beim Start Systemausgaben senden, die von manchen angeschlossenen RS232-Geräten als Befehle interpretiert werden könnten.
  - UART-TTL-Adapter werden aufgrund unterschiedlicher Signalpegel nicht unterstützt.
- Neuer Log-Router hinzugefügt, um Diagnose und Fehlersuche zu verbessern ([#77](https://github.com/unfoldedcircle/ucd3-firmware/pull/77)).
- Unterstützung für das Lernen und Senden von RAW-IR-Befehlen hinzugefügt ([#85](https://github.com/unfoldedcircle/ucd3-firmware/pull/85), [#87](https://github.com/unfoldedcircle/ucd3-firmware/pull/87)).

### Geändert
- Wenn die maximale Anzahl an Client-Verbindungen erreicht ist, wird jetzt automatisch die am wenigsten aktive Verbindung geschlossen, damit eine neue Verbindung hergestellt werden kann ([#58](https://github.com/unfoldedcircle/ucd3-firmware/pull/58)).
- Nicht authentifizierte Clients werden jetzt nach 30 Sekunden getrennt ([#59](https://github.com/unfoldedcircle/ucd3-firmware/pull/59)).
- Die maximale Anzahl an Client-Verbindungen wurde von 7 auf 18 erhöht ([#60](https://github.com/unfoldedcircle/ucd3-firmware/pull/60)).
- Unterstützung für die Firmware-Revisionen 4 und 6 vereinheitlicht ([#66](https://github.com/unfoldedcircle/ucd3-firmware/pull/66)).

## Beta Release 0.8.2 - 2026-02-14
### Fehlerbehebungen
- Korrekter Anfangszustand für IR-LEDs, damit diese nicht leuchten bis ein IR-Befehl gesendet wird ([#48](https://github.com/unfoldedcircle/ucd3-firmware/pull/48)).
- Allgemeine Fehlerbehebungen und Verbesserungen.

### Geändert
- SNTP-Initialisierung, keine Anzeige eines ungültigen Datums auf der Status-Webseite. Beigetragen von @Tosko4, danke! ([#46](https://github.com/unfoldedcircle/ucd3-firmware/pull/46)).

## Beta Release 0.8.1 - 2025-11-03
### Geändert
- Anhebung der Überstrombegrenzung auf 2000mA.

## Beta Release 0.8.0 - 2025-09-23
### Fehlerbehebungen
- Die Verzögerungsfunktion für das Senden von IR-Codes war bei Verzögerungen von mehr als 16 ms ungenau, was bei bestimmten IR-Protokollen und nativen IR-Wiederholungen zu Problemen führte ([bug-tracker#484](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/484)).
- Aktive IR-Wiederholung stoppen, wenn der WebSocket-Client die Verbindung trennt.
- Allgemeine Stabilitätsverbesserungen.

### Neue Funktionen
- Status-LED-Muster für Dock-Einrichtung, IR-Lernen und OTA.

### Geändert
- Nicht in den WiFi-Einrichtungsmodus wechseln, wenn die Einrichtung der Verbindung fehlgeschlagen ist ([bug-tracker#612](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/612))

## Beta Release 0.7.1 - 2025-08-28
### Geändert
- Anhebung der Überstrombegrenzung auf 1850mA ([#30](https://github.com/unfoldedcircle/ucd3-firmware/pull/30)).

## Beta Release 0.7.0 - 2025-07-17
### Fehlerbehebungen
- Automatische Erkennung von zwei IR-Blaster.
- Fehleranzeige auf dem Bildschirm bei Ladeabschaltung aufgrund von Überstromerkennung ([#bug-tracker501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- PRONTO-Code-Parsing mit nachgestellten Leerzeichen ([bug-tracker#495](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/495)).
- Einrichtung mit einem benutzerdefinierten Passwort auf Fernbedienungen mit einer Firmware-Version <= 2.6.1 ([bug-tracker#489](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/489)).
- Rückgabe des korrekten Netzwerkverbindungstyps (Ethernet oder WiFi).
- Anzeige der korrekten Ladeinformationen im Info-Bildschirm beim Drücken der Steuerungstaste.
- Allgemeine Stabilitätsverbesserungen.

### Neue Funktionen
- Überprüfung des Ladegeräts auf Unterspannung.

### Geändert
- Verbesserung der Ladestrommessung und der Überstromerkennung zur Vermeidung von Ladeabschaltungen ([bug-tracker#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- Bestimmung des Ladespannungs-Offset beim Start, um ein Umschalten zwischen den Lade- und Nichtladebildschirmen zu verhindern.

## Beta Release 0.6.0 - 2025-06-02
### Fehlerbehebungen
- Die externen Ports 1 und 2 wurden gemäß Handbuch getauscht: Port 1 befindet sich neben dem Ethernet-Port.
- Startup-Crash beim Netzwerk-Check, wenn andere Initialisierungen länger dauern als erwartet.
- Crash beim Durchlaufen der Infobildschirme mit dem Dock-Button.
- Sendezustand des IR-Senders an den Client weitergeben, z. B. wenn das Senden nicht möglich ist, wenn das IR-Lernen aktiv ist.

### Neue Funktionen
- Automatische Erkennung von IR-Blaster und -Sender.
- Anzeige des gelernten IR-Protokollnamens auf dem Display.

### Geändert
- Verbesserte automatische Erkennung externer IR-Peripheriegeräte, einschließlich des Dock Two IR-Emitters mit Mono-Stecker.
- Informationsbildschirm: Reihenfolge und Layout, Zusammenfassung der Netzwerkinformationen auf einem Bildschirm.
