/* NTOS GWES: process-local logical window policy exported through COREDLL.
 * These are NOT HWNDs or separate PE/KPROCESS instances. Standard WinCE
 * callers keep their existing CreateWindowExW and WNDPROC behavior.
 */
static void gwes_init_if_needed(void) {
    if(!g_nt_gwes_initialized) {
        nt_gwes_init(&g_nt_gwes);
        nt_gwes_sip(&g_nt_gwes,nt_sip_flags&1u);
        g_nt_gwes_initialized=1u;
    }
}
u32 NtosGwesCreate(void) {
    gwes_init_if_needed();return nt_gwes_create(&g_nt_gwes);
}
int NtosGwesClose(u32 id) {
    gwes_init_if_needed();return nt_gwes_close(&g_nt_gwes,id);
}
int NtosGwesSetMode(u32 mode) {
    gwes_init_if_needed();return nt_gwes_setmode(&g_nt_gwes,mode);
}
u32 NtosGwesGetMode(void) {
    gwes_init_if_needed();return g_nt_gwes.mode;
}
int NtosGwesSwap(void) {
    gwes_init_if_needed();return nt_gwes_swap(&g_nt_gwes);
}
int NtosGwesFocus(u32 id) {
    gwes_init_if_needed();return nt_gwes_focus(&g_nt_gwes,id);
}
int NtosGwesGetRect(u32 id,NT_RECT *out) {
    NT_GW_RECT rect;
    gwes_init_if_needed();
    if(!out || !nt_gwes_rect(&g_nt_gwes,id,&rect))return 0;
    out->left=rect.left;out->top=rect.top;
    out->right=rect.right;out->bottom=rect.bottom;
    return 1;
}
u32 NtosGwesHit(int x,int y,u32 *action) {
    unsigned a=0u;
    gwes_init_if_needed();
    u32 result=nt_gwes_hit(&g_nt_gwes,x,y,&a);
    if(action)*action=a;
    return result;
}
int NtosGwesKeyboard(u32 on) {
    if(on>1u)return 0;
    gwes_init_if_needed();
    return SipShowIM(on);
}
int NtosGwesReset(void) {
    gwes_init_if_needed();
    u32 serial=g_nt_gwes.next_id;
    nt_gwes_init(&g_nt_gwes);
    if(serial>1u)g_nt_gwes.next_id=serial;
    nt_gwes_sip(&g_nt_gwes,nt_sip_flags&1u);
    return 1;
}
