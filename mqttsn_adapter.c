// mqttsn_adapter.c - SIMPLE VERSION
#include "mqttsn_adapter.h"
#include "udp_driver.h"
#include <stdio.h>
#include <stdbool.h>

int mqttsn_transport_open(uint16_t local_port){
    return wifi_udp_create(local_port);
}

int mqttsn_transport_send(const char *dest_ip, uint16_t dest_port, const uint8_t *data, size_t len){
    return wifi_udp_send(dest_ip, dest_port, data, len);
}

int mqttsn_transport_receive(uint8_t *buffer, size_t max_len, uint32_t timeout_ms){
    return wifi_udp_receive(buffer, max_len, timeout_ms);
}

void mqttsn_transport_close(void){
    wifi_udp_close();
}