#pragma once

#include <stddef.h>
#include <stdint.h>

enum class MarkColor : uint8_t {
  Cyan,
  Green,
  Yellow,
  Magenta,
  Orange,
};

enum class MarkShape : uint8_t {
  Circle,
  Square,
  Triangle,
  Diamond,
  Hexagon,
};

struct MarkDefinition {
  MarkColor color;
  MarkShape shape;
};

constexpr MarkDefinition MARK_DEFINITIONS[] = {
  {MarkColor::Cyan, MarkShape::Circle},
  {MarkColor::Green, MarkShape::Square},
  {MarkColor::Yellow, MarkShape::Triangle},
  {MarkColor::Magenta, MarkShape::Diamond},
  {MarkColor::Orange, MarkShape::Hexagon},
};

constexpr size_t MARK_COUNT = sizeof(MARK_DEFINITIONS) / sizeof(MARK_DEFINITIONS[0]);
constexpr uint8_t DEFAULT_MARK_INDEX = 0;

constexpr uint8_t normalizeMarkIndex(uint8_t index) {
  return index < MARK_COUNT ? index : DEFAULT_MARK_INDEX;
}

constexpr uint8_t nextMarkIndex(uint8_t index) {
  return static_cast<uint8_t>((normalizeMarkIndex(index) + 1U) % MARK_COUNT);
}

constexpr const MarkDefinition& markAt(uint8_t index) {
  return MARK_DEFINITIONS[normalizeMarkIndex(index)];
}

inline const char* markColorName(MarkColor color) {
  switch (color) {
    case MarkColor::Cyan: return "cyan";
    case MarkColor::Green: return "green";
    case MarkColor::Yellow: return "yellow";
    case MarkColor::Magenta: return "magenta";
    case MarkColor::Orange: return "orange";
  }
  return "unknown";
}

inline const char* markShapeName(MarkShape shape) {
  switch (shape) {
    case MarkShape::Circle: return "circle";
    case MarkShape::Square: return "square";
    case MarkShape::Triangle: return "triangle";
    case MarkShape::Diamond: return "diamond";
    case MarkShape::Hexagon: return "hexagon";
  }
  return "unknown";
}
