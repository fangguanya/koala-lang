
#ifndef _KOALA_STRUCTOBJECT_H_
#define _KOALA_STRUCTOBJECT_H_

#include "object.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct structobject {
  OBJECT_HEAD
  int size;
  TValue items[0];
} StructObject;

Klass *Struct_Klass_New(const char *name);

#ifdef __cplusplus
}
#endif
#endif /* _KOALA_STRUCTOBJECT_H_ */
