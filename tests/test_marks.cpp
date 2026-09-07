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
  require(shapeAt(DEFAULT_SHAPE_INDEX) == MarkShape::Blob,
          "default shape is the rounded blob");
  require(colorAt(DEFAULT_COLOR_INDEX) == MarkColor::Purple,
          "default colour is purple");
}

void testDefinitionsUseEnums() {
  require(MARK_SHAPE_COUNT == 8, "shape table matches the eight shapes offered by the app");
  require(MARK_COLOR_COUNT == 11, "colour table matches the eleven colours offered by the app");
  require(shapeAt(0) == MarkShape::Circle && shapeAt(7) == MarkShape::Drop,
          "shape table runs from circle to drop");
}

void testSelectionCycles() {
  uint8_t index = 0;
  for (size_t i = 1; i < MARK_SHAPE_COUNT; ++i) {
    index = nextShapeIndex(index);
    require(index == i, "shape selection advances");
  }
  require(nextShapeIndex(index) == 0, "shape selection wraps around");

  uint8_t c = 0;
  for (size_t i = 1; i < MARK_COLOR_COUNT; ++i) {
    c = nextColorIndex(c);
    require(c == i, "colour selection advances");
  }
  require(nextColorIndex(c) == 0, "colour selection wraps around");
}

void testInvalidSelectionFailsClosed() {
  require(normalizeShapeIndex(255) == DEFAULT_SHAPE_INDEX,
          "invalid persisted shape falls back to the default");
  require(normalizeColorIndex(255) == DEFAULT_COLOR_INDEX,
          "invalid persisted colour falls back to the default");
  require(shapeAt(255) == shapeAt(DEFAULT_SHAPE_INDEX),
          "invalid shape lookup returns the default");
  require(colorAt(255) == colorAt(DEFAULT_COLOR_INDEX),
          "invalid colour lookup returns the default");
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
