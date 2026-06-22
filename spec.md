# CatFeeder — Networked Dispense Specification

## Overview

The CatFeeder is a WiFi-enabled cat feeder controlled over a home LAN. It consists of
two cooperating software components:

1. **Firmware** — an Arduino sketch (`CatFeeder.ino`) running on an **Arduino UNO R4 WiFi**.
   It joins the home WiFi network and runs a TCP server on a known port. On receiving a
   dispense command, it drives the dispenser until the requested amount of **food-flow
   time** (in milliseconds) has elapsed, then replies when finished. Food-flow time is
   measured with the IR breakbeam (see [Dispense control loop](#dispense-control-loop)).

2. **`dispense` CLI** — a Python 3 command-line script run from a Linux machine on the
   same network. It sends a dispense command to a given IP:port, awaits the reply, logs
   success to stdout, and times out after 10 seconds. It is run manually for testing and
   later scheduled with cron for automatic feeding.

Both devices sit on the same trusted LAN inside the house.

```
  Linux machine                         Arduino UNO R4 WiFi
  ┌──────────────┐   TCP DISPENSE <ms>  ┌──────────────────┐
  │ dispense CLI │ ───────────────────► │  feederServer    │ ──► motor / servo
  │  (cron/shell)│ ◄─────────────────── │  (port 4242)     │
  └──────────────┘     DONE <ms>        └──────────────────┘
```

## Network protocol

- **Transport:** TCP.
- **Port:** default **4242** (configurable in firmware and via the CLI `--port` flag).
- **Encoding:** line-based ASCII text, terminated by `\n`. Human-readable so it can be
  exercised with `nc <ip> 4242`.

### Commands

| Direction        | Message              | Meaning                                                  |
|------------------|----------------------|----------------------------------------------------------|
| Client → Feeder  | `DISPENSE <ms>\n`    | Dispense until `<ms>` of food-flow time has accrued.     |
| Feeder → Client  | `DONE <ms>\n`        | Dispense completed; `<ms>` of food-flow time accrued.    |
| Feeder → Client  | `ERR <reason>\n`     | Command rejected (bad syntax / out-of-range).            |

`<ms>` is **food-flow time**, not wall-clock time — see
[Dispense control loop](#dispense-control-loop). If the hopper empties, the reply may
report fewer milliseconds than requested (the loop hit its wall-clock safety limit).

The feeder accepts **one client connection at a time**, processes a single command,
sends its reply, and closes the connection.

### Validation

- `<ms>` must be a non-negative integer (requested food-flow time).
- Malformed commands (unknown verb, non-numeric duration) yield `ERR <reason>\n`.
- Total run time is bounded by the dispense loop's **10000 ms wall-clock safety limit**
  regardless of the requested food-flow time (see below).

## Component A — Firmware (`CatFeeder.ino`)

- **Target:** Arduino UNO R4 WiFi, using the `WiFiS3` library (`WiFiServer` / `WiFiClient`).
- **WiFi credentials:** read from `arduino_secrets.h` (gitignored). A committed
  `arduino_secrets.h.example` documents the required `SECRET_SSID` / `SECRET_PASS` macros.
- **Startup:** join WiFi, print the assigned IP address to Serial (so the user can find
  the feeder's address), then start `WiFiServer feederServer(4242)`.
- **Dispensing:** a flow-controlled routine `dispenseForDuration(unsigned long ms)` drives
  the dispenser (servo or vibration motor on `servoPin`) until `ms` of **food-flow time**
  has accrued, with `LED_BUILTIN` on during the run. See the control loop below.
- **Compile-time mechanism switch:** honors the existing `#ifdef USING_SERVO` split
  (servo auger vs. vibrating trough).
- **Sensors:** the IR breakbeam (`feedSensorPin`) is used **only** to detect food flow
  inside the dispense loop — never for activation.
- **Activation:**
  - **Manual** — the existing open/close switch (`buttonPin`) triggers a dispense of the
    default duration.
  - **Network** — a `DISPENSE <ms>` command over TCP.
  - **Serial** — retained for development/testing.
  - The payment-beam activation path is **removed**.

### Dispense control loop

`dispenseForDuration(ms)` accrues *food-flow time* rather than wall-clock time:

1. Turn the dispenser motor on and start a wall-clock timer.
2. Repeatedly sample the IR breakbeam:
   - When food **is** detected flowing past the beam, accrue elapsed time toward the `ms`
     target.
   - When **no** food is detected, the accrual timer pauses (time does not count). This
     covers the empty vibration plate when the hopper is first filled, and gaps in flow.
3. Stop when **either**:
   - accrued food-flow time reaches the requested `ms`, **or**
   - the wall-clock timer reaches the **10000 ms safety limit** (the hopper is likely
     empty) — whichever comes first.
4. Turn the motor off and report the food-flow time actually accrued.

This same routine backs both the manual button and the network command.

### Error handling

- If WiFi fails to join at startup, retry/report on Serial.
- Malformed commands are rejected with `ERR`, no dispense occurs.
- An empty hopper is handled gracefully: the loop exits at the 10 s wall-clock limit and
  reports the (smaller) accrued food-flow time.

## Component B — `dispense` CLI

### Usage

```
dispense <ip> [-p/--port PORT] [-d/--duration MS]
```

- `<ip>` — **required** positional argument; the feeder's IP address.
- `-p` / `--port` — optional; defaults to **4242**.
- `-d` / `--duration` — optional; dispense duration in milliseconds, defaults to **800**.

### Behavior

1. Open a TCP connection to `<ip>:<port>` with a **10-second** timeout.
2. Send `DISPENSE <ms>\n`.
3. Await the reply.
   - On `DONE <ms>` → log a success line to stdout, exit code `0`.
   - On `ERR <reason>` → print the error to stdout, exit non-zero.
   - On timeout (no reply within 10s) → print a timeout error to stdout, exit non-zero.
   - On connection failure → print the error to stdout, exit non-zero.

### Implementation notes

- Python 3, standard library only (`socket`, `argparse`, `sys`).
- Shebang `#!/usr/bin/env python3`; the file is made executable.

## Deployment — scheduled feeding

Once the CLI works from the shell, automatic feeding is configured with cron on the
Linux machine. Example (feed at 7:00am and 6:00pm daily):

```cron
0 7,18 * * *  /path/to/dispense 192.168.1.50 -d 800 >> /var/log/catfeeder.log 2>&1
```

## Testing

- **Smoke test:** `printf 'DISPENSE 500\n' | nc <ip> 4242` and confirm a `DONE 500` reply.
- **CLI happy path:** `./dispense <ip> -d 800` from the Linux shell → success logged,
  dispenser runs ~800 ms.
- **Timeout path:** run against an unreachable IP and confirm a timeout error after ~10s.

## Out of scope (v1)

- Authentication / encryption — the LAN is trusted.
- Multiple simultaneous clients.
- Service discovery (mDNS/Bonjour) — the IP is supplied to the CLI explicitly.
