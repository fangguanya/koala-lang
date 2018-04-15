
#include "codegen.h"
#include "opcode.h"
#include "log.h"

Inst *Inst_New(uint8 op, TValue *val)
{
	Inst *i = malloc(sizeof(Inst));
	init_list_head(&i->link);
	i->op = op;
	i->bytes = 1 + opcode_argsize(op);
	if (val)
		i->arg = *val;
	else
		initnilvalue(&i->arg);
	return i;
}

void Inst_Free(Inst *i)
{
	free(i);
}

Inst *Inst_Append(CodeBlock *b, uint8 op, TValue *val)
{
	char buf[64];
	TValue_Print(buf, 32, val, 0);
	debug("inst:'%s %s'", opcode_string(op), buf);
	Inst *i = Inst_New(op, val);
	list_add_tail(&i->link, &b->insts);
	b->bytes += i->bytes;
	i->upbytes = b->bytes;
	return i;
}

void Inst_Gen(AtomTable *atbl, Buffer *buf, Inst *i)
{
	int index = -1;
	Buffer_Write_Byte(buf, i->op);
	switch (i->op) {
		case OP_HALT: {
			break;
		}
		case OP_LOADK: {
			TValue *val = &i->arg;
			if (VALUE_ISINT(val)) {
				index = ConstItem_Set_Int(atbl, VALUE_INT(val));
			} else if (VALUE_ISFLOAT(val)) {
				index = ConstItem_Set_Float(atbl, VALUE_FLOAT(val));
			} else if (VALUE_ISBOOL(val)) {
				index = ConstItem_Set_Bool(atbl, VALUE_BOOL(val));
			} else if (VALUE_ISCSTR(val)) {
				index = ConstItem_Set_String(atbl, VALUE_CSTR(val));
			} else {
				assert(0);
			}
			Buffer_Write_4Bytes(buf, index);
			break;
		}
		case OP_LOADM: {
			index = ConstItem_Set_String(atbl, i->arg.cstr);
			Buffer_Write_4Bytes(buf, index);
			break;
		}
		case OP_GETM: {
			break;
		}
		case OP_LOAD: {
			Buffer_Write_2Bytes(buf, i->arg.ival);
			break;
		}
		case OP_STORE: {
			Buffer_Write_2Bytes(buf, i->arg.ival);
			break;
		}
		case OP_GETFIELD: {
			index = ConstItem_Set_String(atbl, i->arg.cstr);
			Buffer_Write_4Bytes(buf, index);
			break;
		}
		case OP_SETFIELD: {
			index = ConstItem_Set_String(atbl, i->arg.cstr);
			Buffer_Write_4Bytes(buf, index);
			break;
		}
		case OP_CALL:
		case OP_NEW: {
			index = ConstItem_Set_String(atbl, i->arg.cstr);
			Buffer_Write_4Bytes(buf, index);
			Buffer_Write_2Bytes(buf, i->argc);
			break;
		}
		case OP_RET: {
			break;
		}
		case OP_ADD:
		case OP_SUB: {
			break;
		}
		case OP_GT:
		case OP_GE:
		case OP_LT:
		case OP_LE:
		case OP_EQ:
		case OP_NEQ: {
			break;
		}
		case OP_JUMP:
		case OP_JUMP_TRUE:
		case OP_JUMP_FALSE: {
			Buffer_Write_4Bytes(buf, i->arg.ival);
			break;
		}
		default: {
			assert(0);
			break;
		}
	}
}

static void add_locvar(KImage *image, int index, Symbol *sym, int flags,
	int incls)
{
	if (Vector_Size(&sym->locvec) <= 0) {
		if (incls)
			debug("		no vars");
		else
			debug("	no vars");
		return;
	}

	Symbol *item;
	Vector_ForEach(item, &sym->locvec) {
		if (incls)
			debug("		var '%s'", item->name);
		else
			debug("	var '%s'", item->name);
		KImage_Add_LocVar(image, item->name, item->desc, item->index,
			flags, index);
	}
}

struct gencode_struct {
	int bcls;
	KImage *image;
	char *clazz;
};

static void __gen_code_fn(Symbol *sym, void *arg)
{
	struct gencode_struct *tmp = arg;
	switch (sym->kind) {
		case SYM_VAR: {
			if (sym->inherited) {
				assert(tmp->bcls);
				break;
			}

			if (tmp->bcls) {
				debug("	var '%s'", sym->name);
				KImage_Add_Field(tmp->image, tmp->clazz, sym->name, sym->desc);
			} else {
				debug("	%s %s:", sym->access & ACCESS_CONST ? "const" : "var",
					sym->name);
				if (sym->access & ACCESS_CONST)
					KImage_Add_Const(tmp->image, sym->name, sym->desc);
				else
					KImage_Add_Var(tmp->image, sym->name, sym->desc);
			}
			break;
		}
		case SYM_PROTO: {
			if (sym->inherited) {
				assert(tmp->bcls);
				break;
			}
			if (tmp->bcls) {
				debug("	func %s:", sym->name);
			} else {
				debug("func %s:", sym->name);
			}
			CodeBlock *b = sym->ptr;
			int locvars = sym->locvars;
			AtomTable *atbl = tmp->image->table;

			Buffer buf;
			Buffer_Init(&buf, 32);
			Inst *i;
			list_for_each_entry(i, &b->insts, link) {
				Inst_Gen(atbl, &buf, i);
			}

			uint8 *data = Buffer_RawData(&buf);
			int size = Buffer_Size(&buf);
			//code_show(data, size);
			Buffer_Fini(&buf);

			int index;
			if (tmp->bcls) {
				index = KImage_Add_Method(tmp->image, tmp->clazz, sym->name,
					sym->desc->proto, locvars, data, size);
				add_locvar(tmp->image, index, sym, METHLOCVAR, 1);
			} else {
				index = KImage_Add_Func(tmp->image, sym->name, sym->desc->proto, locvars,
					data, size);
				add_locvar(tmp->image, index, sym, FUNCLOCVAR, 0);
			}
			break;
		}
		case SYM_CLASS: {
			debug("----------------------");
			debug("class %s:", sym->name);
			char *path = NULL;
			char *type = NULL;
			if (sym->super) {
				path = sym->super->desc->path;
				type = sym->super->desc->type;
			}
			int size = Vector_Size(&sym->traits);
			TypeDesc descs[size];
			Symbol *trait;
			for (int i = 0; i < size; i++) {
				trait = Vector_Get(&sym->traits, i);
				*(descs + i) = *trait->desc;
			}
			KImage_Add_Class(tmp->image, sym->name, path, type, descs, size);
			struct gencode_struct tmp2 = {1, tmp->image, sym->name};
			STable_Traverse(sym->ptr, __gen_code_fn, &tmp2);
			break;
		}
		case SYM_IPROTO: {
			debug("	abstract func %s;", sym->name);
			KImage_Add_IMeth(tmp->image, tmp->clazz, sym->name, sym->desc->proto);
			break;
		}
		case SYM_TRAIT: {
			debug("----------------------");
			debug("trait %s:", sym->name);
			int size = Vector_Size(&sym->traits);
			TypeDesc descs[size];
			Symbol *trait;
			for (int i = 0; i < size; i++) {
				trait = Vector_Get(&sym->traits, i);
				*(descs + i) = *trait->desc;
			}
			KImage_Add_Trait(tmp->image, sym->name, descs, size);
			struct gencode_struct tmp2 = {1, tmp->image, sym->name};
			STable_Traverse(sym->ptr, __gen_code_fn, &tmp2);
			break;
		}
		default: {
			assertm(0, "unknown symbol kind:%d", sym->kind);
		}
	}
}

void codegen_klc(ParserState *ps, char *out)
{
	printf("----------codegen------------\n");
	KImage *image = KImage_New(ps->package);
	struct gencode_struct tmp = {0, image, NULL};
	STable_Traverse(ps->sym->ptr, __gen_code_fn, &tmp);
	debug("----------------------");
	KImage_Finish(image);
#if SHOW_ENABLED
	KImage_Show(image);
#endif
	KImage_Write_File(image, out);
	printf("----------codegen end--------\n");
}

void codegen_binary(ParserState *ps, int op)
{
	switch (op) {
		case BINARY_ADD: {
			debug("add 'OP_ADD'");
			Inst_Append(ps->u->block, OP_ADD, NULL);
			break;
		}
		case BINARY_SUB: {
			debug("add 'OP_SUB'");
			Inst_Append(ps->u->block, OP_SUB, NULL);
			break;
		}
		case BINARY_GT: {
			debug("add 'OP_GT'");
			Inst_Append(ps->u->block, OP_GT, NULL);
			break;
		}
		case BINARY_LT: {
			debug("add 'OP_LT'");
			Inst_Append(ps->u->block, OP_LT, NULL);
			break;
		}
		case BINARY_EQ: {
			debug("add 'OP_EQ'");
			Inst_Append(ps->u->block, OP_EQ, NULL);
			break;
		}
		default: {
			assert(0);
			break;
		}
	}
}
