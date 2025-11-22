// wifi_driver.h - Simple WiFi reconnection without scanning

#ifndef WIFI_DRIVER_H
#define WIFI_DRIVER_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"


// Configuration
#define RECONNECT_CHECK_INTERVAL_MS 5000   // Check connection every 5 seconds
#define RECONNECT_ATTEMPT_INTERVAL_MS 10000 // Try reconnecting every 10 seconds
#define CONNECTION_TIMEOUT_MS 7000         // Wait 7s for connection attempt

// Simple WiFi state
typedef struct {
    char ssid[33];
    char password[64];
    bool initialized;
    bool connected;
    absolute_time_t last_check_time;
    absolute_time_t last_reconnect_time;
    uint32_t reconnect_count;
    uint32_t disconnect_count;
} simple_wifi_t;

// WiFi IP Binary format - for network operations
typedef struct {
    ip4_addr_t ip;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
} wifi_network_info_t;


// ========== Function Declaration ========== 
int wifi_init(const char *ssid, const char *password);
bool wifi_is_connected(void);
const char* wifi_get_status(void);
// Primary function - returns binary format
int wifi_get_network_info(wifi_network_info_t *info);
int wifi_connect(void);
void wifi_auto_reconnect(void);
void wifi_print_stats(void);
// Or even simpler - just print it
int wifi_print_network_info(void);
// Reset WiFi hardware
int wifi_reset(void);

// Get signal strength in dBm
int wifi_get_rssi(void);

#endif // WIFI_DRIVER_H