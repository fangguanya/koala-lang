
#include "methodobject.h"
#include "tupleobject.h"

static MethodObject *method_new(int type)
{
  MethodObject *m = malloc(sizeof(*m));
  init_object_head(m, &Method_Klass);
  m->type = type;
  return m;
}

Object *CMethod_New(cfunc cf)
{
  MethodObject *m = method_new(METH_CFUNC);
  m->cf = cf;
  return (Object *)m;
}

Object *KMethod_New(uint8 *codes, Object *k, Object *up)
{
  MethodObject *m = method_new(METH_KFUNC);
  m->kf.codes = codes;
  m->kf.k = k;
  m->kf.up = up;
  return (Object *)m;
}

/*-------------------------------------------------------------------------*/
static Object *method_invoke(Object *ob, Object *args)
{
  assert(OB_KLASS(ob) == &Method_Klass);
  MethodObject *m = (MethodObject *)ob;
  if (m->type == METH_CFUNC) {
    TValue *tv = Tuple_Get(args, 0);
    assert(tval_isobject(tv));
    Object *newargs = Tuple_Get_Slice(args, 1, Tuple_Size(args) - 1);
    return m->cf(TVAL_OVAL(tv), newargs);
  } else {
    // new frame and execute it
    return NULL;
  }
}

static MethodStruct method_methods[] = {
  {
    "Invoke",
    "(Okoala/lang.Object;[Okoala/lang.Any;)(Okoala/lang.Any;)",
    ACCESS_PUBLIC,
    method_invoke
  },
  {NULL, NULL, 0, NULL}
};

void Init_Method_Klass(void)
{
  Klass_Add_Methods(&Method_Klass, method_methods);
}

/*-------------------------------------------------------------------------*/

Klass Method_Klass = {
  OBJECT_HEAD_INIT(&Klass_Klass),
  .name  = "Method",
  .bsize = sizeof(MethodObject),
};
