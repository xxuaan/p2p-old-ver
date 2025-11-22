#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

void wait_for_sd_card();
bool initialize_sd_card();
bool check_sd_card_status();
bool scan_and_select_image();
const char* sd_card_get_first_image(void);  // Get first image filename from scan

// SD card function prototypes
void sd_card_deinit(void);
int sd_card_init(void);
int sd_card_init_with_detection(void);
int sd_card_simple_detect(void);
int sd_card_read_sector(uint32_t sector, uint8_t *buffer);
int sd_card_write_sector(uint32_t sector, const uint8_t *buffer);
bool sd_card_is_mounted(void);
bool sd_card_is_initialized(void);  // Hardware initialized (for diskio)
void sd_card_check_status(void);

// FAT32 filesystem operations
int sd_card_mount_fat32(void);
int sd_card_format_fat32(void);

// High-level file operations (FAT32)
int sd_card_write_file(const char *filename, const uint8_t *data, size_t length);
int sd_card_read_file(const char *filename, uint8_t *buffer, size_t max_length, size_t *actual_length);
int sd_card_create_test_file(const char *filename);
int sd_card_send_file(const char *filename, const char *mqtt_topic);
void sd_card_list_files(void);
int sd_card_save_block(const char *filename, const uint8_t *data, size_t size);

#endif // SD_CARD_H