
#include "codeobject.h"
#include "tupleobject.h"
#include "moduleobject.h"
#include "symbol.h"

static CodeObject *code_new(int flags)
{
  CodeObject *code = calloc(1, sizeof(CodeObject));
  init_object_head(code, &Code_Klass);
  code->flags = flags;
  return code;
}

Object *CFunc_New(cfunc cf)
{
  CodeObject *code = code_new(CODE_CLANG);
  code->cf = cf;
  return (Object *)code;
}

Object *KFunc_New(int locvars, uint8 *codes, int size)
{
  CodeObject *code = code_new(CODE_KLANG);
  code->kf.locvars = locvars;
  code->kf.codes = codes;
  code->kf.size = size;
  return (Object *)code;
}

/*-------------------------------------------------------------------------*/

Klass Code_Klass = {
  OBJECT_HEAD_INIT(&Klass_Klass),
  .name  = "Code",
  .bsize = sizeof(CodeObject),
};