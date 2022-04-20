/*
 * MightyOhm Geiger Counter WemosD1 Display/HTTP/MQTT handling
 * by Chris Figge
 * twitter.com/nerdpole - twitch.tv/chrisfigge - zackbummfertig.net
 * 
 * Enclosure and BOM:
 * https://www.printables.com/model/174192-mightyohm-geiger-counter-case 
 *
 * This script is heavily based on the work of:
 * schinken: https://github.com/schinken/esp8266-geigercounter
 * and
 * Hiroyuki ITO: http://intellectualcuriosity.hatenablog.com/ 
 *
 * Thanks for your awesome work.
 *
 * MIT Licensed. 
*/

#include <ArduinoJson.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Ticker.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFiClientSecureBearSSL.h>
#include "CircularBuffer.h"
#include <PubSubClient.h>
#include <SoftwareSerial.h>
#include "settings.h"

#ifdef OTA_PASSWORD
  #include "OTA.h"
#endif

ESP8266WiFiMulti WiFiMulti;
PubSubClient mqttClient;
WiFiClient wifiClient;
SoftwareSerial geigerCounterSerial(PIN_UART_RX, PIN_UART_TX);
ESP8266WebServer server(80);
CircularBuffer<int,HISTORY_SIZE> history;
Ticker ticker;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif

uint8_t idx = 0;
char buffer[64];
char hostname[24];

int lastCPM = 0, currentCPM = 0;
int lastCPS = 0, currentCPS = 0;
float lastuSv = 0, currentuSv = 0;
int loopCnt = 0, dataHttpPushCnt = 0, dataMqttPushCnt = 0;

bool ticking = false;
int lastTickedMs = 0, tickShowMs = 50;
bool buttonState = false;
int buttonLastPressMs = 0;
int currentFrame = FRAME_AUTHOR;
String json;

void setup() {
  delay(1000);
  
  Serial.begin(115200);
  geigerCounterSerial.begin(9600);
  delay(10);
  ticker.attach(1, loopCount);
  setupOLED();
  drawIntro();
  switchFrame();
  fillHistory();
  startWIFI();
  startWebserver();
  handleOTASetup();
 
  if (MQTT_ENABLED) {
    mqttClient.setClient(wifiClient);
    mqttClient.setServer(MQTT_HOST, 1883);
    runMqtt();
  }

  pinMode(PIN_BUTTON, INPUT_PULLUP);

  #ifdef OTA_PASSWORD
    handleOTASetup();
  #endif

  #ifdef PIN_PULSE
    pinMode(PIN_PULSE, INPUT);
    attachInterrupt(PIN_PULSE, onPulse, RISING);
  #endif
}

#ifdef PIN_PULSE
ICACHE_RAM_ATTR void onPulse() {
  if(DEBUG) Serial.println("TICK!");
  lastTickedMs = millis();  
  //if (MQTT_ENABLED) mqttClient.publish(MQTT_TOPIC_PULSE, "true");
}
#endif

/**
  Parse current serial data 
  and decide what we have to do
*/
void parseReceivedLine(char* input) {
  char segment = 0;
  char *token;

  float uSv = 0, cpm = 0, cps = 0;
  token = strtok(input, ", ");

  while (token != NULL) {
    switch (segment) {
      // This is just for validation
      case IDX_CPS_KEY: if (strcmp(token, "CPS") != 0) return;  break;
      case IDX_CPM_KEY: if (strcmp(token, "CPM") != 0) return;  break;
      case IDX_uSv_KEY: if (strcmp(token, "uSv/hr") != 0) return;  break;

      case IDX_CPS:
        if(DEBUG) Serial.printf("Current CPS: %s\n", token);
        cps = atoi(token);
        break;

      case IDX_CPM:
        if(DEBUG) Serial.printf("Current CPM: %s\n", token);
        cpm = atoi(token);
        break;

      case IDX_uSv:
        if(DEBUG) Serial.printf("Current uSv/hr: %s\n", token);
        uSv = atof(token);
        break;
    }

    if (segment > 7) {
      // Invalid! There should be no more than 7 segments
      return;
    }

    token = strtok(NULL, ", ");
    segment++;
  }

  currentuSv = uSv;
  currentCPM = cpm;
  currentCPS = cps;

  if(currentFrame == FRAME_GEIGER) {
      displayCount(currentuSv, UNIT_USV);
      displayCount(currentCPM, UNIT_CPM);
      displayCount(currentCPS, UNIT_CPS);
      showBar(currentCPS);
  }
}

/**
  Draw data into base layout
*/
void displayCount(float cnt, int mode) {
  switch (mode) {
    case UNIT_CPS:
      display.fillRect(110, 22, 18, 8, BLACK);
      display.setCursor(110,22);
      display.setTextSize(1);
      break;
    case UNIT_CPM:
      display.fillRect(6, 50, 54, 24, BLACK);
      display.setCursor(6, 50);
      display.setTextSize(2);
      break;
    case UNIT_USV:
      display.fillRect(80, 50, 54, 24, BLACK);
      display.setCursor(80, 50);
      display.setTextSize(2);
      break;
  }
  switch(mode) {
    case UNIT_CPM:
    case UNIT_CPS:
      if (cnt < 10) {
        display.print("  ");
      } else if (cnt < 100) {
        display.print(" ");
      }
      display.print((int) cnt);
      break;

    default:
      display.print(String(cnt,2));
      break;      
  } 
  display.display();
}

/**
  Looping!!!!!
*/
void loop() {
  #ifdef OTA_PASSWORD
    ArduinoOTA.handle();
  #endif

  if (MQTT_ENABLED) runMqtt();

  if (geigerCounterSerial.available()) {
    char input = geigerCounterSerial.read();
    buffer[idx++] = input;
    if (DEBUG) Serial.write(input);
    
    if (input == '\n') {
      parseReceivedLine(buffer);
      idx = 0;
    }

    if (idx > 42) idx = 0;
  }

  handleTick();
  handleButton();

  handleHttpDataPush();
  handleMqttDataPush();
  server.handleClient();
}

/**
  Start or reconnect mqtt
  by switching into an infinite loop
  as long as the connection is brolen
*/
void runMqtt() {
  if (MQTT_ENABLED) {
    mqttClient.loop();
    while (!mqttClient.connected()) {
      if (mqttClient.connect(MQTT_CLIENTID, MQTT_USER, MQTT_PASSWORD)) {
      //if (mqttClient.connect(hostname, MQTT_TOPIC_LAST_WILL, 1, true, "disconnected")) {
        mqttClient.publish(MQTT_TOPIC_LAST_WILL, "connected", true);
        Serial.println("MQTT connected");
      } else {
        Serial.println("MQTT connect failed");
        delay(1000);
      }
    }
  }
}

/**
   Start or reconnect the wifi
   by switching into an infinite loop
   as long as the connection is broken
*/
void startWIFI() {

  if (WIFI_ENABLED) { 
    #ifdef WIFI_HOSTNAME
      strncpy(hostname, WIFI_HOSTNAME, sizeof(hostname));
    #else
      snprintf(hostname, sizeof(hostname), "ESP-GEIGER-%X", ESP.getChipId());
    #endif

    int loopcnt = 0;
    Serial.println("---");
    WiFi.mode(WIFI_STA);
    Serial.println("Connecting WIFI.");
    Serial.println("(Re)Connecting to Wifi-Network with following credentials:");
    Serial.print("SSID: ");
    Serial.println(hostname);
    Serial.print("Key: ");
    //Serial.println(WIFI_PASSWORD);

    WiFi.hostname(hostname);
    WiFiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

    while (WiFiMulti.run() != WL_CONNECTED) {
      loopcnt++;
      if (loopcnt < 10) {
        Serial.print(".");
      }
      delay(500);
    }

    Serial.println("");
    Serial.println("WiFi connected.");
    Serial.println("IP address:");
    Serial.println(WiFi.localIP().toString());
  }
}

/**
  Draw base layout for displaying geiger data.  
*/
void drawGeigerFrame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("    GEIGER COUNTER");
  display.setCursor(110,14);
  display.println("CPS");
  display.setCursor(24, 37);
  display.println("CPM");
  display.setCursor(97, 37);
  display.println("uSv/h");
  display.display();
}

/**
  Shows SSID and current IP
*/
void drawInfoFrame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("         INFO");
  display.setTextSize(1);
  display.setCursor(3,14);
  display.println("SSID");
  display.setCursor(8,26);
  display.println(WIFI_SSID);
  display.setCursor(3,40);
  display.println("IP");
  display.setCursor(8,52);
  display.println(WiFi.localIP());
  display.display();
}

/**
  Draws some information about the author.
*/
void drawAuthorFrame() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print("         INFO");
  display.setTextSize(1);
  display.setCursor(3,14);
  display.println("Author");
  display.setCursor(8,26);
  display.println("Chris Figge");
  display.setCursor(3,40);
  display.println("Contact");
  display.setCursor(8,52);
  display.println("info@flazer.net");
  display.display();
}

/**
  Draws the intro onto the OLED.
  +10 coolness.
*/
void drawIntro() {
  int x1start = -90, x1end = 8;
  int x2start = 128, x2end = 35;
  bool finished1 = false, finished2 = false;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(4, 55);
  display.setTextColor(WHITE);
  display.print("a chris figge device");
  display.setTextSize(2);
  display.setTextWrap(false);
  while (!finished1 || !finished2) {
    x1start = x1start+2;
    x2start = x2start-2;
    if (!finished1) {
      display.fillRect(0, 0, 128, 25, BLACK);
      display.setCursor(x1start, 5);
      display.print("Geiger");
    }
    if (!finished2) {
      display.fillRect(0, 28, 128, 25, BLACK);
      display.setCursor(x2start, 28);
      display.print("Counter");
    }

    if (x1start >= x1end) finished1 = true;
    if (x2start <= x2end) finished2 = true;
    display.display();
  }
  delay(1500);
}

/**
   Tick, tick, tick
*/
void loopCount() {
  loopCnt++;
  dataHttpPushCnt++;
  dataMqttPushCnt++;
}

/**
   Start webserver for handling
   incoming requests
*/
void startWebserver() {
  Serial.println("Starting HTTP-Server...");
  Serial.println("-- Registering routes.");
  server.on("/cpm", HTTP_GET, []() {
    server.send(200, "text/plain", String(currentCPM));
  });
  server.on("/usv", HTTP_GET, []() {
    server.send(200, "text/plain", String(currentuSv));
  });

  server.onNotFound(handleRequestNotFound);
  Serial.println("-- Launching server ...");
  server.begin();
  Serial.println("-- DONE.");
}

/**
  Setting up OLED Display.
*/
void setupOLED() {
  Serial.println("OLED_DISPLAY starting");
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDR)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) yield();
  }
  display.setRotation(2); // rotate because enclosure forces us to install display upsidedown
  display.clearDisplay();
  display.display();
}

/**
   Handles sending data to web api (if client is enabled)
   This is used to send the last 50 measurements to my own backend, so I can display
   a blinky nuclear-sign based on the last minute. It sends a json-object.
   Key is second, value is ticks. Only seconds with a value > 0 are send, to save some
   bandwith. I know it's not necessary, but i liked the idea. 
*/
void handleHttpDataPush() {
  if (HTTP_CLIENT_ENABLED) {
    if (dataHttpPushCnt > HTTP_PUSH_INTERVAL_SECONDS) {
      Serial.println("[HTTP] Start sending data to api.");
      std::unique_ptr<BearSSL::WiFiClientSecure>client(new BearSSL::WiFiClientSecure);
      client->setInsecure();
      HTTPClient https;
      if (https.begin(*client, HTTP_CLIENT_URL)) {
        https.addHeader("secret-token", HTTP_CLIENT_SECRECT);
        https.addHeader("Content-Type", "application/json");
        https.setTimeout(1000);
        String json;
        DynamicJsonDocument jsonDoc(2024);
        JsonObject pattern  = jsonDoc.createNestedObject("pattern");
        for (int i = 0; i < history.size(); i++) {
          if (history[i] > 0) pattern[String(i)] = history[i];
        }
        serializeJson(jsonDoc, json);
        int httpCode = https.PUT(json);
        jsonDoc.clear();
        Serial.print("Response code: ");
        Serial.println(httpCode);
      }
      Serial.println("[HTTP] end sending data.");
      dataHttpPushCnt = 0;
    }
  }
}

/**
  Publish values via MQTT
  (If MQTT is enabled)
*/
void handleMqttDataPush() {
  if (MQTT_ENABLED) {
    if (dataMqttPushCnt > MQTT_PUSH_INTERVAL_SECONDS) {
      Serial.println("[MQTT] Start sending data via mqtt.");
      char tmp[8];

      if (currentCPM != lastCPM) {
        sprintf(tmp, "%d", currentCPM); 
        //if (MQTT_ENABLED) mqttClient.publish(MQTT_TOPIC_CPM, tmp, true);
      }

      if (currentuSv != lastuSv) {
        sprintf(tmp, "%.2f", currentuSv); 
        Serial.println("PUBLISH");
        if (MQTT_ENABLED) mqttClient.publish(MQTT_TOPIC_USV, tmp, true);
      }

      lastCPS = currentCPS;
      lastCPM = currentCPM;
      lastuSv = currentuSv;
      
      Serial.println("[MQTT] end sending data.");
      dataMqttPushCnt = 0;
    }
  }
}

/**
  Calculating ticks for showing them onto graphical bar.
*/
void showBar(int cnt) {
  history.push(cnt);
  display.fillRect(BAR_X1, BAR_Y1, BAR_X2, BAR_Y2, BLACK);
  int offset = HISTORY_SIZE - BARPOINTS_SIZE;
  for (int i = 0; i < BARPOINTS_SIZE; i++) {
    int height = history[i];
    height++; // for a better view
    if (height > 1) height = (BAR_Y2/10)*height;
    if (height > BAR_Y2) height = BAR_Y2;
    display.fillRect(BAR_X1 + i*2, BAR_Y1 + (BAR_Y1-height), 2, height, WHITE);
  }
  display.display();
}

/**
  Handles displaying every incoming tick 
  (detected via PIN_PULSE)
*/
void handleTick() {
  if (lastTickedMs > 0 && !ticking) {
    display.fillCircle(120, 4, 4, WHITE);
    display.display();
    ticking = true;
    lastTickedMs = millis();
  } else if(ticking && millis() - lastTickedMs > tickShowMs) {
    ticking = false;
    lastTickedMs = 0;
    display.fillCircle(120, 4, 4, BLACK);
    display.display();
  }
}

/**
  Check if the button is pressed.
  Handles debouncing.
*/
void handleButton() {
  bool currentState = digitalRead(PIN_BUTTON);
  if (currentState != buttonState && buttonLastPressMs == 0) {
    buttonState = currentState;
    buttonLastPressMs = millis();
    if (!currentState) {
      switchFrame();
    }
  }
  
  if (millis() - buttonLastPressMs > 300) {
    buttonLastPressMs = 0;
  } 
}

/**
  Switch between the frames.
  Based on the current active one.
*/
void switchFrame() {
  switch (currentFrame) {
    case FRAME_GEIGER:
    currentFrame = FRAME_INFO;
    drawInfoFrame();
    break;

    case FRAME_INFO:
    currentFrame = FRAME_AUTHOR;
    drawAuthorFrame();
    break;

    case FRAME_AUTHOR:
    currentFrame = FRAME_GEIGER;
    drawGeigerFrame();
    break;
    

    default:
    break;
  }
}

/**
  Just put 0 all over the history.
  0 everywhere!
*/
void fillHistory() {
  for (int i=0; i<HISTORY_SIZE; i++) {
    history.push(0);
  }
}

/**
   Unknown Route
   Send teapot.
*/
void handleRequestNotFound() {
  server.send(418, "text/plain", "I'm a geiger counter. Tik, tiktiktiktikttttik.");
}