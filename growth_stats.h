#ifndef FE4_GROWTH_STATS_H
#define FE4_GROWTH_STATS_H

#include <stdint.h>

typedef enum {
    STAT_HP,
    STAT_STR,
    STAT_MAG,
    STAT_SKL,
    STAT_SPD,
    STAT_DEF,
    STAT_RES,
    STAT_LCK,
    STAT_COUNT
} StatType;

/**
 * @brief RNG impact classification enum (forward declared for GrowthStatInfo).
 */
typedef enum {
    RNG_NEUTRAL,
    RNG_BLESSED,
    RNG_CURSED
} RngImpact;

/**
 * @brief Stat label strings for display.
 */
extern const char* stat_label(StatType s);

/**
 * @brief Information about a unit's growth rates for a specific stat.
 */
// ROM class table offsets (per ROM_MAP.md)
#define CLASS_TABLE_OFFSET 0x038FC5
#define CLASS_RECORD_SIZE 31

// Unit record sizes by type (per ROM_MAP.md)
#define TYPE_0_RECORD_SIZE 38   // Playable with fixed stats
#define TYPE_1_RECORD_SIZE 12   // Playable children (inherited)
#define TYPE_3_RECORD_SIZE 13   // Class-based units
#define UNIT_RECORD_MAX_SIZE 40 // Safe upper bound

// Unit record field offsets (Type 0/2, per ROM_MAP.md)
#define UNIT_TYPE_OFFSET 0      // Record type byte
#define UNIT_HP_OFFSET 1        // HP stat
#define UNIT_STR_OFFSET 2       // STR stat
#define UNIT_MAG_OFFSET 3       // MAG stat
#define UNIT_SKL_OFFSET 4       // SKL stat
#define UNIT_SPD_OFFSET 5       // SPD stat
#define UNIT_DEF_OFFSET 6       // DEF stat
#define UNIT_RES_OFFSET 7       // RES stat
#define UNIT_LCK_OFFSET 8       // LCK stat (absolute)
#define UNIT_CLASS_ID_OFFSET 9  // Class ID
#define UNIT_LEVEL_OFFSET 10    // Current level
#define UNIT_GROWTH_HP_OFFSET 20 // HP growth rate
#define UNIT_GROWTH_STR_OFFSET 21 // STR growth rate
#define UNIT_GROWTH_MAG_OFFSET 22 // MAG growth rate
#define UNIT_GROWTH_SKL_OFFSET 23 // SKL growth rate
#define UNIT_GROWTH_SPD_OFFSET 24 // SPD growth rate
#define UNIT_GROWTH_DEF_OFFSET 25 // DEF growth rate
#define UNIT_GROWTH_RES_OFFSET 26 // RES growth rate
#define UNIT_GROWTH_LCK_OFFSET 27 // LCK growth rate
#define UNIT_BLOOD_OFFSET 31    // Holy blood (13 × 2-bit slots)
#define UNIT_RECORD_GROWTH_OFFSET 20   // HP growth base offset in unit record
#define UNIT_NAME_ID_OFFSET 15  // Name ID (2 bytes)

// --- Type 1 child record offsets (12 bytes inline) ---
#define TYPE1_CLASS_OFFSET 1      // Class ID at offset +1
#define TYPE1_LEVEL_OFFSET 2      // Level at offset +2
#define TYPE1_GENDER_OFFSET 8     // Gender at offset +8 (0=male, 1=female)
#define TYPE1_HEIR_OFFSET 11      // Heir flag at offset +11 (1=father, 0=mother)

// --- Type 3 class-based record offsets (13 bytes inline) ---
#define TYPE3_CLASS_OFFSET 5      // Class ID at offset +5
#define TYPE3_LEVEL_OFFSET 7      // Level at offset +7

// --- Parent handle storage offsets (Type 0/2 data blocks) ---
#define PARENT_SAME_GENDER_OFFSET 20   // Same-gender parent handle at offset +20
#define PARENT_SECONDARY_OFFSET 24     // Secondary parent handle at offset +24

// --- Class record field offsets (per ROM_MAP.md) ---
#define CLASS_HP_BASE_OFFSET 1         // HP base stat at offset +1
#define CLASS_STAT_BASE_OFFSET 2       // STR/MAG/SKL/SPD/DEF/RES base stats start at offset +2
#define CLASS_HP_GROWTH_OFFSET 8       // HP growth rate at offset +8
#define CLASS_STAT_GROWTH_OFFSET 9     // STR/MAG/SKL/SPD/DEF/RES growth rates start at offset +9

/**
 * @brief Holy blood type enumeration.
 */
typedef enum {
    BLOOD_NONE = 0,
    BLOOD_BALDO,   // +20% HP, +10% STR, +10% SKL, +10% LCK
    BLOOD_ODO,     // +20% HP, +30% SKL
    BLOOD_HEZUL,   // +20% HP, +30% STR
    BLOOD_NOBA,    // +20% HP, +10% STR, +10% SPD, +10% DEF
    BLOOD_DAIN,    // +20% HP, +30% SPD
    BLOOD_NEIR,    // +20% HP, +30% RES
    BLOOD_ULIR,    // +20% HP, +30% LCK
    BLOOD_BLAGI,   // +10% HP, +10% MAG, +10% LCK, +20% RES
    BLOOD_FALA,    // +20% HP, +30% MAG
    BLOOD_TORDO,   // +20% HP, +30% MAG
    BLOOD_HOLSETY, // +20% HP, +30% SPD
    BLOOD_NARGA,   // +10% HP, +20% MAG
    BLOOD_LOPUTOUSU // +10% HP, +20% MAG
} BloodType;

/**
 * @brief Holy blood bonus for a specific stat.
 */
typedef struct {
    int hp_bonus;      // Percentage bonus to HP growth
    int str_bonus;     // Percentage bonus to STR growth
    int mag_bonus;     // Percentage bonus to MAG growth
    int skl_bonus;     // Percentage bonus to SKL growth
    int spd_bonus;     // Percentage bonus to SPD growth
    int def_bonus;     // Percentage bonus to DEF growth
    int lck_bonus;     // Percentage bonus to LCK growth
    int res_bonus;     // Percentage bonus to RES growth
} BloodBonus;

typedef struct {
    int class_base_growth;       // The base growth rate provided by the current class (from ROM).
    int personal_modifier;       // The additional growth modifier from personal stats or blood (Total - Class).
    int total_growth_rate;       // The actual final growth rate used for level-ups (from Unit Record in ROM)
    
    // Expected value calculations
    int current_level;              // Unit's current level for expected calculation
    int cached_level;               // Cached level for change detection (recalc only on level-up)
    float expected_gain_per_level;  // Mean gain per level-up (growth_rate / 100)
    float std_dev_per_level;        // Standard deviation per level-up
    
    // RNG impact classification
    RngImpact impact;               // BLESSED, CURSED, or NEUTRAL
    
    // Holy blood information (up to 2 blood types per unit)
    BloodType blood_types[2];       // Primary and secondary blood types
    int blood_type_count;           // Number of blood types present (0-2)
    int blood_bonus_hp[2];          // HP bonus per blood type index
    int blood_bonus_str[2];         // STR bonus per blood type index
    int blood_bonus_mag[2];         // MAG bonus per blood type index
    int blood_bonus_skl[2];         // SKL bonus per blood type index
    int blood_bonus_spd[2];         // SPD bonus per blood type index
    int blood_bonus_def[2];         // DEF bonus per blood type index
    int blood_bonus_lck[2];         // LCK bonus per blood type index
    int blood_bonus_res[2];         // RES bonus per blood type index
} GrowthStatInfo;

typedef struct {
    float expected_gain;
    int actual_gain;
    RngImpact impact;
} StatEstimation;

/**
 * @brief Estimates the RNG impact of a stat change compared to expectation.
 */
void get_stat_estimation(int growth_rate, int actual_gain, int levels, StatEstimation* out);

/**
 * @brief Display format for a single stat line.
 */
typedef struct {
    char text[64];           // Formatted string like "HP 12+8=20 gr:75 [B]"
    RngImpact impact;        // Classification for this stat
} StatDisplayLine;

// --- Layer 1: Memory Access (The "Drivers") ---
/** @brief Reads a single byte from RAM/ROM at address a */
static inline uint8_t read_u8(const uint8_t* r, uint32_t a) { return r[a]; }

/** @brief Reads a 16-bit little-endian value from the specified address */
static inline uint16_t read_u16(const uint8_t* r, uint32_t a) { return r[a] | r[a + 1] << 8; }

/** @brief Resolves a 24-bit pointer from SNES RAM (banks $7E and $7F) */
uint32_t resolve_ram_ptr(const uint8_t* r, uint32_t a);

// --- Layer 2: Data Accessors (The "Data Layer") ---
/** @brief Gets the unit's data pointer in RAM from its handle */
uint32_t get_unit_data_ptr(const uint8_t* ram, uint32_t handle);

/** @brief Gets the unit's class ID from RAM data */
uint8_t get_unit_class_id(const uint8_t* ram, uint32_t data_ptr);

/** @brief Calculates the ROM address of a class record from class_id */
uint32_t get_class_record_ptr(uint8_t class_id);

/** @brief Reads a base growth rate from the class table in ROM for a specific stat */
int read_rom_class_growth(const uint8_t* rom, uint32_t class_record_ptr, StatType stat);

/** @brief Gets the unit's record pointer in ROM from its handle */
uint32_t get_unit_record_ptr(const uint8_t* r, uint32_t h);

/** @brief Reads the total growth rate from a unit's ROM record for a specific stat */
int read_rom_total_growth(const uint8_t* rom, uint32_t record_ptr, StatType stat);

/** @brief Gets the unit's current level from RAM data */
int get_unit_level(const uint8_t* ram, uint32_t data_ptr);

// --- Layer 3: Business Logic (The "Engine") ---
/** @brief Calculates expected stat gain over a number of levels using binomial mean */
float calculate_expected_gain(int growth_rate, int levels);

/** @brief Calculates standard deviation for binomial distribution over levels */
float calculate_std_dev(int growth_rate, int levels);

/** @brief Evaluates RNG impact: compares actual gain to expected with sigma threshold */
RngImpact evaluate_rng_impact(int actual_gain, float expected_gain, float std_dev);

// --- Layer 4: Aggregation (The "Service Layer") ---
/** @brief Retrieves detailed growth information for a single stat */
void get_unit_growth_stat(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);

/** @brief Retrieves growth info for all 7 stats (HP, STR, MAG, SKL, SPD, DEF, LCK) */
void get_unit_all_growths(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, GrowthStatInfo* out);

/** @brief Formats a stat line for display: "STAT base+personal=total gr:N [B/C/N]" */
void format_stat_display_line(StatType stat, const GrowthStatInfo* info, StatDisplayLine* out);

/** @brief Formats all 7 stats into display lines */
void format_all_stats_display(const GrowthStatInfo* growths, StatDisplayLine* out_lines);

// --- Layer 2/3: Blood Type Helpers ---
/** @brief Maps a raw byte value to BloodType enum */
BloodType blood_type_from_byte(uint8_t byte_val);

/** @brief Converts BloodType enum back to raw byte for storage */
uint8_t blood_type_to_byte(BloodType blood);

/** @brief Returns the human-readable name of a blood type */
const char* blood_type_name(BloodType blood);

// --- Layer 2/3: Blood Type Detection ---
/** @brief Detects all blood types present on a unit (up to 2) */
void get_all_blood_types(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out);
/** @brief Detects blood types and fills per-stat bonus for the given stat type */
void get_all_blood_types_for_stat(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out, StatType stat);

// --- Layer 2: Blood Detection Accessors ---
/** @brief Reads blood type from unit's RAM data block */
BloodType get_unit_blood_type(const uint8_t* ram, uint32_t data_ptr);

// --- Layer 3: Blood Bonus Calculation ---
/** @brief Returns the blood bonus table entry for a given blood type */
BloodBonus get_blood_bonus_table(BloodType blood);

/** @brief Calculates total percentage bonus from blood type */
int calculate_blood_bonus_total(BloodType blood);

// --- Layer 2: Parent Data Accessors (Generation Inheritance) ---
/** @brief Reads parent handle from unit's RAM data block (same-gender parent at offset +20) */
uint32_t get_same_gender_parent_handle(const uint8_t* ram, uint32_t data_ptr);

/** @brief Reads secondary parent handle from unit's RAM data block (other-gender parent at offset +24) */
uint32_t get_secondary_parent_handle(const uint8_t* ram, uint32_t data_ptr);

// --- Layer 2/3: Blood Type Helpers ---
/** @brief Maps a raw byte value to BloodType enum */
BloodType blood_type_from_byte(uint8_t byte_val);

/** @brief Converts BloodType enum back to raw byte for storage */
uint8_t blood_type_to_byte(BloodType blood);

// --- Layer 3: Inheritance Calculation ---
/** @brief Calculates inherited growth rate per inheritance_rules.md formula */
int calculate_inherited_growth_rate(int same_gender_base_growth, int other_gender_base_growth, BloodType child_blood);

/** @brief Determines child's blood type based on parent blood and gender rules */
BloodType determine_child_blood_type(BloodType same_gender_parent_blood, BloodType other_gender_parent_blood, int is_same_gender_child);

// --- Layer 2: Type 1 Child Data Accessors ---
/** @brief Gets child's gender from RAM data block (offset +8) */
int get_unit_gender(const uint8_t* ram, uint32_t data_ptr);

/** @brief Gets child's heir flag (1=father blood, 0=mother blood) from RAM data block (offset +11) */
int get_child_heir_flag(const uint8_t* ram, uint32_t data_ptr);

// --- Layer 4: Type Detection and Dispatch ---
/** @brief Returns the unit record type byte (0=playable fixed, 1=inherited child, 2=npc/boss, 3=class-based) */
uint8_t get_unit_type(const uint8_t* ram, uint32_t handle);

// --- Layer 4: Type-specific Growth Computation ---
/** @brief Computes growth stats for Type 0/2 units with fixed personal stats */
void compute_fixed_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);

/** @brief Computes inherited growth stats for Type 1 children from parent data */
void compute_inherited_child_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);

/** @brief Computes stats for Type 3 class-based units (generic enemies/bosses) */
void compute_class_based_stats(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);

#endif // FE4_GROWTH_STATS_H
