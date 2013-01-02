/*
** mruby/jitcode.h - Class for XBYAK
**
** See Copyright Notice in mruby.h
*/

#ifndef MRUBY_JITCOD_H
#define MRUBY_JITCODE_H

#include <xbyak/xbyak.h>
extern "C" {
#include "mruby.h"
#include "opcode.h"

#include "mruby/irep.h"
#include "mruby/value.h"
#include "mruby/variable.h"
#include "mruby/proc.h"
#include "mruby/class.h"
#include "mruby/jit.h"
} /* extern "C" */

/* Regs Map                               *
 * ecx   -- pointer to regs               *
 * ebx   -- pointer to pc                 */
class MRBJitCode: public Xbyak::CodeGenerator {

 public:

 MRBJitCode():
  CodeGenerator(1024 * 1024)
  {
  }

  const void *
    gen_entry(mrb_state *mrb, mrb_irep *irep) 
  {
    const void* func_ptr = getCurr();
    return func_ptr;
  }

  void 
    gen_exit(mrb_code *pc) 
  {
    mov(dword [ebx], (Xbyak::uint32)pc);
    ret();
  }
  
  void 
    gen_jump_block(void *entry) 
  {
    jmp(entry);
  }

  void 
    gen_type_guard(enum mrb_vtype tt, mrb_code *pc)
  {
    /* Input eax for type tag */
    if (tt == MRB_TT_FLOAT) {
      cmp(eax, 0xfff00000);
      jb("@f");
    } 
    else {
      cmp(eax, 0xfff00000 | tt);
      jz("@f");
    }

    /* Guard fail exit code */
    mov(dword [ebx], (Xbyak::uint32)pc);
    ret();

    L("@@");
  }

  void
    gen_bool_guard(int b, mrb_code *pc)
  {
    /* Input eax for tested boolean */
    cmp(eax, 0xfff00001);
    if (b) {
      jnz("@f");
    } 
    else {
      jz("@f");
    }

    /* Guard fail exit code */
    mov(dword [ebx], (Xbyak::uint32)pc);
    ret();

    L("@@");
  }

  const void *
    emit_nop(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    return code;
  }

  const void *
    emit_move(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const Xbyak::uint32 srcoff = GETARG_B(**ppc) * sizeof(mrb_value);
    movsd(xmm0, ptr [ecx + srcoff]);
    movsd(ptr [ecx + dstoff], xmm0);
    return code;
  }

  const void *
    emit_loadl(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const Xbyak::uint32 srcoff = GETARG_Bx(**ppc) * sizeof(mrb_value);
    mov(eax, (Xbyak::uint32)irep->pool + srcoff);
    movsd(xmm0, ptr [eax]);
    movsd(ptr [ecx + dstoff], xmm0);

    return code;
  }

  const void *
    emit_loadi(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc) 
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const Xbyak::uint32 src = GETARG_sBx(**ppc);
    mov(eax, src);
    mov(dword [ecx + dstoff], eax);
    mov(eax, 0xfff00000 | MRB_TT_FIXNUM);
    mov(dword [ecx + dstoff + 4], eax);

    return code;
  }

  const void *
    emit_loadself(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc) 
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);

    movsd(xmm0, ptr [ecx]);
    movsd(ptr [ecx + dstoff], xmm0);
    return code;
  }

  const void *
    emit_loadt(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc) 
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    mov(eax, 1);
    mov(dword [ecx + dstoff], eax);
    mov(eax, 0xfff00000 | MRB_TT_TRUE);
    mov(dword [ecx + dstoff + 4], eax);

    return code;
  }

  const void *
    emit_loadf(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc) 
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    mov(eax, 1);
    mov(dword [ecx + dstoff], eax);
    mov(eax, 0xfff00000 | MRB_TT_FALSE);
    mov(dword [ecx + dstoff + 4], eax);

    return code;
  }

  const void *
    emit_getiv(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const int idpos = GETARG_Bx(**ppc);
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const int argsize = 2 * sizeof(void *);

    push(ecx);
    push(ebx);
    push((Xbyak::uint32)irep->syms[idpos]);
    push((Xbyak::uint32)mrb);
    call((void *)mrb_vm_iv_get);
    add(sp, argsize);
    pop(ebx);
    pop(ecx);
    mov(dword [ecx + dstoff], eax);
    mov(dword [ecx + dstoff + 4], edx);

    return code;
  }

  const void *
    emit_setiv(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const int idpos = GETARG_Bx(**ppc);
    const Xbyak::uint32 srcoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const int argsize = 4 * sizeof(void *);

    push(ecx);
    push(ebx);
    mov(eax, dword [ecx + srcoff + 4]);
    push(eax);
    mov(eax, dword [ecx + srcoff]);
    push(eax);
    push((Xbyak::uint32)irep->syms[idpos]);
    push((Xbyak::uint32)mrb);
    call((void *)mrb_vm_iv_set);
    add(sp, argsize);
    pop(ebx);
    pop(ecx);

    return code;
  }

  const void *
    emit_getconst(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const int sympos = GETARG_Bx(**ppc);
    const mrb_value v = mrb_vm_const_get(mrb, irep->syms[sympos]);

    mov(dword [ecx + dstoff], v.value.i);
    mov(dword [ecx + dstoff + 4], v.ttt);
    
    return code;
  }

  const void *
    emit_loadnil(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc) 
  {
    const void *code = getCurr();
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    xor(eax, eax);
    mov(dword [ecx + dstoff], eax);
    mov(eax, 0xfff00000 | MRB_TT_FALSE);
    mov(dword [ecx + dstoff + 4], eax);

    return code;
  }

#define OffsetOf(s_type, field) ((size_t) &((s_type *)0)->field) 
#define CALL_MAXARGS 127

  const void *
    emit_send(mrb_state *mrb, mrbjit_vmstatus *status)
  {
    const void *code = getCurr();

    const mrb_code *pc = *status->pc;
    const mrb_value *pool = *status->pool;
    const int i = *pc;
    const int rcvoff = GETARG_Bx(*(pc + 1));
    struct RClass *orecv = (struct RClass *)pool[rcvoff].value.p;

    const int a = GETARG_A(i);
    const int aoff = (Xbyak::uint32)a * sizeof(mrb_value);
    const mrb_value recv = (*status->regs)[a];
    const struct RClass *c = mrb_class(mrb, recv);

    const int mthoff = rcvoff + 1;
    const struct RProc *m = (struct RProc *)pool[mthoff].value.p;

    const int n = GETARG_C(i);

    const mrb_sym mid = (*status->syms)[GETARG_B(i)];

    if (orecv != c) {
      /* IC miss */
      return NULL;
    }
    if (n == CALL_MAXARGS) {
      /* Variable arguments */
      return NULL;
    }

    if (!MRB_PROC_CFUNC_P(m)) {
      return NULL;
    }

    /* Check IC is match */
    mov(eax, (Xbyak::uint32)orecv);
    mov(eax, ptr [eax]);
    mov(edx, (Xbyak::uint32)c);
    mov(edx, ptr [edx]);
    cmp(eax, edx);
    jz("@f");
    mov(dword [ebx], (Xbyak::uint32)pc);
    ret();
    L("@@");
    
    if (GET_OPCODE(i) != OP_SENDB) {
      const Xbyak::uint32 blkoff = (a + n + 1) * sizeof(mrb_value);

      /* Store block reg to nil */
      xor(eax, eax);
      mov(dword [ecx + blkoff], eax);
      mov(eax, 0xfff00001);
      mov(dword [ecx + blkoff + 4], eax);
    }

    push(ecx);
    push(ebx);
    push((Xbyak::uint32)mrb);
    call((void *)mrbjit_cipush);
    add(esp, 4);
    pop(ebx);
    pop(ecx);

    mov(dword [eax + OffsetOf(mrb_callinfo, mid)], (Xbyak::uint32)mid);
    mov(dword [eax + OffsetOf(mrb_callinfo, proc)], (Xbyak::uint32)m);
    mov(dword [eax + OffsetOf(mrb_callinfo, argc)], (Xbyak::uint32)n);
    mov(dword [eax + OffsetOf(mrb_callinfo, target_class)], (Xbyak::uint32)c);

    char *mrbb = (char *)mrb;
    mov(edx, ptr [mrbb + OffsetOf(mrb_state, stack)]);
    sub(edx, ptr [mrbb + OffsetOf(mrb_state, stbase)]);
    mov(dword [eax + OffsetOf(mrb_callinfo, stackidx)], edx);

    mov(dword [eax + OffsetOf(mrb_callinfo, argc)], (Xbyak::uint32)n);

    mov(dword [eax + OffsetOf(mrb_callinfo, pc)], (Xbyak::uint32)(pc + 1));
    mov(dword [eax + OffsetOf(mrb_callinfo, acc)], (Xbyak::uint32)a);

    mov(eax, (Xbyak::uint32)mrb + OffsetOf(mrb_state, stack));
    add(dword [eax], (Xbyak::uint32)aoff);
    
    if (MRB_PROC_CFUNC_P(m)) {
      mov(eax, ((Xbyak::uint32)mrb) + OffsetOf(mrb_state, ci));
      mov(dword [eax + OffsetOf(mrb_callinfo, nregs)], (Xbyak::uint32)(n + 2));

      push(ecx);
      push(ebx);

      mov(edx, (Xbyak::uint32)mrb + OffsetOf(mrb_state, stack));
      mov(edx, dword [edx]);
      mov(eax, dword [edx + 4]); /* recv.tt */
      push(eax);
      mov(eax, dword [edx]);	/* recv.value */
      push(eax);
      push((Xbyak::uint32)mrb);
      mov(eax, (Xbyak::uint32)m + OffsetOf(RProc, body.func));
      call(ptr [eax]);
      add(esp, 3 * sizeof(void *));
      mov(ebx, (Xbyak::uint32)mrb + OffsetOf(mrb_state, stack));
      mov(ebx, dword [ebx]);
      mov(dword [ebx], eax);
      mov(dword [ebx + 4], edx);

      mov(ecx, ptr [esp + 12]);	/* eax : status 12 means ret, ecx, ebx */
      mov(eax, ptr [ecx + OffsetOf(mrbjit_vmstatus, ai)]);
      mov(eax, ptr [eax]);	/* status->ai is pointer */
      push(eax);
      push((Xbyak::uint32)mrb);
      call((void *) mrb_gc_arena_restore);
      add(esp, 2 * sizeof(void *));

      mov(eax, (Xbyak::uint32)mrb + OffsetOf(mrb_state, ci));
      mov(eax, dword [eax]);
      mov(eax, dword [eax + OffsetOf(mrb_callinfo, stackidx)]);
      mov(edx, (Xbyak::uint32)mrb + OffsetOf(mrb_state, stbase));
      add(eax, dword [edx]);
      mov(edx, (Xbyak::uint32)mrb + OffsetOf(mrb_state, stack));
      mov(dword [edx], eax);
      mov(edx, dword [ecx + OffsetOf(mrbjit_vmstatus, regs)]);
      mov(dword [edx], eax);

      push((Xbyak::uint32)mrb);
      call((void *) mrbjit_cipop);
      add(esp, 1 * sizeof(void *));

      pop(ebx);
      pop(ecx);
    }
    else {
      printf("foo\n");
      push(ecx);
      push(ebx);

      mov(ecx, ptr [esp + 12]);	/* ecx : status */

      mov(ebx, ptr [ecx + OffsetOf(mrbjit_vmstatus, proc)]);
      mov(eax, (Xbyak::uint32)m);
      mov(dword [ebx], eax);
      mov(ebx, ((Xbyak::uint32)mrb) + OffsetOf(mrb_state, ci));
      mov(ebx, dword [ebx]);
      mov(dword [ebx + OffsetOf(mrb_callinfo, proc)], eax);

      mov(eax, ptr [eax + OffsetOf(struct RProc, body.irep)]);
      mov(dword [ecx + OffsetOf(mrbjit_vmstatus, irep)], eax);

      mov(ebx, ptr [eax + OffsetOf(mrb_irep, pool)]);
      mov(dword [ecx + OffsetOf(mrbjit_vmstatus, pool)], ebx);

      mov(ebx, ptr [eax + OffsetOf(mrb_irep, syms)]);
      mov(dword [ecx + OffsetOf(mrbjit_vmstatus, syms)], eax);
      
      mov(ebx, ptr [eax + OffsetOf(mrb_irep, nregs)]);
      mov(eax, ((Xbyak::uint32)mrb) + OffsetOf(mrb_state, ci));
      mov(dword [eax + OffsetOf(mrb_irep, nregs)], ebx);

      mov(eax, ptr [eax + OffsetOf(mrb_callinfo, argc)]); 
      add(eax, 2);
      push(eax);
      push(ebx);
      push((Xbyak::uint32)mrb);
      call((void *) mrbjit_stack_extend);
      add(esp, 3 * sizeof(void *));
      
      mov(eax, (Xbyak::uint32)mrb);
      mov(eax, ptr [eax + OffsetOf(mrb_state, stack)]);
      mov(dword [ecx + OffsetOf(mrbjit_vmstatus, irep)], eax);

      mov(eax, ptr [ecx + OffsetOf(mrbjit_vmstatus, irep)]);
      mov(eax, ptr [eax + OffsetOf(mrb_irep, iseq)]);
      mov(dword [ecx + OffsetOf(mrbjit_vmstatus, pc)], eax);
      
      pop(ebx);
      pop(ecx);
    }

    return code;
  }

#define OVERFLOW_CHECK_GEN(AINSTF)                                      \
    jno("@f");                                                          \
    sub(esp, 8);                                                        \
    movsd(qword [esp], xmm1);                                           \
    mov(eax, dword [ecx + reg0off]);                                    \
    cvtsi2sd(xmm0, eax);                                                \
    mov(eax, dword [ecx + reg1off]);                                    \
    cvtsi2sd(xmm1, eax);                                                \
    AINSTF(xmm0, xmm1);                                                 \
    movsd(dword [ecx + reg0off], xmm0);                                 \
    movsd(xmm1, ptr [esp]);                                             \
    add(esp, 8);                                                        \
    L("@@");                                                            \


#define ARTH_GEN(AINSTI, AINSTF)                                        \
  do {                                                                  \
    int reg0pos = GETARG_A(**ppc);                                      \
    int reg1pos = reg0pos + 1;                                          \
    const Xbyak::uint32 reg0off = reg0pos * sizeof(mrb_value);          \
    const Xbyak::uint32 reg1off = reg1pos * sizeof(mrb_value);          \
    enum mrb_vtype r0type = (enum mrb_vtype) mrb_type(regs[reg0pos]);   \
    enum mrb_vtype r1type = (enum mrb_vtype) mrb_type(regs[reg1pos]);   \
\
    if (r0type != r1type) {                                             \
      return NULL;                                                      \
    }                                                                   \
    mov(eax, dword [ecx + reg0off + 4]); /* Get type tag */             \
    gen_type_guard(r0type, *ppc);                                       \
    mov(eax, dword [ecx + reg1off + 4]); /* Get type tag */             \
    gen_type_guard(r1type, *ppc);                                       \
\
    if (r0type == MRB_TT_FIXNUM && r1type == MRB_TT_FIXNUM) {           \
      mov(eax, dword [ecx + reg0off]);                                  \
      AINSTI(eax, dword [ecx + reg1off]);			        \
      mov(dword [ecx + reg0off], eax);                                  \
      OVERFLOW_CHECK_GEN(AINSTF);                                       \
    }                                                                   \
    else if (r0type == MRB_TT_FLOAT && r1type == MRB_TT_FLOAT) {	\
      movsd(xmm0, ptr [ecx + reg0off]);                                 \
      AINSTF(xmm0, ptr [ecx + reg1off]);				\
      movsd(ptr [ecx + reg0off], xmm0);                                 \
    }                                                                   \
    else {                                                              \
      mov(dword [ebx], (Xbyak::uint32)*ppc);                            \
      ret();                                                            \
    }                                                                   \
} while(0)

  const void *
    emit_add(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    ARTH_GEN(add, addsd);
    return code;
  }

  const void *
    emit_sub(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    ARTH_GEN(sub, subsd);
    return code;
  }

  const void *
    emit_mul(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    ARTH_GEN(imul, mulsd);
    return code;
  }

  const void *
    emit_div(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    return code;
  }

#define OVERFLOW_CHECK_I_GEN(AINSTF)                                    \
    jno("@f");                                                          \
    sub(esp, 8);                                                        \
    movsd(qword [esp], xmm1);                                           \
    mov(eax, dword [ecx + off]);                                        \
    cvtsi2sd(xmm0, eax);                                                \
    mov(eax, y);                                                        \
    cvtsi2sd(xmm1, eax);                                                \
    AINSTF(xmm0, xmm1);                                                 \
    movsd(dword [ecx + off], xmm0);                                     \
    movsd(xmm1, ptr [esp]);                                             \
    add(esp, 8);                                                        \
    L("@@");                                                            \

#define ARTH_I_GEN(AINSTI, AINSTF)                                      \
  do {                                                                  \
    const Xbyak::uint32 y = GETARG_C(**ppc);                            \
    const Xbyak::uint32 off = GETARG_A(**ppc) * sizeof(mrb_value);      \
    int regno = GETARG_A(**ppc);                                        \
    enum mrb_vtype atype = (enum mrb_vtype) mrb_type(regs[regno]);      \
    mov(eax, dword [ecx + off + 4]); /* Get type tag */                 \
    gen_type_guard(atype, *ppc);                                        \
\
    if (atype == MRB_TT_FIXNUM) {                                       \
      mov(eax, dword [ecx + off]);                                      \
      AINSTI(eax, y);                                                   \
      mov(dword [ecx + off], eax);                                      \
      OVERFLOW_CHECK_I_GEN(AINSTF);                                     \
    }                                                                   \
    else if (atype == MRB_TT_FLOAT) {					\
      sub(esp, 8);                                                      \
      movsd(qword [esp], xmm1);                                         \
      movsd(xmm0, ptr [ecx + off]);                                     \
      mov(eax, y);                                                      \
      cvtsi2sd(xmm1, eax);                                              \
      AINSTF(xmm0, xmm1);                                               \
      movsd(ptr [ecx + off], xmm0);                                     \
      movsd(xmm1, ptr [esp]);                                           \
      add(esp, 8);                                                      \
    }                                                                   \
    else {                                                              \
      mov(dword [ebx], (Xbyak::uint32)*ppc);                            \
      ret();                                                            \
    }                                                                   \
} while(0)
    
  const void *
    emit_addi(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    ARTH_I_GEN(add, addsd);
    return code;
  }

  const void *
    emit_subi(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    ARTH_I_GEN(sub, subsd);
    return code;
  }

#define COMP_GEN_II(CMPINST)                                         \
do {                                                                 \
    mov(eax, dword [ecx + off0]);                                    \
    cmp(eax, dword [ecx + off1]);                                    \
    CMPINST(al);						     \
    mov(ah, 0);							     \
} while(0)

#define COMP_GEN_IF(CMPINST)                                         \
do {                                                                 \
    cvtsi2sd(xmm0, ptr [ecx + off0]);                                \
    xor(eax, eax);					             \
    comisd(xmm0, ptr [ecx + off1]);				     \
    CMPINST(al);						     \
} while(0)

#define COMP_GEN_FI(CMPINST)                                         \
do {                                                                 \
    sub(esp, 8); 					             \
    movsd(qword [esp], xmm1);   				     \
    movsd(xmm0, ptr [ecx + off0]);				     \
    cvtsi2sd(xmm1, ptr [ecx + off1]);                                \
    xor(eax, eax);					             \
    comisd(xmm0, xmm1);     			                     \
    CMPINST(al);						     \
    movsd(xmm1, ptr [esp]);					     \
    add(esp, 8);						     \
} while(0)

#define COMP_GEN_FF(CMPINST)                                         \
do {                                                                 \
    movsd(xmm0, dword [ecx + off0]);                                 \
    xor(eax, eax);					             \
    comisd(xmm0, ptr [ecx + off1]);				     \
    CMPINST(al);						     \
} while(0)
    
#define COMP_GEN(CMPINSTI, CMPINSTF)				     \
do {                                                                 \
    int regno = GETARG_A(**ppc);                                     \
    const Xbyak::uint32 off0 = regno * sizeof(mrb_value);            \
    const Xbyak::uint32 off1 = off0 + sizeof(mrb_value);             \
    mov(eax, dword [ecx + off0 + 4]); /* Get type tag */             \
    gen_type_guard((enum mrb_vtype)mrb_type(regs[regno]), *ppc);     \
    mov(eax, dword [ecx + off1 + 4]); /* Get type tag */             \
    gen_type_guard((enum mrb_vtype)mrb_type(regs[regno + 1]), *ppc); \
                                                                     \
    if (mrb_type(regs[regno]) == MRB_TT_FLOAT &&                     \
             mrb_type(regs[regno + 1]) == MRB_TT_FIXNUM) {           \
          COMP_GEN_FI(CMPINSTF);                                     \
    }                                                                \
    else if (mrb_type(regs[regno]) == MRB_TT_FIXNUM &&               \
             mrb_type(regs[regno + 1]) == MRB_TT_FLOAT) {            \
          COMP_GEN_IF(CMPINSTF);                                     \
    }                                                                \
    else if (mrb_type(regs[regno]) == MRB_TT_FLOAT &&                \
             mrb_type(regs[regno + 1]) == MRB_TT_FLOAT) {            \
          COMP_GEN_FF(CMPINSTF);                                     \
    }                                                                \
    else {                                                           \
          COMP_GEN_II(CMPINSTI);                                     \
    }                                                                \
    cwde();                                                          \
    add(eax, eax);                                                   \
    add(eax, 0xfff00001);                                            \
    mov(dword [ecx + off0 + 4], eax);                                \
    mov(dword [ecx + off0], 1);                                      \
 } while(0)
  
  const void *
    emit_eq(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    COMP_GEN(setz, setz);

    return code;
  }

  const void *
    emit_lt(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    COMP_GEN(setl, setb);

    return code;
  }

  const void *
    emit_le(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    COMP_GEN(setle, setbe);

    return code;
  }

  const void *
    emit_gt(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    COMP_GEN(setg, seta);

    return code;
  }

  const void *
    emit_ge(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs) 
  {
    const void *code = getCurr();
    COMP_GEN(setge, setae);

    return code;
  }

  const void *
    emit_getupvar(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const Xbyak::uint32 uppos = GETARG_C(**ppc);
    const Xbyak::uint32 idxpos = GETARG_B(**ppc);
    const Xbyak::uint32 dstoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const int argsize = 3 * sizeof(void *);

    push(ecx);
    push(ebx);
    push(idxpos);
    push(uppos);
    push((Xbyak::uint32)mrb);
    call((void *)mrb_uvget);
    add(sp, argsize);
    pop(ebx);
    pop(ecx);
    mov(dword [ecx + dstoff], eax);
    mov(dword [ecx + dstoff + 4], edx);

    return code;
  }

  const void *
    emit_setupvar(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc)
  {
    const void *code = getCurr();
    const Xbyak::uint32 uppos = GETARG_C(**ppc);
    const Xbyak::uint32 idxpos = GETARG_B(**ppc);
    const Xbyak::uint32 valoff = GETARG_A(**ppc) * sizeof(mrb_value);
    const int argsize = 5 * sizeof(void *);

    push(ecx);
    push(ebx);
    mov(eax, dword [ecx + valoff + 4]);
    push(eax);
    mov(eax, dword [ecx + valoff]);
    push(eax);
    push(idxpos);
    push(uppos);
    push((Xbyak::uint32)mrb);
    call((void *)mrb_uvset);
    add(sp, argsize);
    pop(ebx);
    pop(ecx);

    return code;
  }

  const void *
    emit_jmpif(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs)
  {
    const void *code = getCurr();
    const int cond = GETARG_A(**ppc);
    const Xbyak::uint32 coff =  cond * sizeof(mrb_value);
    
    mov(eax, ptr [ecx + coff + 4]);
    if (mrb_test(regs[cond])) {
      gen_bool_guard(1, *ppc + 1);
    }
    else {
      gen_bool_guard(0, *ppc + GETARG_sBx(**ppc));
    }

    return code;
  }

  const void *
    emit_jmpnot(mrb_state *mrb, mrb_irep *irep, mrb_code **ppc, mrb_value *regs)
  {
    const void *code = getCurr();
    const int cond = GETARG_A(**ppc);
    const Xbyak::uint32 coff =  cond * sizeof(mrb_value);
    
    mov(eax, ptr [ecx + coff + 4]);
    if (!mrb_test(regs[cond])) {
      gen_bool_guard(0, *ppc + 1);
    }
    else {
      gen_bool_guard(1, *ppc + GETARG_sBx(**ppc));
    }

    return code;
  }
};

#endif  /* MRUBY_JITCODE_H */
