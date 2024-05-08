#include <Arduino.h>
#include <JPEGDEC.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>

#include "Joystick.h"

#define PIN_X 1
#define PIN_Y 2
// #define PIN_SW 21

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SCREEN_ORIENTATION 3
#define SCREEN_PIN_LED 7

#define WIFI_SSID "Skynet"
#define WIFI_PSK "Genesis23"
// #define REITER_IP "192.168.1.171"

TFT_eSPI tft = TFT_eSPI();
Joystick joystick = Joystick(PIN_X, PIN_Y);

constexpr unsigned PORT = 8686;

WiFiServer server(PORT);

void setupScreen();
void setupWiFi();
void setupServer();

bool checkHead(byte *);
void createMessage(byte *);

constexpr unsigned headLen = 5;

const byte messageHead[headLen] = {0xC5, 0x33, 0xFC, 0x33, 0xFC};
constexpr unsigned messageHeadLen = sizeof(messageHead);
constexpr unsigned messageLen = messageHeadLen + 2 * sizeof(int);

JPEGDEC jpeg;

void setup()
{
  Serial.begin(115200);
  joystick.init();
  tft.init();
  setupScreen();
  setupWiFi();
  setupServer();
}

void loop()
{
  WiFiClient client = server.available();

  if (client)
  {
    tft.println("Client connected!");
    auto buffer = new byte[messageLen];
    while (client.connected())
    {
      client.read(buffer, messageHeadLen);
      if (checkHead(buffer))
      {
        createMessage(buffer);
        client.write(buffer, messageLen);
      }

      delay(10);
    }
    delete[] buffer;
  }
}

void setupScreen()
{
  pinMode(SCREEN_PIN_LED, OUTPUT);
  digitalWrite(SCREEN_PIN_LED, HIGH);

  // for (int i = 0; i < 3; i++)
  // {
  //   tft.fillScreen(TFT_RED);
  //   delay(1000);
  //   tft.fillScreen(TFT_GREEN);
  //   delay(1000);
  //   tft.fillScreen(TFT_BLUE);
  //   delay(1000);
  // }

  tft.setRotation(SCREEN_ORIENTATION);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.fillScreen(TFT_BLACK);
}

void setupWiFi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  WiFi.setSleep(false);

  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);
  tft.print("Connecting to: ");
  tft.println(WIFI_SSID);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print('.');
    tft.print('.');
  }
  Serial.println();
  tft.println();
}

void setupServer()
{
  server.begin();
  Serial.print("Listening at: ");
  Serial.print(WiFi.localIP());
  Serial.print(':');
  Serial.println(PORT);
}

constexpr int TREASHOLD = 15;
std::pair<int, int> lastReading = std::make_pair(0, 0);

void createMessage(byte *buffer)
{
  std::pair<int, int> xy = joystick.read();

  if (abs(xy.first - lastReading.first) < TREASHOLD && abs(xy.second - lastReading.second) < TREASHOLD)
  {
    xy = lastReading;
  }
  else
  {
    lastReading = xy;
  }
  Serial.print("x: ");
  Serial.print(xy.first);
  Serial.print(", Y: ");
  Serial.print(xy.second);
  Serial.println();

  xy.first = -(xy.first > 0 ? min(xy.first, 2048) : max(xy.first, -2048));
  xy.second = xy.second > 0 ? min(xy.second, 2048) : max(xy.second, -2048);

  int speed = map(xy.second, -2048, 2048, -255, 255);
  int steer = map(xy.first, -2048, 2048, -255, 255);

  // memcpy(buffer, &messageHead, messageHeadLen);
  memcpy(buffer + messageHeadLen, &speed, sizeof(int));
  memcpy(buffer + messageHeadLen + sizeof(int), &steer, sizeof(int));

  Serial.println("Sent");
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