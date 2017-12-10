
#ifndef _KOALA_TABLEOBJECT_H_
#define _KOALA_TABLEOBJECT_H_

#include "hash_table.h"
#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tableobject {
  OBJECT_HEAD
  struct hash_table table;
} TableObject;

/* Exported symbols */
extern Klass Table_Klass;
void Init_Table_Klass(void);
Object *Table_New(void);
int Table_Get(Object *ob, TValue key, TValue *rk, TValue *rv);
int Table_Put(Object *ob, TValue key, TValue value);
typedef void (*Table_Visit)(TValue key, TValue val, void *arg);
void Table_Traverse(Object *ob, Table_Visit visit, void *arg);

#ifdef __cplusplus
}
#endif
#endif /* _KOALA_TABLEOBJECT_H_ */
