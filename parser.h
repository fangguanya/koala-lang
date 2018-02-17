
#ifndef _KOALA_PARSER_H_
#define _KOALA_PARSER_H_

#include "hashtable.h"
#include "object.h"
#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct import {
  HashNode hnode;
  char *path;
  Symbol *sym;
} Import;

typedef struct error {
  char *msg;
  int line;
} Error;

typedef struct inst {
  struct list_head link;
  uint8 op;
  TValue arg;
} Inst;

typedef struct codeblock {
  char *name; /* for debugging */
  struct list_head link;
  STable stbl;
  struct list_head insts;
   /* true if a OP_RET opcode is inserted. */
  int bret;
} CodeBlock;

enum {
  SCOPE_MODULE = 1,
  SCOPE_CLASS,
  SCOPE_FUNCTION,
  SCOPE_BLOCK
};

typedef struct parserunit {
  int scope;
  struct list_head link;
  Symbol *sym;
  STable stbl;
  CodeBlock *block;
  struct list_head blocks;
} ParserUnit;

typedef struct parserstate {
  Vector stmts;       /* all statements */
  char *outfile;
  char *package;
  HashTable imports;  /* external types */
  STable extstbl;     /* external symbol table */
  ParserUnit *u;
  int nestlevel;
  struct list_head ustack;
  int olevel;         /* optimization level */
  Vector errors;
} ParserState;

char *userdef_get_path(ParserState *ps, char *mod);
Symbol *parse_import(ParserState *parser, char *id, char *path);
void parse_vardecls(ParserState *parser, struct stmt *stmt);
void parse_funcdecl(ParserState *parser, struct stmt *stmt);
void parse_typedecl(ParserState *parser, struct stmt *stmt);
void parse_body(ParserState *ps, Vector *stmts);

#ifdef __cplusplus
}
#endif
#endif /* _KOALA_PARSER_H_ */
