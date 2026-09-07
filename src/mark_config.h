// Bot の見た目のバリエーション。実物 (Grok Bot の作成画面) に合わせた 8 形 x 11 色。
//   形: 丸 / ブロブ / 角丸四角 / 横長カプセル / 三角 / 六角 / もこもこ / しずく
//   色: 黒・茶・赤・朱・橙・緑・青緑・青・紫・桃・灰
// ここは描画に依存しないので、ホスト側のテスト (tests/test_marks.cpp) からも読める。
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class MarkColor : uint8_t {
  Black,
  Brown,
  Red,
  Vermilion,
  Amber,
  Green,
  Teal,
  Blue,
  Purple,
  Pink,
  Gray,
};

enum class MarkShape : uint8_t {
  Circle,         // 丸 (少し横長)
  Blob,           // 角の取れた塊 (既定)
  RoundedSquare,  // 角丸四角
  Pill,           // 横長カプセル
  Triangle,       // 角の丸い三角
  Hexagon,        // 六角
  Cloud,          // もこもこ
  Drop,           // しずく
};

constexpr MarkShape MARK_SHAPES[] = {
  MarkShape::Circle, MarkShape::Blob,   MarkShape::RoundedSquare, MarkShape::Pill,
  MarkShape::Triangle, MarkShape::Hexagon, MarkShape::Cloud,      MarkShape::Drop,
};
constexpr size_t MARK_SHAPE_COUNT = sizeof(MARK_SHAPES) / sizeof(MARK_SHAPES[0]);

constexpr MarkColor MARK_COLORS[] = {
  MarkColor::Black, MarkColor::Brown, MarkColor::Red,   MarkColor::Vermilion,
  MarkColor::Amber, MarkColor::Green, MarkColor::Teal,  MarkColor::Blue,
  MarkColor::Purple, MarkColor::Pink, MarkColor::Gray,
};
constexpr size_t MARK_COLOR_COUNT = sizeof(MARK_COLORS) / sizeof(MARK_COLORS[0]);

// 既定は実物の初期選択に合わせて「紫のブロブ」
constexpr uint8_t DEFAULT_SHAPE_INDEX = 1;
constexpr uint8_t DEFAULT_COLOR_INDEX = 8;

constexpr uint8_t normalizeShapeIndex(uint8_t i) { return i < MARK_SHAPE_COUNT ? i : DEFAULT_SHAPE_INDEX; }
constexpr uint8_t normalizeColorIndex(uint8_t i) { return i < MARK_COLOR_COUNT ? i : DEFAULT_COLOR_INDEX; }
constexpr uint8_t nextShapeIndex(uint8_t i) { return (uint8_t)((normalizeShapeIndex(i) + 1U) % MARK_SHAPE_COUNT); }
constexpr uint8_t nextColorIndex(uint8_t i) { return (uint8_t)((normalizeColorIndex(i) + 1U) % MARK_COLOR_COUNT); }
constexpr MarkShape shapeAt(uint8_t i) { return MARK_SHAPES[normalizeShapeIndex(i)]; }
constexpr MarkColor colorAt(uint8_t i) { return MARK_COLORS[normalizeColorIndex(i)]; }

inline const char* markColorName(MarkColor color) {
  switch (color) {
    case MarkColor::Black:     return "black";
    case MarkColor::Brown:     return "brown";
    case MarkColor::Red:       return "red";
    case MarkColor::Vermilion: return "vermilion";
    case MarkColor::Amber:     return "amber";
    case MarkColor::Green:     return "green";
    case MarkColor::Teal:      return "teal";
    case MarkColor::Blue:      return "blue";
    case MarkColor::Purple:    return "purple";
    case MarkColor::Pink:      return "pink";
    case MarkColor::Gray:      return "gray";
  }
  return "unknown";
}

inline const char* markShapeName(MarkShape shape) {
  switch (shape) {
    case MarkShape::Circle:        return "circle";
    case MarkShape::Blob:          return "blob";
    case MarkShape::RoundedSquare: return "rounded square";
    case MarkShape::Pill:          return "pill";
    case MarkShape::Triangle:      return "triangle";
    case MarkShape::Hexagon:       return "hexagon";
    case MarkShape::Cloud:         return "cloud";
    case MarkShape::Drop:          return "drop";
  }
  return "unknown";
}
