#include "WiFi.h"
#include "../config/config_wifi.h"

/**
 * Initializes Wifi, but cycles it twice to avoid the common problem
 * of stuck wifi on startup. Waiting times are random/empirically set up,
 * hence can be optimized.
 */
void initWifi()
{
  WiFi.begin(ssid, password);
  delay(1000);
  WiFi.mode(WIFI_OFF);
  delay(500);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.println("Connecting to WiFi..");
  }
  delay(100);
  Serial.println(WiFi.localIP());
}

