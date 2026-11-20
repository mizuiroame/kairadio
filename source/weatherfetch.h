// Asks Open-Meteo for a country's weather on a thread of its own, and keeps
// what came for a while, so going back and forth between countries asks once.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "weather.h"

#define WEATHER_FRESH_NS (30ull * 60 * 1000000000ull)  // Asked again after this long.

typedef enum { WEATHER_NONE, WEATHER_WAITING, WEATHER_READY, WEATHER_FAILED } WeatherState;

bool weatherFetchStart(void);
void weatherFetchStop(void);
// Asks for the weather at (lat, lon) for key (a country's code), unless it's
// fresh or on its way. The newest ask replaces one still waiting.
void weatherFetchAsk(const char *key, float lat, float lon, uint64_t now_ns);
// What there is for key; out gets the weather when READY.
WeatherState weatherFetchGet(const char *key, Weather *out);
