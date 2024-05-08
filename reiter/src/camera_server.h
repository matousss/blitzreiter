#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include "esp_timer.h"

class CameraServer
{
public:
    esp_err_t sendFrame(WiFiClient &client);
};