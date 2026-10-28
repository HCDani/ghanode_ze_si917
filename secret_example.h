/*
 * Template for project secrets. Copy to secret.h and fill in real values:
 *
 *   copy secret_example.h secret.h
 *
 * secret.h is gitignored; never commit real credentials.
 */
#pragma once

/* Wi-Fi STA SSID */
#ifndef SECRET_WIFI_SSID
#define SECRET_WIFI_SSID "your-wifi-ssid"
#endif

/* Wi-Fi STA passphrase */
#ifndef SECRET_WIFI_PASSWORD
#define SECRET_WIFI_PASSWORD "your-wifi-password"
#endif

/* DHCP client hostname (option 12), max 30 chars */
#ifndef SECRET_WIFI_DHCP_HOSTNAME
#define SECRET_WIFI_DHCP_HOSTNAME "your-hostname"
#endif

/* MQTT broker IPv4 address */
#ifndef SECRET_MQTT_BROKER_ADDR
#define SECRET_MQTT_BROKER_ADDR "192.168.6.1"
#endif

/* MQTT username */
#ifndef SECRET_MQTT_USERNAME
#define SECRET_MQTT_USERNAME "your-mqtt-username"
#endif

/* MQTT password */
#ifndef SECRET_MQTT_PASSWORD
#define SECRET_MQTT_PASSWORD "your-mqtt-password"
#endif
