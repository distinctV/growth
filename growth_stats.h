/**
 * @file growth_stats.h
 * @brief Public API for unit growth rate extraction, expected value analysis, and display formatting.
 */

#ifndef GROWTH_STATS_H
#define GROWTH_STATS_H

#include <stdint.h>

/* ============================================================================
 * Type Aliases (SNES 32-bit pointer / handle)
 * ============================================================================ */
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

/* ============================================================================
 * Stat Types
 * ============================================================================ */
typedef enum {
    STAT_HP   = 0,
    STAT_STR  = 1,
    STAT_MAG  = 2,
    STAT_SKL  = 3,
    STAT_SPD  = 4,
    STAT_DEF  = 5,
    STAT_RES  = 6,
    STAT_LCK  = 7,
    STAT_COUNT
} StatType;

/* ============================================================================
 * Blood Types (13 holy bloods + NONE)
 * ============================================================================ */
typedef enum {
    BLOOD_NONE          = 0,
    BLOOD_BALDO         = 1,
    BLOOD_ODO           = 2,
    BLOOD_HEZUL         = 3,
    BLOOD_NOBA          = 4,
    BLOOD_DAIN          = 5,
    BLOOD_NEIR          = 6,
    BLOOD_ULIR          = 7,
    BLOOD_BLAGI         = 8,
    BLOOD_FALA          = 9,
    BLOOD_TORDO         = 10,
    BLOOD_HOLSETY       = 11,
    BLOOD_NARGA         = 12,
    BLOOD_LOPUTOUSU     = 13,
    BLOOD_COUNT
} BloodType;

/* ============================================================================
 * RNG Impact (Blessed / Cursed / Neutral)
 * ============================================================================ */
typedef enum {
    RNG_BLESSED = 1,
    RNG_CURSED  = -1,
    RNG_NEUTRAL = 0
} RngImpact;

/* ============================================================================
 * Blood Bonus Structure (per-stat percentage bonuses)
 * ============================================================================ */
typedef struct {
    int hp_bonus;
    int str_bonus;
    int mag_bonus;
    int skl_bonus;
    int spd_bonus;
    int def_bonus;
    int lck_bonus;
    int res_bonus;
} BloodBonus;

/* ============================================================================
 * Growth Stat Info (per-stat output) - REFACTORED INTO NESTED STRUCTS
 * ============================================================================ */

typedef struct {
    int base;           /* ROM class base growth rate for this stat */
    int personal;       /* total - class_base (actual contribution) */
    int total;          /* Unit's total growth rate from ROM record */
} StatGrowthData;

typedef struct {
    float mean;         /* Mean gain per level-up (p = growth/100) */
    float std_dev;      /* Std dev per level-up sqrt(p*(1-p)) */
    int   level;        /* Unit's current level */
} StatExpectation;

typedef struct {
    BloodType type;
    BloodBonus bonus;
} BloodEntry;

typedef struct {
    BloodEntry entries[2];
    int count;
} BloodInfo;

// This is what the user's existing code expects (one per stat)
typedef struct {
    StatGrowthData growth;
    StatExpectation expectation;
    BloodInfo blood; // Note: In FE4, blood is unit-wide, not stat-specific.
                     // But for a "stat report", we include it.
} GrowthStatInfo;

/* ============================================================================
 * Stat Display Line (formatted output)
 * ============================================================================ */
typedef struct {
    char   text[128];
    RngImpact impact;
} StatDisplayLine;

/* ============================================================================
 * Legacy Estimation Output
 * ============================================================================ */
typedef struct {
    float     expected_gain;
    int       actual_gain;
    RngImpact impact;
} StatEstimation;

/* ============================================================================
 * Layer 1: Memory Access Primitives
 * ============================================================================ */
uint8  read_u8(const uint8_t* r, uint32_t a);
uint16 read_u16(const uint8_t* r, uint32_t a);
uint32 resolve_ram_ptr(const uint8_t* r, uint32_t a);

/* ============================================================================
 * Layer 2: Data Accessors (Unit / Class / ROM)
 * ============================================================================ */
uint32 get_unit_data_ptr(const uint8_t* ram, uint32_t handle);
uint8  get_unit_class_id(const uint8_t* ram, uint32_t data_ptr);
int    get_unit_level(const uint8_t* ram, uint32_t data_ptr);
uint32 get_unit_record_ptr(const uint8_t* r, uint32_t h);
uint32 get_class_record_ptr(uint8_t class_id);

int read_rom_class_growth(const uint8_t* rom, uint32_t class_record_ptr, StatType stat);
int read_rom_total_growth(const uint8_t* rom, uint32_t record_ptr, StatType stat);

/* Unit type / gender / heir helpers */
uint8  get_unit_type(const uint8_t* ram, uint32_t handle);
int    get_unit_gender(const uint8_t* ram, uint32_t data_ptr);
int    get_child_heir_flag(const uint8_t* ram, uint32_t data_ptr);

/* Blood type helpers */
BloodType blood_type_from_byte(uint8_t byte_val);
uint8     blood_type_to_byte(BloodType blood);
BloodType get_unit_blood_type(const uint8_t* ram, uint32_t data_ptr);

/* Parent handle accessors (Generation Inheritance) */
uint32 get_same_gender_parent_handle(const uint8_t* ram, uint32_t data_ptr);
uint32 get_secondary_parent_handle(const uint8_t* ram, uint32_t data_ptr);

/* ============================================================================
 * Layer 3: Business Logic / Engine
 * ============================================================================ */
float   calculate_expected_gain(int growth_rate, int levels);
float   calculate_std_dev(int growth_rate, int levels);
RngImpact evaluate_rng_impact(int actual_gain, float expected_gain, float std_dev);

BloodBonus get_blood_bonus_table(BloodType blood);
int        calculate_blood_bonus_total(BloodType blood);
int        calculate_inherited_growth_rate(int same_gender_base_growth,
                                           int other_gender_base_growth,
                                           BloodType child_blood);
BloodType  determine_child_blood_type(BloodType same_gender_parent_blood,
                                      BloodType other_gender_parent_blood,
                                      int is_same_gender_child);

/* Type-specific computation dispatchers */
void compute_fixed_stats(const uint8_t* ram, const uint8_t* rom, int romLen,
                         uint32_t handle, StatType stat, GrowthStatInfo* out);
void compute_inherited_child_stats(const uint8_t* ram, const uint8_t* rom, int romLen,
                                   uint32_t handle, StatType stat, GrowthStatInfo* out);
void compute_class_based_stats(const uint8_t* ram, const uint8_t* rom, int romLen,
                               uint32_t handle, StatType stat, GrowthStatInfo* out);

/* ============================================================================
 * Layer 4: Aggregation / Display Service
 * ============================================================================ */
const char* stat_label(StatType s);
void format_stat_display_line(StatType stat, const GrowthStatInfo* info, StatDisplayLine* out);
void format_all_stats_display(const GrowthStatInfo* growths, StatDisplayLine* out_lines);

/* Column-format for ImGui table rendering (menu integration) */
typedef struct {
    char   stat_name[16];
    int    class_base_growth;
    int    personal_modifier;
    int    total_gain;
    int    total_growth_rate;  /* includes blood bonuses */
    int    class_growth_rate;
    int    blood_count;
    int    blood_bonus[2][STAT_COUNT];  /* per-blood, per-stat bonus */
} StatDisplayColumn;
void format_all_stats_columns(const GrowthStatInfo* growths, StatDisplayColumn* out_cols);

/* Multi-blood detection */
void get_all_blood_types(const uint8_t* ram, uint32_t data_ptr, GrowthStatInfo* out);
void get_all_blood_types_for_stat(const uint8_t* ram, uint32_t data_ptr,
                                  GrowthStatInfo* out, StatType stat);

/* Blood name getter */
const char* blood_type_name(BloodType blood);

/* Blood display helpers — extract flat arrays for table formatting */
int  get_blood_count(const GrowthStatInfo* info);
BloodType get_blood_type_at(const GrowthStatInfo* info, int index);
int   get_blood_bonus_for_stat(const GrowthStatInfo* info, int blood_index, StatType stat);

/* Main entry point for a single stat */
void get_unit_growth_stat(const uint8_t* ram, const uint8_t* rom, int romLen,
                          uint32_t handle, StatType stat, GrowthStatInfo* out);

/* All 7 stats at once */
void get_unit_all_growths(const uint8_t* ram, const uint8_t* rom, int romLen,
                          uint32_t handle, GrowthStatInfo* out);

/* ============================================================================
 * Constants & Lookup Tables (Moved from .c for cross-module access)
 * ============================================================================ */

/* ROM class table layout */
extern const uint32_t CLASS_TABLE_OFFSET;
extern const int      CLASS_RECORD_SIZE;

/* Class growth offset per stat index (HP at +8, others at +9..+13, LCK at +15) */
#define CLASS_HP_GROWTH_OFFSET  8
#define CLASS_STAT_GROWTH_OFFSET 9
extern const int CLASS_GROWTH_OFFSET[STAT_COUNT];

/* Unit ROM record layout */
extern const int      UNIT_RECORD_GROWTH_OFFSET;
extern const int      UNIT_RECORD_MAX_SIZE;

/* Unit growth offset per stat index (HP at +20, others at +21..+27) */
extern const int UNIT_GROWTH_OFFSET[STAT_COUNT];

/* Unit RAM data block offsets (Type 0/2 fixed units) */
extern const int      UNIT_CLASS_ID_OFFSET;
extern const int      UNIT_LEVEL_OFFSET;
extern const int      UNIT_BLOOD_OFFSET;

/* Type 1 child inline data offsets */
extern const int TYPE1_CLASS_OFFSET;
extern const int TYPE1_LEVEL_OFFSET;
extern const int TYPE1_GENDER_OFFSET;
extern const int TYPE1_HEIR_OFFSET;

/* Parent handle offsets in Type 1 data block */
extern const int PARENT_SAME_GENDER_OFFSET;
extern const int PARENT_SECONDARY_OFFSET;

/* Type 3 class-based inline data offsets */
extern const int TYPE3_CLASS_OFFSET;
extern const int TYPE3_LEVEL_OFFSET;

/* Class record base stat offsets (Type 3) */
extern const int CLASS_HP_BASE_OFFSET;
extern const int CLASS_STAT_BASE_OFFSET;

/* SNES RAM bank values */
extern const uint8_t RAM_BANK_LOW;
extern const uint8_t RAM_BANK_HIGH;

/* Handle range validation */
extern const uint32_t UNIT_HANDLE_MIN;
extern const uint32_t UNIT_HANDLE_MAX;

/* Class ID mask (upper bits are flags) */
extern const uint8_t CLASS_ID_MASK;

/* Unit type byte values */
extern const uint8_t UNIT_TYPE_PLAYABLE;
extern const uint8_t UNIT_TYPE_CHILD;
extern const uint8_t UNIT_TYPE_NPC_BOSS;
extern const uint8_t UNIT_TYPE_CLASS_BASED;

/* Stat label lookup table (replaces switch) */
extern const char* STAT_LABELS[STAT_COUNT];

/* RNG impact string labels */
extern const char* IMPACT_BLESSED_STR;
extern const char* IMPACT_CURSED_STR;
extern const char* IMPACT_NEUTRAL_STR;

/* RNG sigma thresholds */
extern const float BLESSED_SIGMA_THRESHOLD;
extern const float CURSED_SIGMA_THRESHOLD;

/* Blood type slot mapping (index 0-12 maps to blood enum) */
extern const BloodType BLOOD_TYPE_MAP[13];

/* Blood bonus lookup table indexed by BloodType enum */
extern const BloodBonus BLOOD_BONUS_MAP[BLOOD_COUNT];

/* Blood type name lookup table */
extern const char* BLOOD_NAME_MAP[BLOOD_COUNT];

/* ============================================================================
 * Legacy API Compatibility
 * ============================================================================ */
void get_stat_estimation(int growth_rate, int actual_gain, int levels, StatEstimation* out);

#endif /* GROWTH_STATS_H */
