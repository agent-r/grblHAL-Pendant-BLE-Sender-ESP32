#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_gatt_common_api.h>

// ==================== CONFIG ====================

#define SER2_TX        17
#define SER2_RX        16
#define SER2_BAUDRATE  115200

#define DEBUG_SERIAL   0
#define BLE_MTU_SIZE   247
#define BLE_READY_DELAY_MS 300

static BLEUUID SERVICE_UUID("825aeb6e-7e1d-4973-9c75-30c042c4770c");
static BLEUUID CHARACTERISTIC_UUID_RX("24259347-9d86-4c67-a9ae-84f6a7f0c90d"); // BLE → Serial
static BLEUUID CHARACTERISTIC_UUID_TX("b52e05ac-8a8a-4880-85c7-bd3e6a32dc0e"); // Serial → BLE

// ==================== GLOBALS ====================

BLEServer*         pServer           = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;

QueueHandle_t bleRxQueue;

bool     bleConnected     = false;
bool     oldBleConnected  = false;
uint32_t bleConnectTime  = 0;

// ==================== DEBUG ====================

void debug(const String& msg) {
    if (DEBUG_SERIAL) {
        Serial.println("[BLE BRIDGE] " + msg);
    }
}

// ==================== BLE CALLBACKS ====================

class ServerCallbacks : public BLEServerCallbacks {

    void onConnect(BLEServer* server) override {
        bleConnected    = true;
        bleConnectTime  = millis();

        // MTU erneut erzwingen (sehr wichtig bei Reconnects)
        esp_ble_gatt_set_local_mtu(BLE_MTU_SIZE);

        // TX-Characteristic resetten (CCCD-State!)
        if (pTxCharacteristic) {
            pTxCharacteristic->setValue("");
        }

        debug("client connected");
    }

    void onDisconnect(BLEServer* server) override {
        bleConnected = false;
        debug("client disconnected");
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {

    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        for (char c : value) {
            xQueueSend(bleRxQueue, &c, 0);
        }
    }
};

// ==================== SETUP ====================

void setup() {

    if (DEBUG_SERIAL) {
        Serial.begin(115200);
        delay(200);
    }

    Serial2.begin(SER2_BAUDRATE, SERIAL_8N1, SER2_RX, SER2_TX);
    Serial2.println("{\"msg\":\"[BLE BRIDGE] starting\"}");

    // Queue für BLE → Serial
    bleRxQueue = xQueueCreate(256, sizeof(char));

    // BLE Init
    BLEDevice::init("GRBLHAL");
    BLEDevice::setMTU(BLE_MTU_SIZE);

    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    BLEService* service = pServer->createService(SERVICE_UUID, 30);

    // RX (BLE → Serial)
    BLECharacteristic* rxChar = service->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    rxChar->addDescriptor(new BLE2902());
    rxChar->setCallbacks(new RxCallbacks());

    // TX (Serial → BLE)
    pTxCharacteristic = service->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    service->start();

    pServer->getAdvertising()->start();
    Serial2.println("{\"msg\":\"[BLE BRIDGE] advertising\"}");
}

// ==================== LOOP ====================

void loop() {

    processSerialToBle();
    processBleToSerial();

    // Re-Advertising nach Disconnect
    if (!bleConnected && oldBleConnected) {
        delay(300);
        pServer->startAdvertising();
        Serial2.println("{\"msg\":\"[BLE BRIDGE] advertising\"}");
    }

    oldBleConnected = bleConnected;
}

// ==================== SERIAL → BLE ====================

void processSerialToBle() {

    static String serialBuffer;

    // Sendesperre nach Connect (MTU-Aushandlung!)
    bool bleReady =
        bleConnected &&
        (millis() - bleConnectTime > BLE_READY_DELAY_MS);

    while (Serial2.available()) {
        char c = Serial2.read();

        if (c == '{') {
            serialBuffer = "{";
        }
        else if (c == '}') {
            serialBuffer += '}';

            if (bleReady && pTxCharacteristic) {
                pTxCharacteristic->setValue(serialBuffer.c_str());
                pTxCharacteristic->notify();
            }

            debug(serialBuffer);
            serialBuffer = "";
        }
        else if (serialBuffer.length() > 0) {
            serialBuffer += c;
        }
    }
}

// ==================== BLE → SERIAL ====================

void processBleToSerial() {

    static String bleBuffer;
    char c;

    while (xQueueReceive(bleRxQueue, &c, 0) == pdTRUE) {

        if (c == '{') {
            bleBuffer = "{";
        }
        else if (c == '}') {
            bleBuffer += '}';
            Serial2.println(bleBuffer);
            debug(bleBuffer);
            bleBuffer = "";
        }
        else if (bleBuffer.length() > 0) {
            bleBuffer += c;
        }
    }
}