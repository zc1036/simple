
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

*/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include <sys/mman.h>

#include "x64.h"

/* Intrusive singly-linked list */

struct slist {
  void* next; // this is void* so it can be autocast to the right type
};

void slist_push(struct slist* const list, struct slist* new_node) {
  new_node->next = list;
}

typedef int(*slist_find_pred)(const void*, const void*);

void* slist_find(struct slist* list, const slist_find_pred pred, const void* const context) {
  while (list) {
    if (pred(list, context)) {
      return list;
    }

    list = list->next;
  }

  return NULL;
}

/* Fatal error */

void error(const char* const msg, ...) {
  va_list ap;
  va_start(ap, msg);
  vprintf(msg, ap);
  va_end(ap);

  exit(1);
}

/* Stack */

void stack_new(void*** const base, void*** const top, size_t initial_size) {
  *base = calloc(sizeof(*base), initial_size);
  *top = *base + initial_size - 1;
  *top = (void*)((uintptr_t)*top & ~15ULL); // 16-byte alignment for the stack
}

void stack_push(void*** const s, void* const value) {
  --*s;
  **s = value;
}

void* stack_pop(void*** const s) {
  void* const val = **s;
  ++*s;
  return val;
}

/* Some common definitions */

typedef long (*guest_function)(void**);

/* Symbol table */

struct symtab {
  struct slist list;
  char* symbol_name; // owning pointer
  void* symbol_value; // non-owning
};

#define SYMTAB_EMPTY NULL

int symtab_name_equals(const struct symtab* const tab, const char* const symbol_name) {
  return strcmp(tab->symbol_name, symbol_name) == 0;
}

struct symtab* symtab_lookup_symbol(struct symtab* tab, const char* const symbol_name) {
  return slist_find(&tab->list, (slist_find_pred)symtab_name_equals, symbol_name);
}

struct symtab* symtab_add_symbol(struct symtab* const tab,
                                 const char* const symbol_name,
                                 void* const symbol_value)
{
  struct symtab* const new_entry = calloc(sizeof(struct symtab), 1);

  new_entry->symbol_name = strdup(symbol_name);
  new_entry->symbol_value = symbol_value;

  slist_push(&tab->list, &new_entry->list);

  return new_entry;
}

/* Reader structures */

/** "base-class" of all the types that the reader can return **/
struct rd_object {
  long type;
};

struct rd_symbol {
  struct rd_object base;

  const char* const repr;
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

/** Readtable **/

long read_symbol(void** stackptr) {
}

long read_number(void** stackptr) {
}
long read_string(void** stackptr) {
}
long read_error(void** stackptr) {
}
long read_quote(void** stackptr) {
}
long read_list(void** stackptr) {
}

struct readtable {
  guest_function dispatch[256];
};

const struct readtable default_readtable = {
  {
    ['a' ... 'z'] = read_symbol,
    ['A' ... 'Z'] = read_symbol,
    ['_']         = read_symbol,
    ['!']         = read_symbol,
    ['@']         = read_symbol,
    ['#']         = read_symbol,
    ['$']         = read_symbol,
    ['%']         = read_symbol,
    ['^']         = read_symbol,
    ['&']         = read_symbol,
    ['*']         = read_symbol,
    ['-']         = read_number,
    ['+']         = read_number,
    ['0' ... '9'] = read_number,
    ['"']         = read_string,
    ['[']         = read_quote,
    [']']         = read_error,
    ['(']         = read_list,
    [')']         = read_error,
  }
};

/* Some globals */

struct symtab** global_symbol_table;

struct readtable** current_readtable;

/* Functions */

long read(void** stack) {
  FILE* const stream = stack_pop(&stack);

  int character = fgetc(stream);

  character = toupper(character);
  const guest_function handler = (*current_readtable)->dispatch[character & 0xff];

  if (!handler) {
    error("Invalid character '%c'", character);
  }

  stack_push(&stack, stream);
  stack_push(&stack, (void*)(uintptr_t)character);

  call_guest_function(handler, &stack);

  struct rd_object* const obj = stack_pop(&stack);

  return return_to_guest(stack);
}

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

#define PAGE_SIZE 4096
#define PROGRAM_AREA_SIZE (PAGE_SIZE * 128) // 128 * 4K = .5 mb

  unsigned char* program_area = calloc(PROGRAM_AREA_SIZE + 1, PAGE_SIZE);
  program_area = (void*)(((intptr_t)program_area + PAGE_SIZE) & ~4095ULL);

  mprotect(program_area, PROGRAM_AREA_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);

  unsigned char** program_area_ptr = malloc(sizeof(*program_area_ptr));
  *program_area_ptr = program_area;

  /* Register globals */

#define ADD_SYM(NAME, VALUE) *global_symbol_table = symtab_add_symbol(*global_symbol_table, (NAME), (VALUE));
  ADD_SYM("*SYMTAB*", global_symbol_table);
  ADD_SYM("*READTAB*", global_symbol_table);
  ADD_SYM("*IN*", input);
  ADD_SYM("*OUT*", output);
  ADD_SYM("*PROGRAM*", program_area_ptr);
#undef ADD_SYM

  /* Create stack */

  void** guest_stack_base, ** guest_stack;
  stack_new(&guest_stack_base, &guest_stack, 1000); // this size seems legit

  /* Main program */

  for (int i = 1; i < argc; ++i) {
    *input = fopen(argv[i], "rb");

    struct rd_object* obj;

    while (1) {
      stack_push(&guest_stack, *input);

      call_guest_function(read, &guest_stack);

      obj = stack_pop(&guest_stack);

      if (!obj) {
        break;
      }

      stack_push(&guest_stack, obj);

      call_guest_function(compile, &guest_stack);

      call_guest_function(eval, &guest_stack);
    }

    fclose(*input);
    *input = NULL;
  }

  return return_to_guest(guest_stack);
}
