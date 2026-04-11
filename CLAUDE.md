In this asf_efi workspace we are developing a fuel injection controller (ecu) for a small single cylinder engine.

the project is done on an Arduino Nano microcontroller with an ATMega328P MCU.

The ECU will control a fuel pump and regulate fuel pressure. It will open and close a fuel injector. The injector pulse width is modulated according to a map, like usual injection programs. The ECU will read a throttle position sensor (TPS), a fuel pressure sensor (FPS), outside temperature (IAT), engine temperature (ET) and a crankshaft position pulse (CKPS). The injection amount is mainly determined by RPM and throttle position, using a 12x5 map (rpm x TPS). then there should be additional correction maps for IAT and ET which multiply the injection value by a coefficient. Each correction table has 5 temperature bins. IAT breakpoints: −20, 0, 20, 40, 70 °C. ET breakpoints: 0, 25, 50, 80, 100 °C.

There will be data communication to a PC which is used to read sensor values in real time and also modify the injection map. There has to be a suitable serial interface, with packet frames and CRC error detection.

The injection map and some other tuning values will be saved to EEPROM. It must load the values in startup and save values when they are modified.

We need to also program a PC side application which does the value reading and programming. This will be coded in Python. It should be put into a subfolder. It needs to have a GUI with readouts for RPM and all the sensor values, including pump duty cycle, injector duty cycle, and battery voltage. It needs to have a 2D table interface for displaying and modifying the injection map values. The table has to have an interactive cursor which shows the current injection value that is being used.

## Pin connection information

* TPS: A0, 0v is zero throttle, 5v is full
* FPS: A1, 0.5v is 0 bar, 4.5v is 10 bar
* CKPS: D8, falling edge, 1 pulse per rev, should use timer input capture
* IAT: A2, needs to have a lookup table for values
* ET: A3, needs to have a lookup table 
* injector output: D4, high on
* fuel pump: D3, needs to have pwm generation
* green LED: D12, high on
* red LED: D13, high on

## Fuel pressure control

the ECU has to control fuel pressure using a PID controller. it will read the current pressure and adjust the pump PWM signal accordingly. the PID coefficients can be tuned using the PC app. also the target pressure can be tuned. there has to be a RPM table for the target pressure, 2 values is enough. there will be one threshold rpm where it switches to another pressure value.

the fuel pump must not be immediately enabled at start. it should be enabled only after there has been two CKPS pulses and the rpm value has been obtained. there must be a test function to prime and test the pump from PC.

the pump can operate in two modes selectable from the PC app: PID mode (active pressure regulation, default) and always-on mode (full PWM, no pressure feedback).

## Injector opening frequency

it needs to use a variable frequency so that under a certain rpm threshold, the injection is synchronized to the CKPS signal, injecting once per revolution. above the threshold it will switch to a constant frequency (60hz) signal.

## Indicator leds

The green led must be lit when the system is initialized. When the fuel pump is being run, the green led must blink at 5hz roughly. When the injector is active, the red led must light at the same rate.

## General considerations

Care must be taken that the injector is never left on if the engine stops. There must be a detection when there is no more CKPS signal (timeout).

## Coding guidelines

this will be compiled and run in Arduino IDE but it should still be separated into multiple files.

## Feature documentation

Detailed implementation notes for specific features are kept in the `docs/` folder. Consult these before modifying related code.

| File | Topic |
|---|---|
| [docs/accel_pump.md](docs/accel_pump.md) | Accelerator pump enrichment — TPS rate detection, linear decay logic, EEPROM layout (addresses 200–206), serial commands 0x15/0x16, PC app integration |