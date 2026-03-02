#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

uint8_t receiverAddr[] = {0xA4, 0xF0, 0x0F, 0x8E, 0x8F, 0x14};

typedef struct struct_message {
    float moisture;
    float tankDist;
    float solarVolts;
    float solarAmps;
    char pumpStatus[40];
    bool commandWater; 
} struct_message;

struct_message incomingData;
struct_message outboundReply;
unsigned long nextWaterMillis = 0;
const unsigned long WATER_INTERVAL = 30000; // 8 Hours
bool forceWaterOnce = false;

void OnDataRecv(const uint8_t * mac, const uint8_t *data, int len) {
    memcpy(&incomingData, data, sizeof(incomingData));
    
    Serial.println("\n========================================");
    Serial.println("      REMOTE PLANT CHECK-IN REPORT      ");
    Serial.println("========================================");
    Serial.printf(" [SENSORS] Moisture: %.1f%% | Tank: %.1fcm\n", incomingData.moisture, incomingData.tankDist);
    Serial.printf(" [POWER]   Solar: %.2fV | Current: %.2fmA\n", incomingData.solarVolts, incomingData.solarAmps);
    Serial.printf(" [STATUS]  Last Field Status: %s\n", incomingData.pumpStatus);
    
    // Decision Logic
    bool timerReached = (millis() >= nextWaterMillis);
    bool soilDry = (incomingData.moisture < 30.0);

    if (timerReached || soilDry || forceWaterOnce) {
        outboundReply.commandWater = true;
        Serial.println(" [DECISION] >>> COMMAND: TRIGGER PUMP");
        nextWaterMillis = millis() + WATER_INTERVAL;
        forceWaterOnce = false; 
    } else {
        outboundReply.commandWater = false;
        long minsLeft = (nextWaterMillis - millis()) / 60000;
        Serial.printf(" [DECISION] >>> COMMAND: SLEEP (%ld min remaining)\n", minsLeft);
    }

    esp_now_send(receiverAddr, (uint8_t *) &outboundReply, sizeof(outboundReply));
    Serial.println("========================================\n");
}

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    esp_now_init();
    esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddr, 6);
    esp_now_add_peer(&peerInfo);
    nextWaterMillis = millis() + WATER_INTERVAL;
}

void loop() {
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'w') { forceWaterOnce = true; Serial.println("\n[!] Manual Override Queued."); }
    }
}