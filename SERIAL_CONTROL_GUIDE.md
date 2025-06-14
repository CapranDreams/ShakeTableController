# ESP32 Motor Controller Serial Control Guide

## Overview
This guide explains how to control and monitor the ESP32 motor controller using the Arduino IDE Serial Monitor.

## Setting Up Arduino IDE

1. **Open Serial Monitor**: Tools → Serial Monitor (or Ctrl+Shift+M)
2. **Set Baud Rate**: Select `115200` from the dropdown
3. **Line Ending**: Set to "Newline" or "Both NL & CR"

## Motor Control Commands

### Basic Control
- `START` - Start motor movement (or playback if in playback mode)
- `STOP` - Stop motor movement and disable motor to prevent overheating
- `HOME` - Start automatic homing procedure

### Configuration Commands
These commands update the motor configuration and save to flash memory:

- `MICROSTEPS:<value>` - Set microsteps per revolution (e.g., `MICROSTEPS:1600`)
- `PITCH:<value>` - Set ball screw pitch in mm/rev (e.g., `PITCH:5.0`)
- `ACCEL:<value>` - Set acceleration in mm/s² (e.g., `ACCEL:50.0`)
- `VELOCITY:<value>` - Set max velocity in mm/s (e.g., `VELOCITY:100.0`)
- `POS1:<value>` - Set position 1 in mm (e.g., `POS1:0.0`)
- `POS2:<value>` - Set position 2 in mm (e.g., `POS2:50.0`)

### Pin Configuration
- `INVERTPULSE:<0/1>` - Invert pulse pin logic (1=inverted, 0=normal)
- `INVERTDIR:<0/1>` - Invert direction pin logic
- `INVERTENABLE:<0/1>` - Invert enable pin logic

## Historic Data Playback

### Playback Control
- `PLAYBACK:ON` - Enable playback mode (motor follows historic data)
- `PLAYBACK:OFF` - Return to normal motor operation

### How Playback Works
1. Enable playback mode with `PLAYBACK:ON`
2. Use `START` to begin playing the historic data
3. The motor will follow the position profile from the loaded CSV data (always at 1x speed)
4. Use `STOP` to pause playback
5. Use `PLAYBACK:OFF` to return to normal operation

### Data Storage
- The ESP32 can store up to 2400 data points (120 seconds at 20Hz)
- Data is stored in SPIFFS filesystem and persists across power cycles
- Only time and displacement columns are stored to save memory

## Real-Time Monitoring

### Monitor Commands
- `MONITOR:POS` - Toggle position monitoring
- `MONITOR:VEL` - Toggle velocity monitoring
- `MONITOR:ACC` - Toggle acceleration monitoring
- `MONITOR:ALL` - Enable all monitoring
- `MONITOR:NONE` - Disable all monitoring

### Using the Serial Plotter
1. Close Serial Monitor
2. Open Tools → Serial Plotter
3. Enable monitoring with commands above
4. The plotter will display real-time values

### Monitor Output Format
- Normal mode: `Monitor: Pos=25.000mm Vel=50.000mm/s Acc=50.000mm/s²`
- Playback mode: `Playback[1.50s]: Pos=0.035mm Vel=0.044mm/s Acc=-0.024mm/s²`

## Homing Procedure

The ESP32 has a homing switch connected to GPIO23 (D23) that automatically sets the position reference.

### Homing Command
- `HOME` - Start automatic homing procedure

### How Homing Works
1. **Forward Movement**: Motor moves forward 5mm from current position
2. **Backward Search**: Motor moves backward until homing switch is triggered
3. **Position Zero**: When switch activates, current position is set to 0.0mm
4. **Completion**: Homing completes and normal operation resumes
5. **Safety**: Use `STOP` command to cancel homing at any time

### Homing Switch Behavior
- **Hardware**: GPIO23 with internal pulldown resistor
- **Logic**: LOW when open, HIGH when switch closes
- **Manual Trigger**: Switch can zero position even outside homing procedure
- **Safety Stop**: Any movement stops when switch is manually activated

## Acceleration Test Loop

The acceleration test loop moves the motor back and forth with constant acceleration/deceleration, useful for visualizing acceleration profiles.

### Command Format
`ACCELTEST:distance,acceleration`
- `distance`: Travel distance in mm
- `acceleration`: Acceleration/deceleration in mm/s²

### Example
`ACCELTEST:50,25` - Move 50mm with 25mm/s² acceleration

The motor will:
1. Accelerate from current position to reach the target distance
2. Decelerate to stop at the target
3. Reverse and repeat continuously
4. Use `STOP` to end the test

## Data Upload via Bluetooth

### Upload Process
1. Send `UPLOAD:START` to begin data upload mode
2. Send CSV data line by line (format: `time,displacement`)
3. Send `UPLOAD:END` to complete the upload and save to SPIFFS

### Data Format Requirements
- CSV format with two columns: time (seconds) and displacement (mm)
- Maximum 2400 data points
- Header line is optional and will be automatically detected
- Data persists across power cycles

### Example Upload Session
```
UPLOAD:START
0.05,-0.000040186
0.10,-0.000160827
0.15,-0.000362065
... (continue with all data lines)
UPLOAD:END
```

## Example Workflows

### 1. Basic Motor Testing
```
CONFIG                    # View current configuration
MICROSTEPS:1600          # Set microsteps for DM542T
PITCH:5.0                # Set ball screw pitch
VELOCITY:50              # Set max velocity to 50mm/s
ACCEL:25                 # Set acceleration to 25mm/s²
START                    # Start motor movement
MONITOR:ALL              # Enable all monitoring
STOP                     # Stop motor
```

### 2. Historic Data Playback
```
PLAYBACK:ON              # Enable playback mode
MONITOR:ALL              # Enable monitoring to see values
START                    # Start playback (always 1x speed)
STOP                     # Pause playback
PLAYBACK:OFF             # Return to normal mode
```

### 3. Configuration for DM542T
```
MICROSTEPS:1600          # Common setting for DM542T (8 microsteps)
INVERTPULSE:1            # DM542T typically needs inverted pulse
INVERTDIR:1              # Adjust based on your wiring
INVERTENABLE:1           # DM542T enable is typically active low
CONFIG                   # Verify settings
```

### 4. Homing Procedure
```
CONFIG                   # Check current position
HOME                     # Start homing procedure
# Motor will move forward 5mm, then backward to find switch
# When switch triggers, position becomes 0.0mm
CONFIG                   # Verify position is now 0.0mm
```

### 5. Plotting Real-Time Data
1. In Serial Monitor: `MONITOR:NONE` (clear any text output)
2. Close Serial Monitor
3. Open Serial Plotter
4. In a separate Serial Monitor instance, send: `MONITOR:VEL`
5. Send `START` to begin motor movement
6. Observe velocity plot in real-time

### 6. Acceleration Test Visualization
```
MONITOR:NONE             # Clear text output
ACCELTEST:100,50         # 100mm travel, 50mm/s² acceleration
# Open Serial Plotter
MONITOR:ACC              # Monitor acceleration
# Observe constant acceleration/deceleration pattern
```

### 7. Uploading New Historic Data
```
UPLOAD:START             # Begin upload mode
# Send CSV data lines via serial or Bluetooth
# Each line: time,displacement (e.g., "0.05,-0.000040186")
UPLOAD:END               # Save data to SPIFFS
CONFIG                   # Verify data points loaded
PLAYBACK:ON              # Enable playback mode
START                    # Play new data
```

## Other Commands
- `CONFIG` - Display all current configuration values
- `HELP` - Show available commands
