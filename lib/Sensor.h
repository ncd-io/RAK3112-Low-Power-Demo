#ifndef SENSOR_H
#define SENSOR_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>

// Read sensor data and return as JSON string
String readSensor();

#endif
