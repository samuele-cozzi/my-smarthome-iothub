#include "Arduino.h"
// #include "FS.h"
// #include "SPIFFS.h"
// #include "ESPAsyncWebServer.h"
#include "ArduinoJson.h"

#include "AzIoTSasToken.h"
#include "ac-control.h"
#include "../config/config_az.h"

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>
#include <az_span.h>

#include "mqtt_client.h"

#include <iostream>
#include <string>

using std::cout; using std::cin;
using std::endl; using std::string;

#define STRING(num) #num

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'. 
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp32)"
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define MQTT_QOS1 1
#define DO_NOT_RETAIN_MSG 0

// Memory allocated for the sample's variables and structures.
static esp_mqtt_client_handle_t mqtt_client;
static az_iot_hub_client client;

static char mqtt_client_id[128];
static char mqtt_username[128];
static char mqtt_password[200];
const int mqtt_port = AZ_IOT_DEFAULT_MQTT_CONNECT_PORT;

static unsigned long next_telemetry_send_time_ms = 0;
static char telemetry_topic[128];
static uint8_t telemetry_payload[100];
static uint32_t telemetry_send_count = 0;

static uint8_t sas_signature_buffer[256];

#define INCOMING_DATA_BUFFER_SIZE 128
static char incoming_data[INCOMING_DATA_BUFFER_SIZE];

static AzIoTSasToken sasToken(
    &client,
    AZ_SPAN_FROM_STR(IOT_CONFIG_DEVICE_KEY),
    AZ_SPAN_FROM_BUFFER(sas_signature_buffer),
    AZ_SPAN_FROM_BUFFER(mqtt_password));

DynamicJsonDocument bodyJSON(1024);


//{"power":1,"mode":1,"temperature":26,"fan":1}
void setACStatus(DynamicJsonDocument bodyJSON){
  
  uint16_t power = bodyJSON["power"];
  uint16_t temp = bodyJSON["temperature"];
  String mode = bodyJSON["mode"];
  uint16_t fan = bodyJSON["fan"];
  uint16_t enabled = bodyJSON["enabled"];
  uint16_t interval = bodyJSON["interval"];
  if (interval > 0){
    TELEMETRY_FREQUENCY_MILLISECS = interval;
  }

  if (enabled > 0){
    ACControl(power, temp, mode, fan);
  } 
}

static void initializeIoTHubClient()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t*)IOT_CONFIG_IOTHUB_FQDN, strlen(IOT_CONFIG_IOTHUB_FQDN)),
          az_span_create((uint8_t*)IOT_CONFIG_DEVICE_ID, strlen(IOT_CONFIG_DEVICE_ID)),
          &options)))
  {
    Serial.println("Failed initializing Azure IoT Hub client");
    return;
  }

  size_t client_id_length;
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Serial.println("Failed getting client id");
    return;
  }

  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Serial.println("Failed to get MQTT clientId, return code");
    return;
  }


  Serial.println("Client ID: " + String(mqtt_client_id));
  Serial.println("Username: " + String(mqtt_username));
}

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
    int i, r;

    case MQTT_EVENT_ERROR:
      Serial.println("MQTT event MQTT_EVENT_ERROR");
      break;
    case MQTT_EVENT_CONNECTED:
      Serial.println("MQTT event MQTT_EVENT_CONNECTED");

      r = esp_mqtt_client_subscribe(mqtt_client, AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC, 1);
      if (r == -1)
      {
        Serial.println("Could not subscribe for cloud-to-device messages.");
      }
      else
      {
        Serial.println("Subscribed for cloud-to-device messages; message id:"  + String(r));
      }

      break;
    case MQTT_EVENT_DISCONNECTED:
      Serial.println("MQTT event MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      Serial.println("MQTT event MQTT_EVENT_SUBSCRIBED");
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      Serial.println("MQTT event MQTT_EVENT_UNSUBSCRIBED");
      break;
    case MQTT_EVENT_PUBLISHED:
      Serial.println("MQTT event MQTT_EVENT_PUBLISHED");
      break;
    case MQTT_EVENT_DATA:
      Serial.println("MQTT event MQTT_EVENT_DATA");

      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->topic_len; i++)
      {
        incoming_data[i] = event->topic[i]; 
      }
      incoming_data[i] = '\0';
      Serial.println("Topic: " + String(incoming_data));
      
      for (i = 0; i < (INCOMING_DATA_BUFFER_SIZE - 1) && i < event->data_len; i++)
      {
        incoming_data[i] = event->data[i]; 
      }
      incoming_data[i] = '\0';

      Serial.println("Data: " + String(incoming_data));
      //int len = event->data_len;
      deserializeJson(bodyJSON, incoming_data, event->data_len);
      setACStatus(bodyJSON);      

      break;
    case MQTT_EVENT_BEFORE_CONNECT:
      Serial.println("MQTT event MQTT_EVENT_BEFORE_CONNECT");
      break;
    default:
      Serial.println("MQTT event UNKNOWN");
      break;
  }

  return ESP_OK;
}

static int initializeMqttClient()
{
  if (sasToken.Generate(SAS_TOKEN_DURATION_IN_MINUTES) != 0)
  {
    Serial.println("Failed generating SAS token");
    return 1;
  }

  esp_mqtt_client_config_t mqtt_config;
  memset(&mqtt_config, 0, sizeof(mqtt_config));
  mqtt_config.uri = mqtt_broker_uri;
  mqtt_config.port = mqtt_port;
  mqtt_config.client_id = mqtt_client_id;
  mqtt_config.username = mqtt_username;

  mqtt_config.password = (const char*)az_span_ptr(sasToken.Get());
  
  mqtt_config.keepalive = 30;
  mqtt_config.disable_clean_session = 0;
  mqtt_config.disable_auto_reconnect = false;
  mqtt_config.event_handle = mqtt_event_handler;
  mqtt_config.user_context = NULL;
  mqtt_config.cert_pem = (const char*)ca_pem;

  mqtt_client = esp_mqtt_client_init(&mqtt_config);

  if (mqtt_client == NULL)
  {
    Serial.println("Failed creating mqtt client");
    return 1;
  }

  esp_err_t start_result = esp_mqtt_client_start(mqtt_client);

  if (start_result != ESP_OK)
  {
    Serial.println("Could not start mqtt client; error code:" + start_result);
    return 1;
  }
  else
  {
    Serial.println("MQTT client started");
    return 0;
  }
}

static void getTelemetryPayload(az_span payload, az_span* out_payload, 
  float humidity, float temp, float heatIndex,
  uint32_t _power, uint32_t _temp, String _mode, uint32_t _fan
  )
{
  Serial.println(humidity);
  Serial.println(temp);
  Serial.println(heatIndex);
  Serial.println(_power);
  Serial.println(_temp);
  //Serial.println(_mode);
  Serial.println(_fan);

  az_span original_payload = payload;
  int factor = 100;

  try {
    payload = az_span_copy(
        payload, AZ_SPAN_FROM_STR("{ \"msgCount\": "));
    (void)az_span_u32toa(payload, telemetry_send_count++, &payload);

    payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"humidity\": "));
    (void)az_span_u32toa(payload, humidity * factor, &payload);

    payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"temperature\": "));
    (void)az_span_u32toa(payload, temp * factor, &payload);

    payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"heatIndex\": "));
    (void)az_span_u32toa(payload, heatIndex * factor, &payload);

    payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"factor\": "));
    (void)az_span_u32toa(payload, factor, &payload);

    // payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"acTemp\": "));
    // (void)az_span_u32toa(payload, _temp, &payload);

    // payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"acMode\": "));
    // (void)az_span_u32toa(payload, _mode, &payload);

    // payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" , \"acFan\": "));
    // (void)az_span_u32toa(payload, _fan, &payload);

    payload = az_span_copy(payload, AZ_SPAN_FROM_STR(" }"));
    payload = az_span_copy_u8(payload, '\0');

    Serial.println("json");

    *out_payload = az_span_slice(original_payload, 0, az_span_size(original_payload) - az_span_size(payload) - 1);
  }
  catch(...){
    *out_payload = original_payload;
  }
}

static void AzIoTSetup()
{
  initializeIoTHubClient();
  (void)initializeMqttClient();
}

static void sendTelemetry(float humidity, float temp, float heatIndex,
  uint32_t _power, uint32_t _temp, String _mode, uint32_t _fan)
{
  az_span telemetry = AZ_SPAN_FROM_BUFFER(telemetry_payload);

  Serial.println("Sending telemetry ...");

  // The topic could be obtained just once during setup,
  // however if properties are used the topic need to be generated again to reflect the
  // current values of the properties.
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Serial.println("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  Serial.println("...");

  getTelemetryPayload(telemetry, &telemetry, humidity, temp, heatIndex, _power, _temp, _mode, _fan);

  Serial.println("...");

  if (esp_mqtt_client_publish(
          mqtt_client,
          telemetry_topic,
          (const char*)az_span_ptr(telemetry),
          az_span_size(telemetry),
          MQTT_QOS1,
          DO_NOT_RETAIN_MSG)
      == 0)
  {
    Serial.println("Failed publishing");
  }
  else
  {
    Serial.println("Message published successfully");
  } 
}