# Crédits & licences des composants tiers

Ce projet s'appuie sur du travail existant. Merci à leurs auteurs.

## Pilote de l'écran e-paper
Les fichiers `src/EPD.cpp`, `src/EPD.h`, `src/EPD_Init.cpp`, `src/EPD_Init.h`,
`src/spi.cpp`, `src/spi.h`, `src/EPDfont.h`, `src/icons.h` proviennent (avec
adaptations) du projet :

- **cubic9com/crowpanel-5.79_weather-display** — Licence **MIT**, © 2025 cubic9com
  https://github.com/cubic9com/crowpanel-5.79_weather-display

lui-même basé sur le code de démonstration officiel **Elecrow** pour le
CrowPanel ESP32 E-Paper 5.79" :
- https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792

Adaptations propres à ce projet : fonctions `EPD_ShowStringScaled`,
`EPD_ShowPictureResized`, `EPD_ShowBigTime` / `EPD_ShowBigTimeXL`.

## Polices
- **Chivo Mono** (`src/ChivoMonoFont.h`) — SIL Open Font License 1.1, par Omnibus-Type
  https://fonts.google.com/specimen/Chivo+Mono
- **Roboto Condensed** — Apache License 2.0, par Google
  Utilisée pour générer les gros chiffres nets (`src/bigfont.h`, `src/bigfontxl.h`)
  via `tools/genfont.py`.
  https://fonts.google.com/specimen/Roboto+Condensed

## Bibliothèques
- **ArduinoJson** — MIT, par Benoît Blanchon — https://arduinojson.org
- **Arduino core for ESP32** — LGPL / Apache, Espressif

## Données
- Météo fournie par **OpenWeatherMap** (clé API requise, non incluse).
  https://openweathermap.org
