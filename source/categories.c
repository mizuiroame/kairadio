#include "categories.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"

static const char *stringField(const cJSON *item, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}

bool categoriesParse(const char *json, CategoryList *out)
{
    out->items = NULL;
    out->count = 0;
    cJSON *root = cJSON_Parse(json);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    int n = cJSON_GetArraySize(root);
    if (n > 0 && !(out->items = malloc((size_t)n * sizeof(Category)))) {
        cJSON_Delete(root);
        return false;
    }
    const cJSON *item;
    cJSON_ArrayForEach(item, root)
    {
        const char *name = stringField(item, "name");
        const cJSON *count = cJSON_GetObjectItemCaseSensitive(item, "stationcount");
        int stations = cJSON_IsNumber(count) ? count->valueint : 0;
        if (!name[0] || strlen(name) >= CATEGORY_NAME_MAX || stations <= 0)
            continue;
        Category *c = &out->items[out->count++];
        snprintf(c->name, sizeof(c->name), "%s", name);
        const char *code = stringField(item, "iso_3166_1");
        if (!code[0])
            code = stringField(item, "iso_639");
        snprintf(c->code, sizeof(c->code), "%s", strlen(code) < sizeof(c->code) ? code : "");
        c->count = stations;
    }
    cJSON_Delete(root);
    return true;
}

void categoryListFree(CategoryList *list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
}

// The name as sorted: without a leading "The ".
static const char *sortName(const Category *c)
{
    return strncasecmp(c->name, "the ", 4) == 0 ? c->name + 4 : c->name;
}

static int byName(const void *a, const void *b)
{
    return strcasecmp(sortName(a), sortName(b));
}

static int byMost(const void *a, const void *b)
{
    int ca = ((const Category *)a)->count, cb = ((const Category *)b)->count;
    return ca != cb ? (ca < cb ? 1 : -1) : byName(a, b);
}

static int byFewest(const void *a, const void *b)
{
    int ca = ((const Category *)a)->count, cb = ((const Category *)b)->count;
    return ca != cb ? (ca < cb ? -1 : 1) : byName(a, b);
}

static int byNameReversed(const void *a, const void *b)
{
    return byName(b, a);
}

void categorySort(CategoryList *list, CategorySort sort)
{
    static int (*const COMPARE[SORT_COUNT])(const void *, const void *) = {byMost, byFewest, byName, byNameReversed};
    if (list->count > 1 && sort < SORT_COUNT)
        qsort(list->items, list->count, sizeof(Category), COMPARE[sort]);
}

const char *categorySortLabel(CategorySort sort)
{
    static const char *const LABELS[SORT_COUNT] = {"Most stations", "Fewest stations", "A to Z", "Z to A"};
    return sort < SORT_COUNT ? LABELS[sort] : "";
}

void categoryLabel(const Category *c, char *out, size_t size)
{
    snprintf(out, size, "%s", c->name);
    bool start = true;
    for (char *p = out; *p; p++) {
        if (start && *p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
        start = *p == ' ' || *p == '-' || *p == '/' || *p == '&';
    }
}
