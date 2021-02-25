/*
* Copyright 2018-2021 Redis Labs Ltd. and Contributors
*
* This file is available under the Redis Labs Source Available License Agreement
*/

#include "json_encoder.h"
#include "sds/sds.h"
#include "rmalloc.h"
#include "strutil.h"
#include "../value.h"
#include "../errors.h"
#include "../query_ctx.h"
#include "../graph/graphcontext.h"
#include "../graph/entities/node.h"
#include "../graph/entities/edge.h"
#include "../datatypes/datatypes.h"

// Forward declaration
sds _JsonEncoder_SIValue(SIValue v, sds s);

static inline sds _JsonEncoder_String(SIValue v, sds s) {
	return sdscatfmt(s, "\"%s\"", v.stringval);
}

static sds _JsonEncoder_Properties(const GraphEntity *ge, sds s) {
	const AttributeSet set = GraphEntity_GetAttributeSet(ge);

	s = sdscat(s, "\"properties\": {");
	uint prop_count = AttributeSet_AttributeCount(set);
	GraphContext *gc = QueryCtx_GetGraphCtx();
	for(uint i = 0; i < prop_count; i ++) {
		SIValue v;
		Attribute_ID k;
		AttributeSet_GetAttrIdx(set, i, &v, &k);
		const char *key = GraphContext_GetAttributeString(gc, k);
		s = sdscatfmt(s, "\"%s\": ", key);
		s = _JsonEncoder_SIValue(v, s);
		if(i < prop_count - 1) s = sdscat(s, ", ");
	}
	s = sdscat(s, "}");
	return s;
}

static sds _JsonEncoder_Node(const Node *n, sds s) {
	s = sdscatfmt(s, "\"id\": %U", ENTITY_GET_ID(n));
	s = sdscat(s, ", \"labels\": [");
	// Label data will only be populated if provided by the query string.
	const char *label = NULL;
	GraphContext *gc = QueryCtx_GetGraphCtx();
	int labelID = Graph_GetNodeLabel(gc->g, ENTITY_GET_ID(n));
	if(labelID != GRAPH_NO_LABEL) {
		Schema *schema = GraphContext_GetSchemaByID(gc, labelID, SCHEMA_NODE);
		ASSERT(schema);
		label = Schema_GetName(schema);
		ASSERT(label);
		s = sdscatfmt(s, "\"%s\"", label);
	}
	s = sdscat(s, "], ");
	s = _JsonEncoder_Properties((const GraphEntity *)n, s);
	return s;
}

static sds _JsonEncoder_Edge(Edge *e, sds s) {
	s = sdscatfmt(s, "\"id\": %U", ENTITY_GET_ID(e));
	GraphContext *gc = QueryCtx_GetGraphCtx();
	// Retrieve reltype data.
	int id = Graph_GetEdgeRelation(gc->g, e);
	Schema *schema = GraphContext_GetSchemaByID(gc, id, SCHEMA_EDGE);
	ASSERT(schema);
	const char *relationship = Schema_GetName(schema);
	ASSERT(relationship);
	s = sdscatfmt(s, ", \"relationship\": \"%s\", ", relationship);

	s = _JsonEncoder_Properties((const GraphEntity *)e, s);

	s = sdscat(s, ", \"start\": {");
	// Retrieve source node data.
	Node src;
	Graph_GetNode(gc->g, e->srcNodeID, &src);
	s = _JsonEncoder_Node(&src, s);

	s = sdscat(s, "}, \"end\": {");
	// Retrieve dest node data.
	Node dest;
	Graph_GetNode(gc->g, e->destNodeID, &dest);
	s = _JsonEncoder_Node(&dest, s);

	s = sdscat(s, "}");
	return s;
}

static sds _JsonEncoder_GraphEntity(GraphEntity *ge, sds s, GraphEntityType type) {
	switch(type) {
	case GETYPE_NODE:
		s = sdscat(s, "{\"type\": \"node\", ");
		s = _JsonEncoder_Node((const Node *)ge, s);
		break;
	case GETYPE_EDGE:
		s = sdscat(s, "{\"type\": \"relationship\", ");
		s = _JsonEncoder_Edge((Edge *)ge, s);
		break;
	default:
		ASSERT(false);
	}
	s = sdscat(s, "}");
	return s;
}

static sds _JsonEncoder_Path(SIValue p, sds s) {
	// open path with "["
	s = sdscat(s, "[");

	size_t nodeCount = SIPath_NodeCount(p);
	for(size_t i = 0; i < nodeCount - 1; i ++) {
		// write the next value
		SIValue node = SIPath_GetNode(p, i);
		s = _JsonEncoder_GraphEntity((GraphEntity *)&node, s, GETYPE_NODE);
		s = sdscat(s, ", ");
		SIValue edge = SIPath_GetRelationship(p, i);
		s = _JsonEncoder_GraphEntity((GraphEntity *)&edge, s, GETYPE_EDGE);
		s = sdscat(s, ", ");
	}
	// Handle last node.
	if(nodeCount > 0) {
		SIValue node = SIPath_GetNode(p, nodeCount - 1);
		s = _JsonEncoder_GraphEntity((GraphEntity *)&node, s, GETYPE_NODE);
	}

	// close array with "]"
	s = sdscat(s, "]");
	return s;
}

static sds _JsonEncoder_Array(SIValue list, sds s) {
	// open array with "["
	s = sdscat(s, "[");
	uint arrayLen = SIArray_Length(list);
	for(uint i = 0; i < arrayLen; i ++) {
		// write the next value
		s = _JsonEncoder_SIValue(SIArray_Get(list, i), s);
		// if it is not the last element, add ", "
		if(i != arrayLen - 1) s = sdscat(s, ", ");
	}

	// close array with "]"
	s = sdscat(s, "]");
	return s;
}

static sds _JsonEncoder_Map(SIValue map, sds s) {
	ASSERT(SI_TYPE(map) & T_MAP);

	// "{" marks the beginning of a map
	s = sdscat(s, "{");

	uint key_count = Map_KeyCount(map);
	for(uint i = 0; i < key_count; i ++) {
		Pair p = map.map[i];
		// write the next key/value pair
		s = _JsonEncoder_String(p.key, s);
		s = sdscat(s, ": ");
		s = _JsonEncoder_SIValue(p.val, s);
		// if this is not the last element, add ", "
		if(i != key_count - 1) s = sdscat(s, ", ");
	}

	// "}" marks the end of a map
	s = sdscat(s, "}");
	return s;
}

sds _JsonEncoder_SIValue(SIValue v, sds s) {
	switch(v.type) {
	case T_STRING:
		s = _JsonEncoder_String(v, s);
		break;
	case T_INT64:
		s = sdscatfmt(s, "%I", v.longval);
		break;
	case T_BOOL:
		if(v.longval) s = sdscat(s, "true");
		else s = sdscat(s, "false");
		break;
	case T_DOUBLE:
		s = sdscatprintf(s, "%f", v.doubleval);
		break;
	case T_NODE:
		s = _JsonEncoder_GraphEntity(v.ptrval, s, GETYPE_NODE);
		break;
	case T_EDGE:
		s = _JsonEncoder_GraphEntity(v.ptrval, s, GETYPE_EDGE);
		break;
	case T_ARRAY:
		s = _JsonEncoder_Array(v, s);
		break;
	case T_MAP:
		s = _JsonEncoder_Map(v, s);
		break;
	case T_PATH:
		s = _JsonEncoder_Path(v, s);
		break;
	case T_NULL:
		s = sdscat(s, "null");
		break;
	default:
		// unrecognized type
		ErrorCtx_RaiseRuntimeException("JSON encoder encountered unrecognized type: %d\n", v.type);
		ASSERT(false);
		break;
	}
	return s;
}

char *JsonEncoder_SIValue(SIValue v) {
	// Create an empty sds string.
	sds s = sdsempty();
	// Populate the sds string with encoded data.
	s = _JsonEncoder_SIValue(v, s);
	// Duplicate the sds string into a standard C string.
	char *retval = rm_strdup(s);
	// Free the sds string.
	sdsfree(s);
	return retval;
}

