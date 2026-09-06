// バッジの「気分」。まっくろくろすけと Bot の両方の描画から参照する
#pragma once
#include <stdint.h>

enum class Mood : uint8_t { Awake, Drowsy, Sleep, Surprised, Happy, Startled, Music, Bite };
