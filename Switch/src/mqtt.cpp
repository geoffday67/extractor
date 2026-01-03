#include "mqtt.h"

#include <Arduino.h>

MQTT::MQTT(WiFiClient &wifiClient) {
  this->wifiClient = wifiClient;
}

void MQTT::dumpPacket(byte *ppacket, int length) {
  int n;

  Serial.printf("Packet length %d: ", length);
  for (n = 0; n < length; n++) {
    Serial.printf(" 0x%02X", ppacket[n]);
  }
  Serial.println();
}

bool MQTT::sendCONNECT(char *pclient, int keepAlive) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[strlen(pclient) + 32];

  ppacket[0] = 0x10;
  index = 2;
  ppacket[index++] = 0;  // Protocol name
  ppacket[index++] = 4;
  ppacket[index++] = 'M';
  ppacket[index++] = 'Q';
  ppacket[index++] = 'T';
  ppacket[index++] = 'T';
  ppacket[index++] = 4;     // Protocol level = 4 for version 3.1.1
  ppacket[index++] = 0x02;  // Connect flags => clean session
  ppacket[index++] = keepAlive / 256;
  ppacket[index++] = keepAlive % 256;
  n = strlen(pclient);
  ppacket[index++] = n / 256;
  ppacket[index++] = n % 256;
  memcpy(ppacket + index, pclient, n);
  index += n;
  ppacket[1] = index - 2;
  n = wifiClient.write(ppacket, index);
  if (n != index) {
    Serial.printf("Wrong number of bytes sent, %d sent, expecting %d\n", n, index);
    goto exit;
  }
  Serial.println("Connection requested");

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitCONNACK() {
  byte packet[4];
  int n;
  bool result = false;

  n = wifiClient.readBytes(packet, 4);
  if (n != 4 || packet[0] != 0x20 || packet[3] != 0x00) {
    Serial.println("Invalid CONNACK response");
    dumpPacket(packet, n);
    goto exit;
  }
  Serial.println("Connection acknowledged");

  result = true;

exit:
  return result;
}

bool MQTT::sendPUBLISH(char *ptopic, void *pdata, int length) {
  byte *ppacket;
  int index, n;
  bool result = false;

  ppacket = new byte[strlen(ptopic) + length + 32];

  ppacket[0] = 0x32;
  index = 2;
  n = strlen(ptopic);
  ppacket[index++] = n / 256;
  ppacket[index++] = n % 256;
  memcpy(ppacket + index, ptopic, n);
  index += n;

  // Generate (probably) unique ID
  packetId = millis() & 0xFFFF;
  ppacket[index++] = packetId / 256;
  ppacket[index++] = packetId % 256;

  memcpy(ppacket + index, pdata, length);
  index += length;
  ppacket[1] = index - 2;
  n = wifiClient.write(ppacket, index);
  if (n != index) {
    Serial.printf("Wrong number of bytes sent, %d sent, expecting %d\n", n, index);
    goto exit;
  }
  Serial.printf("Message published to %s with ID 0x%04X\n", ptopic, packetId);

  result = true;

exit:
  delete[] ppacket;
  return result;
}

bool MQTT::awaitPUBACK() {
  byte packet[4];
  int n;
  bool result = false;

  n = wifiClient.readBytes(packet, 4);
  if (n != 4 || packet[0] != 0x40 || packet[2] != packetId / 256 || packet[3] != packetId % 256) {
    Serial.println("Invalid PUBACK response");
    dumpPacket(packet, n);
    goto exit;
  }
  Serial.println("Publish acknowledged");

  result = true;

exit:
  return result;
}

bool MQTT::sendDISCONNECT() {
  byte packet[2];
  int index, n;
  bool result = false;

  packet[0] = 0xE0;
  index = 2;
  packet[1] = index - 2;
  n = wifiClient.write(packet, index);
  if (n != index) {
    Serial.printf("Wrong number of bytes sent, %d sent, expecting %d\n", n, index);
    goto exit;
  }
  Serial.println("Disconnected");

  result = true;

exit:
  return result;
}

bool MQTT::connect(char *pserver, int port, char *pclient) {
  int tries;
  bool result = false;

  if (!wifiClient.connect(pserver, port)) {
    Serial.println("Error connecting to broker");
    goto exit;
  }
  Serial.print("Connected to broker at ");
  Serial.println(wifiClient.remoteIP().toString());
  wifiClient.setNoDelay(true);
  wifiClient.setTimeout(5);

  tries = 0;
  while (true) {
    if (sendCONNECT(pclient, 60) && awaitCONNACK()) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  result = true;

exit:
  return result;
}

bool MQTT::publish(char *ptopic, char *pmessage) {
  int tries;
  bool result = false;

  tries = 0;
  while (true) {
    if (sendPUBLISH(ptopic, pmessage, strlen(pmessage)) && awaitPUBACK()) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  result = true;

exit:
  return result;
}

bool MQTT::disconnect() {
  int tries;
  bool result = false;

  tries = 0;
  while (true) {
    if (sendDISCONNECT()) {
      break;
    }
    if (++tries == 3) {
      goto exit;
    }
  }

  result = true;

exit:
  return result;
}