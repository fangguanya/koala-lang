
#include "parser.h"
#include "log.h"

/*-------------------------------------------------------------------------*/

struct expr *expr_new(int kind)
{
	struct expr *exp = calloc(1, sizeof(struct expr));
	exp->kind = kind;
	return exp;
}

struct expr *expr_from_id(char *id)
{
	struct expr *expr = expr_new(ID_KIND);
	expr->id = id;
	return expr;
}

struct expr *expr_from_int(int64 ival)
{
	struct expr *expr = expr_new(INT_KIND);
	expr->desc = TypeDesc_From_Primitive(PRIMITIVE_INT);
	expr->ival = ival;
	return expr;
}

struct expr *expr_from_float(float64 fval)
{
	struct expr *expr = expr_new(FLOAT_KIND);
	expr->desc = TypeDesc_From_Primitive(PRIMITIVE_FLOAT);
	expr->fval = fval;
	return expr;
}

struct expr *expr_from_string(char *str)
{
	struct expr *expr = expr_new(STRING_KIND);
	expr->desc = TypeDesc_From_Primitive(PRIMITIVE_STRING);
	expr->str = str;
	return expr;
}

struct expr *expr_from_bool(int bval)
{
	struct expr *expr = expr_new(BOOL_KIND);
	expr->desc = TypeDesc_From_Primitive(PRIMITIVE_BOOL);
	expr->bval = bval;
	return expr;
}

struct expr *expr_from_self(void)
{
	struct expr *expr = expr_new(SELF_KIND);
	expr->desc = NULL;
	return expr;
}

struct expr *expr_from_super(void)
{
	struct expr *expr = expr_new(SUPER_KIND);
	expr->desc = NULL;
	return expr;
}

struct expr *expr_from_typeof(void)
{
	struct expr *expr = expr_new(TYPEOF_KIND);
	expr->desc = NULL;
	return expr;
}

struct expr *expr_from_expr(struct expr *exp)
{
	struct expr *expr = expr_new(EXP_KIND);
	expr->desc = NULL;
	expr->exp  = exp;
	return expr;
}

struct expr *expr_from_nil(void)
{
	struct expr *expr = expr_new(NIL_KIND);
	expr->desc = NULL;
	return expr;
}

struct expr *expr_from_array(TypeDesc *desc, Vector *dseq, Vector *tseq)
{
	struct expr *expr = expr_new(ARRAY_KIND);
	expr->desc = desc;
	expr->array.dseq = dseq;
	expr->array.tseq = tseq;
	return expr;
}

struct expr *expr_from_array_with_tseq(Vector *tseq)
{
	struct expr *e = expr_new(SEQ_KIND);
	e->vec = tseq;
	return e;
}

struct expr *expr_from_anonymous_func(Vector *pvec, Vector *rvec, Vector *body)
{
	struct expr *expr = expr_new(ANONYOUS_FUNC_KIND);
	expr->anonyous_func.pvec = pvec;
	expr->anonyous_func.rvec = rvec;
	expr->anonyous_func.body = body;
	return expr;
}

struct expr *expr_from_trailer(enum expr_kind kind, void *trailer,
															 struct expr *left)
{
	struct expr *expr = expr_new(kind);
	switch (kind) {
		case ATTRIBUTE_KIND: {
			expr->ctx = EXPR_LOAD;
			expr->attribute.left = left;
			expr->attribute.id = trailer;
			break;
		}
		case SUBSCRIPT_KIND: {
			expr->subscript.left = left;
			expr->subscript.index = trailer;
			break;
		}
		case CALL_KIND: {
			expr->call.left = left;
			expr->call.args = trailer;
			break;
		}
		default: {
			assertm(0, "unkown expression kind %d\n", kind);
		}
	}
	return expr;
}

struct expr *expr_from_binary(enum binary_op_kind kind,
															struct expr *left, struct expr *right)
{
	struct expr *exp = expr_new(BINARY_KIND);
	exp->desc = left->desc;
	exp->binary.left = left;
	exp->binary.op = kind;
	exp->binary.right = right;
	return exp;
}

struct expr *expr_from_unary(enum unary_op_kind kind, struct expr *expr)
{
	struct expr *exp = expr_new(UNARY_KIND);
	exp->desc = expr->desc;
	exp->unary.op = kind;
	exp->unary.operand = expr;
	return exp;
}

/*--------------------------------------------------------------------------*/

struct stmt *stmt_new(int kind)
{
	struct stmt *stmt = calloc(1, sizeof(struct stmt));
	stmt->kind = kind;
	return stmt;
}

void stmt_free(struct stmt *stmt)
{
	free(stmt);
}

struct stmt *stmt_from_vector(int kind, Vector *vec)
{
	struct stmt *stmt = stmt_new(kind);
	stmt->vec = vec;
	return stmt;
}

struct stmt *stmt_from_expr(struct expr *exp)
{
	struct stmt *stmt = stmt_new(EXPR_KIND);
	stmt->exp = exp;
	return stmt;
}

struct stmt *stmt_from_import(char *id, char *path)
{
	struct stmt *stmt = stmt_new(IMPORT_KIND);
	stmt->import.id = id;
	stmt->import.path = path;
	return stmt;
}

struct stmt *stmt_from_vardecl(struct var *var, struct expr *exp, int bconst)
{
	struct stmt *stmt = stmt_new(VARDECL_KIND);
	var->bconst = bconst;
	stmt->vardecl.var = var;
	stmt->vardecl.exp = exp;
	return stmt;
}

struct stmt *stmt_from_varlistdecl(Vector *vars, Vector *exps,
	TypeDesc *desc, int bconst)
{
	int vsz = Vector_Size(vars);
	int esz = Vector_Size(exps);
	if (esz != 0 && esz != vsz) {
		error("cannot assign %d values to %d variables", esz, vsz);
		return NULL;
	}

	struct var *var;
	struct stmt *stmt;
	struct expr *rexp;
	Vector *vec = NULL;
	for (int i = 0; i < vsz; i++) {
		var = Vector_Get(vars, i);
		var->bconst = bconst;
		var->desc = desc;
		stmt = stmt_new(VARDECL_KIND);
		stmt->vardecl.var = var;

		if (esz > 0) {
			rexp = Vector_Get(exps, i);
			stmt->vardecl.exp = rexp;

			if (var->desc && rexp->desc) {
				if (!TypeDesc_Check(var->desc, rexp->desc)) {
					error("type check failed");
					vec_stmt_free(vec);
					return NULL;
				}
			}

			if (!var->desc) var->desc = rexp->desc;
		}

		if (!var->desc) warn("'%s' type isnot set", var->id);

		if (vsz > 1) {
			if (vec) Vector_Append(vec, stmt);
			else vec = Vector_New();
		}
	}
	if (vsz > 1) stmt = stmt_from_vector(VARDECL_LIST_KIND, vec);

	return stmt;
}

struct stmt *stmt_from_funcdecl(char *id, Vector *pvec, Vector *rvec,
																Vector *body)
{
	struct stmt *stmt = stmt_new(FUNCDECL_KIND);
	stmt->funcdecl.id = id;
	stmt->funcdecl.pvec = pvec;
	stmt->funcdecl.rvec = rvec;
	stmt->funcdecl.body = body;
	return stmt;
}

struct stmt *stmt_from_assign(Vector *left, Vector *right)
{
	int vsz = Vector_Size(left);
	int esz = Vector_Size(right);
	if (esz != vsz) {
		error("cannot assign %d values to %d variables", esz, vsz);
		return NULL;
	}

	struct expr *lexp;
	struct expr *rexp;
	struct stmt *stmt;
	Vector *vec = NULL;
	for (int i = 0; i < vsz; i++) {
		lexp = Vector_Get(left, i);
		rexp = Vector_Get(right, i);
		if ((lexp->desc != NULL) && (rexp->desc != NULL)) {
			if (!TypeDesc_Check(lexp->desc, rexp->desc)) {
				error("type check failed");
				vec_stmt_free(vec);
				return NULL;
			}
		}
		stmt = stmt_new(ASSIGN_KIND);
		stmt->assign.left  = lexp;
		stmt->assign.right = rexp;
		if (vsz > 1) {
			if (vec != NULL) Vector_Append(vec, stmt);
			else vec = Vector_New();
		}
	}

	if (vsz > 1) stmt = stmt_from_vector(ASSIGN_LIST_KIND, vec);

	return stmt;
}

struct stmt *stmt_from_block(Vector *block)
{
	return stmt_from_vector(BLOCK_KIND, block);
}

struct stmt *stmt_from_return(Vector *vec)
{
	return stmt_from_vector(RETURN_KIND, vec);
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

struct stmt *stmt_from_trait(char *id, Vector *traits, Vector *body)
{
	struct stmt *stmt = stmt_new(TRAIT_KIND);
	stmt->class_info.id = id;
	stmt->class_info.traits = traits;
	stmt->class_info.body = body;
	return stmt;
}

struct stmt *stmt_from_funcproto(char *id, Vector *pvec, Vector *rvec)
{
	struct stmt *stmt = stmt_new(FUNCPROTO_KIND);
	stmt->funcproto.id = id;
	stmt->funcproto.pvec = pvec;
	stmt->funcproto.rvec = rvec;
	return stmt;
}

struct stmt *stmt_from_jump(int kind, int level)
{
	if (level <= 0) {
		error("break or continue level must be > 0");
		return NULL;
	}
	struct stmt *stmt = stmt_new(kind);
	stmt->jump_stmt.level = level;
	return stmt;
}

struct stmt *stmt_from_if(struct expr *test, Vector *body,
	struct stmt *orelse)
{
	struct stmt *stmt = stmt_new(IF_KIND);
	stmt->if_stmt.test = test;
	stmt->if_stmt.body = body;
	stmt->if_stmt.orelse = orelse;
	return stmt;
}

struct test_block *new_test_block(struct expr *test, Vector *body)
{
	struct test_block *tb = malloc(sizeof(struct test_block));
	tb->test = test;
	tb->body = body;
	return tb;
}

struct stmt *stmt_from_while(struct expr *test, Vector *body, int btest)
{
	struct stmt *stmt = stmt_new(WHILE_KIND);
	stmt->while_stmt.btest = btest;
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
		assertm(0, "syntax error:not a func call\n");
		exit(0);
	}

	struct stmt *stmt = stmt_new(GO_KIND);
	stmt->go_stmt = expr;
	return stmt;
}

/*--------------------------------------------------------------------------*/

int binop_arithmetic(int op)
{
	if (op >= BINARY_ADD && op <= BINARY_RSHIFT)
		return 1;
	else
		return 0;
}

int binop_relation(int op)
{
	if (op >= BINARY_GT && op <= BINARY_NEQ)
		return 1;
	else
		return 0;
}

int binop_logic(int op)
{
	if (op == BINARY_LAND || op == BINARY_LOR)
		return 1;
	else
		return 0;
}

int binop_bit(int op)
{
	if (op == BINARY_BIT_AND || op == BINARY_BIT_XOR || op == BINARY_BIT_OR)
		return 1;
	else
		return 0;
}

/*--------------------------------------------------------------------------*/

struct var *new_var(char *id, TypeDesc *desc)
{
	struct var *v = malloc(sizeof(struct var));
	v->id = id;
	v->bconst = 0;
	v->desc = desc;
	return v;
}

void free_var(struct var *v)
{
	free(v);
}

/*--------------------------------------------------------------------------*/

static void item_stmt_free(void *item, void *arg)
{
	UNUSED_PARAMETER(arg);
	stmt_free(item);
}

void vec_stmt_free(Vector *stmts)
{
	if (!stmts) return;
	Vector_Free(stmts, item_stmt_free, NULL);
}

void vec_stmt_fini(Vector *stmts)
{
	if (!stmts) return;
	Vector_Fini(stmts, item_stmt_free, NULL);
}

/*--------------------------------------------------------------------------*/

void type_traverse(TypeDesc *desc)
{
	if (desc != NULL) {
		printf("type kind & dims: %d:%d\n", desc->kind, desc->dims);
	} else {
		printf("no type declared\n");
	}
}

void array_traverse(struct expr *expr)
{
	type_traverse(expr->desc);
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
		case ID_KIND: {
			printf("[id]%s\n", expr->id);
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
			Vector *vec = expr->call.args;
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
			printf("[unary expr]op:%d\n", expr->unary.op);
			expr_traverse(expr->unary.operand);
			break;
		}
		case BINARY_KIND: {
			printf("[binary expr]op:%d\n", expr->binary.op);
			expr_traverse(expr->binary.left);
			expr_traverse(expr->binary.right);
			break;
		}
		case SEQ_KIND: {
			printf("[vec]\n");
			for (int i = 0; i < Vector_Size(expr->vec); i++) {
				expr_traverse(Vector_Get(expr->vec, i));
			}
			printf("[end vec]\n");
			break;
		}
		default: {
			assert(0);
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
	Vector *vec = stmt->funcdecl.pvec;
	struct var *var;
	printf("[function]:%s\n", stmt->funcdecl.id);
	printf("  params:");
	char *typestr;
	if (vec != NULL) {
		for (int i = 0; i < Vector_Size(vec); i++) {
			var = Vector_Get(vec, i);
			typestr = TypeDesc_ToString(var->desc);
			printf("  %s %s", var->id, typestr);
			free(typestr);
			if (i + 1 != Vector_Size(vec))
				printf(",");
		}
		printf("\n");
	}

	vec = stmt->funcdecl.rvec;
	printf("  returns:\n");
	if (vec != NULL) {
		TypeDesc *d;
		Vector_ForEach(d, vec) {
			d = Vector_Get(vec, i);
			printf("  %s", TypeDesc_ToString(d));
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

void class_or_trait_traverse(struct stmt *stmt)
{
	Vector *body = stmt->class_info.body;
	struct var *var;
	char *typestr;
	printf("[class(trait)]:%s\n", stmt->class_info.id);
	if (body != NULL) {
		struct stmt *s;
		for (int i = 0; i < Vector_Size(body); i++) {
			s = Vector_Get(body, i);
			if (s->kind == VARDECL_KIND) {
				var = s->vardecl.var;
				typestr = TypeDesc_ToString(var->desc);
				printf("  %s %s\n", var->id, typestr);
				free(typestr);
			} else if (s->kind == FUNCDECL_KIND) {
				printf("method: %s\n", s->funcdecl.id);
			} else if (s->kind == FUNCPROTO_KIND) {
				printf("abstract method: %s\n", s->funcproto.id);
			} else {
				assert(0);
			}
		}
	}
	printf("[end class(trait)]\n");
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
			expr_traverse(stmt->exp);
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
		case CLASS_KIND:
		case TRAIT_KIND: {
			class_or_trait_traverse(stmt);
			break;
		}
		case FUNCPROTO_KIND: {
			printf("[funcprotot]:%s\n", stmt->funcproto.id);
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
			for (int i = 0; i < Vector_Size(stmt->vec); i++) {
				exp = Vector_Get(stmt->vec, i);
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
		case VARDECL_LIST_KIND: {
			printf("var decl list\n");
			break;
		}
		default:{
			printf("[ERROR] unknown stmt kind :%d\n", stmt->kind);
			assert(0);
		}
	}
}

void ast_traverse(Vector *vec)
{
	struct stmt *s;
	Vector_ForEach(s, vec)
		stmt_traverse(s);
}
