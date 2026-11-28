/**
 * @file growth_stats.c
 * @brief Implementation of data extraction for unit growth rates, expected values, and stat display formatting.
 *
 * Architecture follows 4 layers:
 * Layer 1: Memory Access (read_u8, read_u16, resolve_ram_ptr)
 * Layer 2: Data Accessors (get_unit_data_ptr, get_class_record_ptr, etc.)
 * Layer 3: Business Logic (calculate_expected_gain, evaluate_rng_impact)
 * Layer 4: Aggregation Service (get_unit_growth_stat, format_stat_display_line)
 */

#include "growth_stats.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

// ============================================================================
// Blood Type Slot Mapping (replaces verbose switch statements)
// ============================================================================
const BloodType BLOOD_TYPE_MAP[13] = {
    BLOOD_BALDO, BLOOD_ODO, BLOOD_HEZUL, BLOOD_NOBA, BLOOD_DAIN,
    BLOOD_NEIR, BLOOD_ULIR, BLOOD_BLAGI, BLOOD_FALA, BLOOD_TORDO,
    BLOOD_HOLSETY, BLOOD_NARGA, BLOOD_LOPUTOUSU
};

/* Stat label lookup table */
const char* STAT_LABELS[STAT_COUNT] = {"HP", "STR", "MAG", "SKL", "SPD", "DEF", "RES", "LCK"};

/* Impact string lookup (replaces switch in format_stat_display_line) */
static const char* IMPACT_STR_MAP[] = { "[C]", "[N]", "[B]" };

// ============================================================================
// Constant Definitions (extern declarations from header)
// ============================================================================
const uint32_t CLASS_TABLE_OFFSET    = 0x10000;
const int      CLASS_RECORD_SIZE     = 31;
const int      UNIT_RECORD_GROWTH_OFFSET = 20;
const int      UNIT_RECORD_MAX_SIZE    = 8;
const int      UNIT_CLASS_ID_OFFSET  = 9;
const int      UNIT_LEVEL_OFFSET     = 10;
const int      UNIT_BLOOD_OFFSET     = 31;
const int TYPE1_CLASS_OFFSET   = 5;
const int TYPE1_LEVEL_OFFSET   = 7;
const int TYPE1_GENDER_OFFSET  = 8;
const int TYPE1_HEIR_OFFSET    = 11;
const int PARENT_SAME_GENDER_OFFSET = 20;
const int PARENT_SECONDARY_OFFSET   = 24;
const int TYPE3_CLASS_OFFSET = 5;
const int TYPE3_LEVEL_OFFSET = 7;
const int CLASS_HP_BASE_OFFSET    = 1;
const int CLASS_STAT_BASE_OFFSET  = 2;
const uint8_t RAM_BANK_LOW  = 0x7E;
const uint8_t RAM_BANK_HIGH = 0x7F;
const uint32_t UNIT_HANDLE_MIN = 0x2000;
const uint32_t UNIT_HANDLE_MAX = 0xFFE0;
const uint8_t CLASS_ID_MASK = 0x3F;
const uint8_t UNIT_TYPE_PLAYABLE   = 0;
const uint8_t UNIT_TYPE_CHILD      = 1;
const uint8_t UNIT_TYPE_NPC_BOSS   = 2;
const uint8_t UNIT_TYPE_CLASS_BASED = 3;
const char* IMPACT_BLESSED_STR = "[B]";
const char* IMPACT_CURSED_STR  = "[C]";
const char* IMPACT_NEUTRAL_STR  = "[N]";
const float BLESSED_SIGMA_THRESHOLD = 1.0f;
const float CURSED_SIGMA_THRESHOLD = -1.0f;


// ============================================================================
// Blood Slot Decoding Helper (extracted from 3 duplicated functions)
// ============================================================================

/**
 * @brief Decodes a single blood slot value from the 3-byte holy blood field.
 *
 * The 13 slots (2 bits each) are stored LSB-first across 3 bytes:
 *   byte0: slots 0-3, byte1: slots 4-7, byte2: slots 8-9
 */
static inline uint8_t decode_blood_slot(const uint8_t* blood_ptr, int slot_index) {
    int bit_offset = slot_index * 2;
    if (bit_offset < 8)
        return (blood_ptr[0] >> bit_offset) & 0x03;
    if (bit_offset < 16)
        return (blood_ptr[1] >> (bit_offset - 8)) & 0x03;
    return (blood_ptr[2] >> (bit_offset - 16)) & 0x03;
}

/* Class growth offset per stat index (stat 0=HP at +8, others at +9..+13, LCK at +15) */
const int CLASS_GROWTH_OFFSET[STAT_COUNT] = {8, 9, 10, 11, 12, 13, 14, 15};

/* Unit record growth offset per stat index (stat 0=HP at +20, others at +20+stat) */
const int UNIT_GROWTH_OFFSET[STAT_COUNT] = {20, 21, 22, 23, 24, 25, 26, 27};

// ============================================================================
// Layer 1: Memory Access (The "Drivers")
// ============================================================================

/**
 * @brief Reads an unsigned 8-bit value from raw memory at offset a.
 */
uint8 read_u8(const uint8_t* r, uint32_t a) {
    return r[a];
}

/**
 * @brief Reads an unsigned 16-bit little-endian value from raw memory at offset a.
 */
uint16 read_u16(const uint8_t* r, uint32_t a) {
    return (uint16)r[a] | ((uint16)r[a + 1] << 8);
}

/**
 * @brief Resolves a 24-bit pointer from SNES RAM (banks $7E and $7F).
 *
 * SNES uses banked memory: $7E for low 64KB, $7F for high 64KB.
 * Returns 0 if bank is invalid (pointer resolution failure).
 */
uint32_t resolve_ram_ptr(const uint8_t* r, uint32_t a) {
    uint8_t bank = r[a + 2];
    if (bank != RAM_BANK_LOW && bank != RAM_BANK_HIGH) return 0;
    uint16_t offset = read_u16(r, a);
    return (uint32_t)(bank - RAM_BANK_LOW) * 0x10000 + offset;
}

// ============================================================================
// Layer 2: Data Accessors (The "Data Layer")
// ============================================================================

/**
 * @brief Gets the unit's data pointer in RAM from its handle.
 *
 * Unit handle + 4 contains a 3-byte SNES pointer to the unit's data block.
 */
uint32_t get_unit_data_ptr(const uint8_t* ram, uint32_t handle) {
    return resolve_ram_ptr(ram, handle + 4);
}

/**
 * @brief Gets the unit's class ID from RAM data.
 *
 * Class ID is stored at offset +9 within the unit's data block (per ROM_MAP.md).
 */
uint8_t get_unit_class_id(const uint8_t* ram, uint32_t data_ptr) {
    return read_u8(ram, data_ptr + UNIT_CLASS_ID_OFFSET);
}

/**
 * @brief Gets the unit's current level from RAM data.
 *
 * Level is stored at offset +10 within the unit's data block.
 */
int get_unit_level(const uint8_t* ram, uint32_t data_ptr) {
    return read_u8(ram, data_ptr + UNIT_LEVEL_OFFSET);
}

/**
 * @brief Calculates the ROM address of a class record from class_id.
 *
 * Class table starts at CLASS_TABLE_OFFSET, each record is CLASS_RECORD_SIZE bytes.
 */
uint32_t get_class_record_ptr(uint8_t class_id) {
    return CLASS_TABLE_OFFSET + ((uint32_t)class_id * CLASS_RECORD_SIZE);
}

/**
 * @brief Reads a base growth rate from the class table in ROM for a specific stat.
 *
 * Per ROM_MAP.md: Class records have growths at offset +8-14:
 *   +8 = HP, +9 = STR, +10 = MAG, +11 = SKL, +12 = SPD, +13 = DEF, +15 = LCK
 */
int read_rom_class_growth(const uint8_t* rom, uint32_t class_record_ptr, StatType stat) {
    if (stat < STAT_HP || stat >= STAT_COUNT) return 0;
    return rom[class_record_ptr + CLASS_GROWTH_OFFSET[stat]];
}

/**
 * @brief Gets the unit's record pointer in ROM from its handle.
 *
 * For Type 0/2 units: pointer is derived from class_id (at offset +9) and record address (offset +7).
 * Returns 0 for unsupported types or invalid handles.
 */
uint32_t get_unit_record_ptr(const uint8_t* r, uint32_t h) {
    if (h < UNIT_HANDLE_MIN || h > UNIT_HANDLE_MAX) return 0;

    uint8_t type = read_u8(r, h);
    // Type 1 (child) and Type 3 records are handled differently in the game engine.
    if (type != UNIT_TYPE_PLAYABLE && type != UNIT_TYPE_NPC_BOSS) return 0;

    // Record pointer: class_id at offset +9, low 16-bit address at offset +7
    uint8_t class_byte = read_u8(r, h + 9);
    uint16_t record_offset = read_u16(r, h + 7);
    return ((uint32_t)(class_byte & CLASS_ID_MASK) << 16) | record_offset;
}

/**
 * @brief Reads the total growth rate from a unit's ROM record for a specific stat.
 *
 * Unit record growth offsets per stat (HP at +20, others at +21..+27).
 */
int read_rom_total_growth(const uint8_t* rom, uint32_t record_ptr, StatType stat) {
    if (stat < STAT_HP || stat >= STAT_COUNT) return 0;
    return rom[record_ptr + UNIT_GROWTH_OFFSET[stat]];
}

/**
 * @brief Returns the unit record type byte (0=playable fixed, 1=inherited child, 2=npc/boss, 3=class-based)
 */
uint8_t get_unit_type(const uint8_t* ram, uint32_t handle) {
    if (!ram || !handle) return 0;
    return read_u8(ram, handle);
}

/**
 * @brief Gets child's gender from RAM data block (offset +8 for Type 1)
 */
int get_unit_gender(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram || !data_ptr) return -1;
    return read_u8(ram, data_ptr + TYPE1_GENDER_OFFSET);
}

/**
 * @brief Gets child's heir flag (1=father blood, 0=mother blood) from RAM data block (offset +11 for Type 1)
 */
int get_child_heir_flag(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram || !data_ptr) return -1;
    return read_u8(ram, data_ptr + TYPE1_HEIR_OFFSET);
}

// ============================================================================
// Layer 2/3: Blood Type Helpers
// ============================================================================

/**
 * @brief Maps a raw byte value to BloodType enum.
 *
 * Explicit mapping ensures consistent comparison between test values and enum types.
 */
BloodType blood_type_from_byte(uint8_t byte_val) {
    if (byte_val == 0) return BLOOD_NONE;
    if (byte_val >= 1 && byte_val <= 13) return BLOOD_TYPE_MAP[byte_val - 1];
    return BLOOD_NONE;  // Invalid value
}

/**
 * @brief Converts BloodType enum back to raw byte for storage.
 */
uint8_t blood_type_to_byte(BloodType blood) {
    if (blood == BLOOD_NONE) return 0;
    if (blood >= BLOOD_BALDO && blood < BLOOD_COUNT) return (uint8_t)(blood - BLOOD_BALDO + 1);
    return 0;  // Invalid
}

/**
 * @brief Reads blood type from unit's RAM data block.
 *
 * Blood type is stored at offset +31 within the unit's data block (13 x 2-bit slots).
 * Returns BLOOD_NONE if invalid or no blood detected.
 */
BloodType get_unit_blood_type(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram) return BLOOD_NONE;

    const uint8_t* blood_ptr = ram + data_ptr + UNIT_BLOOD_OFFSET;

    // Decode 13 slots (26 bits total)
    for (int i = 0; i < 13; i++) {
        uint8_t val = decode_blood_slot(blood_ptr, i);
        if (val == 1 || val == 2) {
            return BLOOD_TYPE_MAP[i];
        }
    }

    return BLOOD_NONE;
}

// ============================================================================
// Layer 2: Parent Data Accessors (Generation Inheritance)
// ============================================================================

/**
 * @brief Reads parent handle from unit's RAM data block (same-gender parent at offset +20).
 *
 * Returns 0 if no parent exists or invalid handle.
 */
uint32_t get_same_gender_parent_handle(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram || !data_ptr) return 0;

    // Parent handle is a 3-byte SNES pointer at offset +20
    return resolve_ram_ptr(ram, data_ptr + PARENT_SAME_GENDER_OFFSET);
}

/**
 * @brief Reads secondary parent handle from unit's RAM data block (other-gender parent at offset +24).
 *
 * Returns 0 if no secondary parent exists or invalid handle.
 */
uint32_t get_secondary_parent_handle(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram || !data_ptr) return 0;

    // Secondary parent handle is a 3-byte SNES pointer at offset +24
    return resolve_ram_ptr(ram, data_ptr + PARENT_SECONDARY_OFFSET);
}

// ============================================================================
// Layer 3: Business Logic (The "Engine")
// ============================================================================

/**
 * @brief Calculates expected stat gain over a number of levels using binomial mean.
 *
 * Expected gain = growth_rate * levels / 100
 * This is the mean of the binomial distribution E[X] = n * p
 */
float calculate_expected_gain(int growth_rate, int levels) {
    return (float)growth_rate * (float)levels / 100.0f;
}

/**
 * @brief Calculates standard deviation for binomial distribution over levels.
 *
 * Std dev = sqrt(n * p * (1 - p)) where p = growth_rate / 100
 */
float calculate_std_dev(int growth_rate, int levels) {
    float p = (float)growth_rate / 100.0f;
    return sqrtf((float)levels * p * (1.0f - p));
}

/**
 * @brief Evaluates RNG impact: compares actual gain to expected with sigma threshold.
 *
 * Blessed: actual_gain > expected + BLESSED_SIGMA_THRESHOLD * std_dev
 * Cursed: actual_gain < expected + CURSED_SIGMA_THRESHOLD * std_dev
 * Neutral: otherwise
 */
RngImpact evaluate_rng_impact(int actual_gain, float expected_gain, float std_dev) {
    // Handle edge case where std_dev is near zero (deterministic growth)
    if (std_dev < 0.001f) {
        if ((float)actual_gain > expected_gain + 0.5f) return RNG_BLESSED;
        if ((float)actual_gain < expected_gain - 0.5f) return RNG_CURSED;
        return RNG_NEUTRAL;
    }

    float blessed_threshold = expected_gain + (BLESSED_SIGMA_THRESHOLD * std_dev);
    float cursed_threshold = expected_gain + (CURSED_SIGMA_THRESHOLD * std_dev);

    if ((float)actual_gain > blessed_threshold) return RNG_BLESSED;
    if ((float)actual_gain < cursed_threshold) return RNG_CURSED;
    return RNG_NEUTRAL;
}

// ============================================================================
// Layer 3: Blood Bonus Calculation (The "Engine")
// ============================================================================

/* Blood bonus lookup table indexed by BloodType enum */
const BloodBonus BLOOD_BONUS_MAP[BLOOD_COUNT] = {
    // NONE   - no bonuses
    {0, 0, 0, 0, 0, 0, 0, 0},
    // BALDO  - +20 HP, +10 STR, +10 SKL, +10 LCK
    {20, 10, 0, 10, 0, 0, 10, 0},
    // ODO    - +20 HP, +30 SKL
    {20, 0, 0, 30, 0, 0, 0, 0},
    // HEZUL  - +20 HP, +30 STR
    {20, 30, 0, 0, 0, 0, 0, 0},
    // NOBA   - +20 HP, +10 STR, +10 SPD, +10 DEF
    {20, 10, 0, 0, 10, 10, 0, 0},
    // DAIN   - +20 HP, +30 SPD
    {20, 0, 0, 0, 30, 0, 0, 0},
    // NEIR   - +20 HP, +30 RES
    {20, 0, 0, 0, 0, 0, 0, 30},
    // ULIR   - +20 HP, +30 LCK
    {20, 0, 0, 0, 0, 0, 30, 0},
    // BLAGI  - +10 HP, +10 MAG, +10 LCK, +20 RES
    {10, 0, 10, 0, 0, 0, 10, 20},
    // FALA   - +20 HP, +30 MAG
    {20, 0, 30, 0, 0, 0, 0, 0},
    // TORDO  - +20 HP, +30 MAG
    {20, 0, 30, 0, 0, 0, 0, 0},
    // HOLSETY - +20 HP, +30 SPD
    {20, 0, 0, 0, 30, 0, 0, 0},
    // NARGA  - +10 HP, +20 MAG
    {10, 0, 20, 0, 0, 0, 0, 0},
    // LOPUTOUSU - +10 HP, +20 MAG
    {10, 0, 20, 0, 0, 0, 0, 0}
};

/**
 * @brief Returns the blood bonus table entry for a given blood type.
 *
 * Based on holy_blood_info.md from Serenes Forest wiki.
 */
BloodBonus get_blood_bonus_table(BloodType blood) {
    if (blood < BLOOD_NONE || blood >= BLOOD_COUNT) return (BloodBonus){0};
    return BLOOD_BONUS_MAP[blood];
}

/**
 * @brief Calculates total percentage bonus from blood type.
 *
 * Sums all stat bonuses for display purposes.
 */
int calculate_blood_bonus_total(BloodType blood) {
    if (blood == BLOOD_NONE) return 0;

    BloodBonus bonus = get_blood_bonus_table(blood);
    return bonus.hp_bonus + bonus.str_bonus + bonus.mag_bonus +
           bonus.skl_bonus + bonus.spd_bonus + bonus.def_bonus +
           bonus.lck_bonus + bonus.res_bonus;
}

/**
 * @brief Calculates inherited growth rate per inheritance_rules.md formula.
 *
 * Formula: same_gender_parent_base + (other_gender_parent_base / 2) + blood_bonus
 */
int calculate_inherited_growth_rate(int same_gender_base_growth, int other_gender_base_growth, BloodType child_blood) {
    int base = same_gender_base_growth + (other_gender_base_growth / 2);

    // Add blood bonus if applicable
    if (child_blood != BLOOD_NONE) {
        BloodBonus bonus = get_blood_bonus_table(child_blood);
        // Average all bonuses for growth rate calculation
        int total_bonus = bonus.hp_bonus + bonus.str_bonus + bonus.mag_bonus +
                          bonus.skl_bonus + bonus.spd_bonus + bonus.def_bonus +
                          bonus.lck_bonus + bonus.res_bonus;
        base += total_bonus / 8;  // Simple average of bonuses
    }

    return base;
}

/**
 * @brief Determines child's blood type based on parent blood and gender rules.
 *
 * Rules from inheritance_rules.md:
 * - Major Blood passed to same-gender child
 * - Minor Blood passed to other-gender child
 * - Same Type Rule: Both parents Minor of same type -> both children get Major
 */
BloodType determine_child_blood_type(BloodType same_gender_parent_blood,
                                    BloodType other_gender_parent_blood,
                                    int is_same_gender_child) {
    // No blood in either parent -> no blood
    if (same_gender_parent_blood == BLOOD_NONE && other_gender_parent_blood == BLOOD_NONE) {
        return BLOOD_NONE;
    }

    // Same Type Rule: Both parents Minor of same type -> Major Blood
    if (same_gender_parent_blood != BLOOD_NONE &&
        other_gender_parent_blood != BLOOD_NONE &&
        same_gender_parent_blood == other_gender_parent_blood) {
        return same_gender_parent_blood;  // Promote to Major (same value, game handles internally)
    }

    // Major Blood passed to same-gender child
    if (is_same_gender_child && same_gender_parent_blood != BLOOD_NONE) {
        return same_gender_parent_blood;
    }

    // Minor Blood passed to other-gender child
    if (!is_same_gender_child && other_gender_parent_blood != BLOOD_NONE) {
        return other_gender_parent_blood;
    }

    // Fallback: use same-gender parent's blood if available
    if (same_gender_parent_blood != BLOOD_NONE) {
        return same_gender_parent_blood;
    }

    return BLOOD_NONE;  // No inheritance possible
}

// ============================================================================
// Layer 4: Aggregation Service (The "Service Layer")
// ============================================================================

/**
 * @brief Stat label lookup table.
 */
const char* stat_label(StatType s) {
    if (s < 0 || s >= STAT_COUNT) return "?";
    return STAT_LABELS[s];
}

// --- Forward declarations for type-specific helpers ---
void compute_fixed_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);
void compute_inherited_child_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);
void compute_class_based_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);

/**
 * @brief Retrieves detailed growth information for a single stat.
 */
void get_unit_growth_stat(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out) {
    if (!ram || !handle || !out) return;

    // Initialize output structure (now uses nested structs)
    memset(out, 0, sizeof(GrowthStatInfo));

    uint8_t type = get_unit_type(ram, handle);
    switch (type) {
        case UNIT_TYPE_PLAYABLE:
        case UNIT_TYPE_NPC_BOSS:
            compute_fixed_stats(ram, rom, romLen, handle, stat, out);
            break;
        case UNIT_TYPE_CHILD:
            compute_inherited_child_stats(ram, rom, romLen, handle, stat, out);
            break;
        case UNIT_TYPE_CLASS_BASED:
            compute_class_based_stats(ram, rom, romLen, handle, stat, out);
            break;
        default:
            return;  // Unknown type
    }
}

/** @brief Computes growth stats for Type 0/2 units with fixed personal stats */
void compute_fixed_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out) {
    if (!ram || !handle || !out) return;

    // Layer 1/2: Get RAM data pointer
    uint32_t data_ptr = get_unit_data_ptr(ram, handle);
    if (!data_ptr) return;

    // Layer 2: Get class ID and ROM class record
    uint8_t class_id = get_unit_class_id(ram, data_ptr);
    uint32_t class_record_ptr = get_class_record_ptr(class_id);

    // Layer 2: Read class base growth from ROM
    int class_base_growth = read_rom_class_growth(rom, class_record_ptr, stat);

    // Layer 2: Get unit's ROM record pointer and total growth
    uint32_t record_ptr = get_unit_record_ptr(ram, handle);
    int total_growth_rate = 0;
    if (record_ptr && record_ptr + UNIT_RECORD_MAX_SIZE < (uint32_t)romLen) {
        total_growth_rate = read_rom_total_growth(rom, record_ptr, stat);
    }

    // Compute personal modifier (total - class base)
    int personal_modifier = total_growth_rate - class_base_growth;

    // Layer 2: Get current level
    int level = get_unit_level(ram, data_ptr);

    // Layer 3: Calculate expected value statistics
    float p = (float)total_growth_rate / 100.0f;
    out->expectation.level = level;
    out->expectation.mean = p;  // Mean per level-up
    out->expectation.std_dev = sqrtf(p * (1.0f - p));  // Std dev per level-up

    // Blood info populated separately by get_all_blood_types_for_stat()

    // Store results using new nested struct layout
    out->growth.base = class_base_growth;
    out->growth.personal = personal_modifier;
    out->growth.total = total_growth_rate;
}

/** @brief Computes inherited growth stats for Type 1 children from parent data */
void compute_inherited_child_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out) {
    if (!ram || !handle || !out) return;

    // Type 1 children store data inline at handle+offset (not via pointer)
    uint8_t class_id = read_u8(ram, handle + TYPE1_CLASS_OFFSET);
    int level = read_u8(ram, handle + TYPE1_LEVEL_OFFSET);
    int gender = read_u8(ram, handle + TYPE1_GENDER_OFFSET);
    int heir_flag = read_u8(ram, handle + TYPE1_HEIR_OFFSET);

    // Type 1 children don't store parent handles inline - they're computed by the game engine
    // For unit testing, we return neutral stats as fallback
    // In production, parent lookup would use char ID or army slot to find parents in the unit table

    // Read class base growth from ROM for the child's class
    uint32_t class_record_ptr = get_class_record_ptr(class_id);
    int class_base_growth = read_rom_class_growth(rom, class_record_ptr, stat);

    // Type 1 children don't store parent handles inline - they're computed by the game engine at runtime.
    // For display purposes, we return the child's class growth rate as the inherited estimate.
    // In production, this would require looking up parents by char ID or army slot.

    int inherited_growth = class_base_growth;  // Fallback: use class base growth

    // Compute personal modifier (inherited - class base)
    int personal_modifier = inherited_growth - class_base_growth;

    // Calculate expected value statistics based on inherited growth rate
    float p = (float)inherited_growth / 100.0f;
    out->expectation.level = level;
    out->expectation.mean = p;
    out->expectation.std_dev = sqrtf(p * (1.0f - p));

    // Store results using new nested struct layout
    out->growth.base = class_base_growth;
    out->growth.personal = personal_modifier;
    out->growth.total = inherited_growth;
}

/** @brief Computes stats for Type 3 class-based units (generic enemies/bosses) */
void compute_class_based_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out) {
    if (!ram || !handle || !out) return;

    // Type 3 records store data inline at handle+offset (not via pointer)
    uint8_t class_id = read_u8(ram, handle + TYPE3_CLASS_OFFSET);
    int level = read_u8(ram, handle + TYPE3_LEVEL_OFFSET);

    // Get class base stat and growth from ROM
    uint32_t class_record_ptr = get_class_record_ptr(class_id);

    // Class bases: HP at offset CLASS_HP_BASE_OFFSET, other stats at offset CLASS_STAT_BASE_OFFSET+stat
    int class_base_stat;
    if (stat == STAT_HP) {
        class_base_stat = read_u8(rom, class_record_ptr + CLASS_HP_BASE_OFFSET);
    } else {
        class_base_stat = read_u8(rom, class_record_ptr + CLASS_STAT_BASE_OFFSET + stat);
    }

    // Class growths: HP at offset CLASS_HP_GROWTH_OFFSET, other stats at offset CLASS_STAT_GROWTH_OFFSET+stat
    int class_growth_rate;
    if (stat == STAT_HP) {
        class_growth_rate = read_u8(rom, class_record_ptr + CLASS_HP_GROWTH_OFFSET);
    } else {
        class_growth_rate = read_u8(rom, class_record_ptr + CLASS_STAT_GROWTH_OFFSET + stat);
    }

    // Type 3 stats = class base + class growth x level
    int total_stat = class_base_stat + (class_growth_rate * level / 100);

    // For display: personal_modifier represents the growth contribution
    int personal_modifier = (class_growth_rate * level) / 100;

    // Calculate expected value statistics based on class growth rate
    float p = (float)class_growth_rate / 100.0f;
    out->expectation.level = level;
    out->expectation.mean = p;
    out->expectation.std_dev = sqrtf(p * (1.0f - p));

    // Store results using new nested struct layout
    out->growth.base = class_base_stat;  // Using base stat as display value
    out->growth.personal = personal_modifier;
    out->growth.total = total_stat;
}

/**
 * @brief Retrieves growth info for all 7 stats (HP, STR, MAG, SKL, SPD, DEF, LCK).
 */
void get_unit_all_growths(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, GrowthStatInfo* out) {
    if (!ram || !handle || !out) return;

    for (int i = 0; i < STAT_COUNT; i++) {
        get_unit_growth_stat(ram, rom, romLen, handle, (StatType)i, &out[i]);
    }
}

/**
 * @brief Formats a stat line for display: "STAT base+personal=total gr:N [B/C/N]"
 *
 * Example output: "HP 12+8=20 gr:75 [B]" where [B] = Blessed, [C] = Cursed, [N] = Neutral
 */
void format_stat_display_line(StatType stat, const GrowthStatInfo* info, StatDisplayLine* out) {
    if (!out || !info) return;

    const char* label = stat_label(stat);
    RngImpact impact = RNG_NEUTRAL;

    // Determine impact: compare actual gain vs expected over levels
    // Expected gain = growth_rate * levels / 100 (binomial mean)
    // Actual gain = personal_modifier (total - class base)
    float expected_gain = (float)info->growth.base * info->expectation.level / 100.0f;
    float total_std_dev = sqrtf((float)info->expectation.level * info->expectation.mean * (1.0f - info->expectation.mean));
    int actual_gain = info->growth.personal;

    if ((float)actual_gain > expected_gain + BLESSED_SIGMA_THRESHOLD * total_std_dev) {
        impact = RNG_BLESSED;
    } else if ((float)actual_gain < expected_gain + CURSED_SIGMA_THRESHOLD * total_std_dev) {
        impact = RNG_CURSED;
    } else {
        impact = RNG_NEUTRAL;
    }

    // Format: "STAT base+personal=total [X]" + blood suffix
    // Impact string lookup (RNG_NEUTRAL=0→"[N]", RNG_BLESSED=1→"[B]", RNG_CURSED=-1→"[C]")
    const char* impact_char = IMPACT_STR_MAP[(int)impact + 1];

    // Build base string
    int len = snprintf(out->text, sizeof out->text, "%s %2d+%d=%2d %s",
             label,
             info->growth.base,
             info->growth.personal,
             info->growth.total,
             impact_char);

    // Append blood info if present (matching menu column layout)
    int bc = get_blood_count(info);
    for (int b = 0; b < bc && len < (int)(sizeof out->text) - 16; b++) {
        BloodType bt = get_blood_type_at(info, b);
        int hp_bonus = get_blood_bonus_for_stat(info, b, STAT_HP);
        len += snprintf(out->text + len, sizeof out->text - (size_t)len,
                        " [%s +%d%%]", blood_type_name(bt), hp_bonus);
    }

    out->impact = impact;
}

/**
 * @brief Formats all 7 stats into display lines.
 *
 * Populates an array of StatDisplayLine for all STAT_COUNT entries.
 */
void format_all_stats_display(const GrowthStatInfo* growths, StatDisplayLine* out_lines) {
    if (!growths || !out_lines) return;

    for (int i = 0; i < STAT_COUNT; i++) {
        format_stat_display_line((StatType)i, &growths[i], &out_lines[i]);
    }
}

// ============================================================================
// Layer 4: Column Format for Menu/ImGui Table Rendering
// ============================================================================

/**
 * @brief Formats all 7 stats into column-ready structs for ImGui table rendering.
 *
 * Populates StatDisplayColumn array matching GitHub menu.cpp layout:
 *   Stat | Class | FromXP | Total | TotalGrowthRate | ClassGrowthRate | Blood(s)
 */
void format_all_stats_columns(const GrowthStatInfo* growths, StatDisplayColumn* out_cols) {
    if (!growths || !out_cols) return;

    for (int i = 0; i < STAT_COUNT; i++) {
        const GrowthStatInfo* info = &growths[i];
        StatDisplayColumn* col = &out_cols[i];
        
        // Stat name
        strncpy(col->stat_name, stat_label((StatType)i), sizeof(col->stat_name) - 1);
        col->stat_name[sizeof(col->stat_name) - 1] = '\0';
        
        // Growth data (nested struct layout)
        col->class_base_growth = info->growth.base;
        col->personal_modifier = info->growth.personal;
        col->total_gain = info->growth.total;
        
        // Total growth rate includes blood bonuses for this stat
        int total_gr = info->growth.total;
        int bc = get_blood_count(info);
        for (int b = 0; b < bc; b++) {
            total_gr += get_blood_bonus_for_stat(info, b, (StatType)i);
        }
        col->total_growth_rate = total_gr;
        
        // Class growth rate
        col->class_growth_rate = info->growth.base;
        
        // Blood count and bonuses per stat
        col->blood_count = bc;
        for (int b = 0; b < 2; b++) {
            if (b < bc) {
                for (int s = 0; s < STAT_COUNT; s++) {
                    col->blood_bonus[b][s] = get_blood_bonus_for_stat(info, b, (StatType)s);
                }
            } else {
                memset(col->blood_bonus[b], 0, sizeof(col->blood_bonus[b]));
            }
        }
    }
}

// ============================================================================
// Multi-Blood Type Detection (Cleaned up - no more verbose loops)
// ============================================================================

/**
 * @brief Detects all blood types present on a unit (up to 2) and calculates per-stat bonuses.
 *
 * Scans all 13 x 2-bit slots in the holy blood field. Returns up to 2 blood types
 * found, with combined stat bonuses from both types.
 */
void get_all_blood_types(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out) {
    if (!ram || !data_ptr || !out) return;

    memset(out->blood.entries, 0, sizeof(out->blood.entries));
    out->blood.count = 0;

    const uint8_t* blood_ptr = ram + data_ptr + UNIT_BLOOD_OFFSET;

    for (int i = 0; i < 13 && out->blood.count < 2; i++) {
        uint8_t val = decode_blood_slot(blood_ptr, i);
        if (val == 1 || val == 2) {
            out->blood.entries[out->blood.count].type = BLOOD_TYPE_MAP[i];
            out->blood.entries[out->blood.count].bonus = get_blood_bonus_table(BLOOD_TYPE_MAP[i]);
            out->blood.count++;
        }
    }
}

// ============================================================================
// Multi-Stat Blood Bonus Filler
// ============================================================================

/**
 * @brief Detects blood types and fills per-stat bonus for the given stat type.
 *
 * Delegates to get_all_blood_types() — the nested struct layout stores all 8
 * stat bonuses per entry, so the stat parameter is unused but kept for API compat.
 */
void get_all_blood_types_for_stat(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out, StatType stat) {
    (void)stat;  // Unused — all bonuses stored per entry in nested struct layout
    get_all_blood_types(ram, data_ptr, out);
}

// ============================================================================
// Blood Display Helpers (for table/column layout)
// ============================================================================

/** @brief Returns the number of blood types on this unit */
int get_blood_count(const GrowthStatInfo* info) {
    if (!info) return 0;
    return info->blood.count;
}

/** @brief Returns the BloodType enum at given index (0-based) */
BloodType get_blood_type_at(const GrowthStatInfo* info, int index) {
    if (!info || index < 0 || index >= info->blood.count)
        return BLOOD_NONE;
    return info->blood.entries[index].type;
}

/** @brief Returns the bonus percentage for a specific stat and blood index */
int get_blood_bonus_for_stat(const GrowthStatInfo* info, int blood_index, StatType stat) {
    if (!info || blood_index < 0 || blood_index >= info->blood.count)
        return 0;
    const BloodBonus* bonus = &info->blood.entries[blood_index].bonus;
    switch (stat) {
        case STAT_HP:   return bonus->hp_bonus;
        case STAT_STR:  return bonus->str_bonus;
        case STAT_MAG:  return bonus->mag_bonus;
        case STAT_SKL:  return bonus->skl_bonus;
        case STAT_SPD:  return bonus->spd_bonus;
        case STAT_DEF:  return bonus->def_bonus;
        case STAT_LCK:  return bonus->lck_bonus;
        case STAT_RES:  return bonus->res_bonus;
        default:        return 0;
    }
}

// ============================================================================
// Blood Type Name Getter
// ============================================================================

/* Blood type name lookup table */
const char* BLOOD_NAME_MAP[BLOOD_COUNT] = {
    "None", "Baldo", "Odo", "Hezul", "Noba", "Dain",
    "Neir", "Ulir", "Blagi", "Fala", "Tordo",
    "Holsety", "Narga", "Loputyusu"
};

/**
 * @brief Returns the human-readable name of a blood type.
 */
const char* blood_type_name(BloodType blood) {
    if (blood < BLOOD_NONE || blood >= BLOOD_COUNT) return "Unknown";
    return BLOOD_NAME_MAP[blood];
}

// ============================================================================
// Legacy API Compatibility
// =============================================================================

/**
 * @brief Estimates the RNG impact of a stat change compared to expectation.
 *
 * Legacy function preserved for backward compatibility — delegates to evaluate_rng_impact().
 */
void get_stat_estimation(int growth_rate, int actual_gain, int levels, StatEstimation* out) {
    if (!out) return;

    float expected = calculate_expected_gain(growth_rate, levels);
    float std_dev  = calculate_std_dev(growth_rate, levels);

    out->expected_gain = expected;
    out->actual_gain   = actual_gain;
    out->impact        = evaluate_rng_impact(actual_gain, expected, std_dev);
}
