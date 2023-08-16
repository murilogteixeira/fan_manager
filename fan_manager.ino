#include <Arduino.h>
#include <WiFiManager.h>
#include <OTA.h>
#include <Configuration.h>
#include <EspFile.h>
#include <Relay.h>
#include <InputPin.h>

#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

#define RELAY1 D1
#define RELAY2 D2
#define SENSOR D3

String host = "fan_manager";
char ssid[] = "MURILO";
char password[] = "94658243";
const float maxTemp = 31;
const int oneMinute = 1000 * 60;
const int tempCheckInterval = oneMinute;
const int activeTimeWhenIsOn = oneMinute * 3;
const int checkInterval = 1000;

unsigned long lastCheckTemp = checkInterval;
unsigned long lastTempMillis = tempCheckInterval;
unsigned long lastActiveRelayTimes[] = { -1, -1 };

WiFiManager wifi;
OTA ota;
EspFile espFile;
Configuration config;
ESP8266WebServer server;
OneWire oneWire(SENSOR);
DallasTemperature sensors(&oneWire);
HTTPClient http;

Relay relay1(RELAY1, LOW, true);
Relay relay2(RELAY2, LOW, true);
Relay relays[] = { relay1, relay2 };

DynamicJsonDocument statusJson(1024);

void setupConfig();
void setupConnection();
void setupServer();
void handleTemperatures();

void setup() {
  Serial.begin(9600);
  Serial.println("\n\nBooting...");
  LittleFS.begin();
  setupConfig();
  setupConnection();
  sensors.begin();
}

void loop() {
  server.handleClient();
  MDNS.update();
  wifi.refreshConnection();
  handleTemperatures();
}

void setupConfig() {
  config.begin();

  String espName = config.getEspName();

  if (espName != "null" && espName != "" && espName != NULL)
    host = espName;
  else
    config.setEspName(host);
}

void setupConnection() {
  MDNS.begin(host);
  wifi.connectStation(ssid, password, []() {
    setupServer();
    MDNS.addService("http", "tcp", 80);
    Serial.printf("\nMDNS: http://%s.local\n", host.c_str());
  });
}

void setupServer() {
  server.enableCORS(true);

  // OTA
  server.on("/ota/update", HTTP_GET, []() {
    server.send(200, "text/html", espFile.getUploadFileHtml());
  });

  server.on(
    "/ota/update", HTTP_POST, []() {
      ota.updateHandler(&server);
    },
    []() {
      ota.uploadHandler(&server);
    });

  // CONFIG
  server.on("/config/updateName", HTTP_GET, []() {
    config.updateNameHandler(&server);
  });

  // FILE
  server.on(espFile.getUploadPath(), HTTP_GET, []() {
    server.send(200, "text/html", espFile.getUploadFileHtml());
  });

  server.on(
    espFile.getUploadPath(), HTTP_POST, []() {
      server.send(200);
    },
    []() {
      espFile.uploadFileHandler(&server);
    });

  server.on(espFile.getDeletePath(), HTTP_GET, []() {
    espFile.deleteFileHandler(&server);
  });

  server.on(espFile.getListPath(), HTTP_GET, []() {
    espFile.listFileHandler(&server);
  });

  // RELAY
  // server.on("/relay", []() {
  //   relay.toggleHandler(&server);
  // });

  server.on("/relayOff", []() {
    relay1.on();
    relay2.on();
    server.send(200);
  });

  server.on("/relayOn", []() {
    relay1.off();
    relay2.off();
    server.send(200);
  });

  server.on("/status", []() {
    statusJson["relays"]["0"] = relay1.getState();
    statusJson["relays"]["1"] = relay2.getState();

    String statusString;
    serializeJson(statusJson, statusString);
    server.send(200, "application/json", statusString.c_str());
  });

  server.on("/", []() {
    WiFiClientSecure client;
    client.setInsecure();

    http.begin(client, "https://raw.githubusercontent.com/murilogteixeira/fan_manager/main/data/status.html");
    int httpCode = http.GET();

    if(httpCode < 1) {
      Serial.println("request error - " + httpCode);
      return;
    }

    if(httpCode != HTTP_CODE_OK) return;
    String payload = http.getString();
    http.end();

    server.send(httpCode, "text/html", payload.c_str());
  });

  // 404
  server.onNotFound([]() {
    server.send(404, "text/plain", "404: Not Found");
  });

  server.begin();
}

void handleTemperatures() {
  // Request temperatures
  if (millis() - lastCheckTemp < checkInterval) return;
  lastCheckTemp = millis();

  // Request sensors data
  sensors.requestTemperatures();

  int deviceCount = sensors.getDeviceCount();
  float temp[deviceCount];

  // Save the current temperatures
  for (int i = 0; i < deviceCount; i++) {
    temp[i] = sensors.getTempCByIndex(i);
    statusJson["sensors"][String(i)] = temp[i];
  }

  // Update relay states
  if (millis() - lastTempMillis < tempCheckInterval) return;
  lastTempMillis = millis();

  for (int i = 0; i < deviceCount; i++) {
    if (temp[i] > maxTemp) {
      relays[i].on();
      if (lastActiveRelayTimes[i] < 0)
        lastActiveRelayTimes[i] = millis();
    } else if (millis() - lastActiveRelayTimes[i] > activeTimeWhenIsOn) {
      relays[i].off();
      lastActiveRelayTimes[i] = -1;
    }
  }
}