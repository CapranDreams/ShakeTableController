# ESP32 Motor Controller

A Bluetooth-enabled motor controller for ESP32-WROOM development boards, designed to control stepper motors for shake table applications. The controller supports continuous back-and-forth motion between two positions with configurable acceleration and velocity profiles.

## Features

- **Stepper Motor Control**: Precise control using pulse, direction, and enable pins
- **Bluetooth BLE Control**: Wireless configuration and control via iOS/Android apps
- **Serial Console Interface**: Wired debugging and configuration through USB
- **Configurable Motion Profile**: Set acceleration, velocity, and position endpoints
- **Pin Logic Inversion**: Support for both active-high and active-low motor drivers
- **Visual Feedback**: LED indicators for system status and command acknowledgment
- **Non-blocking Operation**: All operations run without blocking the main loop

## Hardware Requirements

- ESP32-WROOM Development Board
- Stepper motor driver (e.g., DRV8825, A4988, TB6600)
- Stepper motor
- Power supply appropriate for your motor
- LED and current-limiting resistor (optional, for status indication)

## Pin Configuration

| Function | GPIO Pin | Description |
|----------|----------|-------------|
| PULSE | 26 | Step/Pulse signal to motor driver |
| DIR | 27 | Direction signal to motor driver |
| ENABLE | 25 | Enable/Disable motor driver |
| LED | 2 | Status LED (built-in on most ESP32 boards) |

## Installation

1. Install the Arduino IDE with ESP32 board support
   - ESP32 boards (not just the arduino ones) from the Arduino Board Manager
2. Install required libraries through Arduino Library Manager:
   - **AccelStepper** by Mike McCauley
3. Open `ESP32_Motor_Controller.ino` in Arduino IDE
4. Select your ESP32 board from Tools > Board menu
5. Upload the sketch to your ESP32

## Configuration Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| microSteps | 1600 | Number of microsteps per full revolution |
| ballScrewPitch | 5.0 | Ball screw pitch in mm per revolution |
| acceleration | 50.0 | Acceleration in mm/s² |
| maxVelocity | 100.0 | Maximum velocity in mm/s |
| position1 | 0.0 | First position endpoint in mm |
| position2 | 50.0 | Second position endpoint in mm |
| invertPulse | true | Invert pulse pin logic (active low) |
| invertDir | true | Invert direction pin logic |
| invertEnable | true | Invert enable pin logic (active low) |

## Usage

### Serial Commands

Connect via USB and open a serial monitor at 115200 baud. Available commands:

- `START` - Start motor motion
- `STOP` - Stop motor motion
- `MICROSTEPS:<value>` - Set microsteps per revolution
- `PITCH:<value>` - Set ball screw pitch (mm/rev)
- `ACCEL:<value>` - Set acceleration (mm/s²)
- `VELOCITY:<value>` - Set maximum velocity (mm/s)
- `POS1:<value>` - Set position 1 (mm)
- `POS2:<value>` - Set position 2 (mm)
- `INVERTPULSE:<0/1>` - Invert pulse pin logic (0=normal, 1=inverted)
- `INVERTDIR:<0/1>` - Invert direction pin logic (0=normal, 1=inverted)
- `INVERTENABLE:<0/1>` - Invert enable pin logic (0=normal, 1=inverted)
- `CONFIG` - Display current configuration

### Bluetooth Commands

The device advertises as "ESP32_Motor_Controller". Connect using a BLE-capable device and send the same commands as listed above.

#### BLE UUIDs Explained

Bluetooth Low Energy uses UUIDs (Universally Unique Identifiers) to identify services and characteristics:

- **Service UUID**: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
  - Think of this as the "category" or "department" of functionality
  - This identifies the motor control service on the device
  
- **Characteristic UUID**: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
  - Think of this as a specific "mailbox" within the service
  - This is where you send commands and receive responses

#### Why These Long UUIDs?

BLE uses 128-bit UUIDs to ensure global uniqueness. While they look like random strings, they serve important purposes:

1. **Standard UUIDs**: Bluetooth SIG defines 16-bit UUIDs for common services (like 0x180F for Battery Service)
2. **Custom UUIDs**: For custom applications like ours, we use 128-bit UUIDs to avoid conflicts

These UUIDs cannot be simple human-readable strings because:
- They must be exactly 128 bits (32 hexadecimal characters plus hyphens)
- They must follow the UUID format: `XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX`
- Human-readable names would likely conflict with other devices

#### Using the UUIDs in Your App

When developing your iOS/Android app, you'll use these UUIDs to:
1. Scan for devices offering the motor control service
2. Connect to the service using the Service UUID
3. Write commands to the Characteristic UUID
4. Read responses from the same Characteristic

Example pseudo-code for your app:
```swift
// iOS Swift example
let SERVICE_UUID = CBUUID(string: "4fafc201-1fb5-459e-8fcc-c5c9c331914b")
let CHAR_UUID = CBUUID(string: "beb5483e-36e1-4688-b7f5-ea07361b26a8")

// Scan for devices with our service
centralManager.scanForPeripherals(withServices: [SERVICE_UUID])

// Write a command
peripheral.writeValue("START".data(using: .utf8)!, 
                     for: characteristic, 
                     type: .withResponse)
```

An example response when asking for the CONFIG could look like below:
```swift
MICROSTEPS:1600
PITCH:5.00
STEPS_PER_MM:320.00
ACCEL:50.00
VELOCITY:100.00
POS1:0.00
POS2:50.00
TRAVEL:50.00
INVERTPULSE:1
INVERTDIR:1
INVERTENABLE:1
MOTOR_STATUS:STOPPED
```

### LED Indicators

- **Single blink**: Direction change during motion
- **3 rapid blinks**: Command received (via Serial or Bluetooth)
- **LED off**: System idle

## Example Configuration

For a typical NEMA 17 stepper motor with 1/16 microstepping and 5mm pitch ball screw:

```
MICROSTEPS:3200
PITCH:5.0
ACCEL:100
VELOCITY:200
POS1:0
POS2:100
START
```

This configuration will:
- Set 3200 steps per revolution (200 full steps × 16 microsteps)
- Set 5mm travel per motor revolution
- Accelerate at 100 mm/s²
- Reach maximum velocity of 200 mm/s
- Move between 0 and 100 mm (20 full revolutions)

## Understanding Units

The system now uses real-world millimeter units for all motion parameters:
- **Position**: Specified in millimeters (mm)
- **Velocity**: Specified in millimeters per second (mm/s)
- **Acceleration**: Specified in millimeters per second squared (mm/s²)

The conversion between motor steps and millimeters is calculated as:
```
Steps per mm = Microsteps per revolution / Ball screw pitch (mm/rev)
```

For example, with 1600 microsteps/rev and 5mm pitch:
```
Steps per mm = 1600 / 5 = 320 steps/mm
```

## Motor Driver Compatibility

### Active-High Drivers (Default)
Most modern drivers use active-high logic where:
- ENABLE LOW = Motor enabled
- Pulse rising edge = Step

### Active-Low Drivers
Some drivers require inverted logic. Use the INVERT commands:
```
INVERTENABLE:1
INVERTPULSE:1
```

## Troubleshooting

1. **Motor not moving**
   - Check power supply to motor driver
   - Verify ENABLE pin is in correct state (check invert settings)
   - Ensure motor driver is properly connected

2. **Motor moves erratically**
   - Reduce acceleration or velocity values
   - Check motor driver current settings
   - Verify microstepping configuration matches driver settings

3. **Bluetooth not connecting**
   - Ensure device supports BLE (Bluetooth Low Energy)
   - Reset ESP32 if needed
   - Check serial monitor for connection status

4. **Position drift**
   - Motor may be missing steps due to excessive acceleration/velocity
   - Increase motor current (if driver supports adjustment)
   - Reduce acceleration and velocity values
