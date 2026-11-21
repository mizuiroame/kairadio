#include "weather.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "lang.h"

#define SECONDS_PER_DAY 86400
#define SECONDS_PER_HOUR 3600

void weatherUrl(float lat, float lon, char *out, size_t size)
{
    snprintf(out, size,
             "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
             "&current=temperature_2m,weather_code,is_day&timezone=auto",
             lat, lon);
}

bool weatherParse(const char *json, Weather *out)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *current = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *temperature = cJSON_GetObjectItemCaseSensitive(current, "temperature_2m");
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(current, "weather_code");
    const cJSON *day = cJSON_GetObjectItemCaseSensitive(current, "is_day");
    const cJSON *offset = cJSON_GetObjectItemCaseSensitive(root, "utc_offset_seconds");
    bool ok = cJSON_IsNumber(temperature) && cJSON_IsNumber(code) && cJSON_IsNumber(offset);
    if (ok) {
        out->temperature = (float)temperature->valuedouble;
        out->code = code->valueint;
        out->day = !cJSON_IsNumber(day) || day->valueint != 0;
        out->utc_offset_s = offset->valueint;
    }
    cJSON_Delete(root);
    return ok;
}

WeatherKind weatherKind(int code)
{
    // WMO codes: 0 clear, 1-2 partly cloudy, 3 overcast, 45-48 fog, 51-57
    // drizzle, 61-67 and 80-82 rain, 71-77 and 85-86 snow, 95-99 thunderstorm.
    if (code <= 0)
        return WEATHER_CLEAR;
    if (code <= 2)
        return WEATHER_PARTLY_CLOUDY;
    if (code == 3)
        return WEATHER_CLOUDY;
    if (code >= 45 && code <= 48)
        return WEATHER_FOG;
    if (code >= 51 && code <= 57)
        return WEATHER_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
        return WEATHER_RAIN;
    if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86))
        return WEATHER_SNOW;
    if (code >= 95)
        return WEATHER_STORM;
    return WEATHER_CLOUDY;
}

const char *weatherKindName(WeatherKind kind)
{
    static const char *const NAMES[] = {N_("Clear"),   N_("Partly cloudy"), N_("Cloudy"), N_("Fog"),
                                        N_("Drizzle"), N_("Rain"),          N_("Snow"),   N_("Thunderstorm")};
    return kind >= 0 && kind <= WEATHER_STORM ? T(NAMES[kind]) : "";
}

void weatherTemperature(float celsius, char *out, size_t size)
{
    int c = (int)lroundf(celsius), f = (int)lroundf(celsius * 9.0f / 5.0f + 32.0f);
    snprintf(out, size, "%d \xC2\xB0" "C  \xC2\xB7  %d \xC2\xB0" "F", c, f);
}

void weatherLocalTime(int64_t utc_s, int utc_offset_s, bool clock_24h, char *out, size_t size)
{
    int64_t local = utc_s + utc_offset_s;
    int seconds = (int)(((local % SECONDS_PER_DAY) + SECONDS_PER_DAY) % SECONDS_PER_DAY);
    int hour = seconds / SECONDS_PER_HOUR, minute = seconds % SECONDS_PER_HOUR / 60;
    if (clock_24h)
        snprintf(out, size, "%02d:%02d", hour, minute);
    else
        snprintf(out, size, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
}
