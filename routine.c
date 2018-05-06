
#include "routine.h"
#include "moduleobject.h"
#include "codeobject.h"
#include "tupleobject.h"
#include "stringobject.h"
#include "koala_state.h"
#include "opcode.h"
#include "log.h"

#define TOP()   rt_stack_top(rt)
#define POP()   rt_stack_pop(rt)
#define PUSH(v) rt_stack_push(rt, (v))

static void frame_loop(Frame *frame);

/*-------------------------------------------------------------------------*/

static void frame_new(Routine *rt, Object *ob, Object *cob, int argc)
{
	CodeObject *code = OB_TYPE_OF(cob, CodeObject, Code_Klass);

	int size = 0;
	if (CODE_ISKFUNC(code)) size = code->kf.locvars;

	Frame *f = malloc(sizeof(Frame) + size * sizeof(TValue));
	init_list_head(&f->link);
	f->rt = rt;
	f->argc = argc;
	f->code = (Object *)code;
	f->pc = 0;
	f->size = size;
	for (int i = 0; i < size; i++)
		initnilvalue(&f->locvars[i]);

	debug("loc vars : %d", size);
	if (size > 0 && Vector_Size(&code->kf.locvec) > 0) {
		Symbol *item;
		debug("------Set LocVar's Type-------");
		Vector_ForEach(item, &code->kf.locvec) {
			char *typestr = TypeDesc_ToString(item->desc);
			debug("set [%d] as '%s' type", item->index, typestr);
			free(typestr); //FIXME
			assert(item->index >= 0 && item->index < size);
			TValue_Set_TypeDesc(f->locvars + item->index, item->desc, ob);
		}
		debug("------Set LocVar's Type End---");
	}

	if (rt->frame)
		list_add(&rt->frame->link, &rt->frames);
	rt->frame = f;
}

static void frame_free(Frame *f)
{
	assert(list_unlinked(&f->link));
	free(f);
}

static void restore_previous_frame(Frame *f)
{
	Routine *rt = f->rt;
	if (!list_empty(&rt->frames)) {
		Frame *nf = list_first_entry(&rt->frames, Frame, link);
		list_del(&nf->link);
		rt->frame = nf;
	} else {
		rt->frame = NULL;
	}

	frame_free(f);
}

static void start_cframe(Frame *f)
{
	Routine *rt = f->rt;
	CodeObject *code = (CodeObject *)f->code;
	TValue val;
	Object *obj;
	Object *args = NULL;

	/* Prepare parameters */
	int sz = rt_stack_size(rt);
	//FIXME: check arguments
	assert(f->argc <= sz);

	val = rt_stack_pop(rt);
	obj = val.ob;
	if (f->argc > 0) args = Tuple_New(f->argc);

	int count = f->argc;
	int i = 0;
	while (count-- > 0) {
		val = rt_stack_pop(rt);
		VALUE_ASSERT(&val);
		Tuple_Set(args, i++, &val);
	}

	/* Call c function */
	Object *result = code->cf(obj, args);

	/* Save the result */
	sz = Tuple_Size(result);
	//FIXME: check results
	for (i = sz - 1; i >= 0; i--) {
		val = Tuple_Get(result, i);
		PUSH(&val);
	}

	/* Get previous frame and free old frame */
	restore_previous_frame(f);
}

static void start_kframe(Frame *f)
{
	Routine *rt = f->rt;
	//CodeObject *code = (CodeObject *)f->code;
	TValue val;

	/* Prepare parameters */
	int sz = rt_stack_size(rt);
	assert(f->argc <= sz);
	//assert(sz == (KFunc_Argc(f->code) + 1) && (sz <= f->size));
	int count = min(f->argc, Func_Argc(f->code)) + 1;
	int i = 0;
	while (count-- > 0) {
		val = rt_stack_pop(rt);
		VALUE_ASSERT(&val);
		TValue_Set_Value(f->locvars + i, &val);
		i++;
	}

	/* Call frame_loop() to execute instructions */
	frame_loop(f);
}

/*-------------------------------------------------------------------------*/
/*
	Create a new routine
	Example:
		Run a function in current module: go func(123, "abc")
		or run one in external module: go extern_mod_name.func(123, "abc")
	Stack:
		Parameters are stored reversely in the stack, including a module.
 */
Routine *Routine_New(Object *code, Object *ob, Object *args)
{
	Routine *rt = calloc(1, sizeof(Routine));
	init_list_head(&rt->link);
	init_list_head(&rt->frames);
	rt_stack_init(rt);

	/* prepare parameters */
	TValue val;
	int size = Tuple_Size(args);
	for (int i = size - 1; i >= 0; i--) {
		val = Tuple_Get(args, i);
		VALUE_ASSERT(&val);
		PUSH(&val);
	}
	setobjvalue(&val, ob);
	PUSH(&val);

	/* new frame */
	frame_new(rt, ob, code, size);

	return rt;
}

/*
static void routine_task_func(struct task *tsk)
{
	Routine *rt = tsk->arg;
	Frame *f = rt->frame;

	while (f) {
		if (CODE_ISCFUNC(f->code)) {
			start_cframe(f);
		} else if (CODE_ISKFUNC(f->code)) {
			if (f->pc == 0)
				start_kframe(f);
			else
				frame_loop(f);
		} else {
			assert(0);
		}
		f = rt->frame;
	}
}
*/

void Routine_Run(Routine *rt)
{
	//task_init(&rt->task, "routine", prio, routine_task_func, rt);
	Frame *f = rt->frame;

	while (f) {
		if (CODE_ISCFUNC(f->code)) {
			start_cframe(f);
		} else if (CODE_ISKFUNC(f->code)) {
			if (f->pc == 0)
				start_kframe(f);
			else
				frame_loop(f);
		} else {
			assert(0);
		}
		f = rt->frame;
	}
}

/*-------------------------------------------------------------------------*/

#define NEXT_CODE(f, codes) codes[f->pc++]

static inline uint8 fetch_byte(Frame *frame, CodeObject *code)
{
	assert(frame->pc < code->kf.size);
	return NEXT_CODE(frame, code->kf.codes);
}

static inline uint16 fetch_2bytes(Frame *frame, CodeObject *code)
{
	assert(frame->pc < code->kf.size);
	//endian?
	uint8 l = NEXT_CODE(frame, code->kf.codes);
	uint8 h = NEXT_CODE(frame, code->kf.codes);
	return (h << 8) + (l << 0);
}

static inline uint32 fetch_4bytes(Frame *frame, CodeObject *code)
{
	assert(frame->pc < code->kf.size);
	//endian?
	uint8 l1 = NEXT_CODE(frame, code->kf.codes);
	uint8 l2 = NEXT_CODE(frame, code->kf.codes);
	uint8 h1 = NEXT_CODE(frame, code->kf.codes);
	uint8 h2 = NEXT_CODE(frame, code->kf.codes);
	return (h2 << 24) + (h1 << 16) + (l2 << 8) + (l1 << 0);
}

static inline uint8 fetch_code(Frame *frame, CodeObject *code)
{
	return fetch_byte(frame, code);
}

static TValue index_const(int index, AtomTable *atbl)
{
	TValue res = NilValue;
	ConstItem *k = AtomTable_Get(atbl, ITEM_CONST, index);

	switch (k->type) {
		case CONST_INT: {
			setivalue(&res, k->ival);
			break;
		}
		case CONST_FLOAT: {
			setfltvalue(&res, k->fval);
			break;
		}
		case CONST_BOOL: {
			setbvalue(&res, k->bval);
			break;
		}
		case CONST_STRING: {
			StringItem *item;
			item = AtomTable_Get(atbl, ITEM_STRING, k->index);
			setobjvalue(&res, String_New(item->data));
			break;
		}
		default: {
			assertm(0, "unknown const type:%d\n", k->type);
			break;
		}
	}
	return res;
}

static inline TValue load(Frame *f, int index)
{
	assert(index < f->size);
	return f->locvars[index];
}

static inline void store(Frame *f, int index, TValue *val)
{
	VALUE_ASSERT(val);
	assert(index < f->size);
	assert(!TValue_Check(f->locvars + index, val));
	TValue *v = &f->locvars[index];
	if (v->klazz == &Int_Klass) {
		v->ival = val->ival;
	} else if (v->klazz == &Float_Klass) {
		v->fval = val->fval;
	} else if (v->klazz == &Bool_Klass) {
		v->bval = val->bval;
	} else {
		v->ob = val->ob;
	}
}

static Object *getcode(Object *ob, char *name, Object **rob)
{
	Object *code = NULL;
	if (OB_CHECK_KLASS(ob, Module_Klass)) {
		debug("getcode '%s' in module", name);
		code = Module_Get_Function(ob, name);
		*rob = ob;
		if (!code) {
			debug("'%s' is not found", name);
			assert(!strcmp(name, "__init__"));
		}
	} else {
		Check_Klass(OB_KLASS(ob));
		debug("getcode '%s' in class %s", name, OB_KLASS(ob)->name);
		code = Object_Get_Method(ob, name, rob);
		if (!code) {
			debug("'%s' is not found", name);
			assert(!strcmp(name, "__init__"));
		}
	}
	return code;
}

static void setfield(Object *ob, char *field, TValue *val)
{
	if (OB_CHECK_KLASS(ob, Module_Klass)) {
		Module_Set_Value(ob, field, val);
	} else {
		Check_Klass(OB_KLASS(ob));
		int res = Object_Set_Value(ob, field, val);
		assert(!res);
	}
}

static TValue getfield(Object *ob, char *field)
{
	if (OB_CHECK_KLASS(ob, Module_Klass)) {
		return Module_Get_Value(ob, field);
	} else {
		Check_Klass(OB_KLASS(ob));
		TValue val = Object_Get_Value(ob, field);
		VALUE_ASSERT(&val);
		return val;
	}
}

// static int check_virtual_call(TValue *val, char *name)
// {
// 	VALUE_ASSERT_OBJECT(val);
// 	Klass *klazz = val->klazz;
// 	assert(klazz);
// 	//module not check
// 	if (klazz == &Module_Klass) return 0;

// 	Check_Klass(klazz);

// 	if (strchr(name, '.')) return 0;
// 	Symbol *sym = Klass_Get_Symbol(klazz, name);
// 	if (!sym) {
// 		error("cannot find '%s' in '%s' class", name, klazz->name);
// 		return -1;
// 	}
// 	if (sym->kind != SYM_PROTO && sym->kind != SYM_IPROTO) {
// 		error("symbol '%s' is not method", name);
// 		return -1;
// 	}
// 	return 0;
// }

static void check_args(Routine *rt, int argc, TypeDesc *proto, char *name)
{
	assert(proto->kind == TYPE_PROTO);
	int size = Vector_Size(proto->pdesc);
	if (argc != size) {
		error("%s argc: expected %d, but %d", name, size, argc);
		exit(-1);
	}

	TValue *val;
	TypeDesc *desc;
	int pos = rt->top - 1;
	for (int i = 0; i < size; i++) {
		val = rt_stack_get(rt, pos--);
		desc = Vector_Get(proto->pdesc, i);
		if (TValue_Check_TypeDesc(val, desc)) {
			error("'%s' args type check failed", name);
			exit(-1);
		}
	}
}

int tonumber(TValue *v)
{
	UNUSED_PARAMETER(v);
	return 1;
}

// void build_traits_init_frames(Routine *rt, Object *ob)
// {
// 	Object *base = ob;
// 	Object *code;
// 	Object *rob;
// 	while (OB_HasBase(base)) {
// 		base = OB_Base(base);
// 		if (OB_KLASS(base)->ob_klass == &Klass_Klass) break;
// 		code = getcode(base, "__init__", &rob);
// 		if (code) frame_new(rt, rob, code, 0);
// 	}
// }

static void frame_loop(Frame *frame)
{
	int loopflag = 1;
	Routine *rt = frame->rt;
	CodeObject *code = (CodeObject *)frame->code;
	AtomTable *atbl = code->kf.atbl;

	uint8 inst;
	int32 index;
	int32 offset;
	TValue val;
	Object *ob;

	while (loopflag) {
		inst = fetch_code(frame, code);
		switch (inst) {
			case OP_HALT: {
				exit(0);
				break;
			}
			case OP_LOADK: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				PUSH(&val);
				break;
			}
			case OP_LOADM: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				char *path = String_RawString(val.ob);
				debug("load module '%s'", path);
				ob = Koala_Load_Module(path);
				assert(ob);
				setobjvalue(&val, ob);
				PUSH(&val);
				break;
			}
			case OP_GETM: {
				val = TOP();
				ob = val.ob;
				if (!OB_CHECK_KLASS(ob, Module_Klass)) {
					val = POP();
					Klass *klazz = OB_KLASS(ob);
					OB_ASSERT_KLASS(klazz, Klass_Klass);
					assert(klazz->module);
					setobjvalue(&val, klazz->module);
					PUSH(&val);
				}
				break;
			}
			case OP_LOAD: {
				index = fetch_2bytes(frame, code);
				val = load(frame, index);
				PUSH(&val);
				break;
			}
			case OP_LOAD0: {
				val = load(frame, 0);
				PUSH(&val);
				break;
			}
			case OP_STORE: {
				index = fetch_2bytes(frame, code);
				val = POP();
				store(frame, index, &val);
				break;
			}
			case OP_GETFIELD: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				char *field = String_RawString(val.ob);
				debug("getfield '%s'", field);
				val = POP();
				ob = val.ob;
				val = getfield(ob, field);
				PUSH(&val);
				//Klass *k = (Klass *)(((CodeObject *)(frame->code))->owner);
				//Object_Get_Value2(ob, k, field);
				break;
			}
			case OP_SETFIELD: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				char *field = String_RawString(val.ob);
				debug("setfield '%s'", field);
				val = POP();
				ob = val.ob;
				val = POP();
				VALUE_ASSERT(&val);
				setfield(ob, field, &val);
				break;
			}
			case OP_CALL: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				char *name = String_RawString(val.ob);
				int argc = fetch_2bytes(frame, code);
				debug("OP_CALL, %s, argc:%d", name, argc);
				val = TOP();
				ob = val.ob;
				//assert(!check_virtual_call(&val, name));
				Object *rob = NULL;
				Object *meth = getcode(ob, name, &rob);
				assert(rob);
				if (rob != ob) {
					debug(">>>>>update object<<<<<");
					POP();
					setobjvalue(&val, rob);
					PUSH(&val);
				}
				assert(meth);
				if (CODE_ISKFUNC(meth)) {
					//FIXME: for c function
					CodeObject *code = OB_TYPE_OF(meth, CodeObject, Code_Klass);
					check_args(rt, argc, code->proto, name);
				}
				frame_new(rt, rob, meth, argc);
				if (!strcmp(name, "__init__")) {
					//build_traits_init_frames(rt, ob);
				}
				loopflag = 0;
				break;
			}
			case OP_RET: {
				restore_previous_frame(frame);
				loopflag = 0;
				break;
			}
			case OP_ADD: {
				TValue v1 = POP();
				TValue v2 = POP();
				val = NilValue;
				// v1 is left value
				NumberFunctions *ops;
				if (v1.klazz && v1.klazz->numops) {
					ops = v1.klazz->numops;
					if (ops->add) {
						val = ops->add(&v1, &v2);
					} else {
						error("unsupported operation, %s", v1.klazz->name);
						exit(-1);
					}
				}
				PUSH(&val);
				break;
			}
			case OP_SUB: {
				// v1 is left value
				TValue v1 = POP();
				TValue v2 = POP();
				val = NilValue;
				if (VALUE_ISINT(&v1) && VALUE_ISINT(&v2)) {
					//printf("---%lld\n", VALUE_INT(&v1));
					uint64 i = (uint64)VALUE_INT(&v1) - (uint64)VALUE_INT(&v2);
					//printf("%lld\n", (int64)i);
					setivalue(&val, i);
				} else if (tonumber(&v2) && tonumber(&v1)) {

				}
				PUSH(&val);
				break;
			}
			// case OP_MUL: {
			//   TValue v1 = POP();
			//   TValue v2 = POP();
			//   val = NilValue;
			//   // v2 is left value
			//   if (VALUE_ISINT(&v2) && VALUE_ISINT(&v1)) {
			//     uint64 i = (uint64)VALUE_INT(&v2) * (uint64)VALUE_INT(&v1);
			//     setivalue(&val, i);
			//   } else if (tonumber(&v2) && tonumber(&v1)) {

			//   }
			//   PUSH(&val);
			//   break;
			// }
			// case OP_DIV: {
			//   /* float division (always with floats) */
			//   TValue v1 = POP();
			//   TValue v2 = POP();
			//   val = NilValue;
			//   // v2 is left value
			//   if (tonumber(&v2) && tonumber(&v1)) {

			//   }
			//   PUSH(&val);
			//   break;
			// }
			case OP_GT: {
				// v1 is left value
				TValue v1 = POP();
				TValue v2 = POP();
				val = NilValue;
				if (VALUE_ISINT(&v1) && VALUE_ISINT(&v2)) {
					int res = VALUE_INT(&v1) - VALUE_INT(&v2);
					if (res > 0) {
						setbvalue(&val, 1);
					} else {
						setbvalue(&val, 0);
					}
				}
				PUSH(&val);
				break;
			}
			case OP_LT: {
				// v1 is left value
				TValue v1 = POP();
				TValue v2 = POP();
				val = NilValue;
				if (VALUE_ISINT(&v1) && VALUE_ISINT(&v2)) {
					int res = VALUE_INT(&v1) - VALUE_INT(&v2);
					if (res < 0) {
						setbvalue(&val, 1);
					} else {
						setbvalue(&val, 0);
					}
				}
				PUSH(&val);
				break;
			}
			case OP_EQ: {
				// v1 is left value
				TValue v1 = POP();
				TValue v2 = POP();
				val = NilValue;
				if (VALUE_ISINT(&v1) && VALUE_ISINT(&v2)) {
					int res = VALUE_INT(&v1) - VALUE_INT(&v2);
					if (!res) {
						setbvalue(&val, 1);
					} else {
						setbvalue(&val, 0);
					}
				}
				PUSH(&val);
				break;
			}
			case OP_MINUS: {
				TValue v = POP();
				if (VALUE_ISINT(&v)) {
					uint64 i = 0UL - (uint64)VALUE_INT(&v);
					setivalue(&v, i);
				} else {
					assert(0);
				}
				PUSH(&v);
				break;
			}
			case OP_JUMP: {
				offset = fetch_4bytes(frame, code);
				frame->pc += offset;
				break;
			}
			case OP_JUMP_TRUE: {
				val = POP();
				VALUE_ASSERT_BOOL(&val);
				offset = fetch_4bytes(frame, code);
				if (val.bval) {
					frame->pc += offset;
				}
				break;
			}
			case OP_JUMP_FALSE: {
				val = POP();
				VALUE_ASSERT_BOOL(&val);
				offset = fetch_4bytes(frame, code);
				if (!val.bval) {
					frame->pc += offset;
				}
				break;
			}
			case OP_NEW: {
				index = fetch_4bytes(frame, code);
				val = index_const(index, atbl);
				char *name = String_RawString(val.ob);
				val = POP();
				int argc = fetch_2bytes(frame, code);
				debug("OP_NEW, %s, argc:%d", name, argc);
				ob = val.ob;
				Klass *klazz = Module_Get_Class(ob, name);
				assert(klazz);
				assert(klazz != &Klass_Klass);
				assert(OB_Base(klazz) != (Object *)&Klass_Klass);
				ob = klazz->ob_alloc(klazz);
				setobjvalue(&val, ob);
				PUSH(&val);
				#if 0
				Object *rob = NULL;
				Object *__init__ = getcode(ob, "__init__", &rob);
				assert(rob);
				assert(rob == ob);
				if (__init__)	{
					CodeObject *code = OB_TYPE_OF(__init__, CodeObject, Code_Klass);
					debug("__init__'s argc: %d", code->kf.proto->psz);
					check_args(rt, argc, code->kf.proto, name);
					if (code->kf.proto->rsz) {
						error("__init__ must be no any returns");
						exit(-1);
					}
					frame_new(rt, rob, __init__, argc);
					loopflag = 0;
				} else {
					debug("no __init__");
					if (argc) {
						error("no __init__, but %d", argc);
						exit(-1);
					}
				}
				#endif
				break;
			}
			default: {
				assertm(0, "unknown instruction:%d\n", inst);
			}
		}
	}
}
