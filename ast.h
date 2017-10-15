
#ifndef _KOALA_AST_H_
#define _KOALA_AST_H_

#include "types.h"
#include "list.h"

#ifdef __cplusplus
extern "C" {
#endif

struct clist {
  struct list_head head;
  int count;
};

struct clist *new_clist(void);
void free_clist(struct clist *list);

#define clist_add_tail(link, list) do { \
  list_add_tail((link), &(list)->head); \
  ++(list)->count; \
} while (0)

#define clist_del(pos, list) do { \
  list_del(pos); \
  --(list)->count; \
  assert((list)->count >= 0); \
} while (0)

#define clist_empty(list) \
  (list_empty(&(list)->head) && ((list)->count == 0))

#define clist_foreach(pos, list, member) \
  list_for_each_entry(pos, &(list)->head, member)

#define clist_foreach_safe(pos, temp, list, member) \
  list_for_each_entry_safe(pos, temp, &(list)->head, member)

/*-------------------------------------------------------------------------*/

enum type_kind {
  PRIMITIVE_KIND = 1, USERDEF_TYPE = 2, FUNCTION_TYPE = 3,
};

struct type {
  enum type_kind kind;
  union {
    int primitive;
    struct {
      char *mod_name;
      char *type_name;
    } userdef;
  } v;
  int dims;
};

struct type *type_from_primitive(int primitive);
struct type *type_from_userdef(char *mod_name, char *type_name);
#define type_set_dims(type, dims_val) ((type)->dims = (dims_val))
#define type_inc_dims(type, dims_val) ((type)->dims += (dims_val))

struct array_tail {
  struct list_head link;
  /* one of pointers is null, and another is not null */
  struct clist *list;
  struct expr *expr;
};

#define array_tail_foreach(pos, list) \
  if ((list) != NULL) clist_foreach(pos, list, link)

struct array_tail *array_tail_from_expr(struct expr *expr);
struct array_tail *array_tail_from_list(struct clist *list);

enum atom_kind {
  NAME_KIND = 1, INT_KIND = 2, FLOAT_KIND = 3, STRING_KIND = 4, BOOL_KIND = 5,
  SELF_KIND = 6, NULL_KIND = 7, EXP_KIND = 8, NEW_PRIMITIVE_KIND = 9,
  ARRAY_KIND,
  ATTRIBUTE_KIND, SUBSCRIPT_KIND, CALL_KIND, INTF_IMPL_KIND
};

struct atom {
  enum atom_kind kind;
  union {
    struct {
      char *id;
      int type;
    } name;
    int64_t ival;
    float64_t fval;
    char *str;
    int bval;
    struct expr *exp;
    struct {
      struct type *type;
      /* one of pointers is null, and another is not null */
      struct clist *dim_list;
      struct clist *tail_list;
    } array;
    /* Trailer */
    struct {
      struct atom *atom;
      char *id;
      int type;
    } attribute;
    struct {
      struct atom *atom;
      struct expr *index;
    } subscript;
    struct {
      struct atom *atom;
      struct clist *list;   /* expression list */
    } call;
    struct {
      struct atom *atom;
      struct clist *list;   /* method implementation list */
    } intf_impl;
  } v;
  struct list_head link;
};

#define atom_foreach_safe(pos, head, temp) \
  clist_foreach_safe(pos, temp, head, link)

enum operator_kind {
  ADD = 1, SUB = 2, MULT = 3, DIV = 4, MOD = 5
};

enum expr_kind {
  ATOM_KIND = 1, BINARY_KIND = 2, UNARY_KIND = 3,
};

struct expr {
  enum expr_kind kind;
  union {
    struct atom *atom;
    struct {
      struct expr *left;
      enum operator_kind op;
      struct expr *right;
    } bin_op;
  } v;
  struct list_head link;
};

#define expr_foreach(pos, list) \
  if ((list) != NULL) clist_foreach(pos, list, link)

struct atom *trailer_from_attribute(char *id);
struct atom *trailer_from_subscript(struct expr *idx);
struct atom *trailer_from_call(struct clist *para);
struct atom *atom_from_name(char *id);
struct atom *atom_from_int(int64_t ival);
struct atom *atom_from_float(float64_t fval);
struct atom *atom_from_string(char *str);
struct atom *atom_from_bool(int bval);
struct atom *atom_from_self(void);
struct atom *atom_from_expr(struct expr *exp);
struct atom *atom_from_null(void);
struct atom *atom_from_array(struct type *type, int tail, struct clist *list);

struct expr *expr_from_atom_trailers(struct clist *list, struct atom *atom);
struct expr *expr_from_atom(struct atom *atom);
void expr_traverse(struct expr *exp);

struct var {
  struct list_head link;
  char *id;
};

struct var *new_var(char *id);
#define var_foreach(pos, list) \
  if ((list) != NULL) clist_foreach(pos, list, link)

enum stmt_kind {
  IMPORT_KIND = 1, EXPR_KIND = 2, ASSIGN_KIND = 3,
};

struct stmt {
  enum stmt_kind kind;
  union {
    struct {
      char *alias;
      char *path;
    } import;
    struct expr *expr;
    struct {
      short bdecl;
      short bconst;
      struct type *type;
      struct clist *varlist;
      struct clist *exprlist;
    } assign;
  } v;
  struct list_head link;
};

#define stmt_foreach(stmt, list) \
  if ((list) != NULL) clist_foreach(stmt, list, link)

struct stmt *stmt_from_expr(struct expr *expr);
struct stmt *stmt_from_import(char *alias, char *path);
struct stmt *stmt_from_assign(struct clist *varlist,
                              struct clist *initlist,
                              int bdecl, int bconst, struct type *type);

struct mod {
  struct clist *imports;
  struct clist *stmts;
};

struct mod *new_mod(struct clist *imports, struct clist *stmts);
void mod_traverse(struct mod *mod);

#ifdef __cplusplus
}
#endif
#endif /* _KOALA_AST_H_ */
