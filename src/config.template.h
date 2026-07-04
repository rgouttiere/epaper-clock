// ============================================================
//  MODÈLE DE CONFIG — copie ce fichier en "config.h" et remplis-le.
//    cp src/config.template.h src/config.h
//  (config.h est ignoré par Git pour protéger tes secrets)
// ============================================================
#ifndef CONFIG_H
#define CONFIG_H

// --- WiFi (2,4 GHz uniquement) ---
#define WIFI_SSID     "TON_RESEAU_WIFI"
#define WIFI_PASSWORD "TON_MOT_DE_PASSE"

// --- OpenWeatherMap ---
// Clé gratuite sur https://openweathermap.org/api (onglet "API keys")
#define OWM_API_KEY   "TA_CLE_API"
// Ville au format "Ville,CodePays" sans accents — ex "Paris,FR"
#define OWM_CITY      "Paris,FR"

// --- Fuseau horaire (Paris, gère l'heure d'été automatiquement) ---
#define TZ_INFO       "CET-1CEST,M3.5.0,M10.5.0/3"

#endif
