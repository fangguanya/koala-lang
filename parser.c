
#include "parser.h"
#include "koala.h"
#include "hash.h"
#include "ast.h"
#include "opcode.h"

extern FILE *yyin;
extern int yyparse(ParserState *ps);
static void parse_body(ParserState *ps, Vector *stmts);
static void parser_visit_expr(ParserState *ps, struct expr *exp);
static ParserUnit *parent_scope(ParserState *ps);

/*-------------------------------------------------------------------------*/

static Import *import_new(char *path)
{
  Import *import = malloc(sizeof(Import));
  import->path = path;
  Init_HashNode(&import->hnode, import);
  return import;
}

static void import_free(Import *import)
{
  free(import);
}

static uint32 import_hash(void *k)
{
  Import *import = k;
  return hash_string(import->path);
}

static int import_equal(void *k1, void *k2)
{
  Import *import1 = k1;
  Import *import2 = k2;
  return !strcmp(import1->path, import2->path);
}

static void init_imports(ParserState *ps)
{
  HashInfo hashinfo;
  Init_HashInfo(&hashinfo, import_hash, import_equal);
  HashTable_Init(&ps->imports, &hashinfo);
  STbl_Init(&ps->extstbl, NULL);
  Symbol *sym = parse_import(ps, "lang", "koala/lang");
  sym->refcnt++;
}

static void visit_import(HashNode *hnode, void *arg)
{
  Symbol *sym = container_of(hnode, Symbol, hnode);
  if (sym->refcnt == 0) {
    warn("package '%s <- %s' is never used",
         sym->str, TypeDesc_ToString(sym->type));
  }
}

static void check_imports(ParserState *ps)
{
  SymTable *stbl = &ps->extstbl;
  HashTable_Traverse(stbl->htbl, visit_import, NULL);
}

static void visit_symbol(HashNode *hnode, void *arg)
{
  Symbol *sym = container_of(hnode, Symbol, hnode);
  if ((sym->access == ACCESS_PRIVATE) && (sym->refcnt == 0)) {
    if (sym->kind == SYM_VAR) {
      warn("variable '%s' is never used", sym->str);
    } else if (sym->kind == SYM_PROTO) {
      warn("function '%s' is never used", sym->str);
    }
  }
}

static void check_variables(ParserState *ps)
{
  ParserUnit *u = ps->u;
  ASSERT_PTR(u);
  SymTable *stbl = &u->stbl;
  HashTable_Traverse(stbl->htbl, visit_symbol, NULL);
}

static void check_unused_symbols(ParserState *ps)
{
  check_imports(ps);
  check_variables(ps);
}

/*-------------------------------------------------------------------------*/

char *userdef_get_path(ParserState *ps, char *mod)
{
  Symbol *sym = STbl_Get(&ps->extstbl, mod);
  if (sym == NULL) {
    error("cannot find module:%s", mod);
    return NULL;
  }
  ASSERT(sym->kind == SYM_STABLE);
  sym->refcnt = 1;
  return sym->type->path;
}

static int check_return_types(ParserUnit *u, Vector *vec)
{
  Proto *proto = u->sym->type->proto;
  if (vec == NULL) {
    return (proto->rsz == 0) ? 1 : 0;
  } else {
    int sz = Vector_Size(vec);
    if (proto->rsz != sz) return 0;
    Vector_ForEach(exp, struct expr, vec) {
      if (!TypeDesc_Check(exp->type, proto->rdesc + i)) {
        error("type check failed");
        return 0;
      }
    }
    return 1;
  }
}

/*--------------------------------------------------------------------------*/

static Symbol *find_id_symbol(ParserState *ps, char *id)
{
  ParserUnit *u = ps->u;
  Symbol *sym = STbl_Get(&u->stbl, id);
  if (sym != NULL) {
    debug("symbol '%s' is found in current scope", id);
    sym->refcnt++;
    return sym;
  }

  if (!list_empty(&ps->ustack)) {
    list_for_each_entry(u, &ps->ustack, link) {
      sym = STbl_Get(&u->stbl, id);
      if (sym != NULL) {
        debug("symbol '%s' is found in parent scope", id);
        sym->refcnt++;
        return sym;
      }
    }
  }

  sym = STbl_Get(&ps->extstbl, id);
  if (sym != NULL) {
    debug("symbol '%s' is found in external scope", id);
    ASSERT(sym->kind == SYM_STABLE);
    sym->refcnt++;
    return sym;
  }

  error("cannot find symbol:%s", id);
  return NULL;
}

static Symbol *find_userdef_symbol(ParserState *ps, TypeDesc *desc)
{
  if (desc->kind != TYPE_USERDEF) {
    error("type(%s) is not class or interface", TypeDesc_ToString(desc));
    return NULL;
  }

  Import key = {.path = desc->path};
  Import *import = HashTable_FindObject(&ps->imports, &key, Import);
  if (import == NULL) {
    error("cannot find '%s.%s'", desc->path, desc->type);
    return NULL;
  }

  Symbol *sym = import->sym;
  ASSERT(sym->kind == SYM_STABLE);
  sym = STbl_Get(sym->obj, desc->type);
  if (sym != NULL) {
    debug("find '%s.%s'", desc->path, desc->type);
    sym->refcnt++;
    return sym;
  }

  error("cannot find '%s.%s'", desc->path, desc->type);
  return NULL;
}

/*--------------------------------------------------------------------------*/
static Inst *inst_new(uint8 op, TValue *val)
{
  Inst *i = malloc(sizeof(Inst));
  i->op = op;
  if (val != NULL)
    i->val = *val;
  else
    initnilvalue(&i->val);
  return i;
}

static void inst_free(void *item, void *arg)
{
  UNUSED_PARAMETER(arg);
  free(item);
}

static void code_insert(ParserState *ps, int index, Inst *inst)
{
  ParserUnit *u = ps->u;
  Vector_Insert(&u->insts, index, inst);
}

static void code_append(ParserState *ps, Inst *inst)
{
  ParserUnit *u = ps->u;
  Vector_Append(&u->insts, inst);
}

static void code_gen(SymTable *stbl, Buffer *buf, Inst *inst)
{
  int index;
  Buffer_Write_Byte(buf, inst->op);
  switch (inst->op) {
    case OP_HALT: {
      break;
    }
    case OP_LOADK: {
      index = ConstItem_Set_String(stbl->atbl, inst->val.cstr);
      Buffer_Write_4Bytes(buf, index);
      break;
    }
    case OP_LOADM: {
      index = ConstItem_Set_String(stbl->atbl, inst->val.cstr);
      Buffer_Write_4Bytes(buf, index);
      break;
    }
    case OP_LOAD: {
      break;
    }
    case OP_STORE: {
      break;
    }
    case OP_GETFIELD: {
      break;
    }
    case OP_SETFIELD: {
      break;
    }
    case OP_CALL: {
      index = ConstItem_Set_String(stbl->atbl, inst->val.cstr);
      Buffer_Write_4Bytes(buf, index);
      break;
    }
    case OP_RET: {
      break;
    }
    default: {
      ASSERT(0);
      break;
    }
  }
}

static void code_merge(ParserState *ps)
{
  ParserUnit *u = ps->u;
  ParserUnit *parent = parent_scope(ps);
  if (parent == NULL) {
    debug("a module");
    return;
  }

  if (parent->scope == SCOPE_MODULE) {
    if (u->scope == SCOPE_FUNCTION) {
      debug("save code to function");
      code_append(ps, inst_new(OP_RET, NULL));
      Buffer buf;
      Buffer_Init(&buf, 32);
      Vector_ForEach(inst, Inst, &u->insts) {
        code_gen(&u->stbl, &buf, inst);
      }
      uint8 *data = Buffer_RawData(&buf);
      Object *code = KFunc_New(u->stbl.next, data, Buffer_Size(&buf));
      u->sym->obj = code;
      Buffer_Fini(&buf);
    }
  }
}

static void code_visit_symbol(Symbol *sym, void *arg)
{
  KImage *image = arg;
  switch (sym->kind) {
    case SYM_VAR: {

      break;
    }
    case SYM_PROTO: {

      break;
    }
    default: {
      ASSERT_MSG(0, "unknown symbol kind:%d", sym->kind);
    }
  }
}

static void code_to_image(ParserState *ps)
{
  ParserUnit *u = ps->u;
  KImage *image = KImage_New(ps->package);
  STbl_Traverse(&u->stbl, code_visit_symbol, image);
}

static void code_show(Vector *insts)
{
  char buf[64];

  printf("-----------------------\n");
  Vector_ForEach(inst, Inst, insts) {
    printf("opcode:%s\n", OPCode_ToString(inst->op));
    TValue_Print(buf, sizeof(buf), &inst->val);
    printf("arg:%s\n", buf);
    printf("-----------------------\n");
  }
  printf("-----------------------\n");
}

/*--------------------------------------------------------------------------*/

void parse_dotaccess(ParserState *ps, struct expr *exp)
{
  struct expr *left = exp->attribute.left;
  left->ctx = CTX_LOAD;
  parser_visit_expr(ps, left);

  debug(".%s", exp->attribute.id);

  Symbol *leftsym = left->sym;
  if (leftsym == NULL) {
    error("cannot find '%s' in '%s'", exp->attribute.id, left->str);
    return;
  }

  Symbol *sym = NULL;
  if (leftsym->kind == SYM_STABLE) {
    debug("symbol '%s' is a module", leftsym->str);
    sym = STbl_Get(leftsym->obj, exp->attribute.id);
    if (sym == NULL) {
      error("cannot find symbol '%s' in '%s'", exp->attribute.id, left->str);
      exp->sym = NULL;
      return;
    }
  } else if (leftsym->kind == SYM_VAR) {
    sym = find_userdef_symbol(ps, leftsym->type);
    if (sym == NULL) {
      error("cannot find symbol '%s' in '%s'", exp->attribute.id,
            TypeDesc_ToString(leftsym->type));
      return;
    }
    ASSERT(sym->kind == SYM_STABLE);
    char *typename = sym->str;
    sym = STbl_Get(sym->obj, exp->attribute.id);
    if (sym == NULL) {
      error("cannot find '%s' in '%s'", exp->attribute.id, typename);
    }
  } else {
    ASSERT(0);
  }

  exp->sym = sym;

  // generate instruction
  if (sym->kind == SYM_VAR) {
    ASSERT(0);
  } else if (sym->kind == SYM_PROTO) {
    TValue val = CSTR_VALUE_INIT(exp->attribute.id);
    code_append(ps, inst_new(OP_CALL, &val));
  } else {
    ASSERT(0);
  }
}

static int check_call_args(Proto *proto, Vector *vec)
{
  int sz = (vec == NULL) ? 0: Vector_Size(vec);

  if (Proto_With_Vargs(proto)) {
    if (proto->psz -1 > sz) {
      return 0;
    } else {
      TypeDesc *desc;
      Vector_ForEach(exp, struct expr, vec) {
        if (i < proto->psz - 1)
          desc = proto->pdesc + i;
        else
          desc = proto->pdesc + proto->psz - 1;
        if (!TypeDesc_Check(exp->type, desc))
          return 0;
      }
    }
  }

  if (proto->psz != sz) {
    return 0;
  }

  Vector_ForEach(exp, struct expr, vec) {
    if (!TypeDesc_Check(exp->type, proto->pdesc + i))
      return 0;
  }

  return 1;
}

void parse_call(ParserState *ps, struct expr *exp)
{
  struct expr *left = exp->call.left;
  left->ctx = CTX_LOAD;
  parser_visit_expr(ps, left);

  if (left->sym == NULL) {
    error("func is not found");
    return;
  }

  Symbol *sym = left->sym;
  if (sym->kind == SYM_PROTO) {
    debug("call %s()", left->sym->str);
    /* check arguments */
    if (!check_call_args(sym->type->proto, exp->call.pvec)) {
      error("arguments are not matched.");
    } else {
      Vector_ForEach(e, struct expr, exp->call.pvec) {
        parser_visit_expr(ps, e);
      }
    }
  } else {
    ASSERT(0);
  }
}

static void parser_visit_expr(ParserState *ps, struct expr *exp)
{
  switch (exp->kind) {
    case NAME_KIND: {
      exp->sym = find_id_symbol(ps, exp->name.id);
      if (exp->type == NULL) {
        exp->type = exp->sym->type;
      }
      char *load = exp->ctx == CTX_STORE ? "store": "load";
      debug("name:%s(%s), type:%s",
            exp->name.id, load, TypeDesc_ToString(exp->type));
      if (exp->sym->kind == SYM_STABLE) {
        TValue val = CSTR_VALUE_INIT(exp->type->path);
        code_append(ps, inst_new(OP_LOADM, &val));
      } else if (exp->sym->kind == SYM_VAR) {

      } else {
        ASSERT(0);
      }
      break;
    }
    case INT_KIND: {
      if (exp->ctx == CTX_STORE) {
        error("cannot assign to %lld", exp->ival);
      }
      break;
    }
    case FLOAT_KIND: {
      if (exp->ctx == CTX_STORE) {
        error("cannot assign to %f", exp->fval);
      }
      break;
    }
    case BOOL_KIND: {
      if (exp->ctx == CTX_STORE) {
        error("cannot assign to %s", exp->bval ? "true":"false");
      }
      break;
    }
    case STRING_KIND: {
      if (exp->ctx == CTX_STORE) {
        error("cannot assign to %s", exp->str);
      } else {
        TValue val = CSTR_VALUE_INIT(exp->str);
        code_insert(ps, 0, inst_new(OP_LOADK, &val));
      }
      break;
    }
    case ATTRIBUTE_KIND: {
      parse_dotaccess(ps, exp);
      break;
    }
    case CALL_KIND: {
      parse_call(ps, exp);
      break;
    }
    case BINARY_KIND: {
      debug("binary_op:%d", exp->bin_op.op);
      parser_visit_expr(ps, exp->bin_op.left);
      exp->type = exp->bin_op.left->type;
      parser_visit_expr(ps, exp->bin_op.right);
      break;
    }
    default:
      ASSERT_MSG(0, "unknown expression type: %d", exp->kind);
      break;
  }
}

/*--------------------------------------------------------------------------*/

static void parser_enter_scope(ParserState *ps, int scope)
{
  AtomTable *atbl = NULL;
  ParserUnit *u = calloc(1, sizeof(ParserUnit));
  init_list_head(&u->link);
  if (ps->u != NULL) atbl = ps->u->stbl.atbl;
  STbl_Init(&u->stbl, atbl);
  u->scope = scope;

  /* Push the old ParserUnit on the stack. */
  if (ps->u != NULL) {
    list_add(&ps->u->link, &ps->ustack);
  }

  ps->u = u;
  ps->nestlevel++;
}

static void parser_unit_free(ParserUnit *u)
{
  STbl_Fini(&u->stbl);
  Vector_Fini(&u->insts, inst_free, NULL);
  free(u);
}

static void parser_exit_scope(ParserState *ps)
{
  printf("-------------------------\n");
  printf("scope-%d symbols:\n", ps->nestlevel);
  STbl_Show(&ps->u->stbl, 0);
  check_unused_symbols(ps);
  code_show(&ps->u->insts);
  printf("-------------------------\n");

  code_merge(ps);

  ps->nestlevel--;
  parser_unit_free(ps->u);
  /* Restore c->u to the parent unit. */
  struct list_head *first = list_first(&ps->ustack);
  if (first != NULL) {
    list_del(first);
    ps->u = container_of(first, ParserUnit, link);
  } else {
    ps->u = NULL;
  }
}

static ParserUnit *parent_scope(ParserState *ps)
{
  if (list_empty(&ps->ustack)) return NULL;
  return list_first_entry(&ps->ustack, ParserUnit, link);
}

/*--------------------------------------------------------------------------*/

void parse_variable(ParserState *ps, struct var *var, struct expr *exp)
{
  ParserUnit *u = ps->u;
  ASSERT_PTR(u);

  if (u->scope == SCOPE_MODULE || u->scope == SCOPE_CLASS) {
    if (exp == NULL) {
      debug("variable declaration is not need generate code");
      return;
    }

    if (exp->kind == NIL_KIND) {
      debug("nil value");
      return;
    }

    if (exp->kind == SELF_KIND) {
      error("cannot use keyword 'self'");
      return;
    }

    ASSERT_PTR(exp->type);

    if (!TypeDesc_Check(var->type, exp->type)) {
      error("typecheck failed");
    } else {
      // parse exp
      debug("parse exp");
      exp->ctx = CTX_LOAD;  /* rvalue */
      parser_visit_expr(ps, exp);

      //generate code
      debug("generate code");
    }
  } else if (u->scope == SCOPE_FUNCTION) {
    debug("parse func vardecl, '%s'", var->id);
    ASSERT(!list_empty(&ps->ustack));
    ParserUnit *parent = parent_scope(ps);
    ASSERT(parent->scope == SCOPE_MODULE || parent->scope == SCOPE_CLASS);
    STbl_Add_Var(&u->stbl, var->id, var->type, var->bconst);
    if (exp != NULL) {
      parser_visit_expr(ps, exp);
      if (!TypeDesc_Check(var->type, exp->type))
        error("typecheck failed");
    }
  } else if (u->scope == SCOPE_BLOCK) {
    debug("parse block vardecl");
    ASSERT(!list_empty(&ps->ustack));
  } else {
    ASSERT_MSG(0, "unknown unit scope:%d", u->scope);
  }
}

void parse_function(ParserState *ps, struct stmt *stmt)
{
  parser_enter_scope(ps, SCOPE_FUNCTION);

  ParserUnit *parent = parent_scope(ps);
  Symbol *sym = STbl_Get(&parent->stbl, stmt->funcdecl.id);
  ASSERT_PTR(sym);
  ps->u->sym = sym;

  if (parent->scope == SCOPE_MODULE) {
    debug("parse function, '%s'", stmt->funcdecl.id);
    Vector_ForEach(var, struct var, stmt->funcdecl.pvec) {
      parse_variable(ps, var, NULL);
    }
    parse_body(ps, stmt->funcdecl.body);
  } else if (parent->scope == SCOPE_CLASS) {
    debug("parse method, '%s'", stmt->funcdecl.id);
  } else {
    ASSERT_MSG(0, "unknown parent scope type:%d", parent->scope);
  }

  parser_exit_scope(ps);
}

void parse_assign(ParserState *ps, struct stmt *stmt)
{
  struct expr *r = stmt->assign.right;
  struct expr *l = stmt->assign.left;
  r->ctx = CTX_LOAD;
  parser_visit_expr(ps, r);
  l->ctx = CTX_STORE;
  parser_visit_expr(ps, l);
}

void paser_return(ParserState *ps, struct stmt *stmt)
{
  ParserUnit *u = ps->u;
  ASSERT_PTR(u);
  if (u->scope == SCOPE_FUNCTION) {
    debug("return in function");
    Vector_ForEach(e, struct expr, stmt->vec) {
      parser_visit_expr(ps, e);
    }
    check_return_types(u, stmt->vec);
  } else if (u->scope == SCOPE_BLOCK) {
    debug("return in some block");
    check_return_types(u, stmt->vec);
  } else {
    ASSERT_MSG(0, "invalid scope:%d", u->scope);
  }

}

void parser_visit_stmt(ParserState *ps, struct stmt *stmt)
{
  switch (stmt->kind) {
    case VARDECL_KIND: {
      parse_variable(ps, stmt->vardecl.var, stmt->vardecl.exp);
      break;
    }
    case FUNCDECL_KIND:
      parse_function(ps, stmt);
      break;
    case CLASS_KIND:
      break;
    case INTF_KIND:
      break;
    case EXPR_KIND: {
      parser_visit_expr(ps, stmt->exp);
      break;
    }
    case ASSIGN_KIND: {
      parse_assign(ps, stmt);
      break;
    }
    case RETURN_KIND: {
      paser_return(ps, stmt);
      break;
    }
    case VARDECL_LIST_KIND: {
      parse_body(ps, stmt->vec);
      break;
    }
    default:
      ASSERT_MSG(0, "unknown statement type: %d", stmt->kind);
      break;
  }
}

/*--------------------------------------------------------------------------*/

/* parse a sequence of statements */
static void parse_body(ParserState *ps, Vector *stmts)
{
  if (stmts == NULL) return;
  Vector_ForEach(stmt, struct stmt, stmts) {
    parser_visit_stmt(ps, stmt);
  }
}

static void init_parser(ParserState *ps)
{
  Koala_Init();
  memset(ps, 0, sizeof(ParserState));
  init_imports(ps);
  init_list_head(&ps->ustack);
  Vector_Init(&ps->errors);
  parser_enter_scope(ps, SCOPE_MODULE);
}

static void fini_parser(ParserState *ps)
{
  parser_exit_scope(ps);
  Koala_Fini();
}

int main(int argc, char *argv[])
{
  ParserState ps;

  if (argc < 2) {
    printf("error: no input files\n");
    return -1;
  }

  init_parser(&ps);

  yyin = fopen(argv[1], "r");
  yyparse(&ps);
  fclose(yyin);

  fini_parser(&ps);

  return 0;
}

/*--------------------------------------------------------------------------*/

static Symbol *add_import(SymTable *stbl, char *id, char *path)
{
  Symbol *sym = STbl_Add_Symbol(stbl, id, SYM_STABLE, 0);
  if (sym == NULL) return NULL;
  idx_t idx = StringItem_Set(stbl->atbl, path);
  ASSERT(idx >= 0);
  sym->desc = idx;
  sym->type = TypeDesc_From_PkgPath(path);
  return sym;
}

Symbol *parse_import(ParserState *ps, char *id, char *path)
{
  Import key = {.path = path};
  Import *import = HashTable_FindObject(&ps->imports, &key, Import);
  Symbol *sym;
  if (import != NULL) {
    sym = import->sym;
    if (sym != NULL && sym->refcnt > 0) {
      warn("find auto imported module '%s'", path);
      if (id != NULL) {
        if (strcmp(id, sym->str)) {
          warn("imported as '%s' is different with auto imported as '%s'",
                id, sym->str);
        } else {
          warn("imported as '%s' is the same with auto imported as '%s'",
                id, sym->str);
        }
      }
      return sym;
    }
  }

  import = import_new(path);
  if (HashTable_Insert(&ps->imports, &import->hnode) < 0) {
    error("module '%s' is imported duplicated", path);
    return NULL;
  }
  Object *ob = Koala_Load_Module(path);
  if (ob == NULL) {
    error("load module '%s' failed", path);
    HashTable_Remove(&ps->imports, &import->hnode);
    import_free(import);
    return NULL;
  }
  if (id == NULL) id = Module_Name(ob);
  sym = add_import(&ps->extstbl, id, path);
  if (sym == NULL) {
    debug("add import '%s <- %s' failed", id, path);
    HashTable_Remove(&ps->imports, &import->hnode);
    import_free(import);
    return NULL;
  }
  debug("add import '%s <- %s' successful", id, path);
  sym->obj = Module_Get_STable(ob, ps->extstbl.atbl);
  import->sym = sym;
  return sym;
}

void parse_vardecl(ParserState *ps, struct stmt *s)
{
  ASSERT(s->kind == VARDECL_LIST_KIND);
  struct var *var;
  Symbol *sym;

  Vector_ForEach(stmt, struct stmt, s->vec) {
    var = stmt->vardecl.var;
    if (var->type->kind == TYPE_USERDEF) {
      if (!find_userdef_symbol(ps, var->type)) {
        error("add '%s %s' failed, because cannot find it's type:%s.%s",
              var->bconst ? "const":"var", var->id,
              var->type->path, var->type->type);
        continue;
      }
    }
    sym = STbl_Add_Var(&ps->u->stbl, var->id, var->type, var->bconst);
    if (sym != NULL) {
      debug("add '%s %s' successful", var->bconst ? "const":"var", var->id);
    } else {
      error("add '%s %s' failed, because it'name is duplicated",
            var->bconst ? "const":"var", var->id);
    }
  }
}

static int var_vec_to_arr(Vector *vec, TypeDesc **arr)
{
  int sz;
  TypeDesc *desc = NULL;
  if (vec == NULL || Vector_Size(vec) == 0) {
    sz = 0;
  } else {
    sz = Vector_Size(vec);
    desc = malloc(sizeof(TypeDesc) * sz);
    ASSERT_PTR(desc);
    Vector_ForEach(var, struct var, vec) {
      memcpy(desc + i, var->type, sizeof(TypeDesc));
    }
  }

  *arr = desc;
  return sz;
}

void parse_funcdecl(ParserState *ps, struct stmt *stmt)
{
  Proto proto;
  int sz;
  TypeDesc *desc = NULL;
  Symbol *sym;

  sz = TypeDesc_Vec_To_Arr(stmt->funcdecl.rvec, &desc);
  proto.rsz = sz; proto.rdesc = desc;

  sz = var_vec_to_arr(stmt->funcdecl.pvec, &desc);
  proto.psz = sz; proto.pdesc = desc;

  sym = STbl_Add_Proto(&ps->u->stbl, stmt->funcdecl.id, &proto);
  if (sym != NULL) {
    debug("add 'func %s' successful", stmt->funcdecl.id);
  } else {
    debug("add 'func %s' failed", stmt->funcdecl.id);
  }
}

void parse_typedecl(ParserState *ps, struct stmt *stmt)
{

}

void parse_module(ParserState *ps, struct mod *mod)
{
  debug("==begin==================");
  ps->package = mod->package;
  parse_body(ps, &mod->stmts);
  debug("==end===================");
  printf("package:%s\n", ps->package);
  STbl_Show(&ps->u->stbl, 1);
  check_imports(ps);
  check_unused_symbols(ps);
  code_to_image(ps);
}
