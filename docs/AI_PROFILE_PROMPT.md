# Arbeitsanweisung zur Profilerstellung

Erstelle ein Sensorprofil nach PROFILE_FORMAT.md und profile-template.json.
Verwende nur die dort dokumentierten generischen Operationen. Keine Treibernamen,
Firmware-Funktionen oder neuen Operationen erfinden. Die vier SD-Profile dienen
als geprüfte Syntaxbeispiele, ihre Hardwarevalidierung steht noch aus.

Eingaben: genaues Sensormodell, Datenblattversion/Link, Breakout-Schaltplan,
Core2-Verdrahtung, gewünschte Messgrößen und Messrate.

1. Versorgung und Logikpegel getrennt prüfen. Fehlende Angaben als offen melden;
   Versorgung wird nicht durch JSON geschaltet.
2. Busadresse/Registerbreite, Protokoll, Bytefolge und signedness belegen.
3. detect ausschließlich lesend formulieren, Chip-ID prüfen falls vorhanden.
   Adressantwort ohne Modell-ID ausdrücklich als unsicher dokumentieren.
4. Vollständige Start-/Initialisierungssequenz mit Hersteller-Konfiguration,
   Wartezeiten und zeitlich begrenzten Bereitschaftsabfragen erstellen.
5. Messsequenz mit Fehlerstatus, Kalibrierregistern, Umrechnung und Einheiten
   beschreiben. Alle Messvariablen werden pro Durchlauf zurückgesetzt.
6. stop auch nach teilweise fehlgeschlagener Initialisierung ausführbar machen.
7. Quellen und abgeleitete Lizenzhinweise festhalten. Nie Registerwerte raten.
8. Fehlende Protokolloperationen melden, statt sie durch erfundene Befehle oder
   weggelassene Prüfschritte zu ersetzen.
9. Profil gegen Engine validieren, mit Busfixtures testen und am Sensor mit
   Referenzwerten prüfen. Erst dann als hardwarevalidiert kennzeichnen.

Korrekturen und Testergebnisse je Profil versionieren. Daraus entsteht eine
wachsende Wissensbasis; dies ist kein autonomes Lernen und kein automatischer
Nachweis, dass KI-generierte Registerzugriffe korrekt sind.
