#ifndef BLOCK_TRANSFER_H
#define BLOCK_TRANSFER_H

#include "pico/stdlib.h"

// Block transfer constants
#define BLOCK_CHUNK_SIZE 128        // Size of each chunk (adjust based on MQTT-SN packet limits)
#define BLOCK_MAX_CHUNKS 3000       // Maximum number of chunks per block (supports up to ~375KB images)
#define BLOCK_BUFFER_SIZE 150000    // 150KB buffer - fits in Pico W's ~264KB RAM with room for stack/WiFi
#define MAX_SUPPORTED_FILE_SIZE 150000  // Maximum file size we can handle (150KB) - safe for Pico W RAM

// Block transfer header structure
typedef struct {
    uint16_t block_id;      // Unique block identifier
    uint16_t part_num;      // Current part number (1-based)
    uint16_t total_parts;   // Total number of parts
    uint16_t data_len;      // Length of data in this chunk
} block_header_t;

// Block status message (for requesting retransmission)
#define BLOCK_STATUS_COMPLETE 0
#define BLOCK_STATUS_MISSING  1
typedef struct {
    uint16_t block_id;
    uint8_t status;           // COMPLETE or MISSING
    uint16_t missing_count;   // Number of missing chunks
    uint16_t missing_chunks[50]; // List of missing chunk numbers (max 50 at a time)
} block_status_msg_t;

// Block reassembly structure
typedef struct {
    uint16_t block_id;
    uint16_t total_parts;
    uint16_t received_parts;
    bool *received_mask;    // Track which parts we've received
    uint8_t *data_buffer;   // Buffer to store reassembled data
    uint32_t total_length;  // Total length of complete message
    uint32_t last_update;   // Timestamp of last received part
} block_assembly_t;

// Block transfer functions
int block_transfer_init(void);
int send_block_transfer(const char *topic, const uint8_t *data, size_t data_len);
int send_block_transfer_qos(const char *topic, const uint8_t *data, size_t data_len, uint8_t qos);
int send_image_file(const char *topic, const char *filename);
int send_image_file_qos(const char *topic, const char *filename, uint8_t qos);
void process_block_chunk(const uint8_t *data, size_t len);
void generate_large_message(char *buffer, size_t size);
bool block_transfer_is_active(void);
void block_transfer_check_timeout(void);
void send_block_status(uint16_t block_id, uint8_t status, uint16_t *missing_chunks, uint16_t missing_count);
void process_block_status(const uint8_t *data, size_t len);

#endif // BLOCK_TRANSFER_H