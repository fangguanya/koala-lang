
#include "codeobject.h"
#include "stringobject.h"
#include "moduleobject.h"
#include "tupleobject.h"
#include "koala_state.h"
#include "log.h"

TValue NilValue  = NIL_VALUE_INIT();
TValue TrueValue = BOOL_VALUE_INIT(1);
TValue FalseValue = BOOL_VALUE_INIT(0);

static void va_integer_build(TValue *v, va_list *ap)
{
	v->type = TYPE_INT;
	v->ival = (int64)va_arg(*ap, uint32);
}

static void va_integer_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_INT);
	int32 *i = va_arg(*ap, int32 *);
	*i = (int32)VALUE_INT(v);
}

static void va_long_build(TValue *v, va_list *ap)
{
	v->type = TYPE_INT;
	v->ival = va_arg(*ap, uint64);
}

static void va_long_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_INT);
	int64 *i = va_arg(*ap, int64 *);
	*i = VALUE_INT(v);
}

static void va_float_build(TValue *v, va_list *ap)
{
	v->type = TYPE_FLOAT;
	v->fval = va_arg(*ap, float64);
}

static void va_float_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_FLOAT);
	float64 *f = va_arg(*ap, float64 *);
	*f = VALUE_FLOAT(v);
}

static void va_bool_build(TValue *v, va_list *ap)
{
	v->type = TYPE_BOOL;
	v->bval = va_arg(*ap, int);
}

static void va_bool_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_BOOL);
	int *i = va_arg(*ap, int *);
	*i = VALUE_BOOL(v);
}

static void va_string_build(TValue *v, va_list *ap)
{
	v->type = TYPE_OBJECT;
	v->ob = String_New(va_arg(*ap, char *));
}

static void va_string_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_OBJECT);
	Object *ob = VALUE_STRING(v);
	char **str = va_arg(*ap, char **);
	*str = String_RawString(ob);
}

static void va_object_build(TValue *v, va_list *ap)
{
	v->type = TYPE_OBJECT;
	v->ob = va_arg(*ap, Object *);
	v->klazz = OB_KLASS(v->ob);
}

static void va_object_parse(TValue *v, va_list *ap)
{
	assert(VALUE_TYPE(v) == TYPE_OBJECT);
	Object **o = va_arg(*ap, Object **);
	*o = VALUE_OBJECT(v);
}

typedef void (*va_build_t)(TValue *v, va_list *ap);
typedef void (*va_parse_t)(TValue *v, va_list *ap);

typedef struct {
	char ch;
	va_build_t build;
	va_parse_t parse;
} va_convert_t;

static va_convert_t va_convert_func[] = {
	{'i', va_integer_build, va_integer_parse},
	{'l', va_long_build,    va_long_parse},
	{'f', va_float_build,   va_float_parse},
	{'d', va_float_build,   va_float_parse},
	{'z', va_bool_build,    va_bool_parse},
	{'s', va_string_build,  va_string_parse},
	{'O', va_object_build,  va_object_parse},
};

va_convert_t *get_convert(char ch)
{
	va_convert_t *convert;
	for (int i = 0; i < nr_elts(va_convert_func); i++) {
		convert = va_convert_func + i;
		if (convert->ch == ch) {
			return convert;
		}
	}
	assertm(0, "unsupported type: %d\n", ch);
	return NULL;
}

TValue Va_Build_Value(char ch, va_list *ap)
{
	TValue val = NilValue;
	va_convert_t *convert = get_convert(ch);
	convert->build(&val, ap);
	return val;
}

TValue TValue_Build(int ch, ...)
{
	va_list vp;
	va_start(vp, ch);
	TValue val = Va_Build_Value(ch, &vp);
	va_end(vp);
	return val;
}

int Va_Parse_Value(TValue *val, char ch, va_list *ap)
{
	va_convert_t *convert = get_convert(ch);
	convert->parse(val, ap);
	return 0;
}

int TValue_Parse(TValue *val, int ch, ...)
{
	int res;
	va_list vp;
	va_start(vp, ch);
	res = Va_Parse_Value(val, ch, &vp);
	va_end(vp);
	return res;
}

/*---------------------------------------------------------------------------*/

static void object_mark(Object *ob)
{
	Check_Klass(OB_KLASS(ob));
	assert(OB_Head(ob) && OB_Head(ob) == ob);
	//FIXME: ob_ref_inc
}

static Object *object_alloc(Klass *klazz)
{
	Check_Klass(klazz);

	int totalsize = 0;
	Klass *base = klazz;
	while (OB_HasBase(base)) {
		debug("number of '%s' fields:%d", base->name, base->itemsize);
		totalsize += base->basesize + sizeof(TValue) * base->itemsize;
		base = (Klass *)OB_Base(base);
	}
	debug("number of '%s' fields:%d", base->name, base->itemsize);
	totalsize += base->basesize + sizeof(TValue) * base->itemsize;
	Object *ob = calloc(1, totalsize);

	//init base class
	Object *next = ob;
	base = klazz;
	while (OB_HasBase(base)) {
		Init_Object_Head(next, base);
		OB_Head(next) = ob;
		base->ob_init(next);
		next = OB_Base(next);
		base = (Klass *)OB_Base(base);
	}
	Init_Object_Head(next, base);
	OB_Head(next) = ob;
	base->ob_init(next);

	return ob;
}

static void object_init(Object *ob)
{
	Klass *klazz = OB_KLASS(ob);
	TValue *values = (TValue *)(ob + 1);
	for (int i = 0; i < klazz->itemsize; i++) {
		initnilvalue(values + i);
	}
}

static void object_free(Object *ob)
{
	Check_Klass(OB_KLASS(ob));
	assert(OB_Head(ob) && OB_Head(ob) == ob);
	free(ob);
}

static uint32 object_hash(TValue *v)
{
	Object *ob = VALUE_OBJECT(v);
	Check_Klass(OB_KLASS(ob));
	assert(OB_Head(ob) && OB_Head(ob) == ob);
	return hash_uint32((uint32)OB_Head(ob), 32);
}

static int object_equal(TValue *v1, TValue *v2)
{
	Object *ob1 = VALUE_OBJECT(v1);
	Check_Klass(OB_KLASS(ob1));
	Object *ob2 = VALUE_OBJECT(v2);
	Check_Klass(OB_KLASS(ob2));
	return OB_Head(ob1) == OB_Head(ob2);
}

static Object *object_tostring(TValue *v)
{
	Object *ob = VALUE_OBJECT(v);
	assert(OB_Head(ob) && OB_Head(ob) == ob);
	Klass *klazz = OB_KLASS(OB_Head(ob));
	char buf[128];
	snprintf(buf, 128, "%s@%x", klazz->name, (int32)OB_Head(ob));
	return Tuple_Build("O", String_New(buf));
}

/*---------------------------------------------------------------------------*/

Klass *Klass_New(char *name, Klass *base, int flags)
{
	Klass *klazz = calloc(1, sizeof(Klass));
	Init_Object_Head(klazz, &Klass_Klass);
	if (base) OB_Base(klazz) = (Object *)base;
	klazz->name = strdup(name);
	klazz->basesize = sizeof(Object);
	klazz->itemsize = 1;
	klazz->flags = flags;
	klazz->ob_mark  = object_mark;
	klazz->ob_alloc = object_alloc;
	klazz->ob_init  = object_init;
	klazz->ob_free  = object_free,
	klazz->ob_hash  = object_hash;
	klazz->ob_equal = object_equal;
	klazz->ob_tostr = object_tostring;
	return klazz;
}

void Fini_Klass(Klass *klazz)
{
	STable_Fini(&klazz->stbl);
}

void Check_Klass(Klass *klazz)
{
	while (klazz) {
		OB_ASSERT_KLASS(klazz, Klass_Klass);
		klazz = OB_HasBase(klazz) ? (Klass *)OB_Base(klazz) : NULL;
	}
}

int Klass_Add_Field(Klass *klazz, char *name, TypeDesc *desc)
{
	Check_Klass(klazz);
	Symbol *sym = STable_Add_Var(&klazz->stbl, name, desc, 0);
	klazz->itemsize++;
	return sym ? 0 : -1;
}

int Klass_Add_Method(Klass *klazz, char *name, Proto *proto, Object *code)
{
	Check_Klass(klazz);
	Symbol *sym = STable_Add_Proto(&klazz->stbl, name, proto);
	if (sym) {
		CodeObject *co = (CodeObject *)code;
		sym->ob = code;
		if (CODE_ISKFUNC(code)) {
			co->kf.atbl = klazz->stbl.atbl;
			co->kf.proto = Proto_Dup(proto);
		}
		return 0;
	}
	return -1;
}

Symbol *Klass_Get_FieldSymbol(Klass *klazz, char *name)
{
	return STable_Get(&klazz->stbl, name);
}

Object *Klass_Get_Method(Klass *klazz, char *name)
{
	Symbol *sym = STable_Get(&klazz->stbl, name);
	if (!sym) return NULL;
	if (sym->kind != SYM_PROTO) return NULL;
	OB_ASSERT_KLASS(sym->ob, Code_Klass);
	return sym->ob;
}

int Klass_Add_CFunctions(Klass *klazz, FuncDef *funcs)
{
	int res;
	FuncDef *f = funcs;
	Object *meth;
	Proto *proto;

	while (f->name) {
		proto = Proto_New(f->rsz, f->rdesc, f->psz, f->pdesc);
		meth = CFunc_New(f->fn);
		res = Klass_Add_Method(klazz, f->name, proto, meth);
		assert(res == 0);
		++f;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static void klass_mark(Object *ob)
{
	OB_ASSERT_KLASS(ob, Klass_Klass);
	//FIXME: ob_ref_inc
}

static void klass_free(Object *ob)
{
	OB_ASSERT_KLASS(ob, Klass_Klass);
	assertm(ob != (Object *)&Klass_Klass, "Why goes here?");
	free(ob);
}

Klass Klass_Klass = {
	OBJECT_HEAD_INIT(&Klass_Klass, &Klass_Klass)
	.name = "Klass",
	.basesize = sizeof(Klass),
	.ob_mark = klass_mark,
	.ob_free = klass_free,
};

Klass Any_Klass = {
	OBJECT_HEAD_INIT(&Any_Klass, &Klass_Klass)
	.name = "Any",
};

/*---------------------------------------------------------------------------*/

static Symbol *get_field_symbol(Object *ob, char *name, Object **rob)
{
	Check_Klass(OB_KLASS(ob));
	Object *base;
	Symbol *sym = NULL;
	char *dot = strchr(name, '.');
	if (dot) {
		char *classname = strndup(name, dot - name);
		char *fieldname = strndup(dot + 1, strlen(name) - (dot - name) - 1);
		base = OB_Base(ob);
		assert(base && !strcmp(OB_KLASS(base)->name, classname));
		sym = Klass_Get_FieldSymbol(OB_KLASS(base), fieldname);
		*rob = base;
		free(classname);
		free(fieldname);
	} else {
		base = ob;
		while (base) {
			sym = Klass_Get_FieldSymbol(OB_KLASS(base), name);
			if (sym) {
				*rob = base;
				break;
			}
			base = OB_HasBase(ob) ? OB_Base(ob) : NULL;
		}
	}

	assert(sym && sym->kind == SYM_VAR);
	return sym;
}

TValue Object_Get_Value(Object *ob, char *name)
{
	Object *rob = NULL;
	Symbol *sym = get_field_symbol(ob, name, &rob);
	assert(rob);
	TValue *value = (TValue *)(rob + 1);
	int index = sym->index;
	assert(index >= 0 && index < rob->ob_size);
	return value[index];
}

int Object_Set_Value(Object *ob, char *name, TValue *val)
{
	Object *rob = NULL;
	Symbol *sym = get_field_symbol(ob, name, &rob);
	assert(rob);
	TValue *value = (TValue *)(rob + 1);
	int index = sym->index;
	assert(index >= 0 && index < rob->ob_size);
	value[index] = *val;
	return 0;
}

Object *Object_Get_Method(Object *ob, char *name, Object **rob)
{
	Check_Klass(OB_KLASS(ob));
	Object *base;
	Object *code;
	char *dot = strchr(name, '.');
	if (dot) {
		char *classname = strndup(name, dot - name);
		char *funcname = strndup(dot + 1, strlen(name) - (dot - name) - 1);
		base = OB_Base(ob);
		assert(base);
		assert(!strcmp(OB_KLASS(base)->name, classname));
		code = Klass_Get_Method(OB_KLASS(base), funcname);
		assert(code);
		*rob = base;
		free(classname);
		free(funcname);
		return code;
	} else {
		if (!strcmp(name, "__init__")) {
			//__init__ method is allowed to be searched only in current class
			code = Klass_Get_Method(OB_KLASS(ob), name);
			//FIXME: class with no __init__ ?
			//assert(code);
			*rob = ob;
			return code;
		} else {
			base = ob;
			while (base) {
				code = Klass_Get_Method(OB_KLASS(base), name);
				if (code) {
					*rob = base;
					return code;
				}
				base = OB_HasBase(ob) ? OB_Base(ob) : NULL;
			}
			assertm(0, "cannot find func '%s'", name);
			return NULL;
		}
	}
}

/*---------------------------------------------------------------------------*/

static int NilValue_Print(char *buf, int sz, TValue *val)
{
	UNUSED_PARAMETER(val);
	return snprintf(buf, sz, "(nil)");
}

static int IntValue_Print(char *buf, int sz, TValue *val)
{
	return snprintf(buf, sz, "%lld", VALUE_INT(val));
}

static int FltValue_Print(char *buf, int sz, TValue *val)
{
	return snprintf(buf, sz, "%f", VALUE_FLOAT(val));
}

static int BoolValue_Print(char *buf, int sz, TValue *val)
{
	return snprintf(buf, sz, "%s", VALUE_BOOL(val) ? "true" : "false");
}

static int ObjValue_Print(char *buf, int sz, TValue *val)
{
	Object *ob = VALUE_OBJECT(val);
	Klass *klazz = OB_KLASS(ob);
	ob = klazz->ob_tostr(val);
	OB_ASSERT_KLASS(ob, Tuple_Klass);
	TValue v = Tuple_Get(ob, 0);
	ob = VALUE_OBJECT(&v);
	return snprintf(buf, sz, "%s", String_RawString(ob));
}

static char escchar(char ch)
{
  static struct escmap {
    char esc;
    char ch;
  } escmaps[] = {
    {'a', 7},
    {'b', 8},
    {'f', 12},
    {'n', 10},
    {'r', 13},
    {'t', 9},
    {'v', 11}
  };

  for (int i = 0; i < nr_elts(escmaps); i++) {
    if (escmaps[i].esc == ch) return escmaps[i].ch;
  }

  return 0;
}

static char *string_escape(char *escstr, int len)
{
  char *str = calloc(1, len + 1);
  char ch, escch;
  int i = 0;
  while ((ch = *escstr) && (len > 0)) {
    if (ch != '\\') {
      ++escstr; len--;
      str[i++] = ch;
    } else {
      ++escstr; len--;
      ch = *escstr;
      escch = escchar(ch);
      if (escch > 0) {
        ++escstr; len--;
        str[i] = escch;
      } else {
        str[i] = '\\';
      }
    }
  }
  return str;
}

static int CStrValue_Print(char *buf, int sz, TValue *val, int escape)
{
	char *str = VALUE_CSTR(val);
	if (!escape) return snprintf(buf, sz, "%s", str);
	// handle esc characters
	char *escstr = string_escape(str, strlen(str));
	int cnt = snprintf(buf, sz, "%s", escstr);
	free(escstr);
	return cnt;
}

int TValue_Print(char *buf, int sz, TValue *val, int escape)
{
	if (!val) {
		buf[0] = '\0';
		return 0;
	}

	int count = 0;
	switch (val->type) {
		case TYPE_NIL: {
			count = NilValue_Print(buf, sz, val);
			break;
		}
		case TYPE_INT: {
			count = IntValue_Print(buf, sz, val);
			break;
		}
		case TYPE_FLOAT: {
			count = FltValue_Print(buf, sz, val);
			break;
		}
		case TYPE_BOOL: {
			count = BoolValue_Print(buf, sz, val);
			break;
		}
		case TYPE_OBJECT: {
			count = ObjValue_Print(buf, sz, val);
			break;
		}
		case TYPE_CSTR: {
			count = CStrValue_Print(buf, sz, val, escape);
			break;
		}
		default: {
			assert(0);
		}
	}
	return count;
}

static int check_inheritance(Klass *k1, Klass *k2)
{
	Klass *base = k1;
	while (base) {
		if (base == k2) return 0;
		base = OB_HasBase(base) ? (Klass *)OB_Base(base) : NULL;
	}

	base = k2;
	while (base) {
		if (base == k1) return 0;
		base = OB_HasBase(base) ? (Klass *)OB_Base(base) : NULL;
	}

	return -1;
}

int TValue_Check(TValue *v1, TValue *v2)
{
	if (v1->type != v2->type) {
		error("v1's type %d vs v2's type %d", v1->type, v2->type);
		return -1;
	}

	if (v1->type == TYPE_OBJECT) {
		if (v1->klazz == NULL || v2->klazz == NULL) {
			error("object klazz is not set");
			return -1;
		}
		if (v1->klazz != v2->klazz) {
			if (!check_inheritance(v1->klazz, v2->klazz)) return 0;
			error("v1's klazz '%s' vs v2's klazz '%s'",
				v1->klazz->name, v2->klazz->name);
			return -1;
		}
	}
	return 0;
}

int TValue_Check_TypeDesc(TValue *val, TypeDesc *desc)
{
	switch (desc->kind) {
		case TYPE_PRIMITIVE: {
			if (desc->primitive == PRIMITIVE_INT && VALUE_ISINT(val))
				return 0;
			if (desc->primitive == PRIMITIVE_FLOAT && VALUE_ISFLOAT(val))
				return 0;
			if (desc->primitive == PRIMITIVE_BOOL && VALUE_ISBOOL(val))
				return 0;
			if (desc->primitive == PRIMITIVE_STRING && VALUE_ISCSTR(val))
				return 0;
			break;
		}
		case TYPE_USERDEF: {
			if (!VALUE_ISOBJECT(val)) return -1;
			Object *ob = OB_KLASS(val->ob)->module;
			Klass *klazz = Koala_Get_Klass(ob, desc->path, desc->type);
			if (!klazz) return -1;
			Klass *k = OB_KLASS(val->ob);
			while (k) {
				if (k == klazz) return 0;
				k = OB_HasBase(k) ? (Klass *)OB_Base(k) : NULL;
			}
			break;
		}
		case TYPE_PROTO: {
			break;
		}
		default: {
			assertm(0, "unknown type's kind %d\n", desc->kind);
			break;
		}
	}
	return -1;
}

static void TValue_Set_Primitive(TValue *val, int primitive)
{
	switch (primitive) {
		case PRIMITIVE_INT:
			setivalue(val, 0);
			break;
		case PRIMITIVE_FLOAT:
			setfltvalue(val, 0.0);
			break;
		case PRIMITIVE_BOOL:
			setbvalue(val, 0);
			break;
		case PRIMITIVE_STRING:
			setcstrvalue(val, NULL);
			break;
		case PRIMITIVE_ANY:
		default:
			assertm(0, "unknown primitive %c", primitive);
			break;
	}
}

void TValue_Set_TypeDesc(TValue *val, TypeDesc *desc, Object *ob)
{
	assert(ob);
	assert(desc);

	switch (desc->kind) {
		case TYPE_PRIMITIVE: {
			TValue_Set_Primitive(val, desc->primitive);
			break;
		}
		case TYPE_USERDEF: {
			Object *module = NULL;
			if (desc->path) {
				module = Koala_Load_Module(desc->path);
			} else {
				if (OB_CHECK_KLASS(ob, Module_Klass)) {
					module = ob;
				} else if (OB_CHECK_KLASS(OB_KLASS(ob), Klass_Klass)) {
					module = OB_KLASS(ob)->module;
				} else {
					assert(0);
				}
			}

			Klass *klazz = Module_Get_Class(module, desc->type);
			assert(klazz);
			debug("find class: %s", klazz->name);
			setobjtype(val, klazz);
			break;
		}
		case TYPE_PROTO: {
			break;
		}
		default: {
			assert(0);
			break;
		}
	}
}

void TValue_Set_Value(TValue *val, TValue *v)
{
	switch (v->type) {
		case TYPE_INT: {
			VALUE_ASSERT_INT(val);
			val->ival = v->ival;
			break;
		}
		case TYPE_FLOAT: {
			VALUE_ASSERT_FLOAT(val);
			val->fval = v->fval;
			break;
		}
		case TYPE_BOOL: {
			VALUE_ASSERT_BOOL(val);
			val->bval = v->bval;
			break;
		}
		case TYPE_OBJECT: {
			if (VALUE_ISNIL(val)) {
				debug("TValue is nil");
				*val = *v;
			} else {
				VALUE_ASSERT_OBJECT(val);
				val->ob = v->ob;
			}
			break;
		}
		case TYPE_CSTR: {
			VALUE_ASSERT_CSTR(val);
			val->cstr = v->cstr;
			break;
		}
		default: {
			assert(0);
		}
	}
}
