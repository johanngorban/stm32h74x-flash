#include "flash.h"
#include "stm32h7xx_hal.h"
#include <stdlib.h>
#include <string.h>

#define FLASH_BANK1_END             (FLASH_BANK2_BASE - 1)
#define FLASH_BANK2_END             (FLASH_BANK2_BASE + FLASH_SIZE - 1)
#define FLASH_WORD_PROGRAM_WORDS    (FLASH_WORD_SIZE / sizeof(uint32_t))
#define FLASH_WORD_SIZE             (32U)         // 32 bytes
#define MCU_WORD_SIZE               (4U)          // 4 byte (32-bit)

static inline uint8_t IsFlash(uint32_t addr) {
    return (addr >= FLASH_BASE && addr <= FLASH_END);
}

static inline uint8_t GetBank(uint32_t addr) {
    if (addr <= FLASH_BANK1_END) {
        return FLASH_BANK_1;
    }
    return FLASH_BANK_2;
}

static inline uint8_t GetSector(uint32_t addr) {
    uint32_t sector_offset = 0;

    uint8_t bank = GetBank(addr);
    if (bank == FLASH_BANK_1) { 
        sector_offset = addr - FLASH_BANK1_BASE;
    }
    else {
        sector_offset = addr - FLASH_BANK2_BASE;
    }

    return sector_offset / FLASH_SECTOR_SIZE;
}

static inline flash_result_t __flash_write_aligned(uint32_t addr, const uint32_t *data, uint32_t length);
static inline flash_result_t __flash_erase(uint32_t addr, uint32_t length);
static inline void __copy_from_flash(uint32_t *addr, uint32_t *buffer, uint32_t length);

flash_result_t flash_copy(uint32_t addr_from, uint32_t addr_to, uint32_t length) {
    uint32_t bytes_to_copy = length * MCU_WORD_SIZE;
    if (!IsFlash(addr_from) ||
        !IsFlash(addr_to) ||
        (addr_from > FLASH_END - bytes_to_copy) ||
        (addr_to > FLASH_END - bytes_to_copy)    
    ) {
        return FLASH_ERROR_ADDRESS;
    }

    if (((addr_from % FLASH_WORD_SIZE) != 0) ||
        ((addr_to % FLASH_WORD_SIZE) != 0)
    ) {
        return FLASH_ERROR_ALIGNMENT;
    }

    if (((addr_from <= addr_to) && (addr_from > addr_to - bytes_to_copy)) ||
        ((addr_from > addr_to) && (addr_to > addr_from - bytes_to_copy))
    ) {
        return FLASH_ERROR_COPY;
    }

    flash_result_t status = FLASH_OK;

    uint32_t *buffer = (uint32_t *) malloc(length * MCU_WORD_SIZE);
    if (buffer == NULL) {
        return FLASH_ERROR_COPY;
    }

    __copy_from_flash((uint32_t *) addr_from, buffer, length);

    HAL_FLASH_Unlock();
    status = __flash_erase(addr_to, bytes_to_copy);
    if (status != FLASH_OK) {
        free(buffer);
        return status;
    }

    status = __flash_write_aligned(addr_to, buffer, length);
    free(buffer);

    HAL_FLASH_Lock();

    return status;
}

flash_result_t flash_write(uint32_t addr, const uint32_t *data, uint32_t length) {
    if (!IsFlash(addr) || (addr > FLASH_END - length)) {
        return FLASH_ERROR_ADDRESS;
    }

    // addr should be aligned to the Flash word (256 bites)
    if ((addr % FLASH_WORD_SIZE) != 0) {
        return FLASH_ERROR_ALIGNMENT;
    }

    flash_result_t status = FLASH_OK;

    HAL_FLASH_Unlock();
    // count of data should be aligned to 8
    if ((length % FLASH_WORD_PROGRAM_WORDS) == 0) {
        status = __flash_write_aligned(addr, data, length);
    }
    else {
        // Write first aligned to FLASH_WORD_PROGRAM_WORDS words
        uint16_t offset = length - (length % FLASH_WORD_PROGRAM_WORDS);
        status = __flash_write_aligned(addr, data, offset);
        if (status != FLASH_OK) {
            HAL_FLASH_Lock();
            return status;
        }
        
        // Write last words
        uint32_t buffer[FLASH_WORD_PROGRAM_WORDS];
        uint16_t remaining = length % FLASH_WORD_PROGRAM_WORDS;
        memcpy(buffer, data + offset, remaining * MCU_WORD_SIZE);

        uint32_t *addr_to_copy = (uint32_t *) (addr + offset * MCU_WORD_SIZE); 
        memcpy(
            buffer + remaining,
            addr_to_copy,
            (FLASH_WORD_PROGRAM_WORDS - remaining) * MCU_WORD_SIZE
        );

        status = __flash_write_aligned((uint32_t) addr_to_copy, buffer, FLASH_WORD_PROGRAM_WORDS);
    }
    
    HAL_FLASH_Lock();

    return status;
}

flash_result_t flash_erase(uint32_t addr, uint32_t length) {
    if (!IsFlash(addr) || (addr > FLASH_END - length)) {
        return FLASH_ERROR_ADDRESS;
    }

    if ((addr % FLASH_SECTOR_SIZE) != 0) {
        return FLASH_ERROR_ALIGNMENT;
    }

    HAL_FLASH_Unlock();
    flash_result_t status = __flash_erase(addr, length);
    HAL_FLASH_Lock();

    return status;
}

flash_result_t __flash_write_aligned(uint32_t addr, const uint32_t *data, uint32_t length) {
    uint32_t offset = 0;
    uint32_t addr_end = addr + length * MCU_WORD_SIZE;
    while (addr < addr_end) {
        if (HAL_FLASH_Program(
                FLASH_TYPEPROGRAM_FLASHWORD,
                addr,
                (uint32_t) (data + offset)
            ) != HAL_OK) {
            return FLASH_ERROR_WRITE;
        }

        // 8 length (32 bytes in total) written per iteration
        offset += FLASH_WORD_PROGRAM_WORDS;

        // 32 byte is written per time
        addr += FLASH_WORD_SIZE; 
    }

    return FLASH_OK;
}

flash_result_t __flash_erase(uint32_t addr, uint32_t bytes) {
    uint32_t current_addr = addr;
    while (current_addr < (addr + bytes)) {
        uint8_t bank = GetBank(current_addr);
        uint8_t sector = GetSector(current_addr);

        uint32_t sector_start = (bank == FLASH_BANK_1) ? FLASH_BANK1_BASE : FLASH_BANK2_BASE;
        sector_start += sector * FLASH_SECTOR_SIZE;

        FLASH_EraseInitTypeDef EraseInit = {0};
        EraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
        EraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
        EraseInit.Banks = bank;
        EraseInit.Sector = sector;
        EraseInit.NbSectors = 1;

        uint32_t error;
        if (HAL_FLASHEx_Erase(&EraseInit, &error) != HAL_OK) {
            return FLASH_ERROR_ERASE;
        }

        current_addr = sector_start + FLASH_SECTOR_SIZE;
    }

    return FLASH_OK;
}

void __copy_from_flash(uint32_t *source, uint32_t *dest, uint32_t length) {
    memcpy(dest, source, length * MCU_WORD_SIZE);
}