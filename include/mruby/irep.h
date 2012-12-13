/*
** mruby/irep.h - mrb_irep structure
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_IREP_H
#define MRUBY_IREP_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct mrb_irep {
  int idx:16;
  int nlocals:16;
  int nregs:16;
  int flags:8;

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
  mrbjit_code *native_iseq;
} mrb_irep;

#define MRB_ISEQ_NO_FREE 1

mrb_irep *mrb_add_irep(mrb_state *mrb);

#if defined(__cplusplus)
}  /* extern "C" { */
#endif

mrb_value mrb_load_irep(mrb_state*,const char*);

#endif  /* MRUBY_IREP_H */
