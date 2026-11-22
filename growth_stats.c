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

// --- Constants for RNG Analysis ---
static const float BLESSED_SIGMA_THRESHOLD = 1.0f;   // Blessed if > 1 sigma above mean
static const float CURSED_SIGMA_THRESHOLD = -1.0f;   // Cursed if < 1 sigma below mean

// ============================================================================
// Layer 1: Memory Access (The "Drivers")
// ============================================================================

/**
 * @brief Resolves a 24-bit pointer from SNES RAM (banks $7E and $7F).
 *
 * SNES uses banked memory: $7E for low 64KB, $7F for high 64KB.
 * Returns 0 if bank is invalid (pointer resolution failure).
 */
uint32_t resolve_ram_ptr(const uint8_t* r, uint32_t a) {
    uint8_t bank = r[a + 2];
    if (bank != 0x7E && bank != 0x7F) return 0;
    uint16_t offset = read_u16(r, a);
    return (uint32_t)(bank - 0x7E) * 0x10000 + offset;
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
    return read_u8(ram, data_ptr + 9);
}

/**
 * @brief Gets the unit's current level from RAM data.
 *
 * Level is stored at offset +10 within the unit's data block.
 */
int get_unit_level(const uint8_t* ram, uint32_t data_ptr) {
    return read_u8(ram, data_ptr + 10);
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
    if (stat == STAT_HP) {
        return rom[class_record_ptr + CLASS_HP_GROWTH_OFFSET];  // offset +8
    } else if (stat == STAT_LCK) {
        return rom[class_record_ptr + 15];  // LCK at offset +15
    }
    // STR=1->+9, MAG=2->+10, SKL=3->+11, SPD=4->+12, DEF=5->+13
    return rom[class_record_ptr + CLASS_STAT_GROWTH_OFFSET + stat - 1];
}

/**
 * @brief Gets the unit's record pointer in ROM from its handle.
 *
 * For Type 0/2 units: pointer is derived from class_id (at offset +9) and record address (offset +7).
 * Returns 0 for unsupported types or invalid handles.
 */
uint32_t get_unit_record_ptr(const uint8_t* r, uint32_t h) {
    if (h < 0x2000 || h > 0xFFE0) return 0;

    uint8_t type = read_u8(r, h);
    // Type 1 (child) and Type 3 records are handled differently in the game engine.
    if (type != 0 && type != 2) return 0;

    // Record pointer: class_id at offset +9, low 16-bit address at offset +7
    uint8_t class_byte = read_u8(r, h + 9);
    uint16_t record_offset = read_u16(r, h + 7);
    return ((uint32_t)(class_byte & 0x3F) << 16) | record_offset;
}

/**
 * @brief Reads the total growth rate from a unit's ROM record for a specific stat.
 *
 * HP growth is at UNIT_RECORD_GROWTH_OFFSET, other stats are at offset +stat.
 */
int read_rom_total_growth(const uint8_t* rom, uint32_t record_ptr, StatType stat) {
    if (stat == STAT_HP) {
        return rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET];
    }
    return rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + stat];
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
    switch (byte_val) {
        case 0: return BLOOD_NONE;
        case 1: return BLOOD_BALDO;
        case 2: return BLOOD_ODO;
        case 3: return BLOOD_HEZUL;
        case 4: return BLOOD_NOBA;
        case 5: return BLOOD_DAIN;
        case 6: return BLOOD_NEIR;
        case 7: return BLOOD_ULIR;
        case 8: return BLOOD_BLAGI;
        case 9: return BLOOD_FALA;
        case 10: return BLOOD_TORDO;
        case 11: return BLOOD_HOLSETY;
        case 12: return BLOOD_NARGA;
        case 13: return BLOOD_LOPUTOUSU;
        default: return BLOOD_NONE;  // Invalid value
    }
}

/**
 * @brief Converts BloodType enum back to raw byte for storage.
 */
uint8_t blood_type_to_byte(BloodType blood) {
    switch (blood) {
        case BLOOD_NONE: return 0;
        case BLOOD_BALDO: return 1;
        case BLOOD_ODO: return 2;
        case BLOOD_HEZUL: return 3;
        case BLOOD_NOBA: return 4;
        case BLOOD_DAIN: return 5;
        case BLOOD_NEIR: return 6;
        case BLOOD_ULIR: return 7;
        case BLOOD_BLAGI: return 8;
        case BLOOD_FALA: return 9;
        case BLOOD_TORDO: return 10;
        case BLOOD_HOLSETY: return 11;
        case BLOOD_NARGA: return 12;
        case BLOOD_LOPUTOUSU: return 13;
        default: return 0;  // Invalid
    }
}

/**
 * @brief Reads blood type from unit's RAM data block.
 *
 * Blood type is stored at offset +31 within the unit's data block (13 x 2-bit slots).
 * Returns BLOOD_NONE if invalid or no blood detected.
 */
BloodType get_unit_blood_type(const uint8_t* ram, uint32_t data_ptr) {
    if (!ram) return BLOOD_NONE;

    // Holy blood at offset UNIT_BLOOD_OFFSET (13 x 2-bit slots, LSB first)
    // Each slot is 2 bits: 0=none, 1=minor, 2=major
    // Slots map to blood types: Baldo(0), Odo(1), Hezul(2), etc.

    uint32_t blood_ptr = data_ptr + UNIT_BLOOD_OFFSET;
    uint8_t byte0 = read_u8(ram, blood_ptr);
    uint8_t byte1 = read_u8(ram, blood_ptr + 1);
    uint8_t byte2 = read_u8(ram, blood_ptr + 2);

    // Decode 13 slots (26 bits total)
    for (int i = 0; i < 13; i++) {
        int slot_offset = i * 2;
        uint8_t val;
        if (slot_offset < 8) {
            val = (byte0 >> slot_offset) & 0x03;
        } else if (slot_offset < 16) {
            val = (byte1 >> (slot_offset - 8)) & 0x03;
        } else {
            val = (byte2 >> (slot_offset - 16)) & 0x03;
        }

        if (val == 1 || val == 2) {
            // Found a blood type - map slot index to BloodType enum
            switch (i) {
                case 0: return BLOOD_BALDO;
                case 1: return BLOOD_ODO;
                case 2: return BLOOD_HEZUL;
                case 3: return BLOOD_NOBA;
                case 4: return BLOOD_DAIN;
                case 5: return BLOOD_NEIR;
                case 6: return BLOOD_ULIR;
                case 7: return BLOOD_BLAGI;
                case 8: return BLOOD_FALA;
                case 9: return BLOOD_TORDO;
                case 10: return BLOOD_HOLSETY;
                case 11: return BLOOD_NARGA;
                case 12: return BLOOD_LOPUTOUSU;
                default: return BLOOD_NONE;
            }
        }
    }

    return BLOOD_NONE;  // No blood detected
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

/**
 * @brief Returns the blood bonus table entry for a given blood type.
 *
 * Based on holy_blood_info.md from Serenes Forest wiki.
 */
BloodBonus get_blood_bonus_table(BloodType blood) {
    BloodBonus bonus = {0, 0, 0, 0, 0, 0, 0, 0};

    switch (blood) {
        case BLOOD_BALDO:
            bonus.hp_bonus = 20; bonus.str_bonus = 10;
            bonus.skl_bonus = 10; bonus.lck_bonus = 10; break;
        case BLOOD_ODO:
            bonus.hp_bonus = 20; bonus.skl_bonus = 30; break;
        case BLOOD_HEZUL:
            bonus.hp_bonus = 20; bonus.str_bonus = 30; break;
        case BLOOD_NOBA:
            bonus.hp_bonus = 20; bonus.str_bonus = 10;
            bonus.spd_bonus = 10; bonus.def_bonus = 10; break;
        case BLOOD_DAIN:
            bonus.hp_bonus = 20; bonus.spd_bonus = 30; break;
        case BLOOD_NEIR:
            bonus.hp_bonus = 20; bonus.res_bonus = 30; break;
        case BLOOD_ULIR:
            bonus.hp_bonus = 20; bonus.lck_bonus = 30; break;
        case BLOOD_BLAGI:
            bonus.hp_bonus = 10; bonus.mag_bonus = 10;
            bonus.lck_bonus = 10; bonus.res_bonus = 20; break;
        case BLOOD_FALA:
            bonus.hp_bonus = 20; bonus.mag_bonus = 30; break;
        case BLOOD_TORDO:
            bonus.hp_bonus = 20; bonus.mag_bonus = 30; break;
        case BLOOD_HOLSETY:
            bonus.hp_bonus = 20; bonus.spd_bonus = 30; break;
        case BLOOD_NARGA:
            bonus.hp_bonus = 10; bonus.mag_bonus = 20; break;
        case BLOOD_LOPUTOUSU:
            bonus.hp_bonus = 10; bonus.mag_bonus = 20; break;
        default:
            // No blood or unknown type
            break;
    }

    return bonus;
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
    switch (s) {
        case STAT_HP:  return "HP";
        case STAT_STR: return "SP";
        case STAT_MAG: return "MT";
        case STAT_SKL: return "LK";
        case STAT_SPD: return "SD";
        case STAT_DEF: return "DF";
        case STAT_RES: return "--";  // Reserved/unused stat
        case STAT_LCK: return "LK";
        default:       return "?";
    }
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

    // Initialize output structure
    memset(out, 0, sizeof(GrowthStatInfo));

    uint8_t type = get_unit_type(ram, handle);
    switch (type) {
        case 0:
        case 2:
            compute_fixed_stats(ram, rom, romLen, handle, stat, out);
            break;
        case 1:
            compute_inherited_child_stats(ram, rom, romLen, handle, stat, out);
            break;
        case 3:
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
    out->current_level = level;
    out->expected_gain_per_level = p;  // Mean per level-up
    out->std_dev_per_level = sqrtf(p * (1.0f - p));  // Std dev per level-up

    // Blood info populated separately by get_all_blood_types_for_stat()

    // Store results
    out->class_base_growth = class_base_growth;
    out->personal_modifier = personal_modifier;
    out->total_growth_rate = total_growth_rate;
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
    BloodType child_blood = BLOOD_NONE;
    
    // Heir flag indicates which parent's blood to use (1=father, 0=mother)
    // Without parent data available inline, we default to no blood
    if (heir_flag == 1) {
        // Father heir - would need father lookup to determine blood type
    } else {
        // Mother heir - would need mother lookup to determine blood type
    }

    // Compute personal modifier (inherited - class base)
    int personal_modifier = inherited_growth - class_base_growth;

    // Calculate expected value statistics based on inherited growth rate
    float p = (float)inherited_growth / 100.0f;
    out->current_level = level;
    out->expected_gain_per_level = p;
    out->std_dev_per_level = sqrtf(p * (1.0f - p));

    // Store results
    out->class_base_growth = class_base_growth;
    out->personal_modifier = personal_modifier;
    out->total_growth_rate = inherited_growth;
    // Blood info populated separately by get_all_blood_types_for_stat()
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
    out->current_level = level;
    out->expected_gain_per_level = p;
    out->std_dev_per_level = sqrtf(p * (1.0f - p));

    // Store results
    out->class_base_growth = class_base_stat;  // Using base stat as display value
    out->personal_modifier = personal_modifier;
    out->total_growth_rate = total_stat;
    // Class-based units have no blood — zero arrays and count
    out->blood_type_count = 0;
    for (int b = 0; b < 2; b++) {
        out->blood_types[b] = BLOOD_NONE;
    }
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
    float expected_gain = (float)info->class_base_growth * info->current_level / 100.0f;
    float total_std_dev = sqrtf((float)info->current_level * info->expected_gain_per_level * (1.0f - info->expected_gain_per_level));
    int actual_gain = info->personal_modifier;

    if ((float)actual_gain > expected_gain + BLESSED_SIGMA_THRESHOLD * total_std_dev) {
        impact = RNG_BLESSED;
    } else if ((float)actual_gain < expected_gain + CURSED_SIGMA_THRESHOLD * total_std_dev) {
        impact = RNG_CURSED;
    } else {
        impact = RNG_NEUTRAL;
    }

    // Format: "STAT base+personal=total gr:N [X]"
    const char* impact_char;
    switch (impact) {
        case RNG_BLESSED: impact_char = "[B]"; break;
        case RNG_CURSED:  impact_char = "[C]"; break;
        default:          impact_char = "[N]"; break;
    }

    snprintf(out->text, sizeof out->text, "%s %2d+%d=%2d gr:%d %s",
             label,
             info->class_base_growth,
             info->personal_modifier,
             info->total_growth_rate,
             info->total_growth_rate,
             impact_char);

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
// Multi-Blood Type Detection
// ============================================================================

/**
 * @brief Detects all blood types present on a unit (up to 2) and calculates per-stat bonuses.
 *
 * Scans all 13 x 2-bit slots in the holy blood field. Returns up to 2 blood types
 * found, with combined stat bonuses from both types.
 */
void get_all_blood_types(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out) {
    if (!ram || !data_ptr || !out) return;

    // Zero all per-blood-type bonus arrays
    for (int b = 0; b < 2; b++) {
        out->blood_bonus_hp[b] = 0;
        out->blood_bonus_str[b] = 0;
        out->blood_bonus_mag[b] = 0;
        out->blood_bonus_skl[b] = 0;
        out->blood_bonus_spd[b] = 0;
        out->blood_bonus_def[b] = 0;
        out->blood_bonus_lck[b] = 0;
        out->blood_bonus_res[b] = 0;
    }
    out->blood_type_count = 0;

    // Holy blood at offset UNIT_BLOOD_OFFSET (13 x 2-bit slots, LSB first)
    uint32_t blood_ptr = data_ptr + UNIT_BLOOD_OFFSET;
    uint8_t byte0 = read_u8(ram, blood_ptr);
    uint8_t byte1 = read_u8(ram, blood_ptr + 1);
    uint8_t byte2 = read_u8(ram, blood_ptr + 2);

    // Decode all 13 slots (26 bits total)
    for (int i = 0; i < 13 && out->blood_type_count < 2; i++) {
        int slot_offset = i * 2;
        uint8_t val;
        if (slot_offset < 8) {
            val = (byte0 >> slot_offset) & 0x03;
        } else if (slot_offset < 16) {
            val = (byte1 >> (slot_offset - 8)) & 0x03;
        } else {
            val = (byte2 >> (slot_offset - 16)) & 0x03;
        }

        if (val == 1 || val == 2) {
            // Found a blood type - map slot index to BloodType enum
            switch (i) {
                case 0: out->blood_types[out->blood_type_count++] = BLOOD_BALDO; break;
                case 1: out->blood_types[out->blood_type_count++] = BLOOD_ODO; break;
                case 2: out->blood_types[out->blood_type_count++] = BLOOD_HEZUL; break;
                case 3: out->blood_types[out->blood_type_count++] = BLOOD_NOBA; break;
                case 4: out->blood_types[out->blood_type_count++] = BLOOD_DAIN; break;
                case 5: out->blood_types[out->blood_type_count++] = BLOOD_NEIR; break;
                case 6: out->blood_types[out->blood_type_count++] = BLOOD_ULIR; break;
                case 7: out->blood_types[out->blood_type_count++] = BLOOD_BLAGI; break;
                case 8: out->blood_types[out->blood_type_count++] = BLOOD_FALA; break;
                case 9: out->blood_types[out->blood_type_count++] = BLOOD_TORDO; break;
                case 10: out->blood_types[out->blood_type_count++] = BLOOD_HOLSETY; break;
                case 11: out->blood_types[out->blood_type_count++] = BLOOD_NARGA; break;
                case 12: out->blood_types[out->blood_type_count++] = BLOOD_LOPUTOUSU; break;
                default: break;
            }
        }
    }

    // Store per-blood-type stat bonuses (each blood type's contribution separately)
    for (int b = 0; b < out->blood_type_count; b++) {
        BloodBonus bonus = get_blood_bonus_table(out->blood_types[b]);
        out->blood_bonus_hp[b] = bonus.hp_bonus;
        out->blood_bonus_str[b] = bonus.str_bonus;
        out->blood_bonus_mag[b] = bonus.mag_bonus;
        out->blood_bonus_skl[b] = bonus.skl_bonus;
        out->blood_bonus_spd[b] = bonus.spd_bonus;
        out->blood_bonus_def[b] = bonus.def_bonus;
        out->blood_bonus_lck[b] = bonus.lck_bonus;
        out->blood_bonus_res[b] = bonus.res_bonus;
    }
}

// ============================================================================
// Per-Stat Blood Bonus Filler
// ============================================================================

/**
 * @brief Detects blood types and fills per-stat bonus for the given stat type.
 *
 * Scans all 13 x 2-bit slots, stores up to 2 blood types,
 * then populates the correct per-stat bonus array index based on StatType.
 */
void get_all_blood_types_for_stat(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out, StatType stat) {
    if (!ram || !data_ptr || !out) return;

    // Zero all per-blood-type bonus arrays
    for (int b = 0; b < 2; b++) {
        out->blood_bonus_hp[b] = 0;
        out->blood_bonus_str[b] = 0;
        out->blood_bonus_mag[b] = 0;
        out->blood_bonus_skl[b] = 0;
        out->blood_bonus_spd[b] = 0;
        out->blood_bonus_def[b] = 0;
        out->blood_bonus_lck[b] = 0;
        out->blood_bonus_res[b] = 0;
    }
    out->blood_type_count = 0;

    // Holy blood at offset UNIT_BLOOD_OFFSET (13 x 2-bit slots, LSB first)
    uint32_t blood_ptr = data_ptr + UNIT_BLOOD_OFFSET;
    uint8_t byte0 = read_u8(ram, blood_ptr);
    uint8_t byte1 = read_u8(ram, blood_ptr + 1);
    uint8_t byte2 = read_u8(ram, blood_ptr + 2);

    // Decode all 13 slots (26 bits total)
    for (int i = 0; i < 13 && out->blood_type_count < 2; i++) {
        int slot_offset = i * 2;
        uint8_t val;
        if (slot_offset < 8) {
            val = (byte0 >> slot_offset) & 0x03;
        } else if (slot_offset < 16) {
            val = (byte1 >> (slot_offset - 8)) & 0x03;
        } else {
            val = (byte2 >> (slot_offset - 16)) & 0x03;
        }

        if (val == 1 || val == 2) {
            // Found a blood type - map slot index to BloodType enum
            switch (i) {
                case 0: out->blood_types[out->blood_type_count++] = BLOOD_BALDO; break;
                case 1: out->blood_types[out->blood_type_count++] = BLOOD_ODO; break;
                case 2: out->blood_types[out->blood_type_count++] = BLOOD_HEZUL; break;
                case 3: out->blood_types[out->blood_type_count++] = BLOOD_NOBA; break;
                case 4: out->blood_types[out->blood_type_count++] = BLOOD_DAIN; break;
                case 5: out->blood_types[out->blood_type_count++] = BLOOD_NEIR; break;
                case 6: out->blood_types[out->blood_type_count++] = BLOOD_ULIR; break;
                case 7: out->blood_types[out->blood_type_count++] = BLOOD_BLAGI; break;
                case 8: out->blood_types[out->blood_type_count++] = BLOOD_FALA; break;
                case 9: out->blood_types[out->blood_type_count++] = BLOOD_TORDO; break;
                case 10: out->blood_types[out->blood_type_count++] = BLOOD_HOLSETY; break;
                case 11: out->blood_types[out->blood_type_count++] = BLOOD_NARGA; break;
                case 12: out->blood_types[out->blood_type_count++] = BLOOD_LOPUTOUSU; break;
                default: break;
            }
        }
    }

    // Fill the correct per-stat bonus array based on StatType
    for (int b = 0; b < out->blood_type_count; b++) {
        BloodBonus bonus = get_blood_bonus_table(out->blood_types[b]);
        switch (stat) {
            case STAT_HP:   out->blood_bonus_hp[b] = bonus.hp_bonus; break;
            case STAT_STR:  out->blood_bonus_str[b] = bonus.str_bonus; break;
            case STAT_MAG:  out->blood_bonus_mag[b] = bonus.mag_bonus; break;
            case STAT_SKL:  out->blood_bonus_skl[b] = bonus.skl_bonus; break;
            case STAT_SPD:  out->blood_bonus_spd[b] = bonus.spd_bonus; break;
            case STAT_DEF:  out->blood_bonus_def[b] = bonus.def_bonus; break;
            case STAT_RES:  out->blood_bonus_res[b] = bonus.res_bonus; break;
            case STAT_LCK:  out->blood_bonus_lck[b] = bonus.lck_bonus; break;
            default: break;
        }
    }
}

// ============================================================================
// Blood Type Name Getter
// ============================================================================

/**
 * @brief Returns the human-readable name of a blood type.
 */
const char* blood_type_name(BloodType blood) {
    switch (blood) {
        case BLOOD_BALDO: return "Baldo";
        case BLOOD_ODO: return "Odo";
        case BLOOD_HEZUL: return "Hezul";
        case BLOOD_NOBA: return "Noba";
        case BLOOD_DAIN: return "Dain";
        case BLOOD_NEIR: return "Neir";
        case BLOOD_ULIR: return "Ulir";
        case BLOOD_BLAGI: return "Blagi";
        case BLOOD_FALA: return "Fala";
        case BLOOD_TORDO: return "Tordo";
        case BLOOD_HOLSETY: return "Holsety";
        case BLOOD_NARGA: return "Narga";
        case BLOOD_LOPUTOUSU: return "Loputyusu";
        default: return "None";
    }
}

// ============================================================================
// Legacy API Compatibility
// =============================================================================

/**
 * @brief Estimates the RNG impact of a stat change compared to expectation.
 *
 * Legacy function preserved for backward compatibility with existing code.
 */
void get_stat_estimation(int growth_rate, int actual_gain, int levels, StatEstimation* out) {
    if (!out) return;

    float p = (float)growth_rate / 100.0f;
    float expected = calculate_expected_gain(growth_rate, levels);

    // Variance of Binomial distribution: Var(X) = n * p * (1 - p)
    float variance = (float)levels * p * (1.0f - p);
    float std_dev = sqrtf(variance);

    out->expected_gain = expected;
    out->actual_gain = actual_gain;

    // If std_dev is extremely small, we treat it as 0 to avoid issues,
    // but comparison still works.
    if (std_dev < 0.001f) {
        if ((float)actual_gain > expected + 0.5f) { // Small epsilon for float comparison
            out->impact = RNG_BLESSED;
        } else if ((float)actual_gain < expected - 0.5f) {
            out->impact = RNG_CURSED;
        } else {
            out->impact = RNG_NEUTRAL;
        }
    } else {
        if (expected + (BLESSED_SIGMA_THRESHOLD * std_dev) < (float)actual_gain) {
            out->impact = RNG_BLESSED;
        } else if (expected + (CURSED_SIGMA_THRESHOLD * std_dev) > (float)actual_gain) {
            out->impact = RNG_CURSED;
        } else {
            out->impact = RNG_NEUTRAL;
        }
    }
}
