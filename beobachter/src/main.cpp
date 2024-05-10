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

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

const IPAddress local_ip(192, 168, 1, 1);
const IPAddress gateway(192, 168, 1, 1);
const IPAddress subnet(255, 255, 255, 0);

TFT_eSPI tft = TFT_eSPI();
Joystick joystick = Joystick(PIN_X, PIN_Y);

constexpr unsigned PORT = 8686;

WiFiServer server(PORT);

void setupScreen();
void setupWiFi();
void setupServer();

bool checkHead(byte *);
void createMessage(byte *);

const byte messageHead[] = {0xC5, 0x33, 0xFC, 0x33, 0xFC};
constexpr unsigned messageHeadLen = sizeof(messageHead);
constexpr unsigned messageLen = messageHeadLen + 2 * sizeof(int);

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
byte buffer[16];
WiFiClient client;

volatile bool doDrawing = false;

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

void fetchFrame(void *)
{
  for (;;)
  {
    if (client.connected())
    {
      memcpy(buffer, &messageHead, messageHeadLen);
      client.write(buffer, messageHeadLen);
      client.flush();
      memset(buffer, 0, sizeof(buffer));

      while (client.available() < messageHeadLen + 4)
      {
        taskYIELD();
      }

      client.read(buffer, messageHeadLen + 4);
      if (checkHead(buffer))
      {
        unsigned len = 0;

        memcpy(&len, buffer + messageHeadLen, 4);

        memset(buffer + messageHeadLen, 0, sizeof(buffer) - messageHeadLen);

        if (len > 10000)
        {
          Serial.println("Received len bigger than 10000 - that must be corruption >:(");
          while (client.read(buffer, 1))
          {
            // read bytes until empty
          };
          delay(100);
          continue;
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
              Serial.println("No data received");
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
    else
      delay(100);
  }
}

void setup()
{
  Serial.begin(115200);
  joystick.init();
  tft.init();
  setupScreen();
  setupWiFi();
  xTaskCreatePinnedToCore(draw, "draw", 5000, nullptr, 0, nullptr, 0);
  xTaskCreatePinnedToCore(fetchFrame, "fetchFrame", 5000, nullptr, 0, nullptr, 1);
  setupServer();
  jpegBlock = new JPEGBlock;
}

void loop()
{
  // WiFiClient client = server.available();

  // if (client)
  // {
  //   tft.println("Client connected!");
  //   auto buffer = new byte[messageLen];
  //   while (client.connected())
  //   {
  //     client.read(buffer, messageHeadLen);
  //     if (checkHead(buffer))
  //     {
  //       createMessage(buffer);
  //       client.write(buffer, messageLen);
  //       Serial.println("Sent");
  //     }

  //     delay(10);
  //   }
  //   delete[] buffer;
  // }
  if (!client.connected())
  {
    wifi_sta_list_t stations;
    tcpip_adapter_sta_list_t stationsAdapters;
    esp_wifi_ap_get_sta_list(&stations);
    tcpip_adapter_get_sta_list(&stations, &stationsAdapters);

    if (stationsAdapters.num > 0)
    {
      Serial.println("-----------");
      for (unsigned i = 0; i < stationsAdapters.num; i++)
      {
        tcpip_adapter_sta_info_t station = stationsAdapters.sta[i];
        Serial.print((String) "[+] Device " + i + " | MAC : ");
        Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", station.mac[0], station.mac[1], station.mac[2], station.mac[3], station.mac[4], station.mac[5]);
        ip4_addr_t stationIP;
        stationIP.addr = station.ip.addr;
        Serial.println((String) " | IP " + ip4addr_ntoa(&stationIP));
      }

      for (unsigned i = 0; i < stationsAdapters.num; i++)
      {
        ip4_addr_t stationIP;
        stationIP.addr = stationsAdapters.sta[i].ip.addr;
        Serial.println((String) "Trying :" + ip4addr_ntoa(&stationIP));

        if (client.connect(ip4addr_ntoa(&stationIP), PORT))
        {
          Serial.println("Client disconnected");
          break;
        }
      }
    }
  }

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

  memcpy(buffer, &messageHead, messageHeadLen);
  memcpy(buffer + messageHeadLen, &speed, sizeof(int));
  memcpy(buffer + messageHeadLen + sizeof(int), &steer, sizeof(int));
}

bool checkHead(byte *buffer)
{
  for (unsigned i = 0; i < messageHeadLen; i++)
  {
    if (buffer[i] != messageHead[i])
      return false;
  }
  return true;
}