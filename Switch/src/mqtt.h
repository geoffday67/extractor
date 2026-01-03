#pragma once

#include <WiFiClient.h>

class MQTT {
 private:
  WiFiClient wifiClient;
  uint16_t packetId;
  void dumpPacket(byte *ppacket, int length);
  bool sendCONNECT(char *pclient, int keepAlive);
  bool awaitCONNACK();
  bool sendPUBLISH(char *ptopic, void *pdata, int length);
  bool awaitPUBACK();
  bool sendDISCONNECT();

 public:
  MQTT(WiFiClient &wifiClient);
  bool connect(char *pserver, int port, char *pclient);
  bool publish(char *ptopic, char *pmessage);
  bool disconnect();
};