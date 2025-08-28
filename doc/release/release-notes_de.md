## Beta Release 0.7.1
### Geändert
- Anhebung der Überstrombegrenzung auf 1850mA ([#30](https://github.com/unfoldedcircle/ucd3-firmware/pull/30)).

## Beta Release 0.7.0
### Fehlerbehebungen
- Automatische Erkennung von zwei IR-Blaster.
- Fehleranzeige auf dem Bildschirm bei Ladeabschaltung aufgrund von Überstromerkennung ([#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- PRONTO-Code-Parsing mit nachgestellten Leerzeichen ([#495](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/495)).
- Einrichtung mit einem benutzerdefinierten Passwort auf Fernbedienungen mit einer Firmware-Version <= 2.6.1 ([#489](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/489)).
- Rückgabe des korrekten Netzwerkverbindungstyps (Ethernet oder WiFi).
- Anzeige der korrekten Ladeinformationen im Info-Bildschirm beim Drücken der Steuerungstaste.
- Allgemeine Stabilitätsverbesserungen.

### Neue Funktionen
- Überprüfung des Ladegeräts auf Unterspannung.

### Geändert
- Verbesserung der Ladestrommessung und der Überstromerkennung zur Vermeidung von Ladeabschaltungen ([#501](https://github.com/unfoldedcircle/feature-and-bug-tracker/issues/501)).
- Bestimmung des Ladespannungs-Offset beim Start, um ein Umschalten zwischen den Lade- und Nichtladebildschirmen zu verhindern.

## Beta Release 0.6.0
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
