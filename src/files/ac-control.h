#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <ir_Mitsubishi.h>

const uint16_t kIrLed = 4;  // ESP8266 GPIO pin to use. Recommended: 4 (D2).
IRMitsubishi136 ac(kIrLed);  // Set the GPIO used for sending messages.

uint32_t power = 0;
uint32_t temp = 0;
String mode = "";
uint32_t fan = 0;

void ACSetup()
{
  ac.begin();
}

void ACControl(uint16_t _power, uint16_t _temp, String _mode, uint16_t _fan)
{
  power = _power;
  temp = _temp;
  mode = _mode;
  fan = _fan;
  
  Serial.println("ACControl");
  Serial.println(power);
  Serial.println(temp);
  Serial.println(mode);
  Serial.println(fan);

  if (power == 0)
  {
    ac.off();
  }
  else
  {
    ac.on();
  }
  
  if (mode == "cool")
  {
    ac.setMode(kMitsubishi136Cool);
  }
  else if (mode == "dry")
  {
    ac.setMode(kMitsubishi136Dry);
  }
  else if (mode == "fan")
  {
    ac.setMode(kMitsubishi136Fan);
  }
  else if (mode == "heat")
  {
    ac.setMode(kMitsubishi136Heat);
  }
  else if (mode == "auto")
  {
    ac.setMode(kMitsubishi136Auto);
  }
  
  ac.setTemp(temp);
  ac.setFan(fan);
  
  //ac.setVane(kMitsubishi136VaneAuto);

  ac.send();
}