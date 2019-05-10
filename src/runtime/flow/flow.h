#ifndef ENCORE_FLOW_H
#define ENCORE_FLOW_H

#include "pony.h"

typedef struct flow_s flow_t;
typedef enum result_type_e result_type_t;
extern pony_type_t flow_type;

/**
 * Create a new flow with result type @a result_type.
 */
flow_t* flow_mk(pony_ctx_t** ctx, pony_type_t* type, result_type_t result_type);

/**
 * Create a new flow, fulfiled with value @a value.
 * 
 * This is used when casting values from T to Flow[T]. For example, consider a 
 * function returning Flow[int] ; if a final statement in the function returns a
 * value of type int, we need to transform this value into a Flow. This is where
 * this function comes in. 
 * 
 * The result_type of the resulting future is set to VALUE.
 */
flow_t* flow_mk_from_value(pony_ctx_t** cttx, pony_type_t* type, 
                           encore_arg_t value);

encore_arg_t flow_get(pony_ctx_t** cttx, pony_type_t* type, flow_t* flow);
void flow_fulfil(pony_ctx_t** cttx, flow_t* flow, encore_arg_t value);
bool flow_fulfilled();

void flow_trace(pony_ctx_t* ctx, void* p);

#endif
