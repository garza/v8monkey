/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=4 sw=4 et tw=78:
 *
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#ifndef jsinterp_h___
#define jsinterp_h___
/*
 * JS interpreter interface.
 */
#include "jsprvtd.h"
#include "jspubtd.h"
#include "jsfun.h"
#include "jsopcode.h"
#include "jsscript.h"

typedef struct JSFrameRegs {
    jsbytecode      *pc;            /* program counter */
    js::Value       *sp;            /* stack pointer */
} JSFrameRegs;

/* JS stack frame flags. */
enum JSFrameFlags {
    JSFRAME_CONSTRUCTING       =  0x01, /* frame is for a constructor invocation */
    JSFRAME_COMPUTED_THIS      =  0x02, /* frame.thisv was computed already and
                                           JSVAL_IS_OBJECT(thisv) */
    JSFRAME_ASSIGNING          =  0x04, /* a complex (not simplex JOF_ASSIGNING) op
                                           is currently assigning to a property */
    JSFRAME_DEBUGGER           =  0x08, /* frame for JS_EvaluateInStackFrame */
    JSFRAME_EVAL               =  0x10, /* frame for obj_eval */
    JSFRAME_FLOATING_GENERATOR =  0x20, /* frame copy stored in a generator obj */
    JSFRAME_YIELDING           =  0x40, /* js_Interpret dispatched JSOP_YIELD */
    JSFRAME_ITERATOR           =  0x80, /* trying to get an iterator for for-in */
    JSFRAME_GENERATOR          = 0x200, /* frame belongs to generator-iterator */
    JSFRAME_OVERRIDE_ARGS      = 0x400, /* overridden arguments local variable */

    JSFRAME_SPECIAL            = JSFRAME_DEBUGGER | JSFRAME_EVAL
};

/*
 * JS stack frame, may be allocated on the C stack by native callers.  Always
 * allocated on cx->stackPool for calls from the interpreter to an interpreted
 * function.
 *
 * NB: This struct is manually initialized in jsinterp.c and jsiter.c.  If you
 * add new members, update both files.
 */
struct JSStackFrame
{
    /* N.B. alignment (TODO: remove these members) */
    js::Value           thisv;          /* "this" pointer if in method */
    js::Value           rval;           /* function return value */

    jsbytecode          *imacpc;        /* null or interpreter macro call pc */
    JSObject            *callobj;       /* lazily created Call object */
    JSObject            *argsobj;       /* lazily created arguments object */
    JSScript            *script;        /* script being interpreted */
    JSFunction          *fun;           /* function being called or null */
    uintN               argc;           /* actual argument count */
    js::Value           *argv;          /* base of argument stack slots */
    void                *annotation;    /* used by Java security */

    /* Maintained by StackSpace operations */
    JSStackFrame        *down;          /* previous frame, part of
                                           stack layout invariant */
    jsbytecode          *savedPC;       /* only valid if cx->fp != this */
#ifdef DEBUG
    static jsbytecode *const sInvalidPC;
#endif

    /*
     * We can't determine in advance which local variables can live on
     * the stack and be freed when their dynamic scope ends, and which
     * will be closed over and need to live in the heap.  So we place
     * variables on the stack initially, note when they are closed
     * over, and copy those that are out to the heap when we leave
     * their dynamic scope.
     *
     * The bytecode compiler produces a tree of block objects
     * accompanying each JSScript representing those lexical blocks in
     * the script that have let-bound variables associated with them.
     * These block objects are never modified, and never become part
     * of any function's scope chain.  Their parent slots point to the
     * innermost block that encloses them, or are NULL in the
     * outermost blocks within a function or in eval or global code.
     *
     * When we are in the static scope of such a block, blockChain
     * points to its compiler-allocated block object; otherwise, it is
     * NULL.
     *
     * scopeChain is the current scope chain, including 'call' and
     * 'block' objects for those function calls and lexical blocks
     * whose static scope we are currently executing in, and 'with'
     * objects for with statements; the chain is typically terminated
     * by a global object.  However, as an optimization, the young end
     * of the chain omits block objects we have not yet cloned.  To
     * create a closure, we clone the missing blocks from blockChain
     * (which is always current), place them at the head of
     * scopeChain, and use that for the closure's scope chain.  If we
     * never close over a lexical block, we never place a mutable
     * clone of it on scopeChain.
     *
     * This lazy cloning is implemented in js_GetScopeChain, which is
     * also used in some other cases --- entering 'with' blocks, for
     * example.
     */
    JSObject        *scopeChain;
    JSObject        *blockChain;

    uint32          flags;          /* frame flags -- see below */
    JSStackFrame    *displaySave;   /* previous value of display entry for
                                       script->staticLevel */

    /* Members only needed for inline calls. */
    void            *hookData;      /* debugger call hook data */
    JSVersion       callerVersion;  /* dynamic version of calling script */

    void putActivationObjects(JSContext *cx) {
        /*
         * The order of calls here is important as js_PutCallObject needs to
         * access argsobj.
         */
        if (callobj) {
            js_PutCallObject(cx, this);
            JS_ASSERT(!argsobj);
        } else if (argsobj) {
            js_PutArgsObject(cx, this);
        }
    }

    /* Get the frame's current bytecode, assuming |this| is in |cx|. */
    jsbytecode *pc(JSContext *cx) const;

    js::Value *argEnd() const {
        return (js::Value *)this;
    }

    js::Value *slots() const {
        return (js::Value *)(this + 1);
    }

    js::Value *base() const {
        return slots() + script->nfixed;
    }

    const js::Value &calleeValue() {
        JS_ASSERT(argv);
        return argv[-2];
    }

    JSObject *callee() {
        return argv ? &argv[-2].asObject() : NULL;
    }

    /*
     * Get the object associated with the Execution Context's
     * VariableEnvironment (ES5 10.3). The given CallStack must contain this
     * stack frame.
     */
    JSObject *varobj(js::CallStack *cs) const;

    /* Short for: varobj(cx->activeCallStack()). */
    JSObject *varobj(JSContext *cx) const;

    inline JSObject *getThisObject(JSContext *cx);

    bool isGenerator() const { return flags & JSFRAME_GENERATOR; }
    bool isFloatingGenerator() const {
        JS_ASSERT_IF(flags & JSFRAME_FLOATING_GENERATOR, isGenerator());
        return flags & JSFRAME_FLOATING_GENERATOR;
    }
};

namespace js {

JS_STATIC_ASSERT(sizeof(JSStackFrame) % sizeof(Value) == 0);
static const size_t VALUES_PER_STACK_FRAME = sizeof(JSStackFrame) / sizeof(Value);

} /* namespace js */

static JS_INLINE uintN
GlobalVarCount(JSStackFrame *fp)
{
    JS_ASSERT(!fp->fun);
    return fp->script->nfixed;
}

/*
 * Refresh and return fp->scopeChain.  It may be stale if block scopes are
 * active but not yet reflected by objects in the scope chain.  If a block
 * scope contains a with, eval, XML filtering predicate, or similar such
 * dynamically scoped construct, then compile-time block scope at fp->blocks
 * must reflect at runtime.
 */
extern JSObject *
js_GetScopeChain(JSContext *cx, JSStackFrame *fp);

/*
 * Given a context and a vector of [callee, this, args...] for a function that
 * was specified with a JSFUN_THISP_PRIMITIVE flag, get the primitive value of
 * |this| into *thisvp. In doing so, if |this| is an object, insist it is an
 * instance of clasp and extract its private slot value to return via *thisvp.
 *
 * NB: this function loads and uses *vp before storing *thisvp, so the two may
 * alias the same Value.
 */
extern JSBool
js_GetPrimitiveThis(JSContext *cx, js::Value *vp, js::Class *clasp,
                    const js::Value **vpp);

namespace js {

/*
 * For a call with arguments argv including argv[-1] (nominal |this|) and
 * argv[-2] (callee) replace null |this| with callee's parent, replace
 * primitive values with the equivalent wrapper objects and censor activation
 * objects as, per ECMA-262, they may not be referred to by |this|. argv[-1]
 * must not be a JSVAL_VOID.
 */
extern bool
ComputeThisFromArgv(JSContext *cx, js::Value *argv);

JS_ALWAYS_INLINE JSObject *
ComputeThisObjectFromVp(JSContext *cx, js::Value *vp)
{
    extern bool ComputeThisFromArgv(JSContext *, js::Value *);
    return ComputeThisFromArgv(cx, vp + 2) ? &vp[1].asObject() : NULL;
}

JS_ALWAYS_INLINE bool
ComputeThisFromVpInPlace(JSContext *cx, js::Value *vp)
{
    extern bool ComputeThisFromArgv(JSContext *, js::Value *);
    return ComputeThisFromArgv(cx, vp + 2);
}

class PrimitiveValue
{
    static const unsigned THISP_MASK       = 0x7;
    static const unsigned THISP_ARRAY_SIZE = 8;
    static const unsigned THISP_SHIFT      = 8;

    void staticAssert() {
        JS_STATIC_ASSERT(JSFUN_THISP_PRIMITIVE >> THISP_SHIFT == THISP_MASK);
        JS_STATIC_ASSERT(THISP_MASK == THISP_ARRAY_SIZE - 1);
    }

    static const Value::MaskType Masks[THISP_ARRAY_SIZE];

  public:
    static bool test(JSFunction *fun, const Value &v) {
        return bool(Masks[(fun->flags >> THISP_SHIFT) & THISP_MASK] & v.mask);
    }
};

/*
 * The js::InvokeArgumentsGuard passed to js_Invoke must come from an
 * immediately-enclosing successful call to js::StackSpace::pushInvokeArgs,
 * i.e., there must have been no un-popped pushes to cx->stack(). Furthermore,
 * |args.getvp()[0]| should be the callee, |args.getvp()[1]| should be |this|,
 * and the range [args.getvp() + 2, args.getvp() + 2 + args.getArgc()) should
 * be initialized actual arguments.
 */
extern JS_REQUIRES_STACK bool
Invoke(JSContext *cx, const InvokeArgsGuard &args, uintN flags);

extern JS_REQUIRES_STACK JS_FRIEND_API(bool)
InvokeFriendAPI(JSContext *cx, const InvokeArgsGuard &args, uintN flags);

/*
 * Consolidated js_Invoke flags simply rename certain JSFRAME_* flags, so that
 * we can share bits stored in JSStackFrame.flags and passed to:
 *
 *   js_Invoke
 *   js_InternalInvoke
 *   js_ValueToFunction
 *   js_ValueToFunctionObject
 *   js_ValueToCallableObject
 *   js_ReportIsNotFunction
 *
 * See jsfun.h for the latter four and flag renaming macros.
 */
#define JSINVOKE_CONSTRUCT      JSFRAME_CONSTRUCTING
#define JSINVOKE_ITERATOR       JSFRAME_ITERATOR

/*
 * Mask to isolate construct and iterator flags for use with jsfun.h functions.
 */
#define JSINVOKE_FUNFLAGS       (JSINVOKE_CONSTRUCT | JSINVOKE_ITERATOR)

extern bool
InternalInvoke(JSContext *cx, JSObject *obj, const Value &fval, uintN flags,
               uintN argc, const Value *argv, Value *rval);

static JS_ALWAYS_INLINE bool
InternalCall(JSContext *cx, JSObject *obj, const Value &fval, uintN argc,
             const Value *argv, Value *rval)
{
    return InternalInvoke(cx, obj, fval, 0, argc, argv, rval);
}

static JS_ALWAYS_INLINE bool
InternalConstruct(JSContext *cx, JSObject *obj, const Value &fval, uintN argc,
                  const Value *argv, Value *rval)
{
    return InternalInvoke(cx, obj, fval, JSINVOKE_CONSTRUCT, argc, argv, rval);
}

extern bool
InternalGetOrSet(JSContext *cx, JSObject *obj, jsid id, const Value &fval,
                 JSAccessMode mode, uintN argc, const Value *argv, Value *rval);

extern JS_FORCES_STACK bool
Execute(JSContext *cx, JSObject *chain, JSScript *script,
        JSStackFrame *down, uintN flags, Value *result);

extern JS_REQUIRES_STACK bool
InvokeConstructor(JSContext *cx, const InvokeArgsGuard &args, JSBool clampReturn);

extern JS_REQUIRES_STACK bool
Interpret(JSContext *cx);

#define JSPROP_INITIALIZER 0x100   /* NB: Not a valid property attribute. */

extern bool
CheckRedeclaration(JSContext *cx, JSObject *obj, jsid id, uintN attrs,
                   JSObject **objp, JSProperty **propp);

extern bool
StrictlyEqual(JSContext *cx, const Value &lval, const Value &rval);

/* === except that NaN is the same as NaN and -0 is not the same as +0. */
extern bool
SameValue(JSContext *cx, const Value &v1, const Value &v2);

extern JSType
TypeOfValue(JSContext *cx, const js::Value &v);

inline bool
InstanceOf(JSContext *cx, JSObject *obj, Class *clasp, Value *argv)
{
    if (obj && obj->getClass() == clasp)
        return true;
    extern bool InstanceOfSlow(JSContext *, JSObject *, Class *, Value *);
    return InstanceOfSlow(cx, obj, clasp, argv);
}

inline void *
GetInstancePrivate(JSContext *cx, JSObject *obj, Class *clasp, Value *argv)
{
    if (!InstanceOf(cx, obj, clasp, argv))
        return NULL;
    return obj->getPrivate();
}

extern Value
BoxedWordToValue(jsboxedword w);

extern bool
ValueToBoxedWord(JSContext *cx, const Value &v, jsboxedword *w);

extern Value
IdToValue(jsid id);

extern bool
ValueToId(JSContext *cx, const Value &v, jsid *idp);

} /* namespace js */

/*
 * Given an active context, a static scope level, and an upvar cookie, return
 * the value of the upvar.
 */
extern const js::Value &
js_GetUpvar(JSContext *cx, uintN level, uintN cookie);

/*
 * JS_LONE_INTERPRET indicates that the compiler should see just the code for
 * the js_Interpret function when compiling jsinterp.cpp. The rest of the code
 * from the file should be visible only when compiling jsinvoke.cpp. It allows
 * platform builds to optimize selectively js_Interpret when the granularity
 * of the optimizations with the given compiler is a compilation unit.
 *
 * JS_STATIC_INTERPRET is the modifier for functions defined in jsinterp.cpp
 * that only js_Interpret calls. When JS_LONE_INTERPRET is true all such
 * functions are declared below.
 */
#ifndef JS_LONE_INTERPRET
# ifdef _MSC_VER
#  define JS_LONE_INTERPRET 0
# else
#  define JS_LONE_INTERPRET 1
# endif
#endif

#define JS_MAX_INLINE_CALL_COUNT 3000

#if !JS_LONE_INTERPRET
# define JS_STATIC_INTERPRET    static
#else
# define JS_STATIC_INTERPRET

extern JS_REQUIRES_STACK JSBool
js_EnterWith(JSContext *cx, jsint stackIndex);

extern JS_REQUIRES_STACK void
js_LeaveWith(JSContext *cx);

extern JS_REQUIRES_STACK js::Class *
js_IsActiveWithOrBlock(JSContext *cx, JSObject *obj, int stackDepth);

/*
 * Unwind block and scope chains to match the given depth. The function sets
 * fp->sp on return to stackDepth.
 */
extern JS_REQUIRES_STACK JSBool
js_UnwindScope(JSContext *cx, jsint stackDepth, JSBool normalUnwind);

extern JSBool
js_OnUnknownMethod(JSContext *cx, js::Value *vp);

/*
 * Find the results of incrementing or decrementing *vp. For pre-increments,
 * both *vp and *vp2 will contain the result on return. For post-increments,
 * vp will contain the original value converted to a number and vp2 will get
 * the result. Both vp and vp2 must be roots.
 */
extern JSBool
js_DoIncDec(JSContext *cx, const JSCodeSpec *cs, js::Value *vp, js::Value *vp2);

/*
 * Opcode tracing helper. When len is not 0, cx->fp->regs->pc[-len] gives the
 * previous opcode.
 */
extern JS_REQUIRES_STACK void
js_TraceOpcode(JSContext *cx);

/*
 * JS_OPMETER helper functions.
 */
extern void
js_MeterOpcodePair(JSOp op1, JSOp op2);

extern void
js_MeterSlotOpcode(JSOp op, uint32 slot);

#endif /* JS_LONE_INTERPRET */

inline JSObject *
JSStackFrame::getThisObject(JSContext *cx)
{
    if (flags & JSFRAME_COMPUTED_THIS)
        return &thisv.asObject();
    if (!js::ComputeThisFromArgv(cx, argv))
        return NULL;
    thisv = argv[-1];
    flags |= JSFRAME_COMPUTED_THIS;
    return &thisv.asObject();
}

#endif /* jsinterp_h___ */
