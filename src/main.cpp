#include <Arduino.h>
#include "WiFi.h"
#include "FS.h"
#include "SPIFFS.h"

#include "files/mywifi.h"
#include "files/dht.h"
#include "files/AzIoT-hub.h"
#include "files/time.h"



void setup() {
  Serial.begin(kBaudRate, SERIAL_8N1);
  while (!Serial)  // Wait for the serial connection to be establised.
    delay(50);

  // put your setup code here, to run once:
  ACSetup();
  DHTSetup();
  initWifi();
  TimeSetup();
  AzIoTSetup();
  delay(10000);
}

void loop() {
  try {
    // put your main code here, to run repeatedly:
    long myLoop = millis();
    Serial.println("START");

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Wifi sconnected");
      initWifi();
      TimeSetup();
    }
    else if (sasToken.IsExpired())
    {
      Serial.println("Token is expired");
      (void)esp_mqtt_client_destroy(mqtt_client);
      initializeMqttClient();
    }
    else if (millis() > next_telemetry_send_time_ms)
    {    
      DHTPrintValues();
      sendTelemetry(humidity, temperature, heatIndex, power, temp, mode, fan);
      next_telemetry_send_time_ms = millis() + TELEMETRY_FREQUENCY_MILLISECS;
    }  

    Serial.println("END");
    delay(10000);
  }
  catch(...){
    Serial.println("Error");
    delay(10000);
  }
}

