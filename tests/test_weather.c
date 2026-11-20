// The time and weather beside the globe: reading Open-Meteo's answer, and saying it.
#include <string.h>

#include "check.h"
#include "weather.h"

static const char ANSWER[] =
    "{\"latitude\":35.7,\"longitude\":139.69,\"utc_offset_seconds\":32400,\"timezone\":\"Asia/Tokyo\","
    "\"current_units\":{\"temperature_2m\":\"\u00b0C\"},"
    "\"current\":{\"time\":\"2026-11-20T21:00\",\"temperature_2m\":12.6,\"weather_code\":61,\"is_day\":0}}";

void testWeather(void)
{
    char url[WEATHER_URL_MAX];
    weatherUrl(35.704f, 139.6912f, url, sizeof(url));
    CHECK(strstr(url, "https://api.open-meteo.com/v1/forecast?latitude=35.70&longitude=139.69&") == url);
    CHECK(strstr(url, "current=temperature_2m,weather_code,is_day") && strstr(url, "timezone=auto"));

    Weather w;
    CHECK(weatherParse(ANSWER, &w));
    CHECK(w.utc_offset_s == 32400 && w.code == 61 && !w.day && w.temperature > 12.5f && w.temperature < 12.7f);
    CHECK(!weatherParse("{\"error\":true}", &w) && !weatherParse("not json", &w));

    CHECK(weatherKind(0) == WEATHER_CLEAR && weatherKind(2) == WEATHER_PARTLY_CLOUDY && weatherKind(3) == WEATHER_CLOUDY);
    CHECK(weatherKind(45) == WEATHER_FOG && weatherKind(53) == WEATHER_DRIZZLE && weatherKind(63) == WEATHER_RAIN);
    CHECK(weatherKind(81) == WEATHER_RAIN && weatherKind(73) == WEATHER_SNOW && weatherKind(95) == WEATHER_STORM);
    CHECK(!strcmp(weatherKindName(WEATHER_RAIN), "Rain"));

    char text[48];
    weatherTemperature(12.6f, text, sizeof(text));
    CHECK(!strcmp(text, "13 \xC2\xB0" "C  \xC2\xB7  55 \xC2\xB0" "F"));
    weatherTemperature(-3.4f, text, sizeof(text));
    CHECK(strstr(text, "-3 \xC2\xB0" "C") == text && strstr(text, "26 \xC2\xB0" "F"));

    // 12:00 UTC is 21:00 in Tokyo and 7:00 AM in New York.
    int64_t noon = 1795176000;  // 2026-11-20 12:00 UTC.
    weatherLocalTime(noon, 32400, true, text, sizeof(text));
    CHECK(!strcmp(text, "21:00"));
    weatherLocalTime(noon, -5 * 3600, false, text, sizeof(text));
    CHECK(!strcmp(text, "7:00 AM"));
    weatherLocalTime(noon + 13 * 3600, 0, false, text, sizeof(text));  // Past midnight.
    CHECK(!strcmp(text, "1:00 AM"));
    weatherLocalTime(noon, 0, false, text, sizeof(text));
    CHECK(!strcmp(text, "12:00 PM"));
}
