# ESP32 Motor Controller

A comprehensive Bluetooth Low Energy (BLE) enabled motor controller for ESP32, designed to control stepper motors through a DM542T driver. The system supports both wireless control via iOS app and direct serial commands, with advanced features including historic data playback, acceleration testing, and automatic homing.

## Features

### Core Motor Control
- **Bluetooth Low Energy (BLE)** connectivity for wireless control
- **Serial Command Interface** for direct control via Arduino IDE
- **DM542T Stepper Driver** compatibility with configurable microsteps
- **Ball Screw Control** with precise positioning in millimeters
- **Automatic Homing** with limit switch on GPIO23

### Advanced Functionality
- **Historic Data Playback** from CSV files (up to 2400 data points)
- **Acceleration Test Loops** for motion profile visualization
- **Real-time Monitoring** of position, velocity, and acceleration
- **CSV Data Upload** via Bluetooth for custom motion profiles
- **SPIFFS Storage** for persistent historic data

### Configuration & Monitoring
- **Configurable Parameters** including acceleration, velocity, and travel limits
- **Pin Inversion Settings** for compatibility with different driver wiring
- **Flash Memory Storage** for persistent configuration
- **Real-time Status Monitoring** via BLE notifications and serial output
- **Arduino Serial Plotter** support for data visualization

## Hardware Requirements

### Essential Components
- **ESP32 Development Board**
- **DM542T Stepper Motor Driver**
- **Stepper Motor** (compatible with DM542T)
- **Ball Screw Assembly** (5mm pitch recommended)
- **Power Supply** (24V recommended for DM542T)

### Optional Components
- **Limit Switch** for homing (connects to GPIO23)
- **iOS Device** for wireless control
- **Pull-down resistor** (internal pulldown used for homing switch)

### Recommended Specifications
- Ball screw pitch: 5mm per revolution
- Microstep setting: 1600 steps/revolution (8 microsteps)
- Homing switch: Normally open, connects GPIO23 to VCC when closed

## Pin Configuration

| Function | ESP32 Pin | Description |
|----------|-----------|-------------|
| Pulse | GPIO26 | Step pulse signal to DM542T |
| Direction | GPIO27 | Direction control signal |
| Enable | GPIO25 | Motor enable/disable signal |
| Homing Switch | GPIO23 | Limit switch input (with internal pulldown) |
| LED | GPIO2 | Status LED (built-in) |

### Homing Switch Wiring
- GPIO23 configured with internal pulldown resistor
- Switch should be normally open
- When closed, switch connects GPIO23 to VCC (3.3V)
- Switch activation automatically sets position to 0.0mm

## Installation

1. **Clone the Repository**
   ```bash
   git clone [repository-url]
   cd ESP32_Motor_Controller
   ```

2. **Arduino IDE Setup**
   - Install ESP32 board package in Arduino IDE
   - Install required libraries:
     - AccelStepper
     - ESP32 BLE Arduino
     - SPIFFS (included with ESP32 package)

3. **Choose Version**
   - **ESP32_Motor_Controller.ino**: Basic version with original features
   - **ESP32_Motor_Controller_Enhanced.ino**: Full-featured version with all new capabilities

4. **Upload to ESP32**
   - Connect ESP32 via USB
   - Select appropriate board and port
   - Upload the chosen sketch
   - Open Serial Monitor at 115200 baud for status and control

## Configuration

The motor controller can be configured via BLE commands (iOS app) or serial commands (Arduino IDE). All settings are automatically saved to flash memory.

### Operation Modes

#### Playback Mode
- Follows historic CSV data for position vs. time
- Supports up to 2400 data points (120 seconds at 20Hz)
- Linear interpolation for smooth motion
- Only displacement stored, time reconstructed from 20Hz sampling
- Need to upload from App first

#### Acceleration Test Mode
- Configurable distance and acceleration parameters
- Continuous back-and-forth motion with constant acceleration/deceleration
- Ideal for testing and visualization of acceleration profiles

#### Homing
- Automatic position referencing using limit switch
- Moves forward 5mm, then backward until switch triggers

## Command Reference

### Motor Control Commands
- `START` - Start motor movement/playback/test
- `STOP` - Stop all motor operations and disable motor to prevent overheating
- `HOME` - Start automatic homing procedure

### Mode Commands
- `PLAYBACK:ON` - Enable historic data playback mode
- `PLAYBACK:OFF` - Return to normal operation
- `ACCELTEST:distance,acceleration` - Start acceleration test (e.g., `ACCELTEST:50,25`)

### Configuration Commands
- `MICROSTEPS:value` - Set microsteps per revolution
- `PITCH:value` - Set ball screw pitch (mm/rev)
- `ACCEL:value` - Set acceleration (mm/s²)
- `VELOCITY:value` - Set maximum velocity (mm/s)
- `POS1:value` - Set position 1 (mm)
- `POS2:value` - Set position 2 (mm)
- `INVERTPULSE:0/1` - Invert pulse pin logic
- `INVERTDIR:0/1` - Invert direction pin logic
- `INVERTENABLE:0/1` - Invert enable pin logic

### Data Upload Commands
- `UPLOAD:START` - Begin CSV data upload
- *(Send CSV lines between START and END)*
- `UPLOAD:END` - Complete data upload and save

### Monitoring Commands
- `MONITOR:POS` - Toggle position monitoring
- `MONITOR:VEL` - Toggle velocity monitoring
- `MONITOR:ACC` - Toggle acceleration monitoring
- `MONITOR:ALL` - Enable all monitoring
- `MONITOR:NONE` - Disable all monitoring

### Status Commands
- `CONFIG` - Show current configuration
- `HELP` - Display command help

## BLE Protocol
Change these if you have multiple devices, this is what I used. 

### Service UUID
`4fafc201-1fb5-459e-8fcc-c5c9c331914b`

### Characteristic UUID
`beb5483e-36e1-4688-b7f5-ea07361b26a8`
