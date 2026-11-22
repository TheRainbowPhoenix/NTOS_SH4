/* Portable EX-word 8.3 PE import-name convention. Must match the
 * checked-in tools/package_exword_flat.py FNV-1a naming rule.
 * Returns false on unsupported/incomplete WinCE module names. */
#ifndef NTOS_EXWORD_SHORTNAME_H
#define NTOS_EXWORD_SHORTNAME_H
#ifdef __cplusplus
extern "C" {
#endif
static inline int ntos_exword_shortname(const char *name,char out[13])
{
    if(!name || !out)return 0;
    unsigned length=0u;
    while(name[length] && length<64u)++length;
    if(!length || length>=64u)return 0;
    unsigned dot=0u;
    while(dot<length && name[dot]!='.')++dot;
    if(dot<1u || dot>48u || dot+4u!=length)return 0;
    char ext[4];
    for(unsigned i=0u;i<3u;++i) {
        char x=name[dot+1u+i];
        if(x>='a'&&x<='z')x=(char)(x-'a'+'A');
        ext[i]=x;
    }
    ext[3]=0;
    if(!((ext[0]=='E'&&ext[1]=='X'&&ext[2]=='E') ||
         (ext[0]=='D'&&ext[1]=='L'&&ext[2]=='L') ||
         (ext[0]=='C'&&ext[1]=='P'&&ext[2]=='L')))return 0;
    for(unsigned i=0u;i<dot;++i) {
        char x=name[i];
        if(x>='a'&&x<='z')x=(char)(x-'a'+'A');
        if(!((x>='A'&&x<='Z')||(x>='0'&&x<='9')||x=='_'))
            return 0;
        if(i<8u)out[i]=x;
    }
    /* Prefer recognizable 8.3 names: HELLOCORE -> HELLOCOR.
     * The package builder rejects any ambiguity/collision. */
    unsigned n=dot<8u?dot:8u;
    out[n++]='.';
    for(unsigned i=0u;i<3u;++i)out[n++]=ext[i];
    out[n]=0;
    return 1;
}
#ifdef __cplusplus
}
#endif
#endif
