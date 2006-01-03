/*
 * v m . c				-- The STklos Virtual Machine
 * 
 * Copyright � 2000-2005 Erick Gallesio - I3S-CNRS/ESSI <eg@unice.fr>
 * 
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, 
 * USA.
 * 
 *           Author: Erick Gallesio [eg@unice.fr]
 *    Creation date:  1-Mar-2000 19:51 (eg)
 * Last file update: 23-Aug-2005 08:44 (eg)
 */
#include "stklos.h"
#include "object.h"
#include "vm.h"
#include "vm-instr.h"
#include "struct.h"

/* #define DEBUG_VM */
/* #define STAT_VM  */

#ifdef STAT_VM 
#  define DEBUG_VM
static int couple_instr[NB_VM_INSTR][NB_VM_INSTR];
static int cpt_inst[NB_VM_INSTR];
#endif

#ifdef DEBUG_VM
static int debug_level = 0;	/* 0 is quiet, 1, 2, ... are more verbose */
#endif


#if defined(__GNUC__) && !defined(DEBUG_VM)
   /* Use computed gotos to have better performances */
#  define USE_COMPUTED_GOTO
#  define CASE(x) 	lab_##x:
#  define NEXT		goto *jump_table[fetch_next()]
#else 
   /* Standard C compiler. Use the classic switch statement */
#  define CASE(x)	case x:
#  define NEXT		continue;/* Be sure to not use continue elsewhere */
#endif

#define NEXT0 		{valc = 0; NEXT;}
#define NEXT1		{valc = 1; NEXT;}


#ifdef sparc
#  define FLUSH_REGISTERS_WINDOW()	asm("t 0x3") /* Stolen in Elk 2.0 source */
#else
#  define FLUSH_REGISTERS_WINDOW()
#endif


#define MY_SETJMP(jb) 		(jb.blocked = get_signal_mask(), setjmp(jb.j))
#define MY_LONGJMP(jb, val)	(longjmp((jb).j, val))


static Inline sigset_t get_signal_mask(void)
{
  sigset_t new, old;

  sigemptyset(&new);
  sigprocmask(SIG_BLOCK, &new, &old);
  return old;
}

static Inline void set_signal_mask(sigset_t mask)
{
  sigprocmask(SIG_SETMASK, &mask, NULL);
}


/*===========================================================================*\
 * 
 * 			V M   S T A C K   &   C O D E 
 *
\*===========================================================================*/

static SCM *stack;
static int stack_len;


/* ==== Stack access macros ==== */
#define push(v)		(*(--sp) = (v))
#define pop()		(*(sp++))
#define IS_IN_STACKP(a)   ((stack <= (SCM*)(a)) && ((SCM*)(a) < &stack[stack_len]))

/* ==== Code access macros ==== */
#define fetch_next()	(*pc++)
#define fetch_const()	(constants[fetch_next()])
#define fetch_global()	(*(checked_globals[(unsigned) fetch_next()]))


/*===========================================================================*\
 * 
 * 			V M   R E G I S T E R S
 *
\*===========================================================================*/

static STk_instr *pc;		/* Program Counter		*/
static SCM *fp;			/* Frame pointer		*/
static SCM *sp;			/* Stack pointer		*/
static SCM val;			/* Current value register 	*/
static SCM env;			/* Current environment register */
static SCM *constants;		/* Constants of current code 	*/
static SCM *handlers;		/* Exceptions handlers		*/

static SCM r1, r2;		/* general registers		 */

#define MAX_VALS 8
static SCM vals[MAX_VALS];	/* registers for multiple values */
static int valc;		/* # of multiple values 	 */


static jbuf *top_jmp_buf = NULL;  


/*
 * Activation records 
 * 
 */

#define ACT_RECORD_SIZE	   7

#define ACT_VARARG(reg)	   ((reg)[0]) /* place holder for &rest parameters */
#define ACT_SAVE_ENV(reg)  ((reg)[1])
#define ACT_SAVE_PC(reg)   ((reg)[2])
#define ACT_SAVE_CST(reg)  ((reg)[3])
#define ACT_SAVE_FP(reg)   ((reg)[4])
#define ACT_SAVE_PROC(reg) ((reg)[5])
#define ACT_SAVE_INFO(reg) ((reg)[6])

/*
 * VM state
 *
 */ 
#define VM_STATE_SIZE 5
#define VM_STATE_PC(reg)	((reg)[0])
#define VM_STATE_CST(reg)	((reg)[1])
#define VM_STATE_ENV(reg) 	((reg)[2])
#define VM_STATE_FP(reg)  	((reg)[3])
#define VM_STATE_JUMP_BUF(reg)	((reg)[4])

#define SAVE_VM_STATE()			{		\
  sp 			-= VM_STATE_SIZE;		\
  VM_STATE_PC(sp)       = (SCM) pc;			\
  VM_STATE_CST(sp)      = (SCM) constants;		\
  VM_STATE_ENV(sp)      = (SCM) env;			\
  VM_STATE_FP(sp)       = (SCM) fp;			\
  VM_STATE_JUMP_BUF(sp) = (SCM) top_jmp_buf;		\
}

#define FULL_RESTORE_VM_STATE(p)	{			\
  pc		     = (STk_instr *) VM_STATE_PC(p);		\
  RESTORE_VM_STATE(p);						\
}

#define RESTORE_VM_STATE(p)		{			\
  /* pc is not restored here. See FULL_RESTORE_VM_STATE */	\
  constants          = (SCM *)  VM_STATE_CST(p);		\
  env                = (SCM)    VM_STATE_ENV(p);		\
  fp                 = (SCM *)  VM_STATE_FP(p);			\
  top_jmp_buf 	     = (jbuf *) VM_STATE_JUMP_BUF(p);		\
  sp         	    += VM_STATE_SIZE;				\
}


/*
 * Handlers
 *
 */
#define EXCEPTION_HANDLER_SIZE 3

#define HANDLER_PROC(reg) 	((reg)[0])
#define HANDLER_END(reg)	((reg)[1])
#define HANDLER_PREV(reg)	((reg)[2])


#define SAVE_HANDLER_STATE(proc, addr)  { 		\
  sp 		   -= EXCEPTION_HANDLER_SIZE;		\
  HANDLER_PROC(sp)  =  (SCM) (proc);			\
  HANDLER_END(sp)   =  (SCM) (addr);			\
  HANDLER_PREV(sp)  =  (SCM) handlers;			\
  handlers          = sp;				\
}

#define UNSAVE_HANDLER_STATE()  { 			\
  SCM *old = handlers;					\
							\
  handlers = (SCM *) HANDLER_PREV(handlers);		\
  sp       = old + EXCEPTION_HANDLER_SIZE;		\
}


/*===========================================================================*\
 * 
 * 			C A L L S
 *
\*===========================================================================*/

#define PREP_CALL() {					\
  SCM fp_save = (SCM) fp;				\
							\
  /* Push an activation record on the stack */		\
  sp -= ACT_RECORD_SIZE;				\
  fp  = sp;						\
  ACT_SAVE_FP(fp)   = fp_save;				\
  ACT_SAVE_PROC(fp) = STk_false;			\
  ACT_SAVE_INFO(fp) = STk_false;			\
  /* Other fields will be initialized later */		\
}


#define RET_CALL() {					\
  sp 	    = fp + ACT_RECORD_SIZE;			\
  env       = ACT_SAVE_ENV(fp);				\
  pc        = ACT_SAVE_PC(fp);				\
  constants = ACT_SAVE_CST(fp);				\
  fp        = ACT_SAVE_FP(fp);				\
}


/* 
 * 		         M i s c .
 */

#define CHECK_GLOBAL_INIT_SIZE	50
static SCM** checked_globals;
static int   checked_globals_len  = CHECK_GLOBAL_INIT_SIZE;
static int   checked_globals_used = 0;


#define FIRST_BYTE(n)  ((n) >> 8)
#define SECOND_BYTE(n) ((n) & 0xff)




#define PUSH_ENV(nargs, func, next_env)  {	\
    BOXED_TYPE(sp)   = tc_frame;		\
    FRAME_LENGTH(sp) = nargs;			\
    FRAME_NEXT(sp)   = next_env;		\
    FRAME_OWNER(sp)  = func;			\
}

#define CALL_CLOSURE(func) {			\
    pc        = CLOSURE_BCODE(func);		\
    constants = CLOSURE_CONST(func);		\
    env       = (SCM) sp;			\
}

#define CALL_PRIM(v, args) {			\
    ACT_SAVE_PROC(fp) = v;			\
    v = PRIMITIVE_FUNC(v)args;			\
}

#define REG_CALL_PRIM(name) {			\
  extern SCM CPP_CONCAT(STk_o_, name)();	\
  ACT_SAVE_PROC(fp) = CPP_CONCAT(STk_o_, name);	\
}


#define RETURN_FROM_PRIMITIVE() {		\
    sp = fp + ACT_RECORD_SIZE;			\
    fp = (SCM *) ACT_SAVE_FP(fp);		\
}

static void run_vm(STk_instr *code, SCM *constants, SCM envt);


/*===========================================================================*\
 * 
 * 				Utilities
 * 
\*===========================================================================*/

#ifdef DEBUG_VM
void STk_print_vm_registers(char *msg, STk_instr *code)
{
  if (IS_IN_STACKP(env))
    STk_fprintf(STk_stderr, "%s VAL=~S PC=%d SP=%d FP=%d CST=%x ENV=%x (%d)\n", 
		msg, val, pc-code, sp-stack, fp-stack, constants, env, 
		(SCM*)env-stack);
  else
    STk_fprintf(STk_stderr, "%s VAL=~S PC=%d SP=%d FP=%d CST=%x ENV=%x (%d)", 
		msg, val, pc-code, sp-stack, fp-stack, constants, env, 
		(SCM*)env-stack);
}

#endif


static Inline SCM listify_top(int n)
{
  SCM *p, res = STk_nil;

  for (p = sp, sp+=n; p < sp; p++)
    res = STk_cons(*p, res);
  return res;
}


static Inline SCM clone_env(SCM e)
{
  /* clone environment til we find one which is in the heap */
  if (FRAMEP(e) && IS_IN_STACKP(e)) {
    e = STk_clone_frame(e);
    FRAME_NEXT(e) = clone_env((SCM) FRAME_NEXT(e));
  }
  return e;
}


static void error_bad_arity(SCM func, int arity, short given_args, SCM *fp)
{
   ACT_SAVE_PROC(fp) = func;
  if (arity >= 0)
    STk_error("%d argument%s required in call to ~S (%d provided)",
	      arity, ((arity>1)? "s": ""), func, given_args);
  else
    STk_error("~S requires at least %d argument%s (%d provided)",
	      func, -arity-1, ((arity>1)? "s" : ""), given_args);
}


static Inline short adjust_arity(SCM func, short nargs, SCM *fp)
{
  short arity = CLOSURE_ARITY(func);

  if (arity != nargs) {
    if (arity >= 0) 
      error_bad_arity(func, arity, nargs, fp);
    else {						/* nary procedure call */
      short min_arity = -arity-1;

      if (nargs < min_arity)
	error_bad_arity(func, arity, nargs, fp); 
      else { /* Make a list from the arguments which are on the stack. */
	SCM res = STk_nil;
	
	while (nargs-- > min_arity) res = STk_cons(pop(), res);
	push(res);
      }
      return -arity;
    }
  }
  return arity;
}


/* Add a new global reference to the table of checked references */
static int add_global(SCM *ref)
{
  if (checked_globals_used >= checked_globals_len) {
    /* resize the checked global array */
    checked_globals_len += checked_globals_len / 2;
    checked_globals      = STk_must_realloc(checked_globals, 
					    checked_globals_len * sizeof(SCM*));
  }
  checked_globals[checked_globals_used] = ref;
  return checked_globals_used++;
}


/*===========================================================================*\
 * 
 * 				      C A L L S
 *
\*===========================================================================*/

/*
<doc  apply
 * (apply proc arg1 ... args)
 *
 * |Proc| must be a procedure and |args| must be a list. Calls |proc| with the
 * elements of the list 
 * @lisp
 * (append (list arg1 ...) args)
 * @end lisp 
 * as the actual arguments.
 * @lisp
 * (apply + (list 3 4))              =>  7
 *
 * (define compose
 *   (lambda (f g)
 *      (lambda args
 *        (f (apply g args)))))
 * 
 * ((compose sqrt *) 12 75)          =>  30
 * @end lisp
doc>
 */
DEFINE_PRIMITIVE("apply", scheme_apply, apply, (int argc))
{
  SCM l, func, *tmp, *argv;
  int len, nargs;

  if (argc == 0) STk_error("no function given");

  nargs = argc - 1;
  argv  = sp + nargs;
  func  = *argv;

  if (nargs > 0) {
    /* look at last argument */
    l   = *sp;
    len = STk_int_length(l);

    if (len < 0)
      STk_error("last argument ~S is not a list", l);
    else {
      /* move all the arguments, except the last one, one cell lower in the 
       * stack (i.e. overwrite the function to call) */
      for (tmp = argv-1; tmp > sp; tmp--)
	*(tmp+1) = *tmp;

      sp = tmp + 2;
      if (len != 0) {
	/* Unfold the last argument in place */
	while (!NULLP(l)) {
	  push(CAR(l));
	  l = CDR(l);
	}
      }
      nargs += len-1;
    }
  }

  /* Place the function to apply and its number of arguments in R1 and R2 */
  r1 = func;
  r2 = AS_SCM(nargs);

  return STk_apply_call;
}


/*===========================================================================*\
 *
 * 				S T k _ C _ a p p l y
 *
 *
 * Execute a Scheme function from C. This function can be used as a 
 * an "excv" or an "execl" function. If nargs is > 0 it is as a Unix "execl" 
 * function: 
 *    STk_C_apply(STk_cons, 2, MAKE_INT(1), MAKE_INT(2)) => (1 . 2)
 * If nargs is < 0, we have something similar to an "execv fucntion
 *    STk_C_apply(...STk_intern("cons")..., -2, Argv)
 * where Argv[0] == MAKE_INT(1) and Argv[1] == MAKE_INT(2) ==> (1 . 2)
 *
\*===========================================================================*/

SCM STk_C_apply(SCM func, int nargs, ...) 
{
  static STk_instr code[]= {INVOKE, 0, END_OF_CODE};
  va_list ap;
  int i;

  va_start(ap, nargs);
  //  sp -= VM_STATE_SIZE;
  SAVE_VM_STATE();				    /* Save the VM regs */
  PREP_CALL();					    /* PREPARE_CALL */

  if (nargs < 0) {				    /* Push the arguments */
    /* args are in argc/argv form */
    SCM *argv = va_arg(ap, SCM*);

    nargs = -nargs;
    
    for (i = 0; i < nargs; i++) push(*argv++);
  } else {
    /* We have nargs SCM parameters to read */
    for (i = 0; i < nargs; i++) push(va_arg(ap, SCM));
  }

  val     = func;				    /* Store fun in VAL */
  code[1] = (short) nargs;			    /* Patch # of args  */

  run_vm(code, constants, env);
  FULL_RESTORE_VM_STATE(sp);
  
  return val;
}


DEFINE_PRIMITIVE("%execute", execute, subr23, (SCM code, SCM consts, SCM envt))
{
  int i, len;
  STk_instr *vinstr, *p;

  if (!envt) envt = STk_current_module;
  
  if (!VECTORP(code)) 	STk_error("bad code vector ~S", code);
  if (!VECTORP(consts)) STk_error("bad constant list ~S", consts);
  if (!MODULEP(envt))   STk_error("bad module for evaluation ~S", envt);
  
  /* convert code to a vector of instructions */
  len = VECTOR_SIZE(code);
  vinstr = p = STk_must_malloc(len * sizeof(STk_instr));

  for (i = 0; i < len; i++)
    *p++ = (STk_instr) STk_integer_value(VECTOR_DATA(code)[i]);

  SAVE_VM_STATE();
  run_vm(vinstr, VECTOR_DATA(consts), envt);
  FULL_RESTORE_VM_STATE(sp);

  return val;
}


/*===========================================================================*\
 * 
 * 				V A L U E S
 *
\*===========================================================================*/
/*
<doc values
 * (values obj ...)
 *
 * Delivers all of its arguments to its continuation. 
 * ,(bold "Note:") R5RS imposes to use multiple values in the context of 
 * of a |call-with-values|. In STklos, if |values| is not used with 
 * |call-with-values|, only the first value is used (i.e. others values are 
 * ,(emph "ignored")).
 *
doc>
*/
DEFINE_PRIMITIVE("values", values, vsubr, (int argc, SCM *argv))
{
  int i;

  if (argc == 0)
    val = STk_void;
  else {
    val = argv[0];
    if (argc <= MAX_VALS) {
      for (i = 1; i < argc; i++)
	vals[i] = argv[-i];
    } else {
      /* More than MAX_VALS values. Use a vector and store it in vals[0] */
      SCM tmp = STk_makevect(argc, (SCM) NULL);
      
      for (i = 0; i < argc; i++) VECTOR_DATA(tmp)[i] = *argv--;
      vals[0] = tmp;
    }
  }
  
  /* Retain in valc the number of values */
  valc = argc;
  return val;
}

/*
<doc call-with-values
 * (call-with-values producer consumer)
 *
 * Calls its producer argument with no values and a continuation that, 
 * when passed some values, calls the consumer procedure with those values 
 * as arguments. The continuation for the call to consumer is the 
 * continuation of the call to call-with-values. 
 * @lisp
 * (call-with-values (lambda () (values 4 5))
 *                   (lambda (a b) b))                =>  5
 *
 * (call-with-values * -)                             =>  -1
 * @end lisp
doc>
 */
DEFINE_PRIMITIVE("call-with-values", call_with_values, subr2, (SCM prod, SCM con))
{
  int tmp;

  if (!STk_procedurep(prod)) STk_error("bad producer", prod);
  if (!STk_procedurep(con))  STk_error("bad consumer", con);
  
  val  = STk_C_apply(prod, 0);
  tmp  = valc;

  if (tmp == 0)
    return STk_C_apply(con, 0);
  else if (tmp == 1)
    return STk_C_apply(con, 1, val);
  else if (tmp <= MAX_VALS) {
    vals[0] = val;
    return STk_C_apply(con , -tmp, vals);
  } else {
    return STk_C_apply(con, -tmp, VECTOR_DATA(vals[0]));
  }
}


SCM STk_n_values(int n, ...)
{
  valc = n;
  
  if (!n)
    val = STk_void;
  else {
    va_list ap;
    int i;

    va_start(ap, n);
    val = va_arg(ap, SCM);

    if (n <= MAX_VALS) {
      for (i = 1; i < n; i++)
	vals[i] = va_arg(ap, SCM);
    } else {
      /* More than MAX_VALS values. Use a vector and store it in vals[0] */
      SCM tmp = STk_makevect(n, (SCM) NULL);
      
      for (i = 0; i < n; i++) VECTOR_DATA(tmp)[i] = va_arg(ap, SCM);
      vals[0] = tmp;
    }
  }
  return val;
}

/*===========================================================================*\
 * 
 * 				V M _ D E B U G
 *
\*===========================================================================*/

/* Add support for debugging 
 * vm_debug is called wiemacs th the kind of desired support and sp. It returns 
 * the number of elements used on the stack
 */

static void vm_debug(int kind, SCM *fp)
{
  switch (kind) {
  case 0: /* old trace code position. Don't use it anymode */
    {
      SCM line = val;
      SCM file = pop();
      STk_panic("Recompile code in file ~S (contains obsolete line informations)",
		file, line);
      break;
    }
  case 1: /* Embed line information in a procedure call */
    {
      SCM line = val;
      
      ACT_SAVE_INFO(fp) = STk_cons(pop(), line);
      break; 
    }
  }
}

DEFINE_PRIMITIVE("%vm-backtrace", vm_bt, subr0, (void))
{  
  SCM res, *lfp;
  
  res = STk_nil;
  for (lfp = fp; lfp; lfp = ACT_SAVE_FP(lfp)) {
    SCM self = (SCM) (ACT_SAVE_PROC(lfp));
    
    if (!self) break;

    res = STk_cons(STk_cons(self, ACT_SAVE_INFO(lfp)), 
		   res);
  }
  return STk_dreverse(res);
}



#ifdef DEBUG_VM
#  ifdef STAT_VM
#    define DEFINE_NAME_TABLE
#    include "vm-instr.h"

static void dump_couple_instr(void)
{
  int i, j;
  FILE *dump;

  dump = fopen("/tmp/dump.out", "w");
  fprintf(dump, "[\n");

  for (i = NOP; i < NB_VM_INSTR; i++) {
    fprintf(dump, "((%s %d) ", name_table[i], cpt_inst[i]);
    for (j = NOP; j < NB_VM_INSTR; j++)
      fprintf(dump, "(%s %4d) ", name_table[j], couple_instr[i][j]);
    fprintf(dump, ")\n");
  }
  fprintf(dump, "\n]\n");
}
# endif

DEFINE_PRIMITIVE("%vm", set_vm_debug, vsubr, (int argc, SCM *argv))
{
  /* 
   * This function is just a placeholder for debugging the VM. It's body is 
   * changed depending of the current bug to track 
   */

}
#endif 



/*===========================================================================*\
 * 
 *	 	       S T k l o s   V i r t u a l   M a c h i n e 
 *
\*===========================================================================*/

static void run_vm(STk_instr *code, SCM *consts, SCM envt)
{
  jbuf jb; 
  jbuf *old_jb = NULL; 		/* to make Gcc happy */
  short offset, nargs=0;
  short tailp;
#if defined(USE_COMPUTED_GOTO) 
#  define DEFINE_JUMP_TABLE
#  include "vm-instr.h"
#else
   short byteop;
#endif
#if defined(DEBUG_VM)
#    define DEFINE_NAME_TABLE
#    include "vm-instr.h"
  static STk_instr *code_base = NULL;
#endif
#if defined(STAT_VM)
  static short previous_op = NOP;
#endif

  constants   = consts;
  env         = envt;
  pc	      = code;


#if defined(USE_COMPUTED_GOTO)
  NEXT;
#else
  for ( ; ; ) {		
    /* Execution loop */
    byteop = fetch_next();
#  ifdef DEBUG_VM
    if (debug_level > 1)
      fprintf(stderr, "%08x [%03d]: %20s  sp=%-6d fp=%-6d env=%x\n", 
	      pc - 1,
	      pc-code_base-1, 
	      name_table[(int)byteop], sp-stack, fp-stack, (int) env);
#    ifdef STAT_VM
    couple_instr[previous_op][byteop]++;
    cpt_inst[byteop]++;
    previous_op = byteop;
#    endif
#  endif
    switch (byteop) {
#endif /*  USE_COMPUTED_GOTO */


CASE(NOP) { NEXT; }


CASE(IM_FALSE)  { val = STk_false;       NEXT1;}
CASE(IM_TRUE)   { val = STk_true;        NEXT1;}
CASE(IM_NIL)    { val = STk_nil;         NEXT1;}
CASE(IM_MINUS1) { val = MAKE_INT(-1);    NEXT1;}
CASE(IM_ZERO)   { val = MAKE_INT(0);     NEXT1;}
CASE(IM_ONE)    { val = MAKE_INT(1);     NEXT1;}
CASE(IM_VOID)   { val = STk_void; 	 NEXT1;}

CASE(SMALL_INT) { val = MAKE_INT(fetch_next());		NEXT1;}
CASE(CONSTANT)  { val = fetch_const();	        	NEXT1;}

CASE(FALSE_PUSH)  { push(val = STk_false);       NEXT1;}
CASE(TRUE_PUSH)   { push(val = STk_true);        NEXT1;}
CASE(NIL_PUSH)    { push(val = STk_nil);         NEXT1;}
CASE(MINUS1_PUSH) { push(val = MAKE_INT(-1));    NEXT1;}
CASE(ZERO_PUSH)   { push(val = MAKE_INT( 0));    NEXT1;}
CASE(ONE_PUSH)    { push(val = MAKE_INT(+1));    NEXT1;}
CASE(VOID_PUSH)   { push(val = STk_void);        NEXT1;}


CASE(INT_PUSH)      { push(val=MAKE_INT(fetch_next())) ; NEXT1; }
CASE(CONSTANT_PUSH) { push(val=fetch_const()); 		 NEXT1; }


CASE(GLOBAL_REF) {
  SCM ref;

  val = STk_lookup(fetch_const(), env, &ref, TRUE);
  /* patch the code for optimize next accesses */
  pc[-2] = UGLOBAL_REF;
  pc[-1] = add_global(&CDR(ref));
  NEXT1;
}
CASE(UGLOBAL_REF) { /* Never produced by compiler */ val = fetch_global(); NEXT1; }

CASE(GREF_INVOKE) {
  SCM ref;

  val   = STk_lookup(fetch_const(), env, &ref, TRUE);
  nargs = fetch_next();
  /* patch the code for optimize next accesses (pc[-1] is already equal to nargs)*/
  pc[-3] = UGREF_INVOKE;
  pc[-2] = add_global(&CDR(ref));

  /*and now invoke */
  tailp=FALSE; goto FUNCALL;
}

CASE(UGREF_INVOKE) { /* Never produced by compiler */ 
  val   = fetch_global();
  nargs = fetch_next();
  
  /* invoke */
  tailp = FALSE; goto FUNCALL;
}

CASE(LOCAL_REF0) { val = FRAME_LOCAL(env, 0); NEXT1;}
CASE(LOCAL_REF1) { val = FRAME_LOCAL(env, 1); NEXT1;}
CASE(LOCAL_REF2) { val = FRAME_LOCAL(env, 2); NEXT1;}
CASE(LOCAL_REF3) { val = FRAME_LOCAL(env, 3); NEXT1;}
CASE(LOCAL_REF4) { val = FRAME_LOCAL(env, 4); NEXT1;}
CASE(LOCAL_REF)  { val = FRAME_LOCAL(env, fetch_next()); NEXT1;}
CASE(DEEP_LOCAL_REF) {
  int level, info = fetch_next();
  SCM e = env;

  /* Go down in the dynamic environment */
  for (level = FIRST_BYTE(info); level; level--)
    e = (SCM) FRAME_NEXT(e);

  val = FRAME_LOCAL(e, SECOND_BYTE(info));
  NEXT1;
}


CASE(GLOBAL_SET) {
  SCM ref;
  
  STk_lookup(fetch_const(), env, &ref, TRUE);
  CDR(ref) = val;
  val = STk_void;
  /* patch the code for optimize next accesses */
  pc[-2] = UGLOBAL_SET;
  pc[-1] = add_global(&CDR(ref));
  NEXT0;
}
CASE(UGLOBAL_SET) { /* Never produced by compiler */
  fetch_global() = val; val = STk_void; NEXT0;
}

CASE(LOCAL_SET0) { FRAME_LOCAL(env, 0)         = val; val = STk_void; NEXT0;}
CASE(LOCAL_SET1) { FRAME_LOCAL(env, 1)         = val; val = STk_void; NEXT0;}
CASE(LOCAL_SET2) { FRAME_LOCAL(env, 2)         = val; val = STk_void; NEXT0;}
CASE(LOCAL_SET3) { FRAME_LOCAL(env, 3)         = val; val = STk_void; NEXT0;}
CASE(LOCAL_SET4) { FRAME_LOCAL(env, 4)         = val; val = STk_void; NEXT0;}
CASE(LOCAL_SET)  { FRAME_LOCAL(env,fetch_next())=val; val = STk_void; NEXT0;}
CASE(DEEP_LOCAL_SET) {
  int level, info = fetch_next();
  SCM e = env;

  /* Go down in the dynamic environment */
  for (level = FIRST_BYTE(info); level; level--)
    e = (SCM) FRAME_NEXT(e);

  FRAME_LOCAL(e, SECOND_BYTE(info)) = val;
  val = STk_void;
  NEXT0;
}


CASE(GOTO) { offset = fetch_next(); pc += offset; NEXT;}
CASE(JUMP_FALSE) { 
  offset = fetch_next(); 
  if (val == STk_false) pc += offset;
  NEXT;
}
CASE(JUMP_TRUE) {
  offset = fetch_next();
  if (val != STk_false) pc += offset;
  NEXT;
}

CASE(JUMP_NUMDIFF) {
  offset = fetch_next(); if (!STk_numeq2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NUMEQ) {
  offset = fetch_next(); if (STk_numeq2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NUMLT) {
  offset = fetch_next(); if (STk_numlt2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NUMLE) {
  offset = fetch_next(); if (STk_numle2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NUMGT) {
  offset = fetch_next(); if (STk_numgt2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NUMGE) {
  offset = fetch_next(); if (STk_numge2(pop(), val)) pc += offset; NEXT;
}
CASE(JUMP_NOT_EQ) {
  offset = fetch_next(); if ((pop() != val)) pc += offset; NEXT;
}
CASE(JUMP_NOT_EQV) {
  offset = fetch_next(); if ((STk_eqv(pop(), val) == STk_false)) pc += offset; NEXT;
}
CASE(JUMP_NOT_EQUAL) {
  offset = fetch_next(); if ((STk_equal(pop(), val)==STk_false)) pc += offset; NEXT;
}


CASE(DEFINE_SYMBOL) {
  SCM var = fetch_const();

  STk_define_variable(var, val, env);
  if (CLOSUREP(val) && CLOSURE_NAME(val) == STk_false) CLOSURE_NAME(val) = var;
  val     = STk_void;
  vals[1] = var;
  valc    = 2;
  NEXT;
}


CASE(SET_CUR_MOD) {
  if (!MODULEP(val)) STk_error("bad module ~S", val);
  STk_current_module = env = val;
  val = STk_void;
  NEXT0;
}


CASE(POP)     { val = pop(); NEXT1; }
CASE(PUSH)    { push(val);   NEXT; }

CASE(DBG_VM)  { vm_debug(fetch_next(), fp); NEXT; }


CASE(CREATE_CLOSURE) {
  /* pc[0] = offset; pc[1] = arity ; code of the routine starts in pc+2 */
  env    = clone_env(env);
  val    = STk_make_closure(pc+2, pc[0]-1, pc[1], constants, env);
  pc    += pc[0] + 1;
  NEXT1;
}


CASE(PREPARE_CALL) { PREP_CALL(); NEXT; }
CASE(RETURN) 	   { RET_CALL();  NEXT; }
CASE(INVOKE)       {
  nargs = fetch_next();
  tailp = FALSE; goto FUNCALL;
}

CASE(TAIL_INVOKE) {
  nargs = fetch_next();
  tailp = TRUE;
  goto FUNCALL;
}

CASE(ENTER_LET_STAR) {
  nargs = fetch_next();
  
  /* roughly equivalent to PREPARE_CALL; nargs * PUSH; ENTER_LET */
  PREP_CALL();
  sp -= nargs + (sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM);
  PUSH_ENV(nargs, val, env);
  env = (SCM) sp;
  NEXT;
}

CASE(ENTER_LET) {
  nargs = fetch_next();

  /* Push a new env. on the stack. Activation record does not need to be updated  */
  sp -= (sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM);
  PUSH_ENV(nargs, val, env);
  env = (SCM) sp;
  NEXT;
}

CASE(LEAVE_LET) {
  sp  = fp + ACT_RECORD_SIZE;
  env = FRAME_NEXT(env);
  fp  = ACT_SAVE_FP(fp);
  NEXT;
}


CASE(ENTER_TAIL_LET_STAR) { 
  nargs = fetch_next();

  /* roughly equivalent to PREPARE_CALL; nargs * PUSH; ENTER_TAIL_LET */
  PREP_CALL();
  sp -= nargs;
  goto enter_tail_let;
}

CASE(ENTER_TAIL_LET) {
  nargs = fetch_next();
 enter_tail_let:
  {
    SCM *old_fp = (SCM *) ACT_SAVE_FP(fp);
    
    /* Move the arguments of the function to the old_fp as in TAIL_INVOKE */
    if (IS_IN_STACKP(env)) {
      if (nargs) memmove((SCM*)env-nargs, sp, nargs*sizeof(SCM));
      fp = old_fp;
      
      /* Push a new environment on the stack */
      sp = (SCM*)env-nargs-((sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM));
    }
    else {
      if (nargs) memmove(old_fp-nargs, sp, nargs*sizeof(SCM));
      fp = old_fp;
      sp = fp - nargs - ((sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM));
    }
    
    PUSH_ENV(nargs, val, env);
    env  = (SCM) sp;
    NEXT;
  }
}


CASE(PUSH_HANDLER) {
  int offset = fetch_next();

  /* place the value in val on the stack as well as the value of handlers */
  if (STk_procedurep(val) == STk_false)
    STk_error("bad exception handler ~S", val);

  old_jb      = top_jmp_buf;
  top_jmp_buf = &jb;

  if (MY_SETJMP(jb)) {
    /* We come back from an error. */
    set_signal_mask(jb.blocked);
  } 
  else {
    SAVE_VM_STATE();
    SAVE_HANDLER_STATE(val, pc+offset);
  }
  NEXT;
}

CASE(POP_HANDLER) {
  UNSAVE_HANDLER_STATE();
  RESTORE_VM_STATE(sp);
  top_jmp_buf = old_jb;
  NEXT;
}


CASE(MAKE_EXPANDER) {
  SCM name = fetch_const();
  SCM ref;

  STk_lookup(STk_intern("*expander-list*"), STk_current_module, &ref, TRUE);
  CDR(ref) = STk_cons(STk_cons(name, val), CDR(ref));
  valc    = 2;
  val     = STk_void;
  vals[1] = name;
  NEXT;
}

CASE(END_OF_CODE) { 
  return;
}

 
CASE(UNUSED_1) {
}

CASE(UNUSED_2) {
}
  


/******************************************************************************
 *
 * 			     I n l i n e d   F u n c t i o n s
 *
 ******************************************************************************/
#define SCHEME_NOT(x) (((x) == STk_false) ? STk_true: STk_false)


CASE(IN_ADD2)   { REG_CALL_PRIM(plus);           val = STk_add2(pop(), val); NEXT1;}
CASE(IN_SUB2)   { REG_CALL_PRIM(difference);     val = STk_sub2(pop(), val); NEXT1;}
CASE(IN_MUL2)   { REG_CALL_PRIM(multiplication); val = STk_mul2(pop(), val); NEXT1;}
CASE(IN_DIV2)   { REG_CALL_PRIM(division);       val = STk_div2(pop(), val); NEXT1;}

CASE(IN_NUMEQ)  { REG_CALL_PRIM(numeq); 
  		  val = MAKE_BOOLEAN(STk_numeq2(pop(), val));      NEXT1;}
CASE(IN_NUMDIFF){ REG_CALL_PRIM(numeq);
		  val = MAKE_BOOLEAN(!STk_numeq2(pop(), val));     NEXT1;}
CASE(IN_NUMLT)  { REG_CALL_PRIM(numlt); 
  		  val = MAKE_BOOLEAN(STk_numlt2(pop(), val));      NEXT1;}
CASE(IN_NUMGT)  { REG_CALL_PRIM(numgt); 
  		  val = MAKE_BOOLEAN(STk_numgt2(pop(), val));      NEXT1;}
CASE(IN_NUMLE)  { REG_CALL_PRIM(numle);
  		  val = MAKE_BOOLEAN(STk_numle2(pop(), val));      NEXT1;}
CASE(IN_NUMGE)  { REG_CALL_PRIM(numge);
		  val = MAKE_BOOLEAN(STk_numge2(pop(), val));      NEXT1;}

CASE(IN_INCR)   { REG_CALL_PRIM(plus);      val = STk_add2(val, MAKE_INT(1)); NEXT1;}
CASE(IN_DECR)   { REG_CALL_PRIM(difference);val = STk_sub2(val, MAKE_INT(1)); NEXT1;}

CASE(IN_CONS)   { val = STk_cons(pop(), val);		           NEXT1;}
CASE(IN_CAR)    { REG_CALL_PRIM(car); val = STk_car(val); 	   NEXT1;}
CASE(IN_CDR)    { REG_CALL_PRIM(cdr); val = STk_cdr(val); 	   NEXT1;}
CASE(IN_NULLP)  { val = MAKE_BOOLEAN(val == STk_nil);	           NEXT1;}
CASE(IN_LIST)   { val = listify_top(fetch_next());	           NEXT1;}
CASE(IN_NOT)    { val = SCHEME_NOT(val); 			   NEXT1;}

CASE(IN_EQUAL)  { val = STk_equal(pop(), val);			   NEXT1;}
CASE(IN_EQV)    { val = STk_eqv(pop(), val);			   NEXT1;}
CASE(IN_EQ)     { val = MAKE_BOOLEAN(pop() == val);		   NEXT1;}

CASE(IN_NOT_EQUAL) { val = SCHEME_NOT(STk_equal(pop(), val)); 	   NEXT1; }
CASE(IN_NOT_EQV)   { val = SCHEME_NOT(STk_eqv(pop(), val)); 	   NEXT1; }
CASE(IN_NOT_EQ)    { val = MAKE_BOOLEAN(pop() != val);		   NEXT1; }

CASE(IN_VREF) { 
  REG_CALL_PRIM(vector_ref);
  val = STk_vector_ref(pop(), val);
  NEXT1;
}
CASE(IN_SREF) {
  REG_CALL_PRIM(string_ref);
  val = STk_string_ref(pop(), val);
  NEXT1;
}
CASE(IN_VSET)   {
  SCM index = pop();
  REG_CALL_PRIM(vector_set);
  val = STk_vector_set(pop(), index, val);
  NEXT0;
}
CASE(IN_SSET)   { 
  SCM index = pop();
  REG_CALL_PRIM(string_set);
  val = STk_string_set(pop(), index, val);
  NEXT0;
}

CASE(IN_APPLY)   {
  STk_panic("INSTRUCTION IN-APPLY!!!!!!!!!!!!!!!!!!!!!!!");
  NEXT;
}

/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
FUNCALL:  /* (int nargs, int tailp) */
{
  switch (STYPE(val)) {

    case tc_instance: {
      if (PUREGENERICP(val)) {
	SCM *argv = sp+nargs-1;
	SCM methods, nm; 
	
	/* methods is the list of applicable methods. Apply the first
	 * one with the tail of the list as first parameter
	 * (next-method). If methods is STk_nil, that's because the
	 * no-applicable-method triggered didn't yield an error.  
	 */
	methods = STk_compute_applicable_methods(val, nargs, argv, FALSE);
	if (NULLP(methods)) { val = STk_void; return; }
	
	/* Place the procedure of the first method in the VAL register and
	 * store the next method in the ``next-method'' variable. 
	 */
	nm   = STk_make_next_method(val, nargs, argv, methods);
	val  = INST_SLOT(CAR(methods), S_procedure);
	SET_NEXT_METHOD(val, nm);
	/* NO BREAK */
      } else {
	SCM gf, args;

	/* Use the MOP and do the call (apply-generic gf args) */
	args = listify_top(nargs);
	push(val); 
	push(args);
	val = STk_lookup(STk_intern("apply-generic"), STk_current_module, 
			 &gf, FALSE);
	nargs = 2;
	goto FUNCALL;
      }
    }

    case tc_closure: {
      nargs = adjust_arity(val, nargs, fp);

      if (tailp) {
	/* Tail call: Reuse the old frame for this call.*/
	SCM *old_fp = (SCM *) ACT_SAVE_FP(fp);
	
	/* Move the arguments of the function to the old_fp */
	if (nargs) memcpy(old_fp-nargs, sp, nargs*sizeof(SCM));
	fp = old_fp;
	
	/* Push a new environment on the stack */
	sp = fp - nargs - ((sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM));
	PUSH_ENV(nargs, val, CLOSURE_ENV(val));
      } else {
	/* Push a new environment on the stack */
	sp -= (sizeof(struct frame_obj) - sizeof(SCM)) / sizeof(SCM);
	PUSH_ENV(nargs, val, CLOSURE_ENV(val));
      
	/* Finish initialisation of current activation record */
	ACT_SAVE_ENV(fp)  = env;
	ACT_SAVE_PC(fp)   = pc;
	ACT_SAVE_CST(fp)  = constants;
      }

      ACT_SAVE_PROC(fp) = val; 
      
      /* Do the call */
      CALL_CLOSURE(val);
      goto end_funcall;;
    }

    case tc_next_method: {
      SCM methods, nm, *argv, proc;
      int i;
      
      methods = NXT_MTHD_METHODS(val);
      
      if (nargs == 0) {
	/* no argument given, place the ones of the original call on top of stack */
	nargs = NXT_MTHD_ARGC(val);
	argv  = NXT_MTHD_ARGV(val);

	for (i = 0; i < nargs; i++) 
	  push(argv[i]);
      }
      
      argv  = sp+nargs-1;

      if (NULLP(methods)) {
	/* Do the call (no-next-method gf method args) */
	argv = listify_top(nargs);
	push(NXT_MTHD_GF(val));
	push(NXT_MTHD_METHOD(val));
	push(argv);
	nargs = 3;
	val   = STk_lookup(STk_intern("no-next-method"), STk_current_module, 
			   &proc, FALSE);
      } else {
	/* Call the next method after creating a new next-method */
	nm   = STk_make_next_method(val, nargs, argv, methods);
	val  = INST_SLOT(CAR(methods), S_procedure);
	SET_NEXT_METHOD(val, nm);
      }
      goto FUNCALL;
    }

    case tc_apply: {
      /* Call the function in the call frame of apply */
      STk_scheme_apply(nargs);
      nargs = (short) AS_LONG(r2); 
      val   = r1; 
      goto FUNCALL;
    }

    case tc_subr0:
      if (nargs == 0) { CALL_PRIM(val, ());				 break;}
      goto error_invoke;
    case tc_subr1:
      if (nargs == 1) { CALL_PRIM(val, (sp[0]));			 break;}
      goto error_invoke;
    case tc_subr2:
      if (nargs == 2) { CALL_PRIM(val, (sp[1], sp[0]));			 break;}
      goto error_invoke;
    case tc_subr3:
      if (nargs == 3) { CALL_PRIM(val, (sp[2], sp[1], sp[0]));		 break;}
      goto error_invoke;
    case tc_subr4:
      if (nargs==4) { CALL_PRIM(val, (sp[3], sp[2],sp[1], sp[0]));	 break;}
      goto error_invoke;
    case tc_subr5:
      if (nargs==5) { CALL_PRIM(val, (sp[4],sp[3],sp[2],sp[1],sp[0]));  break;}
      goto error_invoke;

    case tc_subr01:
      if (nargs == 0) { CALL_PRIM(val, ((SCM) NULL));			 break;}
      if (nargs == 1) { CALL_PRIM(val, (sp[0]));			 break;}
      goto error_invoke;
    case tc_subr12:
      if (nargs == 1) { CALL_PRIM(val, (sp[0], (SCM) NULL));		 break;}
      if (nargs == 2) { CALL_PRIM(val, (sp[1], sp[0]));			 break;}
      goto error_invoke;
    case tc_subr23:
      if (nargs == 2) { CALL_PRIM(val, (sp[1],sp[0],(SCM)NULL));	 break;}
      if (nargs == 3) { CALL_PRIM(val, (sp[2],sp[1],sp[0]));		 break;}
      goto error_invoke;
    case tc_vsubr: CALL_PRIM(val, (nargs, sp+nargs-1));			 break;

    case tc_parameter:
      if (nargs == 0) {val = STk_get_parameter(val);			break;}
      if (nargs == 1) {val = STk_set_parameter(val, sp[0]);		break;}
      goto error_invoke;
      
    default: 
      STk_error("bad function ~S. Cannot be applied", val);
    error_invoke:
      ACT_SAVE_PROC(fp) = val;
      /* We are here when we had a primitive call with a bad number of parameters */
      STk_error("incorrect number of parameters (%d) in call to ~S", nargs, val);

  }
  /* We are here when we have called a primitive */
  RETURN_FROM_PRIMITIVE();
end_funcall:
  NEXT;
}
/* ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */
#ifndef USE_COMPUTED_GOTO
      default:
	STk_panic("INSTRUCTION %d NOT IMPLEMENTED\n", byteop);
    }
  }
#endif
  STk_panic("abnormal exit from the VM");
}

void STk_raise_exception(SCM cond)
{
  SCM proc;
  SCM *save_vm_state = handlers + EXCEPTION_HANDLER_SIZE;

  if (!handlers) {
    STk_print(cond, STk_stderr, DSP_MODE);
    STk_fprintf(STk_stderr, ": ");
    STk_print(STk_int_struct_ref(cond, STk_intern("message")), 
	      STk_stderr, 
	      DSP_MODE);
    STk_fprintf(STk_stderr, "\n**** FATAL ERROR: no handler present!\nABORT\n");
    exit(1);
  }

  /*
   * Grab the handler infos 
   */
  proc  = (SCM) 	HANDLER_PROC(handlers);
  pc    = (STk_instr *) HANDLER_END(handlers);

  UNSAVE_HANDLER_STATE();
  
  RESTORE_VM_STATE(save_vm_state);

  /* Execute the procedure handler on behalf of the old handler (since the 
   * procedure can be itself erroneous). 
   */
  val = STk_C_apply(proc, 1, cond);

  /* 
   * Return in the good "run_vm" incarnation 
   */
  MY_LONGJMP(*top_jmp_buf, 1);
}

/*===========================================================================*\
 * 
 *			   C O N T I N U A T I O N S
 *
\*===========================================================================*/
void STk_get_stack_pointer(void **addr)
{
  char c;
  *addr = (void *) &c;
}


DEFINE_PRIMITIVE("%make-continuation", make_continuation, subr0, (void))
{
  SCM z;
  struct continuation_obj *k;
  int csize, ssize;
  void *cstart, *sstart, *cend, *send;
  void *addr;

  /* Determine the size of the C stack and the start address */
  STk_get_stack_pointer(&addr);
  if ((unsigned long) addr < (unsigned long) STk_start_stack) {
    csize  = (unsigned long) STk_start_stack - (unsigned long) addr;
    cstart = addr;
    cend   = STk_start_stack;
  } else {
    csize  = (unsigned long) addr - (unsigned long) STk_start_stack;
    cstart = STk_start_stack;
    cend   = addr;
  }

  /* Determine the size of the Scheme stack */
  sstart = sp;
  send   = stack + stack_len;
  ssize  = (send - sstart) * sizeof(SCM);

  /* Allocate a continuation object */
  NEWCELL_WITH_LEN(z, continuation, sizeof(struct continuation_obj) + ssize + csize);
  k = (struct continuation_obj *) z;

  k->csize 	= csize;
  k->cstart	= cstart;
  k->cend	= cend;

  k->ssize	= ssize;
  k->sstart	= sstart;
  k->send	= send;

  k->pc		 = pc;
  k->fp		 = fp;
  k->sp		 = sp;
  k->env	 = clone_env(env);
  k->constants	 = constants;
  k->handlers	 = handlers;
  k->jb		 = top_jmp_buf;

  /* Save the Scheme stack */
  //  k->sstack = STk_must_malloc(ssize);
  //  k->stacks = STk_must_malloc(ssize + csize);
  memcpy(k->stacks, sstart, ssize);

  /* Save the C stack */
  //  k->cstack = STk_must_malloc(csize);
  //memcpy(k->cstack, cstart, csize);
  memcpy(k->stacks + ssize, cstart, csize);

  k->fresh = 1;

  if (MY_SETJMP(k->state) == 0) {
    /* This is the initial call to %make_continuation */
    return z;
  } else {
    /* We come back and restore the continuation */
    return val;
  }
}

#define CALL_CC_SPACE	1024	/* Add some space for restoration bookeepping */

DEFINE_PRIMITIVE("%restore-continuation", restore_cont, subr2, (SCM cont, SCM value))
{
  struct continuation_obj *k;
  volatile void *p;
  void *addr;
  int cur_stack_size; 

  if (!CONTP(cont)) STk_error("bad continuation ~S", cont);

  k = (struct continuation_obj *) cont;

  val		= value;
  
  pc		= k->pc;
  fp		= k->fp;
  sp		= k->sp;
  env		= k->env;
  constants	= k->constants;
  handlers	= k->handlers;
  top_jmp_buf	= k->jb;
  

  k->fresh = 0;
  /* Restore the Scheme stack */
  memcpy(k->sstart, k->stacks, k->ssize);

  /* Restore the C stack */
  STk_get_stack_pointer(&addr);
  cur_stack_size = STk_start_stack - addr;
  if (cur_stack_size < 0) cur_stack_size = -cur_stack_size;
  if (cur_stack_size <= (k->csize + CALL_CC_SPACE))
    /* Allocate some space on stack to allow restoration */
    p = alloca(k->csize + CALL_CC_SPACE - cur_stack_size);
  //  memcpy(k->cstart, k->cstack, k->csize);
  memcpy(k->cstart, k->stacks + k->ssize, k->csize);
  
  /* Return */
  MY_LONGJMP(k->state, 1);

  return STk_void; /* never reached */
}

DEFINE_PRIMITIVE("%continuation?", continuationp, subr1, (SCM obj))
{
  return MAKE_BOOLEAN(CONTP(obj));
}

DEFINE_PRIMITIVE("%fresh-continuation?", fresh_continuationp, subr1, (SCM obj))
{
  return MAKE_BOOLEAN(CONTP(obj) && (((struct continuation_obj *) obj)->fresh));
}


static void print_continuation(SCM cont, SCM port, int mode)
{
  STk_fprintf(port, "#[continuation (C=%d S=%d) %x]", 
	      ((struct continuation_obj *)cont)->csize,
	      ((struct continuation_obj *)cont)->ssize,
	      (unsigned long) cont);
}

static struct extended_type_descr xtype_continuation = {
  "continuation",		/* name */
  print_continuation		/* print function */
};


/*===========================================================================*\
 * 
 *			   Bytecode file dump/load stuff 
 *
\*===========================================================================*/

static int system_has_booted = 0;



/* This function is used to dump the code in a file */
DEFINE_PRIMITIVE("%dump-code", dump_code, subr2, (SCM f, SCM v))
{
  int size, i;
  SCM *tmp;
  STk_instr instr;
  
  if (!FPORTP(f))  STk_error("bad file port ~S", f);
  if (!VECTORP(v)) STk_error("bad code vector ~S", v);

  size = VECTOR_SIZE(v); tmp = VECTOR_DATA(v);

  /* Print size as a Scheme value */
  STk_print(MAKE_INT(size), f, DSP_MODE);
  STk_putc('\n', f);

  /* Print the content of the vector as bytes */
  for (i = 0; i < size; i++) {
    if (!INTP(*tmp)) STk_error("bad value in code vector ~S", v);
   
    instr = (STk_instr) INT_VAL(*tmp++);
    STk_putc(FIRST_BYTE(instr), f);
    STk_putc(SECOND_BYTE(instr), f);
  
  }
  STk_putc('\n', f);
  return STk_void;
}


static Inline STk_instr* read_code(SCM f, int len) /* read a code phrase */
{
  STk_instr *res, *tmp;
  int i, c1, c2;

  tmp = res = STk_must_malloc_atomic(len * sizeof(STk_instr));

  /* skip the separator */
  STk_getc(f);

  /* Read 'len' instruction (coded on 2 bytes) */
  for (i = 0; i < len; i++) {
    c1 = STk_getc(f);
    c2 = STk_getc(f);
    if (c2 == EOF) /* not useful to test c1 */
      STk_error("truncated bytecode file ~S", f);
    
    *tmp++ = (STk_instr) (c1 << 8 | c2);
  }

  return res;  
}

SCM STk_load_bcode_file(SCM f)
{
  SCM consts, code_size, *save_constants, save_env;
  STk_instr *code, *save_pc;
  int size;

  /* Save machine state */
  save_pc = pc; save_constants = constants; save_env = env;

  /* Signature has been skipped during file type analysing */
  for ( ; ; ) {
    consts = STk_read_constant(f, TRUE); 		   /* Read  the constants */
    if (consts == STk_eof) break;

    code_size = STk_read(f, STk_read_case_sensitive);	    /* Read the code size */
    size      = STk_integer_value(code_size);
    if (size < 0) {
      if (system_has_booted)
	STk_error("Bad bytecode file ~S", f);
      else 
	return STk_false;
    }

    code = read_code(f, size);			    	   /* Read the code */
    run_vm(code, VECTOR_DATA(consts), STk_current_module); /* Execute code read */
  }
  
  /* restore machine state */
  pc = save_pc; constants = save_constants, env = save_env;
  return STk_true;
}


int STk_load_boot(char *filename)
{
  SCM f, tmp;

  f = STk_open_file(filename, "rb");
  if (f == STk_false) return -1;

  /* Verify that the file is a bytecode file */
  tmp = STk_read(f, TRUE);
  if (tmp != STk_intern("STklos")) return -2;
  STk_read(f, FALSE); /* Read the version -- unused for now */
  
  tmp = STk_load_bcode_file(f);
  if (tmp == STk_false) return -3;
   
  /* The system has booted on the given file */
  system_has_booted = 1;
  return 0;
}

int STk_boot_from_C(void)
{
  SCM port, consts;

  /* Get the constants */
  port = STk_open_C_string(STk_boot_consts);
  consts = STk_read(port, TRUE);
  /* Run the VM */
  run_vm(STk_boot_code, VECTOR_DATA(consts), STk_current_module); 
  system_has_booted = 1;
  return 0;
}


/*
 * Stack Allocation 
 * 
 */
void STk_allocate_stack(long n)
{
  stack_len = n;
  stack     = STk_must_malloc(n * sizeof(SCM));
  if (!stack) {
    fprintf(stderr, "Cannot allocate a stack with size %ld\n", n);
    fflush(stderr);
    exit(1);
  }
}


int STk_init_vm(void)
{
  /* Initialize the VM registers */
  sp       = stack + stack_len;
  fp       = sp;
  val      = STk_void;
  env      = STk_current_module;
  handlers = NULL;

  DEFINE_XTYPE(continuation, &xtype_continuation);

  /* Initialize the table of checked references */
  checked_globals = STk_must_malloc(checked_globals_len * sizeof(SCM));

  /* Add the apply primitive */
  ADD_PRIMITIVE(scheme_apply);
  ADD_PRIMITIVE(execute);
  ADD_PRIMITIVE(dump_code);
  ADD_PRIMITIVE(vm_bt);

  ADD_PRIMITIVE(values);
  ADD_PRIMITIVE(call_with_values);
  ADD_PRIMITIVE(make_continuation);
  ADD_PRIMITIVE(restore_cont);
  ADD_PRIMITIVE(continuationp);
  ADD_PRIMITIVE(fresh_continuationp);
#ifdef DEBUG_VM
  ADD_PRIMITIVE(set_vm_debug);
#endif
  return TRUE;
}
