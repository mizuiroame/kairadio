// A minimal check macro shared by the host test files.
#pragma once

#include <stdio.h>

extern int g_failures;
extern int g_checks;

#define CHECK(cond)                                                                  \
    do {                                                                             \
        g_checks++;                                                                  \
        if (!(cond)) {                                                               \
            g_failures++;                                                            \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                            \
    } while (0)

#define TMP_ROOT "/tmp/kairadio_test"

void testIcy(void);
void testLinks(void);
void testStations(void);
void testRing(void);
void testHls(void);
void testDirectory(void);
void testLogos(void);
void testCategories(void);
void testLibrary(void);
void testWorldMap(void);
void testFlags(void);
void testIcons(void);
void testSettings(void);
