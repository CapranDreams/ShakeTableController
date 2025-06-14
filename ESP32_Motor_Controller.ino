#include <AccelStepper.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <SPIFFS.h>

#define PULSE_PIN 26
#define DIR_PIN 27
#define ENABLE_PIN 25
#define LED_PIN 2
#define HOMING_PIN 23

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

// Playback control
bool playbackMode = false;
bool playbackRunning = false;
unsigned long playbackStartTime = 0;
int currentDataIndex = 0;

// Historic data storage (2400 points max) - time optimized (20Hz sampling)
const int MAX_DATA_POINTS = 2400;
const float SAMPLE_RATE_HZ = 20.0;  // 20Hz sampling rate
const float TIME_STEP = 1.0 / SAMPLE_RATE_HZ;  // 0.05 seconds per sample
float* historicDisplacement = nullptr;  // Only store displacement, time = index * TIME_STEP
int historicDataCount = 0;

// Monitoring variables
bool monitorPosition = false;
bool monitorVelocity = false;
bool monitorAcceleration = false;
unsigned long lastMonitorTime = 0;
const unsigned long MONITOR_INTERVAL = 100; // ms
bool plotterLabelsShown = false;

// Acceleration test loop
bool accelTestRunning = false;
float accelTestDistance = 50.0;  // mm
float accelTestAccel = 25.0;     // mm/s²
bool accelTestDirection = true;  // true = forward, false = backward
float accelTestStartPos = 0.0;

// Data upload
bool dataUploadMode = false;
String uploadBuffer = "";
int uploadLineCount = 0;

// Homing switch
bool lastHomingState = false;
bool homingTriggered = false;

// Homing procedure
enum HomingState {
  HOMING_IDLE,
  HOMING_MOVE_FORWARD,
  HOMING_MOVE_BACKWARD,
  HOMING_COMPLETE
};
HomingState homingState = HOMING_IDLE;
bool homingInProgress = false;
float homingStartPosition = 0.0;
float homingForwardDistance = 5.0;  // mm to move forward before backing up
float homingSpeed = 10.0;           // mm/s for homing moves

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
void loadHistoricData();
void createDefaultHistoricData();
void saveHistoricData();
void runPlayback();
void runAccelTest();
void sendMonitorData();
void interpolatePosition(float time, float& position);
void checkHomingSwitch();
void runHomingProcedure();
void startHomingProcedure();

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
    if (playbackMode) {
      if (historicDataCount < 2) {
        Serial.println("Error: No historic data loaded for playback");
        return;
      }
      playbackRunning = true;
      playbackStartTime = millis();
      currentDataIndex = 0;
      digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);  // Enable motor for playback
      // Set appropriate speeds for smooth playback
      stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
      stepper.setAcceleration(mmToSteps(config.acceleration * 2));  // Higher accel for responsive playback
      Serial.print("Playback started - ");
      Serial.print(historicDataCount);
      Serial.print(" data points, duration: ");
      Serial.print((historicDataCount - 1) * TIME_STEP);
      Serial.println(" seconds");
    } else if (accelTestRunning) {
      Serial.println("Acceleration test already running");
    } else {
      motorRunning = true;
      digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);
      Serial.println("Motor started");
    }
  } else if (command == "STOP") {
    if (playbackMode && playbackRunning) {
      playbackRunning = false;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor when playback stops
      Serial.println("Playback stopped");
    } else if (accelTestRunning) {
      accelTestRunning = false;
      motorRunning = false;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
      Serial.println("Acceleration test stopped");
    } else if (homingInProgress) {
      homingInProgress = false;
      homingState = HOMING_IDLE;
      motorRunning = false;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
      Serial.println("Homing procedure stopped");
    } else {
      motorRunning = false;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor to prevent overheating
      Serial.println("Motor stopped - motor disabled to prevent overheating");
    }
  } else if (command == "PLAYBACK:ON") {
    playbackMode = true;
    playbackRunning = false;
    accelTestRunning = false;
    currentDataIndex = 0;
    Serial.print("Playback mode enabled - ");
    if (historicDataCount > 0) {
      Serial.print(historicDataCount);
      Serial.print(" data points loaded, duration: ");
      Serial.print((historicDataCount - 1) * TIME_STEP);
      Serial.println(" seconds. Use START to begin playback.");
    } else {
      Serial.println("WARNING: No historic data loaded! Use UPLOAD to load data first.");
    }
  } else if (command == "PLAYBACK:OFF") {
    playbackMode = false;
    playbackRunning = false;
    digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor when not in use
    Serial.println("Playback mode disabled - normal operation");
  } else if (command.find("ACCELTEST:") == 0) {
    // Format: ACCELTEST:distance,acceleration
    int commaPos = command.find(',', 10);
    if (commaPos != std::string::npos) {
      accelTestDistance = atof(command.substr(10, commaPos - 10).c_str());
      accelTestAccel = atof(command.substr(commaPos + 1).c_str());
      accelTestRunning = true;
      accelTestDirection = true;
      accelTestStartPos = stepsToMm(stepper.currentPosition());
      playbackMode = false;
      motorRunning = true;
      digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);
      Serial.print("Acceleration test started: distance=");
      Serial.print(accelTestDistance);
      Serial.print("mm, accel=");
      Serial.print(accelTestAccel);
      Serial.println("mm/s²");
    } else {
      Serial.println("Invalid ACCELTEST format. Use: ACCELTEST:distance,acceleration");
    }
  } else if (command == "UPLOAD:START") {
    dataUploadMode = true;
    uploadBuffer = "";
    uploadLineCount = 0;
    Serial.println("Data upload started. Send CSV lines, end with UPLOAD:END");
  } else if (command == "UPLOAD:END") {
    if (dataUploadMode) {
      dataUploadMode = false;
      saveHistoricData();
      Serial.print("Data upload complete. Received ");
      Serial.print(uploadLineCount);
      Serial.println(" lines");
    }
  } else if (command.find("BATCH:") == 0 && dataUploadMode) {
    // Handle batch upload format: BATCH:value1,value2,value3,...
    String batchData = command.substr(6).c_str();
    int batchStartCount = uploadLineCount;
    
    // Process each value in the batch
    int startIdx = 0;
    int commaIdx = batchData.indexOf(',');
    
    while (commaIdx != -1) {
      String value = batchData.substring(startIdx, commaIdx);
      value.trim();
      if (value.length() > 0) {
        uploadBuffer += value;
        uploadBuffer += "\n";
        uploadLineCount++;
      }
      startIdx = commaIdx + 1;
      commaIdx = batchData.indexOf(',', startIdx);
    }
    
    // Process the last value
    String lastValue = batchData.substring(startIdx);
    lastValue.trim();
    if (lastValue.length() > 0) {
      uploadBuffer += lastValue;
      uploadBuffer += "\n";
      uploadLineCount++;
    }
    
    int batchValueCount = uploadLineCount - batchStartCount;
    Serial.print("Batch received: ");
    Serial.print(batchValueCount);
    Serial.print(" values, total: ");
    Serial.print(uploadLineCount);
    Serial.println(" lines");
    
    // Report progress every 100 lines
    if (uploadLineCount >= 100 && (uploadLineCount % 100) < batchValueCount) {
      Serial.print("Progress: ");
      Serial.print(uploadLineCount);
      Serial.println(" lines received...");
    }
  } else if (dataUploadMode && command.length() > 0) {
    // Process single CSV line during upload (existing code)
    uploadBuffer += command.c_str();
    uploadBuffer += "\n";
    uploadLineCount++;
    if (uploadLineCount % 100 == 0) {
      Serial.print("Received ");
      Serial.print(uploadLineCount);
      Serial.println(" lines...");
    }
  } else if (command == "MONITOR:POS") {
    monitorPosition = !monitorPosition;
    plotterLabelsShown = false;  // Reset labels when changing monitoring
    Serial.print("Position monitoring: ");
    Serial.println(monitorPosition ? "ON" : "OFF");
  } else if (command == "MONITOR:VEL") {
    monitorVelocity = !monitorVelocity;
    plotterLabelsShown = false;  // Reset labels when changing monitoring
    Serial.print("Velocity monitoring: ");
    Serial.println(monitorVelocity ? "ON" : "OFF");
  } else if (command == "MONITOR:ACC") {
    monitorAcceleration = !monitorAcceleration;
    plotterLabelsShown = false;  // Reset labels when changing monitoring
    Serial.print("Acceleration monitoring: ");
    Serial.println(monitorAcceleration ? "ON" : "OFF");
  } else if (command == "MONITOR:ALL") {
    monitorPosition = true;
    monitorVelocity = true;
    monitorAcceleration = true;
    plotterLabelsShown = false;  // Reset labels when changing monitoring
    Serial.println("All monitoring enabled");
  } else if (command == "MONITOR:NONE") {
    monitorPosition = false;
    monitorVelocity = false;
    monitorAcceleration = false;
    plotterLabelsShown = false;  // Reset labels when changing monitoring
    Serial.println("All monitoring disabled");
  } else if (command == "HOME") {
    if (homingInProgress) {
      Serial.println("Homing already in progress");
    } else {
      startHomingProcedure();
    }
  } else if (command.find("MICROSTEPS:") == 0) {
    config.microSteps = atoi(command.substr(11).c_str());
    Serial.print("Microsteps set to: ");
    Serial.println(config.microSteps);
    stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
    stepper.setAcceleration(mmToSteps(config.acceleration));
    saveConfig();
  } else if (command.find("PITCH:") == 0) {
    config.ballScrewPitch = atof(command.substr(6).c_str());
    Serial.print("Ball screw pitch set to: ");
    Serial.print(config.ballScrewPitch);
    Serial.println(" mm/rev");
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
  } else if (command == "HELP") {
    printHelp();
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
  preferences.begin("motor-config", true);
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

void createDefaultHistoricData() {
  Serial.println("========================================");
  Serial.println("CREATING TEST DATA FOR IMMEDIATE PLAYBACK");
  Serial.println("========================================");
  Serial.println("No historic data found. Creating simple test pattern...");
  
  // Allocate memory for test data
  if (historicDisplacement) delete[] historicDisplacement;
  historicDisplacement = new float[100];  // Small test dataset
  historicDataCount = 100;
  
  // Create a simple sine wave pattern for testing (5 seconds at 20Hz)
  for (int i = 0; i < historicDataCount; i++) {
    float time = i * TIME_STEP;
    historicDisplacement[i] = 10.0 * sin(2.0 * 3.14159 * 0.2 * time);  // 10mm amplitude, 0.2Hz frequency
  }
  
  Serial.print("Created test data: ");
  Serial.print(historicDataCount);
  Serial.print(" points, duration: ");
  Serial.print((historicDataCount - 1) * TIME_STEP);
  Serial.println(" seconds");
  Serial.println("");
  Serial.println("✓ You can now test playback immediately:");
  Serial.println("  1. PLAYBACK:ON");
  Serial.println("  2. START");
  Serial.println("");
  Serial.println("For full dataset (2400 points, 120 seconds):");
  Serial.println("  1. Copy contents of 'displacement_upload.txt'");
  Serial.println("  2. Paste into Serial Monitor");
  Serial.println("  3. Wait for upload completion");
  Serial.println("========================================");
}

void loadHistoricData() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  
  File file = SPIFFS.open("/historic.dat", "r");
  if (!file) {
    Serial.println("No historic data file found - creating default data");
    createDefaultHistoricData();
    // Try to load the newly created default data
    file = SPIFFS.open("/historic.dat", "r");
    if (!file) {
      Serial.println("Failed to create or load default historic data");
      return;
    }
  }
  
  // Allocate memory for displacement data only (50% memory savings)
  if (historicDisplacement) delete[] historicDisplacement;
  historicDisplacement = new float[MAX_DATA_POINTS];
  historicDataCount = 0;
  
  // Read displacement values line by line
  while (file.available() && historicDataCount < MAX_DATA_POINTS) {
    String line = file.readStringUntil('\n');
    if (line.length() > 0) {
      historicDisplacement[historicDataCount] = line.toFloat();
      historicDataCount++;
    }
  }
  
  file.close();
  Serial.print("Loaded ");
  Serial.print(historicDataCount);
  Serial.print(" historic data points, duration: ");
  Serial.print((historicDataCount - 1) * TIME_STEP);
  Serial.println(" seconds (time optimized storage)");
}

void saveHistoricData() {
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  
  // Parse uploaded displacement data (time optimized format)
  if (historicDisplacement) delete[] historicDisplacement;
  historicDisplacement = new float[MAX_DATA_POINTS];
  historicDataCount = 0;
  
  int startIdx = 0;
  int endIdx = uploadBuffer.indexOf('\n');
  
  // Parse displacement values line by line
  while (endIdx > 0 && historicDataCount < MAX_DATA_POINTS) {
    String line = uploadBuffer.substring(startIdx, endIdx);
    line.trim();
    if (line.length() > 0) {
      historicDisplacement[historicDataCount] = line.toFloat();
      historicDataCount++;
    }
    startIdx = endIdx + 1;
    endIdx = uploadBuffer.indexOf('\n', startIdx);
  }
  
  // Save to SPIFFS in optimized format (displacement only)
  File file = SPIFFS.open("/historic.dat", "w");
  if (file) {
    for (int i = 0; i < historicDataCount; i++) {
      file.println(historicDisplacement[i], 6);
    }
    file.close();
    Serial.print("Saved ");
    Serial.print(historicDataCount);
    Serial.print(" data points to SPIFFS (50% memory savings), duration: ");
    Serial.print((historicDataCount - 1) * TIME_STEP);
    Serial.println(" seconds");
  } else {
    Serial.println("Failed to save historic data");
  }
  
  uploadBuffer = ""; // Clear buffer
}

void interpolatePosition(float time, float& position) {
  if (historicDataCount < 2 || !historicDisplacement) {
    position = 0;
    return;
  }
  
  // Calculate index from time using 20Hz sampling rate
  float exactIndex = time / TIME_STEP;
  
  // Handle time before first point
  if (exactIndex <= 0) {
    position = historicDisplacement[0];
    return;
  }
  
  // Handle time after last point
  if (exactIndex >= historicDataCount - 1) {
    position = historicDisplacement[historicDataCount - 1];
    return;
  }
  
  // Linear interpolation between adjacent data points
  int lowerIndex = (int)exactIndex;
  int upperIndex = lowerIndex + 1;
  float t = exactIndex - lowerIndex;  // Fractional part
  
  position = historicDisplacement[lowerIndex] + t * (historicDisplacement[upperIndex] - historicDisplacement[lowerIndex]);
}

void runPlayback() {
  if (!playbackRunning || historicDataCount < 2) return;
  
  float currentTime = (millis() - playbackStartTime) / 1000.0;
  
  // Check if playback is complete
  if (currentTime > (historicDataCount - 1) * TIME_STEP) {
    playbackRunning = false;
    digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor when playback complete
    Serial.println("Playback complete");
    return;
  }
  
  // Get interpolated position
  float targetPosition;
  interpolatePosition(currentTime, targetPosition);
  
  // Move to target position
  stepper.moveTo(mmToSteps(targetPosition));
  
  // Debug output every 5 seconds during playback
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 5000) {
    lastDebugTime = millis();
    Serial.print("Playback: t=");
    Serial.print(currentTime, 2);
    Serial.print("s, target=");
    Serial.print(targetPosition, 3);
    Serial.print("mm, current=");
    Serial.print(stepsToMm(stepper.currentPosition()), 3);
    Serial.println("mm");
  }
}

void runAccelTest() {
  if (!accelTestRunning) return;
  
  float currentPos = stepsToMm(stepper.currentPosition());
  
  // Set acceleration
  stepper.setAcceleration(mmToSteps(accelTestAccel));
  
  // Check if we need to change direction
  if (stepper.distanceToGo() == 0) {
    if (accelTestDirection) {
      // Moving forward, switch to backward
      stepper.moveTo(mmToSteps(accelTestStartPos));
      accelTestDirection = false;
      Serial.println("Acceleration test: reversing direction");
    } else {
      // Moving backward, switch to forward
      stepper.moveTo(mmToSteps(accelTestStartPos + accelTestDistance));
      accelTestDirection = true;
      Serial.println("Acceleration test: forward direction");
    }
  }
}

void checkHomingSwitch() {
  bool currentHomingState = digitalRead(HOMING_PIN);
  
  // Detect rising edge (switch activation)
  if (currentHomingState && !lastHomingState) {
    homingTriggered = true;
    
    if (homingInProgress && homingState == HOMING_MOVE_BACKWARD) {
      // Homing procedure found the switch
      stepper.setCurrentPosition(0);
      homingState = HOMING_COMPLETE;
      homingInProgress = false;
      motorRunning = false;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
      Serial.println("Homing complete - Position set to 0.0mm");
    } else if (!homingInProgress) {
      // Manual trigger outside of homing procedure
      stepper.setCurrentPosition(0);
      Serial.println("Homing switch triggered - Position set to 0.0mm");
      
      // Stop current movement when homing
      if (motorRunning || playbackRunning || accelTestRunning) {
        Serial.println("Movement stopped due to homing switch");
        motorRunning = false;
        playbackRunning = false;
        accelTestRunning = false;
        digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor when stopped
      }
    }
  }
  
  lastHomingState = currentHomingState;
}

void startHomingProcedure() {
  if (digitalRead(HOMING_PIN)) {
    Serial.println("Homing switch already active - moving off switch first");
    // Move forward a bit to get off the switch
    homingStartPosition = stepsToMm(stepper.currentPosition());
    stepper.moveTo(mmToSteps(homingStartPosition + homingForwardDistance));
    homingState = HOMING_MOVE_FORWARD;
  } else {
    Serial.println("Starting homing procedure - moving forward then backward");
    homingStartPosition = stepsToMm(stepper.currentPosition());
    stepper.moveTo(mmToSteps(homingStartPosition + homingForwardDistance));
    homingState = HOMING_MOVE_FORWARD;
  }
  
  homingInProgress = true;
  motorRunning = true;
  digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);
  
  // Set homing speed
  stepper.setMaxSpeed(mmToSteps(homingSpeed));
  stepper.setAcceleration(mmToSteps(homingSpeed * 2)); // Quick acceleration for homing
}

void runHomingProcedure() {
  if (!homingInProgress) return;
  
  switch (homingState) {
    case HOMING_MOVE_FORWARD:
      if (stepper.distanceToGo() == 0) {
        // Forward movement complete, now move backward
        Serial.println("Forward movement complete - now moving backward to find switch");
        stepper.moveTo(mmToSteps(homingStartPosition - 200.0)); // Move back up to 200mm to find switch
        homingState = HOMING_MOVE_BACKWARD;
      }
      break;
      
    case HOMING_MOVE_BACKWARD:
      // Continue moving backward until switch is hit
      // Switch detection is handled in checkHomingSwitch()
      if (stepper.distanceToGo() == 0) {
        // Reached end of backward travel without hitting switch
        Serial.println("Homing failed - switch not found within 200mm range");
        homingInProgress = false;
        homingState = HOMING_IDLE;
        motorRunning = false;
        digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor when homing fails
        // Restore normal speeds
        stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
        stepper.setAcceleration(mmToSteps(config.acceleration));
      }
      break;
      
    case HOMING_COMPLETE:
      // Homing completed successfully
      homingInProgress = false;
      homingState = HOMING_IDLE;
      digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);  // Disable motor after successful homing
      // Restore normal speeds
      stepper.setMaxSpeed(mmToSteps(config.maxVelocity));
      stepper.setAcceleration(mmToSteps(config.acceleration));
      break;
      
    default:
      break;
  }
}

void sendMonitorData() {
  if (millis() - lastMonitorTime < MONITOR_INTERVAL) return;
  lastMonitorTime = millis();
  
  if (!monitorPosition && !monitorVelocity && !monitorAcceleration) return;
  
  // Send column headers for Arduino Serial Plotter (only once per monitoring session)
  if (!plotterLabelsShown) {
    String labels = "//";
    
    if (monitorPosition && monitorVelocity && monitorAcceleration) {
      labels += "Position_mm,Velocity_mm/s,Acceleration_mm/s2";
    } else if (monitorPosition && monitorVelocity) {
      labels += "Position_mm,Velocity_mm/s";
    } else if (monitorPosition && monitorAcceleration) {
      labels += "Position_mm,Acceleration_mm/s2";
    } else if (monitorVelocity && monitorAcceleration) {
      labels += "Velocity_mm/s,Acceleration_mm/s2";
    } else if (monitorPosition) {
      labels += "Position_mm";
    } else if (monitorVelocity) {
      labels += "Velocity_mm/s";
    } else if (monitorAcceleration) {
      labels += "Acceleration_mm/s2";
    }
    
    Serial.println(labels);
    
    // Also send labels over Bluetooth for iOS app
    if (deviceConnected && pCharacteristic) {
      String bleLabels = "MONITOR_LABELS:" + labels.substring(2); // Remove "//" prefix
      pCharacteristic->setValue(bleLabels.c_str());
      pCharacteristic->notify();
    }
    
    plotterLabelsShown = true;
  }
  
  float currentPosMm = stepsToMm(stepper.currentPosition());
  float currentVelMmS = stepsToMm(stepper.speed());
  float currentAccelMmS2 = 0;
  
  if (accelTestRunning) {
    currentAccelMmS2 = stepper.distanceToGo() > 0 ? accelTestAccel : -accelTestAccel;
  } else if (playbackMode && playbackRunning && historicDataCount >= 3) {
    // Calculate acceleration from position data
    float currentTime = (millis() - playbackStartTime) / 1000.0;
    float dt = 0.05; // Small time delta for numerical derivative
    float pos1, pos2, pos3;
    interpolatePosition(currentTime - dt, pos1);
    interpolatePosition(currentTime, pos2);
    interpolatePosition(currentTime + dt, pos3);
    float vel1 = (pos2 - pos1) / dt;
    float vel2 = (pos3 - pos2) / dt;
    currentAccelMmS2 = (vel2 - vel1) / dt;
  } else {
    currentAccelMmS2 = config.acceleration;
  }
  
  // Arduino Serial Plotter format: space or tab separated values
  String serialOutput = "";
  String bleOutput = "MONITOR_DATA:";
  
  if (monitorPosition && monitorVelocity && monitorAcceleration) {
    // All three values: Position Velocity Acceleration
    serialOutput = String(currentPosMm, 3) + " " + String(currentVelMmS, 3) + " " + String(currentAccelMmS2, 3);
    bleOutput += String(currentPosMm, 3) + "," + String(currentVelMmS, 3) + "," + String(currentAccelMmS2, 3);
  } else if (monitorPosition && monitorVelocity) {
    // Position and Velocity
    serialOutput = String(currentPosMm, 3) + " " + String(currentVelMmS, 3);
    bleOutput += String(currentPosMm, 3) + "," + String(currentVelMmS, 3);
  } else if (monitorPosition && monitorAcceleration) {
    // Position and Acceleration
    serialOutput = String(currentPosMm, 3) + " " + String(currentAccelMmS2, 3);
    bleOutput += String(currentPosMm, 3) + "," + String(currentAccelMmS2, 3);
  } else if (monitorVelocity && monitorAcceleration) {
    // Velocity and Acceleration
    serialOutput = String(currentVelMmS, 3) + " " + String(currentAccelMmS2, 3);
    bleOutput += String(currentVelMmS, 3) + "," + String(currentAccelMmS2, 3);
  } else if (monitorPosition) {
    // Position only
    serialOutput = String(currentPosMm, 3);
    bleOutput += String(currentPosMm, 3);
  } else if (monitorVelocity) {
    // Velocity only
    serialOutput = String(currentVelMmS, 3);
    bleOutput += String(currentVelMmS, 3);
  } else if (monitorAcceleration) {
    // Acceleration only
    serialOutput = String(currentAccelMmS2, 3);
    bleOutput += String(currentAccelMmS2, 3);
  }
  
  if (serialOutput.length() > 0) {
    // Send to Serial (for Arduino Serial Plotter)
    Serial.println(serialOutput);
    
    // Send to Bluetooth (for iOS app)
    if (deviceConnected && pCharacteristic) {
      pCharacteristic->setValue(bleOutput.c_str());
      pCharacteristic->notify();
    }
  }
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
  configStr += "MOTOR_STATUS:" + String(motorRunning ? "RUNNING" : "STOPPED") + "\n";
  configStr += "PLAYBACK_MODE:" + String(playbackMode ? "ON" : "OFF") + "\n";
  configStr += "HOMING_SWITCH:" + String(digitalRead(HOMING_PIN) ? "ACTIVE" : "INACTIVE") + "\n";
  configStr += "HOMING_STATUS:" + String(homingInProgress ? "IN_PROGRESS" : "IDLE") + "\n";
  configStr += "MONITOR_POS:" + String(monitorPosition ? "ON" : "OFF") + "\n";
  configStr += "MONITOR_VEL:" + String(monitorVelocity ? "ON" : "OFF") + "\n";
  configStr += "MONITOR_ACC:" + String(monitorAcceleration ? "ON" : "OFF") + "\n";
  configStr += "HISTORIC_DATA:" + String(historicDataCount) + " points";
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
  Serial.print("Playback Mode: ");
  Serial.println(playbackMode ? "Enabled" : "Disabled");
  Serial.print("Historic Data: ");
  Serial.print(historicDataCount);
  Serial.println(" points loaded");
  Serial.print("Homing Switch: ");
  Serial.println(digitalRead(HOMING_PIN) ? "Active (HIGH)" : "Inactive (LOW)");
  Serial.print("Homing Status: ");
  Serial.println(homingInProgress ? "In Progress" : "Idle");
  Serial.println("============================\n");
}

void printHelp() {
  Serial.println("\n=== Serial Command Help ===");
  Serial.println("Motor Control:");
  Serial.println("  START - Start motor/playback/test");
  Serial.println("  STOP - Stop motor/playback/test");
  Serial.println("  HOME - Start homing procedure (forward then backward to switch)");
  Serial.println("\nPlayback Control:");
  Serial.println("  PLAYBACK:ON - Enable playback mode");
  Serial.println("  PLAYBACK:OFF - Disable playback mode");
  Serial.println("\nAcceleration Test:");
  Serial.println("  ACCELTEST:distance,acceleration - Start acceleration test");
  Serial.println("    Example: ACCELTEST:50,25 (50mm distance, 25mm/s² accel)");
  Serial.println("\nData Upload:");
  Serial.println("  UPLOAD:START - Begin CSV data upload");
  Serial.println("  UPLOAD:END - Complete data upload");
  Serial.println("  BATCH:v1,v2,v3,... - Upload multiple values at once (during upload)");
  Serial.println("\nMonitoring:");
  Serial.println("  MONITOR:POS - Toggle position monitoring");
  Serial.println("  MONITOR:VEL - Toggle velocity monitoring");
  Serial.println("  MONITOR:ACC - Toggle acceleration monitoring");
  Serial.println("  MONITOR:ALL - Enable all monitoring");
  Serial.println("  MONITOR:NONE - Disable all monitoring");
  Serial.println("  Note: For Serial Plotter, labels are automatically added");
  Serial.println("\nConfiguration:");
  Serial.println("  MICROSTEPS:<value> - Set microsteps per revolution");
  Serial.println("  PITCH:<value> - Set ball screw pitch (mm/rev)");
  Serial.println("  ACCEL:<value> - Set acceleration (mm/s²)");
  Serial.println("  VELOCITY:<value> - Set max velocity (mm/s)");
  Serial.println("  POS1:<value> - Set position 1 (mm)");
  Serial.println("  POS2:<value> - Set position 2 (mm)");
  Serial.println("  INVERTPULSE:<0/1> - Invert pulse pin logic");
  Serial.println("  INVERTDIR:<0/1> - Invert direction pin logic");
  Serial.println("  INVERTENABLE:<0/1> - Invert enable pin logic");
  Serial.println("\nOther:");
  Serial.println("  CONFIG - Show current configuration");
  Serial.println("  HELP - Show this help message");
  Serial.println("===========================\n");
}

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 Motor Controller Starting...");
  
  // Load configuration from flash memory
  loadConfig();
  
  // Load historic data from SPIFFS
  loadHistoricData();
  
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(HOMING_PIN, INPUT_PULLDOWN);
  digitalWrite(ENABLE_PIN, config.invertEnable ? LOW : HIGH);
  
  // Read initial homing switch state
  lastHomingState = digitalRead(HOMING_PIN);
  Serial.print("Homing switch initial state: ");
  Serial.println(lastHomingState ? "Active (HIGH)" : "Inactive (LOW)");
  
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
  Serial.println("\nType 'HELP' for command list");
  Serial.println("Motor starts disabled to prevent overheating when idle");
  
  printConfig();
}

void loop() {
  processSerialCommand();
  
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
  
  // Check homing switch on every loop
  checkHomingSwitch();
  
  if (playbackMode && playbackRunning) {
    runPlayback();
    stepper.run();
    digitalWrite(ENABLE_PIN, config.invertEnable ? HIGH : LOW);
  } else if (homingInProgress) {
    runHomingProcedure();
    stepper.run();
  } else if (accelTestRunning) {
    runAccelTest();
    stepper.run();
  } else if (motorRunning && !playbackMode && !accelTestRunning && !homingInProgress) {
    if (stepper.distanceToGo() == 0) {
      blinkPattern = 1;
      blinkCount = 0;
      
      float currentPosMm = stepsToMm(stepper.currentPosition());
      float tolerance = 0.1;
      
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
    // Keep motor enabled during normal operation to maintain position
    stepper.run();
  }
  
  sendMonitorData();
  updateLED();
}