/*
 * TEST SUITE: Growth Rate Tracker (Unit Tests - Isolated Responsibility)
 * ======================================================================
 * Monolithic test file covering all layers.
 */

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <math.h>
#include "../src/growth_stats.h"

// Test infrastructure
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, desc) do { \
    tests_run++; \
    if (cond) { \
        printf("  [PASS] %s\n", desc); \
        tests_passed++; \
    } else { \
        printf("  [FAIL] %s\n", desc); \
        tests_failed++; \
    } \
} while(0)

// ============================================================================
// Layer 1: Memory Access Primitives Tests (Isolated - No Mock RAM/ROM)
// ============================================================================

void test_read_u8() {
    printf("\n=== Layer 1: read_u8 ===\n");

    uint8_t buffer[256] = {0};
    buffer[0] = 0x42;
    buffer[100] = 0xFF;
    buffer[200] = 0x00;

    TEST_ASSERT(read_u8(buffer, 0) == 0x42, "read_u8 at offset 0 returns 0x42");
    TEST_ASSERT(read_u8(buffer, 100) == 0xFF, "read_u8 at offset 100 returns 0xFF");
    TEST_ASSERT(read_u8(buffer, 200) == 0x00, "read_u8 at offset 200 returns 0x00");
}

void test_read_u16() {
    printf("\n=== Layer 1: read_u16 ===\n");

    uint8_t buffer[256] = {0};
    // Little-endian: 0x1234 -> [0x34, 0x12]
    buffer[0] = 0x34; buffer[1] = 0x12;
    // Little-endian: 0x00FF -> [0xFF, 0x00]
    buffer[10] = 0xFF; buffer[11] = 0x00;

    TEST_ASSERT(read_u16(buffer, 0) == 0x1234, "read_u16 at offset 0 returns 0x1234 (LE)");
    TEST_ASSERT(read_u16(buffer, 10) == 0x00FF, "read_u16 at offset 10 returns 0x00FF (LE)");
}

void test_resolve_ram_ptr() {
    printf("\n=== Layer 1: resolve_ram_ptr ===\n");

    uint8_t buffer[256] = {0};

    // Bank $7E, offset 0x1234 -> 0x00001234
    buffer[0] = 0x34; buffer[1] = 0x12; buffer[2] = 0x7E;
    TEST_ASSERT(resolve_ram_ptr(buffer, 0) == 0x00001234, "Bank $7E offset 0x1234 -> 0x00001234");

    // Bank $7F, offset 0x5678 -> 0x00015678 (1 * 0x10000 + 0x5678)
    buffer[10] = 0x78; buffer[11] = 0x56; buffer[12] = 0x7F;
    TEST_ASSERT(resolve_ram_ptr(buffer, 10) == 0x00015678, "Bank $7F offset 0x5678 -> 0x00015678");

    // Invalid bank (0x00) -> returns 0
    buffer[20] = 0x99; buffer[21] = 0x88; buffer[22] = 0x00;
    TEST_ASSERT(resolve_ram_ptr(buffer, 20) == 0, "Invalid bank 0x00 -> returns 0");
}

// ============================================================================
// Layer 2: Data Accessor Tests (Simplified Mock)
// ============================================================================

void test_get_unit_data_ptr() {
    printf("\n=== Layer 2: get_unit_data_ptr ===\n");

    uint8_t ram[256] = {0};
    uint32_t handle = 0;

    // Handle +4 contains bank $7E, offset 0x1000
    ram[handle + 4] = 0x00; ram[handle + 5] = 0x10; ram[handle + 6] = 0x7E;
    uint32_t ptr = get_unit_data_ptr(ram, handle);
    TEST_ASSERT(ptr == 0x00001000, "Data pointer resolved from handle+4");
}

void test_get_unit_class_id() {
    printf("\n=== Layer 2: get_unit_class_id ===\n");

    uint8_t ram[256] = {0};
    uint32_t data_ptr = 0;

    // Class ID at offset UNIT_CLASS_ID_OFFSET (per ROM_MAP.md)
    ram[data_ptr + UNIT_CLASS_ID_OFFSET] = 5;  // Class ID = 5
    uint8_t class_id = get_unit_class_id(ram, data_ptr);
    TEST_ASSERT(class_id == 5, "Class ID read from data block");
}

void test_get_unit_level() {
    printf("\n=== Layer 2: get_unit_level ===\n");

    uint8_t ram[256] = {0};
    uint32_t data_ptr = 0;

    // Level at offset UNIT_LEVEL_OFFSET
    ram[data_ptr + UNIT_LEVEL_OFFSET] = 7;  // Level = 7
    int level = get_unit_level(ram, data_ptr);
    TEST_ASSERT(level == 7, "Level read from data block");
}

void test_get_unit_record_ptr() {
    printf("\n=== Layer 2: get_unit_record_ptr ===\n");

    uint8_t ram[0x4000] = {0};  // Large buffer for valid handle range
    uint32_t handle = 0x2000;  // Must be in valid range [0x2000, 0xFFE0]

    // Type 0, valid range
    ram[handle] = 0;  // Type 0
    ram[handle + 7] = 0x00; ram[handle + 8] = 0x20;  // Offset 0x2000
    ram[handle + 9] = 1;  // Class byte & 0x3F = 1

    uint32_t record_ptr = get_unit_record_ptr(ram, handle);
    TEST_ASSERT(record_ptr == (((uint32_t)(1 & 0x3F) << 16) | 0x2000), "Record pointer calculated");

    // Invalid type (Type 1)
    ram[handle] = 1;
    record_ptr = get_unit_record_ptr(ram, handle);
    TEST_ASSERT(record_ptr == 0, "Invalid type returns 0");
}

void test_get_unit_type() {
    printf("\n=== Layer 2: get_unit_type ===\n");

    uint8_t ram[16] = {0};
    uint32_t handle = 1;  // Small handle for inline type detection

    // Type 0 (playable fixed)
    ram[handle] = 0;
    TEST_ASSERT(get_unit_type(ram, handle) == 0, "Type 0 record detected");

    // Type 1 (child inherited)
    ram[handle] = 1;
    TEST_ASSERT(get_unit_type(ram, handle) == 1, "Type 1 child detected");

    // Type 2 (npc/boss fixed)
    ram[handle] = 2;
    TEST_ASSERT(get_unit_type(ram, handle) == 2, "Type 2 npc/boss detected");

    // Type 3 (class-based)
    ram[handle] = 3;
    TEST_ASSERT(get_unit_type(ram, handle) == 3, "Type 3 class-based detected");
}

void test_get_unit_gender() {
    printf("\n=== Layer 2: get_unit_gender ===\n");

    uint8_t ram[256] = {0};
    uint32_t data_ptr = 1;

    // Male (0)
    ram[data_ptr + TYPE1_GENDER_OFFSET] = 0;
    TEST_ASSERT(get_unit_gender(ram, data_ptr) == 0, "Male gender detected");

    // Female (1)
    ram[data_ptr + TYPE1_GENDER_OFFSET] = 1;
    TEST_ASSERT(get_unit_gender(ram, data_ptr) == 1, "Female gender detected");
}

void test_get_child_heir_flag() {
    printf("\n=== Layer 2: get_child_heir_flag ===\n");

    uint8_t ram[256] = {0};
    uint32_t data_ptr = 1;

    // Father heir (1)
    ram[data_ptr + TYPE1_HEIR_OFFSET] = 1;
    TEST_ASSERT(get_child_heir_flag(ram, data_ptr) == 1, "Father heir flag set");

    // Mother heir (0)
    ram[data_ptr + TYPE1_HEIR_OFFSET] = 0;
    TEST_ASSERT(get_child_heir_flag(ram, data_ptr) == 0, "Mother heir flag set");
}

void test_blood_type_helpers() {
    printf("\n=== Layer 2/3: blood_type helpers ===\n");

    // Byte to enum
    TEST_ASSERT(blood_type_from_byte(0) == BLOOD_NONE, "Byte 0 -> NONE");
    TEST_ASSERT(blood_type_from_byte(1) == BLOOD_BALDO, "Byte 1 -> BALDO");
    TEST_ASSERT(blood_type_from_byte(3) == BLOOD_HEZUL, "Byte 3 -> HEZUL");
    TEST_ASSERT(blood_type_from_byte(20) == BLOOD_NONE, "Byte 20 (invalid) -> NONE");

    // Enum to byte
    TEST_ASSERT(blood_type_to_byte(BLOOD_NONE) == 0, "NONE -> byte 0");
    TEST_ASSERT(blood_type_to_byte(BLOOD_BALDO) == 1, "BALDO -> byte 1");
    TEST_ASSERT(blood_type_to_byte(BLOOD_HEZUL) == 3, "HEZUL -> byte 3");
}

void test_get_unit_blood_type() {
    printf("\n=== Layer 2: get_unit_blood_type ===\n");

    uint8_t ram[64] = {0};
    uint32_t data_ptr = 0;

    // No blood (all zeros)
    BloodType bt1 = get_unit_blood_type(ram, data_ptr);
    TEST_ASSERT(bt1 == BLOOD_NONE, "No blood -> NONE");

    // Baldo blood at slot 0 (value 1 in bits 0-1 of byte 0)
    ram[data_ptr + UNIT_BLOOD_OFFSET] = 0x01;  // Slot 0 = value 1 (minor Baldo)
    BloodType bt2 = get_unit_blood_type(ram, data_ptr);
    TEST_ASSERT(bt2 == BLOOD_BALDO, "Baldo blood -> BALDO");

    // Hezul blood at slot 2 (value 1 in bits 4-5 of byte 0)
    memset(ram, 0, sizeof(ram));
    ram[data_ptr + UNIT_BLOOD_OFFSET] = 0x10;  // Slot 2 = value 1 (minor Hezul)
    BloodType bt3 = get_unit_blood_type(ram, data_ptr);
    TEST_ASSERT(bt3 == BLOOD_HEZUL, "Hezul blood -> HEZUL");

    // Invalid blood slot value
    memset(ram, 0, sizeof(ram));
    ram[data_ptr + UNIT_BLOOD_OFFSET] = 0xFF;  // All slots set to invalid values
    BloodType bt4 = get_unit_blood_type(ram, data_ptr);
    TEST_ASSERT(bt4 == BLOOD_NONE, "Invalid blood slot value -> NONE");
}

void test_get_parent_handles() {
    printf("\n=== Layer 2: get_parent_handles ===\n");

    uint8_t ram[256] = {0};
    uint32_t data_ptr = 1;  // Non-zero to pass null check

    // Same-gender parent at offset PARENT_SAME_GENDER_OFFSET (bank $7E, offset 0x1000)
    ram[data_ptr + PARENT_SAME_GENDER_OFFSET] = 0x00;
    ram[data_ptr + PARENT_SAME_GENDER_OFFSET + 1] = 0x10;
    ram[data_ptr + PARENT_SAME_GENDER_OFFSET + 2] = 0x7E;
    uint32_t same_parent = get_same_gender_parent_handle(ram, data_ptr);
    TEST_ASSERT(same_parent == 0x00001000u, "Same-gender parent handle resolved");

    // Secondary parent at offset PARENT_SECONDARY_OFFSET (bank $7F, offset 0x2000)
    ram[data_ptr + PARENT_SECONDARY_OFFSET] = 0x00;
    ram[data_ptr + PARENT_SECONDARY_OFFSET + 1] = 0x20;
    ram[data_ptr + PARENT_SECONDARY_OFFSET + 2] = 0x7F;
    uint32_t sec_parent = get_secondary_parent_handle(ram, data_ptr);
    TEST_ASSERT(sec_parent == 0x00012000u, "Secondary parent handle resolved");
}

void test_get_class_record_ptr() {
    printf("\n=== Layer 2: get_class_record_ptr ===\n");

    // Class table starts at CLASS_TABLE_OFFSET, each record is CLASS_RECORD_SIZE bytes
    TEST_ASSERT(get_class_record_ptr(0) == CLASS_TABLE_OFFSET, "Class 0 -> base offset");
    TEST_ASSERT(get_class_record_ptr(1) == CLASS_TABLE_OFFSET + CLASS_RECORD_SIZE, "Class 1 -> +31 bytes");
    TEST_ASSERT(get_class_record_ptr(5) == CLASS_TABLE_OFFSET + (5 * CLASS_RECORD_SIZE), "Class 5 -> +155 bytes");
}

void test_read_rom_class_growth() {
    printf("\n=== Layer 2: read_rom_class_growth ===\n");

    uint8_t rom[256] = {0};
    uint32_t record_ptr = 0;

    // Setup class record with growth rates
    // Per ROM_MAP.md: Class records have growths at offset +8-14 (+15 for LCK)
    rom[record_ptr + CLASS_HP_GROWTH_OFFSET] = 10;   // HP growth (stat=0, offset+8)
    rom[record_ptr + CLASS_STAT_GROWTH_OFFSET] = 15;  // STR growth (offset+9)
    rom[record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_MAG - 1] = 5;   // MAG growth (offset+10)
    rom[record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_SKL - 1] = 20;  // SKL growth (offset+11)
    rom[record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_SPD - 1] = 8;   // SPD growth (offset+12)
    rom[record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_DEF - 1] = 12;  // DEF growth (offset+13)
    rom[record_ptr + 15] = 25;  // LCK growth (offset+15)

    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_HP) == 10, "HP class growth = 10");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_STR) == 15, "STR class growth = 15");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_MAG) == 5, "MAG class growth = 5");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_SKL) == 20, "SKL class growth = 20");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_SPD) == 8, "SPD class growth = 8");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_DEF) == 12, "DEF class growth = 12");
    TEST_ASSERT(read_rom_class_growth(rom, record_ptr, STAT_LCK) == 25, "LCK class growth = 25");
}

void test_read_rom_total_growth() {
    printf("\n=== Layer 2: read_rom_total_growth ===\n");

    uint8_t rom[256] = {0};
    uint32_t record_ptr = 0;

    // Setup unit record with total growth rates (HP at offset, others at offset+stat)
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET] = 12;  // HP total growth
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_STR] = 18;  // STR total
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_MAG] = 7;   // MAG total
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_SKL] = 22;  // SKL total
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_SPD] = 10;  // SPD total
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_DEF] = 14;  // DEF total
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET + STAT_LCK] = 28;  // LCK total

    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_HP) == 12, "HP total growth = 12");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_STR) == 18, "STR total growth = 18");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_MAG) == 7, "MAG class growth = 7");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_SKL) == 22, "SKL class growth = 22");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_SPD) == 10, "SPD class growth = 10");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_DEF) == 14, "DEF class growth = 14");
    TEST_ASSERT(read_rom_total_growth(rom, record_ptr, STAT_LCK) == 28, "LCK class growth = 28");
}

// ============================================================================
// Layer 3: Type-Specific Computation Tests
// ============================================================================

void test_compute_fixed_stats() {
    printf("\n=== Layer 3: compute fixed stats ===\n");

    // Mock ROM with class data
    uint8_t rom[0x40000] = {0};
    GrowthStatInfo info;

    // Setup class record at CLASS_TABLE_OFFSET
    uint32_t class_record_ptr = get_class_record_ptr(0);
    rom[class_record_ptr + 1] = 20;   // HP base stat
    rom[class_record_ptr + 8] = 75;   // HP growth rate

    // Mock Type 0 unit with personal stats
    uint8_t ram[0x4000] = {0};
    uint32_t handle = 0x2000;
    ram[handle] = 0;  // Type 0
    // Data pointer at handle+4: $7E:1000 -> data_ptr = 0x00001000
    ram[handle + 4] = 0x00; ram[handle + 5] = 0x10; ram[handle + 6] = 0x7E;
    // Record pointer fields at handle+7 (low offset) and handle+9 (class_id)
    ram[handle + 7] = 0x00; ram[handle + 8] = 0x10;  // record_offset = 0x1000
    uint32_t data_ptr = 0x00001000;
    // Class ID at handle+9 (read by get_unit_record_ptr) and data_ptr+9 (read by get_unit_class_id)
    ram[handle + 9] = 0;  // Class ID 0
    ram[data_ptr + 9] = 0;  // Class ID 0 (same value)
    // Total growth at rom address record_ptr+20 (HP), not ram!
    uint32_t record_ptr = ((uint32_t)(0 & 0x3F) << 16) | 0x1000;  // 0x00001000
    rom[record_ptr + UNIT_RECORD_GROWTH_OFFSET] = 25;  // HP total growth

    compute_fixed_stats(ram, rom, sizeof(rom), handle, STAT_HP, &info);
    // class_base_growth comes from read_rom_class_growth which returns growth rate at offset +8
    TEST_ASSERT(info.class_base_growth == 75, "Class base HP growth = 75");
    TEST_ASSERT(info.total_growth_rate == 25, "Total HP = 25");
    TEST_ASSERT(info.personal_modifier == -50, "Personal modifier (total - class) = 25 - 75 = -50");
}

void test_compute_inherited_child_stats() {
    printf("\n=== Layer 3: compute inherited child stats ===\n");

    // Mock ROM with class data
    uint8_t rom[0x40000] = {0};
    GrowthStatInfo info;

    // Setup class record at CLASS_TABLE_OFFSET
    uint32_t class_record_ptr = get_class_record_ptr(0);
    rom[class_record_ptr + 1] = 20;   // HP base stat
    rom[class_record_ptr + 8] = 75;   // HP growth rate

    // Mock Type 1 child with inline data at handle+offset
    uint8_t ram[32] = {0};
    uint32_t handle = 0x2000;
    ram[handle] = 1;  // Type 1 (child)
    ram[handle + TYPE1_CLASS_OFFSET] = 0;  // Class ID 0
    ram[handle + TYPE1_LEVEL_OFFSET] = 5;  // Level 5
    ram[handle + TYPE1_GENDER_OFFSET] = 0;  // Male
    ram[handle + TYPE1_HEIR_OFFSET] = 1;  // Father heir

    compute_inherited_child_stats(ram, rom, sizeof(rom), handle, STAT_HP, &info);
    // class_base_growth comes from read_rom_class_growth which returns growth rate at offset +8
    TEST_ASSERT(info.class_base_growth == 75, "Class base HP growth = 75");
    TEST_ASSERT(info.blood_type_count == 0, "No blood without parent lookup");
}

void test_compute_class_based_stats() {
    printf("\n=== Layer 3: compute class-based stats ===\n");

    // Mock ROM large enough to hold class record at CLASS_TABLE_OFFSET
    uint8_t rom[0x40000] = {0};
    GrowthStatInfo info;
    StatDisplayLine line;

    // Setup class record: Knight Lord (class_id=0, stored at CLASS_TABLE_OFFSET)
    uint32_t class_record_ptr = get_class_record_ptr(0);

    // Class bases: HP at offset CLASS_HP_BASE_OFFSET, STR at offset CLASS_STAT_BASE_OFFSET+STAT_STR, etc.
    rom[class_record_ptr + CLASS_HP_BASE_OFFSET] = 20;   // HP base stat
    rom[class_record_ptr + CLASS_STAT_BASE_OFFSET + STAT_STR] = 18;  // STR base stat
    rom[class_record_ptr + CLASS_STAT_BASE_OFFSET + STAT_MAG] = 10;  // MAG base stat
    rom[class_record_ptr + CLASS_STAT_BASE_OFFSET + STAT_DEF] = 12;  // DEF base stat

    // Class growths: HP at offset CLASS_HP_GROWTH_OFFSET, STR at offset CLASS_STAT_GROWTH_OFFSET+STAT_STR, etc.
    rom[class_record_ptr + CLASS_HP_GROWTH_OFFSET] = 75;   // HP growth rate
    rom[class_record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_STR] = 90;  // STR growth rate
    rom[class_record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_MAG] = 60;  // MAG growth rate
    rom[class_record_ptr + CLASS_STAT_GROWTH_OFFSET + STAT_DEF] = 45;  // DEF growth rate

    // Type 3 unit: level 10 Knight Lord (inline data at handle+offset)
    uint8_t ram[32] = {0};
    uint32_t handle = 1;  // Small handle for inline Type 3 record
    ram[handle + TYPE3_CLASS_OFFSET] = 0;  // Class ID 0 (Knight Lord) at offset +5
    ram[handle + TYPE3_LEVEL_OFFSET] = 10; // Level 10 at offset +7

    // Test HP stat: class_base=20, growth=75, level=10
    // total = 20 + (75 * 10 / 100) = 20 + 7 = 27
    compute_class_based_stats(ram, rom, sizeof(rom), handle, STAT_HP, &info);
    TEST_ASSERT(info.class_base_growth == 20, "Class base HP = 20");
    TEST_ASSERT(info.total_growth_rate == 27, "Total HP = 20 + 7 = 27");
    TEST_ASSERT(info.personal_modifier == 7, "Personal modifier (growth contribution) = 7");
    TEST_ASSERT(info.blood_type_count == 0, "Class-based units have no blood");
}

// ============================================================================
// Layer 3: Pure Math Logic (Isolated - No Memory Access Needed)
// ============================================================================

void test_calculate_expected_gain() {
    printf("\n=== Layer 3: calculate_expected_gain ===\n");

    // Expected gain = growth_rate * levels / 100
    TEST_ASSERT(calculate_expected_gain(50, 2) == 1.0f, "50% over 2 levels = 1.0");
    TEST_ASSERT(calculate_expected_gain(100, 5) == 5.0f, "100% over 5 levels = 5.0");
    TEST_ASSERT(calculate_expected_gain(25, 4) == 1.0f, "25% over 4 levels = 1.0");
    TEST_ASSERT(calculate_expected_gain(75, 3) == 2.25f, "75% over 3 levels = 2.25");
    TEST_ASSERT(calculate_expected_gain(0, 10) == 0.0f, "0% over 10 levels = 0.0");

    // Edge cases
    TEST_ASSERT(calculate_expected_gain(1, 1) == 0.01f, "1% over 1 level = 0.01");
    TEST_ASSERT(calculate_expected_gain(99, 1) == 0.99f, "99% over 1 level = 0.99");
    TEST_ASSERT(calculate_expected_gain(50, 1) == 0.5f, "50% over 1 level = 0.5");
    TEST_ASSERT(calculate_expected_gain(10, 20) == 2.0f, "10% over 20 levels = 2.0");
}

void test_calculate_std_dev() {
    printf("\n=== Layer 3: calculate_std_dev ===\n");

    // Std dev = sqrt(n * p * (1-p)) where p = growth_rate / 100
    float std_50_2 = calculate_std_dev(50, 2);
    TEST_ASSERT(fabsf(std_50_2 - 0.7071f) < 0.01f, "50% over 2 levels: std_dev ~ 0.707");

    float std_100_5 = calculate_std_dev(100, 5);
    TEST_ASSERT(std_100_5 == 0.0f, "100% deterministic: std_dev = 0");

    float std_0_5 = calculate_std_dev(0, 5);
    TEST_ASSERT(std_0_5 == 0.0f, "0% deterministic: std_dev = 0");

    float std_25_4 = calculate_std_dev(25, 4);
    TEST_ASSERT(fabsf(std_25_4 - 0.866f) < 0.01f, "25% over 4 levels: std_dev ~ 0.866");

    // Edge cases
    float std_1_1 = calculate_std_dev(1, 1);
    TEST_ASSERT(fabsf(std_1_1 - 0.0995f) < 0.01f, "1% over 1 level: std_dev ~ 0.099");

    float std_99_1 = calculate_std_dev(99, 1);
    TEST_ASSERT(fabsf(std_99_1 - 0.0995f) < 0.01f, "99% over 1 level: std_dev ~ 0.099");

    float std_50_10 = calculate_std_dev(50, 10);
    TEST_ASSERT(fabsf(std_50_10 - 1.581f) < 0.01f, "50% over 10 levels: std_dev ~ 1.581");
}

void test_evaluate_rng_impact() {
    printf("\n=== Layer 3: evaluate_rng_impact ===\n");

    // Blessed: actual > expected + 1*std_dev
    RngImpact r1 = evaluate_rng_impact(2, 1.0f, 0.707f);  // 2 > 1.0 + 0.707 = 1.707
    TEST_ASSERT(r1 == RNG_BLESSED, "Blessed: actual=2 > expected+std");

    // Cursed: actual < expected - 1*std_dev
    RngImpact r2 = evaluate_rng_impact(0, 1.0f, 0.707f);  // 0 < 1.0 - 0.707 = 0.293
    TEST_ASSERT(r2 == RNG_CURSED, "Cursed: actual=0 < expected-std");

    // Neutral: within sigma bounds
    RngImpact r3 = evaluate_rng_impact(1, 1.0f, 0.707f);  // 1 is between 0.293 and 1.707
    TEST_ASSERT(r3 == RNG_NEUTRAL, "Neutral: actual=1 within sigma bounds");

    // Edge case: std_dev near zero (deterministic) - use clear thresholds
    RngImpact r4 = evaluate_rng_impact(6, 4.5f, 0.0001f);  // deterministic, 6 > 4.5+0.5
    TEST_ASSERT(r4 == RNG_BLESSED, "Deterministic blessed: actual=6 > expected+0.5");

    RngImpact r5 = evaluate_rng_impact(3, 4.5f, 0.0001f);  // deterministic, 3 < 4.5-0.5
    TEST_ASSERT(r5 == RNG_CURSED, "Deterministic cursed: actual=3 < expected+0.5");

    // Boundary cases (at exactly threshold)
    RngImpact r6 = evaluate_rng_impact(2, 1.0f, 1.0f);  // 2 == 1.0 + 1*1.0 = 2.0
    TEST_ASSERT(r6 == RNG_NEUTRAL, "Boundary blessed: actual=2 == expected+std");

    RngImpact r7 = evaluate_rng_impact(0, 1.0f, 1.0f);  // 0 == 1.0 - 1*1.0 = 0.0
    TEST_ASSERT(r7 == RNG_NEUTRAL, "Boundary cursed: actual=0 == expected-std");
}

// ============================================================================
// Layer 3: Blood Bonus Calculation Tests
// ============================================================================

void test_get_blood_bonus_table() {
    printf("\n=== Layer 3: get_blood_bonus_table ===\n");

    BloodBonus bonus;

    // Baldo: +20% HP, +10% STR, +10% SKL, +10% LCK
    bonus = get_blood_bonus_table(BLOOD_BALDO);
    TEST_ASSERT(bonus.hp_bonus == 20, "Baldo HP bonus = 20");
    TEST_ASSERT(bonus.str_bonus == 10, "Baldo STR bonus = 10");
    TEST_ASSERT(bonus.skl_bonus == 10, "Baldo SKL bonus = 10");
    TEST_ASSERT(bonus.lck_bonus == 10, "Baldo LCK bonus = 10");

    // Hezul: +20% HP, +30% STR
    bonus = get_blood_bonus_table(BLOOD_HEZUL);
    TEST_ASSERT(bonus.hp_bonus == 20, "Hezul HP bonus = 20");
    TEST_ASSERT(bonus.str_bonus == 30, "Hezul STR bonus = 30");

    // No blood
    bonus = get_blood_bonus_table(BLOOD_NONE);
    TEST_ASSERT(bonus.hp_bonus == 0 && bonus.str_bonus == 0, "No blood -> zero bonuses");
}

void test_calculate_blood_bonus_total() {
    printf("\n=== Layer 3: calculate_blood_bonus_total ===\n");

    // Baldo: 20+10+10+10 = 50
    TEST_ASSERT(calculate_blood_bonus_total(BLOOD_BALDO) == 50, "Baldo total bonus = 50");

    // Hezul: 20+30 = 50
    TEST_ASSERT(calculate_blood_bonus_total(BLOOD_HEZUL) == 50, "Hezul total bonus = 50");

    // No blood: 0
    TEST_ASSERT(calculate_blood_bonus_total(BLOOD_NONE) == 0, "No blood total bonus = 0");

    // Fala: 20+30 = 50
    TEST_ASSERT(calculate_blood_bonus_total(BLOOD_FALA) == 50, "Fala total bonus = 50");
}

// ============================================================================
// Layer 3: Inheritance Calculation Tests
// ============================================================================

void test_calculate_inherited_growth_rate() {
    printf("\n=== Layer 3: calculate_inherited_growth_rate ===\n");

    // Formula: same_gender + (other_gender / 2) + blood_bonus
    int rate1 = calculate_inherited_growth_rate(50, 30, BLOOD_NONE);  // 50 + 15 + 0 = 65
    TEST_ASSERT(rate1 == 65, "No blood: 50 + 30/2 = 65");

    int rate2 = calculate_inherited_growth_rate(40, 20, BLOOD_BALDO);  // 40 + 10 + avg_bonus
    TEST_ASSERT(rate2 > 50, "With Baldo blood: 40 + 20/2 + bonus > 50");
}

void test_determine_child_blood_type() {
    printf("\n=== Layer 3: determine_child_blood_type ===\n");

    // Same Type Rule: Both parents Minor of same type -> Major Blood
    BloodType blood1 = determine_child_blood_type(BLOOD_BALDO, BLOOD_BALDO, 1);
    TEST_ASSERT(blood1 == BLOOD_BALDO, "Same blood both parents -> inherited");

    // Major Blood passed to same-gender child
    BloodType blood2 = determine_child_blood_type(BLOOD_HEZUL, BLOOD_NONE, 1);
    TEST_ASSERT(blood2 == BLOOD_HEZUL, "Same-gender child gets parent blood");

    // No blood in either parent
    BloodType blood3 = determine_child_blood_type(BLOOD_NONE, BLOOD_NONE, 0);
    TEST_ASSERT(blood3 == BLOOD_NONE, "No parent blood -> no blood");
}

// ============================================================================
// Layer 4: Display Formatting (Isolated - No Memory Access Needed)
// ============================================================================

void test_stat_label() {
    printf("\n=== Layer 4: stat_label ===\n");

    TEST_ASSERT(strcmp(stat_label(STAT_HP), "HP") == 0, "stat_label(HP) = 'HP'");
    TEST_ASSERT(strcmp(stat_label(STAT_STR), "SP") == 0, "stat_label(STR) = 'SP'");
    TEST_ASSERT(strcmp(stat_label(STAT_MAG), "MT") == 0, "stat_label(MAG) = 'MT'");
    TEST_ASSERT(strcmp(stat_label(STAT_SKL), "LK") == 0, "stat_label(SKL) = 'LK'");
    TEST_ASSERT(strcmp(stat_label(STAT_SPD), "SD") == 0, "stat_label(SPD) = 'SD'");
    TEST_ASSERT(strcmp(stat_label(STAT_DEF), "DF") == 0, "stat_label(DEF) = 'DF'");
    TEST_ASSERT(strcmp(stat_label(STAT_RES), "--") == 0, "stat_label(RES) = 'RS'");
}

void test_format_stat_display_line() {
    printf("\n=== Layer 4: format_stat_display_line ===\n");

    GrowthStatInfo info;
    StatDisplayLine line;

    // Blessed case: total > expected + std_dev
    info.class_base_growth = 10;
    info.personal_modifier = 5;
    info.total_growth_rate = 15;
    info.expected_gain_per_level = 0.1f;
    info.std_dev_per_level = 0.3f;

    format_stat_display_line(STAT_HP, &info, &line);
    TEST_ASSERT(line.impact == RNG_BLESSED, "Blessed display: total=15 > expected");
    TEST_ASSERT(strstr(line.text, "[B]") != NULL, "Display contains [B] indicator");

    // Cursed case: total < expected - std_dev
    info.class_base_growth = 10;
    info.personal_modifier = -3;
    info.total_growth_rate = 7;
    info.expected_gain_per_level = 0.1f;
    info.std_dev_per_level = 0.3f;

    format_stat_display_line(STAT_STR, &info, &line);
    TEST_ASSERT(line.impact == RNG_CURSED, "Cursed display: total=7 < expected");
    TEST_ASSERT(strstr(line.text, "[C]") != NULL, "Display contains [C] indicator");

    // Neutral case (within sigma bounds)
    info.class_base_growth = 10;
    info.personal_modifier = 0;
    info.total_growth_rate = 10;
    info.expected_gain_per_level = 0.1f;
    info.std_dev_per_level = 0.3f;

    format_stat_display_line(STAT_MAG, &info, &line);
    TEST_ASSERT(line.impact == RNG_NEUTRAL, "Neutral display: within bounds");
    TEST_ASSERT(strstr(line.text, "[N]") != NULL, "Display contains [N] indicator");

    // Edge cases: zero growth, negative modifier
    info.class_base_growth = 0;
    info.personal_modifier = 0;
    info.total_growth_rate = 0;
    info.expected_gain_per_level = 0.0f;
    info.std_dev_per_level = 0.0f;

    format_stat_display_line(STAT_SPD, &info, &line);
    TEST_ASSERT(strlen(line.text) > 0, "Zero growth displays text");

    // High growth rates
    info.class_base_growth = 255;
    info.personal_modifier = 10;
    info.total_growth_rate = 265;
    info.expected_gain_per_level = 2.55f;
    info.std_dev_per_level = 0.5f;

    format_stat_display_line(STAT_LCK, &info, &line);
    TEST_ASSERT(line.impact == RNG_BLESSED, "High growth displays as blessed");
}

void test_format_all_stats_display() {
    printf("\n=== Layer 4: format_all_stats_display ===\n");

    GrowthStatInfo growths[STAT_COUNT];
    StatDisplayLine lines[STAT_COUNT];

    // Setup mock growth data for all stats (no memory access needed)
    for (int i = 0; i < STAT_COUNT; i++) {
        growths[i].class_base_growth = 10 + i * 2;
        growths[i].personal_modifier = i;
        growths[i].total_growth_rate = 10 + i * 3;
        growths[i].expected_gain_per_level = 0.1f + (float)i * 0.05f;
        growths[i].std_dev_per_level = 0.2f;
    }

    format_all_stats_display(growths, lines);

    TEST_ASSERT(lines[STAT_HP].text[0] != '\0', "HP line has text");
    TEST_ASSERT(lines[STAT_STR].text[0] != '\0', "STR line has text");
    TEST_ASSERT(strlen(lines[STAT_HP].text) > 0, "HP line has text");
    TEST_ASSERT(strlen(lines[STAT_LCK].text) > 0, "LCK line has text");
}

// ============================================================================
// Legacy API Compatibility Tests (Isolated - No Memory Access Needed)
// ============================================================================

void test_get_stat_estimation() {
    printf("\n=== Legacy: get_stat_estimation ===\n");

    StatEstimation est;

    // Original regression tests from test_growth_stats.c
    get_stat_estimation(50, 1, 2, &est);
    TEST_ASSERT(est.impact == RNG_NEUTRAL, "50%, 2 levels, gain=1: NEUTRAL");

    get_stat_estimation(50, 2, 2, &est);
    TEST_ASSERT(est.impact == RNG_BLESSED, "50%, 2 levels, gain=2: BLESSED");

    get_stat_estimation(50, 0, 2, &est);
    TEST_ASSERT(est.impact == RNG_CURSED, "50%, 2 levels, gain=0: CURSED");

    get_stat_estimation(10, 1, 1, &est);
    TEST_ASSERT(est.impact == RNG_BLESSED, "10%, 1 level, gain=1: BLESSED");

    get_stat_estimation(10, 0, 1, &est);
    TEST_ASSERT(est.impact == RNG_NEUTRAL, "10%, 1 level, gain=0: NEUTRAL");
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main(void) {
    printf("========================================\n");
    printf("Growth Stats Unit Test Suite\n");
    printf("(Isolated Responsibility - No Mock RAM/ROM)\n");
    printf("========================================\n");

    // Layer 1: Memory Access Primitives
    test_read_u8();
    test_read_u16();
    test_resolve_ram_ptr();

    // Layer 2: Data Accessors
    test_get_unit_data_ptr();
    test_get_unit_class_id();
    test_get_unit_level();
    test_get_unit_record_ptr();
    test_blood_type_helpers();
    test_get_unit_blood_type();
    test_get_parent_handles();
    test_get_unit_type();
    test_get_unit_gender();
    test_get_child_heir_flag();
    test_get_class_record_ptr();
    test_read_rom_class_growth();
    test_read_rom_total_growth();

    // Layer 3: Type-Specific Computation
    test_compute_fixed_stats();
    test_compute_inherited_child_stats();
    test_compute_class_based_stats();

    // Layer 3: Math Logic
    test_calculate_expected_gain();
    test_calculate_std_dev();
    test_evaluate_rng_impact();

    // Layer 3: Blood Bonus & Inheritance
    test_get_blood_bonus_table();
    test_calculate_blood_bonus_total();
    test_calculate_inherited_growth_rate();
    test_determine_child_blood_type();

    // Layer 4: Display Formatting
    test_stat_label();
    test_format_stat_display_line();
    test_format_all_stats_display();

    // Legacy API Compatibility
    test_get_stat_estimation();

    printf("\n========================================\n");
    printf("RESULTS: %d/%d passed, %d failed\n", tests_passed, tests_run, tests_failed);
    printf("========================================\n");

    if (tests_failed > 0) {
        printf("WARNING: Some tests failed!\n");
        return 1;
    }

    printf("\nALL TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
