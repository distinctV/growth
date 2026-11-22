/**
 * @file growth_stats.c
 * @brief Implementation of data extraction for unit growth rates and stats.
 */

#include "growth_stats.h"
#include <string.h>

// --- Constants ---
static const uint32_t CLASS_TABLE_OFFSET = 0x038FC5;
static const int CLASS_RECORD_SIZE = 31;
static const int UNIT_RECORD_GROWTH_OFFSET = 20;
static const int UNIT_RECORD_MAX_SIZE = 40;

// --- Internal Helpers (Private) ---

/**
 * @brief Reads a 16-bit little-endian value from the specified address.
 */
static uint32_t w16(const uint8_t* r, uint32_t a) { return r[a] | r[a + 1] << 8; }

/**
 * @brief Resolves a 24-bit pointer from SNES RAM (banks $7E and $7F).
 */
static uint32_t ram_ptr(const uint8_t* r, uint32_t a) {
  uint8_t bank = r[a + 2];
  if(bank != 0x7E && bank != 0x7F) return 0;
  return (uint32_t)(bank - 0x7E) * 0x10000 + w16(r, a);
}

/**
 * @brief Retrieves the pointer to a unit's record in ROM.
 */
static uint32_t get_unit_record_ptr(const uint8_t* r, uint32_t h) {
  if (h < 0x2000 || h > 0xFFE0) return 0;
  uint8_t t = r[h];
  // Type 1 (child) records are handled differently in the game engine.
  if(t != 1 && t != 3) {
    return (uint32_t)((r[h + 9] & 0x3F) << 16 | w16(r, h + 7));
  }
  return 0;
}

/**
 * @brief Gets the unit's data pointer in RAM.
 */
static uint32_t get_unit_data_ptr(const uint8_t* ram, uint32_t handle) {
    uint32_t data_ptr = ram_ptr(ram, handle + 4);
    return (data_ptr != 0) ? data_ptr : 0;
}

/**
 * @brief Gets the unit's class ID from RAM.
 */
static uint8_t get_unit_class_id(const uint8_t* ram, uint32_t data_ptr) {
    return ram[data_ptr + 8];
}

/**
 * @brief Calculates the ROM address of a class record.
 */
static uint32_t get_class_record_ptr(uint8_t class_id) {
    return CLASS_TABLE_OFFSET + (class_id * CLASS_RECORD_SIZE);
}

/**
 * @brief Reads a base growth rate from the class table in ROM.
 */
static int read_rom_class_growth(const uint8_t* rom, uint32_t class_record_ptr, StatType stat) {
    // Class growth rates are at offset +1 relative to the start of the record (e.g., Str=2, Mag=3...)
    return rom[class_record_ptr + stat + 1];
}

/**
 * @brief Reads the total growth rate from a unit's ROM record.
 */
static int read_rom_total_growth(const uint8_t* rom, uint32_t record_ptr, StatType stat) {
    if (stat == STAT_HP) {
        return rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET];
    }
    return rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + stat];
}

// --- Public API Implementation ---

void get_unit_growth_stat(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out) {
    memset(out, 0, sizeof *out);
    if (handle < 0x2000 || handle > 0xFFE0) return;

    uint8_t type = ram[handle];
    // We only support Type 0 and 2 for now as they have the most data in ROM.
    if (type != 0 && type != 2) return;

    uint32_t data_ptr = get_unit_data_ptr(ram, handle);
    if (!data_ptr) return;

    // 1. Get Class Base Growth from the class table in ROM
    uint8_t class_id = get_unit_class_id(ram, data_ptr);
    uint32_t class_record_ptr = get_class_record_ptr(class_id);
    int class_base = read_rom_class_growth(rom, class_record_ptr, stat);

    // 2. Get Total Growth Rate from the unit's record in ROM
    uint32_t record_ptr = get_unit_record_ptr(ram, handle);
    int total_growth = 0;
    if (record_ptr && record_ptr + UNIT_RECORD_MAX_SIZE < (uint32_t)romLen) {
        total_growth = read_rom_total_growth(rom, record_ptr, stat);
    }

    // 3. Calculate Personal Modifier
    // Total Growth = Class Base + Personal Modifier
    int personal = total_growth - class_base;

    out->class_base_growth = class_base;
    out->personal_modifier = personal;
    out->total_growth_rate = total_growth;
}

void get_unit_all_growths(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, GrowthStatInfo* out) {
    for (int i = 0; i < STAT_COUNT; i++) {
        get_unit_growth_stat(ram, rom, romLen, handle, (StatType)i, &out[i]);
    }
}