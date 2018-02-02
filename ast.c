
#include "ast.h"
#include "codeformat.h"
#include "log.h"

struct type *type_new(int kind)
{
  struct type *type = calloc(1, sizeof(struct type));
  type->kind = kind;
  return type;
}

struct type *type_from_primitive(int primitive)
{
  struct type *type = type_new(PRIMITIVE_KIND);
  type->primitive = primitive;
  return type;
}

struct type *type_from_userdef(char *mod_name, char *type_name)
{
  if (!strcmp(mod_name, "lang") && !strcmp(type_name, "String")) {
    return type_from_primitive(PRIMITIVE_STRING);
  } else {
    struct type *type = type_new(USERDEF_KIND);
    type->userdef.mod = mod_name;
    type->userdef.type = type_name;
    return type;
  }
}

struct type *type_from_functype(Vector *tseq, Vector *rseq)
{
  struct type *type = type_new(FUNCTION_KIND);
  type->functype.tseq = tseq;
  type->functype.rseq = rseq;
  return type;
}

int type_check(struct type *t1, struct type *t2)
{
  if (t1->kind != t2->kind) return 0;
  if (t1->dims != t2->dims) return 0;

  int kind = t1->kind;
  int eq = 0;
  switch (kind) {
    case PRIMITIVE_KIND: {
      eq = t1->primitive == t2->primitive;
      break;
    }
    case USERDEF_KIND: {
      eq = !strcmp(t1->userdef.mod, t2->userdef.mod) &&
            !strcmp(t1->userdef.type, t2->userdef.type);
      break;
    }
    case FUNCTION_KIND: {
      eq = 0;
      break;
    }
    default: {
      ASSERT_MSG(0, "unknown type's kind %d\n", kind);
    }
  }
  return eq;
}

static char *type_tostring(struct type *t)
{
  char *str = "";
  switch (t->kind) {
    case PRIMITIVE_KIND: {
      str = primitive_tostring(t->primitive);
      break;
    }
    default: {
      assert(0);
      break;
    }
  }
  return str;
}

/*-------------------------------------------------------------------------*/

struct expr *expr_new(int kind)
{
  struct expr *exp = calloc(1, sizeof(struct expr));
  exp->kind = kind;
  return exp;
}

struct expr *expr_from_name(char *id)
{
  struct expr *expr = expr_new(NAME_KIND);
  expr->type = NULL;
  expr->name.id = id;
  return expr;
}

struct expr *expr_from_name_type(char *id, struct type *type)
{
  struct expr *expr = expr_new(NAME_KIND);
  expr->type = type;
  expr->name.id = id;
  return expr;
}

struct expr *expr_from_int(int64 ival)
{
  struct expr *expr = expr_new(INT_KIND);
  expr->type = type_from_primitive(PRIMITIVE_INT);
  expr->ival = ival;
  return expr;
}

struct expr *expr_from_float(float64 fval)
{
  struct expr *expr = expr_new(FLOAT_KIND);
  expr->type = type_from_primitive(PRIMITIVE_FLOAT);
  expr->fval = fval;
  return expr;
}

struct expr *expr_from_string(char *str)
{
  struct expr *expr = expr_new(STRING_KIND);
  expr->type = type_from_primitive(PRIMITIVE_STRING);
  expr->str  = str;
  return expr;
}

struct expr *expr_from_bool(int bval)
{
  struct expr *expr = expr_new(BOOL_KIND);
  expr->type = type_from_primitive(PRIMITIVE_BOOL);
  expr->bval = bval;
  return expr;
}

struct expr *expr_from_self(void)
{
  struct expr *expr = expr_new(SELF_KIND);
  expr->type = NULL;
  return expr;
}

struct expr *expr_from_expr(struct expr *exp)
{
  struct expr *expr = expr_new(EXP_KIND);
  expr->type = NULL;
  expr->exp  = exp;
  return expr;
}

struct expr *expr_from_nil(void)
{
  struct expr *expr = expr_new(NIL_KIND);
  expr->type = NULL;
  return expr;
}

struct expr *expr_from_array(struct type *type, Vector *dseq, Vector *tseq)
{
  struct expr *expr = expr_new(ARRAY_KIND);
  expr->type = type;
  expr->array.dseq = dseq;
  expr->array.tseq = tseq;
  return expr;
}

struct expr *expr_from_array_with_tseq(Vector *tseq)
{
  struct expr *e = expr_new(SEQ_KIND);
  e->seq = tseq;
  return e;
}

struct expr *expr_from_anonymous_func(Vector *pseq, Vector *rseq, Vector *body)
{
  struct expr *expr = expr_new(ANONYOUS_FUNC_KIND);
  expr->anonyous_func.pseq = pseq;
  expr->anonyous_func.rseq = rseq;
  expr->anonyous_func.body = body;
  return expr;
}

struct expr *expr_from_trailer(enum expr_kind kind, void *trailer,
                               struct expr *left)
{
  struct expr *expr = expr_new(kind);
  switch (kind) {
    case ATTRIBUTE_KIND: {
      expr->attribute.left = left;
      expr->attribute.id   = trailer;
      break;
    }
    case SUBSCRIPT_KIND: {
      expr->subscript.left  = left;
      expr->subscript.index = trailer;
      break;
    }
    case CALL_KIND: {
      expr->call.left = left;
      expr->call.pseq = trailer;
      break;
    }
    default: {
      fprintf(stderr, "[ERROR]unkown expression kind %d\n", kind);
      ASSERT(0);
    }
  }
  return expr;
}

struct expr *expr_from_binary(enum operator_kind kind,
                              struct expr *left, struct expr *right)
{
  struct expr *exp = expr_new(BINARY_KIND);
  exp->type = left->type;
  exp->bin_op.left  = left;
  exp->bin_op.op    = kind;
  exp->bin_op.right = right;
  return exp;
}

struct expr *expr_from_unary(enum unary_op_kind kind, struct expr *expr)
{
  struct expr *exp = expr_new(UNARY_KIND);
  exp->type = expr->type;
  exp->unary_op.op = kind;
  exp->unary_op.operand = expr;
  return exp;
}

/*--------------------------------------------------------------------------*/

struct stmt *stmt_new(int kind)
{
  struct stmt *stmt = calloc(1, sizeof(struct stmt));
  stmt->kind = kind;
  return stmt;
}

struct stmt *stmt_from_expr(struct expr *expr)
{
  struct stmt *stmt = stmt_new(EXPR_KIND);
  stmt->expr = expr;
  return stmt;
}

struct stmt *stmt_from_import(char *id, char *path)
{
  struct stmt *stmt = stmt_new(IMPORT_KIND);
  stmt->import.id = id;
  stmt->import.path = path;
  return stmt;
}

Vector *handle_vardecl_stmt(Vector *varvec, Vector *expvec,
                            int bconst, struct type *type)
{
  Vector *vec = Vector_Create();
  int vsz = Vector_Size(varvec);
  int esz = (expvec != NULL) ? Vector_Size(expvec) : 0;
  int error = 0;
  if (esz != vsz) {
    if (esz != 0)
      error("cannot assign %d values to %d variables\n", esz, vsz);
    error = 1;
  }

  struct stmt *stmt;
  struct var *var;
  for (int i = 0; i < vsz; i++) {
    var = Vector_Get(varvec, i);
    var->bconst = bconst;
    var->type = type;
    stmt = stmt_new(VARDECL_KIND);
    stmt->vardecl.var = var;
    Vector_Append(vec, stmt);
    if (!error) {
      stmt->vardecl.exp = Vector_Get(expvec, i);
      if (var->type == NULL) {
        ASSERT_PTR(stmt->vardecl.exp);
        var->type = stmt->vardecl.exp->type;
      }
    }
  }

  Vector_Destroy(varvec, NULL, NULL);
  Vector_Destroy(expvec, NULL, NULL);
  return vec;
}

struct stmt *stmt_from_funcdecl(char *id, Vector *pseq, Vector *rseq,
                                Vector *body)
{
  struct stmt *stmt = stmt_new(FUNCDECL_KIND);
  stmt->funcdecl.id = id;
  stmt->funcdecl.pseq = pseq;
  stmt->funcdecl.rseq = rseq;
  stmt->funcdecl.body = body;
  return stmt;
}

struct stmt *stmt_from_assign(Vector *left_seq, Vector *right_seq)
{
  struct stmt *stmt = stmt_new(ASSIGN_KIND);
  stmt->assign.left_seq  = left_seq;
  stmt->assign.right_seq = right_seq;
  return stmt;
}

struct stmt *stmt_from_compound_assign(struct expr *left,
                                       enum assign_operator op,
                                       struct expr *right)
{
  struct stmt *stmt = stmt_new(COMPOUND_ASSIGN_KIND);
  stmt->compound_assign.left  = left;
  stmt->compound_assign.op    = op;
  stmt->compound_assign.right = right;
  return stmt;
}

struct stmt *stmt_from_block(Vector *block)
{
  struct stmt *stmt = stmt_new(BLOCK_KIND);
  stmt->seq  = block;
  return stmt;
}

struct stmt *stmt_from_return(Vector *seq)
{
  struct stmt *stmt = stmt_new(RETURN_KIND);
  stmt->seq  = seq;
  return stmt;
}

struct stmt *stmt_from_structure(char *id, Vector *seq)
{
  struct stmt *stmt = stmt_new(CLASS_KIND);
  stmt->structure.id  = id;
  stmt->structure.seq = seq;
  return stmt;
}

struct stmt *stmt_from_interface(char *id, Vector *seq)
{
  struct stmt *stmt = stmt_new(INTF_KIND);
  stmt->structure.id  = id;
  stmt->structure.seq = seq;
  return stmt;
}

struct stmt *stmt_from_jump(int kind)
{
  struct stmt *stmt = stmt_new(kind);
  return stmt;
}

struct stmt *stmt_from_if(struct test_block *if_part,
                          Vector *elseif_seq,
                          struct test_block *else_part)
{
  struct stmt *stmt = stmt_new(IF_KIND);
  stmt->if_stmt.if_part    = if_part;
  stmt->if_stmt.elseif_seq = elseif_seq;
  stmt->if_stmt.else_part  = else_part;
  return stmt;
}

struct test_block *new_test_block(struct expr *test, Vector *body)
{
  struct test_block *tb = malloc(sizeof(struct test_block));
  tb->test = test;
  tb->body = body;
  return tb;
}

struct stmt *stmt_from_while(struct expr *test, Vector *body, int b)
{
  struct stmt *stmt = stmt_new(WHILE_KIND);
  stmt->while_stmt.btest = b;
  stmt->while_stmt.test  = test;
  stmt->while_stmt.body  = body;
  return stmt;
}

struct stmt *stmt_from_switch(struct expr *expr, Vector *case_seq)
{
  struct stmt *stmt = stmt_new(SWITCH_KIND);
  stmt->switch_stmt.expr = expr;
  stmt->switch_stmt.case_seq = case_seq;
  return stmt;
}

struct stmt *stmt_from_for(struct stmt *init, struct stmt *test,
                           struct stmt *incr, Vector *body)
{
  struct stmt *stmt = stmt_new(FOR_TRIPLE_KIND);
  stmt->for_triple_stmt.init = init;
  stmt->for_triple_stmt.test = test;
  stmt->for_triple_stmt.incr = incr;
  stmt->for_triple_stmt.body = body;
  return stmt;
}

struct stmt *stmt_from_foreach(struct var *var, struct expr *expr,
                               Vector *body, int bdecl)
{
  struct stmt *stmt = stmt_new(FOR_EACH_KIND);
  stmt->for_each_stmt.bdecl = bdecl;
  stmt->for_each_stmt.var   = var;
  stmt->for_each_stmt.expr  = expr;
  stmt->for_each_stmt.body  = body;
  return stmt;
}

struct stmt *stmt_from_go(struct expr *expr)
{
  if (expr->kind != CALL_KIND) {
    fprintf(stderr, "syntax error:not a func call\n");
    exit(0);
  }

  struct stmt *stmt = stmt_new(GO_KIND);
  stmt->go_stmt = expr;
  return stmt;
}

struct var *new_var(char *id, struct type *type)
{
  struct var *v = malloc(sizeof(struct var));
  v->id   = id;
  v->bconst = 0;
  v->type = type;
  return v;
}

struct field *new_struct_field(char *id, struct type *t, struct expr *e)
{
  return NULL;
}

struct intf_func *new_intf_func(char *id, Vector *pseq, Vector *rseq)
{
  return NULL;
}

/*--------------------------------------------------------------------------*/

void type_traverse(struct type *type)
{
  if (type != NULL) {
    printf("type kind & dims: %d:%d\n", type->kind, type->dims);
  } else {
    printf("no type declared\n");
  }
}

void array_traverse(struct expr *expr)
{
  type_traverse(expr->type);
  if (expr->array.tseq != NULL) {
    printf("array init's list, length:%d\n", Vector_Size(expr->array.tseq));
    for (int i = 0; i < Vector_Size(expr->array.tseq); i++) {
      expr_traverse(Vector_Get(expr->array.tseq, i));
    }
  } else {
    printf("array dim's list, length:%d\n", Vector_Size(expr->array.dseq));
    for (int i = 0; i < Vector_Size(expr->array.dseq); i++) {
      expr_traverse(Vector_Get(expr->array.dseq, i));
    }
  }
}

void expr_traverse(struct expr *expr)
{
  if (expr == NULL) return;

  switch (expr->kind) {
    case NAME_KIND: {
      /*
      id scope
      method:
      local variable -> field variable -> module variable
      -> external module name
      function:
      module variable -> external module name
      */
      printf("[id]%s\n", expr->name.id);
      break;
    }
    case INT_KIND: {
      printf("[int]%lld\n", expr->ival);
      break;
    }
    case FLOAT_KIND: {
      printf("[float]%f\n", expr->fval);
      break;
    }
    case STRING_KIND: {
      printf("[string]%s\n", expr->str);
      break;
    }
    case BOOL_KIND: {
      printf("%s\n", expr->bval ? "true" : "false");
      break;
    }
    case SELF_KIND: {
      printf("self\n");
      break;
    }
    case NIL_KIND: {
      printf("nil\n");
      break;
    }
    case EXP_KIND: {
      printf("[sub-expr]\n");
      expr_traverse(expr->exp);
      break;
    }
    case ARRAY_KIND: {
      printf("[new array]\n");
      array_traverse(expr);
      break;
    }
    case ANONYOUS_FUNC_KIND: {
      printf("[anonymous function]\n");
      break;
    }
    case ATTRIBUTE_KIND: {
      expr_traverse(expr->attribute.left);
      printf("[attribute].%s\n", expr->attribute.id);
      break;
    }
    case SUBSCRIPT_KIND: {
      expr_traverse(expr->subscript.left);
      printf("[subscript]\n");
      expr_traverse(expr->subscript.index);
      break;
    }
    case CALL_KIND: {
      expr_traverse(expr->call.left);
      printf("[func call]paras:\n");
      struct expr *temp;
      Vector *vec = expr->call.pseq;
      if (vec != NULL) {
        for (int i = 0; i < Vector_Size(vec); i++) {
          temp = Vector_Get(vec, i);
          expr_traverse(temp);
        }
      } else {
        printf("no args\n");
      }
      printf("[end func call]\n");
      break;
    }
    case UNARY_KIND: {
      printf("[unary expr]op:%d\n", expr->unary_op.op);
      expr_traverse(expr->unary_op.operand);
      break;
    }
    case BINARY_KIND: {
      printf("[binary expr]op:%d\n", expr->bin_op.op);
      expr_traverse(expr->bin_op.left);
      expr_traverse(expr->bin_op.right);
      break;
    }
    case SEQ_KIND: {
      printf("[seq]\n");
      for (int i = 0; i < Vector_Size(expr->seq); i++) {
        expr_traverse(Vector_Get(expr->seq, i));
      }
      printf("[end seq]\n");
      break;
    }
    default: {
      ASSERT(0);
      break;
    }
  }
}

/*--------------------------------------------------------------------------*/

void vardecl_traverse(struct stmt *stmt)
{
  printf("variable:\n");
  struct var *var = stmt->vardecl.var;
  printf("%s %s ", var->id, var->bconst ? "const":"");
  putchar('\n');

  printf("initializer:\n");
  struct expr *exp = stmt->vardecl.exp;
  if (exp != NULL) {
    expr_traverse(exp);
  } else {
    printf("var is not initialized\n");
  }
}

void stmt_traverse(struct stmt *stmt);

void func_traverse(struct stmt *stmt)
{
  Vector *vec = stmt->funcdecl.pseq;
  struct var *var;
  printf("[function]\n");
  printf("params:");
  if (vec != NULL) {
    for (int i = 0; i < Vector_Size(vec); i++) {
      var = Vector_Get(vec, i);
      printf(" %s %s", var->id, type_tostring(var->type));
      if (i + 1 != Vector_Size(vec))
        printf(",");
    }
    printf("\n");
  }

  vec = stmt->funcdecl.rseq;
  struct type *t;
  printf("returns:");
  if (vec != NULL) {
    for (int i = 0; i < Vector_Size(vec); i++) {
      t = Vector_Get(vec, i);
      printf(" %s", type_tostring(t));
      if (i + 1 != Vector_Size(vec))
        printf(",");
    }
    printf("\n");
  }

  vec = stmt->funcdecl.body;
  if (vec != NULL) {
    for (int i = 0; i < Vector_Size(vec); i++)
      stmt_traverse(Vector_Get(vec, i));
  }
  printf("[end function]\n");
}

/*--------------------------------------------------------------------------*/

void stmt_traverse(struct stmt *stmt)
{
  switch (stmt->kind) {
    case IMPORT_KIND: {
      printf("[import]%s:%s\n", stmt->import.id, stmt->import.path);
      break;
    }
    case EXPR_KIND: {
      printf("[expr]\n");
      expr_traverse(stmt->expr);
      printf("[end expr]\n");
      break;
    }
    case VARDECL_KIND: {
      printf("[var decl]\n");
      vardecl_traverse(stmt);
      printf("[end var decl]\n");
      break;
    }
    case FUNCDECL_KIND: {
      printf("[func decl]:%s\n", stmt->funcdecl.id);
      func_traverse(stmt);
      break;
    }
    case ASSIGN_KIND: {
      printf("[assignment list]\n");
      // struct expr *expr;
      // clist_foreach(expr, stmt->v.assign.left_list) {
      //   expr_traverse(expr);
      // }
      //
      // clist_foreach(expr, stmt->v.assign.right_list) {
      //   expr_traverse(expr);
      // }
      break;
    }
    case COMPOUND_ASSIGN_KIND: {
      printf("[compound assignment]:%d\n", stmt->compound_assign.op);
      // expr_traverse(stmt->v.compound_assign.left);
      // expr_traverse(stmt->v.compound_assign.right);
      break;
    }
    case CLASS_KIND: {
      printf("[structure]:%s\n", stmt->structure.id);
      break;
    }
    case INTF_KIND: {
      printf("[interface]:%s\n", stmt->structure.id);
      break;
    }
    case IF_KIND: {
      printf("[if statement]\n");
      // ifstmt_traverse(stmt);
      break;
    }
    case WHILE_KIND: {
      printf("[while statement]\n");
      // whilestmt_traverse(stmt);
      break;
    }
    case SWITCH_KIND: {
      printf("[switch statement]\n");
      // switchstmt_traverse(stmt);
      break;
    }
    case FOR_TRIPLE_KIND: {
      printf("[for triple statement]\n");
      break;
    }
    case FOR_EACH_KIND: {
      printf("[for each statement]\n");
      break;
    }
    case BREAK_KIND: {
      printf("[break statement]\n");
      break;
    }
    case CONTINUE_KIND: {
      printf("[continue statement]\n");
      break;
    }
    case RETURN_KIND: {
      printf("[return statement]\n");
      struct expr *exp;
      for (int i = 0; i < Vector_Size(stmt->seq); i++) {
        exp = Vector_Get(stmt->seq, i);
        expr_traverse(exp);
      }
      printf("[end return statement]\n");
      break;
    }
    case GO_KIND: {
      printf("[go statement]\n");
      expr_traverse(stmt->go_stmt);
      printf("[end go statement]\n");
      break;
    }
    default:{
      printf("[ERROR] unknown stmt kind :%d\n", stmt->kind);
      ASSERT(0);
    }
  }
}

void ast_traverse(Vector *vec)
{
  for (int i = 0; i < Vector_Size(vec); i++)
    stmt_traverse(Vector_Get(vec, i));
}
