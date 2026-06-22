# CatFeeder

An Arduino sketch for a WiFi-enabled cat feeder that dispenses kibble.

## Activation

Food can be dispensed two ways:

1. Pressing a button on the feeder.
2. An activation packet sent over the network. *(planned — not yet implemented)*

## Dispensing mechanisms

The sketch supports two interchangeable dispensing methods, selected at compile time:

- **Vibrating trough (default)** — a DC motor vibrates a trough to feed kibble, with an
  IR breakbeam sensor confirming each unit dispensed. No jams and precise control.
- **Servo auger** — define `USING_SERVO` to drive an auger with a continuous-rotation servo.

## Hardware

- **Board:** Arduino UNO (or any Arduino with enough pins — 3 digital inputs, 1 PWM output).
- **Sensors:** IR breakbeam detectors for payment and for confirming dispensed food.
- **Pins:** payment beam = 8, feed-dispensed sensor = 4, button/perch = 7, PWM drive = 9.

3D-printable parts:
- Vibrating trough: https://www.thingiverse.com/thing:2118961
- Auger: https://www.thingiverse.com/thing:958180

## License

MIT — see the header in `CatFeeder.ino`. Contributions welcome.
