// network_errors.h - Unified error codes for WiFi and UDP operations
// used across wifi.c, udp_driver.c

#ifndef NETWORK_ERRORS_H
#define NETWORK_ERRORS_H

#define WIFI_OK     0 // Operation Successful
#define WIFI_ENONETIF   -10 // Network Interface not available
#define WIFI_ENOIP      -11 // No IP Address assigned (DHCP failed)

#define WIFI_ETIMEDOUT  -12 // Operation timeout
#define WIFI_ESOCKET    -13 // Socket creation/bind failed
#define WIFI_ENOMEM     -14 // Out of Memory (pbuf allocation)
#define WIFI_ENOROUTE   -15 // No Route to host
#define WIFI_EINVAL     -16 // Invalid parameters
#define WIFI_ENOTCONN   -17 // Not connected (when operations requires connection)

#endif