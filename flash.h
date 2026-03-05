#pragma once

#include <stdint.h>

typedef enum
{
    FLASH_OK,
    FLASH_ERROR_ADDRESS,
    FLASH_ERROR_ALIGNMENT,
    FLASH_ERROR_ERASE,
    FLASH_ERROR_WRITE,
    FLASH_ERROR_OVERFLOW,
    FLASH_ERROR_COPY,
} flash_result_t;


/**
 * Erase sectors of Flash
 * 
 * The function erases all sectors that intersect the specified range.
 * The number of erased sectors equals ceil(bytes / FLASH_SECTOR_SIZE)
 * 
 * @param addr Base address to erase. Should be aligned to FLASH_SECTOR_SIZE
 * @param bytes Bytes to erase
 * 
 *  @return FLASH_OK on success
 */
flash_result_t flash_erase(uint32_t addr, uint32_t bytes);

/**
 * Write length words from data array
 * 
 * @param addr Base address to write. Should be aligned to FLASH_WORD_SIZE (32 bytes)
 * @param data Array of words to be written
 * @param length Length of data (words)
 * 
 * @return FLASH_OK on success
 */
flash_result_t flash_write(
    uint32_t addr,
    const uint32_t *data,
    uint32_t length
);

/**
 * Copy length words from addr_from to addr_to
 * 
 * @param addr_from Address of data to be copied
 * @param addr_to Copy destination address
 * @param bytes Count of bytes to be copied
 * 
 * @note addr_from + bytes should be less than addr_to (addr_from + bytes < addr_to). Otherwise, FLASH_ERROR_COPY will be returned
 * @note All data in the sectors of [addr_to; addr_to + bytes - 1] will be erased except data to copy
 * 
 * @return FLASH_OK on success. FLASH_ERROR_WRITE in case of errors during writing, FLASH_ERROR_ERASE in case of errors during erasing
 */
flash_result_t flash_copy(uint32_t addr_from, uint32_t addr_to, uint32_t bytes);