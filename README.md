# ghanode (Zephyr) — SiWx917 sensor MQTT client

Zephyr port of [ghanode_si917](https://github.com/HCDani/ghanode_si917), the
FreeRTOS/WiSeConnect sensor node for the **SiWx917** (BRD4338A on a BRD4002A
mainboard). Same payload and the same GHA ingest contract; the RTOS, drivers
and network stack are now Zephyr's. Recovery differs where the old NWP MQTT
client and the 30-reading backlog did.

The node joins a WPA2/WPA3 network as a Wi-Fi station over DHCP, samples the
on-board Si7021 and a GL5528 photoresistor every 20 s, and publishes JSON to
`ghanode/sensor` on an MQTT broker. The payload has no timestamp, so only the
latest unpublished reading is kept. Older ones are dropped, and an outage shows
up as a gap.

```
{"Sensor ID": "8C65A319AC98", "temperature":23.4, "humidity": 47, "light": 767}
```

`Sensor ID` is the STA MAC, which is also the MQTT client ID.

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
  PUBACK arrives. A reading that is never acknowledged is dropped, not replayed.
* **No reading backlog.** The FreeRTOS node buffered 30 readings (10 minutes)
  and flushed them after a reconnect. Those readings were stamped with the
  reconnect time, so this node keeps one and drops the rest.
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

* **One unpublished reading.** A new sample replaces the one still waiting to be
  sent. The log says `Dropped an unpublished reading`.
* **Events.** `net_mgmt` callbacks run on the net_mgmt thread and only post
  `k_event` bits (scan done, connected, DHCP bound, Wi-Fi down).
* **One publish in flight.** The next reading is formatted only after the
  previous one's PUBACK. If that acknowledgement never arrives, the reading is
  dropped.
* **Backoff.** Failed Wi-Fi joins and MQTT connects retry after 5 s, doubling
  up to 60 s. The MQTT wait ends early if Wi-Fi is lost.
* **Dead sessions.** Three publish errors in a row, no PUBACK within 10 s, or
  a PINGREQ unanswered for a whole keep-alive period (60 s) end the session
  even without a disconnect event. The unsent reading is dropped.
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
| `CONFIG_GHANODE_WIFI_COUNTRY` | `DK` |

## Build and flash

This repository is the application. Build it from a west workspace that has
Zephyr **4.4.1** and the Silicon Labs HAL, passing the path to the clone:

```
west build -p always -b siwx917_rb4338a <path-to-this-repo> -d build
west flash -d build
```

`west flash` uses Simplicity Commander and writes `build/zephyr/zephyr.rps`.

**NWP firmware must match that Zephyr release.** Zephyr 4.4.1 expects NWP
**2.15.5** and refuses to start Wi-Fi if the major or minor version differs. A
board flashed for the FreeRTOS project (2.16.x) needs the image from the HAL
blobs, which live in the west workspace:

```
west blobs fetch hal_silabs
commander rps load modules/hal/silabs/zephyr/blobs/wiseconnect/connectivity_firmware/standard/SiWG917-B.2.15.5.3.0.1.rps --device SiWG917M111GTBA
```

Logs are on the VCOM serial port (115200 8N1).

## Layout

| Path | Role |
| --- | --- |
| `src/main.c` | Reading queue, sensor and network threads, stack reports, fatal handler |
| `src/sensors.c` | Si7021 power gating and probe, temperature/humidity, light ADC and lux |
| `src/wifi.c` | Scan, AP selection, join, DHCP, link-loss detection, diagnostics |
| `src/mqtt_link.c` | MQTT connect, publishing, dead-session detection, recovery ladder |
| `boards/siwx917_rb4338a.overlay` | GPIO_25 ADC input, `SENSOR_ENABLE` pin |
| `Kconfig`, `prj.conf` | App knobs and Zephyr configuration |

## Run on the board

On a BRD4338A the node joined a WPA3 network, received a DHCP lease, and
published the JSON above. A beacon loss kept sampling and rejoined after the
access point changed channel. A broker that stopped answering produced a 10 s
publish timeout, a Wi-Fi restart, three connect timeouts, and one MCU reset,
after which publishing resumed. The firmware on the board keeps only the latest
unpublished reading.

## Known limitations

* **No timestamp in the payload.** A reading sent just after a reconnect is
  stamped then, so it can be up to one sample period late. Readings that could
  not be published are dropped instead of replayed.
* **Plain MQTT on 1883.** TLS isn't wired up.
* **Lux is uncalibrated**; adjust `LDR_R10_OHMS` in `src/sensors.c` from a lux
  meter reading near 2,000 lux.
* **Humidity is rounded** to whole %RH in the payload.
* **A failed Si7021 read skips the sample**, so a dead sensor shows up as a gap.
