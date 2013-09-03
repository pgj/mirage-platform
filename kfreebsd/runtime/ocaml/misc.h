/***********************************************************************/
/*                                                                     */
/*                                OCaml                                */
/*                                                                     */
/*         Xavier Leroy and Damien Doligez, INRIA Rocquencourt         */
/*                                                                     */
/*  Copyright 1996 Institut National de Recherche en Informatique et   */
/*  en Automatique.  All rights reserved.  This file is distributed    */
/*  under the terms of the GNU Library General Public License, with    */
/*  the special exception on linking described in file ../LICENSE.     */
/*                                                                     */
/***********************************************************************/

/* Miscellaneous macros and variables. */

#ifndef CAML_MISC_H
#define CAML_MISC_H

#ifndef CAML_NAME_SPACE
#include "compatibility.h"
#endif
#include "config.h"

/* Standard definitions */
#if defined(__FreeBSD__) && defined(_KERNEL)
#define OCAML_OS_TYPE   "kFreeBSD"

#include <sys/types.h>
#include <sys/libkern.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#ifdef MEM_DEBUG
#define __malloc(X) \
  mir_malloc(X, M_NOWAIT, __FILE__, __LINE__, NULL)
#define __mallocC(X,C) \
  mir_malloc(X, M_NOWAIT, __FILE__, __LINE__, C)
#define __realloc(P,X) \
  mir_realloc(P, X, M_NOWAIT, __FILE__, __LINE__, NULL)
#define __reallocC(P,X,C) \
  mir_realloc(P, X, M_NOWAIT, __FILE__, __LINE__, C)
#define __calloc(X,S) \
  mir_malloc(X * S, M_NOWAIT | M_ZERO, __FILE__, __LINE__, NULL)
#define __callocC(X,S,C) \
  mir_malloc(X * S, M_NOWAIT | M_ZERO, __FILE__, __LINE__, C)
#define __free(X) \
  mir_free(X, __FILE__, __LINE__)
#define __contigmalloc(S,F,L,H,A,B) \
  mir_contigmalloc(S, F, L, H, A, B, __FILE__, __LINE__, NULL)
#define __contigmallocC(S,F,L,H,A,B,C) \
  mir_contigmalloc(S, F, L, H, A, B, __FILE__, __LINE__, C)
#define __contigfree(P,S) \
  mir_contigfree(P, S, __FILE__, __LINE__)

void *mir_malloc(unsigned long size, int flags, char* file, int line,
  char *comment);
void *mir_realloc(void *addr, unsigned long size, int flags, char* file,
  int line, char *comment);
void *mir_contigmalloc(unsigned long size, int flags, vm_paddr_t low,
  vm_paddr_t high, unsigned long alignment, unsigned long boundary, char *file,
  int line, char *comment);
void mir_free(void *addr, char *file, int line);
void mir_contigfree(void *addr, unsigned long size, char *file, int line);
#else
#define __malloc(X)                 mir_malloc(X, M_NOWAIT)
#define __realloc(P,X)              mir_realloc(P, X, M_NOWAIT)
#define __calloc(X,S)               mir_malloc(X * S, M_NOWAIT | M_ZERO)
#define __free(X)                   mir_free(X)
#define __contigmalloc(S,F,L,H,A,B) mir_contigmalloc(S, F, L, H, A, B)
#define __contigfree(P,S)           mir_contigfree(P, S)

void *mir_malloc(unsigned long size, int flags);
void *mir_realloc(void *addr, unsigned long size, int flags);
void *mir_contigmalloc(unsigned long size, int flags, vm_paddr_t low,
  vm_paddr_t high, unsigned long alignment, unsigned long boundary);
void mir_free(void *addr);
void mir_contigfree(void *addr, unsigned long size);
#endif /* MEM_DEBUG */

#define __fprintf(F,X...)       printf(X)

int atoi(const char *str);
#else /* __FreeBSD__ && _KERNEL */
#define __malloc(X)             malloc(X)
#define __realloc(P,X)          realloc(P,X)
#define __calloc(X,S)           calloc(X,S)
#define __free(X)               free(X)
#define __fprintf(F,X...)       fprintf(F,X)

#include <stddef.h>
#include <stdlib.h>
#endif /* __FreeBSD__ && _KERNEL */


/* Basic types and constants */

typedef size_t asize_t;

#ifndef NULL
#define NULL 0
#endif

/* <private> */
typedef char * addr;
/* </private> */

#if defined(__GNUC__) && !defined(__FreeBSD__) && !defined(_KERNEL)
  /* Works only in GCC 2.5 and later */
  #define Noreturn __attribute__ ((noreturn))
#else
  #define Noreturn
#endif

/* Export control (to mark primitives and to handle Windows DLL) */

#define CAMLexport
#define CAMLprim
#define CAMLextern extern

/* Weak function definitions that can be overriden by external libs */
/* Conservatively restricted to ELF and MacOSX platforms */
#if defined(__GNUC__) && (defined (__ELF__) || defined(__APPLE__))
#define CAMLweakdef __attribute__((weak))
#else
#define CAMLweakdef
#endif

/* Assertions */

/* <private> */

#ifdef DEBUG
#define CAMLassert(x) \
  ((x) ? (void) 0 : caml_failed_assert ( #x , __FILE__, __LINE__))
CAMLextern int caml_failed_assert (char *, char *, int);
#else
#define CAMLassert(x) ((void) 0)
#endif

CAMLextern void caml_fatal_error (char *msg) Noreturn;
CAMLextern void caml_fatal_error_arg (char *fmt, char *arg) Noreturn;
CAMLextern void caml_fatal_error_arg2 (char *fmt1, char *arg1,
                                       char *fmt2, char *arg2) Noreturn;

/* Data structures */

struct ext_table {
  int size;
  int capacity;
  void ** contents;
};

extern void caml_ext_table_init(struct ext_table * tbl, int init_capa);
extern int caml_ext_table_add(struct ext_table * tbl, void * data);
extern void caml_ext_table_free(struct ext_table * tbl, int free_entries);

/* GC flags and messages */

extern uintnat caml_verb_gc;
void caml_gc_message (int, char *, uintnat);

/* Memory routines */

char *caml_aligned_malloc (asize_t, int, void **);

#ifdef DEBUG
#ifdef ARCH_SIXTYFOUR
#define Debug_tag(x) (0xD700D7D7D700D6D7ul \
                      | ((uintnat) (x) << 16) \
                      | ((uintnat) (x) << 48))
#else
#define Debug_tag(x) (0xD700D6D7ul | ((uintnat) (x) << 16))
#endif /* ARCH_SIXTYFOUR */

/*
  00 -> free words in minor heap
  01 -> fields of free list blocks in major heap
  03 -> heap chunks deallocated by heap shrinking
  04 -> fields deallocated by [caml_obj_truncate]
  10 -> uninitialised fields of minor objects
  11 -> uninitialised fields of major objects
  15 -> uninitialised words of [caml_aligned_malloc] blocks
  85 -> filler bytes of [caml_aligned_malloc]

  special case (byte by byte):
  D7 -> uninitialised words of [caml_stat_alloc] blocks
*/
#define Debug_free_minor     Debug_tag (0x00)
#define Debug_free_major     Debug_tag (0x01)
#define Debug_free_shrink    Debug_tag (0x03)
#define Debug_free_truncate  Debug_tag (0x04)
#define Debug_uninit_minor   Debug_tag (0x10)
#define Debug_uninit_major   Debug_tag (0x11)
#define Debug_uninit_align   Debug_tag (0x15)
#define Debug_filler_align   Debug_tag (0x85)

#define Debug_uninit_stat    0xD7

extern void caml_set_fields (char *, unsigned long, unsigned long);
#endif /* DEBUG */


#ifndef CAML_AVOID_CONFLICTS
#define Assert CAMLassert
#endif

/* </private> */

#endif /* CAML_MISC_H */
