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
    int pumpDurationSec;  
    float tankLimitCm;    
    long waterIntervalMs; 
    int sleepTimeMin;     // Note: We keep the struct name 'Min' for compatibility, 
                          // but we will treat the VALUE as Seconds.
    float moistThreshold; 
} struct_message;

struct_message incoming;
struct_message config;

unsigned long nextWaterMillis = 0; 
bool inMenu = false;
unsigned long menuStartTime = 0;
int lowBatCounter = 0;

void setup() {
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    esp_now_init();
    
    // --- UPDATED DEFAULTS ---
    config.pumpDurationSec = 12;
    config.tankLimitCm = 50.0;          
    config.waterIntervalMs = 3600000;   // 1 Hour default
    config.sleepTimeMin = 10;            // 10 SECONDS default
    config.moistThreshold = 20.0;       
    
    nextWaterMillis = millis() + config.waterIntervalMs;

    esp_now_register_recv_cb(esp_now_recv_cb_t([](const uint8_t *mac, const uint8_t *data, int len) {
        memcpy(&incoming, data, sizeof(incoming));
        
        Serial.println("\n==========================================");
        Serial.println("      FIELD RECEIVER CHECK-IN REPORT      ");
        Serial.println("==========================================");
        Serial.printf(" [TIME]     Current: %lu s | Next Water: %lu s\n", millis()/1000, nextWaterMillis/1000);
        Serial.printf(" [SENSORS]  Moisture: %.1f%%  | Tank Dist: %.1f cm\n", incoming.moisture, incoming.tankDist);
        Serial.printf(" [POWER]    Solar: %.2f V    | Current: %.2f mA\n", incoming.solarVolts, incoming.solarAmps);
        
        // Battery Health Warning
        if (incoming.solarVolts < 3.7) lowBatCounter++;
        else lowBatCounter = 0;
        if (lowBatCounter >= 3) Serial.println(" [!] WARNING: Battery voltage low for 3 cycles!");

        // Trigger Logic
        bool timerTrigger = (millis() >= nextWaterMillis);
        bool moistureTrigger = (incoming.moisture < config.moistThreshold);

        if (timerTrigger || moistureTrigger) {
            if (incoming.tankDist < config.tankLimitCm) {
                config.commandWater = true;
                nextWaterMillis = millis() + config.waterIntervalMs;
                Serial.println(" [ACTION]   >>> COMMAND: WATERING DISPATCHED");
            } else {
                config.commandWater = false;
                Serial.println(" [ACTION]   >>> PUMP BLOCKED: Tank safety limit hit.");
            }
        } else {
            config.commandWater = false;
            Serial.printf(" [ACTION]   >>> STANDBY: %ld sec to next cycle\n", (nextWaterMillis - millis())/1000);
        }

        esp_now_send(receiverAddr, (uint8_t *) &config, sizeof(config));
        Serial.println("==========================================\n");
    }));

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverAddr, 6);
    esp_now_add_peer(&peerInfo);
}

void showSettings() {
    Serial.println("\n--- CURRENT CONFIGURATION ---");
    Serial.printf(" 1. Pump Time   : %d sec\n", config.pumpDurationSec);
    Serial.printf(" 2. Tank Limit  : %.1f cm\n", config.tankLimitCm);
    Serial.printf(" 3. Interval    : %.2f hours\n", (float)config.waterIntervalMs / 3600000.0);
    Serial.printf(" 4. Sleep Time  : %d SECONDS\n", config.sleepTimeMin);
    Serial.printf(" 5. Moist Target: %.1f%%\n", config.moistThreshold);
    Serial.println("-----------------------------");
}

void loop() {
    if (inMenu && (millis() - menuStartTime > 30000)) { inMenu = false; Serial.println("\n[Menu Timeout]"); }

    if (Serial.available()) {
        char c = Serial.read();
        if (!inMenu) {
            if (c == 's') showSettings();
            if (c == 'w') { nextWaterMillis = millis(); Serial.println("\n[Manual Override Queued]"); }
            if (c == 'm') {
                if ((nextWaterMillis - millis()) < 60000 && (nextWaterMillis - millis()) > 0) {
                    Serial.println("\n[!] Lockout: Active watering starts in < 1 min.");
                } else {
                    inMenu = true;
                    menuStartTime = millis();
                    Serial.println("\nENTER CHOICE (1-5): ");
                }
            }
        } else {
            int choice = String(c).toInt();
            Serial.print("Enter Value: ");
            while(!Serial.available()) { if(millis()-menuStartTime > 30000) return; }
            float val = Serial.parseFloat();
            
            if (choice == 3) { // Interval validation (val is in Hours)
                if ((val * 3600) < config.sleepTimeMin) {
                    Serial.printf("\n[!] Error: Interval (%.0f s) cannot be smaller than Sleep (%d s)!\n", val*3600, config.sleepTimeMin);
                } else {
                    config.waterIntervalMs = (long)(val * 3600000);
                    nextWaterMillis = millis() + config.waterIntervalMs;
                    Serial.println("Accepted.");
                }
            } 
            else if (choice == 4) { // Sleep validation (val is in Seconds)
                if (val > (float)config.waterIntervalMs / 1000.0) {
                    Serial.println("\n[!] Error: Sleep Time cannot be longer than Watering Interval!");
                } else {
                    config.sleepTimeMin = (int)val;
                    Serial.println("Accepted.");
                }
            }
            else {
                if (choice == 1) config.pumpDurationSec = (int)val;
                if (choice == 2) config.tankLimitCm = val;
                if (choice == 5) config.moistThreshold = val;
                Serial.println("Accepted.");
            }
            inMenu = false;
        }
    }
}