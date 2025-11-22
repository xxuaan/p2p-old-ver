/*-----------------------------------------------------------------------*/
/* Low level disk I/O module for Pico W SD Card                         */
/*-----------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declarations from sd_card.h
extern int sd_card_init(void);
extern int sd_card_read_sector(uint32_t sector, uint8_t *buffer);
extern int sd_card_write_sector(uint32_t sector, const uint8_t *buffer);
extern bool sd_card_is_initialized(void);  // Check hardware only, not filesystem

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/
DSTATUS disk_status (
    BYTE pdrv       /* Physical drive number to identify the drive */
)
{
    if (pdrv != 0) return STA_NOINIT;
    
    return sd_card_is_initialized() ? 0 : STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Initialize a Drive                                                    */
/*-----------------------------------------------------------------------*/
DSTATUS disk_initialize (
    BYTE pdrv               /* Physical drive number to identify the drive */
)
{
    if (pdrv != 0) return STA_NOINIT;
    
    if (sd_card_init() == 0) {
        return 0;  // Success
    }
    return STA_NOINIT;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/
DRESULT disk_read (
    BYTE pdrv,      /* Physical drive number to identify the drive */
    BYTE *buff,     /* Data buffer to store read data */
    LBA_t sector,   /* Start sector in LBA */
    UINT count      /* Number of sectors to read */
)
{
    if (pdrv != 0) return RES_PARERR;
    if (!sd_card_is_initialized()) return RES_NOTRDY;
    
    for (UINT i = 0; i < count; i++) {
        if (sd_card_read_sector(sector + i, buff + (i * 512)) != 0) {
            return RES_ERROR;
        }
    }
    
    return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/
#if FF_FS_READONLY == 0
DRESULT disk_write (
    BYTE pdrv,          /* Physical drive number to identify the drive */
    const BYTE *buff,   /* Data to be written */
    LBA_t sector,       /* Start sector in LBA */
    UINT count          /* Number of sectors to write */
)
{
    if (pdrv != 0) return RES_PARERR;
    if (!sd_card_is_initialized()) return RES_NOTRDY;
    
    for (UINT i = 0; i < count; i++) {
        if (sd_card_write_sector(sector + i, buff + (i * 512)) != 0) {
            return RES_ERROR;
        }
    }
    
    return RES_OK;
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/
DRESULT disk_ioctl (
    BYTE pdrv,      /* Physical drive number (0..) */
    BYTE cmd,       /* Control code */
    void *buff      /* Buffer to send/receive control data */
)
{
    if (pdrv != 0) return RES_PARERR;
    
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;
            
        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = 0;  // Will be detected automatically
            return RES_OK;
            
        case GET_SECTOR_SIZE:
            *(WORD*)buff = 512;
            return RES_OK;
            
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;  // 1 sector
            return RES_OK;
    }
    
    return RES_PARERR;
}

/*-----------------------------------------------------------------------*/
/* Get current time for filesystem timestamps                            */
/*-----------------------------------------------------------------------*/
DWORD get_fattime(void)
{
    // Return a fixed time: 2025-10-31 20:00:00
    return ((2025 - 1980) << 25)  // Year
         | (10 << 21)             // Month
         | (31 << 16)             // Day
         | (20 << 11)             // Hour
         | (0 << 5)               // Minute
         | (0 >> 1);              // Second / 2
}
