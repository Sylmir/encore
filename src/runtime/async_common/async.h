#ifndef ENCORE_ASYNC_H
#define ENCORE_ASYNC_H

#include <pony.h>
#include <ucontext.h>

#include "closure.h"
#include "encore.h"
#include "flow.h"
#include "future.h"

typedef struct actor_entry actor_entry_t;
typedef struct closure_entry closure_entry_t;
typedef struct message_entry message_entry_t;

// Terminology:
// Producer -- the actor responsible for fulfilling a future / flow
// Consumer -- an non-producer actor using a future / flow

typedef enum responsibility_t
{
  // A closure that should be run by the producer
  DETACHED_CLOSURE,
  // A message blocked on this future / flow
  BLOCKED_MESSAGE
} responsibility_t;

struct closure_entry
{
  // The consumer that created closure
  pony_actor_t *actor;
  // The future or flow where the result of the closure should be stored
  union {
    flow_t       *flow;
    future_t     *future;
  } storage;
  // The closure to be run on fulfilment of the future or flow
  closure_t    *closure;

  closure_entry_t *next;

};

struct message_entry
{
  // The consumer that created closure
  pony_actor_t *actor;
  // FIXME: add context
};

struct actor_entry
{
  responsibility_t type;
  union
  {
    closure_entry_t closure;
    message_entry_t message;
  };
};

typedef struct actor_list {
  encore_actor_t *actor;
  ucontext_t *uctx;
  struct actor_list *next;
} actor_list;

#endif