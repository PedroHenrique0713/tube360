// Algumas rotinas AltiVec usam multiplicação/soma inteira que o VMX128 não tem.
#include "config.h"
#undef HAVE_ALTIVEC
#define HAVE_ALTIVEC 0
