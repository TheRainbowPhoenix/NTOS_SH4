/* Original light TouchKB renderer, same geometry as NTOS phone
 * fingerkb. WinCE COREDLL entry: SipShowIM and GetMessageW/WM_CHAR.
 * Uses only integer nearest-neighbour UGUI glyph pixels; no DWM,
 * antialiasing, external keyboard DLL or controller/MMIO access.
 */
static void nt_sip_key(int l,int t,int r,const char *label,int modifier)
{
    ntos_fillrect((u32)l,(u32)t,(u32)r,(u32)(t+47),
        modifier?0x00E4E4E4u:0x00FFFFFFu);
    if(!label)return;
    u16 word[9];
    unsigned n=0u;
    while(label[n] && n<8u) {
        word[n]=(u16)(unsigned char)label[n];++n;
    }
    word[n]=0u;
    if(n)TextOutW((void *)(uintptr_t)1u,l+5,t+15,word,(int)n);
}
static void nt_sip_paint(void)
{
    if(!(nt_sip_flags&1u))return;
    const void **a=ntapi();
    if(!a || !a[9] || !a[7])return;
    void *dc=(void *)(uintptr_t)1u;
    const int old_h=g_font_height;
    const u32 old_ink=SetTextColor(dc,0x00181818u);
    const int old_mode=SetBkMode(dc,1);
    g_font_height=14;
    ntos_fillrect(0u,271u,320u,528u,0x00F2F2F2u);
    ntos_fillrect(0u,271u,320u,273u,0x00A0A0A0u);
    const char *first=g_nt_keyboard.numbers?"1234567890":"qwertyuiop";
    const char *second=g_nt_keyboard.numbers?"@#$%&*()-":"asdfghjkl";
    const char *third=g_nt_keyboard.numbers?";:/?+=_":"zxcvbnm";
    for(int k=0;k<10;++k) {
        char label[2]={first[k],0};
        if(!g_nt_keyboard.numbers && (g_nt_keyboard.shift^g_nt_keyboard.caps) &&
           label[0]>='a' && label[0]<='z')label[0]-=32;
        nt_sip_key(k*32+2,317,k*32+30,label,0);
    }
    for(int k=0;k<9;++k) {
        char label[2]={second[k],0};
        if(!g_nt_keyboard.numbers && (g_nt_keyboard.shift^g_nt_keyboard.caps) &&
           label[0]>='a' && label[0]<='z')label[0]-=32;
        nt_sip_key(18+k*32,370,46+k*32,label,0);
    }
    nt_sip_key(2,422,47,g_nt_keyboard.numbers?"#+":"SH",
        g_nt_keyboard.shift || g_nt_keyboard.numbers);
    for(int k=0;k<7;++k) {
        char label[2]={third[k],0};
        if(!g_nt_keyboard.numbers && (g_nt_keyboard.shift^g_nt_keyboard.caps) &&
           label[0]>='a' && label[0]<='z')label[0]-=32;
        nt_sip_key(50+k*32,422,78+k*32,label,0);
    }
    nt_sip_key(276,422,318,"BS",1);
    nt_sip_key(2,475,47,g_nt_keyboard.numbers?"abc":"&123",1);
    nt_sip_key(51,475,79,",",0);
    nt_sip_key(83,475,241,"SPACE",0);
    nt_sip_key(244,475,272,".",0);
    nt_sip_key(276,475,318,"GO",1);
    g_font_height=old_h;
    SetTextColor(dc,old_ink);
    SetBkMode(dc,old_mode);
}
