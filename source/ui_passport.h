// The listening passport: a panel over the World view with a stamp (flag,
// name, stations heard, first heard) for each country listened to, and the
// button on the World view that opens it.
#pragma once

#include "app.h"

#define UI_PASSPORT_ROWS 3  // Rows of stamps that show at once.

// The World view's button, at x on the subtitle's line.
void uiDrawPassportButton(const App *app, int x);
void uiDrawPassport(const App *app);
// How many rows of stamps the passport has.
int uiPassportRows(const App *app);
