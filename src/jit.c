/*
** jit.c - Toplevel of JIT
**
** See Copyright Notice in mruby.h
*/

#include "mruby.h"
#include "opcode.h"
#include "error.h"
#include "mruby/jit.h"
#include "mruby/irep.h"
#include "mruby/variable.h"
#include "mruby/proc.h"
#include "mruby/class.h"
#include "mruby/array.h"
#include <stddef.h>
#include <stdio.h>
#include <setjmp.h>

#ifdef JIT_DEBUG
# define DEBUG(x) (x)
#else
# define DEBUG(x)
#endif

#define SET_NIL_VALUE(r) MRB_SET_VALUE(r, MRB_TT_FALSE, value.i, 0)

void *
mrbjit_exec_send_c(mrb_state *mrb, mrbjit_vmstatus *status,
		 struct RProc *m, struct RClass *c)
{
  /* A B C  R(A) := call(R(A),Sym(B),R(A+1),... ,R(A+C-1)) */
  mrb_code *pc = *status->pc;
  mrb_value *regs = *status->regs;
  mrb_sym *syms = *status->syms;
  mrb_irep *irep;
  int ai = *status->ai;
  mrb_code i = *pc;

  int a = GETARG_A(i);
  int n = GETARG_C(i);
  mrb_callinfo *ci;
  mrb_value recv, result;
  mrb_sym mid = syms[GETARG_B(i)];
  int orgdisflg = mrb->compile_info.disable_jit;

  recv = regs[a];

  // printf("C %d %x %x %x\n", m->body.func, regs, n);
  // puts(mrb_sym2name(mrb, mid));

  ci = mrbjit_cipush(mrb);
  ci->proc_pool = mrb->c->proc_pool;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = n;
  ci->target_class = c;

  ci->pc = pc + 1;
  ci->acc = a;

  /* prepare stack */
  mrb->c->stack += a;

  ci->nregs = n + 2;
  //mrb_p(mrb, recv);
  mrb->compile_info.disable_jit = 1;
  result = m->body.func(mrb, recv);
  mrb->compile_info.disable_jit = orgdisflg;
  mrb->c->stack[0] = result;
  mrb_gc_arena_restore(mrb, ai);
  if (mrb->exc) {
    ci->mid = mid;
    ci->proc = m;
    return status->gototable[0];	/* goto L_RAISE; */
  }
  /* pop stackpos */
  ci = mrb->c->ci;
  if (!ci->target_class) { /* return from context modifying method (resume/yield) */
    if (!MRB_PROC_CFUNC_P(ci[-1].proc)) {
      irep = *(status->irep) = ci[-1].proc->body.irep;
      *(status->pool) = irep->pool;
      *(status->syms) = irep->syms;
    }
    *(status->regs) = mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
    mrbjit_cipop(mrb);
    *(status->pc) = ci->pc;

    return status->optable[GET_OPCODE(**(status->pc))];
  }
  mrb->c->stack = mrb->c->stbase + mrb->c->ci->stackidx;
  mrbjit_cipop(mrb);

  return NULL;
}

void *
mrbjit_exec_send_mruby(mrb_state *mrb, mrbjit_vmstatus *status,
		 struct RProc *m, struct RClass *c)
{
  /* A B C  R(A) := call(R(A),Sym(B),R(A+1),... ,R(A+C-1)) */
  mrb_code *pc = *status->pc;
  mrb_irep *irep = *status->irep;
  mrb_value *regs = *status->regs;
  mrb_sym *syms = *status->syms;
  mrb_code i = *pc;

  int a = GETARG_A(i);
  int n = GETARG_C(i);
  int ioff;
  mrb_callinfo *ci;
  mrb_value recv;
  mrb_sym mid = syms[GETARG_B(i)];

  recv = regs[a];

  //printf("%d %x %x %x %x %x\n", MRB_PROC_CFUNC_P(m), irep, pc,regs[a].ttt, regs, n);

  /* push callinfo */
  ci = mrbjit_cipush(mrb);
  ci->proc_pool = mrb->c->proc_pool;
  ci->mid = mid;
  ci->proc = m;
  ci->stackidx = mrb->c->stack - mrb->c->stbase;
  ci->argc = n;
  if (c->tt == MRB_TT_ICLASS) {
    ci->target_class = c->c;
  }
  else {
    ci->target_class = c;
  }
  ci->pc = pc + 1;
  ioff = ISEQ_OFFSET_OF(pc + 1);
  ci->acc = a;

  /* prepare stack */
  mrb->c->stack += a;

  /* setup environment for calling method */
  *status->proc = mrb->c->ci->proc = m;
  irep = *status->irep = m->body.irep;
  *status->pool = irep->pool;
  *status->syms = irep->syms;
  ci->nregs = irep->nregs;
  mrbjit_stack_extend(mrb, irep->nregs,  ci->argc+2);
  *status->regs = mrb->c->stack;
  *status->pc = irep->iseq;
  //mrb_p(mrb, recv);

  //printf("%d %x %x %x %x %x\n", MRB_PROC_CFUNC_P(m), irep, *status->pc,regs[a].ttt, regs, __builtin_return_address(0));
  //puts(mrb_sym2name(mrb, mid));
  return NULL;
}

void *
mrbjit_exec_enter(mrb_state *mrb, mrbjit_vmstatus *status)
{
  mrb_code *pc = *status->pc;
  mrb_value *regs = *status->regs;
  mrb_code i = *pc;

  //printf("enter %x %x \n", pc, mrb->c->ci);
  /* Ax             arg setup according to flags (24=5:5:1:5:5:1:1) */
  /* number of optional arguments times OP_JMP should follow */
  int ax = GETARG_Ax(i);
  int m1 = (ax>>18)&0x1f;
  int o  = (ax>>13)&0x1f;
  int r  = (ax>>12)&0x1;
  int m2 = (ax>>7)&0x1f;
  /* unused
     int k  = (ax>>2)&0x1f;
     int kd = (ax>>1)&0x1;
     int b  = (ax>>0)& 0x1;
  */
  int argc = mrb->c->ci->argc;
  mrb_value *argv = regs+1;
  mrb_value *argv0 = argv;
  int len = m1 + o + r + m2;
  mrb_value *blk = &argv[argc < 0 ? 1 : argc];

  if (argc < 0) {
    struct RArray *ary = mrb_ary_ptr(regs[1]);
    argv = ary->ptr;
    argc = ary->len;
    mrb_gc_protect(mrb, regs[1]);
  }
  if (mrb->c->ci->proc && MRB_PROC_STRICT_P(mrb->c->ci->proc)) {
    if (argc >= 0) {
      if (argc < m1 + m2 || (r == 0 && argc > len)) {
	mrbjit_argnum_error(mrb, m1+m2);
	return status->gototable[0]; /* L_RAISE */
      }
    }
  }
  else if (len > 1 && argc == 1 && mrb_array_p(argv[0])) {
    argc = mrb_ary_ptr(argv[0])->len;
    argv = mrb_ary_ptr(argv[0])->ptr;
  }
  mrb->c->ci->argc = len;
  if (argc < len) {
    regs[len+1] = *blk; /* move block */
    if (argv0 != argv) {
      memmove(&regs[1], argv, sizeof(mrb_value)*(argc-m2)); /* m1 + o */
    }
    if (m2) {
      memmove(&regs[len-m2+1], &argv[argc-m2], sizeof(mrb_value)*m2); /* m2 */
    }
    if (r) {                  /* r */
      regs[m1+o+1] = mrb_ary_new_capa(mrb, 0);
    }
    if (o == 0) {
      *(status->pc) += 1;
      return NULL;
    }
    else
      *(status->pc) += argc - m1 - m2 + 1;
  }
  else {
    if (argv0 != argv) {
      memmove(&regs[1], argv, sizeof(mrb_value)*(m1+o)); /* m1 + o */
    }
    if (r) {                  /* r */
      regs[m1+o+1] = mrb_ary_new_from_values(mrb, argc-m1-o-m2, argv+m1+o);
    }
    if (m2) {
      memmove(&regs[m1+o+r+1], &argv[argc-m2], sizeof(mrb_value)*m2);
    }
    if (argv0 == argv) {
      regs[len+1] = *blk; /* move block */
    }
    *(status->pc) += o + 1;
    if (o == 0) {
      return NULL;
    }
  }

  return status->optable[GET_OPCODE(**(status->pc))];
}

void *
mrbjit_exec_return(mrb_state *mrb, mrbjit_vmstatus *status)
{
  mrb_code i = **(status->pc);
  void *rc = NULL;

  //printf("return %x\n", *status->irep);
  //printf("rc %x %x %s\n", *status->pc, mrb->c->ci, mrb_sym2name(mrb, mrb->c->ci->mid));
  //printf("%x\n", mrb->c->ci->jit_entry);

  /* A      return R(A) */
  if (mrb->exc) {
    mrb_callinfo *ci;
    int eidx;

  L_RAISE:
    ci = mrb->c->ci;
    mrb_obj_iv_ifnone(mrb, mrb->exc, mrb_intern(mrb, "lastpc"), mrb_voidp_value(*status->pc));
    mrb_obj_iv_set(mrb, mrb->exc, mrb_intern(mrb, "ciidx"), mrb_fixnum_value(ci - mrb->c->cibase));
    eidx = ci->eidx;
    if (ci == mrb->c->cibase) {
      if (ci->ridx == 0) {
	return status->gototable[4]; /* L_STOP */
      }
      else {
	return status->gototable[2]; /* L_RESCUE */
      }
    }
    while (ci[0].ridx == ci[-1].ridx) {
      mrbjit_cipop(mrb);
      ci = mrb->c->ci;
      mrb->c->proc_pool = ci->proc_pool;
      if (ci[1].acc < 0 && *status->prev_jmp) {
	mrb->jmp = *status->prev_jmp;
	longjmp(*(jmp_buf*)mrb->jmp, 1);
      }
      while (eidx > mrb->c->ci->eidx) {
        mrbjit_ecall(mrb, --eidx);
      }
      if (ci == mrb->c->cibase) {
	if (ci->ridx == 0) {
	  *status->regs = mrb->c->stack = mrb->c->stbase;
	  return status->gototable[4]; /* L_STOP */
	}
	break;
      }
    }
    *status->irep = ci->proc->body.irep;
    *status->pool = (*status->irep)->pool;
    *status->syms = (*status->irep)->syms;
    *status->regs = mrb->c->stack = mrb->c->stbase + ci[1].stackidx;
    *status->pc = mrb->c->rescue[--ci->ridx];
  }
  else {
    mrb_callinfo *ci = mrb->c->ci;

    int acc, eidx = mrb->c->ci->eidx;
    mrb_value v = (*status->regs)[GETARG_A(i)];

    switch (GETARG_B(i)) {
    case OP_R_RETURN:
      // Fall through to OP_R_NORMAL otherwise
      if ((*status->proc)->env && !MRB_PROC_STRICT_P(*status->proc)) {
	struct REnv *e = mrbjit_top_env(mrb, *status->proc);

	if (e->cioff < 0) {
	  mrbjit_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
	  goto L_RAISE;
	}
	ci = mrb->c->cibase + e->cioff;
	if (ci == mrb->c->cibase) {
	  mrbjit_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
	  goto L_RAISE;
	}
	rc = status->optable[GET_OPCODE(*ci->pc)];
	mrb->c->ci = ci;
	break;
      }
    case OP_R_NORMAL:
      if (ci == mrb->c->cibase) {
	if (!mrb->c->prev) { /* toplevel return */
	  mrbjit_localjump_error(mrb, LOCALJUMP_ERROR_RETURN);
	  goto L_RAISE;
	}
	if (mrb->c->prev->ci == mrb->c->prev->cibase) {
	  mrb_value exc = mrb_exc_new3(mrb, E_RUNTIME_ERROR, mrb_str_new(mrb, "double resume", 13));
	  mrb->exc = mrb_obj_ptr(exc);
	  goto L_RAISE;
	}
	/* automatic yield at the end */
	mrb->c->status = MRB_FIBER_TERMINATED;
	mrb->c = mrb->c->prev;
      }
      ci = mrb->c->ci;
      break;
    case OP_R_BREAK:
      if ((*status->proc)->env->cioff < 0) {
	mrbjit_localjump_error(mrb, LOCALJUMP_ERROR_BREAK);
	goto L_RAISE;
      }
      ci = mrb->c->ci = mrb->c->cibase + (*status->proc)->env->cioff + 1;
      rc = status->optable[GET_OPCODE(*ci->pc)];
      break;
    default:
      /* cannot happen */
      break;
    }
    mrbjit_cipop(mrb);
    mrb->c->proc_pool = ci->proc_pool;
    acc = ci->acc;
    *status->pc = ci->pc;
    *status->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    while (eidx > mrb->c->ci->eidx) {
      mrbjit_ecall(mrb, --eidx);
    }
    if (acc < 0) {
      mrb->jmp = *status->prev_jmp;
      return rc;		/* return v */
    }
    DEBUG(printf("from :%s\n", mrb_sym2name(mrb, ci->mid)));
    *status->proc = mrb->c->ci->proc;
    *status->irep = (*status->proc)->body.irep;
    *status->pool = (*status->irep)->pool;
    *status->syms = (*status->irep)->syms;

    (*status->regs)[acc] = v;

    // return status->optable[GET_OPCODE(*ci->pc)];
  }

  return rc;
}

void *
mrbjit_exec_call(mrb_state *mrb, mrbjit_vmstatus *status)
{
  mrb_callinfo *ci;
  mrb_value recv = mrb->c->stack[0];
  struct RProc *m = mrb_proc_ptr(recv);

  /* replace callinfo */
  ci = mrb->c->ci;
  ci->target_class = m->target_class;
  ci->proc = m;
  if (m->env) {
    if (m->env->mid) {
      ci->mid = m->env->mid;
    }
    if (!m->env->stack) {
      m->env->stack = mrb->c->stack;
    }
  }

  /* prepare stack */
  if (MRB_PROC_CFUNC_P(m)) {
    int orgdisflg = mrb->compile_info.disable_jit;
    mrb->compile_info.disable_jit = 1;
    recv = m->body.func(mrb, recv);
    mrb->compile_info.disable_jit = orgdisflg;
    mrb_gc_arena_restore(mrb, *status->ai);
    if (mrb->exc) return status->gototable[0]; /* L_RAISE */
    /* pop stackpos */
    ci = mrb->c->ci;
    *status->regs = mrb->c->stack = mrb->c->stbase + ci->stackidx;
    (*status->regs)[ci->acc] = recv;
    *status->pc = ci->pc;
    mrb->c->proc_pool = ci->proc_pool;
    mrbjit_cipop(mrb);
    *status->irep = mrb->c->ci->proc->body.irep;
    *status->pool = (*(status->irep))->pool;
    *status->syms = (*(status->irep))->syms;
  }
  else {
    /* setup environment for calling method */
    *status->proc = m;
    *status->irep = m->body.irep;
    if (!(*status->irep)) {
      mrb->c->stack[0] = mrb_nil_value();
      return NULL;
    }
    *status->pool = (*(status->irep))->pool;
    *status->syms = (*(status->irep))->syms;
    ci->nregs = (*(status->irep))->nregs;
    if (ci->argc < 0) {
      mrbjit_stack_extend(mrb, ((*(status->irep))->nregs < 3) ? 3 : (*(status->irep))->nregs, 3);
    }
    else {
      mrbjit_stack_extend(mrb, (*(status->irep))->nregs,  ci->argc+2);
    }
    *status->regs = mrb->c->stack;
    if (m->env) {
      (*(status->regs))[0] = m->env->stack[0];
    }
    else {
      (*(status->regs))[0] = mrb_obj_value(m->target_class);
    }
    *status->pc = m->body.irep->iseq;
    //printf("call %x %x\n", *status->irep, (*(status->irep))->jit_top_entry);
    //printf("%x\n", mrb->c->ci->jit_entry);
  }
  mrb->c->proc_pool = mrb->c->ci->proc_pool;

  return NULL;
}

void
disasm_once(mrb_state *mrb, mrb_irep *irep, mrb_code c)
{
  int i = 0;
  switch (GET_OPCODE(c)) {
  case OP_NOP:
    printf("OP_NOP\n");
    break;
  case OP_MOVE:
    printf("OP_MOVE\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
    break;
  case OP_LOADL:
    printf("OP_LOADL\tR%d\tL(%d)\n", GETARG_A(c), GETARG_Bx(c));
    break;
  case OP_LOADI:
    printf("OP_LOADI\tR%d\t%d\n", GETARG_A(c), GETARG_sBx(c));
    break;
  case OP_LOADSYM:
    printf("OP_LOADSYM\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_LOADNIL:
    printf("OP_LOADNIL\tR%d\n", GETARG_A(c));
    break;
  case OP_LOADSELF:
    printf("OP_LOADSELF\tR%d\n", GETARG_A(c));
    break;
  case OP_LOADT:
    printf("OP_LOADT\tR%d\n", GETARG_A(c));
    break;
  case OP_LOADF:
    printf("OP_LOADF\tR%d\n", GETARG_A(c));
    break;
  case OP_GETGLOBAL:
    printf("OP_GETGLOBAL\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_SETGLOBAL:
    printf("OP_SETGLOBAL\t:%s\tR%d\n",
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
	   GETARG_A(c));
    break;
  case OP_GETCONST:
    printf("OP_GETCONST\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_SETCONST:
    printf("OP_SETCONST\t:%s\tR%d\n",
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
	   GETARG_A(c));
    break;
  case OP_GETMCNST:
    printf("OP_GETMCNST\tR%d\tR%d::%s\n", GETARG_A(c), GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_SETMCNST:
    printf("OP_SETMCNST\tR%d::%s\tR%d\n", GETARG_A(c)+1,
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
	   GETARG_A(c));
    break;
  case OP_GETIV:
    printf("OP_GETIV\tR%d\t%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_SETIV:
    printf("OP_SETIV\t%s\tR%d\n",
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
	   GETARG_A(c));
    break;
  case OP_GETUPVAR:
    printf("OP_GETUPVAR\tR%d\t%d\t%d\n",
	   GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_SETUPVAR:
    printf("OP_SETUPVAR\tR%d\t%d\t%d\n",
	   GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_GETCV:
    printf("OP_GETCV\tR%d\t%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]));
    break;
  case OP_SETCV:
    printf("OP_SETCV\t%s\tR%d\n",
	   mrb_sym2name(mrb, irep->syms[GETARG_Bx(c)]),
	   GETARG_A(c));
    break;
  case OP_JMP:
    printf("OP_JMP\t\t%03d\n", i+GETARG_sBx(c));
    break;
  case OP_JMPIF:
    printf("OP_JMPIF\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
    break;
  case OP_JMPNOT:
    printf("OP_JMPNOT\tR%d\t%03d\n", GETARG_A(c), i+GETARG_sBx(c));
    break;
  case OP_SEND:
    printf("OP_SEND\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_SENDB:
    printf("OP_SENDB\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_FSEND:
    printf("OP_FSEND\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_CALL:
    printf("OP_CALL\tR%d\n", GETARG_A(c));
    break;
  case OP_TAILCALL:
    printf("OP_TAILCALL\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_SUPER:
    printf("OP_SUPER\tR%d\t%d\n", GETARG_A(c),
	   GETARG_C(c));
    break;
  case OP_ARGARY:
    printf("OP_ARGARY\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
	   (GETARG_Bx(c)>>10)&0x3f,
	   (GETARG_Bx(c)>>9)&0x1,
	   (GETARG_Bx(c)>>4)&0x1f,
	   (GETARG_Bx(c)>>0)&0xf);
    break;

  case OP_ENTER:
    printf("OP_ENTER\t%d:%d:%d:%d:%d:%d:%d\n",
	   (GETARG_Ax(c)>>18)&0x1f,
	   (GETARG_Ax(c)>>13)&0x1f,
	   (GETARG_Ax(c)>>12)&0x1,
	   (GETARG_Ax(c)>>7)&0x1f,
	   (GETARG_Ax(c)>>2)&0x1f,
	   (GETARG_Ax(c)>>1)&0x1,
	   GETARG_Ax(c) & 0x1);
    break;
  case OP_RETURN:
    printf("OP_RETURN\tR%d", GETARG_A(c));
    switch (GETARG_B(c)) {
    case OP_R_NORMAL:
      printf("\n"); break;
    case OP_R_RETURN:
      printf("\treturn\n"); break;
    case OP_R_BREAK:
      printf("\tbreak\n"); break;
    default:
      printf("\tbroken\n"); break;
      break;
    }
    break;
  case OP_BLKPUSH:
    printf("OP_BLKPUSH\tR%d\t%d:%d:%d:%d\n", GETARG_A(c),
	   (GETARG_Bx(c)>>10)&0x3f,
	   (GETARG_Bx(c)>>9)&0x1,
	   (GETARG_Bx(c)>>4)&0x1f,
	   (GETARG_Bx(c)>>0)&0xf);
    break;

  case OP_LAMBDA:
    printf("OP_LAMBDA\tR%d\tI(%+d)\t%d\n", GETARG_A(c), GETARG_b(c), GETARG_c(c));
    break;
  case OP_RANGE:
    printf("OP_RANGE\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_METHOD:
    printf("OP_METHOD\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
    break;

  case OP_ADD:
    printf("OP_ADD\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_ADDI:
    printf("OP_ADDI\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_SUB:
    printf("OP_SUB\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_SUBI:
    printf("OP_SUBI\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_MUL:
    printf("OP_MUL\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_DIV:
    printf("OP_DIV\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_LT:
    printf("OP_LT\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_LE:
    printf("OP_LE\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_GT:
    printf("OP_GT\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_GE:
    printf("OP_GE\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;
  case OP_EQ:
    printf("OP_EQ\tR%d\t:%s\t%d\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]),
	   GETARG_C(c));
    break;

  case OP_STOP:
    printf("OP_STOP\n");
    break;

  case OP_ARRAY:
    printf("OP_ARRAY\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_ARYCAT:
    printf("OP_ARYCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
    break;
  case OP_ARYPUSH:
    printf("OP_ARYPUSH\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
    break;
  case OP_AREF:
    printf("OP_AREF\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_APOST:
    printf("OP_APOST\tR%d\t%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  case OP_STRING:
    {
      /*      mrb_value s = irep->pool[GETARG_Bx(c)];
	
      s = mrb_str_dump(mrb, s);
      printf("OP_STRING\tR%d\t%s\n", GETARG_A(c), RSTRING_PTR(s));*/
      printf("OP_STRING\n");
    }
    break;
  case OP_STRCAT:
    printf("OP_STRCAT\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
    break;
  case OP_HASH:
    printf("OP_HASH\tR%d\tR%d\t%d\n", GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;

  case OP_OCLASS:
    printf("OP_OCLASS\tR%d\n", GETARG_A(c));
    break;
  case OP_CLASS:
    printf("OP_CLASS\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
    break;
  case OP_MODULE:
    printf("OP_MODULE\tR%d\t:%s\n", GETARG_A(c),
	   mrb_sym2name(mrb, irep->syms[GETARG_B(c)]));
    break;
  case OP_EXEC:
    printf("OP_EXEC\tR%d\tI(%d)\n", GETARG_A(c), GETARG_Bx(c));
    break;
  case OP_SCLASS:
    printf("OP_SCLASS\tR%d\tR%d\n", GETARG_A(c), GETARG_B(c));
    break;
  case OP_TCLASS:
    printf("OP_TCLASS\tR%d\n", GETARG_A(c));
    break;
  case OP_ERR:
    printf("OP_ERR\tL(%d)\n", GETARG_Bx(c));
    break;
  case OP_EPUSH:
    printf("OP_EPUSH\t:I(%d)\n", GETARG_Bx(c));
    break;
  case OP_ONERR:
    printf("OP_ONERR\t%03d\n", i+GETARG_sBx(c));
    break;
  case OP_RESCUE:
    printf("OP_RESCUE\tR%d\n", GETARG_A(c));
    break;
  case OP_RAISE:
    printf("OP_RAISE\tR%d\n", GETARG_A(c));
    break;
  case OP_POPERR:
    printf("OP_POPERR\t%d\n", GETARG_A(c));
    break;
  case OP_EPOP:
    printf("OP_EPOP\t%d\n", GETARG_A(c));
    break;

  default:
    printf("OP_unknown %d\t%d\t%d\t%d\n", GET_OPCODE(c),
	   GETARG_A(c), GETARG_B(c), GETARG_C(c));
    break;
  }
}

mrb_irep *
search_irep(mrb_state *mrb, mrb_code *pc)
{
  int i = 0;
  for (i = 0; i < mrb->irep_len; i++) {
    mrb_irep *irep = mrb->irep[i];
    if (irep->iseq <= pc && pc <= irep->iseq + irep->ilen) {
      return irep;
    }
  }

  return NULL;
}

void
disasm_irep(mrb_state *mrb, mrb_irep *irep)
{
  int i = 0;
  for (i = 0; i < irep->ilen; i++) {
    printf("%4x ", i);
    disasm_once(mrb, irep, irep->iseq[i]);
  }
}

