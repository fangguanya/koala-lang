
#include "object.h"
#include "stringobject.h"
#include "methodobject.h"
#include "moduleobject.h"
#include "debug.h"

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
  ASSERT(VALUE_TYPE(v) == TYPE_INT);
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
  ASSERT(VALUE_TYPE(v) == TYPE_INT);
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
  ASSERT(VALUE_TYPE(v) == TYPE_FLOAT);
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
  ASSERT(VALUE_TYPE(v) == TYPE_BOOL);
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
  ASSERT(VALUE_TYPE(v) == TYPE_OBJECT);
  Object *ob = VALUE_STRING(v);
  char **str = va_arg(*ap, char **);
  *str = String_RawString(ob);
}

static void va_object_build(TValue *v, va_list *ap)
{
  v->type = TYPE_OBJECT;
  v->ob = va_arg(*ap, Object *);
}

static void va_object_parse(TValue *v, va_list *ap)
{
  ASSERT(VALUE_TYPE(v) == TYPE_OBJECT);
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
  ASSERT_MSG("unsupported type: %d\n", ch);
  return NULL;
}

TValue Va_Build_Value(char ch, va_list *ap)
{
  TValue val = NilValue;
  va_convert_t *convert = get_convert(ch);
  convert->build(&val, ap);
  return val;
}

TValue TValue_Build(char ch, ...)
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

int TValue_Parse(TValue *val, char ch, ...)
{
  int res;
  va_list vp;
  va_start(vp, ch);
  res = Va_Parse_Value(val, ch, &vp);
  va_end(vp);
  return res;
}

/*-------------------------------------------------------------------------*/

Klass *Klass_New(char *name, int bsize, int isize, Klass *parent)
{
  Klass *klazz = calloc(1, sizeof(*klazz));
  memset(klazz, 0, sizeof(*klazz));
  init_object_head(klazz, parent);
  klazz->name  = name;
  klazz->bsize = bsize;
  klazz->isize = isize;
  return klazz;
}

int Klass_Add_Field(Klass *klazz, char *name, char *desc)
{
  OB_ASSERT_KLASS(klazz, Klass_Klass);
  Symbol *sym = STable_Add_Field(&klazz->stable, name, desc);
  if (sym != NULL) {
    sym->index = klazz->avail_index++;
    return 0;
  }
  return -1;
}

int Klass_Add_Method(Klass *klazz, char *name, char *rdesc, char *pdesc,
                     Object *method)
{
  OB_ASSERT_KLASS(klazz, Klass_Klass);
  Symbol *sym = STable_Add_Method(&klazz->stable, name, rdesc, pdesc);
  if (sym != NULL) {
    sym->obj = method;
    return 0;
  }
  return -1;
}

// int Klass_Add_IProto(Klass *klazz, char *name, char *rdesc, char *pdesc)
// {
//   OB_ASSERT_KLASS(klazz, Klass_Klass);
//   int name_index = StringItem_Set(klazz->itable, name, strlen(name));
//   int desc_index = ProtoItem_Set(klazz->itable, rdesc, pdesc, NULL, NULL);
//   Symbol *sym = Symbol_New(name_index, SYM_IMETHOD, access, desc_index);
//   sym->index = klazz->avail_index++;
//   return HashTable_Insert(__get_table(klazz), &sym->hnode);
// }

Object *Klass_Get_Method(Klass *klazz, char *name)
{
  Symbol *s = STable_Get(&klazz->stable, name);
  if (s == NULL) return NULL;
  if (s->kind != SYM_METHOD) return NULL;
  Object *temp = s->obj;
  OB_ASSERT_KLASS(temp, Method_Klass);
  return temp;
}

int Klass_Add_CFunctions(Klass *klazz, FuncStruct *funcs)
{
  int res;
  FuncStruct *f = funcs;
  Object *meth;
  MethodProto proto;
  while (f->name != NULL) {
    FuncStruct_Get_Proto(&proto, f);
    meth = CMethod_New(f->func, &proto);
    res = Klass_Add_Method(klazz, f->name, f->rdesc, f->pdesc, meth);
    ASSERT(res == 0);
    ++f;
  }
  return 0;
}

void Init_Klass_Klass(Object *ob)
{
  Module_Add_Class(ob, &Klass_Klass);
}

/*-------------------------------------------------------------------------*/

static void klass_mark(Object *ob)
{
  OB_ASSERT_KLASS(ob, Klass_Klass);
  //ob_incref(ob);
  //FIXME
}

static void klass_free(Object *ob)
{
  // Klass_Klass cannot be freed.
  if (ob == (Object *)&Klass_Klass) {
    ASSERT_MSG("Klass_Klass cannot be freed\n");
  }

  OB_ASSERT_KLASS(ob, Klass_Klass);
  free(ob);
}

Klass Klass_Klass = {
  OBJECT_HEAD_INIT(&Klass_Klass),
  .name  = "Class",
  .bsize = sizeof(Klass),

  .ob_mark = klass_mark,

  .ob_free = klass_free,
};
