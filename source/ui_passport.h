// The listening passport: a panel over the World view with a stamp (flag,
// name, stations heard, first heard) for each country listened to, and the
// button on the World view that opens it.
#pragma once

#include "app.h"
#include "worldmap.h"

#define UI_PASSPORT_ROWS 3  // Rows of stamps that show at once.
// The World view's card that opens it: at the bottom right, beside the globe;
// on the flat map at the top right, over the Arctic (the bottom right is New Zealand).
#define UI_PASSPORT_CARD_W 260
#define UI_PASSPORT_CARD_H 78
#define UI_PASSPORT_CARD_X (MAP_X + MAP_W - UI_PASSPORT_CARD_W)
#define UI_PASSPORT_CARD_Y_GLOBE (MAP_Y + MAP_H - UI_PASSPORT_CARD_H - 8)
#define UI_PASSPORT_CARD_Y_FLAT (MAP_Y + 8)

// The World view's card: the passport's countries, its newest flags, and LS.
void uiDrawPassportCard(const App *app);
void uiDrawPassport(const App *app);
// How many rows of stamps the passport has.
int uiPassportRows(const App *app);
