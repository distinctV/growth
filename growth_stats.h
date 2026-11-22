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
 * @brief Information about a unit's growth rates for a specific stat.
 * 
 * This structure is designed to help players analyze unit potential by 
 * breaking down the total growth rate into its components.
 */
typedef struct {
    int class_base_growth;   // The base growth rate provided by the current class (from ROM).
    int personal_modifier;   // The additional growth modifier from personal stats or blood (Total - Class).
    int total_growth_rate;   // The actual final growth rate used for level-ups (from Unit Record in ROM).
} GrowthStatInfo;

/**
 * @brief Retrieves detailed growth information for a unit.
 * 
 * @param ram Pointer to game RAM.
 * @param rom Pointer to game ROM.
 * @param romLen Length of the ROM in bytes.
 * @param handle The unit's handle (RAM address).
 * @param stat The type of stat to retrieve.
 * @param out Pointer to a GrowthStatInfo struct to fill.
 */
void get_unit_growth_stat(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, StatType stat, GrowthStatInfo* out);
void get_unit_all_growths(const uint8_t* ram, const uint8_t* rom, int romLen, uint32_t handle, GrowthStatInfo* out);

#endif // FE4_GROWTH_STATS_H
