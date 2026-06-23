# CatFeeder — Networked Dispense Implementation Plan

Working plan for implementing the design in [`spec.md`](./spec.md). Phases are ordered so
that each one is independently testable before moving to the next.

## Progress

- **Phase 1 — done.** WiFi scaffold on the UNO R4 WiFi; boots, prints IP, listens on 4242.
- **Phase 2 — done (simulation).** Network `DISPENSE`/`DONE` command handling, button as
  manual trigger, payment-beam activation removed. A compile-time `SIMULATION_MODE`
  (defined by default) makes dispensing a timed stub so the whole path runs on a bare
  board with no sensors/actuators. The real flow-controlled loop is written behind the
  `#else` and awaits hardware bring-up.
- **Phase 3 — done.** `dispense` Python CLI; happy / refused / 10s-timeout paths verified
  locally against a fake feeder.
- **Phase 4 — done (hardware).** Brought up on real hardware: 10ms→5ms sampling, button
  is hold-to-dispense (reports food-flow ms), INPUT_PULLUP fixes for button and IR beam
  on the R4. The dispense loop was refactored into `actuatorOn/Off`/`dispenseSample`
  primitives, and food-flow time now measures flow *duration* (edge detection + 150ms
  window) rather than beam-occlusion time. Verified 2490ms held → 1790ms food-flow.
- **Phase 5 — done.** README updated to the implemented design with CLI usage, protocol,
  compile-time options, and a cron scheduling example.
- **Remaining cleanup:** comment out `DEBUG_SENSOR` before considering this production.

## Phase 1 — Firmware networking scaffold

**Goal:** the feeder joins WiFi and accepts a TCP connection on port 4242.

- [ ] Create `arduino_secrets.h.example` with `SECRET_SSID` / `SECRET_PASS` macros (committed).
- [ ] Add `arduino_secrets.h` to `.gitignore` (real credentials, never committed).
- [ ] In `CatFeeder.ino`:
  - [ ] `#include "WiFiS3.h"` and `#include "arduino_secrets.h"`.
  - [ ] Add a `#define FEEDER_PORT 4242` and `WiFiServer feederServer(FEEDER_PORT);`.
  - [ ] In `setup()`, join WiFi (`WiFi.begin(SECRET_SSID, SECRET_PASS)`), wait for
        connection, and `Serial.println(WiFi.localIP())` so the IP is discoverable.
  - [ ] `feederServer.begin();`

**Verify:** flash, open Serial Monitor, confirm the board prints its IP and that
`nc <ip> 4242` connects (no command handling yet).

## Phase 2 — Firmware dispense loop & command handling

**Goal:** `DISPENSE <ms>` drives the flow-controlled dispense loop and replies `DONE <ms>`.

- [ ] Add `unsigned long dispenseForDuration(unsigned long targetFlowMs)` — accrues
      *food-flow time*, not wall-clock time:
  - [ ] Motor on, `LED_BUILTIN` on; record wall-clock start.
  - [ ] Loop sampling `feedSensorPin`: accrue elapsed time only while food is detected
        flowing; pause accrual when no food is seen (empty plate / flow gaps).
  - [ ] Exit when accrued flow ≥ `targetFlowMs` **or** wall-clock ≥ `MAX_WALL_MS`
        (10000) — whichever first.
  - [ ] Motor off, LED off; return accrued food-flow time.
  - [ ] Respect the existing `#ifdef USING_SERVO` split.
- [ ] Repurpose the button (`buttonPin`, open/close switch) as the **manual dispense**
      trigger → `dispenseForDuration(DEFAULT_DURATION)`.
- [ ] **Remove** the payment-beam activation path; keep `feedSensorPin` for flow only.
- [ ] In `loop()`, add network handling alongside the serial/button logic:
  - [ ] `WiFiClient client = feederServer.available();`
  - [ ] If a client is present, read one line, parse `DISPENSE <ms>`.
  - [ ] Valid → `accrued = dispenseForDuration(ms)` then `client.print("DONE <accrued>\n")`.
  - [ ] Invalid → `client.print("ERR <reason>\n")`.
  - [ ] `client.stop();`
- [ ] Confirm the button and serial paths still function.

**Verify:** `printf 'DISPENSE 500\n' | nc <ip> 4242` → dispenser runs and replies
`DONE 500` (with food present). With an empty hopper, the loop ends at ~10 s and reports
a smaller accrued value. Send a garbage line → `ERR ...`.

## Phase 3 — Python `dispense` CLI

**Goal:** a shell-callable client that sends a command and reports the result.

- [ ] Create `dispense` (executable, `#!/usr/bin/env python3`).
- [ ] `argparse`: positional `ip`; `-p/--port` default 4242; `-d/--duration` default 800.
- [ ] Open `socket` to `ip:port` with a 10s timeout; send `DISPENSE <ms>\n`.
- [ ] Read reply:
  - [ ] `DONE` → print success to stdout, exit 0.
  - [ ] `ERR` → print error to stdout, exit non-zero.
  - [ ] timeout / connection error → print error to stdout, exit non-zero.
- [ ] `chmod +x dispense`.

**Verify:** `./dispense <ip> -d 800` logs success; `./dispense 10.255.255.1` times out
after ~10s with an error.

## Phase 4 — Integration test

**Goal:** end-to-end confirmation on real hardware.

- [ ] Flash firmware; note the IP from Serial.
- [ ] From the Linux shell, run `./dispense <ip> -d 800` and confirm the motor runs and
      the CLI logs success.
- [ ] Test the failure path against a bad IP (10s timeout).

## Phase 5 — Schedule with cron

**Goal:** automatic feeding.

- [ ] Add a crontab entry on the Linux machine (e.g. 7am & 6pm).
- [ ] Document the cron example in `README.md` (and confirm logging to a file works).

## Files touched

| File                       | Action  | Phase |
|----------------------------|---------|-------|
| `arduino_secrets.h.example`| create  | 1     |
| `.gitignore`               | edit    | 1     |
| `CatFeeder.ino`            | edit    | 1–2   |
| `dispense`                 | create  | 3     |
| `README.md`                | edit    | 5     |
