/*
 * TEST SUITE: Growth Rate Tracker
 * -------------------------------
 * This test suite validates the growth rate calculation logic.
 * 
 * Related files modified for this feature:
 * - src/growth_stats.c
 * - src/growth_stats.h
 * - src/menu.cpp
 * - src/menu.h
 */

// Unit test for growth stats logic
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../src/growth_stats.h"

// Mocking the memory buffers
#define MOCK_RAM_SIZE 0x10000
#define MOCK_ROM_SIZE 0x100000

void test_growth_calculations() {
    uint8_t ram[MOCK_RAM_SIZE] = {0};
    uint8_t rom[MOCK_ROM_SIZE] = {0};
    int romLen = MOCK_ROM_SIZE;

    // --- Setup Mock Data ---
    
    // 1. Set up a Class Record in ROM (at offset 0x1000)
    // Let's say STR growth is 5% (0x05) and MAG growth is 3% (0x03)
    uint32_t class_record_ptr = 0x1000;
    rom[class_record_ptr + STAT_STR + 1] = 5; // STR base growth
    rom[class_record_ptr + STAT_MAG + 1] = 3; // MAG base growth

    // 2. Set up a Unit Record in ROM (at offset 0x2000)
    uint32_t unit_record_ptr = 0x2000;
    rom[unit_record_ptr + 20] = 10; // HP growth total
    rom[unit_record_ptr + 21] = 5;  // STR growth total (Base 5 + Personal 0)
    rom[unit_record_ptr + 22] = 8;  // MAG growth total (Base 3 + Personal 5)

    // 3. Set up the Unit in RAM (at handle 0x7E2000 - using bank 7F for simplicity)
    uint32_t handle = 0x7E2000;
    ram[handle] = 0; // Type 0 (Standard unit)
    // The pointer in RAM points to the record we just made in ROM
    *(uint16_t*)(ram + handle + 4) = 0x0000; // Low word
    ram[handle + 6] = 0x7F;                 // Bank (7F - 7E = 1, so offset is 0x10000)
    // Wait, let's simplify: if bank is 7F, ram_ptr returns (1 * 0x10000) + w16.
    // So we want the pointer to be 0x2000 in our mock ROM buffer.
    *(uint16_t*)(ram + handle + 4) = 0x2000; // Low word (offset within bank)
    ram[handle + 6] = 0x7F;                 // Bank 7F

    // --- The Test ---
    GrowthStatInfo info[STAT_COUNT];
    get_unit_all_growths(ram, rom, romLen, handle, info);

    printf("Running Growth Rate Tests...\n");

    // Test STR: Base (5) + Personal (0) = Total (5)
    assert(info[STAT_STR].class_base_growth == 5);
    assert(info[STAT_STR].total_growth_rate == 5);
    printf("  [PASS] STR calculation\n");

    // Test MAG: Base (3) + Personal (5) = Total (8)
    // We need to set a personal modifier in the unit record.
    // In our mock, total is 8, base is 3, so personal must be 5.
    assert(info[STAT_MAG].class_base_growth == 3);
    assert(info[STAT_MAG].total_growth_rate == 8);
    printf("  [PASS] MAG calculation\n");

    // Test HP: Base (0) + Personal (10) = Total (10)
    assert(info[STAT_HP].total_growth_rate == 10);
    printf("  [PASS] HP calculation\n");

    printf("\nALL TESTS PASSED SUCCESSFULLY!\n");
}

int main() {
    test_growth_calculations();
    return 0;
}