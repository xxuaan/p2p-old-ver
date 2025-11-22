#include <stdio.h>
#include <string.h>

// Pico SDK header files
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "hardware/gpio.h"
#include "ff.h"

// Custom header files
#include "network_config.h"
#include "wifi_driver.h"
#include "udp_driver.h"
#include "network_errors.h"
#include "mqttsn_client.h"
#include "block_transfer.h"
#include "sd_card.h"

#define QOS_TOGGLE 22  // GP22
#define BLOCK_TRANSFER 21  // GP22

// Button debouncing
static volatile uint32_t last_button_press = 0;
static uint32_t last_block_transfer_button_press = 0;
static const uint32_t DEBOUNCE_MS = 300;

// GPIO interrupt handler for button
void gpio_callback(uint gpio, uint32_t events) {
    if (gpio == QOS_TOGGLE && (events & GPIO_IRQ_EDGE_FALL)) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        
        // Debounce check
        if (now - last_button_press > DEBOUNCE_MS) {
            last_button_press = now;
            
            // Toggle QoS level: 0 -> 1 -> 2 -> 0
            int current_qos = mqttsn_get_qos();
            int next_qos = (current_qos + 1) % 3;
            mqttsn_set_qos(next_qos);
            
            printf("\n[BUTTON] QoS level changed: %d -> %d\n", current_qos, next_qos);
            printf("[INFO] Next publish will use QoS %d\n", next_qos);
        }
    }
}

// init SD + block transfer
static bool app_init_sd_card_once(void){
    static bool initialised = false;

    if (initialised) {
        return true;
    }

    printf("[SD] Initialising SD card...\n");
    if(sd_card_init_with_detection() != 0){
        printf("[SD] SD card hardware initialisation failed.\n");
        return false;
    }

    if (sd_card_mount_fat32() != 0){
        printf("[SD] FAT32 mount failed.\n");
        return false;
    }
    
    printf("[SD] SD card initalised and FAT32 mounted!\n");
    initialised = true;
    return true;
}

// Simple poll-based check for GP21
static bool block_transfer_button_pressed(void){
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (gpio_get(BLOCK_TRANSFER) == 0){
        if (now - last_block_transfer_button_press > DEBOUNCE_MS) {
            last_block_transfer_button_press = now;
            return true;
        }
    }
    return false;
}

static void app_start_block_transfer(void){
    if (!app_init_sd_card_once()) {
        printf("[APP] Cannot start image transfer: SD initialisation failed\n");
        return;
    }

    // Scan SD card for image files and get the first one
    printf("\n[APP] Scanning SD card for images...\n");
    const char *filename = sd_card_get_first_image();

    if (filename == NULL) {
        printf("[APP] ✗ No image files found on SD card\n");
        printf("[APP] Please add a .jpg or .jpeg file to the SD card\n");
        return;
    }

    const char *topic = "pico/chunks";  // Send to pico/chunks for receiver to save to repo
    int qos = mqttsn_get_qos();

    // Check if topic is registered before starting transfer
    if (mqttsn_chunks_topicid == 0) {
        printf("[APP] ✗ Cannot start block transfer: topic 'pico/chunks' is not registered.\n");
        printf("[APP] Please ensure MQTT-SN connection and topic registration succeeded.\n");
        return;
    }

    printf("\n[APP] Block transfer requested (file='%s', topic='%s', QoS='%d')\n", filename, topic, qos);
    printf("[APP] Sending image from SD card via MQTT-SN...\n");

    int rc = send_image_file_qos(topic, filename, (uint8_t)qos);
    if (rc == 0){
        printf("[APP] ✓ Block Transfer completed successfully\n");
        printf("[APP] Image '%s' sent via MQTT-SN\n", filename);
    } else {
        printf("[APP] ✗ Block Transfer failed (rc=%d)\n", rc);
    }
}

void buttons_init() {

    gpio_init(BLOCK_TRANSFER);
    gpio_set_dir(BLOCK_TRANSFER, GPIO_IN);
    gpio_pull_up(BLOCK_TRANSFER);  // Enable pull-up (button connects to GND)

    gpio_init(QOS_TOGGLE);
    gpio_set_dir(QOS_TOGGLE, GPIO_IN);
    gpio_pull_up(QOS_TOGGLE);  // Enable pull-up (button connects to GND)

    // Enable interrupt on falling edge (button press)
    gpio_set_irq_enabled_with_callback(QOS_TOGGLE, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
}

// Process incoming PUBLISH messages (for block status)
static unsigned short status_topicid = 0;  // Store subscribed topic ID
static void process_publish_message(unsigned char *buf, int len) {
    if (len < 7) return;
    
    // Parse PUBLISH packet to get topic ID and payload
    unsigned char msg_type = buf[1];
    if (msg_type != 0x0C) return; // Not a PUBLISH
    
    // Extract topic ID (bytes 2-3 in little endian for short topic ID)
    unsigned short topic_id = buf[2] | (buf[3] << 8);
    
    // Payload starts after header (typically byte 7 for short topic ID)
    int header_len = 7;
    int payload_len = len - header_len;
    unsigned char *payload = buf + header_len;
    
    printf("[PUBLISHER] Received message on TopicID=%u, len=%d\n", topic_id, payload_len);
    
    // Route to block status handler if it matches our subscribed topic
    if (topic_id == status_topicid) {
        process_block_status(payload, payload_len);
    }
}

int main(){
    stdio_init_all();
    sleep_ms(3000); // Provide time for serial monitor to connect

    printf("\n=== MQTT-SN Pico W Client Starting ===\n");

    // ========================= Button Setup =========================
    buttons_init();
    
    printf("[BUTTON] GP22 configured for QoS toggle (pull-up enabled), GP21: Block transfer\n");
    printf("[INFO] Press button to cycle: QoS 0 -> QoS 1 -> QoS 2 -> QoS 0\n");

    // ========================= WiFi Init =========================
    if (wifi_init(WIFI_SSID, WIFI_PASSWORD) != 0){
        printf("[WARNING] WiFi Initialisation Failed...\n");
        return 1;
    }

    if (wifi_connect() != 0) {
        printf("[WARNING] Initial connection failed - will retry automatically\n");
    }

    sleep_ms(2000);

    block_transfer_init();

    // Main Loop
    bool was_connected = wifi_is_connected();
    absolute_time_t last_status_print = get_absolute_time();
    bool mqtt_demo_started = false;
    uint32_t last_publish = 0;
    uint32_t connection_start_time = 0;

    while (true){
        uint32_t now = to_ms_since_boot(get_absolute_time());

        // ========================= WiFi Reconnection Handling =========================
        wifi_auto_reconnect();  
        bool is_connected = wifi_is_connected();

        // ========================= WiFi Events =========================
        // 1. WiFi Reconnected
        if (is_connected && !was_connected){
            printf("[INFO] WiFi Reconnected! Reinitializing Network Services...\n");
            connection_start_time = now;
            
            // Reset MQTT demo state on reconnection
            mqtt_demo_started = false;
            mqttsn_demo_close();
        }

        // 2. WiFi Disconnected
        if (!is_connected && was_connected){
            printf("[WARNING] WiFi Connection Lost!\n");
            mqtt_demo_started = false;
        }
        
        was_connected = is_connected;  

        // 3. WiFi Connected
        if (is_connected){
            cyw43_arch_poll();

            if (!mqtt_demo_started){
                printf("\n[MQTT-SN] Initializing MQTT-SN Demo...\n");
                
                // Give publisher a unique client ID
                // Modify mqttsn_client.c to accept client ID parameter
                if (mqttsn_demo_init(0, "pico_w_publisher") == 0) {
                    printf("[MQTT-SN] ✓ MQTT-SN Demo initialized successfully\n");
                    
                    // Subscribe to block_status topic to receive retransmission requests
                    printf("[PUBLISHER] Subscribing to pico/block_status for retransmission...\n");
                    int status_sub_rc = mqttsn_demo_subscribe("pico/block_status", 103, &status_topicid);
                    if (status_sub_rc > 0) {  // Positive return = TopicID on success
                        printf("[PUBLISHER] ✓ Subscribed to pico/block_status (TopicID=%u)\n", status_topicid);
                    } else {
                        printf("[PUBLISHER] ✗ Failed to subscribe to pico/block_status (rc=%d)\n", status_sub_rc);
                    }
                    
                    mqtt_demo_started = true;
                } else {
                    printf("[MQTT-SN] ✗ MQTT-SN Demo initialization failed, retrying...\n");
                    sleep_ms(10000);
                }
            } else {
                // Process incoming MQTT-SN messages
                unsigned char buf[512];
                int rc = mqttsn_transport_receive(buf, sizeof(buf), 100);
                
                if (rc > 0) {
                    uint8_t msg_type = buf[1];
                    
                    if (msg_type == 0x0C) {  // PUBLISH
                        process_publish_message(buf, rc);
                    } else if (msg_type == 0x16) {  // PINGREQ
                        printf("[PUBLISHER] Received PINGREQ - sending PINGRESP\n");
                        unsigned char pingresp[] = {0x02, 0x17};
                        mqttsn_transport_send(MQTTSN_GATEWAY_IP, MQTTSN_GATEWAY_PORT, 
                                             pingresp, sizeof(pingresp));
                    } else if (msg_type == 0x18) {  // DISCONNECT
                        printf("[PUBLISHER] ✗ Received DISCONNECT\n");
                        mqtt_demo_started = false;
                        mqttsn_demo_close();
                    }
                } else if (rc == -1) {
                    printf("[MQTTSN] Connection lost - will reconnect...\n");
                    mqtt_demo_started = false;
                    mqttsn_demo_close();
                    sleep_ms(5000);
                    continue;
                }

                // Periodically publish every 5 seconds
                uint32_t now_ms = to_ms_since_boot(get_absolute_time());
                if (now_ms - last_publish > 5000){
                    static uint32_t message_count = 0;
                    char msg[64];
                    int qos = mqttsn_get_qos();
                    snprintf(msg, sizeof(msg), "Hello from Pico W #%lu (QoS%d)", message_count++, qos);
                    
                    printf("\n[MQTTSN] >>> Publishing message #%lu with QoS %d <<<\n", message_count, qos);
                    
                    uint32_t pub_start = to_ms_since_boot(get_absolute_time());
                    int pub_result = mqttsn_demo_publish_name("pico/test", (const uint8_t*)msg, (int)strlen(msg));
                    uint32_t pub_end = to_ms_since_boot(get_absolute_time());
                    
                    if (pub_result == 0) {
                        printf("[MQTTSN] ✓ SUCCESS: Message published (latency=%lums)\n", pub_end - pub_start);
                    } else {
                        printf("[MQTTSN] ✗ WARNING: Publish failed (rc=%d)\n", pub_result);
                        mqtt_demo_started = false;
                        mqttsn_demo_close();
                    }
                    last_publish = now_ms;
                }

                if (block_transfer_button_pressed()) {
                    printf("[BUTTON] Block Transfer button pressed.\n");
                    app_start_block_transfer();
                }
            }

        } else {
            if (now % 5000 < 100) {
                printf("[APP] Waiting for WiFi... (Status: %s)\n", wifi_get_status());
            }
        }

        // Print stats every 30 seconds
        if (absolute_time_diff_us(last_status_print, get_absolute_time()) > 30000000) {
            printf("\n=== System Statistics ===\n");
            wifi_print_stats();
            printf("MQTT-SN Status: %s\n", mqtt_demo_started ? "Connected" : "Disconnected");
            printf("Current QoS Level: %d\n", mqttsn_get_qos());
            if (mqtt_demo_started) {
                printf("Uptime: %lu seconds\n", (now - connection_start_time) / 1000);
            }
            last_status_print = get_absolute_time();
        }

        cyw43_arch_poll();
        sleep_ms(10);
    }

    mqttsn_demo_close();
    return 0;
}