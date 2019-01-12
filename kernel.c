
/* A simple concatenative interpreter */

/*

Style guide
===========

const-correctness
-----------------

Values that aren't const MUST BE MODIFIED (potentially conditionally)
by the function declaring that variable, or else they MUST BE used in
such a way that requires them to be non-const (i.e. returned as a
non-const value or passed as a non-const argument).

misc
----

Put commas after the last element of sequences, e.g. array initializers,
enum definitions, etc.

*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>

#include <sys/mman.h>

#include "x64.h"
#include "asm.h"

/* Fatal error function */

static void error(const char* const msg, ...) {
  va_list ap;
  va_start(ap, msg);
  vfprintf(stderr, msg, ap);
  va_end(ap);

  fputc('\n', stderr);

  exit(1);
}

/* Some data structures */

/** Intrusive singly-linked list **/

struct slist {
  void* next; // this is void* so it can be autocast to the right type
};

static void slist_push(struct slist* const list, struct slist* new_node) {
  new_node->next = list;
}

typedef int(*slist_find_pred)(const void*, const void*);

static void* slist_find(struct slist* list, const slist_find_pred pred, const void* const context) {
  while (list) {
    if (pred(list, context)) {
      return list;
    }

    list = list->next;
  }

  return NULL;
}

/** Stack **/

static void stack_new(void*** const base, void*** const top, size_t initial_size) {
  *base = calloc(sizeof(*base), initial_size);
  *top = *base + initial_size - 1;
  *top = (void*)((uintptr_t)*top & ~15ULL); // 16-byte alignment for the stack
}

static void stack_push(void*** const s, void* const value) {
  --*s;
  **s = value;
}

static void* stack_pop(void*** const s) {
  void* const val = **s;
  ++*s;
  return val;
}

/** Vector **/

struct vector {
  char* data;
  size_t fill, size;
};

static void vector_new(struct vector* const vec) {
  vec->data = NULL;
  vec->fill = 0;
  vec->size = 0;
}

static void vector_delete(struct vector* const vec) {
  free(vec->data);
  vec->data = NULL;
  vec->fill = 0;
  vec->size = 0;
}

static inline size_t vector_length(const struct vector* const vec) {
  return vec->fill;
}

static void* vector_append(struct vector* const vec, const size_t size) {
  // check for overflow
  if (vec->fill + size < size) {
    error("Overflow in vector size");
  }

  const size_t newfill = vec->fill + size;

  if (newfill > vec->size) {
    vec->size = newfill * 2;
    vec->data = realloc(vec->data, vec->size);

    if (!vec->data) {
      error("Failed to allocate %lu bytes for vector", (unsigned long)vec->size);
    }
  }

  void* const new_element = vec->data + vec->fill;

  vec->fill = newfill;
  
  return new_element;
}

#define VECTOR_APPEND(VEC, TYPE, VALUE)         \
  *(TYPE*)vector_append((VEC), sizeof(TYPE)) = (VALUE)

static inline void* vector_data(struct vector* const vec) {
  return vec->data;
}

#define VECTOR_AT(VEC, T, IDX)                  \
  (((T*)vector_data((VEC)))[IDX])

/* Some common typedefs */

typedef long (*guest_function)(void**);

/* Symbol table */

enum symbol_type {
  symtype_function,
  symtype_value,
};

typedef unsigned long symbol_type_t;

struct symtab {
  struct slist list;
  char* symbol_name; // owning pointer
  void* symbol_value; // non-owning
  symbol_type_t symbol_type;
};

#define SYMTAB_EMPTY NULL

static int symtab_name_equals(const struct symtab* const tab, const char* const symbol_name) {
  return strcmp(tab->symbol_name, symbol_name) == 0;
}

static struct symtab* symtab_lookup_symbol(struct symtab* tab, const char* const symbol_name) {
  return slist_find(&tab->list, (slist_find_pred)symtab_name_equals, symbol_name);
}

static struct symtab* symtab_add_symbol(struct symtab* const tab,
                                        const char* const symbol_name,
                                        void* const symbol_value,
                                        const enum symbol_type symtype)
{
  struct symtab* const new_entry = calloc(sizeof(struct symtab), 1);

  new_entry->symbol_name = strdup(symbol_name);
  new_entry->symbol_value = symbol_value;
  new_entry->symbol_type = symtype;

  slist_push(&tab->list, &new_entry->list);

  return new_entry;
}

/* Reader structures */

enum rd_type {
  rd_type_symbol,
  rd_type_number,
  rd_type_string,
  rd_type_quote,
  rd_type_cons,
};

/** "base-class" of all the types that the reader can return **/
struct rd_object {
  enum rd_type type;
};

struct rd_symbol {
  struct rd_object base;

  const char* repr;
};

struct rd_number {
  struct rd_object base;

  long value;
};

struct rd_string {
  struct rd_object base;

  char* contents;
};

struct rd_quote {
  struct rd_object base;

  struct rd_quote* next;
  void* value;
};

struct rd_cons {
  struct rd_object base;

  void* car, * cdr;
};

union rd_any {
  struct rd_object base;
  struct rd_symbol sym;
  struct rd_number num;
  struct rd_string str;
  struct rd_quote quote;
  struct rd_cons cons;
};

/** Readtable **/

#define BIT(X) (1 << X)

enum cprop {
  cprop_constituent = BIT(0),
  cprop_number_init = BIT(1),
  cprop_number      = BIT(2),
  cprop_macro       = BIT(4),
  cprop_whitespace  = BIT(5),
  cprop_error       = BIT(6),
};

typedef unsigned char char_prop_t;

struct readtable {
  char_prop_t char_properties[256];

  guest_function macro_dispatch[256];
};

/* Some globals */

static struct symtab** global_symbol_table;

static struct readtable** current_readtable;

static unsigned char** program_area_ptr;

/* Functions */

#define GUESTFUNC(NAME, ARGNAME)                \
  long NAME(void** ARGNAME)

/** Reader functions **/

static GUESTFUNC(read_symbol, stack) {
  intptr_t character = (intptr_t)stack_pop(&stack);
  FILE* const stream = stack_pop(&stack);

  struct vector symrepr;
  vector_new(&symrepr);

  VECTOR_APPEND(&symrepr, char, character);

  while (1) {
    character = fgetc(stream);

    if (character == EOF) {
      break;
    }

    character = toupper(character & 0xff);

    const char_prop_t cprop = (*current_readtable)->char_properties[character];

    if (!(cprop & cprop_constituent)) {
      ungetc(character, stream);
      break;
    }

    VECTOR_APPEND(&symrepr, char, character);
  }

  struct rd_symbol* const sym = calloc(sizeof(*sym), 1);

  sym->base.type = rd_type_symbol;
  sym->repr = vector_data(&symrepr);

  stack_push(&stack, sym);

  return return_to_guest(stack);
}

static GUESTFUNC(read_number, stack) {
  intptr_t character = (intptr_t)stack_pop(&stack);
  FILE* const stream = stack_pop(&stack);

  struct vector repr;
  vector_new(&repr);

  int negate = 0;

  if (character == '-') {
    negate = 1;
  } else if (character == '+') {
    // nothing
  } else {
    VECTOR_APPEND(&repr, char, character);
  }

  while (1) {
    character = fgetc(stream);

    if (character == EOF) {
      break;
    }

    const char_prop_t cprop = (*current_readtable)->char_properties[character];

    if (!(cprop & cprop_number)) {
      ungetc(character, stream);
      break;
    }

    VECTOR_APPEND(&repr, char, character);
  }

  long value = 0;
  long factor = 1;

  for (size_t i = vector_length(&repr); i > 0; --i) {
    value += (VECTOR_AT(&repr, char, i - 1) - '0') * factor;
    factor *= 10;
  }

  if (negate) {
    value = -value;
  }

  struct rd_number* const num = calloc(sizeof(*num), 1);
  num->base.type = rd_type_number;
  num->value = value;

  stack_push(&stack, num);

  return return_to_guest(stack);
}
static GUESTFUNC(read_string, stack) {
  error("%s not implemented", __func__);
  return return_to_guest(stack);
}
static GUESTFUNC(read_error, stack) {
  error("%s not implemented", __func__);
  return return_to_guest(stack);
}
static GUESTFUNC(read_quote, stack) {
  error("%s not implemented", __func__);
  return return_to_guest(stack);
}
static GUESTFUNC(read_list, stack) {
  error("%s not implemented", __func__);
  return return_to_guest(stack);
}

static GUESTFUNC(read, stack) {
  FILE* const stream = stack_pop(&stack);

  int character = 0;

  // the work done below is to fill this out so we know which function
  // is appropriate to read the datum, then we call it and return
  guest_function handler = NULL;


  while (1) {
    character = fgetc(stream);

    if (character == EOF) {
      stack_push(&stack, NULL);
      return return_to_guest(stack);
    }

    character = toupper(character & 0xff);

    const char_prop_t cprop = (*current_readtable)->char_properties[character];

    if (cprop & cprop_error) {
      error("Reader encountered illegal character '%c' (%d)\n", character, character);
    }

    if (cprop & cprop_whitespace) {
      continue;
    }

    if (cprop & cprop_macro) {
      handler = (*current_readtable)->macro_dispatch[character];

      if (!handler) {
        error("Invalid character '%c' (%d)", character, character);
      }

      break;
    }

    if (cprop & cprop_number_init) {
      handler = read_number;
      break;
    }

    if (cprop & cprop_constituent) {
      handler = read_symbol;
      break;
    }

    if (cprop & cprop_number) {
      error("Encountered number continuation outside of a number");
    }

    error("Encountered character with no properties '%c' (%d)", character, character);
  }

  stack_push(&stack, stream);
  stack_push(&stack, (void*)(uintptr_t)character);

  call_guest_function(handler, &stack);

  //struct rd_object* const obj = stack_pop(&stack);

  return return_to_guest(stack);
}

/** Compiler **/

static GUESTFUNC(compile, stack) {
  union rd_any* const rdobj = stack_pop(&stack);

  void* ret = NULL;

  switch (rdobj->base.type) {
  case rd_type_symbol:
    {
      struct symtab* const obj = symtab_lookup_symbol(*global_symbol_table, rdobj->sym.repr);

      if (!obj) {
        error("The name '%s' is undefined", rdobj->sym.repr);
      }

      ret = *program_area_ptr;
      *program_area_ptr = asm_call(*program_area_ptr, obj->symbol_value);
    }
    break;
  case rd_type_number:
    ret = *program_area_ptr;
    *program_area_ptr = asm_integer(*program_area_ptr, rdobj->num.value);
    break;
  case rd_type_string:
  case rd_type_quote:
  case rd_type_cons:
    error("unimplemented");
  }

  stack_push(&stack, ret);

  return return_to_guest(stack);
}

/** Evaluator **/

static GUESTFUNC(eval, stack) {
  /** eval: obj -> ?  

      The eval() function doesn't modify the stack in its own right,
      but simply evaluates its argument and leaves its results on the
      stack. **/

  union rd_any* const rdobj = stack_pop(&stack);

  switch (rdobj->base.type) {
  case rd_type_symbol:
    {
      struct symtab* const obj = symtab_lookup_symbol(*global_symbol_table, rdobj->sym.repr);

      if (!obj) {
        error("The name '%s' is undefined", rdobj->sym.repr);
      }

      if (obj->symbol_type == symtype_function) {
        call_guest_function(obj->symbol_value, &stack);
      } else {
        stack_push(&stack, obj->symbol_value);
      }
    }
    break;
  case rd_type_number:
    stack_push(&stack, (void*)rdobj->num.value);
    break;
  case rd_type_string:
  case rd_type_quote:
  case rd_type_cons:
    error("unimplemented");
  }

  return return_to_guest(stack);
}

/** Some intrinsics **/

static GUESTFUNC(dup, stack) {
  void* const value = *stack;
  stack_push(&stack, value);
  return return_to_guest(stack);
}

static GUESTFUNC(mult, stack) {
  void* const a = stack_pop(&stack),
      * const b = stack_pop(&stack);
  
  stack_push(&stack, (void*)((long)a * (long)b));
  return return_to_guest(stack);
}

static GUESTFUNC(print_int, stack) {
  const long a = (long)stack_pop(&stack);

  printf("%ld\n", a);
  
  return return_to_guest(stack);
}

/* The default readtable, which is immutable */

static const struct readtable default_readtable = {
  { // char_properties
    ['a' ... 'z'] = cprop_constituent,
    ['A' ... 'Z'] = cprop_constituent,
    ['_']         = cprop_constituent,
    ['!']         = cprop_constituent,
    ['@']         = cprop_constituent,
    ['#']         = cprop_constituent,
    ['$']         = cprop_constituent,
    ['%']         = cprop_constituent,
    ['^']         = cprop_constituent,
    ['&']         = cprop_constituent,
    ['*']         = cprop_constituent,
    [':']         = cprop_constituent,
    [',']         = cprop_constituent,
    ['.']         = cprop_constituent,
    ['<']         = cprop_constituent,
    ['>']         = cprop_constituent,
    ['=']         = cprop_constituent,
    ['/']         = cprop_constituent,
    ['?']         = cprop_constituent,

    ['-']         = cprop_number_init | cprop_constituent,
    ['+']         = cprop_number_init | cprop_constituent,
    ['0' ... '9'] = cprop_number_init | cprop_number | cprop_constituent,

    [';']         = cprop_macro,
    ['"']         = cprop_macro,
    ['[']         = cprop_macro,
    [']']         = cprop_error,
    ['(']         = cprop_macro,
    [')']         = cprop_error,

    [' ']         = cprop_whitespace,
    ['\n']        = cprop_whitespace,
    ['\t']        = cprop_whitespace,
    ['\r']        = cprop_whitespace,
  },
  
  { // macro_dispatch
    ['"']         = read_string,
    ['[']         = read_quote,
    [']']         = read_error,
    ['(']         = read_list,
    [')']         = read_error,
  }
};

int main(const int argc, const char* const argv[const]) {
  /* Create globals accessible from the guest */

  global_symbol_table = calloc(sizeof(*global_symbol_table), 1);
  *global_symbol_table = SYMTAB_EMPTY;

  FILE** const input = calloc(sizeof(*input), 1);
  FILE** const output = calloc(sizeof(*input), 1);

  *output = stdout;

  current_readtable = calloc(sizeof(*current_readtable), 1);
  *current_readtable = calloc(sizeof(**current_readtable), 1);
  **current_readtable = default_readtable;

  /** Create page-aligned program area and set memory protections such
   ** that it's executable (DEP is for weenies) **/

#define PAGE_SIZE 4096ULL
#define PROGRAM_AREA_SIZE (PAGE_SIZE * 128ULL) // 128 * 4K = .5 mb

  unsigned char* program_area = calloc(PROGRAM_AREA_SIZE + PAGE_SIZE, 1);
  memset(program_area, 0xcc, PROGRAM_AREA_SIZE + PAGE_SIZE);

  // mprotect wants a page-aligned pointer
  program_area = (void*)(((uintptr_t)program_area + PAGE_SIZE) & ~4095ULL);

  mprotect(program_area, PROGRAM_AREA_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

  program_area_ptr = malloc(sizeof(*program_area_ptr));
  *program_area_ptr = program_area;

  /* Register globals */

#define ADD_SYM(NAME, VALUE, SYMTYPE) \
  *global_symbol_table = symtab_add_symbol(*global_symbol_table, (NAME), (VALUE), (SYMTYPE));

  ADD_SYM("*SYMTAB*", global_symbol_table, symtype_value);
  ADD_SYM("*READTAB*", current_readtable, symtype_value);
  ADD_SYM("*IN*", input, symtype_value);
  ADD_SYM("*OUT*", output, symtype_value);
  ADD_SYM("*PROGRAM*", program_area_ptr, symtype_value);

  ADD_SYM("DUP", dup, symtype_function);
  ADD_SYM("*", mult, symtype_function);
  ADD_SYM("PRINTI", print_int, symtype_function);
#undef ADD_SYM

  /* Create stack */

  void** guest_stack_base, ** guest_stack;
  stack_new(&guest_stack_base, &guest_stack, 1000); // this size seems legit

  /* Main program */

  for (int i = 1; i < argc; ++i) {
    if (strcmp(argv[i], "-") == 0) {
      *input = stdin;
    } else {
      *input = fopen(argv[i], "rb");
    }

    if (!*input) {
      error("Could not open file '%s'", argv[i]);
    }

    struct rd_object* obj;

    while (1) {
      stack_push(&guest_stack, *input);

      call_guest_function(read, &guest_stack);

      obj = stack_pop(&guest_stack);

      if (!obj) {
        break;
      }

      stack_push(&guest_stack, obj);

      call_guest_function(eval, &guest_stack);
    }

    fclose(*input);
    *input = NULL;
  }

  return 0;
}
