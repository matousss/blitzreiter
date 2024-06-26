#include <Arduino.h>
#include <JPEGDEC.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "Joystick.h"

#define PIN_X 1
#define PIN_Y 2

#define SCREEN_ORIENTATION 3
#define SCREEN_PIN_LED 7

#define WIFI_SSID "blitzreiter"
#define WIFI_PSK "Genesis23"
#define PORT 8686

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

// Declarations

void setupScreen();
void setupWiFi();
void setupServer();
void draw(void *);
void handleVideo(void *);

// Network
const IPAddress local_ip(192, 168, 1, 1);
const IPAddress gateway(192, 168, 1, 1);
const IPAddress subnet(255, 255, 255, 0);

WiFiServer server(PORT);

const byte messageControl[] = {0xC5, 0x33, 0xFC, 0x33, 0xFC};
constexpr unsigned messageControlLen = sizeof(messageControl);
constexpr unsigned messageHeadLen = messageControlLen + 1;
constexpr unsigned messageDriveLen = messageHeadLen + 2 * sizeof(int);
byte buffer[16]; // for storing message heads

// Components
TFT_eSPI tft = TFT_eSPI();
Joystick joystick = Joystick(PIN_X, PIN_Y);

// Joystick
constexpr int TREASHOLD = 15;
std::pair<int, int> lastReading = std::make_pair(0, 0);

// JPEG
JPEGDEC jpeg;
struct JPEGBlock
{
  uint16_t pPixels[20000];
  int x;
  int y;
  int w;
  int h;
};

JPEGBlock *jpegBlock = nullptr;
volatile bool doDrawing = false;

void setup()
{
  Serial.begin(115200);
  tft.init();
  setupScreen();
  setupWiFi();

  jpegBlock = new JPEGBlock;
  xTaskCreatePinnedToCore(draw, "draw", 5000, nullptr, 0, nullptr, 0);
  xTaskCreatePinnedToCore(handleVideo, "handleVideo", 5000, nullptr, 0, nullptr, 1);

  setupServer();

  joystick.init();
}

void loop()
{

  delay(500);
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
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_ip, gateway, subnet);
  WiFi.softAP(WIFI_SSID, WIFI_PSK);
  WiFi.setSleep(false);

  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  tft.print("SSID: ");
  tft.println(WIFI_SSID);

  // while (WiFi.status() != WL_CONNECTED)
  // {
  //   delay(1000);
  //   Serial.print('.');
  //   tft.print('.');
  // }
  Serial.println();
  tft.println();
}

void setupServer()
{
  server.begin();
  Serial.print("Listening at: ");
  Serial.print(WiFi.softAPIP());
  Serial.print(':');
  Serial.println(PORT);
}

bool checkHead(byte *buffer)
{
  for (unsigned i = 0; i < messageControlLen; i++)
  {
    if (buffer[i] != messageControl[i])
      return false;
  }
  return true;
}

// jpeg decoding callback, waits for current drawing to end then copies block to `jpegBlock` and enables drawing
int drawCallback(JPEGDRAW *pDraw)
{
  for (;;)
  {
    if (doDrawing)
    {
      taskYIELD();
      continue;
    }

    memcpy(jpegBlock->pPixels, pDraw->pPixels, pDraw->iWidth * pDraw->iHeight * sizeof(uint16_t));
    jpegBlock->x = pDraw->x;
    jpegBlock->y = pDraw->y;
    jpegBlock->w = pDraw->iWidth;
    jpegBlock->h = pDraw->iHeight;
    doDrawing = true;
    return 1;
  }
}

// threded task - waits for `doDrawing` which indicates new data in `jpegBlock` then show it on display
void draw(void *)
{
  for (;;)
  {
    if (doDrawing)
    {
      tft.pushImage(jpegBlock->x, jpegBlock->y, jpegBlock->w, jpegBlock->h, jpegBlock->pPixels);

      doDrawing = false;
    }
  }
}

void fetchFrame(WiFiClient &client)
{
  memcpy(buffer, &messageControl, messageControlLen);
  buffer[messageControlLen] = 1;
  Serial.println("Requesting frame");
  client.write(buffer, messageHeadLen);
  client.flush();
  memset(buffer, 0, sizeof(buffer));

  unsigned long start = millis();
  Serial.println("Waiting for response");
  while (client.available() < messageControlLen + 4)
  {
    taskYIELD();
    if (millis() - start > 1000)
    {
      Serial.println("Never got response like from your crush (response timeout)");
      client.stop();
      return;
    }
  }

  client.read(buffer, messageControlLen + 4);
  if (checkHead(buffer))
  {
    unsigned len = 0;

    memcpy(&len, buffer + messageControlLen, 4);

    memset(buffer + messageControlLen, 0, sizeof(buffer) - messageControlLen);

    if (len > 10000)
    {
      Serial.println("Received len bigger than 10000 - that must be corruption >:(");
      while (client.read(buffer, 1))
      {
        // read bytes until empty
      };
      delay(100);
      return;
    }

    Serial.printf("Waiting for: %u bytes\n", len);
    delay(20);
    auto imgBuffer = new u_int8_t[len];
    int read = 0;
    unsigned counter = 0;
    while (read < len)
    {
      unsigned chunkLen = client.read(imgBuffer + read, len - read);
      if (chunkLen == 0)
      {
        if (counter++ == 200)
        {
          counter = 0;
          len = 0;
          delay(100);
          break;
        }
      }
      else
      {
        counter = 0;
      }
      read += chunkLen;
    }
    if (len > 0)
    {

      Serial.println("Received data");
      if (jpeg.openRAM(imgBuffer, len, drawCallback))
      {
        jpeg.setPixelType(RGB565_BIG_ENDIAN);

        if (jpeg.decode(0, 0, 1))
        {
          Serial.println("Decoded!");
        }
        else
        {
          Serial.println("Could not decode jpeg");
        }
      }
      else
      {
        Serial.println("Could not open jpeg");
      }
    }
    else
    {
      Serial.println("Received no or incomplete data");
    }
    delete[] imgBuffer;
  }
  else
  {
    Serial.println("Invalid head");
    while (client.read(buffer, 1))
    {
      // read bytes until empty
    };
    memset(buffer, 0, sizeof(buffer));
  }
}

void waitForClient(WiFiClient &client)
{
  unsigned start = millis();
  while (!client.connected())
  {
    if (millis() - start > 500)
    {
      Serial.println("Waiting for client");
      tft.fillRect(0, 0, 320, 16, TFT_RED);
      tft.setCursor(0, 0);
      tft.println("Waiting for connection...");
      start = millis();
    }
    client = server.available();
    delay(100);
  }
}

void createCommandMessage(byte *buffer)
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

  xy.first = -(xy.first > 0 ? min(xy.first, 2048) : max(xy.first, -2048));
  xy.second = xy.second > 0 ? min(xy.second, 2048) : max(xy.second, -2048);

  xy.first = (xy.first / 100.0) * 100;
  xy.second = (xy.second / 100.0) * 100;

  int speed = -map(xy.second, -2048, 2048, -255, 255);
  int steer = map(xy.first, -2048, 2048, -255, 255);

  Serial.printf("speed: %d, steer: %d\n", speed, steer);

  memcpy(buffer, &messageControl, messageControlLen);
  buffer[messageControlLen] = 0;
  memcpy(buffer + messageControlLen + 1, &speed, sizeof(int));
  memcpy(buffer + messageControlLen + 1 + sizeof(int), &steer, sizeof(int));
}

// thread task to request and decode frame
void handleVideo(void *)
{
  WiFiClient client;
  for (;;)
  {
    waitForClient(client);
    if (client.connected())
    {
      fetchFrame(client);
      createCommandMessage(buffer);
      client.write(buffer, messageDriveLen);
      client.flush();
    }
    else
    {
      Serial.println("Client disconnected");
      delay(100);
    }
  }
}
