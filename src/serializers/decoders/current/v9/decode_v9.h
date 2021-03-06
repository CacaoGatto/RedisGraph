/*
 * Copyright 2018-2020 Redis Labs Ltd. and Contributors
 *
 * This file is available under the Redis Labs Source Available License Agreement
 */

#pragma once

#include "../../../serializers_include.h"

#ifdef LABEL_DATABLOCK

typedef struct ConversionInfo {
    struct memkind *kind;
    uint64_t total_nodes;
    NodeID *ref_pool;
} ConversionInfo_t;

extern ConversionInfo_t *conv_info;

#endif

GraphContext *RdbLoadGraph_v9(RedisModuleIO *rdb);
void RdbLoadNodes_v9(RedisModuleIO *rdb, GraphContext *gc, uint64_t node_count);
void RdbLoadDeletedNodes_v9(RedisModuleIO *rdb, GraphContext *gc, uint64_t deleted_node_count);
void RdbLoadEdges_v9(RedisModuleIO *rdb, GraphContext *gc, uint64_t edge_count);
void RdbLoadDeletedEdges_v9(RedisModuleIO *rdb, GraphContext *gc, uint64_t deleted_edge_count);
void RdbLoadGraphSchema_v9(RedisModuleIO *rdb, GraphContext *gc);

