
#include "koala.h"

/*-------------------------------------------------------------------------*/

Object *Module_New(char *name)
{
  ModuleObject *ob = malloc(sizeof(ModuleObject));
  init_object_head(ob, &Module_Klass);
  ob->name = name;
  STbl_Init(&ob->stbl, NULL);
  ob->tuple = NULL;
  return (Object *)ob;
}

void Module_Free(Object *ob)
{
  OB_ASSERT_KLASS(ob, Module_Klass);
  free(ob);
}

int Module_Add_Var(Object *ob, char *name, TypeDesc *desc, int bconst)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Add_Var(&mob->stbl, name, desc, bconst);
  return (sym != NULL) ? 0 : -1;
}

int Module_Add_Func(Object *ob, char *name, ProtoInfo *proto, Object *meth)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Add_Proto(&mob->stbl, name, proto);
  if (sym != NULL) {
    sym->obj = meth;
    return 0;
  }
  return -1;
}

int Module_Add_CFunc(Object *ob, FuncDef *f)
{
  ProtoInfo proto;
  Init_ProtoInfo(&f->type, &proto);
  Object *meth = CFunc_New(f->fn, &proto);
  return Module_Add_Func(ob, f->name, &proto, meth);
}

int Module_Add_Class(Object *ob, Klass *klazz)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Add_Class(&mob->stbl, klazz->name);
  if (sym != NULL) {
    sym->obj = klazz;
    STbl_Init(&klazz->stbl, Module_AtomTable(mob));
    return 0;
  }
  return -1;
}

int Module_Add_Interface(Object *ob, Klass *klazz)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Add_Intf(&mob->stbl, klazz->name);
  if (sym != NULL) {
    sym->obj = klazz;
    STbl_Init(&klazz->stbl, Module_AtomTable(mob));
    return 0;
  }
  return -1;
}

static int __get_value_index(ModuleObject *mob, char *name)
{
  Symbol *sym = STbl_Get(&mob->stbl, name);
  if (sym != NULL) {
    if (sym->kind == SYM_VAR) {
      return sym->index;
    } else {
      error("symbol is not a variable");
    }
  }
  return -1;
}

Object *__get_tuple(ModuleObject *mob)
{
  if (mob->tuple == NULL) {
    mob->tuple = Tuple_New(mob->stbl.next);
  }
  return mob->tuple;
}

TValue Module_Get_Value(Object *ob, char *name)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  int index = __get_value_index(mob, name);
  if (index < 0) return NilValue;
  return Tuple_Get(__get_tuple(mob), index);
}

TValue Module_Get_Value_ByIndex(Object *ob, int index)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  return Tuple_Get(__get_tuple(mob), index);
}

int Module_Set_Value(Object *ob, char *name, TValue *val)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  int index = __get_value_index(mob, name);
  return Tuple_Set(__get_tuple(mob), index, val);
}

int Module_Set_Value_ByIndex(Object *ob, int index, TValue *val)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  return Tuple_Set(__get_tuple(mob), index, val);
}

Object *Module_Get_Function(Object *ob, char *name)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Get(&mob->stbl, name);
  if (sym != NULL) {
    if (sym->kind == SYM_PROTO) {
      return sym->obj;
    } else {
      error("symbol is not a function");
    }
  }

  return NULL;
}

Klass *Module_Get_Class(Object *ob, char *name)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Get(&mob->stbl, name);
  if (sym != NULL) {
    if (sym->kind == SYM_CLASS) {
      return sym->obj;
    } else {
      error("symbol is not a class");
    }
  }
  return NULL;
}

Klass *Module_Get_Intf(Object *ob, char *name)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Get(&mob->stbl, name);
  if (sym != NULL) {
    if (sym->kind == SYM_INTF) {
      return sym->obj;
    } else {
      error("symbol is not a interface");
    }
  }
  return NULL;
}

Klass *Module_Get_Klass(Object *ob, char *name)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  Symbol *sym = STbl_Get(&mob->stbl, name);
  if (sym != NULL) {
    if (sym->kind == SYM_CLASS || sym->kind == SYM_INTF) {
      return sym->obj;
    } else {
      error("symbol is not a class");
    }
  }
  return NULL;
}

int Module_Add_CFunctions(Object *ob, FuncDef *funcs)
{
  int res;
  FuncDef *f = funcs;
  while (f->name != NULL) {
    res = Module_Add_CFunc(ob, f);
    ASSERT(res == 0);
    ++f;
  }
  return 0;
}

static void symbol_visit(Symbol *sym, void *arg)
{
  SymTable *stbl = arg;
  Symbol *tmp;
  if (sym->kind == SYM_CLASS || sym->kind == SYM_INTF) {
    tmp = STbl_Add_Symbol(stbl, sym->str, SYM_STABLE, 0);
    tmp->obj = STbl_New(stbl->atbl);
    STbl_Traverse(Klass_STable(sym->obj), symbol_visit, tmp->obj);
  } else if (sym->kind == SYM_VAR) {
    STbl_Add_Var(stbl, sym->str, sym->type, sym->konst);
  } else if (sym->kind == SYM_PROTO) {
    STbl_Add_Proto(stbl, sym->str, sym->type->proto);
  } else if (sym->kind == SYM_IPROTO) {
    STbl_Add_IProto(stbl, sym->str, sym->type->proto);
  } else {
    ASSERT(0);
  }
}

SymTable *Module_Get_STable(Object *ob, AtomTable *atbl)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  SymTable *stbl = STbl_New(atbl);
  STbl_Traverse(&mob->stbl, symbol_visit, stbl);
  return stbl;
}

/*-------------------------------------------------------------------------*/

static void module_free(Object *ob)
{
  Module_Free(ob);
}

Klass Module_Klass = {
  OBJECT_HEAD_INIT(&Klass_Klass),
  .name  = "Module",
  .bsize = sizeof(ModuleObject),

  .ob_free = module_free
};

/*-------------------------------------------------------------------------*/

void Module_Show(Object *ob)
{
  ModuleObject *mob = OBJ_TO_MOD(ob);
  printf("package:%sym\n", mob->name);
  STbl_Show(&mob->stbl, 1);
}
