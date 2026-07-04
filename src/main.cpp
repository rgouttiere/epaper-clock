// ============================================================
//  Horloge + météo e-paper — CrowPanel 5.79" (792x272)
//  ESP32-S3 / PlatformIO / lib Elecrow
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "EPD.h"
#include "icons.h"
#include "config.h"

// Mets à false une fois config.h rempli, pour passer en WiFi + API réels.
static const bool TEST_MODE = false;

#define EPD_BUFFER_SIZE 27200
uint8_t ImageBW[EPD_BUFFER_SIZE];
#define EPD_PWR_PIN 7

// Intervalles
static const unsigned long WEATHER_EVERY_MS = 30UL * 60UL * 1000UL; // re-fetch météo
static const int FULL_REFRESH_EVERY = 30; // full refresh tous les N partials (minutes)

// --- Icônes météo (indices dans Weather_Num[7]) ---
enum { ICON_CLEAR_DAY=0, ICON_CLEAR_NIGHT=1, ICON_CLOUDS=2, ICON_RAIN=3,
       ICON_THUNDER=4, ICON_SNOW=5, ICON_MIST=6 };

// --- Données météo ---
struct Slot    { char label[6]; int icon; int temp; };
struct GraphPt { int hour; int temp; int pop; };        // pour le graphe 24h
struct DayFc   { char label[4]; int tmin; int tmax; int icon; }; // prévisions 5j
struct Weather {
  bool valid = false;
  Slot slots[4]; // [0]=maintenant, [1]=+3h, [2]=+6h, [3]=+9h
  char desc[40] = ""; // UTF-8 (accents) -> plus d'octets
  int  humidity = 0;
  int  windKmh  = 0;
  char sunrise[6] = "--:--";
  char sunset[6]  = "--:--";
  GraphPt graph[8]; int graphN = 0;   // 8 points sur 24h
  DayFc   days[5];  int daysN  = 0;   // jusqu'à 5 jours
} weather;

// --- Noms français ---
const char* JOURS_ABR[] = {"dim","lun","mar","mer","jeu","ven","sam"};
const char* MOIS_ABR[]  = {"jan","fév","mars","avr","mai","juin","juil",
                           "août","sept","oct","nov","déc"};
const char* JOURS[] = {"dimanche","lundi","mardi","mercredi","jeudi","vendredi","samedi"};
const char* MOIS[]  = {"janvier","février","mars","avril","mai","juin","juillet",
                       "août","septembre","octobre","novembre","décembre"};

// --- Navigation (switch 5 directions, actif à l'état bas) ---
#define KEY_EXIT 1
#define KEY_NEXT 4
#define KEY_OK   5
#define KEY_PRV  6
#define NUM_PAGES 4  // 0=horloge+meteo, 1=horloge XL, 2=graphe 24h, 3=prev 5 jours
int  page = 0;
int  prevNext = 1, prevPrv = 1, prevOk = 1, prevExit = 1;

enum Mode { NORMAL, MENU };
Mode mode = NORMAL;
int  menuSel = 0;
const char* PAGE_NAMES[NUM_PAGES] = {
  "Horloge + météo", "Horloge plein écran", "Graphe 24h", "Prévisions 5 jours"
};

// --- État ---
int  lastMinute   = -1;
int  partialCount = 0;
unsigned long lastWeatherMs = 0;

// --- Prototypes ---
void connectWiFi();
void syncTime();
void fetchWeather();
void drawScreen();
void drawMenu();
void fullRefresh();
void partialRefresh();
bool keyEdge(int pin, int &prev);
int  iconFromCode(const String &code);
void setDesc(const char* s);
void drawDegree(uint16_t x, uint16_t y, uint16_t r);

// ---------- Utils ----------
void drawDegree(uint16_t x, uint16_t y, uint16_t r) {
  EPD_DrawCircle(x, y, r, BLACK, false);
  EPD_DrawCircle(x, y, r-1, BLACK, false);
}

void setDesc(const char* s) {
  if (!s) { weather.desc[0] = 0; return; }
  strncpy(weather.desc, s, sizeof(weather.desc) - 1);
  weather.desc[sizeof(weather.desc) - 1] = 0;
  if (weather.desc[0] >= 'a' && weather.desc[0] <= 'z') weather.desc[0] -= 32; // capitalise
}

int iconFromCode(const String &code) {
  if (code.startsWith("01")) return code.endsWith("n") ? ICON_CLEAR_NIGHT : ICON_CLEAR_DAY;
  if (code.startsWith("02") || code.startsWith("03") || code.startsWith("04")) return ICON_CLOUDS;
  if (code.startsWith("09") || code.startsWith("10")) return ICON_RAIN;
  if (code.startsWith("11")) return ICON_THUNDER;
  if (code.startsWith("13")) return ICON_SNOW;
  return ICON_MIST;
}

// ---------- WiFi / heure / météo ----------
void connectWiFi() {
  Serial.printf("WiFi: connexion a %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) { delay(500); Serial.print("."); tries++; }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.printf("WiFi OK, IP=%s\n", WiFi.localIP().toString().c_str());
  else Serial.println("WiFi ECHEC");
}

void syncTime() {
  configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
  struct tm t;
  Serial.print("NTP");
  for (int i = 0; i < 20 && !getLocalTime(&t, 500); i++) Serial.print(".");
  Serial.println(getLocalTime(&t) ? " OK" : " ECHEC");
}

bool httpGetJson(const String &url, JsonDocument &doc) {
  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != 200) { Serial.printf("HTTP %d\n", code); http.end(); return false; }
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) { Serial.printf("JSON err: %s\n", err.c_str()); return false; }
  return true;
}

void fetchWeather() {
  String base = "http://api.openweathermap.org/data/2.5/";

  // --- Maintenant (slot 0) ---
  String u1 = base + "weather?q=" + OWM_CITY + "&units=metric&lang=fr&appid=" + OWM_API_KEY;
  JsonDocument d1;
  if (httpGetJson(u1, d1)) {
    struct tm now; getLocalTime(&now);
    snprintf(weather.slots[0].label, 6, "%02dh", now.tm_hour);
    weather.slots[0].temp = (int)lround((float)d1["main"]["temp"]);
    weather.slots[0].icon = iconFromCode(d1["weather"][0]["icon"].as<String>());
    setDesc(d1["weather"][0]["description"].as<const char*>());
    weather.humidity = d1["main"]["humidity"].as<int>();
    weather.windKmh  = (int)lround((float)d1["wind"]["speed"] * 3.6f);
    time_t sr = d1["sys"]["sunrise"].as<long>();
    time_t ss = d1["sys"]["sunset"].as<long>();
    struct tm tsr, tss;
    localtime_r(&sr, &tsr); localtime_r(&ss, &tss);
    snprintf(weather.sunrise, 6, "%02d:%02d", tsr.tm_hour, tsr.tm_min);
    snprintf(weather.sunset,  6, "%02d:%02d", tss.tm_hour, tss.tm_min);
    weather.valid = true;
  }

  // --- Prévisions complètes (5 jours / 3h) avec filtre pour la RAM ---
  String u2 = base + "forecast?q=" + OWM_CITY + "&units=metric&lang=fr&appid=" + OWM_API_KEY;
  JsonDocument filter;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["weather"][0]["icon"] = true;
  filter["list"][0]["pop"] = true;

  HTTPClient http;
  http.begin(u2);
  if (http.GET() == 200) {
    JsonDocument d2;
    DeserializationError err = deserializeJson(d2, http.getStream(),
                                               DeserializationOption::Filter(filter));
    if (!err) {
      JsonArray list = d2["list"].as<JsonArray>();

      // slots +3h/+6h/+9h (page principale)
      for (int i = 0; i < 3 && i < (int)list.size(); i++) {
        JsonObject it = list[i];
        time_t tt = it["dt"].as<long>(); struct tm tmv; localtime_r(&tt, &tmv);
        snprintf(weather.slots[i+1].label, 6, "%02dh", tmv.tm_hour);
        weather.slots[i+1].temp = (int)lround((float)it["main"]["temp"]);
        weather.slots[i+1].icon = iconFromCode(it["weather"][0]["icon"].as<String>());
      }

      // graphe 24h (8 points de 3h)
      weather.graphN = 0;
      for (int i = 0; i < 8 && i < (int)list.size(); i++) {
        JsonObject it = list[i];
        time_t tt = it["dt"].as<long>(); struct tm tmv; localtime_r(&tt, &tmv);
        weather.graph[i].hour = tmv.tm_hour;
        weather.graph[i].temp = (int)lround((float)it["main"]["temp"]);
        weather.graph[i].pop  = (int)lround((float)(it["pop"] | 0.0f) * 100);
        weather.graphN++;
      }

      // agrégation journalière (5 jours), icône proche de 13h
      weather.daysN = 0;
      int curMday = -1, bestDiff[5] = {99,99,99,99,99};
      for (JsonObject it : list) {
        time_t tt = it["dt"].as<long>(); struct tm tmv; localtime_r(&tt, &tmv);
        int temp = (int)lround((float)it["main"]["temp"]);
        if (tmv.tm_mday != curMday) {
          if (weather.daysN >= 5) break;
          curMday = tmv.tm_mday;
          int d = weather.daysN++;
          strncpy(weather.days[d].label, JOURS_ABR[tmv.tm_wday], 4);
          weather.days[d].tmin = temp; weather.days[d].tmax = temp;
          weather.days[d].icon = iconFromCode(it["weather"][0]["icon"].as<String>());
          bestDiff[d] = abs(tmv.tm_hour - 13);
        } else {
          int d = weather.daysN - 1;
          if (temp < weather.days[d].tmin) weather.days[d].tmin = temp;
          if (temp > weather.days[d].tmax) weather.days[d].tmax = temp;
          int diff = abs(tmv.tm_hour - 13);
          if (diff < bestDiff[d]) { bestDiff[d] = diff; weather.days[d].icon = iconFromCode(it["weather"][0]["icon"].as<String>()); }
        }
      }
    } else { Serial.printf("JSON forecast err: %s\n", err.c_str()); }
  } else { Serial.println("HTTP forecast != 200"); }
  http.end();
}

// ---------- Rendu ----------
// PAGE 1 : horloge + météo (bandeau)
void drawPageMain() {
  struct tm t;
  char hhmm[6] = "--:--";
  char dateStr[40] = "";
  if (getLocalTime(&t)) {
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(dateStr, sizeof(dateStr), "%s %d %s", JOURS_ABR[t.tm_wday], t.tm_mday, MOIS_ABR[t.tm_mon]);
  }

  // ---- Panneau gauche : horloge ----
  EPD_ShowBigTime(14, 22, hhmm, BLACK);                  // grande police nette, 128 px
  if (weather.valid) EPD_ShowText(22, 162, weather.desc, BLACK);
  EPD_ShowTextScaled(22, 196, dateStr, 2, BLACK);        // date agrandie (accents)

  // Séparateur vertical principal
  EPD_DrawLine(372, 12, 372, 260, BLACK);

  // ---- Panneau droit : 4 colonnes météo ----
  if (weather.valid) {
    const int X0 = 380;
    const int COLW = 103;         // 4 colonnes de 103 px (380..792)
    const int ICON = 72;
    for (int i = 0; i < 4; i++) {
      int cx = X0 + i * COLW;
      // Label (heure)
      int lw = EPD_TextWidth(weather.slots[i].label);
      EPD_ShowText(cx + (COLW - lw) / 2, 16, weather.slots[i].label, BLACK);
      // Icône réduite
      EPD_ShowPictureResized(cx + (COLW - ICON) / 2, 46, 128, 128, ICON, ICON,
                             Weather_Num[weather.slots[i].icon], WHITE);
      // Température (gros)
      char ft[8]; snprintf(ft, sizeof(ft), "%d", weather.slots[i].temp);
      int w = strlen(ft) * 22;
      int tx = cx + (COLW - (w + 12)) / 2;
      EPD_ShowString(tx, 135, ft, 44, BLACK);
      drawDegree(tx + w + 6, 142, 5);
      // Séparateurs de colonnes (au-dessus du bandeau bas)
      if (i < 3) EPD_DrawLine(cx + COLW, 20, cx + COLW, 188, BLACK);
    }

    // ---- Bandeau d'infos en bas ----
    EPD_DrawLine(380, 192, 788, 192, BLACK);
    char line1[48], line2[48];
    snprintf(line1, sizeof(line1), "Humidité %d%%    Vent %d km/h", weather.humidity, weather.windKmh);
    snprintf(line2, sizeof(line2), "Lever %s    Coucher %s", weather.sunrise, weather.sunset);
    EPD_ShowText(392, 204, line1, BLACK);
    EPD_ShowText(392, 232, line2, BLACK);
  }
}

// PAGE 2 : horloge plein écran
void drawPageClock() {
  struct tm t;
  char hhmm[6] = "--:--";
  char dateStr[48] = "";
  if (getLocalTime(&t)) {
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", t.tm_hour, t.tm_min);
    snprintf(dateStr, sizeof(dateStr), "%s %d %s", JOURS[t.tm_wday], t.tm_mday, MOIS[t.tm_mon]);
  }
  // Heure XL centrée (largeur "HH:MM" = 553 px)
  EPD_ShowBigTimeXL((792 - 553) / 2, 8, hhmm, BLACK);
  // Date centrée en dessous
  int dw = EPD_TextWidth(dateStr) * 2;
  EPD_ShowTextScaled((792 - dw) / 2, 224, dateStr, 2, BLACK);
}

// PAGE 3 : graphe température + pluie sur 24h
void drawPageGraph() {
  EPD_ShowText(12, 8, "Température 24h", BLACK);
  if (!weather.valid || weather.graphN < 2) {
    EPD_ShowText(12, 120, "Pas de données", BLACK);
    return;
  }
  int n = weather.graphN;
  const int x0 = 60, x1 = 774, ytop = 54, ybot = 172;
  int tlo = 999, thi = -999;
  for (int i = 0; i < n; i++) { tlo = min(tlo, weather.graph[i].temp); thi = max(thi, weather.graph[i].temp); }
  if (thi == tlo) thi = tlo + 1;
  #define GX(i) (x0 + (int)((long)(i) * (x1 - x0) / (n - 1)))
  #define GY(t) (ybot - (int)((long)((t) - tlo) * (ybot - ytop) / (thi - tlo)))

  EPD_DrawLine(x0, ybot, x1, ybot, BLACK); // axe

  // Barres de pluie (probabilité), légères, sous la courbe
  for (int i = 0; i < n; i++) {
    int px = GX(i);
    int h = weather.graph[i].pop * 46 / 100;
    if (h > 0) EPD_DrawRectangle(px - 10, 236 - h, px + 10, 236, BLACK, 1);
  }
  // Courbe de température
  for (int i = 0; i < n - 1; i++)
    EPD_DrawLine(GX(i), GY(weather.graph[i].temp), GX(i+1), GY(weather.graph[i+1].temp), BLACK);
  // Marqueurs + labels
  for (int i = 0; i < n; i++) {
    int px = GX(i), py = GY(weather.graph[i].temp);
    EPD_DrawCircle(px, py, 3, BLACK, true);
    char b[8]; snprintf(b, sizeof(b), "%d°", weather.graph[i].temp);
    EPD_ShowText(px - EPD_TextWidth(b) / 2, py - 32, b, BLACK);
    char h[6]; snprintf(h, sizeof(h), "%02dh", weather.graph[i].hour);
    EPD_ShowText(px - EPD_TextWidth(h) / 2, 246, h, BLACK);
  }
  EPD_ShowText(x1 - 60, 214, "pluie", BLACK);
  #undef GX
  #undef GY
}

// PAGE 4 : prévisions 5 jours
void drawPage5Days() {
  EPD_ShowText(12, 8, "Prévisions 5 jours", BLACK);
  if (!weather.valid || weather.daysN == 0) {
    EPD_ShowText(12, 120, "Pas de données", BLACK);
    return;
  }
  int n = weather.daysN;
  int colw = 792 / n;
  for (int i = 0; i < n; i++) {
    int cx = i * colw;
    int lw = EPD_TextWidth(weather.days[i].label);
    EPD_ShowText(cx + (colw - lw) / 2, 44, weather.days[i].label, BLACK);
    EPD_ShowPictureResized(cx + (colw - 80) / 2, 74, 128, 128, 80, 80,
                           Weather_Num[weather.days[i].icon], WHITE);
    char mx[8]; snprintf(mx, sizeof(mx), "%d°", weather.days[i].tmax);
    char mn[8]; snprintf(mn, sizeof(mn), "%d°", weather.days[i].tmin);
    EPD_ShowText(cx + (colw - EPD_TextWidth(mx)) / 2, 168, mx, BLACK);
    EPD_ShowText(cx + (colw - EPD_TextWidth(mn)) / 2, 206, mn, BLACK);
    if (i < n - 1) EPD_DrawLine(cx + colw, 20, cx + colw, 250, BLACK);
  }
}

// Dispatcher
void drawScreen() {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  switch (page) {
    case 0: drawPageMain();  break;
    case 1: drawPageClock(); break;
    case 2: drawPageGraph(); break;
    case 3: drawPage5Days(); break;
  }
}

// Menu de sélection d'affichage (ouvert par OK)
void drawMenu() {
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  EPD_ShowText(30, 12, "Choisir l'affichage :", BLACK);
  EPD_DrawLine(30, 44, 762, 44, BLACK);
  const int y0 = 56, lh = 50;
  for (int i = 0; i < NUM_PAGES; i++) {
    int y = y0 + i * lh;
    if (i == menuSel) {
      EPD_DrawRectangle(30, y - 4, 762, y + 50, BLACK, 1); // barre pleine
      EPD_ShowTextScaled(46, y, PAGE_NAMES[i], 2, WHITE);  // texte blanc
    } else {
      EPD_ShowTextScaled(46, y, PAGE_NAMES[i], 2, BLACK);
    }
  }
  EPD_ShowText(30, 250, "Tourner: choisir   OK: valider   Exit: annuler", BLACK);
}

// Lecture d'un bouton : front descendant confirmé (anti-glitch), après 2 s
bool keyEdge(int pin, int &prev) {
  int v = digitalRead(pin);
  bool e = false;
  if (millis() > 2000 && prev == 1 && v == 0) {
    delay(25);
    if (digitalRead(pin) == 0) e = true;
  }
  prev = v;
  return e;
}

// ---------- Refresh ----------
// Full refresh : nettoyage complet (léger flash), à faire périodiquement.
void fullRefresh() {
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  EPD_Clear_R26A6H();
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}
// Partial : rapide, sans reset hardware -> pas de ghosting cumulé si l'init
// a été faite une fois (état RAM conservé).
void partialRefresh() {
  EPD_Display(ImageBW);
  EPD_PartUpdate();
}

// ---------- Setup / loop ----------
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Horloge e-paper ===");

  pinMode(EPD_PWR_PIN, OUTPUT);
  digitalWrite(EPD_PWR_PIN, HIGH);
  delay(100);
  EPD_GPIOInit();

  // Boutons de navigation (roue crantée), idle = HIGH
  pinMode(KEY_NEXT, INPUT_PULLUP);
  pinMode(KEY_PRV,  INPUT_PULLUP);
  pinMode(KEY_OK,   INPUT_PULLUP);
  pinMode(KEY_EXIT, INPUT_PULLUP);
  delay(50);
  prevNext = digitalRead(KEY_NEXT); // capture l'état réel au repos
  prevPrv  = digitalRead(KEY_PRV);
  prevOk   = digitalRead(KEY_OK);
  prevExit = digitalRead(KEY_EXIT);

  connectWiFi();
  syncTime();
  fetchWeather();
  lastWeatherMs = millis();

  drawScreen();
  fullRefresh(); // baseline propre
  Serial.println("Affichage initial OK");
}

void loop() {
  // --- Lecture des boutons (fronts descendants confirmés) ---
  bool goNext = keyEdge(KEY_NEXT, prevNext);
  bool goPrv  = keyEdge(KEY_PRV,  prevPrv);
  bool okP    = keyEdge(KEY_OK,   prevOk);
  bool exitP  = keyEdge(KEY_EXIT, prevExit);

  if (mode == NORMAL) {
    if (okP) {
      // Ouvre le menu
      mode = MENU;
      menuSel = page;
      drawMenu();
      fullRefresh();
      delay(250);
    } else if (goNext || goPrv) {
      // Bascule directe de page (sans passer par le menu)
      page = goNext ? (page + 1) % NUM_PAGES : (page - 1 + NUM_PAGES) % NUM_PAGES;
      Serial.printf("Page -> %d\n", page);
      drawScreen();
      fullRefresh();
      partialCount = 0;
      delay(250);
    }
  } else { // MENU
    if (goNext || goPrv) {
      menuSel = goNext ? (menuSel + 1) % NUM_PAGES : (menuSel - 1 + NUM_PAGES) % NUM_PAGES;
      drawMenu();
      partialRefresh();     // déplacement du curseur : rapide
      delay(200);
    } else if (okP) {
      page = menuSel;       // valide le choix
      mode = NORMAL;
      Serial.printf("Menu -> page %d\n", page);
      drawScreen();
      fullRefresh();
      partialCount = 0;
      delay(250);
    } else if (exitP) {
      mode = NORMAL;        // annule, revient à la page courante
      drawScreen();
      fullRefresh();
      partialCount = 0;
      delay(250);
    }
  }

  // --- Mise à jour à chaque changement de minute (uniquement hors menu) ---
  if (mode == NORMAL) {
    struct tm t;
    if (getLocalTime(&t) && t.tm_min != lastMinute) {
      lastMinute = t.tm_min;

      if (millis() - lastWeatherMs > WEATHER_EVERY_MS) {
        fetchWeather();
        lastWeatherMs = millis();
      }

      drawScreen();
      if (partialCount >= FULL_REFRESH_EVERY) {
        fullRefresh();       // nettoyage ghosting périodique
        partialCount = 0;
      } else {
        partialRefresh();    // mise à jour rapide
        partialCount++;
      }
    }
  }
  delay(50); // réactif aux boutons
}
