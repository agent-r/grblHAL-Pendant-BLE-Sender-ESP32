#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// -------------------- Serial / BLE --------------------
#define SER2_TX 17
#define SER2_RX 16
#define SER2_BAUDRATE 115200

static BLEUUID SERVICE_UUID("825aeb6e-7e1d-4973-9c75-30c042c4770c");
static BLEUUID CHARACTERISTIC_UUID_RX("24259347-9d86-4c67-a9ae-84f6a7f0c90d");
static BLEUUID CHARACTERISTIC_UUID_TX("b52e05ac-8a8a-4880-85c7-bd3e6a32dc0e");

BLEServer *pServer = nullptr;
BLECharacteristic *pTxCharacteristic;

bool BLEConnected = false;
bool oldBLEConnected = false;

String rxBuffer = ""; // Puffer für eingehende JSONs über BLE
String serialBuffer = ""; // Puffer für eingehende JSONs über Serial2

// -------------------- BLE Callbacks --------------------
class MyServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override { BLEConnected = true; }
    void onDisconnect(BLEServer* pServer) override { BLEConnected = false; }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) override {
        std::string val = pCharacteristic->getValue();
        for (char c : val) rxBuffer += c; // Einfach an den RX-Puffer anhängen
    }
};

// -------------------- Setup --------------------
void setup() {
    Serial2.begin(SER2_BAUDRATE, SERIAL_8N1, SER2_RX, SER2_TX);
    Serial2.println("{\"msg\":\"[BLE] starting BLE\"}");

    BLEDevice::init("GRBLHAL");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    BLEService *pService = pServer->createService(SERVICE_UUID, 30);
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->addDescriptor(new BLE2902());
    pRxCharacteristic->setCallbacks(new RxCallbacks());

    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());

    pService->start();
    pServer->getAdvertising()->start();
    Serial2.println("{\"msg\":\"[BLE] start advertising\"}");
}

// -------------------- Loop --------------------
void loop() {
    // Serial → BLE
    processSerialBuffer();

    // BLE → Serial
    processRxBuffer();

    // BLE Verbindung überwachen
    if (!BLEConnected && oldBLEConnected) {
        delay(500);
        pServer->startAdvertising();
        oldBLEConnected = BLEConnected;
        Serial2.println("{\"msg\":\"[BLE] client disconnected\"}");
    }
    if (BLEConnected && !oldBLEConnected) {
        oldBLEConnected = BLEConnected;
        Serial2.println("{\"msg\":\"[BLE] client connected\"}");
    }
}

// -------------------- Serial → BLE --------------------
void processSerialBuffer() {
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '{') serialBuffer = "{";  // Beginn JSON
        else if (c == '}') {               // Ende JSON
            serialBuffer += '}';
            if (BLEConnected && pTxCharacteristic != nullptr) {
                pTxCharacteristic->setValue(serialBuffer.c_str());
                pTxCharacteristic->notify();
            }
            serialBuffer = "";
        } else if (serialBuffer.length() > 0) {
            serialBuffer += c;
        }
    }
}

// -------------------- BLE → Serial --------------------
void processRxBuffer() {
    for (size_t i = 0; i < rxBuffer.length(); i++) {
        char c = rxBuffer[i];
        if (c == '{') serialBuffer = "{";  // Beginn JSON
        else if (c == '}') {               // Ende JSON
            serialBuffer += '}';
            Serial2.println(serialBuffer);
            serialBuffer = "";
        } else if (serialBuffer.length() > 0 || c == '{') {
            serialBuffer += c;
        }
    }
    rxBuffer = ""; // Alles verarbeitet
}
