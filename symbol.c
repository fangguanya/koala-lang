
#include "symbol.h"
#include "hash.h"
#include "log.h"

static Symbol *symbol_new(void)
{
  Symbol *sym = calloc(1, sizeof(Symbol));
  Init_HashNode(&sym->hnode, sym);
  return sym;
}

static void symbol_free(Symbol *sym)
{
  if (sym->kind == SYM_STABLE) {
    STbl_Free(sym->stbl);
  }
  free(sym);
}

static uint32 symbol_hash(void *k)
{
  Symbol *s = k;
  return hash_uint32(s->name, 0);
}

static int symbol_equal(void *k1, void *k2)
{
  Symbol *s1 = k1;
  Symbol *s2 = k2;
  return s1->name == s2->name;
}

static void ht_symbol_free(HashNode *hnode, void *arg)
{
  UNUSED_PARAMETER(arg);
  Symbol *sym = container_of(hnode, Symbol, hnode);
  symbol_free(sym);
}

#define SYMBOL_ACCESS(name) \
  (isupper(name[0]) ? ACCESS_PUBLIC : ACCESS_PRIVATE)

/*-------------------------------------------------------------------------*/

static HashTable *__get_hashtable(STable *stbl)
{
  if (stbl->htbl == NULL) {
    HashInfo hashinfo;
    Init_HashInfo(&hashinfo, symbol_hash, symbol_equal);
    stbl->htbl = HashTable_New(&hashinfo);
  }
  return stbl->htbl;
}

int STbl_Init(STable *stbl, AtomTable *atbl)
{
  stbl->htbl = NULL;
  if (atbl == NULL) {
    HashInfo hashinfo;
    Init_HashInfo(&hashinfo, item_hash, item_equal);
    stbl->atbl = AtomTable_New(&hashinfo, ITEM_MAX);
  } else {
    stbl->atbl = atbl;
  }
  //FIXME: start from 1st position, because position-0 is for object
  stbl->next = 1;
  return 0;
}

void STbl_Fini(STable *stbl)
{
  HashTable_Free(stbl->htbl, ht_symbol_free, NULL);
  //AtomTable_Free(stbl->atbl, NULL, NULL);
  stbl->htbl = NULL;
  stbl->atbl = NULL;
  stbl->next = 1;
}

STable *STbl_New(AtomTable *atbl)
{
  STable *stbl = malloc(sizeof(STable));
  STbl_Init(stbl, atbl);
  return stbl;
}

void STbl_Free(STable *stbl)
{
  STbl_Fini(stbl);
  free(stbl);
}

Symbol *STbl_Add_Var(STable *stbl, char *name, TypeDesc *desc, bool konst)
{
  Symbol *sym = STbl_Add_Symbol(stbl, name, SYM_VAR, konst);
  if (sym == NULL) return NULL;

  idx_t idx = -1;
  if (desc != NULL) {
    idx = TypeItem_Set(stbl->atbl, desc);
    ASSERT(idx >= 0);
  }
  sym->desc  = idx;
  sym->type  = desc;
  sym->index = stbl->next++;
  return sym;
}

Symbol *STbl_Add_Proto(STable *stbl, char *name, Proto *proto)
{
  Symbol *sym = STbl_Add_Symbol(stbl, name, SYM_PROTO, 0);
  if (sym == NULL) return NULL;

  idx_t idx = ProtoItem_Set(stbl->atbl, proto);
  ASSERT(idx >= 0);
  sym->desc = idx;
  TypeDesc *type = TypeDesc_New(TYPE_PROTO);
  type->proto = proto;
  sym->type = type;
  return sym;
}

Symbol *STbl_Add_IProto(STable *stbl, char *name, Proto *proto)
{
  Symbol *sym = STbl_Add_Proto(stbl, name, proto);
  if (sym == NULL) return NULL;
  sym->kind = SYM_IPROTO;
  return sym;
}

Symbol *STbl_Add_Symbol(STable *stbl, char *name, int kind, bool konst)
{
  Symbol *sym = symbol_new();
  idx_t idx = StringItem_Set(stbl->atbl, name);
  ASSERT(idx >= 0);
  sym->name = idx;
  if (HashTable_Insert(__get_hashtable(stbl), &sym->hnode) < 0) {
    symbol_free(sym);
    return NULL;
  }
  sym->kind = kind;
  sym->konst = konst;
  sym->access = SYMBOL_ACCESS(name);
  sym->str = name;
  return sym;
}

void STbl_Delete(STable *stbl, Symbol *sym)
{
  HashTable_Remove(__get_hashtable(stbl), &sym->hnode);
  symbol_free(sym);
}

Symbol *STbl_Get(STable *stbl, char *name)
{
  idx_t index = StringItem_Get(stbl->atbl, name);
  if (index < 0) return NULL;
  Symbol sym = {.name = index};
  HashNode *hnode = HashTable_Find(__get_hashtable(stbl), &sym);
  if (hnode != NULL) {
    return container_of(hnode, Symbol, hnode);
  } else {
    return NULL;
  }
}

/*-------------------------------------------------------------------------*/

struct visit_entry {
  symbolfunc fn;
  void *arg;
};

static void symbol_visit(HashNode *hnode, void *arg)
{
  struct visit_entry *data = arg;
  Symbol *sym = container_of(hnode, Symbol, hnode);
  data->fn(sym, data->arg);
}

void STbl_Traverse(STable *stbl, symbolfunc fn, void *arg)
{
  struct visit_entry data = {fn, arg};
  HashTable_Traverse(stbl->htbl, symbol_visit, &data);
}

/*-------------------------------------------------------------------------*/

static void desc_show(TypeDesc *type)
{
  char *str = TypeDesc_ToString(type);
  printf("%s", str);
  free(str);
}

static void symbol_show(HashNode *hnode, void *arg)
{
  UNUSED_PARAMETER(arg);
  Symbol *sym = container_of(hnode, Symbol, hnode);
  switch (sym->kind) {
    case SYM_VAR: {
      /* show's format: "type name desc;" */
      printf("%s %s ", sym->konst ? "const":"var", sym->str);
      desc_show(sym->type);
      puts(";"); /* with newline */
      break;
    }
    case SYM_PROTO: {
      /* show's format: "func name args rets;" */
      printf("func %s", sym->str);
      desc_show(sym->type);
      puts(""); /* with newline */
      break;
    }
    case SYM_CLASS: {
      printf("class %s;\n", sym->str);
      break;
    }
    case SYM_INTF: {
      printf("interface %s;\n", sym->str);
      break;
    }
    default: {
      ASSERT(0);
      break;
    }
  }
}

void STbl_Show(STable *stbl, int detail)
{
  HashTable_Traverse(stbl->htbl, symbol_show, stbl);
  if (detail) AtomTable_Show(stbl->atbl);
}
