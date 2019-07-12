#include <assert.h>
#include <pony.h>
#include <pthread.h>

#include "async.h"
#include "encore.h"
#include "flow.h"
#include "../libponyrt/actor/messageq.h"
#include "../libponyrt/sched/scheduler.h"

#define FLOW_MAX_CAPABILITIES 16

/// Attributes for the mutex in flow_t. We use a recursive mutex so the same
/// thread can lock several times in a row.
static pthread_mutexattr_t attr;

struct flow_s 
{
  pony_type_t* type;

  /// Type of the next Flow in the chain
  flow_result_type_t result_type;
  /**
   * Next Flow in the chain. We use encore_arg_t to have an interface similar
   * to what is found in the future library. 
   * 
   * Another way would be to have two fulfil functions, one called when the 
   * resolving value is a Flow, one when the resolving value is not a Flow.
   */
  encore_arg_t result;

  bool fulfiled;
  actor_entry_t blocked_actors[FLOW_MAX_CAPABILITIES];
  int n_blocked_actors;

  /// Lock based synchronisation for now
  pthread_mutex_t lock;

  /// When chaining
  // flow_t* parent;

  /// Closures to run upon fulfilment
  // closure_entry_t* children;

  // actor_list_t* awaited_actors;
};

/**
 * Consider the following examples :
 * 
 * 
 * Example 1 : returning int in functionning returning Flow[int]
 * fun f() : Flow[int]
 *   2
 * end
 * 
 * When translating the above from Encore to C, we must have a way to turn the
 * 2 into a Flow[int]. We use the flow_mk_from_value function to do that. The
 * created Flow has a value_type of VALUE, since there is no next Flow. 
 * 
 * 
 * Example 2 : asynchronous flow call to a method / function returning int
 * fun f() : int
 *   2
 * end
 * 
 * active class Foo
 *   def f() : int
 *     2
 *   end
 * end
 * 
 * active class Main
 *   def main() : unit
 *     var foo = new Foo
 *     foo!!f()
 *     async* f()
 *   end
 * end
 * 
 * When translating foo!!f() and async* f(), we must create a Flow that has a
 * value_type of VALUE, but still needs to be fulfilled. This is basically the
 * same thing as an asynchronous call with futures.
 * 
 * 
 * Example 3 : asynchronous flow call to a method / function returning a Flow 
 * Consider example 2, with all occurrences of "int" replaced with Flow[int]. 
 * 
 * In these cases, we see a chain of Flow appearing. The first Flow results from
 * the asynchronous flow call (foo!!f() / async* f()), the second (and later) 
 * result from the asynchronous flow calls / conversions in the called methods /
 * functions. The first Flow has a result_type of FLOW, since there is a chain.
 * The following flows have a result_type depending on the context.
 * 
 * 
 * Example 4 : complete example with the factorial function
 * 
 * fun fact(n : int) : Flow[int]
 *   let fact' = fun (n : int, acc : int) : Flow[int]
 *                 if (n <= 1) then
 *                   acc
 *                 else
 *                   async* fact'(n - 1, acc * n)
 *               end
 *   in fact'(n, 1)
 * end
 * 
 * Let's assume we have a call : async* fact(3). We will end up with the 
 * following chain of calls : async* fact(3) => async* fact'(3, 1)
 * => async* fact'(2, 3) => async* fact'(1, 6). 
 */

/// Initialize the pthread_mutexattr_t global object.
static void init_mutexattr();

/// Dummy function
static void init_mutexattr_dummy();

static void flow_finalize(flow_t* flow);

static void flow_block_actor(pony_ctx_t** ctx, flow_t* flow);

/// Pointer to the function used to initialize the global pthread_mutexattr_t
/// object. This prevents the attribute from being initialized multiple times.
static void (*init_attr)() = &init_mutexattr;

/// Garbage collection

/// Acquire the value of a flow in the garbage collector.
static void acquire_flow_value();

/* extern */ void encore_async_gc_acquireobject(pony_ctx_t* ctx, void*p, 
    pony_type_t* type, int mutability);

/* extern */ void encore_async_gc_acquireactor(pony_ctx_t* ctx, pony_actor_t* actor);

static void encore_gc_acquire(pony_ctx_t* ctx);

static inline void flow_gc_trace_value(pony_ctx_t* ctx, flow_t* flow)
{
  assert(flow);

  if (flow->type == ENCORE_ACTIVE) {
    encore_trace_actor(ctx, flow->result.p);
  } else if (flow->type != ENCORE_PRIMITIVE) {
    encore_trace_object(ctx, flow->result.p, flow->type->trace);
  }
}

static void flow_gc_send_value(pony_ctx_t* ctx, flow_t* flow);

static void flow_gc_recv_value(pony_ctx_t* ctx, flow_t* flow);

pony_type_t flow_type = {
  .id = ID_FLOW,
  .size = sizeof(flow_t),
  .trace = flow_trace,
  .final = (void*)&flow_finalize
};

flow_t* flow_mk(pony_ctx_t** cttx, pony_type_t* type, flow_result_type_t result_type)
{
  pony_ctx_t* ctx = *cttx;
  flow_t* flow = pony_alloc_final(ctx, sizeof(flow_t));
  flow->fulfiled = false;
  flow->result_type = result_type;
  flow->type = type;

  init_attr();
  pthread_mutex_init(&(flow->lock), &attr);

  return flow;
}

flow_t* flow_mk_from_value(pony_ctx_t** cttx, pony_type_t* type, 
                           encore_arg_t value)
{
  pony_ctx_t* ctx = *cttx;
  flow_t* flow = pony_alloc_final(ctx, sizeof(flow_t));
  flow->fulfiled = true;
  flow->result = value;
  flow->result_type = FLOW_RESULT_VALUE;
  flow->type = type;

  init_attr();
  pthread_mutex_init(&(flow->lock), &attr);

  return flow;
}

encore_arg_t flow_get(pony_ctx_t** ctx, flow_t* flow)
{
  if (!flow->fulfiled) {
      flow_block_actor(ctx, flow);
  }

  assert(flow->fulfiled);
  acquire_flow_value(ctx, flow);

  if (flow->result_type == FLOW_RESULT_FLOW) {
    // Perform a new get. Infinite recursion incoming
    return flow_get(ctx, (flow_t*)flow->result.p);
  } else {
    return flow->result;
  }

  // return flow->result;
}

void flow_fulfil(pony_ctx_t** ctx, flow_t* flow, encore_arg_t result)
{
  assert(!flow->fulfiled);

  pthread_mutex_lock(&flow->lock);

  flow->result = result;
  flow->fulfiled = true;

  pony_ctx_t* cctx = *ctx;
  flow_gc_send_value(cctx, flow);

  for (int i = 0; i < flow->n_blocked_actors; ++i) {
    actor_entry_t* actor = flow->blocked_actors + i;
    switch (actor->type) {
      case BLOCKED_MESSAGE:
        actor_set_resume((encore_actor_t*)actor->message.actor);
        pony_schedule(cctx, actor->message.actor);
        break;

      case DETACHED_CLOSURE:
        assert(false);
        exit(-1);

      default:
        break;
    }
  }

  // Execute chaining

  // Unlock awaiting actors

  pthread_mutex_unlock(&flow->lock);
}

bool flow_fulfilled(flow_t* const flow)
{
  bool result;
  
  pthread_mutex_lock(&flow->lock);
  result = flow->fulfiled;
  pthread_mutex_unlock(&flow->lock);

  return result;
}

void flow_trace(pony_ctx_t* ctx, void* p)
{
  (void)ctx;
  (void)p;
}

// =============================================================================
// Local functions

void init_mutexattr() 
{
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  init_attr = &init_mutexattr_dummy;
}

void init_mutexattr_dummy() 
{
  
}

void flow_finalize(flow_t* flow)
{
  pony_ctx_t* cctx = pony_ctx();
  flow_gc_recv_value(cctx, flow);
}

void flow_block_actor(pony_ctx_t** ctx, flow_t* flow) 
{
  pony_ctx_t* cctx = *ctx;
  pony_actor_t* actor = cctx->current;

  pthread_mutex_lock(&flow->lock);

  if (flow->fulfiled) {
    pthread_mutex_unlock(&flow->lock);
    return;
  }

  pony_unschedule(cctx, actor);
  assert(flow->n_blocked_actors < FLOW_MAX_CAPABILITIES);
  flow->blocked_actors[flow->n_blocked_actors++] = (actor_entry_t) {
    .type = BLOCKED_MESSAGE,
    .message = (message_entry_t) {
      .actor = actor
    }
  };

  encore_actor_t* as_encore_actor = (encore_actor_t*)actor;
  assert(as_encore_actor->lock == NULL);
  as_encore_actor->lock = &flow->lock;
  actor_block(ctx, as_encore_actor);

  // pthread_mutex_unlock(&flow->lock);
}

void acquire_flow_value(pony_ctx_t** ctx, flow_t* flow)
{
  pony_ctx_t* cctx = *ctx;
  encore_gc_acquire(cctx);
  flow_gc_trace_value(cctx, flow);
  pony_acquire_done(cctx);
}

void encore_gc_acquire(pony_ctx_t* ctx)
{
  assert(ctx->stack == NULL);
  ctx->trace_object = encore_async_gc_acquireobject;
  ctx->trace_actor = encore_async_gc_acquireactor;
}

void flow_gc_send_value(pony_ctx_t* ctx, flow_t* flow)
{
  pony_gc_send(ctx);
  flow_gc_trace_value(ctx, flow);
  pony_send_done(ctx);
}

void flow_gc_recv_value(pony_ctx_t* ctx, flow_t* flow)
{
  pony_gc_recv(ctx);
  flow_gc_trace_value(ctx, flow);
  ponyint_gc_handlestack(ctx);
}
