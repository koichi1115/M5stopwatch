#pragma once

#include <M5Unified.h>

#include "mark_config.h"

inline uint16_t markColorRgb565(MarkColor color) {
  switch (color) {
    case MarkColor::Cyan: return 0x07FF;
    case MarkColor::Green: return 0x07E0;
    case MarkColor::Yellow: return 0xFFE0;
    case MarkColor::Magenta: return 0xF81F;
    case MarkColor::Orange: return 0xFD20;
  }
  return 0xFFFF;
}

inline void drawBotMark(
    M5Canvas& canvas, int centerX, int centerY, int radius,
    const MarkDefinition& mark) {
  const uint16_t color = markColorRgb565(mark.color);

  switch (mark.shape) {
    case MarkShape::Circle:
      canvas.fillCircle(centerX, centerY, radius, color);
      break;
    case MarkShape::Square:
      canvas.fillRect(
          centerX - radius, centerY - radius, radius * 2, radius * 2, color);
      break;
    case MarkShape::Triangle:
      canvas.fillTriangle(
          centerX, centerY - radius,
          centerX - radius, centerY + radius,
          centerX + radius, centerY + radius,
          color);
      break;
    case MarkShape::Diamond:
      canvas.fillTriangle(
          centerX, centerY - radius,
          centerX - radius, centerY,
          centerX, centerY + radius,
          color);
      canvas.fillTriangle(
          centerX, centerY - radius,
          centerX + radius, centerY,
          centerX, centerY + radius,
          color);
      break;
    case MarkShape::Hexagon: {
      const int halfRadius = radius / 2;
      canvas.fillTriangle(
          centerX - radius, centerY,
          centerX - halfRadius, centerY - radius,
          centerX - halfRadius, centerY + radius,
          color);
      canvas.fillRect(
          centerX - halfRadius, centerY - radius,
          radius, radius * 2, color);
      canvas.fillTriangle(
          centerX + radius, centerY,
          centerX + halfRadius, centerY - radius,
          centerX + halfRadius, centerY + radius,
          color);
      break;
    }
  }
}
