/*
** mruby/irep.h - mrb_irep structure
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_IREP_H
#define MRUBY_IREP_H

#include "jit.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mrb_irep {
  short idx;
  short nlocals;
  short nregs;
  signed char flags;

  mrb_code *iseq;
  mrb_value *pool;
  mrb_sym *syms;

  /* debug info */
  const char *filename;
  short *lines;

  int ilen, plen, slen;

  mrb_int is_method_cache_used;

  /* JIT stuff */
  int *prof_info;
  mrbjit_codetab *jit_entry_tab;
  mrbjit_comp_info *compile_info;
} mrb_irep;

typedef struct mrbjit_vmstatus {
  mrb_irep **irep;
  struct RProc **proc;
  mrb_code **pc;
  mrb_value **pool;
  mrb_sym **syms;
  mrb_value **regs;
  int *ai;
} mrbjit_vmstatus;

#define MRB_ISEQ_NO_FREE 1

mrb_irep *mrb_add_irep(mrb_state *mrb);
mrb_value mrb_load_irep(mrb_state*,const char*);

#if defined(__cplusplus)
}  /* extern "C" { */
#endif

#endif  /* MRUBY_IREP_H */
