// ESP32 BoatHub - A.1 board bring-up
//
// Verifies the N16R8 configuration over the CH343P serial port before any
// sensor work starts: flash size, octal PSRAM and the partition table. A
// blinking LED only proves the toolchain; these numbers prove the board is
// configured as the design documents assume.
//
// See docs/design/A-001-devkit-and-carrier.md and A-002-bench-setup-usb.md.

#include <Arduino.h>
#include <esp_partition.h>

static const uint32_t EXPECTED_FLASH = 16u * 1024u * 1024u;  // N16
static const uint32_t EXPECTED_PSRAM = 8u * 1024u * 1024u;   // R8

// ESP.getPsramSize() reports the SPIRAM *heap* total, not the raw chip size:
// the allocator's own overhead sits in between (8386279 of 8388608 on this
// board). Check a floor for PSRAM, an exact value only for flash.
static const uint32_t MIN_PSRAM = EXPECTED_PSRAM - 64u * 1024u;

static bool checkAtLeast(const char *label, uint32_t actual, uint32_t minimum) {
  const bool ok = (actual >= minimum);
  Serial.printf("  %-14s %8lu bytes  at least %8lu  %s\n", label,
                (unsigned long)actual, (unsigned long)minimum,
                ok ? "OK" : "*** TOO SMALL ***");
  return ok;
}

static bool checkValue(const char *label, uint32_t actual, uint32_t expected) {
  const bool ok = (actual == expected);
  Serial.printf("  %-14s %8lu bytes  expected %8lu  %s\n", label,
                (unsigned long)actual, (unsigned long)expected,
                ok ? "OK" : "*** MISMATCH ***");
  return ok;
}

static void printPartitions() {
  Serial.println("Partitions:");
  esp_partition_iterator_t it =
      esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
  while (it != NULL) {
    const esp_partition_t *p = esp_partition_get(it);
    Serial.printf("  %-10s type 0x%02x sub 0x%02x  @ 0x%06lx  %8lu bytes\n",
                  p->label, p->type, p->subtype, (unsigned long)p->address,
                  (unsigned long)p->size);
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);  // NULL is allowed
}

static bool exercisePsram() {
  if (!psramFound()) {
    Serial.println("PSRAM: not found - check board_build.arduino.memory_type = qio_opi");
    return false;
  }
  const size_t block = 1024 * 1024;
  void *buf = ps_malloc(block);
  if (buf == NULL) {
    Serial.println("PSRAM: found but a 1 MB allocation failed");
    return false;
  }
  memset(buf, 0xA5, block);
  const bool ok = (((uint8_t *)buf)[block - 1] == 0xA5);
  free(buf);
  Serial.printf("PSRAM: 1 MB write/read %s\n", ok ? "OK" : "FAILED");
  return ok;
}

void setup() {
  Serial.begin(115200);
  const uint32_t deadline = millis() + 2000;
  while (!Serial && millis() < deadline) {
    delay(10);
  }

  Serial.println();
  Serial.println("=== ESP32 BoatHub - board bring-up ===");
  Serial.printf("Chip:  %s rev %d, %d core(s), %lu MHz\n", ESP.getChipModel(),
                ESP.getChipRevision(), ESP.getChipCores(),
                (unsigned long)getCpuFrequencyMhz());
  Serial.printf("SDK:   %s\n", ESP.getSdkVersion());

  Serial.println("Configuration:");
  bool ok = true;
  ok &= checkValue("flash", ESP.getFlashChipSize(), EXPECTED_FLASH);
  ok &= checkAtLeast("psram", ESP.getPsramSize(), MIN_PSRAM);
  Serial.printf("  %-14s %8lu bytes\n", "heap free",
                (unsigned long)ESP.getFreeHeap());

  ok &= exercisePsram();
  printPartitions();

#ifdef RGB_BUILTIN
  Serial.printf("RGB LED: assuming GPIO%d - if it stays dark, this clone wires it elsewhere\n",
                PIN_NEOPIXEL);
#else
  Serial.println("RGB LED: no RGB_BUILTIN for this board definition");
#endif

  Serial.printf("Result: %s\n", ok ? "configuration matches N16R8"
                                    : "CONFIGURATION MISMATCH - see above");
  Serial.println("======================================");
}

void loop() {
  static uint32_t beats = 0;

#ifdef RGB_BUILTIN
  neopixelWrite(RGB_BUILTIN, 0, 8, 0);  // dim green, the LED is bright
  delay(100);
  neopixelWrite(RGB_BUILTIN, 0, 0, 0);
  delay(900);
#else
  delay(1000);
#endif

  Serial.printf("heartbeat %lu  uptime %lus  heap %lu\n", (unsigned long)++beats,
                (unsigned long)(millis() / 1000), (unsigned long)ESP.getFreeHeap());
}
