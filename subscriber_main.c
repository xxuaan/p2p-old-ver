#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/gpio.h"

#include "network_config.h"
#include "wifi_driver.h"
#include "mqttsn_client.h"
#include "block_transfer.h"
#include "sd_card.h"

#ifdef HAVE_PAHO
#include "MQTTSNPacket.h"
#include "MQTTSNConnect.h"
#include "MQTTSNPublish.h"
#include "MQTTSNSubscribe.h"
#endif

// LED pin for visual feedback
#define LED_PIN 25

static bool mqtt_subscriber_ready = false;
static unsigned short subscribed_topicid = 0;
static unsigned short chunks_topicid = 0;  // Topic ID for pico/chunks (block transfer)

// Process received PUBLISH messages
static void process_publish_message(unsigned char *buf, int len) {
    #ifdef HAVE_PAHO
    unsigned char dup, retained;
    unsigned short msgid;
    int qos;
    MQTTSN_topicid topic;
    unsigned char *payload;
    int payloadlen;
    
    int rc = MQTTSNDeserialize_publish(&dup, &qos, &retained, &msgid, 
                                       &topic, &payload, &payloadlen, 
                                       buf, len);
    
    if (rc == 1) {
        // Debug: Print received topic ID to verify matching
        if (topic.type == MQTTSN_TOPIC_TYPE_NORMAL) {
            printf("[DEBUG] Received PUBLISH: TopicID=%u, Expected chunks_topicid=%u\n", 
                   topic.data.id, chunks_topicid);
        }
        
        // Check if this is a block transfer chunk (from pico/chunks)
        if (topic.data.id == chunks_topicid && chunks_topicid != 0) {
            // Enhanced debug output for block transfers (commented out to improve performance)
            // printf("[SUBSCRIBER] Chunk received (%d bytes, QoS %d)\n", payloadlen, qos);
            
            // Poll WiFi stack to prevent buffer overflow during heavy traffic
            cyw43_arch_poll();
            
            // QoS 1: Process chunk FIRST, then send PUBACK (proper QoS 1 semantics)
            // This ensures PUBACK is only sent after successful processing
            process_block_chunk(payload, payloadlen);
            
            // Poll WiFi again after processing
            cyw43_arch_poll();
            
            // Send PUBACK for QoS 1 AFTER successful processing
            if (qos == 1) {
                unsigned char puback_buf[7];
                puback_buf[0] = 7;
                puback_buf[1] = 0x0D;
                puback_buf[2] = (topic.data.id >> 8);
                puback_buf[3] = (topic.data.id & 0xFF);
                puback_buf[4] = (msgid >> 8);
                puback_buf[5] = (msgid & 0xFF);
                puback_buf[6] = 0x00;
                
                int send_rc = mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                                    puback_buf, sizeof(puback_buf));
                if (send_rc != 0) {
                    printf("[ERROR] Failed to send PUBACK (rc=%d)\n", send_rc);
                }
            }
            
            return;  // Skip normal message processing for block chunks
        }
        
        printf("\n[SUBSCRIBER] ✓ Message received:\n");
        printf("  TopicID: %u\n", topic.data.id);
        printf("  QoS: %d\n", qos);
        printf("  MsgID: %u\n", msgid);
        printf("  Payload (%d bytes): ", payloadlen);
        
        // Print payload (assume text)
        for (int i = 0; i < payloadlen; i++) {
            printf("%c", payload[i]);
        }
        printf("\n");
        
        // Send PUBACK for QoS 1
        if (qos == 1) {
            unsigned char puback_buf[7];
            puback_buf[0] = 7;              // Length
            puback_buf[1] = 0x0D;           // PUBACK
            puback_buf[2] = (topic.data.id >> 8);
            puback_buf[3] = (topic.data.id & 0xFF);
            puback_buf[4] = (msgid >> 8);
            puback_buf[5] = (msgid & 0xFF);
            puback_buf[6] = 0x00;           // Return code (accepted)
            
            mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                 puback_buf, sizeof(puback_buf));
            printf("[SUBSCRIBER] → PUBACK sent (MsgID=%u)\n", msgid);
        }
        
        // Send PUBREC for QoS 2
        if (qos == 2) {
            unsigned char pubrec_buf[4];
            pubrec_buf[0] = 4;              // Length
            pubrec_buf[1] = 0x0F;           // PUBREC
            pubrec_buf[2] = (msgid >> 8);
            pubrec_buf[3] = (msgid & 0xFF);
            
            mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                 pubrec_buf, sizeof(pubrec_buf));
            printf("[SUBSCRIBER] → PUBREC sent (MsgID=%u)\n", msgid);
            
            // Wait for PUBREL
            printf("[SUBSCRIBER] Waiting for PUBREL...\n");
            unsigned char pubrel_buf[256];
            int pubrel_rc = mqttsn_transport_receive(pubrel_buf, sizeof(pubrel_buf), 5000);
            
            if (pubrel_rc > 0 && pubrel_buf[1] == 0x10) {  // 0x10 = PUBREL
                printf("[SUBSCRIBER] ✓ PUBREL received\n");
                
                // Send PUBCOMP
                unsigned char pubcomp_buf[4];
                pubcomp_buf[0] = 4;              // Length
                pubcomp_buf[1] = 0x0E;           // PUBCOMP
                pubcomp_buf[2] = (msgid >> 8);
                pubcomp_buf[3] = (msgid & 0xFF);
                
                mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                     pubcomp_buf, sizeof(pubcomp_buf));
                printf("[SUBSCRIBER] → PUBCOMP sent (MsgID=%u) - QoS 2 complete\n", msgid);
            } else {
                printf("[SUBSCRIBER] ✗ PUBREL not received\n");
            }
        }
        
        // Blink LED to indicate message received
        gpio_put(LED_PIN, 1);
        sleep_ms(100);
        gpio_put(LED_PIN, 0);
        
    } else {
        printf("[SUBSCRIBER] Failed to deserialize PUBLISH\n");
    }
    #endif
}

// Subscribe to a topic
static int subscribe_to_topic(const char *topic_name) {
    #ifdef HAVE_PAHO
    unsigned char buf[256];
    MQTTSN_topicid topic;
    topic.type = MQTTSN_TOPIC_TYPE_NORMAL;
    topic.data.long_.name = (char*)topic_name;
    topic.data.long_.len = (int)strlen(topic_name);
    
    unsigned short msgid = 100;  // Arbitrary message ID
    
    // Change QoS from 0 to 2 (maximum QoS level)
    // Parameters: (buf, buflen, dup, qos, msgid, topic)
    int len = MQTTSNSerialize_subscribe(buf, sizeof(buf), 0, 2, msgid, &topic);
    //                                                        ↑
    //                                                      QoS 2 (was 0)
    
    if (len <= 0) {
        printf("[SUBSCRIBER] Failed to serialize SUBSCRIBE\n");
        return -1;
    }
    
    printf("[SUBSCRIBER] Sending SUBSCRIBE to '%s' with QoS 2...\n", topic_name);
    int s = mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, buf, len);
    if (s != 0) {
        printf("[SUBSCRIBER] SUBSCRIBE send failed\n");
        return -2;
    }
    
    // Wait for SUBACK
    printf("[SUBSCRIBER] Waiting for SUBACK...\n");
    int r = mqttsn_transport_receive(buf, sizeof(buf), 5000);
    if (r > 0) {
        int granted_qos;
        unsigned short topicid;
        unsigned short ret_msgid;
        unsigned char returncode;
        
        int d = MQTTSNDeserialize_suback(&granted_qos, &topicid, &ret_msgid, 
                                         &returncode, buf, r);
        if (d == 1 && returncode == 0) {
            subscribed_topicid = topicid;
            printf("[SUBSCRIBER] ✓ Subscribed to '%s' (TopicID=%u, QoS=%d)\n", 
                   topic_name, topicid, granted_qos);
            return 0;
        } else {
            printf("[SUBSCRIBER] ✗ Subscription rejected (code=%d)\n", returncode);
            return -3;
        }
    } else {
        printf("[SUBSCRIBER] ✗ SUBACK timeout\n");
        return -4;
    }
    #else
    printf("[SUBSCRIBER] Paho library not available\n");
    return -1;
    #endif
}

int main() {
    stdio_init_all();
    sleep_ms(3000);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("   MQTT-SN Pico W Subscriber - Block Transfer Receiver\n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("  Function: Receives image blocks from publisher via MQTT-SN\n");
    printf("  Hardware: Maker Pi Pico W + SD card (built-in slot)\n");
    printf("  Protocol: MQTT-SN over UDP (QoS 2 supported)\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    // Setup LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
    printf("[INIT] LED initialized on GPIO %d\n", LED_PIN);
    
    // Initialize SD card
    printf("[INIT] Initializing SD card...\n");
    if (sd_card_init() == 0) {
        printf("[INIT] ✓ SD card hardware initialized\n");
        
        // Mount FAT32 filesystem
        if (sd_card_mount_fat32() == 0) {
            printf("[INIT] ✓ FAT32 filesystem mounted - ready to save blocks\n");
        } else {
            printf("[WARNING] FAT32 mount failed - blocks will not be saved\n");
            printf("[WARNING] Ensure SD card is formatted as FAT32\n");
        }
    } else {
        printf("[WARNING] SD card initialization failed - blocks will not be saved\n");
        printf("[WARNING] Insert SD card and reset Pico to enable saving\n");
    }
    
    // WiFi Init
    printf("[INIT] Connecting to WiFi SSID: %s\n", WIFI_SSID);
    if (wifi_init(WIFI_SSID, WIFI_PASSWORD) != 0) {
        printf("[ERROR] WiFi initialization failed\n");
        return 1;
    }
    
    if (wifi_connect() != 0) {
        printf("[WARNING] Initial connection failed - will retry\n");
    }
    
    sleep_ms(2000);
    
    // Main loop
    bool was_connected = false;
    
    while (true) {
        wifi_auto_reconnect();
        bool is_connected = wifi_is_connected();
        
        // Reconnection handling
        if (is_connected && !was_connected) {
            printf("[INFO] WiFi connected! Initializing MQTT-SN subscriber...\n");
            mqtt_subscriber_ready = false;
        }
        
        if (!is_connected && was_connected) {
            printf("[WARNING] WiFi disconnected!\n");
            mqtt_subscriber_ready = false;
        }
        
        was_connected = is_connected;
        
        if (is_connected) {
            cyw43_arch_poll();
            
            // Initialize MQTT-SN connection
            if (!mqtt_subscriber_ready) {
                printf("\n[SUBSCRIBER] Connecting to MQTT-SN gateway...\n");
                
                if (mqttsn_demo_init(0, "pico_w_subscriber") == 0) {
                    printf("[SUBSCRIBER] ✓ Connected to gateway\n");
                    
                    // Initialize block transfer system
                    block_transfer_init();
                    printf("[SUBSCRIBER] ✓ Block transfer initialized\n");
                    
                    // Subscribe to pico/test
                    if (subscribe_to_topic("pico/test") == 0) {
                        // Subscribe to pico/chunks for block transfer
                        printf("[SUBSCRIBER] Subscribing to pico/chunks for block transfer...\n");
                        
                        unsigned short chunks_topic_id_temp = 0;
                        int chunks_sub_rc = mqttsn_demo_subscribe("pico/chunks", 102, &chunks_topic_id_temp);
                        
                        if (chunks_sub_rc > 0) {
                            chunks_topicid = chunks_topic_id_temp;
                            printf("[SUBSCRIBER] ✓ Subscribed to pico/chunks (TopicID=%u)\n", chunks_topicid);
                            
                            mqtt_subscriber_ready = true;
                            printf("[SUBSCRIBER] ✓✓✓ Ready to receive messages and blocks ✓✓✓\n");
                        } else {
                            printf("[SUBSCRIBER] ✗ Failed to subscribe to pico/chunks (rc=%d)\n", chunks_sub_rc);
                            printf("[SUBSCRIBER] Will retry on next connection...\n");
                            mqttsn_demo_close();
                            sleep_ms(5000);
                        }
                    } else {
                        printf("[SUBSCRIBER] Subscription to pico/test failed, retrying...\n");
                        mqttsn_demo_close();
                        sleep_ms(5000);
                    }
                } else {
                    printf("[SUBSCRIBER] Gateway connection failed, retrying...\n");
                    sleep_ms(10000);
                }
            } else {
                // Listen for incoming messages
                unsigned char buf[512];
                int rc = mqttsn_transport_receive(buf, sizeof(buf), 100);
                
                if (rc > 0) {
                    uint8_t msg_type = buf[1];
                    
                    if (msg_type == 0x0C) {  // PUBLISH
                        process_publish_message(buf, rc);
                    } else if (msg_type == 0x16) {  // PINGREQ
                        printf("[SUBSCRIBER] Received PINGREQ - sending PINGRESP\n");
                        unsigned char pingresp[] = {0x02, 0x17};
                        mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                             pingresp, sizeof(pingresp));
                    } else if (msg_type == 0x18) {  // DISCONNECT
                        printf("[SUBSCRIBER] ✗ Received DISCONNECT\n");
                        mqtt_subscriber_ready = false;
                        mqttsn_demo_close();
                    }
                }
                
                // Check for block transfer timeouts
                block_transfer_check_timeout();
            }
        }
        
        sleep_ms(10);
    }
    
    mqttsn_demo_close();
    return 0;
}