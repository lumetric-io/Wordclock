# Word Clock Firmware - Test Suite

## Overview

This directory contains unit tests for the Word Clock firmware using Google Test (gtest) framework with PlatformIO Native testing.

## Structure

```
test/
├── test_time_mapper/         # Time-to-LED mapping logic tests
│   └── test_time_mapper.cpp
├── test_led_controller/      # LED control and display tests
│   └── test_led_controller.cpp
├── test_night_mode/          # Night mode scheduling tests
│   └── test_night_mode.cpp
├── mocks/                    # Mock implementations for testing
│   ├── mock_arduino.h        # Mock Arduino core functions
│   ├── mock_preferences.h    # Mock ESP32 Preferences/NVS
│   ├── mock_grid_layout.h    # Mock grid layout data
│   ├── mock_time.h           # Time helpers
│   ├── mock_log.h            # Mock logging
│   └── mock_mqtt.h           # Mock MQTT publishing
├── helpers/                  # Test utilities
│   └── test_utils.h          # Helper functions and assertions
└── README.md                 # This file
```

## Running Tests

### Prerequisites

- PlatformIO CLI installed
- C++17 compatible compiler (included with PlatformIO)

### Run All Tests

```bash
pio test -e native
```

### Run Specific Test Suite

```bash
# Time mapper tests only
pio test -e native -f test_time_mapper

# LED controller tests only
pio test -e native -f test_led_controller

# Night mode tests only
pio test -e native -f test_night_mode
```

### Verbose Output

```bash
pio test -e native -v
```

### Clean and Run

```bash
pio test -e native --clean
```

## Test Coverage

| Module | Test File | Test Count | Coverage Target |
|--------|-----------|------------|-----------------|
| time_mapper.cpp | test_time_mapper.cpp | 20+ tests | 90% |
| led_controller.cpp | test_led_controller.cpp | 10+ tests | 85% |
| night_mode.cpp | test_night_mode.cpp | 30+ tests | 85% |

## Writing New Tests

### Test Structure

Follow the Arrange-Act-Assert (AAA) pattern:

```cpp
#include <gtest/gtest.h>
#include "../mocks/mock_dependencies.h"
#include "../../src/module_to_test.cpp"

class ModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup before each test
    }
    
    void TearDown() override {
        // Cleanup after each test
    }
};

TEST_F(ModuleTest, DescriptiveTestName) {
    // Arrange: Set up test data
    auto input = createTestInput();
    
    // Act: Execute the function under test
    auto result = functionUnderTest(input);
    
    // Assert: Verify the results
    ASSERT_EQ(expected, result);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
```

### Best Practices

1. **One Assertion Per Test** (when possible)
   - Makes failures easier to diagnose
   - Tests remain focused and readable

2. **Descriptive Test Names**
   - Use pattern: `MethodName_StateUnderTest_ExpectedBehavior`
   - Example: `SetBrightness_ValueAbove255_ClampsTo255`

3. **Independent Tests**
   - Tests should not depend on each other
   - Use SetUp/TearDown for initialization/cleanup

4. **Fast Execution**
   - Unit tests should run in milliseconds
   - Avoid sleeps, network calls, file I/O

5. **Clear Error Messages**
   - Use descriptive assertion messages
   - Example: `ASSERT_TRUE(result) << "Expected feature X to be enabled";`

### Available Mocks

#### Mock Arduino String

```cpp
#include "../mocks/mock_arduino.h"

String str = "Hello";
str += " World";
ASSERT_STREQ("Hello World", str.c_str());
```

#### Mock Preferences

```cpp
#include "../mocks/mock_preferences.h"

Preferences prefs;
prefs.begin("namespace", false);
prefs.putUInt("key", 42);
ASSERT_EQ(42, prefs.getUInt("key"));
prefs.end();

// Reset between tests
Preferences::reset();
```

#### Mock Time

```cpp
#include "../mocks/mock_time.h"

struct tm time = createTestTime(12, 30, 0);
// time.tm_hour = 12, time.tm_min = 30, time.tm_sec = 0
```

#### Mock Grid Layout

```cpp
#include "../mocks/mock_grid_layout.h"

// Provides test word definitions and LED mapping
auto word = find_word("HET");
ASSERT_NE(nullptr, word);
```

### Custom Assertions

```cpp
#include "../helpers/test_utils.h"

// Compare LED vectors (order-independent)
std::vector<uint16_t> expected = {1, 2, 3};
std::vector<uint16_t> actual = {3, 1, 2};
ASSERT_LED_VECTOR_EQ(expected, actual);

// Check if vector contains value
ASSERT_TRUE(vectorContains(leds, 42));

// Check if vector contains all values from another vector
ASSERT_TRUE(vectorContainsAll(haystack, needles));
```

## Test Organization

### Time Mapper Tests

Tests for `src/time_mapper.cpp`:

- Word lookup (`get_leds_for_word`)
- Time-to-LED mapping (`get_led_indices_for_time`)
- All 5-minute intervals (00:00 to 23:55)
- Extra minute LEDs (for minutes 1-4)
- Midnight wraparound
- Word segments with keys

**Key Test Cases:**
- Exact hours (12:00)
- 5-minute intervals (12:05, 12:10, 12:15, etc.)
- Half past (12:30 shows next hour)
- Quarter to/past (12:15, 12:45)
- Midnight handling (00:00, 23:55)

### LED Controller Tests

Tests for `src/led_controller.cpp`:

- Initialization
- Showing single/multiple LEDs
- Clearing LEDs
- LED updates
- Order preservation
- Duplicate handling

**Key Test Cases:**
- Single LED display
- Multiple LED display
- Clear operation
- Update replaces previous LEDs
- Handles empty vector

### Night Mode Tests

Tests for `src/night_mode.cpp`:

- Initialization with defaults
- Enable/disable functionality
- Effect settings (Off, Dim)
- Dim percentage application
- Schedule calculations (with/without midnight wraparound)
- Override modes (Auto, ForceOn, ForceOff)
- Time string parsing
- Persistence (save/restore settings)
- Auto-flush mechanism

**Key Test Cases:**
- Schedule within single day (9:00 to 17:00)
- Schedule across midnight (22:00 to 6:00)
- Boundary conditions (exact start/end times)
- Override modes override schedule
- Brightness calculations
- Zero-length schedule handling

## Continuous Integration

Tests run automatically on:

- Push to `main`, `master`, or `develop` branches
- Pull requests to these branches
- Manual workflow dispatch

See `.github/workflows/tests.yml` for CI configuration.

### CI Test Results

Test results are uploaded as artifacts and retained for 30 days. View them in the GitHub Actions tab.

## Troubleshooting

### Common Issues

#### Test Compilation Fails

```bash
# Clean build cache and retry
pio test -e native --clean

# Check compiler errors in output
pio test -e native -v
```

#### Tests Pass Locally but Fail in CI

- Check for platform-dependent code
- Verify mock implementations match production code
- Check for uninitialized variables or timing dependencies

#### Mock Linker Errors

If you see "undefined reference" errors:

1. Ensure all required mocks are included
2. Check that mock headers are in `test/mocks/` directory
3. Verify `platformio.ini` includes mock path: `-I test/mocks`

#### Arduino.h or ESP32-specific Headers

Native tests cannot use actual ESP32 headers. Use provided mocks:

```cpp
// Instead of:
#include <Arduino.h>
#include <Preferences.h>

// Use (automatically redirected in test/mocks/):
#include "mock_arduino.h"
#include "mock_preferences.h"
```

### Debug Test Failures

1. **Run with verbose output:**
   ```bash
   pio test -e native -v
   ```

2. **Run specific failing test:**
   ```bash
   pio test -e native -f test_name
   ```

3. **Add debug output in tests:**
   ```cpp
   std::cout << "Debug: value = " << value << std::endl;
   ```

4. **Use GDB for native tests:**
   ```bash
   # Build test
   pio test -e native --without-uploading
   
   # Run with GDB
   gdb .pio/build/native/program
   ```

## Adding New Test Suites

To add tests for a new module:

1. **Create test directory:**
   ```bash
   mkdir test/test_my_module
   ```

2. **Create test file:**
   ```cpp
   // test/test_my_module/test_my_module.cpp
   #include <gtest/gtest.h>
   #include "../mocks/required_mocks.h"
   #include "../../src/my_module.cpp"
   
   class MyModuleTest : public ::testing::Test {
   protected:
       void SetUp() override { }
   };
   
   TEST_F(MyModuleTest, BasicTest) {
       ASSERT_TRUE(true);
   }
   
   int main(int argc, char **argv) {
       ::testing::InitGoogleTest(&argc, argv);
       return RUN_ALL_TESTS();
   }
   ```

3. **Run new tests:**
   ```bash
   pio test -e native -f test_my_module
   ```

## Resources

- [Google Test Documentation](https://google.github.io/googletest/)
- [PlatformIO Unit Testing](https://docs.platformio.org/en/latest/advanced/unit-testing/index.html)
- [Effective Unit Testing](https://martinfowler.com/bliki/UnitTest.html)

## Contributing

When contributing code:

1. Write tests for new features
2. Ensure existing tests pass
3. Maintain test coverage targets
4. Follow existing test patterns
5. Update this README if adding new test utilities

---

**Test Suite Version:** 1.0  
**Last Updated:** January 2026  
**Status:** ✅ Operational

