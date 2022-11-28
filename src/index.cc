#include "index.h"
#include "parser.h"
#include <string.h>

#define REDISMODULE_API extern

extern "C"
{
#include "redismodule.h"
}

const char *INDEX_META_SUFFIX = "meta";
const char *INDEX_POLYGONS_SUFFIX = "polygons";
const char *INDEX_CELLS_SUFFIX = "cells";
const char *POLYGON_CELLINFO_SUFFIX = "cellinfo";
const char ENTITY_DELIM = ':';
const char *INDEX_PARAMS_KEY = "params";
const char *INDEX_PARAMS_VALUE = "<index>";

std::unique_ptr<S2Polygon> ParsePolygon(RedisModuleCtx *ctx, RedisModuleString *body)
{
    std::unique_ptr<S2Polygon> polygon(nullptr);
    size_t len;
    const char *cBody = RedisModule_StringPtrLen(body, &len);
    int ret = ParseS2Polygon(cBody, &polygon);
    if (ret != 0)
    {
        return nullptr;
    }
    return std::move(polygon);
}

std::unique_ptr<S2LatLng> ParseLatLng(RedisModuleCtx *ctx, RedisModuleString *body)
{
    std::unique_ptr<S2LatLng> latLng(nullptr);
    size_t len;
    const char *cBody = RedisModule_StringPtrLen(body, &len);
    int ret = ParseS2LatLng(cBody, &latLng);
    if (ret != 0)
    {
        return nullptr;
    }
    return std::move(latLng);
}

RedisModuleString *CreateIndexMetaHashKey(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s", cIndexName, ENTITY_DELIM, INDEX_META_SUFFIX);
}

RedisModuleString *CreateIndexPolygonsHashKey(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s", cIndexName, ENTITY_DELIM, INDEX_POLYGONS_SUFFIX);
}

RedisModuleString *CreateIndexCellsSetKey(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *cellId)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    const char *cCellId = RedisModule_StringPtrLen(cellId, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s%c%s", cIndexName, ENTITY_DELIM, INDEX_CELLS_SUFFIX, ENTITY_DELIM, cCellId);
}

RedisModuleString *CreateIndexCellsSetKeyCStr(RedisModuleCtx *ctx, RedisModuleString *indexName, const char *cCellId)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s%c%s", cIndexName, ENTITY_DELIM, INDEX_CELLS_SUFFIX, ENTITY_DELIM, cCellId);
}

RedisModuleString *CreatePolygonCellInfoSetKey(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    const char *cPolygonName = RedisModule_StringPtrLen(polygonName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s%c%s", cIndexName, ENTITY_DELIM, POLYGON_CELLINFO_SUFFIX, ENTITY_DELIM, cPolygonName);
}

RedisModuleString *CreatePolygonCellInfoPattern(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s%c*", cIndexName, ENTITY_DELIM, POLYGON_CELLINFO_SUFFIX, ENTITY_DELIM);
}

RedisModuleString *CreateIndexCellsPattern(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    return RedisModule_CreateStringPrintf(ctx, "%s%c%s%c*", cIndexName, ENTITY_DELIM, INDEX_CELLS_SUFFIX, ENTITY_DELIM);
}

int ValidateIndex(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    RedisModuleString *metaHashKey = CreateIndexMetaHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HGET", "sc", metaHashKey, INDEX_PARAMS_KEY);
    RedisModule_Log(ctx, "debug", "RedisModule_Call reply type %d", RedisModule_CallReplyType(reply));
    int type = RedisModule_CallReplyType(reply);
    if (type == REDISMODULE_REPLY_NULL || type == REDISMODULE_REPLY_UNKNOWN)
    {
        return S2GEO_ERR_NO_SUCH_INDEX;
    }
    if (RedisModule_CallReplyType(reply) != REDISMODULE_REPLY_STRING)
    {
        return S2GEO_ERR_INVALID_INDEX;
    }

    RedisModuleString *value = RedisModule_CreateStringFromCallReply(reply);
    size_t len;
    const char *cValue = RedisModule_StringPtrLen(value, &len);
    if (strcmp(INDEX_PARAMS_VALUE, cValue) != 0)
    {
        return S2GEO_ERR_INVALID_INDEX;
    }

    return 0;
}

int ValidateEntityName(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    size_t len;
    const char *cIndexName = RedisModule_StringPtrLen(indexName, &len);
    if (len == 0 || strchr(cIndexName, (int)ENTITY_DELIM) != 0)
    {
        return S2GEO_ERR_INVALID_ENTITY_NAME;
    }
    return 0;
}

int CreateIndex(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    RedisModuleString *metaHashKey = CreateIndexMetaHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HSET", "scc", metaHashKey, INDEX_PARAMS_KEY, INDEX_PARAMS_VALUE);
    return 0;
}

int DeleteIndex(RedisModuleCtx *ctx, RedisModuleString *indexName)
{
    RedisModuleCallReply *polygons;
    int ret = ListPolygons(ctx, indexName, &polygons);
    if (ret != 0)
    {
        return ret;
    }
    size_t len = RedisModule_CallReplyLength(polygons);
    for (size_t i = 0; i < len; i++)
    {
        RedisModuleString *polygonName = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(polygons, i));
        ret = DeletePolygonBody(ctx, indexName, polygonName);
        if (ret != 0)
        {
            return S2GEO_ERR_UNKNOWN;
        }

        ret = DeletePolygonCells(ctx, indexName, polygonName);
        if (ret == S2GEO_ERR_NO_SUCH_POLYGON)
        {
            return S2GEO_ERR_UNKNOWN;
        }
        if (ret != 0)
        {
            return S2GEO_ERR_UNKNOWN;
        }
    }

    RedisModuleString *metaHashKey = CreateIndexMetaHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "DEL", "s", metaHashKey);
    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR)
    {
        return S2GEO_ERR_UNKNOWN;
    }

    return 0;
}

int ListPolygons(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleCallReply **polygons)
{
    RedisModuleString *polygonsHash = CreateIndexPolygonsHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HKEYS", "s", polygonsHash);
    ;
    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR)
    {
        return S2GEO_ERR_UNKNOWN;
    }

    *polygons = reply;
    return 0;
}

int SetPolygonBody(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName, RedisModuleString *polygonBody)
{
    RedisModuleString *polygonsHash = CreateIndexPolygonsHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HSET", "sss", polygonsHash, polygonName, polygonBody);
    return 0;
}

int GetPolygonBody(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName, RedisModuleString **output)
{
    RedisModuleString *polygonsHash = CreateIndexPolygonsHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HGET", "ss", polygonsHash, polygonName);
    if (RedisModule_CallReplyType(reply) != REDISMODULE_REPLY_STRING)
    {
        return S2GEO_ERR_NO_SUCH_POLYGON;
    }
    *output = RedisModule_CreateStringFromCallReply(reply);
    return 0;
}

int GetPolygonBodies(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString **polygonNames, size_t polygonCount, RedisModuleCallReply **output)
{
    RedisModuleString *polygonsHash = CreateIndexPolygonsHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HMGET", "sv", polygonsHash, polygonNames, polygonCount);
    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR)
    {
        return S2GEO_ERR_UNKNOWN;
    }
    *output = reply;
    return 0;
}

int DeletePolygonBody(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName)
{
    RedisModuleString *polygonsHash = CreateIndexPolygonsHashKey(ctx, indexName);
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "HDEL", "ss", polygonsHash, polygonName);
    return 0;
}

int SetPolygonCells(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName, const std::unordered_set<std::string> &cells)
{
    RedisModuleString *cellInfoSetKey = CreatePolygonCellInfoSetKey(ctx, indexName, polygonName);
    std::unique_ptr<RedisModuleString *[]> cellStrings(new RedisModuleString *[cells.size()]);

    int idx = 0;
    for (const auto &cell : cells)
    {
        cellStrings.get()[idx] = RedisModule_CreateString(ctx, cell.c_str(), cell.length());
        idx++;
    }

    RedisModuleCallReply *reply = RedisModule_Call(ctx, "SADD", "sv", cellInfoSetKey, cellStrings.get(), cells.size());
    for (size_t idx = 0; idx < cells.size(); idx++)
    {
        RedisModuleString *indexCellsHashKey = CreateIndexCellsSetKey(ctx, indexName, cellStrings.get()[idx]);
        RedisModule_Call(ctx, "SADD", "ss", indexCellsHashKey, polygonName);
    }
    return 0;
}

int DeletePolygonCells(RedisModuleCtx *ctx, RedisModuleString *indexName, RedisModuleString *polygonName)
{
    RedisModuleString *cellInfoSetKey = CreatePolygonCellInfoSetKey(ctx, indexName, polygonName);
    RedisModuleCallReply *cells = RedisModule_Call(ctx, "SMEMBERS", "s", cellInfoSetKey);
    size_t len = RedisModule_CallReplyLength(cells);
    if (len == 0)
    {
        return S2GEO_ERR_NO_SUCH_POLYGON;
    }

    RedisModule_Call(ctx, "DEL", "s", cellInfoSetKey);

    for (size_t idx = 0; idx < len; idx++)
    {
        RedisModuleString *cellId = RedisModule_CreateStringFromCallReply(RedisModule_CallReplyArrayElement(cells, idx));
        RedisModuleString *indexCellsHashKey = CreateIndexCellsSetKey(ctx, indexName, cellId);
        RedisModule_Call(ctx, "SREM", "ss", indexCellsHashKey, polygonName);
    }
    return 0;
}

int GetPolygonsInCells(RedisModuleCtx *ctx, RedisModuleString *indexName, const std::unordered_set<std::string> &cells, RedisModuleCallReply **polygons)
{
    std::unique_ptr<RedisModuleString *[]> cellStrings(new RedisModuleString *[cells.size()]);

    int idx = 0;
    for (const auto &cell : cells)
    {
        cellStrings.get()[idx] = CreateIndexCellsSetKeyCStr(ctx, indexName, cell.c_str());
        idx++;
    }
    RedisModuleCallReply *reply = RedisModule_Call(ctx, "SUNION", "v", cellStrings.get(), cells.size());
    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR)
    {
        return S2GEO_ERR_UNKNOWN;
    }

    *polygons = reply;
    return 0;
}
