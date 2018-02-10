
#include "methodobject.h"
#include "tupleobject.h"
#include "moduleobject.h"
#include "symbol.h"

static MethodObject *__method_new(void)
{
  MethodObject *m = malloc(sizeof(MethodObject));
  init_object_head(m, &Method_Klass);
  return m;
}

Object *CFunc_New(cfunc cf, ProtoInfo *proto)
{
  MethodObject *m = __method_new();
  m->flags = METH_CFUNC;
  if (proto->vargs) m->flags |= METH_VARGS;
  m->cf = cf;
  m->rets = proto->rsz;
  m->args = proto->psz;
  m->locals = 0;
  return (Object *)m;
}

Object *Method_New(FuncInfo *info, AtomTable *atbl)
{
  MethodObject *m = __method_new();
  m->flags = METH_KFUNC;
  if (info->proto->vargs) m->flags |= METH_VARGS;
  m->rets = info->proto->rsz;
  m->args = info->proto->psz;
  m->locals = info->proto->psz + info->locals;
  m->kf.codeinfo = *info->code;
  m->kf.atbl = atbl;
  return (Object *)m;
}

/*-------------------------------------------------------------------------*/

static void method_free(Object *ob)
{
  MethodObject *m = OB_TYPE_OF(ob, MethodObject, Method_Klass);
  free(m);
}

Klass Method_Klass = {
  OBJECT_HEAD_INIT(&Klass_Klass),
  .name  = "Method",
  .bsize = sizeof(MethodObject),

  .ob_free = method_free
};
