#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ==================== CONFIG ====================

#define SER2_TX        17
#define SER2_RX        16
#define SER2_BAUDRATE  115200

#define DEBUG_SERIAL   0

static BLEUUID SERVICE_UUID("825aeb6e-7e1d-4973-9c75-30c042c4770c");
static BLEUUID CHARACTERISTIC_UUID_RX("24259347-9d86-4c67-a9ae-84f6a7f0c90d"); // BLE → Serial
static BLEUUID CHARACTERISTIC_UUID_TX("b52e05ac-8a8a-4880-85c7-bd3e6a32dc0e"); // Serial → BLE

// ==================== GLOBALS ====================

BLEServer        *pServer          = nullptr;
BLECharacteristic *pTxCharacteristic = nullptr;

bool BLEConnected     = false;
bool oldBLEConnected  = false;

// FreeRTOS queue for BLE RX bytes
QueueHandle_t bleRxQueue;

// Serial JSON buffer
String serialJson;

// ==================== DEBUG ====================

void debug(const String &msg) {
    if (DEBUG_SERIAL) {
        Serial.println("[BLE Bridge] " + msg);
    }
}

// ==================== BLE CALLBACKS ====================

class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        BLEConnected = true;
    }

    void onDisconnect(BLEServer* pServer) override {
        BLEConnected = false;
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        for (char c : value) {
            xQueueSend(bleRxQueue, &c, 0);   // thread-safe
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

    debug("starting");
    Serial2.println("{\"msg\":\"[BLE Bridge] starting\"}");

    // Create BLE RX queue
    bleRxQueue = xQueueCreate(256, sizeof(char));
    if (!bleRxQueue) {
        debug("BLE RX queue allocation failed!");
    }

    // BLE init
    BLEDevice::init("GRBLHAL");
    BLEDevice::setMTU(247);
    
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID, 30);

    // RX characteristic (BLE → Serial)
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->addDescriptor(new BLE2902());
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    // TX characteristic (Serial → BLE)
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pService->start();
    pServer->getAdvertising()->start();

    debug("advertising");
    Serial2.println("{\"msg\":\"[BLE Bridge] advertising\"}");
}

// ==================== LOOP ====================

void loop() {

    processSerialToBle();
    processBleToSerial();

    // Connection state handling
    if (!BLEConnected && oldBLEConnected) {
        delay(300);
        pServer->startAdvertising();
        oldBLEConnected = BLEConnected;
        debug("client disconnected");
        debug("advertising");
        Serial2.println("{\"msg\":\"[BLE Bridge] advertising\"}");
    }

    if (BLEConnected && !oldBLEConnected) {
        oldBLEConnected = BLEConnected;
        debug("client connected");
    }
}

// ==================== SERIAL → BLE ====================

void processSerialToBle() {

    static String serialBuffer;

    while (Serial2.available()) {
        char c = Serial2.read();

        if (c == '{') {
            serialBuffer = "{";
        }
        else if (c == '}') {
            serialBuffer += '}';
            if (BLEConnected && pTxCharacteristic) {
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
