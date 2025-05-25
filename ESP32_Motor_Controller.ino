#include <AccelStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

#define PULSE_PIN 26
#define DIR_PIN 27
#define ENABLE_PIN 25
#define LED_PIN 2

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

AccelStepper stepper(AccelStepper::DRIVER, PULSE_PIN, DIR_PIN);
Preferences preferences;

struct MotorConfig {
  int microSteps = 1600;
  float ballScrewPitch = 5.0;  // mm per revolution
  float acceleration = 50.0;   // mm/s²
  float maxVelocity = 100.0;   // mm/s
  float position1 = 0.0;       // mm
  float position2 = 50.0;      // mm
  bool invertPulse = true;
  bool invertDir = true;
  bool invertEnable = true;
} config;

bool motorRunning = false;
bool deviceConnected = false;
bool oldDeviceConnected = false;
unsigned long lastBlinkTime = 0;
int blinkCount = 0;
int blinkPattern = 0;
bool ledState = false;

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

void processCommand(std::string command);

// Conversion functions between millimeters and steps
float stepsPerMm() {
  return config.microSteps / config.ballScrewPitch;
}

long mmToSteps(float mm) {
  return (long)(mm * stepsPerMm());
}

float stepsToMm(long steps) {
  return steps / stepsPerMm();
}

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rawValue = pCharacteristic->getValue();
      std::string value = std::string(rawValue.c_str());
      if (value.length() > 0) {
        blinkPattern = 3;
        blinkCount = 0;
        processCommand(value);
      }
    }
};

void processCommand(std::string command) {
  Serial.print("Received command: ");
  Serial.println(command.c_str());
  
  if (command == "START") {
    motorRunning = true;
    digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);
    Serial.println("Motor started");
  } else if (command == "STOP") {
    motorRunning = false;
    digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
    Serial.println("Motor stopped");
  } else if (command.find("MICROSTEPS:") == 0) {
    config.microSteps = atoi(command.substr(11).c_str());
    Serial.print("Microsteps set to: ");
    Serial.println(config.microSteps);
    // Update stepper parameters with new steps/mm ratio
    stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
    stepper.setAcceleration(mmToSteps(config.acceleration));
    saveConfig();
  } else if (command.find("PITCH:") == 0) {
    config.ballScrewPitch = atof(command.substr(6).c_str());
    Serial.print("Ball screw pitch set to: ");
    Serial.print(config.ballScrewPitch);
    Serial.println(" mm/rev");
    // Update stepper parameters with new steps/mm ratio
    stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
    stepper.setAcceleration(mmToSteps(config.acceleration));
    saveConfig();
  } else if (command.find("ACCEL:") == 0) {
    config.acceleration = atof(command.substr(6).c_str());
    stepper.setAcceleration(mmToSteps(config.acceleration));
    Serial.print("Acceleration set to: ");
    Serial.print(config.acceleration);
    Serial.println(" mm/s²");
    saveConfig();
  } else if (command.find("VELOCITY:") == 0) {
    config.maxVelocity = atof(command.substr(9).c_str());
    stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
    Serial.print("Max velocity set to: ");
    Serial.print(config.maxVelocity);
    Serial.println(" mm/s");
    saveConfig();
  } else if (command.find("POS1:") == 0) {
    config.position1 = atof(command.substr(5).c_str());
    Serial.print("Position 1 set to: ");
    Serial.print(config.position1);
    Serial.println(" mm");
    saveConfig();
  } else if (command.find("POS2:") == 0) {
    config.position2 = atof(command.substr(5).c_str());
    Serial.print("Position 2 set to: ");
    Serial.print(config.position2);
    Serial.println(" mm");
    saveConfig();
  } else if (command.find("INVERTPULSE:") == 0) {
    config.invertPulse = command.substr(12) == "1" || command.substr(12) == "true";
    stepper.setPinsInverted(config.invertDir, config.invertPulse, config.invertEnable);
    Serial.print("Pulse pin inverted: ");
    Serial.println(config.invertPulse ? "true" : "false");
    saveConfig();
  } else if (command.find("INVERTDIR:") == 0) {
    config.invertDir = command.substr(10) == "1" || command.substr(10) == "true";
    stepper.setPinsInverted(config.invertDir, config.invertPulse, config.invertEnable);
    Serial.print("Direction pin inverted: ");
    Serial.println(config.invertDir ? "true" : "false");
    saveConfig();
  } else if (command.find("INVERTENABLE:") == 0) {
    config.invertEnable = command.substr(13) == "1" || command.substr(13) == "true";
    stepper.setPinsInverted(config.invertDir, config.invertPulse, config.invertEnable);
    digitalWrite(ENABLE_PIN, motorRunning ? (config.invertEnable ? HIGH : LOW) : (config.invertEnable ? LOW : HIGH));
    Serial.print("Enable pin inverted: ");
    Serial.println(config.invertEnable ? "true" : "false");
    saveConfig();
  } else if (command == "CONFIG") {
    String configData = getConfigString();
    if (deviceConnected && pCharacteristic) {
      pCharacteristic->setValue(configData.c_str());
      pCharacteristic->notify();
      Serial.println("Configuration sent via Bluetooth");
    }
    printConfig();
  }
}

void processSerialCommand() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    blinkPattern = 3;
    blinkCount = 0;
    processCommand(command.c_str());
  }
}

void saveConfig() {
  preferences.begin("motor-config", false);
  preferences.putInt("microSteps", config.microSteps);
  preferences.putFloat("pitch", config.ballScrewPitch);
  preferences.putFloat("accel", config.acceleration);
  preferences.putFloat("velocity", config.maxVelocity);
  preferences.putFloat("pos1", config.position1);
  preferences.putFloat("pos2", config.position2);
  preferences.putBool("invPulse", config.invertPulse);
  preferences.putBool("invDir", config.invertDir);
  preferences.putBool("invEnable", config.invertEnable);
  preferences.end();
  Serial.println("Configuration saved to flash");
}

void loadConfig() {
  preferences.begin("motor-config", true);  // true = read-only mode
  config.microSteps = preferences.getInt("microSteps", 1600);
  config.ballScrewPitch = preferences.getFloat("pitch", 5.0);
  config.acceleration = preferences.getFloat("accel", 50.0);
  config.maxVelocity = preferences.getFloat("velocity", 100.0);
  config.position1 = preferences.getFloat("pos1", 0.0);
  config.position2 = preferences.getFloat("pos2", 50.0);
  config.invertPulse = preferences.getBool("invPulse", true);
  config.invertDir = preferences.getBool("invDir", true);
  config.invertEnable = preferences.getBool("invEnable", true);
  preferences.end();
  Serial.println("Configuration loaded from flash");
}

void updateLED() {
  unsigned long currentTime = millis();
  
  if (blinkPattern > 0) {
    if (currentTime - lastBlinkTime > 100) {
      lastBlinkTime = currentTime;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
      
      if (ledState == LOW) {
        blinkCount++;
        if (blinkCount >= blinkPattern) {
          blinkPattern = 0;
          blinkCount = 0;
        }
      }
    }
  } else {
    digitalWrite(LED_PIN, LOW);
  }
}

String getConfigString() {
  String configStr = "";
  configStr += "MICROSTEPS:" + String(config.microSteps) + "\n";
  configStr += "PITCH:" + String(config.ballScrewPitch) + "\n";
  configStr += "STEPS_PER_MM:" + String(stepsPerMm()) + "\n";
  configStr += "ACCEL:" + String(config.acceleration) + "\n";
  configStr += "VELOCITY:" + String(config.maxVelocity) + "\n";
  configStr += "POS1:" + String(config.position1) + "\n";
  configStr += "POS2:" + String(config.position2) + "\n";
  configStr += "TRAVEL:" + String(abs(config.position2 - config.position1)) + "\n";
  configStr += "INVERTPULSE:" + String(config.invertPulse ? "1" : "0") + "\n";
  configStr += "INVERTDIR:" + String(config.invertDir ? "1" : "0") + "\n";
  configStr += "INVERTENABLE:" + String(config.invertEnable ? "1" : "0") + "\n";
  configStr += "MOTOR_STATUS:" + String(motorRunning ? "RUNNING" : "STOPPED");
  return configStr;
}

void printConfig() {
  Serial.println("\n=== Current Configuration ===");
  Serial.print("Microsteps: ");
  Serial.print(config.microSteps);
  Serial.println(" steps/rev");
  Serial.print("Ball Screw Pitch: ");
  Serial.print(config.ballScrewPitch);
  Serial.println(" mm/rev");
  Serial.print("Steps per mm: ");
  Serial.println(stepsPerMm());
  Serial.print("Acceleration: ");
  Serial.print(config.acceleration);
  Serial.println(" mm/s²");
  Serial.print("Max Velocity: ");
  Serial.print(config.maxVelocity);
  Serial.println(" mm/s");
  Serial.print("Position 1: ");
  Serial.print(config.position1);
  Serial.println(" mm");
  Serial.print("Position 2: ");
  Serial.print(config.position2);
  Serial.println(" mm");
  Serial.print("Travel Distance: ");
  Serial.print(abs(config.position2 - config.position1));
  Serial.println(" mm");
  Serial.print("Pulse Pin Inverted: ");
  Serial.println(config.invertPulse ? "Yes" : "No");
  Serial.print("Direction Pin Inverted: ");
  Serial.println(config.invertDir ? "Yes" : "No");
  Serial.print("Enable Pin Inverted: ");
  Serial.println(config.invertEnable ? "Yes" : "No");
  Serial.print("Motor Status: ");
  Serial.println(motorRunning ? "Running" : "Stopped");
  Serial.println("============================\n");
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Motor Controller Starting...");
  
  // Load configuration from flash memory
  loadConfig();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
  
  stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
  stepper.setAcceleration(mmToSteps(config.acceleration));
  stepper.setPinsInverted(config.invertDir, config.invertPulse, config.invertEnable);
  stepper.setCurrentPosition(mmToSteps(config.position1));
  stepper.moveTo(mmToSteps(config.position2));
  
  BLEDevice::init("ESP32_Motor_Controller");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
  
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setCallbacks(new MyCallbacks());
  
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);
  BLEDevice::startAdvertising();
  
  Serial.println("Bluetooth device started, waiting for connections...");
  Serial.println("\nSerial Commands:");
  Serial.println("START - Start motor");
  Serial.println("STOP - Stop motor");
  Serial.println("MICROSTEPS:<value> - Set microsteps per revolution");
  Serial.println("PITCH:<value> - Set ball screw pitch (mm/rev)");
  Serial.println("ACCEL:<value> - Set acceleration (mm/s²)");
  Serial.println("VELOCITY:<value> - Set max velocity (mm/s)");
  Serial.println("POS1:<value> - Set position 1 (mm)");
  Serial.println("POS2:<value> - Set position 2 (mm)");
  Serial.println("INVERTPULSE:<0/1> - Invert pulse pin logic");
  Serial.println("INVERTDIR:<0/1> - Invert direction pin logic");
  Serial.println("INVERTENABLE:<0/1> - Invert enable pin logic");
  Serial.println("CONFIG - Show current configuration\n");
  
  printConfig();
}

void loop() {
  processSerialCommand();
  
  if (Serial.available() && Serial.readStringUntil('\n').equalsIgnoreCase("CONFIG")) {
    printConfig();
  }
  
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("Start advertising");
    oldDeviceConnected = deviceConnected;
  }
  
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    Serial.println("Device connected");
  }
  
  if (motorRunning) {
    if (stepper.distanceToGo() == 0) {
      blinkPattern = 1;
      blinkCount = 0;
      
      float currentPosMm = stepsToMm(stepper.currentPosition());
      float tolerance = 0.1; // 0.1mm tolerance for position comparison
      
      if (abs(currentPosMm - config.position2) < tolerance) {
        stepper.moveTo(mmToSteps(config.position1));
        Serial.print("Direction changed: Moving to Position 1 (");
        Serial.print(config.position1);
        Serial.println(" mm)");
      } else {
        stepper.moveTo(mmToSteps(config.position2));
        Serial.print("Direction changed: Moving to Position 2 (");
        Serial.print(config.position2);
        Serial.println(" mm)");
      }
    }
    stepper.run();
  }
  
  updateLED();
}
