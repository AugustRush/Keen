//
//  Keen.h
//  Keen
//
//  Created by pingwei liu on 2018/11/21.
//  Copyright © 2018 pingwei liu. All rights reserved.
//

#ifndef Keen_h
#define Keen_h

#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

typedef struct Keen *KeenRef;
typedef uint8_t BYTE;

KeenRef KeenRefNew(void);
void KeenRefAddItem(KeenRef keen, const char *key, const void *value, int32_t value_length, uint8_t value_type);
const BYTE* KeenRefGetItem(KeenRef keen, const char *key);

#endif /* Keen_h */
