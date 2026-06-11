## Release 0.10.0 - 2026-06-11
### Neue Funktionen
- Integrierte Web-Management-Benutzeroberfläche ([#76](https://github.com/unfoldedcircle/ucd3-firmware/pull/76)).
- Statische Netzwerkkonfiguration ([#81](https://github.com/unfoldedcircle/ucd3-firmware/pull/81)).
- Unterstützung für Serielle Bridge (TCP/RS232) ([#72](https://github.com/unfoldedcircle/ucd3-firmware/pull/72)).
- Neuer Log-Router für verbesserte Diagnose ([#77](https://github.com/unfoldedcircle/ucd3-firmware/pull/77)).
- Verbesserte Webserver-Performance und Caching ([#75](https://github.com/unfoldedcircle/ucd3-firmware/pull/75), [#73](https://github.com/unfoldedcircle/ucd3-firmware/pull/73)).

### Fehlerbehebungen
- Allgemeine Fehlerbehebungen und Laufzeitverbesserungen.

### Geändert
- Vereinheitlichung der Firmware-Revisionen 4 und 6 ([#66](https://github.com/unfoldedcircle/ucd3-firmware/pull/66)).

## Release 0.9.1 - 2026-05-18
### Fehlerbehebungen
- IR-Codes mit dem Datenwert 0 zulassen, z. B. die Ziffer "0" bei Philips-Fernsehern, die das RC6-Protokoll verwenden ([bug-tracker#721](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/721)).

## Release 0.9.0 - 2026-04-28
### Fehlerbehebungen
- Verschiedene Stabilitätsverbesserungen und automatisches Trennen von hängenden Client-Verbindungen.
  
### Geändert
- Die am wenigsten aktive Client-Verbindung automatisch schließen, wenn keine Verbindungen mehr verfügbar sind ([#58](https://github.com/unfoldedcircle/ucd3-firmware/pull/58)).
- Nicht authentifizierte Clients nach 30 Sekunden trennen ([#59](https://github.com/unfoldedcircle/ucd3-firmware/pull/59)).
- Die maximale Anzahl an Client-Verbindungen wurde von 7 auf 18 erhöht ([#60](https://github.com/unfoldedcircle/ucd3-firmware/pull/60)).

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
