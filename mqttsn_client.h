// mqttsn_client.h
#ifndef MQTTSN_CLIENT_H
#define MQTTSN_CLIENT_H

#define MQTTSN_OK     0 // Operation Successful
#define MQTTSN_ERROR   -1


#include <stdint.h>

int mqttsn_demo_init(uint16_t local_port, const char *client_id);
int mqttsn_demo_send_test(const char *payload);
int mqttsn_demo_receive(uint8_t *buffer, size_t max_len, uint32_t timeout_ms);
void mqttsn_demo_close(void);

// Paho-enabled helpers
int mqttsn_demo_subscribe(const char *topicname, unsigned short packetid, unsigned short *out_topicid);
int mqttsn_demo_publish_name(const char *topicname, const uint8_t *payload, int payloadlen);
int mqttsn_demo_process_once(uint32_t timeout_ms);

// QoS management
int mqttsn_get_qos(void);
void mqttsn_set_qos(int qos);

// Topic IDs (exported for checking registration status)
extern unsigned short mqttsn_chunks_topicid;

#endif // MQTTSN_CLIENT_H
