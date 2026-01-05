#pragma once

#include <WiFiClient.h>

class MQTT {
 private:
  WiFiClient wifiClient;
  uint16_t packetId;
  void dumpPacket(byte *ppacket, int length);
  bool sendCONNECT(const char *pclient, int keepAlive);
  bool awaitCONNACK();
  bool sendPUBLISH(const char *ptopic, const void *pdata, int length);
  bool awaitPUBACK();
  bool sendDISCONNECT();

 public:
  MQTT(WiFiClient &wifiClient);
  bool connect(const char *pserver, int port, const char *pclient);
  bool publish(const char *ptopic, const char *pmessage);
  bool disconnect();
};