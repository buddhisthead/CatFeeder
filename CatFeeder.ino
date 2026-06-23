// Copyright One Mind LLC, 2026
// Contact: Chris Tilt at chris.tilt@gmail.com
// License: MIT - permission is granted to use and modify for your use. Feel free to
//                make a PR with any improvements. Thanks!
//
// An Arduino sketch for a Wifi enabled Cat Feeder (kibbles)
//
// This feeder uses a servo or vibrating trough to dispense food. It supports two kinds
// of activations for dispensing food:
// 1. Press a button the feeder
// 2. An activation packet over the network

// Notes
// -----
// The dispensing of food has had some revisions. Version 1 used an auger, which
// jams easily, even with fancy jiggling and forward/reverse action. Version 2
// uses a vibration trough feeder, which also uses an additional IR breakbeam
// detector to know when a unit of food has been served. This is far superior and is
// commonly used in industrial applications. No jams, and precise control over the
// amount of reward dispensed.
//
// Hardware
// --------
// Vibrating trough
// 3D printed parts for the vibrating trough feeder are here:
// https://www.thingiverse.com/thing:2118961
// A motor that was recommended by the original author is here:
// https://www.ebay.com/itm/vibration-mini-vibrating-motor-DC-1-5V-Micro-Electric-Motor-for-Toy-Massager-DIY/121988191575
// I used this one: https://www.amazon.com/gp/product/B08264XXMH and it's too narrow of a diameter
// for the motor holder, so I had to wrap it in a rigid foil tape.
// Parts for an auger are here:
// https://www.thingiverse.com/thing:958180
//
// Electronics
// -----------
// I used an Arduino UNO, but this code will work on nearly any arduino with enough pins
// for your needs. You need three digital inputs and one PWM output. I used two IR breakbeam
// detectors: one for sensing payment and one for sending food reward. The PWM output is used
// to drive a servo for an auger or a DC motor driver for a vibrating feed trough. I prefer
// the later because it jams less often and has more precise control over the amount of food
// dispensed.
//
// TODO: describe the circuit
// 

// To use a servo, uncomment the following. You can attach an auger to the servo instead of
// using a vibrating feed trough.
// #define USING_SERVO

#ifdef USING_SERVO
#include <Servo.h>
#endif

// WiFi networking (Arduino UNO R4 WiFi). Credentials live in arduino_secrets.h,
// which is gitignored. Copy arduino_secrets.h.example to get started.
#include <WiFiS3.h>
#include "arduino_secrets.h"

#define FEEDER_PORT 4242 // TCP port the feeder listens on for DISPENSE commands

// Simulation mode: build with no sensors or actuators attached so the full command
// path (CLI -> network -> reply) can be exercised on a bare Arduino board. In this
// mode dispensing is a timed stub. Comment this out to build for real hardware.
// #define SIMULATION_MODE

// Hardware bring-up gate (only relevant when SIMULATION_MODE is off): leave this
// commented out to run the motor for the full requested duration with the IR beam
// bypassed -- Stage A, motor-only test. Uncomment once the flow sensor is wired in so
// accrued food-flow time is gated on detected flow.
#define USE_FLOW_SENSOR

#define MAX_WALL_MS 10000     // dispense-loop wall-clock safety limit (empty hopper)
#define SAMPLE_INTERVAL_MS 5  // dispense-loop sample period (ms)
#define FLOW_WINDOW_MS 150    // food counts as "flowing" while a piece passed this recently

// Debug: when defined, print IR beam state transitions to Serial so you can verify the
// sensor wiring and polarity (wave something through the beam and watch). Comment out
// for normal operation.
#define DEBUG_SENSOR

const int feedSensorPin = 4; // Feed dispensed sensor (food-flow detection only)
const int buttonPin = 7;     // Manual dispense button (open/close switch)
const int servoPin = 9;      // Drive either a servo or the vibrator motor driver

#ifdef USING_SERVO
// Servo Driver
Servo dispenserServo;
#else
// Motor Driver has no additional setup
#endif

// TCP server that listens for DISPENSE commands over the network.
WiFiServer feederServer(FEEDER_PORT);

int lastFeedSensorState = 0;

// Flow-detection state, reset by actuatorOn() at the start of each dispense. A kibble
// shows as a HIGH->LOW beam edge; food is "flowing" while pieces keep arriving.
int flowPrevBeam = HIGH;
unsigned long flowLastPieceMs = 0;
bool flowSawPiece = false;

// Join the home WiFi network and start the dispense server. Blocks until connected
// so the feeder is always reachable once setup() completes; prints the assigned IP
// to Serial so you can find the feeder's address.
void connectWiFi() {
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("WiFi module not found - halting.");
    while (true) { ; } // nothing to do without WiFi
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(SECRET_SSID);
  while (WiFi.begin(SECRET_SSID, SECRET_PASS) != WL_CONNECTED) {
    Serial.println("  ...retrying in 5s");
    delay(5000);
  }

  Serial.print("Connected. Feeder IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Listening for DISPENSE commands on port ");
  Serial.println(FEEDER_PORT);
}

void setup() {
  // put your setup code here, to run once:

  // Serial port to 9600 baud
  Serial.begin(9600);

  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);
  
  // initialize the dispense pushbutton pin as an input with the internal pull-up.
  // The switch is normally-open between buttonPin and GND, so idle reads HIGH and a
  // press reads LOW. Use INPUT_PULLUP: the old AVR "INPUT + digitalWrite(HIGH)" trick
  // does not engage the pull-up on the UNO R4 WiFi (Renesas RA4M1).
  pinMode(buttonPin, INPUT_PULLUP);

  // Feed dispensed sensor (IR breakbeam). Its output is open-collector and needs a
  // pull-up; use INPUT_PULLUP for the same reason as the button -- the legacy
  // digitalWrite(HIGH) trick does not engage the pull-up on the UNO R4 WiFi (Renesas
  // RA4M1). With the pull-up, an intact beam reads HIGH and a broken beam (food
  // passing) reads LOW.
  pinMode(feedSensorPin, INPUT_PULLUP);
  
  #ifdef USING_SERVO
  
  // set servoPin for PWM output
  dispenserServo.attach(servoPin);

  // set servo rotation speed for 0. Continuous servo's domain is 0 .. 90 .. 180
  dispenserServo.write(90);
  jiggle();

  #else

  // Motor driver will take PWM from Arduino
  pinMode(servoPin, OUTPUT);
  // initialize feed sensor to whatever it is now. It should be clear.
  lastFeedSensorState = digitalRead(feedSensorPin);

  #endif // USING_SERVO

  // Join WiFi and start listening for network dispense commands.
  connectWiFi();
  feederServer.begin();
}

// ---------------------------------------------------------------------------
// Legacy hardware dispense routines, retained for reference during bring-up.
// Activation now flows through dispenseForDuration() (defined below); these are
// not called yet and will be reconciled when we tune the real flow-control loop.
// ---------------------------------------------------------------------------

#ifdef USING_SERVO

// Using the Auger,
// Dispense some food. This is best done after a jiggle so that food is settled.
//
// Servos tend to whine and creep unless we control them with a very expensive
// hardware driver. We'll use the cheap on-board pulse width modulation pins on
// the Arduino; and to avoid creep and whine, we will only congfigure the output
// pins when we're using them. Thus the attach and detach.
//
// Also, my servo turns 360 degrees. Set to 90 to stop it. 180 full one way,
// -180 is full the other way.
//
void dispense(int delta, int delayMillis) {
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on
  dispenserServo.attach(servoPin);   // connect to the servo

  int reverseDelay = 100;
  int offset = 20;
  int itterations = delayMillis / reverseDelay / 3;
  for (int i=0; i<itterations; i++) {
    dispenserServo.write(90-offset); // advance for 2x time that will reverse
    delay(reverseDelay*2);
    dispenserServo.write(90+offset); // reverse for 1x time
    delay(reverseDelay);
  }
  dispenserServo.write(90); // stop servo
  
  dispenserServo.detach(); // disconnect so the servo doesn't creep and whine
  
  digitalWrite(LED_BUILTIN, LOW);   // turn the LED off
}

void jiggle() {
  dispenserServo.attach(servoPin);
  
  dispenserServo.write(90);
  int delayMillis = 100; // any shorter and the server doesn't recover equally in both directions
  int offset = 30;
  for (int i=0; i<6; i++) {
    dispenserServo.write(90-offset);
    delay(delayMillis);
    dispenserServo.write(90+offset);
    delay(delayMillis);
  }
  dispenserServo.write(90);
  
  dispenserServo.detach();
}

#else

// Dispense food using vibrating feed trough.
//
// Vibrate food until we detect a piece of food passing the IR breakbeam at dispenser.
// Timeout if we don't see food in the dispenser tube.
void dispense(int delta, int delayMillis) { // parameters ignored for vibration
  Serial.println("Start Dispensing!");
  digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on to indicate dispensing
  int pwmOutput = 255; // full power to driver
  analogWrite(servoPin, pwmOutput);
  // wait to detect food dispensed, but timeout after too long
  unsigned long startTime = millis();
  Serial.print("\nFood dispensing time is now: ");
  Serial.println(startTime);
  const unsigned long timeout = 15000; // maximum vibration time is 5 seconds
  bool gaveReward = false;
  bool timedOut = false;
  while (gaveReward == false && timedOut == false) {
    unsigned long timeNow = millis();
    if (timeNow > startTime + timeout) {
      Serial.print("\nFood dispensing timed out: ");
      Serial.println(timeNow);
      Serial.println(startTime + timeout);
      timedOut = true;
    }
    // check if the sensor beam is broken
    // if it is, the sensorState is LOW:
    // read the state of the IR sensor:
    int sensorState = digitalRead(feedSensorPin);
    if (sensorState != lastFeedSensorState) {
      // Food detected!
      Serial.println("\nFood detected");
      gaveReward = true;
    } else {
      // Serial.print(".");
    }
  }

  Serial.println("STOP Dispensing!");
  // stop PWM output
  analogWrite(servoPin, 0);
  
  digitalWrite(LED_BUILTIN, LOW);   // turn the indicator LED off
}

void jiggle() {
  // nothing to jiggle for vibrator.
}

#endif

// ---------------------------------------------------------------------------
// Dispenser primitives. All of the SIMULATION_MODE / USING_SERVO / USE_FLOW_SENSOR
// conditionals live in these three helpers, so the dispense routines that use them
// read cleanly and only express *when to stop*.
// ---------------------------------------------------------------------------

// Start the dispenser (motor or servo) and light the activity LED.
void actuatorOn() {
  digitalWrite(LED_BUILTIN, HIGH);

  // Reset flow tracking for this dispense.
  flowSawPiece = false;
  flowLastPieceMs = 0;
#ifdef USE_FLOW_SENSOR
  flowPrevBeam = digitalRead(feedSensorPin);
#endif

#ifndef SIMULATION_MODE
  #ifdef USING_SERVO
  dispenserServo.attach(servoPin);
  dispenserServo.write(70);   // advance (90 == stop)
  #else
  analogWrite(servoPin, 255); // full power to the vibration motor
  #endif
#endif
}

// Stop the dispenser and turn the activity LED off.
void actuatorOff() {
#ifndef SIMULATION_MODE
  #ifdef USING_SERVO
  dispenserServo.write(90);   // stop
  dispenserServo.detach();
  #else
  analogWrite(servoPin, 0);
  #endif
#endif
  digitalWrite(LED_BUILTIN, LOW);
}

// Run one SAMPLE_INTERVAL_MS sample period and return the food-flow time to accrue for
// it: a full interval if food is flowing, otherwise zero. In SIMULATION_MODE (no sensor)
// or motor-only bring-up (USE_FLOW_SENSOR off) every period counts, so dispensing runs
// for the requested wall-clock time.
//
// With the flow sensor, kibble passes as discrete pieces, so the beam is only blocked a
// small fraction of the time even during good flow. We therefore treat food as "flowing"
// whenever the beam is currently broken OR a piece passed within FLOW_WINDOW_MS -- steady
// flow accrues at the wall-clock rate, while an empty plate or a real gap pauses accrual.
unsigned long dispenseSample() {
#if defined(SIMULATION_MODE) || !defined(USE_FLOW_SENSOR)
  delay(SAMPLE_INTERVAL_MS);
  return SAMPLE_INTERVAL_MS;
#else
  int beam = digitalRead(feedSensorPin);    // LOW == beam broken == food present
  if (beam == LOW && flowPrevBeam == HIGH) { // a piece just entered the beam
    flowSawPiece = true;
    flowLastPieceMs = millis();
  }
  flowPrevBeam = beam;

  delay(SAMPLE_INTERVAL_MS);

  bool flowing = (beam == LOW) ||
                 (flowSawPiece && (millis() - flowLastPieceMs <= FLOW_WINDOW_MS));
  return flowing ? SAMPLE_INTERVAL_MS : 0;
#endif
}

// Dispense until targetFlowMs of food-flow time has accrued, or the MAX_WALL_MS
// wall-clock safety limit is reached (the hopper is likely empty). Returns the
// food-flow time actually accrued, in milliseconds. Used by the network and serial paths.
unsigned long dispenseForDuration(unsigned long targetFlowMs) {
  Serial.print("Start Dispensing for ");
  Serial.print(targetFlowMs);
  Serial.println(" ms of food flow");

  actuatorOn();
  unsigned long accruedFlow = 0;
  unsigned long wallStart = millis();
  while (accruedFlow < targetFlowMs) {
    if (millis() - wallStart >= MAX_WALL_MS) {
      Serial.println("Dispense hit wall-clock safety limit (hopper empty?)");
      break;
    }
    accruedFlow += dispenseSample();
  }
  actuatorOff();

  Serial.print("STOP Dispensing! Accrued flow ms: ");
  Serial.println(accruedFlow);
  return accruedFlow;
}

// Dispense for as long as the button is held (active-low), bounded by the same
// wall-clock safety limit. Returns the food-flow time accrued while held -- a handy way
// to dial in a good dispense duration by feel.
unsigned long dispenseWhileButtonHeld() {
  Serial.println("Dispensing while button held");

  actuatorOn();
  unsigned long accruedFlow = 0;
  unsigned long wallStart = millis();
  while (digitalRead(buttonPin) == LOW) {
    if (millis() - wallStart >= MAX_WALL_MS) {
      Serial.println("Dispense hit wall-clock safety limit");
      break;
    }
    accruedFlow += dispenseSample();
  }
  unsigned long elapsedMs = millis() - wallStart;
  actuatorOff();

  Serial.print("Button released after ");
  Serial.print(elapsedMs);
  Serial.println(" ms of dispensing");
  Serial.print("Food-flow time (use as 'dispense -d'): ");
  Serial.print(accruedFlow);
  Serial.println(" ms");
  return accruedFlow;
}

// Handle one pending network client, if any: read a single DISPENSE command line,
// run the dispense, and reply with DONE <accrued_ms> or ERR <reason>.
void handleNetwork() {
  WiFiClient client = feederServer.available();
  if (!client) {
    return;
  }

  Serial.println("Client connected");
  String line = client.readStringUntil('\n');
  line.trim();
  Serial.print("Command: ");
  Serial.println(line);

  if (line.startsWith("DISPENSE")) {
    int sp = line.indexOf(' ');
    if (sp < 0) {
      client.print("ERR missing duration\n");
    } else {
      long ms = line.substring(sp + 1).toInt();
      if (ms <= 0 || ms > 10000) {
        client.print("ERR invalid duration\n");
      } else {
        unsigned long accrued = dispenseForDuration((unsigned long)ms);
        client.print("DONE ");
        client.print(accrued);
        client.print("\n");
      }
    }
  } else {
    client.print("ERR unknown command\n");
  }

  client.stop();
  Serial.println("Client disconnected");
}

void loop() {
#ifdef DEBUG_SENSOR
  // Print the IR beam state whenever it changes, so you can confirm wiring/polarity.
  static int lastBeamDebug = -1;
  int beamNow = digitalRead(feedSensorPin);
  if (beamNow != lastBeamDebug) {
    Serial.print("Beam: ");
    Serial.println(beamNow == LOW ? "BROKEN (food present)" : "clear");
    lastBeamDebug = beamNow;
  }
#endif

  // 1. Network: a DISPENSE command from the dispense CLI over TCP.
  handleNetwork();

  // 2. Serial: type a digit 1-9 in the Serial Monitor to dispense that many hundred
  //    ms of food-flow time (e.g. '8' -> 800 ms). Handy for local testing.
  if (Serial.available() > 0) {
    char inChar = Serial.read();
    int value = inChar - '0';
    if (value >= 1 && value <= 9) {
      Serial.print("Dispensing from serial: ");
      Serial.print(value * 100);
      Serial.println(" ms");
      dispenseForDuration(value * 100);
    }
    return;
  }

  // 3. Manual: hold the button to dispense, release to stop.
  if (digitalRead(buttonPin) == LOW) {
    delay(20); // debounce
    if (digitalRead(buttonPin) == LOW) {
      dispenseWhileButtonHeld();
    }
  }
}
