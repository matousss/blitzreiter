#include <Arduino.h>
#include <WiFi.h>
#include "cam_config.h"
#include <esp_camera.h>
#include <esp_timer.h>

#define WIFI_SSID "blitzreiter"
#define WIFI_PSK "Genesis23"
#define PORT 8686

// Declarations
void setupWiFi();
void setupServer();

void handleConnection(void *);

// Network
constexpr unsigned headLen = 5;

const byte messageHead[headLen] = {0xC5, 0x33, 0xFC, 0x33, 0xFC};
constexpr unsigned messageHeadLen = sizeof(messageHead);
constexpr unsigned messageLen = messageHeadLen + 2 * sizeof(int);

byte buffer[16];

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config = getCamConfig();

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }

  s->set_brightness(s, 1);

  setupWiFi();
  xTaskCreatePinnedToCore(handleConnection, "handleConnection", 5000, nullptr, 0, nullptr, 1);
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Reconnecting to WiFi");
    setupWiFi();
    delay(500);
  }
  delay(500);
}

void setupWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  WiFi.setSleep(false);

  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print('.');
  }
  Serial.println();
  Serial.println(WiFi.localIP());
}

bool checkHead(byte *buffer)
{
  for (unsigned i = 0; i < headLen; i++)
  {
    if (buffer[i] != messageHead[i])
      return false;
  }
  return true;
}

esp_err_t sendFrame(WiFiClient &client)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t imgBufferLen;
  uint8_t *imgBuffer;
  // char *part_buf[64];
  // static int64_t last_frame = 0;
  // if (!last_frame)
  // {
  //   last_frame = esp_timer_get_time();
  // }

  fb = esp_camera_fb_get();
  if (!fb)
  {
    Serial.println("Camera capture failed");
    res = ESP_FAIL;
    return res;
  }
  Serial.println("Camera captured a frame");

  imgBufferLen = fb->len;
  imgBuffer = fb->buf;

  byte *message = new byte[headLen + sizeof(unsigned)];
  memcpy(message, messageHead, messageHeadLen);
  memcpy(message + messageHeadLen, &imgBufferLen, sizeof(size_t));
  client.write(message, headLen + sizeof(unsigned));
  delete[] message;

  client.write(imgBuffer, imgBufferLen);
  client.flush();
  esp_camera_fb_return(fb);

  // int64_t fr_end = esp_timer_get_time();
  // int64_t frame_time = fr_end - last_frame;
  // last_frame = fr_end;
  // frame_time /= 1000;

  // last_frame = 0;
  return res;
}

void handleConnection(void *)
{
  WiFiClient client;
  for (;;)
  {
    if (!client.connected())
    {
      Serial.println("Waiting for connection...");
      for (;;)
      {
        if (client.connect(WiFi.gatewayIP(), PORT))
        {
          Serial.println("Client connected!");
          break;
        }

        Serial.println("Connection not enstablished");
        delay(100);
      }
    }

    unsigned start = millis();
    while (client.available() < messageHeadLen)
    {
      if (!client.connected() || WiFi.status() != WL_CONNECTED)
      {
        Serial.println("Disconnected while waiting for request");
      }
      if (millis() - start > 1000)
      {
        Serial.println("Request took more than 1000ms (timeout)");
        client.stop();
        break;
      }

      taskYIELD();
    }

    if (client.connected())
    {
      if (client.read(buffer, messageHeadLen) == messageHeadLen && checkHead(buffer))
      {
        memset(buffer + messageHeadLen, 0, sizeof(buffer) - messageHeadLen);
        // int speed, steer;
        // memcpy(&speed, buffer + messageHeadLen, sizeof(int));
        // memcpy(&steer, buffer + messageHeadLen + sizeof(int), sizeof(int));
        // Serial.printf("speed: %d, steer: %d\n", speed, steer);

        sendFrame(client);
      }
      else
        Serial.println("Invalid head");
    }
    delay(10);
  }
}