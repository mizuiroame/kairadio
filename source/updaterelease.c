#include "updaterelease.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static const char *text(const cJSON *object, const char *name)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

bool updateReadRelease(const char *json, const char *repo, const char *nro_name, UpdateRelease *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const char *tag = text(root, "tag_name");
    bool ok = cJSON_IsObject(root) && tag && strlen(tag) < sizeof(out->tag) && updateIsNewer(tag, "0.0.0") &&
              !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "prerelease")) &&
              !cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "draft"));
    const cJSON *asset = NULL;
    if (ok) {
        const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets"), *each;
        cJSON_ArrayForEach(each, assets) {
            const char *name = text(each, "name");
            if (cJSON_IsObject(each) && name && strcmp(name, nro_name) == 0) {
                asset = each;
                break;
            }
        }
        ok = asset != NULL;
    }
    if (ok) {
        const char *url = text(asset, "browser_download_url");
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(asset, "size");
        ok = updateUrlOk(url, repo, tag, nro_name) && updateDigest(text(asset, "digest"), out->sha256) &&
             cJSON_IsNumber(size) && size->valuedouble > 0 && size->valuedouble <= UPDATE_NRO_MAX;
        if (ok) {
            snprintf(out->tag, sizeof(out->tag), "%s", tag);
            snprintf(out->url, sizeof(out->url), "%s", url);
            out->size = (int64_t)size->valuedouble;
        }
    }
    cJSON_Delete(root);
    if (!ok)
        memset(out, 0, sizeof(*out));
    return ok;
}
