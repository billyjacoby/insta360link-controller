# Zoom Feature Implementation
In preparation for a PCB implementation of this system which will incorporate camera zoom functionality, the feature is being tested on the breadboard prototype. 

## Hardware Interface Changes
Per `/hardware/specs.md` the 10K potentiometer ("pot") will have its output connected to `GPIO33`, with the legs at +3.3 V and GND. 

## Firmware Changes

### Polling and Update
The firmware must read the potentiometer value, and use it to send an update command to the daemon. The zoom direction and speed should be based on how far the potentiometer has been moved from its center value. The speed should have a gain on it that is configurable from code. 

### New Calibration Feature
Calibrating the usable range of a potentiometer is slightly different from calibrating the joysticks, in that it doesn't return to center by default. So, the calibration function for this component has to be different, and should be run with a different command, like `calZoom`. The procedure is as follows:
1. Prompt user to spin the potentiometer all the way to the left, then take ADC reading
2. Prompt the user to spin the potentiometer fully the other way, and take another ADC reading
3. Find the center value in that ADC range, and print it to the serial monitor. Prompt the user to move the pot until ADC readings match the center value. Take new ADC readings at a frequency of 5 Hz, but make this adjustable in code. 
4. Once the pot is centered, print a confirmation message to the serial monitor. 

## Daemon Changes
The daemon must accept these zoom change commands as valid inputs, and relay the appropriate commands to the camera. 

## Testing and Verification
1. Build and flash the firmware. 
2. End the daemon process currently active, then rebuild and restart. 
3. At that point, alert the user that the system is ready for them to test. 
