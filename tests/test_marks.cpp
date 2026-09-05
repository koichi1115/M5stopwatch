#include <stdio.h>
#include <stdlib.h>

#include "mark_config.h"

namespace {

void require(bool condition, const char* name) {
  if (!condition) {
    fprintf(stderr, "[FAIL] %s\n", name);
    exit(1);
  }
  printf("[PASS] %s\n", name);
}

void testDefaultSelection() {
  require(DEFAULT_MARK_INDEX == 0, "default selection is the first mark");
  require(markAt(DEFAULT_MARK_INDEX).color == MarkColor::Cyan,
          "default selection has the configured color");
  require(markAt(DEFAULT_MARK_INDEX).shape == MarkShape::Circle,
          "default selection has the configured shape");
}

void testDefinitionsUseEnums() {
  require(MARK_COUNT == 5, "mark table exposes five local definitions");
  require(markAt(1).color == MarkColor::Green &&
              markAt(1).shape == MarkShape::Square,
          "mark definitions pair enumerated color and shape");
}

void testSelectionCycles() {
  uint8_t index = DEFAULT_MARK_INDEX;
  for (size_t i = 1; i < MARK_COUNT; ++i) {
    index = nextMarkIndex(index);
    require(index == i, "selection advances to the next mark");
  }
  require(nextMarkIndex(index) == DEFAULT_MARK_INDEX,
          "selection wraps from the last mark to the first");
}

void testInvalidSelectionFailsClosed() {
  require(normalizeMarkIndex(255) == DEFAULT_MARK_INDEX,
          "invalid persisted selection falls back to default");
  require(&markAt(255) == &markAt(DEFAULT_MARK_INDEX),
          "invalid lookup returns the default definition");
  require(nextMarkIndex(255) == 1,
          "cycling an invalid selection starts from the default");
}

}  // namespace

int main() {
  testDefaultSelection();
  testDefinitionsUseEnums();
  testSelectionCycles();
  testInvalidSelectionFailsClosed();
  puts("All mark tests passed.");
  return 0;
}
