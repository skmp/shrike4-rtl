/* ftrv_specials_vectors.h -- the FTRV special-operand cases of ftrv_dn0_dump.c, shared with the host checker.
 * The matrix is given as ROWS (row i = coefficients of result i); XMTRX is column major: XF[4c + r] = row[r][c]. */
#ifndef FTRV_SPECIALS_VECTORS_H
#define FTRV_SPECIALS_VECTORS_H
#include <stdint.h>
#define ONE 0x3f800000u
#define MONE 0xbf800000u
#define TWO 0x40000000u
#define MAXF 0x7f7fffffu
#define INF 0x7f800000u
#define QNAN 0x7fbfffffu
#define SNAN 0x7fc00000u
#define DEN 0x00000001u
/* matrix given as ROWS for readability: row i = coefficients of result i; stored column major below */
static const struct { const char *name; uint32_t row[4][4]; uint32_t v[4]; } TS[] = {
    { "identity x (1,2,3,1.5)",  {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,TWO,0x40400000u,0x3fc00000u} },
    { "zero matrix",             {{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}},                             {ONE,TWO,0x40400000u,0x3fc00000u} },
    { "inexact rows",            {{0x3eaaaaabu,0,0,0},{0,0x3f800001u,0,0},{ONE,ONE,ONE,ONE},{ONE,MONE,0,0}}, {0x40400000u,0x3f800001u,0x3eaaaaabu,ONE} },
    { "row0 overflow",           {{MAXF,MAXF,0,0},{ONE,0,0,0},{0,0,0,0},{0,0,0,ONE}},                   {TWO,TWO,ONE,ONE} },
    { "row2 underflow",          {{ONE,0,0,0},{0,ONE,0,0},{0,0,0x1f800000u,0},{0,0,0,ONE}},             {ONE,ONE,0x1f800000u,ONE} },
    { "row1 inf",                {{ONE,0,0,0},{INF,0,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,ONE,ONE,ONE} },
    { "row1 inf*0",              {{ONE,0,0,0},{0,INF,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,0,ONE,ONE} },
    { "row3 inf-inf",            {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{INF,INF,0,0}},                   {ONE,MONE,ONE,ONE} },
    { "row0 -inf,-inf quirk",    {{INF,INF,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                   {MONE,MONE,ONE,ONE} },
    { "row2 qnan in matrix",     {{ONE,0,0,0},{0,ONE,0,0},{0,0,QNAN,0},{0,0,0,ONE}},                    {ONE,ONE,ONE,ONE} },
    { "row2 snan in matrix",     {{ONE,0,0,0},{0,ONE,0,0},{0,0,SNAN,0},{0,0,0,ONE}},                    {ONE,ONE,ONE,ONE} },
    { "qnan in vector[1]",       {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,QNAN,ONE,ONE} },
    { "snan in vector[3]",       {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,ONE,ONE,SNAN} },
    { "snan in vector[3], col 3 zero", {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{ONE,0,0,0}},               {ONE,ONE,ONE,SNAN} },
    { "denormal in vector[0]",   {{ONE,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {DEN,ONE,ONE,ONE} },
    { "denormal in matrix",      {{DEN,0,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,ONE}},                     {ONE,ONE,ONE,ONE} },
    { "overflow row0 + snan row3", {{MAXF,MAXF,0,0},{0,ONE,0,0},{0,0,ONE,0},{0,0,0,SNAN}},              {TWO,TWO,ONE,ONE} },
};
#define NT (sizeof TS / sizeof TS[0])
#endif
