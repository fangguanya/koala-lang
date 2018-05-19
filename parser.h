
#ifndef _KOALA_PARSER_H_
#define _KOALA_PARSER_H_

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LINE_MAX_LEN 256
#define TOKEN_MAX_LEN 80

typedef struct line_buf {
	char line[LINE_MAX_LEN];
	int linelen;
	int lineleft;
	char lasttoken[TOKEN_MAX_LEN];
	int lastlen;
	int row;
	int col;
	char print;
	char copy;
} LineBuffer;

/*---------------------------------------------------------------------------*/

#define ARG_NIL   0
#define ARG_INT   1
#define ARG_FLOAT 2
#define ARG_BOOL  3
#define ARG_STR   4

typedef struct argument {
	int kind;
	union {
		int64 ival;
		float64 fval;
		int bval;
		char *str;
	};
} Argument;

typedef struct inst {
	struct list_head link;
	int bytes;
	int argc;
	uint8 op;
	Argument arg;
	int upbytes;  // break and continue statements
} Inst;

#define JMP_BREAK    1
#define JMP_CONTINUE 2

typedef struct jmp_inst {
	Inst *inst;
	int type;
} JmpInst;

typedef struct codeblock {
	//struct list_head link;
	int bytes;
	struct list_head insts;
	struct codeblock *next;  /* control flow */
	int bret;  /* false, no OP_RET, needs add one */
} CodeBlock;

void codeblock_free(CodeBlock *b);

/*---------------------------------------------------------------------------*/

typedef struct import {
	HashNode hnode;
	Line line;
	char *path;
	Symbol *sym;
} Import;

enum scope {
	SCOPE_MODULE = 1,
	SCOPE_CLASS,
	SCOPE_FUNCTION,
	SCOPE_METHOD,
	SCOPE_CLOSURE,
	SCOPE_BLOCK
};

typedef struct parserunit {
	enum scope scope;
	int8 merge;
	int8 loop;
	struct list_head link;
	Symbol *sym;
	STable *stbl;
	CodeBlock *block;
	Vector jmps;
} ParserUnit;

#define MAX_ERRORS 8

typedef struct parserstate {
	char *filename;     /* compile file's name */
	void *scanner;      /* lexer pointer */
	LineBuffer line;    /* input line buffer */
	Vector stmts;       /* all statements */
	char *package;      /* package name */
	HashTable imports;  /* external types */
	STable *extstbl;    /* external symbol table */
	Symbol *sym;        /* current module's symbol */
	ParserUnit *u;
	int nestlevel;
	struct list_head ustack;
	short olevel;       /* optimization level */
	short wlevel;       /* warning level */
	int errnum;         /* number of errors */
	Vector errors;      /* error messages */
} ParserState;

ParserState *new_parser(void *filename);
void destroy_parser(ParserState *ps);
void parser_module(ParserState *ps, FILE *in);
void Parser_PrintError(ParserState *ps, Line *l, char *fmt, ...);

// API used by lex
int Lexer_DoYYInput(ParserState *ps, char *buf, int size, FILE *in);
void Lexer_DoUserAction(ParserState *ps, char *text);
#define Lexer_PrintError(ps, errmsg, ...) do {\
	if (ps->errnum >= MAX_ERRORS) { \
		exit(-1); \
	} \
	LineBuffer *lb = &(ps)->line; \
	if (!lb->print) { \
		Line l = {lb->line, lb->row, lb->col}; \
		Parser_PrintError(ps, &l, errmsg, ##__VA_ARGS__); \
		lb->print = 1; \
	} else { \
		++ps->errnum; \
	} \
} while (0)
#define Lexer_Token parser->line.lasttoken

// API used by yacc
Symbol *Parse_Import(ParserState *ps, char *id, char *path);
void Parse_VarDecls(ParserState *ps, struct stmt *stmt);
void Parse_Function(ParserState *ps, struct stmt *stmt);
void Parse_UserDef(ParserState *ps, struct stmt *stmt);
char *Import_Get_Path(ParserState *ps, char *id);
void Parser_SetLine(ParserState *ps, struct expr *exp);

#ifdef __cplusplus
}
#endif
#endif /* _KOALA_PARSER_H_ */
