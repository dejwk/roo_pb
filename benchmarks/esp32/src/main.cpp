#include "backend.h"

#ifdef ARDUINO
#include <Arduino.h>
#include <esp_arduino_version.h>
#include <esp_timer.h>

SET_LOOP_TASK_STACK_SIZE(32768);

namespace {

constexpr size_t kRounds = 9;
constexpr int64_t kTargetBatchUs = 40000;
constexpr size_t kMaxIterations = 65536;
uint8_t wire[benchmark::kCapacity];
uint8_t output[benchmark::kCapacity];
volatile uint32_t checksum = 0;

// Keeps serial I/O, task yielding, validation and filling outside measurement.
int64_t Batch(const benchmark::Case& test, bool encode, size_t iterations,
              size_t wire_size) {
  int64_t start = esp_timer_get_time();
  for (size_t i = 0; i < iterations; ++i) {
    if (encode) {
      size_t size = 0;
      if (!test.encode(output, sizeof(output), size)) {
        return -1;
      }
      checksum = checksum + output[size - 1] + static_cast<uint32_t>(size);
    } else {
      if (!test.decode(wire, wire_size)) {
        return -1;
      }
      checksum = checksum + static_cast<uint32_t>(wire_size);
    }
  }
  return esp_timer_get_time() - start;
}

// Calibrates batches and reports median plus range, not a best-case single run.
bool Measure(const benchmark::Case& test, bool encode, size_t wire_size) {
  size_t iterations = 1;
  while (true) {
    int64_t elapsed = Batch(test, encode, iterations, wire_size);
    if (elapsed < 0) {
      return false;
    }
    delay(1);
    if (elapsed >= kTargetBatchUs || iterations == kMaxIterations) {
      break;
    }
    iterations *= 2;
  }
  int64_t samples[kRounds];
  for (size_t round = 0; round < kRounds; ++round) {
    delay(1);
    samples[round] = Batch(test, encode, iterations, wire_size);
    if (samples[round] < 0) {
      return false;
    }
  }
  // Preserve unsorted batch durations; printing is outside all timed regions.
  for (size_t round = 0; round < kRounds; ++round) {
    Serial.printf("BATCH,%s,%s,%s,%u,%u,%lld\n", benchmark::kBackend, test.name,
                  encode ? "encode" : "decode",
                  static_cast<unsigned>(round + 1),
                  static_cast<unsigned>(iterations),
                  static_cast<long long>(samples[round]));
  }
  for (size_t i = 1; i < kRounds; ++i) {
    int64_t sample = samples[i];
    size_t j = i;
    while (j > 0 && samples[j - 1] > sample) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = sample;
  }
  Serial.printf("RESULT,%s,%s,%s,%u,%u,%u,%.4f,%.4f,%.4f,%08lx\n",
                benchmark::kBackend, test.name, encode ? "encode" : "decode",
                static_cast<unsigned>(wire_size),
                static_cast<unsigned>(test.object_size),
                static_cast<unsigned>(iterations),
                static_cast<double>(samples[kRounds / 2]) / iterations,
                static_cast<double>(samples[0]) / iterations,
                static_cast<double>(samples[kRounds - 1]) / iterations,
                static_cast<unsigned long>(benchmark::Hash(wire, wire_size)));
  Serial.flush();
  return true;
}

void Run() {
  Serial.printf("BEGIN,%s\n", benchmark::kBackend);
  Serial.printf(
      "META,chip=%s,revision=%u,cpu_mhz=%u,arduino=%s,idf=%s,compiler=%s,"
      "rounds=%u,target_batch_us=%lld,core=%u,opt=Os,lto=off,buffer_only=1,"
      "utf8=1,cplusplus=%ld,exceptions=0,rtti=0\n",
      ESP.getChipModel(), ESP.getChipRevision(), ESP.getCpuFreqMHz(),
      ESP_ARDUINO_VERSION_STR, ESP.getSdkVersion(), __VERSION__,
      static_cast<unsigned>(kRounds), static_cast<long long>(kTargetBatchUs),
      static_cast<unsigned>(xPortGetCoreID()), static_cast<long>(__cplusplus));
  Serial.println(
      "COLUMNS,backend,case,operation,wire_bytes,object_bytes,iterations,"
      "median_us,min_us,max_us,wire_fnv1a");
  for (const benchmark::Case& test : benchmark::kCases) {
    size_t size = 0;
    if (!benchmark::Validate(test, wire, size) || !Measure(test, true, size) ||
        !Measure(test, false, size)) {
      Serial.printf("FAIL,%s,%s\n", benchmark::kBackend, test.name);
      return;
    }
  }
  Serial.printf("END,%s,checksum=%lu,stack_free_bytes=%u\n",
                benchmark::kBackend, static_cast<unsigned long>(checksum),
                static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
  Serial.flush();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(3000);
  Run();
}

void loop() {
  if (Serial.available() > 0 && Serial.read() == 'r') {
    Run();
  }
  delay(20);
}

#else

// Host validation deliberately reports no MCU performance numbers.
int main() {
  uint8_t wire[benchmark::kCapacity];
  for (const benchmark::Case& test : benchmark::kCases) {
    size_t size = 0;
    if (!benchmark::Validate(test, wire, size)) {
      std::fprintf(stderr, "FAIL,%s,%s\n", benchmark::kBackend, test.name);
      return 1;
    }
    std::printf("WIRE,%s,", test.name);
    for (size_t i = 0; i < size; ++i) {
      std::printf("%02x", wire[i]);
    }
    std::printf("\n");
  }
}
#endif
