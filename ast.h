
#ifndef _KOALA_AST_H_
#define _KOALA_AST_H_

#include "symbol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lineinfo {
	char *line;
	int row;
	int col;
} LineInfo;

/*-------------------------------------------------------------------------*/

typedef enum unary_op_kind {
	UNARY_PLUS = 1, UNARY_MINUS = 2, UNARY_BIT_NOT = 3, UNARY_LNOT = 4
} UnaryOpKind;

typedef enum binary_op_kind {
	BINARY_ADD = 1, BINARY_SUB,
	BINARY_MULT, BINARY_DIV, BINARY_MOD, BINARY_QUOT, BINARY_POWER,
	BINARY_LSHIFT, BINARY_RSHIFT,
	BINARY_GT, BINARY_GE, BINARY_LT, BINARY_LE,
	BINARY_EQ, BINARY_NEQ,
	BINARY_BIT_AND,
	BINARY_BIT_XOR,
	BINARY_BIT_OR,
	BINARY_LAND,
	BINARY_LOR,
} BinaryOpKind;

int binop_arithmetic(int op);
int binop_relation(int op);
int binop_logic(int op);
int binop_bit(int op);

typedef enum expr_kind {
	ID_KIND = 1, INT_KIND = 2, FLOAT_KIND = 3, BOOL_KIND = 4,
	STRING_KIND = 5, SELF_KIND = 6, SUPER_KIND = 7, TYPEOF_KIND = 8,
	NIL_KIND = 9, EXP_KIND = 10, ARRAY_KIND = 11, ANONYOUS_FUNC_KIND = 12,
	ATTRIBUTE_KIND = 13, SUBSCRIPT_KIND = 14, CALL_KIND = 15,
	UNARY_KIND = 16, BINARY_KIND = 17, SEQ_KIND = 18,
	EXPR_KIND_MAX
} ExprKind;

typedef enum expr_ctx {
	EXPR_INVALID = 0, EXPR_LOAD = 1, EXPR_STORE = 2,
	EXPR_CALL_FUNC = 3, EXPR_LOAD_FUNC = 4,
} ExprCtx;

struct expr {
	enum expr_kind kind;
	TypeDesc *desc;
	enum expr_ctx ctx;
	Symbol *sym;
	int argc;
	struct expr *right;  //for super(); super.name;
	char *supername;
	LineInfo line;
	union {
		char *id;
		int64 ival;
		float64 fval;
		char *str;
		int bval;
		struct expr *exp;
		struct {
			/* one of pointers is null, and another is not null */
			Vector *dseq;
			Vector *tseq;
		} array;
		struct {
			Vector *pvec;
			Vector *rvec;
			Vector *body;
		} anonyous_func;
		/* Trailer */
		struct {
			struct expr *left;
			char *id;
		} attribute;
		struct {
			struct expr *left;
			struct expr *index;
		} subscript;
		struct {
			struct expr *left;
			Vector *args;     /* arguments list */
		} call;
		/* arithmetic operation */
		struct {
			enum unary_op_kind op;
			struct expr *operand;
		} unary;
		struct {
			struct expr *left;
			enum binary_op_kind op;
			struct expr *right;
		} binary;
		Vector vec;
	};
};

struct expr *expr_from_trailer(enum expr_kind kind, void *trailer,
															 struct expr *left);
struct expr *expr_from_id(char *id);
struct expr *expr_from_int(int64 ival);
struct expr *expr_from_float(float64 fval);
struct expr *expr_from_string(char *str);
struct expr *expr_from_bool(int bval);
struct expr *expr_from_self(void);
struct expr *expr_from_super(void);
struct expr *expr_from_typeof(void);
struct expr *expr_from_expr(struct expr *exp);
struct expr *expr_from_nil(void);
struct expr *expr_from_array(TypeDesc *desc, Vector *dseq, Vector *tseq);
struct expr *expr_from_array_with_tseq(Vector *tseq);
struct expr *expr_from_anonymous_func(Vector *pvec, Vector *rvec, Vector *body);
struct expr *expr_from_binary(enum binary_op_kind kind,
															struct expr *left, struct expr *right);
struct expr *expr_from_unary(enum unary_op_kind kind, struct expr *expr);
void expr_traverse(struct expr *exp);

/*-------------------------------------------------------------------------*/

struct var {
	char *id;
	short bconst;
	short typealias;
	TypeDesc *desc;
};

struct var *new_var(char *id, TypeDesc *desc);
void free_var(struct var *v);

/*-------------------------------------------------------------------------*/

struct test_block {
	struct expr *test;
	Vector *body;
};

struct test_block *new_test_block(struct expr *test, Vector *body);

enum assign_operator {
	OP_ASSIGN = 1,
	OP_PLUS_ASSIGN, OP_MINUS_ASSIGN,
	OP_MULT_ASSIGN, OP_DIV_ASSIGN,
	OP_MOD_ASSIGN, OP_AND_ASSIGN, OP_OR_ASSIGN, OP_XOR_ASSIGN,
	OP_RSHIFT_ASSIGN, OP_LSHIFT_ASSIGN,
};

enum stmt_kind {
	VARDECL_KIND = 1, FUNCDECL_KIND, FUNCPROTO_KIND, CLASS_KIND,
	TRAIT_KIND, EXPR_KIND, ASSIGN_KIND,
	RETURN_KIND, IF_KIND, WHILE_KIND, SWITCH_KIND, FOR_TRIPLE_KIND,
	FOR_EACH_KIND, BREAK_KIND, CONTINUE_KIND, GO_KIND, BLOCK_KIND,
	VARDECL_LIST_KIND, ASSIGN_LIST_KIND, TYPEALIAS_KIND, STMT_KIND_MAX
};

struct assign {
	struct expr *left;
	struct expr *right;
};

struct stmt {
	enum stmt_kind kind;
	LineInfo line;
	union {
		struct {
			struct var *var;
			struct expr *exp;
		} vardecl;
		struct {
			char *id;
			Vector *pvec;
			Vector *rvec;
			Vector *body;
		} funcdecl;
		struct {
			struct expr *left;
			enum assign_operator op;
			struct expr *right;
		} assign;
		struct {
			char *id;
			TypeDesc *super;
			Vector *traits;
			Vector *body;
		} class_info;
		struct {
			char *id;
			Vector *pvec;
			Vector *rvec;
		} funcproto;
		struct {
			char *id;
			TypeDesc *desc;
		} user_typedef;
		struct {
			char *id;
			TypeDesc *desc;
		} typealias;
		struct {
			int belse;
			struct expr *test;
			Vector *body;
			struct stmt *orelse;
		} if_stmt;
		struct {
			int btest;
			struct expr *test;
			Vector *body;
		} while_stmt;
		struct {
			int level;
		} jump_stmt;
		struct {
			struct expr *expr;
			Vector *case_seq;
		} switch_stmt;
		struct {
			struct stmt *init;
			struct stmt *test;
			struct stmt *incr;
			Vector *body;
		} for_triple_stmt;
		struct {
			int bdecl;
			struct var *var;
			struct expr *expr;
			Vector *body;
		} for_each_stmt;
		struct expr *go_stmt;
		struct expr *exp;
		Vector vec;
	};
};

struct stmt *stmt_new(int kind);
struct stmt *stmt_from_expr(struct expr *exp);
struct stmt *stmt_from_vardecl(struct var *var, struct expr *exp, int bconst);
struct stmt *stmt_from_funcdecl(char *id, Vector *pvec, Vector *rvec,
	Vector *body);
struct stmt *stmt_from_assign(struct expr *left, enum assign_operator op,
	struct expr *right);
struct stmt *stmt_from_block(Vector *vec);
struct stmt *stmt_from_return(Vector *vec);
struct stmt *stmt_from_empty(void);
struct stmt *stmt_from_trait(char *id, Vector *traits, Vector *body);
struct stmt *stmt_from_funcproto(char *id, Vector *pvec, Vector *rvec);
struct stmt *stmt_from_jump(int kind, int level);
struct stmt *stmt_from_if(struct expr *test, Vector *body,
	struct stmt *orelse);
struct stmt *stmt_from_while(struct expr *test, Vector *body, int btest);
struct stmt *stmt_from_switch(struct expr *expr, Vector *case_seq);
struct stmt *stmt_from_for(struct stmt *init, struct stmt *test,
	struct stmt *incr, Vector *body);
struct stmt *stmt_from_foreach(struct var *var, struct expr *expr,
	Vector *body, int bdecl);
struct stmt *stmt_from_go(struct expr *expr);
struct stmt *stmt_from_vardecl_list(Vector *vars, TypeDesc *desc, Vector *exprs,
	int bconst);
struct stmt *stmt_from_typealias(char *id, TypeDesc *desc);
void stmt_free(struct stmt *stmt);
void vec_stmt_free(Vector *stmts);
void vec_stmt_fini(Vector *stmts);
void stmt_free_vardecl_list(struct stmt *stmt);

/*-------------------------------------------------------------------------*/


#ifdef __cplusplus
}
#endif
#endif /* _KOALA_AST_H_ */
