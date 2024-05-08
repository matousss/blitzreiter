#include <Arduino.h>
#include <WiFi.h>
#include "cam_pinout.h"
#include <esp_camera.h>
#include <esp_timer.h>
// #include "camera_server.h"

#define WIFI_SSID "Skynet"
#define WIFI_PSK "Genesis23"

void setupWiFi();
void setupServer();

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    if (psramFound())
    {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  }

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
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

  setupWiFi();
  setupServer();
}

constexpr unsigned PORT = 8686;

WiFiServer server(PORT);

constexpr unsigned headLen = 5;

const byte messageHead[headLen] = {0xC5, 0x33, 0xFC, 0x33, 0xFC};
constexpr unsigned messageHeadLen = sizeof(messageHead);
constexpr unsigned messageLen = messageHeadLen + 2 * sizeof(int);

bool checkHead(byte *);

// CameraServer cameraServer = CameraServer();
esp_err_t sendFrame(WiFiClient &client);

void loop()
{
  Serial.println("Waiting for client...");
  WiFiClient client = server.available();

  if (client)
  {
    Serial.println("Client connected!");
    auto buffer = new byte[messageLen];
    while (client.connected())
    {
      client.read(buffer, messageHeadLen);
      if (checkHead(buffer))
      {
        // unsigned test = 5;
        sendFrame(client);
        // byte *message = new byte[headLen + sizeof(unsigned)];
        // memcpy(message, messageHead, messageHeadLen);
        // memcpy(message + messageHeadLen, &test, sizeof(unsigned));
        // client.write(message, headLen + sizeof(unsigned));
        // delete[] message;
      }
      else
        Serial.println("Invalid head");
      delay(10);
    }
    delete[] buffer;
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

void setupServer()
{
  server.begin();
  Serial.print("Listening at: ");
  Serial.print(WiFi.localIP());
  Serial.print(':');
  Serial.println(PORT);
}

esp_err_t sendFrame(WiFiClient &client)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len;
  uint8_t *_jpg_buf;
  char *part_buf[64];
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

  _jpg_buf_len = fb->len;
  _jpg_buf = fb->buf;

  byte *message = new byte[headLen + sizeof(unsigned)];
  memcpy(message, messageHead, messageHeadLen);
  memcpy(message + messageHeadLen, &_jpg_buf_len, sizeof(size_t));
  client.write(message, headLen + sizeof(unsigned));
  delete[] message;

  client.write(_jpg_buf, _jpg_buf_len);

  esp_camera_fb_return(fb);

  // int64_t fr_end = esp_timer_get_time();
  // int64_t frame_time = fr_end - last_frame;
  // last_frame = fr_end;
  // frame_time /= 1000;

  // last_frame = 0;
  return res;
}