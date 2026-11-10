#ifndef SHIM_CTYPE_H
#define SHIM_CTYPE_H
static __inline int isdigit(int c) { return c >= '0' && c <= '9'; }
static __inline int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
static __inline int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static __inline int isupper(int c) { return c >= 'A' && c <= 'Z'; }
static __inline int tolower(int c) { return isupper(c) ? c + 32 : c; }
static __inline int abs(int v) { return v < 0 ? -v : v; }
#endif
