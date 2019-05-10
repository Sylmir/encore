#include "encore.h"
#include "flow.h"

/// Attributes for the mutex in flow_t.
static pthread_mutexattr_t attr;

/**
 * Possible types for the next Flow in a chain. See below for examples.
 */
 enum result_type_e {
  /// Next Flow is a Flow
  FLOW,
  /// Next Flow is a usable value
  VALUE
};

struct flow_s 
{
  /// Type of the next Flow in the chain
  result_type_t result_type;
  /**
   * Next Flow in the chain. We use encore_arg_t to have an interface similar
   * to what is found in the future library. 
   * 
   * Another way would be to have two fulfil functions, one called when the 
   * resolving value is a Flow, one when the resolving value is not a Flow.
   */
  encore_arg_t result;

  bool fulfiled;
  // actor_entry_t blocked_actors[16];
  // int n_blocked_actors;

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

static void init_mutexattr();
static void init_mutexattr_dummy();
static void flow_finalize(void*);
static void (*init_attr)() = &init_mutexattr;

pony_type_t flow_type = {
  .id = ID_FLOW,
  .size = sizeof(flow_t),
  .trace = flow_trace,
  .final = flow_finalize
};

flow_t* flow_mk(pony_ctx_t** cttx, pony_type_t* type, result_type_t result_type)
{
  init_attr();

  pony_ctx_t* ctx = *cttx;
  flow_t* flow = encore_alloc(ctx, sizeof(flow_t));
  flow->fulfiled = false;
  flow->result_type = result_type;
  pthread_mutex_init(&(flow->lock), &attr);
}

flow_t* flow_mk_from_value(pony_ctx_t** cttx, pony_type_t* type, 
                           encore_arg_t value)
{
  init_attr();

  pony_ctx_t* ctx = *cttx;
  flow_t* flow = encore_alloc(ctx, sizeof(flow_t));
  flow->fulfiled = true;
  flow->result = value;
  flow->result_type = VALUE;
  pthread_mutex_init(&(flow->lock), &attr);
}

encore_arg_t flow_get(pony_ctx_t** ctx, pony_type_t* type, flow_t* flow)
{
  if (flow->result_type == FLOW) {
    if (!flow->fulfiled) {
      
    }
  } else {

  }
}

void flow_fulfil(pony_ctx_t** ctx, flow_t* flow, encore_arg_t value)
{

}

bool flow_fulfilled()
{

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

void flow_finalize(void* p)
{
  (void)p;
}