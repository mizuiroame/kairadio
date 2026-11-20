// The time and weather where a country's stations are, beside the globe:
// from Open-Meteo (free, no account), asked with the country's middle on the
// map and nothing else. Reading its answer is pure logic; weatherfetch.c asks.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WEATHER_URL_MAX 256

typedef enum {
    WEATHER_CLEAR,
    WEATHER_PARTLY_CLOUDY,
    WEATHER_CLOUDY,
    WEATHER_FOG,
    WEATHER_DRIZZLE,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_STORM,
} WeatherKind;

typedef struct {
    float temperature;     // Degrees Celsius.
    int code;              // WMO weather code, as Open-Meteo gives it.
    bool day;
    int utc_offset_s;      // The place's time: UTC plus this.
} Weather;

// The address that asks for the weather now at (lat, lon), and the place's time zone.
void weatherUrl(float lat, float lon, char *out, size_t size);
// Reads Open-Meteo's answer. False if it isn't one.
bool weatherParse(const char *json, Weather *out);
WeatherKind weatherKind(int code);
// "Clear", "Partly cloudy", "Rain"...
const char *weatherKindName(WeatherKind kind);
// "21 °C · 70 °F".
void weatherTemperature(float celsius, char *out, size_t size);
// The place's time now ("14:32" or "2:32 PM") from the time now in UTC.
void weatherLocalTime(int64_t utc_s, int utc_offset_s, bool clock_24h, char *out, size_t size);
