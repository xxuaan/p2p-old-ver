// udp_driver.h - Handles logic for UDP creation, removal, send and receive

#ifndef UDP_DRIVER_H
#define UDP_DRIVER_H
#include <stdbool.h>

// Create UDP socket and bind to local port
int wifi_udp_create(uint16_t local_port);
// Sned UDP packet
int wifi_udp_send(const char *dest_ip, uint16_t dest_port, const uint8_t *data, size_t len);
// Receive UDP packet with timeout (ms). 0 = non-blocking
int wifi_udp_receive(uint8_t *buffer, size_t max_len, uint32_t timeout_ms);
// Close UDP socket
void wifi_udp_close(void);
// Get UDP connection
bool is_udp_open(void);

#endif