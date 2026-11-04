# ghanode (Zephyr) — SiWx917 sensor MQTT client

Zephyr port of [ghanode_si917](https://github.com/HCDani/ghanode_si917), the
FreeRTOS/WiSeConnect sensor node for the **SiWx917** (BRD4338A on a BRD4002A
mainboard). Same payload keys as the GHA pipeline of the bachelor project used
(that pipeline is discontinued), plus a timestamp; the RTOS, drivers and
network stack are now Zephyr's. Recovery differs where the old NWP MQTT
client and the 30-reading backlog did.

The node joins a WPA2/WPA3 network as a Wi-Fi station over DHCP, samples the
on-board Si7021 and a GL5528 photoresistor every 20 s, and publishes JSON to
`ghanode/sensor` on an MQTT broker. Each reading carries the UTC time it was
sampled, from SNTP, so up to an hour of readings is queued through an outage
and published afterwards with their own times.

```
{"Sensor ID": "8C65A319AC98", "temperature":23.4, "humidity": 47, "light": 767, "timestamp": "2026-11-04T12:55:00Z"}
```

`Sensor ID` is the STA MAC, which is also the MQTT client ID. `timestamp` is
the sample time in UTC, whole seconds. Until the first SNTP answer it is left
out, and a subscriber has to time the reading on arrival.

## What changed from the FreeRTOS version

| FreeRTOS / WiSeConnect | Zephyr |
| --- | --- |
| CMSIS-RTOS2 threads, `osMessageQueue`, `osEventFlags` | `K_THREAD_DEFINE`, `k_msgq`, `k_event` |
| `osDelayUntil` | `k_sleep(K_TIMEOUT_ABS_MS())` |
| `configCHECK_FOR_STACK_OVERFLOW 2` + hook | High-water log. This SoC has no MPU stack guard |
| `sl_net_*` / `sl_wifi_*` | `net_mgmt` Wi-Fi requests and events |
| NWP embedded MQTT client | Zephyr MQTT library over sockets, native TCP/IP stack |
| `sl_si91x_si70xx` + local float fork | Zephyr `si7006` sensor driver (already fractional) |
| `sl_si91x_adc` + manual offset-binary fix | Zephyr ADC driver (applies eFuse calibration) |
| `sl_si91x_gpio_driver_set_uulp_pad_configuration` | UULP GPIO driver + one register write for pad ownership |
| `#define` knobs in `app.c` | Kconfig (`CONFIG_GHANODE_*`) |

Behavioural differences:

* **QoS 1 instead of QoS 0.** A reading leaves the node only once the broker's
  PUBACK arrives. One that is not acknowledged is sent again after the
  reconnect, so a subscriber can receive it twice with the same `Sensor ID`
  and `timestamp`.
* **Timestamped backlog.** The FreeRTOS node buffered 30 readings (10 minutes)
  and flushed them after a reconnect, where the GHA pipeline stamped them all
  with the reconnect time. This node queues 180 (one hour) and stamps each with its
  sample time from SNTP.
* **Sampling starts at boot**, not after the first Wi-Fi join. Zephyr boots the
  NWP at `POST_KERNEL`, before `main()` touches any ULP peripheral, so the
  bring-up order constraint is met without waiting for Wi-Fi.
* **Channel pinned, BSSID not.** The node still scans, picks the strongest
  matching AP and joins on its channel, but the Zephyr SiWx91x driver ignores
  the BSSID in a connect request.
* **No NWP MQTT workarounds.** The fresh-port, never-two-connects, and
  release-the-NWP-client fixes were specific to the NWP's embedded MQTT
  client. With sockets on the native stack every connect gets a new ephemeral
  port and `mqtt_connect()` is synchronous.

## RTOS design

| Thread | Priority | Stack | Role |
| --- | --- | --- | --- |
| `sensor` | `K_PRIO_PREEMPT(10)` | 2048 B | Producer: reads the Si7021 and the ADC, queues a reading every 20 s |
| `net` | `K_PRIO_PREEMPT(11)` | 4096 B | Wi-Fi bring-up, then consumer: owns MQTT, publishes the queue |
| Zephyr net / Wi-Fi driver threads | driver defaults | — | NWP communication, TCP/IP, `net_mgmt` callbacks |

The stack sizes are a first guess. Both threads log their high-water mark after
the first sample and then hourly (`Stack unused at high-water: …`). This SoC
does not support `CONFIG_HW_STACK_PROTECTION`, so that log is the overflow
check.

* **Reading queue.** `CONFIG_GHANODE_READING_QUEUE_LEN` readings (180, one
  hour) wait for the broker. When the queue is full the oldest is dropped and
  the log says `Reading queue full, dropped the oldest reading`. The queue is
  in RAM, so an MCU reset loses it.
* **Time.** Each reading stores the uptime it was sampled at. The network
  thread queries SNTP before the first MQTT connect, retries every 60 s until
  it gets an answer, and then resyncs hourly, logging how far each resync
  moved the clock. At publish time the latest sync turns the uptime into UTC,
  so readings taken before the first sync get the right time too.
* **No time, no backlog.** Before the first SNTP answer, a reading older than
  one sample period is dropped instead of sent (`No SNTP time, dropped a
  reading taken … s ago`), because a subscriber could only time it on arrival.
* **Events.** `net_mgmt` callbacks run on the net_mgmt thread and only post
  `k_event` bits (scan done, connected, DHCP bound, Wi-Fi down).
* **One publish in flight.** The next reading is formatted only after the
  previous one's PUBACK. If that acknowledgement never arrives, the reading is
  dropped.
* **Backoff.** Failed Wi-Fi joins and MQTT connects retry after 5 s, doubling
  up to 60 s. The MQTT wait ends early if Wi-Fi is lost.
* **Dead sessions.** Three publish errors in a row, no PUBACK within 10 s, or
  a PINGREQ unanswered for a whole keep-alive period (60 s) end the session
  even without a disconnect event. The unsent reading is kept for the next
  session.
* **Recovery ladder.** A dead session first restarts Wi-Fi. If no publish gets
  through before the next failure, or three connects in a row fail after the
  restart, the MCU resets.
* **Fatal errors.** `k_sys_fatal_error_handler` stores the thread name in
  `fatal_thread_name` for a debugger and halts. It does not catch a stack
  overflow, because this SoC provides no stack guard.

## Hardware notes carried over

* **Si7021 power gating.** `SENSOR_ENABLE` (UULP_VBAT_GPIO_1) must be an
  M4-owned output driven high. The Zephyr UULP GPIO driver never clears the
  pad's NWP-select bit, so `sensors.c` clears it, drives the pin, reads the
  output latch back and waits the datasheet's 80 ms.
* **Light.** GL5528 between GPIO_25 and GND with a 4.7 kΩ pull-up;
  `lux = 10 · (R10 / R)^(1/γ)`, R10 = 15 kΩ, γ = 0.7. Full scale reports 0.
  `boards/siwx917_rb4338a.overlay` moves the board's ADC channel from GPIO_28
  to GPIO_25.
* **EU channels and PMF.** `CONFIG_GHANODE_WIFI_COUNTRY` (default `DK`)
  selects the EU region so channels 12–13 are scanned. WPA3 / transition APs
  are joined with SAE and PMF required; WPA2 APs with PMF optional.
* **Diagnostics.** After each join the node logs the AP scan, its IP
  configuration, the NWP firmware version, and pings the broker.

## Secrets

```
copy secret_example.h secret.h
```

`secret.h` is gitignored. Without it the build uses the placeholders in
`secret_example.h` and prints a warning.

| Macro | Purpose |
| --- | --- |
| `SECRET_WIFI_SSID` / `SECRET_WIFI_PASSWORD` | STA credentials |
| `SECRET_WIFI_DHCP_HOSTNAME` | DHCP option 12 hostname, 30 characters or fewer |
| `SECRET_MQTT_BROKER_ADDR` | Broker IPv4 address |
| `SECRET_MQTT_USERNAME` / `SECRET_MQTT_PASSWORD` | MQTT login |

## Configuration (`prj.conf` or `menuconfig`)

| Kconfig | Default |
| --- | --- |
| `CONFIG_GHANODE_MQTT_BROKER_PORT` | `1883` |
| `CONFIG_GHANODE_MQTT_TOPIC` | `ghanode/sensor` |
| `CONFIG_GHANODE_SAMPLE_INTERVAL_MS` | `20000` |
| `CONFIG_GHANODE_READING_QUEUE_LEN` | `180` |
| `CONFIG_GHANODE_SNTP_SERVER` | `pool.ntp.org` (host name via the DHCP DNS server, or an IPv4 address) |
| `CONFIG_GHANODE_SNTP_RESYNC_S` | `3600` |
| `CONFIG_GHANODE_SNTP_RETRY_S` | `60` |
| `CONFIG_GHANODE_WIFI_COUNTRY` | `DK` |

## Build and flash

This repository is the application. Build it from a west workspace that has
Zephyr **4.4.1** and the Silicon Labs HAL, passing the path to the clone:

```
west build -p always -b siwx917_rb4338a <path-to-this-repo> -d build
west flash -d build
```

`west flash` uses Simplicity Commander and writes `build/zephyr/zephyr.rps`.

Without a workspace, `west.yml` creates one with Simplicity SDK for Zephyr
v2026.6.0, which is based on Zephyr 4.4.1:

```
mkdir ghanode-ws && cd ghanode-ws
git clone https://github.com/HCDani/ghanode_ze_si917
west init -l ghanode_ze_si917
west update
west packages pip --install
west blobs fetch hal_silabs
```

**NWP firmware must match that Zephyr release.** Zephyr 4.4.1 expects NWP
**2.15.5** and refuses to start Wi-Fi if the major or minor version differs. A
board flashed for the FreeRTOS project (2.16.x) needs the image from the HAL
blobs, which live in the west workspace:

```
west blobs fetch hal_silabs
commander rps load modules/hal/silabs/zephyr/blobs/wiseconnect/connectivity_firmware/standard/SiWG917-B.2.15.5.3.0.1.rps --device SiWG917M111GTBA
```

Logs are on the VCOM serial port (115200 8N1).

## Tests

The logic that does not touch hardware is in its own files and has ztest
suites that run on `native_sim` (Linux or WSL):

| Suite | Covers |
| --- | --- |
| `payload` | The exact JSON, with and without `timestamp`, UTC calendar edges, humidity rounding, buffer overflow |
| `recovery` | The recovery ladder: when a failure restarts Wi-Fi and when it resets the MCU |
| `reading_queue` | Oldest-first order; a full queue drops the oldest reading |
| `timesync` | SNTP answer to UTC: fractions, readings before and after the sync, resync step, implausible answers |
| `lux` | LDR voltage to lux: R10 point, full scale, clamping |
| `backoff` | 5 s doubling to the 60 s cap |

```
west twister -T tests -p native_sim/native/64
```

GitHub Actions runs these and builds the firmware for `siwx917_rb4338a` on
every push and pull request (`.github/workflows/ci.yml`). The CI build uses
`secret_example.h`.

## Layout

| Path | Role |
| --- | --- |
| `src/main.c` | Reading queue, sensor and network threads, stack reports, fatal handler |
| `src/sensors.c` | Si7021 power gating and probe, temperature/humidity, light ADC |
| `src/wifi.c` | Scan, AP selection, join, DHCP, link-loss detection, diagnostics |
| `src/mqtt_link.c` | MQTT connect, publishing, dead-session detection, acting on the recovery ladder |
| `src/sntp_sync.c` | SNTP queries and their schedule |
| `src/timesync.c` | Uptime to UTC from the latest SNTP answer (no hardware) |
| `src/recovery.c` | Recovery ladder decisions (no hardware) |
| `src/payload.c` | JSON payload formatting, UTC timestamp (no hardware) |
| `src/reading.c` | Drop-oldest reading queue (no hardware) |
| `src/lux.c` | LDR voltage to lux (no hardware) |
| `src/backoff.h` | Retry delay doubling |
| `tests/unit/` | ztest suites for the files above, on `native_sim` |
| `boards/siwx917_rb4338a.overlay` | GPIO_25 ADC input, `SENSOR_ENABLE` pin |
| `Kconfig`, `prj.conf` | App knobs and Zephyr configuration |
| `west.yml` | Workspace manifest for CI or a fresh checkout |

## Run on the board

On a BRD4338A the node joined a WPA3 network, received a DHCP lease, and
published the JSON above. A beacon loss kept sampling and rejoined after the
access point changed channel. A broker that stopped answering produced a 10 s
publish timeout, a Wi-Fi restart, three connect timeouts, and one MCU reset,
after which publishing resumed. Those runs predate SNTP, when the node kept only
the latest unpublished reading; the timestamped queue has not yet been run on
the board.

## Known limitations

* **Duplicates are possible.** A reading re-sent after a lost PUBACK arrives
  twice; `Sensor ID` plus `timestamp` identifies it.
* **No SNTP, no backlog.** If the SNTP server never answers, the node behaves
  like before: only fresh readings are published, the rest are dropped.
* **An SNTP query blocks the network thread** for up to 3 s (plus the DNS
  lookup), hourly. Publishing and keep-alives wait meanwhile.
* **Plain MQTT on 1883.** TLS isn't wired up.
* **Lux is uncalibrated**; adjust `LDR_R10_OHMS` in `src/lux.c` from a lux
  meter reading near 2,000 lux.
* **Humidity is rounded** to whole %RH in the payload.
* **A failed Si7021 read skips the sample**, so a dead sensor shows up as a gap.
