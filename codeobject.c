
#include "codeobject.h"
#include "tupleobject.h"
#include "moduleobject.h"
#include "symbol.h"
#include "log.h"

static CodeObject *code_new(int flags, TypeDesc *proto)
{
	CodeObject *code = calloc(1, sizeof(CodeObject));
	Init_Object_Head(code, &Code_Klass);
	code->flags = flags;
	code->proto = proto;
	return code;
}

Object *CFunc_New(cfunc cf, TypeDesc *proto)
{
	CodeObject *code = code_new(CODE_CLANG, proto);
	code->cf = cf;
	return (Object *)code;
}

void CodeObject_Free(Object *ob)
{
	CodeObject *code = OB_TYPE_OF(ob, CodeObject, Code_Klass);
	if (CODE_ISKFUNC(code)) {
		//FIXME
		//Proto_Free(code->kf.proto);
	}
	free(ob);
}

Object *KFunc_New(int locvars, uint8 *codes, int size, TypeDesc *proto)
{
	CodeObject *code = code_new(CODE_KLANG, proto);
	code->kf.locvars = locvars;
	code->kf.codes = codes;
	code->kf.size = size;
	return (Object *)code;
}

int KFunc_Add_LocVar(Object *ob, char *name, TypeDesc *desc, int pos)
{
	CodeObject *code = OB_TYPE_OF(ob, CodeObject, Code_Klass);
	if (CODE_ISKFUNC(code)) {
		Symbol *sym = Symbol_New(SYM_VAR);
		int32 idx = StringItem_Set(code->kf.atbl, name);
		assert(idx >= 0);
		sym->nameidx = idx;
		sym->name = strdup(name);

		idx = -1;
		if (desc) {
			idx = TypeItem_Set(code->kf.atbl, desc);
			assert(idx >= 0);
		}
		sym->descidx = idx;
		sym->desc = desc;
		sym->index = pos;

		Vector_Append(&code->kf.locvec, sym);

		return 0;
	} else {
		error("add locvar to cfunc?");
		return -1;
	}
}

/*-------------------------------------------------------------------------*/

Klass Code_Klass = {
	OBJECT_HEAD_INIT(&Code_Klass, &Klass_Klass)
	.name = "Code",
	.basesize = sizeof(CodeObject),
};
