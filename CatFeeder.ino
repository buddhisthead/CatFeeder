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
#define SIMULATION_MODE

#define DEFAULT_DISPENSE_MS 800 // food-flow ms for a manual (button) dispense
#define MAX_WALL_MS 10000       // dispense-loop wall-clock safety limit (empty hopper)

#define BUTTON_HOLDOFF_MILLIS  1000 // 60000 // 60 additional seconds of waiting

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

bool buttonArmed = false;
int lastFeedSensorState = 0;

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
  
  // initialize the dispense pushbutton pin as an input:
  pinMode(buttonPin, INPUT);
  digitalWrite(buttonPin, HIGH); // turn on the pullup

  // Feed dispensed sensor
  pinMode(feedSensorPin, INPUT);     
  digitalWrite(feedSensorPin, HIGH); // turn on the pullup
  
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

// Dispense until targetFlowMs of *food-flow time* has accrued, or the MAX_WALL_MS
// wall-clock safety limit is reached (the hopper is likely empty). Returns the
// food-flow time actually accrued, in milliseconds.
//
// In SIMULATION_MODE there is no motor or sensor: we simply wait targetFlowMs so the
// whole command path (CLI -> network -> reply) can be tested on a bare board. Request
// a duration longer than the CLI's 10s timeout to exercise the timed-out path.
unsigned long dispenseForDuration(unsigned long targetFlowMs) {
#ifdef SIMULATION_MODE

  Serial.print("[SIM] Dispensing for ");
  Serial.print(targetFlowMs);
  Serial.println(" ms");
  digitalWrite(LED_BUILTIN, HIGH);
  delay(targetFlowMs);
  digitalWrite(LED_BUILTIN, LOW);
  Serial.println("[SIM] Dispense complete");
  return targetFlowMs;

#else

  Serial.print("Start Dispensing for ");
  Serial.print(targetFlowMs);
  Serial.println(" ms of food flow");
  digitalWrite(LED_BUILTIN, HIGH);

  // Start the actuator.
  #ifdef USING_SERVO
  dispenserServo.attach(servoPin);
  dispenserServo.write(70); // advance (90 == stop)
  #else
  analogWrite(servoPin, 255); // full power to the vibration motor
  #endif

  // Accrue elapsed time only while food is actively flowing past the beam. A piece of
  // food momentarily breaks the beam, so we treat "flowing" as having seen a beam
  // transition within FLOW_WINDOW_MS. Before the first piece is seen (empty plate at
  // hopper fill) no time accrues.
  const unsigned long FLOW_WINDOW_MS = 500;
  unsigned long wallStart = millis();
  unsigned long lastSample = wallStart;
  unsigned long lastFlowSeen = 0;
  unsigned long accruedFlow = 0;
  int lastBeam = digitalRead(feedSensorPin);
  bool sawFood = false;

  while (accruedFlow < targetFlowMs) {
    unsigned long now = millis();
    if (now - wallStart >= MAX_WALL_MS) {
      Serial.println("Dispense hit wall-clock safety limit (hopper empty?)");
      break;
    }

    int beam = digitalRead(feedSensorPin);
    if (beam != lastBeam) { // a piece of food passed the beam
      sawFood = true;
      lastFlowSeen = now;
      lastBeam = beam;
    }

    if (sawFood && (now - lastFlowSeen <= FLOW_WINDOW_MS)) {
      accruedFlow += (now - lastSample);
    }
    lastSample = now;
  }

  // Stop the actuator.
  #ifdef USING_SERVO
  dispenserServo.write(90); // stop
  dispenserServo.detach();
  #else
  analogWrite(servoPin, 0);
  #endif

  digitalWrite(LED_BUILTIN, LOW);
  Serial.print("STOP Dispensing! Accrued flow ms: ");
  Serial.println(accruedFlow);
  return accruedFlow;

#endif // SIMULATION_MODE
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

  // 3. Manual: the open/close switch triggers a default-duration dispense.
  int buttonState = digitalRead(buttonPin);
  if (buttonState == LOW && buttonArmed) {
    buttonArmed = false;
    Serial.println("Dispensing from button");
    dispenseForDuration(DEFAULT_DISPENSE_MS);
    // don't check the button until after a holdoff duration
    delay(BUTTON_HOLDOFF_MILLIS);
  } else if (buttonState == HIGH && buttonArmed == false) {
    // debounce
    delay(1000);
    buttonState = digitalRead(buttonPin);
    if (buttonState == HIGH) {
      buttonArmed = true;
    }
  }
}
