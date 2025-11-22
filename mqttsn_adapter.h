// mqttsn_adapter.h
// Simple transport adapter that wraps existing UDP driver for MQTT-SN usage

#ifndef MQTTSN_ADAPTER_H
#define MQTTSN_ADAPTER_H

#include <stdint.h>
#include <stddef.h>

// Open transport (bind a local UDP port)
int mqttsn_transport_open(uint16_t local_port);

// Send a datagram to destination IP:port. dest_ip is dotted decimal string.
int mqttsn_transport_send(const char *dest_ip, uint16_t dest_port, const uint8_t *data, size_t len);

// Receive into buffer up to max_len bytes with timeout in ms (0 = non-blocking).
// Returns number of bytes received (>0), 0 for no data (non-blocking), or negative on error.
int mqttsn_transport_receive(uint8_t *buffer, size_t max_len, uint32_t timeout_ms);

// Close transport
void mqttsn_transport_close(void);

#endif // MQTTSN_ADAPTER_H
