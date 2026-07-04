# Horloge & météo e-paper — CrowPanel 5.79"

**Français** · [English](README.en.md)

Horloge et station météo minimaliste sur écran **e-paper CrowPanel ESP32 5.79"**
(792×272, ESP32-S3), programmée en **PlatformIO**. Heure synchronisée par NTP,
météo via OpenWeatherMap, navigation par la roue crantée entre plusieurs pages.

![Montage horloge e-paper CrowPanel 5.79](images/montage.jpg)

## Fonctionnalités

- ⏰ **Horloge** synchronisée NTP (fuseau + heure d'été gérés)
- 🌤️ **Météo temps réel** (OpenWeatherMap) : conditions, température, humidité,
  vent, lever/coucher du soleil
- 🖥️ **4 pages** navigables à la roue crantée :
  1. Horloge + météo (bandeau : heure, prévisions +3h/+6h/+9h, infos)
  2. Horloge plein écran
  3. Graphe de température sur 24h + probabilité de pluie
  4. Prévisions 5 jours (icône + min/max)
- 🎛️ **Menu de sélection** (appui sur la roue) pour choisir l'affichage
- ✨ Grandes polices nettes (rendues à la vraie résolution, pas agrandies)
- 🔄 Rafraîchissement partiel à la minute **sans ghosting** + full refresh périodique

## Matériel

- [Elecrow CrowPanel ESP32 E-Paper HMI 5.79" (792×272, N&B)](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- Câble USB-C (alimentation + flash)

Aucun câblage supplémentaire : l'écran, l'ESP32-S3 et la roue crantée sont sur la carte.

## Installation

1. Installe [PlatformIO](https://platformio.org/) (Core ou l'extension VS Code).
2. Copie le modèle de config et remplis-le :
   ```bash
   cp src/config.template.h src/config.h
   ```
   Renseigne : SSID/mot de passe WiFi (**2,4 GHz**), clé API
   [OpenWeatherMap](https://openweathermap.org/api) (gratuite) et ta ville
   (format `Ville,FR`, sans accents).
3. Compile et flashe :
   ```bash
   pio run -t upload
   ```
   > Si l'upload échoue à 921600 bauds, il est déjà réglé sur 460800 dans
   > `platformio.ini` (la puce UART n'aime pas la vitesse max).

`src/config.h` contient tes secrets : il est **exclu du dépôt** (`.gitignore`).

## Navigation

| Action | Effet |
|---|---|
| Tourner la roue | Page suivante / précédente |
| Appui (OK) | Ouvrir le menu de sélection |
| Dans le menu : tourner | Déplacer le curseur |
| Dans le menu : OK | Valider le choix |
| Dans le menu : Exit | Annuler |

## Régénérer les polices

Les gros chiffres sont générés depuis une police TTF (Roboto Condensed par défaut) :

```bash
pip install pillow
python3 tools/genfont.py /chemin/vers/police.ttf   # met à jour src/bigfont.h et src/bigfontxl.h
```

## Pièges rencontrés (utile si tu reprends cette carte)

- **GPIO 7 = alimentation de la dalle.** Il faut `digitalWrite(7, HIGH)` avant
  toute utilisation, sinon le contrôleur ne répond pas et le code se bloque dans
  la boucle d'attente `BUSY` (écran vide). Non documenté dans les vieux exemples.
- **GxEPD2 ne fonctionne pas** sur ce panneau : il est piloté par **deux
  contrôleurs SSD1683 en cascade** (2×396×272 avec décalage). On utilise le
  pilote Elecrow (SPI logiciel).
- **Rafraîchissement** : `EPD_FastMode1Init()` fait un reset matériel — ne pas
  l'appeler à chaque minute (ça casse le différentiel et crée du ghosting). Init
  une fois, puis `EPD_Display` + `EPD_PartUpdate` pour les mises à jour rapides,
  et un full refresh périodique pour nettoyer.
- **Accents** : les polices embarquées n'ont pas les caractères accentués ; le
  texte UTF-8 est converti en ASCII (`deaccent()`).
- **Roue crantée** = switch 5 directions lu comme des boutons actifs à l'état bas
  (GPIO 4 = suivant, 6 = précédent, 5 = OK, 1 = exit).

## Crédits & licences

Basé sur le pilote de [cubic9com](https://github.com/cubic9com/crowpanel-5.79_weather-display)
(MIT) et le code de démo Elecrow. Voir [CREDITS.md](CREDITS.md) pour le détail.

Code de ce projet sous licence [MIT](LICENSE).
