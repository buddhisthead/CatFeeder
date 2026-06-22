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

#define DISPENSE_MILLIS 500 // milliseconds of food dispensing. 1 sec was too much food.
#define SENSOR_HOLDOFF_MILLIS  2000 // 2 additional seconds of waiting
#define BUTTON_HOLDOFF_MILLIS  1000 // 60000 // 60 additional seconds of waiting

const int beamSensorPin = 8; // Payment sensor (reversed for development)
const int feedSensorPin = 4; // Feed dispensed sensor
const int buttonPin = 7;     // Perch sensor
const int servoPin = 9;      // Drive either a server or the vibrator motor driver

#ifdef USING_SERVO
// Servo Driver
Servo dispenserServo;
#else
// Motor Driver has no additional setup
#endif

// TCP server that listens for DISPENSE commands over the network.
WiFiServer feederServer(FEEDER_PORT);

int speedDelta = 10;
int lastState = 0;  // variable for remembering the last IR beam status
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

  // The Payment sensor
  pinMode(beamSensorPin, INPUT);     
  digitalWrite(beamSensorPin, HIGH); // turn on the pullup

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

void loop() {


  
  // put your main code here, to run repeatedly:
  if (Serial.available() > 0) {
    // read incoming serial data:
    char inChar = Serial.read();
    // Echo the value back to the monitor
    Serial.println(inChar);

    int value = inChar - '0';
    Serial.println(value);
    
    if (value >= 0 && value <= 9) {
      speedDelta = value * 10;
      Serial.println(speedDelta);
      dispense(speedDelta, DISPENSE_MILLIS);
    } else {
      Serial.println("Out of range: ");
      Serial.println(value);
    }
  } else {
    // check if the pushbutton is pressed.
    // if it is, the buttonState is LOW:
    // read the state of the pushbutton value:
    int buttonState = digitalRead(buttonPin);
    if (buttonState == LOW && buttonArmed) {
      buttonArmed = false;
      jiggle();
      // dispense food
      Serial.println("Dispensing from button");
      dispense(speedDelta, DISPENSE_MILLIS);
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

    // check if the sensor beam is broken
    // if it is, the sensorState is LOW:
    // read the state of the IR sensor:
    int sensorState = digitalRead(beamSensorPin);
    if (sensorState && !lastState) {
      Serial.println("Payment Beam Unbroken");
    } 
    if (!sensorState && lastState) {
      Serial.println("Payment Beam Broken");
    }
    
    if (sensorState == LOW && lastState != sensorState) {
      // agitate food first
      jiggle();
      // dispense food
      Serial.println("Dispensing from payment sensor");
      dispense(speedDelta, DISPENSE_MILLIS);
      // don't accept new requests to dispense until after a holdoff duration
      delay(SENSOR_HOLDOFF_MILLIS);
    }

    lastState = sensorState;
  }
}
