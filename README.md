# CatFeeder

A WiFi-enabled cat feeder that dispenses kibble on demand or on a schedule. It has two
parts:

1. **Firmware** (`CatFeeder.ino`) — runs on an **Arduino UNO R4 WiFi**, joins your home
   network, and listens on a TCP port for dispense commands. It can also be triggered by
   a button on the feeder.
2. **`dispense` CLI** — a small Python script you run from a Linux/macOS machine on the
   same network to dispense food, manually or from `cron`.

See [`spec.md`](./spec.md) for the full specification and [`plan.md`](./plan.md) for the
implementation history.

## How dispensing works

Food is dispensed for a requested amount of **food-flow time** (in milliseconds). A
vibrating trough (or optional servo auger) drives kibble past an IR breakbeam, and the
firmware accrues flow time only while food is actually flowing — so the startup lead
before kibble reaches the beam, and any gaps in flow, don't count. A 10-second
wall-clock safety limit stops the motor if the hopper runs empty.

## Activation

- **Button** — hold the button to dispense; release to stop. On release the Serial
  Monitor prints the food-flow time, which is handy for choosing a duration for the CLI.
- **Network** — a `DISPENSE <ms>` command over TCP (see [Protocol](#protocol)).
- **Serial** — type a digit `1`–`9` in the Serial Monitor to dispense that many hundred
  milliseconds (e.g. `8` → 800 ms). Handy for bench testing.

## Hardware

- **Board:** Arduino UNO R4 WiFi (Renesas RA4M1 + ESP32-S3 radio).
- **Dispenser:** vibrating trough driven by a DC motor via a MOSFET driver (default), or
  a continuous-rotation servo auger (`#define USING_SERVO`).
- **Sensor:** one IR breakbeam across the dispense chute for food-flow detection.
- **Button:** a normally-open switch between the button pin and GND.

| Function            | Pin |
|---------------------|-----|
| IR breakbeam (flow) | 4   |
| Button              | 7   |
| Motor / servo (PWM) | 9   |

3D-printable parts:
- Vibrating trough: https://www.thingiverse.com/thing:2118961
- Auger: https://www.thingiverse.com/thing:958180

## Firmware setup

1. Copy the secrets template and fill in your WiFi credentials:
   ```sh
   cp arduino_secrets.h.example arduino_secrets.h
   # edit arduino_secrets.h -> SECRET_SSID, SECRET_PASS
   ```
   `arduino_secrets.h` is gitignored and never committed.
2. Open `CatFeeder.ino` in the Arduino IDE, select **Arduino UNO R4 WiFi**, and flash.
3. Open the Serial Monitor at **9600 baud** and note the printed IP address — that's the
   feeder's address for the CLI.

### Compile-time options (top of `CatFeeder.ino`)

| Define              | Default     | Purpose                                                        |
|---------------------|-------------|----------------------------------------------------------------|
| `SIMULATION_MODE`   | off         | No sensors/actuators — dispensing is a timed stub. Test the full CLI→network path on a bare board. |
| `USE_FLOW_SENSOR`   | on          | Gate food-flow time on the IR beam. Off = run the motor for the full requested duration (motor bring-up). |
| `USING_SERVO`       | off         | Use a servo auger instead of the vibrating trough.             |
| `DEBUG_SENSOR`      | on          | Log IR beam transitions to Serial. Comment out for normal use. |
| `FEEDER_PORT`       | `4242`      | TCP port the feeder listens on.                                |
| `SAMPLE_INTERVAL_MS`| `5`         | Flow-loop sample period.                                       |
| `FLOW_WINDOW_MS`    | `150`       | Food counts as "flowing" while a piece passed this recently.   |
| `MAX_WALL_MS`       | `10000`     | Wall-clock safety limit per dispense.                          |

## The `dispense` CLI

```
dispense <ip> [-p PORT] [-d MS]
```

- `<ip>` — feeder IP address (required).
- `-p` / `--port` — TCP port (default `4242`).
- `-d` / `--duration` — food-flow time in ms (default `800`).

Examples:
```sh
./dispense 10.1.1.143            # default 800 ms on port 4242
./dispense 10.1.1.143 -d 1790    # dispense ~1790 ms of food flow
```

It connects, sends `DISPENSE <ms>`, waits up to **10 seconds** for the reply, and logs
the result. Exit code `0` on success, non-zero on rejection, timeout, or connection
failure. Python 3, standard library only.

### Protocol

Line-based ASCII over TCP (so you can also test with `nc <ip> 4242`):

| Direction       | Message           | Meaning                                          |
|-----------------|-------------------|--------------------------------------------------|
| Client → Feeder | `DISPENSE <ms>\n` | Dispense until `<ms>` of food-flow time accrues. |
| Feeder → Client | `DONE <ms>\n`     | Completed; `<ms>` of food-flow time accrued.     |
| Feeder → Client | `ERR <reason>\n`  | Rejected (bad syntax, or duration ≤0 or >10000). |

## Scheduled feeding with cron

Once the CLI works from the shell, schedule it with `cron` on the Linux machine. Example
— feed at 7:00 am and 6:00 pm daily, logging to a file:

```cron
0 7,18 * * *  /path/to/dispense 10.1.1.143 -d 1790 >> /var/log/catfeeder.log 2>&1
```

Edit your crontab with `crontab -e`. Use an absolute path to `dispense`, and check the
log to confirm each run reports `OK`.

## License

MIT — see the header in `CatFeeder.ino`. Contributions welcome.
