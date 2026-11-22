/* COREDLL memory-DC / bitmap support. One process/DLL instance owns a
 * bounded set of opaque, generation-monotonic handles; no host framebuffer
 * readback, no raw bitmap pointer exposed to the PE. Windows CE COLORREF
 * remains 0x00BBGGRR, while storage/device pixels are SH7305 RGB565.
 * Under current NTOS all COREDLL code is still privileged: this is not
 * a substitute for a future unprivileged syscall/copyin boundary.
 */
#define GDI_SCREEN_DC ((void *)(uintptr_t)1u)
#define GDI_MEMDC_TAG 0x03000000u
#define GDI_BITMAP_TAG 0x05000000u
#define GDI_STOCK_BITMAP ((void *)(uintptr_t)0x04000001u)
#define GDI_SRCCOPY 0x00CC0020u
#define GDI_INVALID_COLOR 0xFFFFFFFFu
#define GDI_MAX_DCS 4u
#define GDI_MAX_BITMAPS 4u

typedef struct {
    u32 handle,bitmap;
} GDI_MEMDC;
typedef struct {
    u32 handle,selected_dc;
    int w,h;
    u16 *pixels;
} GDI_BITMAP;
static GDI_MEMDC gdi_dcs[GDI_MAX_DCS];
static GDI_BITMAP gdi_bitmaps[GDI_MAX_BITMAPS];
static u32 gdi_next_token=1u;
static u32 gdi_new_handle(u32 tag)
{
    if(gdi_next_token>0x00FFFFFFu)return 0u; /* never recycle stale handles */
    return tag | gdi_next_token++;
}
static GDI_MEMDC *gdi_dc(void *handle)
{
    u32 id=(u32)(uintptr_t)handle;
    if((id & 0xFF000000u)!=GDI_MEMDC_TAG)return 0;
    for(unsigned i=0;i<GDI_MAX_DCS;++i)
        if(gdi_dcs[i].handle==id)return &gdi_dcs[i];
    return 0;
}
static GDI_BITMAP *gdi_bitmap(void *handle)
{
    u32 id=(u32)(uintptr_t)handle;
    if((id & 0xFF000000u)!=GDI_BITMAP_TAG)return 0;
    for(unsigned i=0;i<GDI_MAX_BITMAPS;++i)
        if(gdi_bitmaps[i].handle==id)return &gdi_bitmaps[i];
    return 0;
}
static GDI_BITMAP *gdi_selected(void *dc)
{
    GDI_MEMDC *d=gdi_dc(dc);
    return d && d->bitmap ? gdi_bitmap((void *)(uintptr_t)d->bitmap):0;
}
static u32 gdi_from565(u32 v)
{
    const u32 r=(v>>11u)&31u,g=(v>>5u)&63u,b=v&31u;
    return ((b<<3u)|(b>>2u))<<16u |
           ((g<<2u)|(g>>4u))<<8u |
           ((r<<3u)|(r>>2u));
}
static int gdi_device_dc(void *dc)
{
    return dc==GDI_SCREEN_DC;
}
static int gdi_valid_dc(void *dc)
{
    return gdi_device_dc(dc) || gdi_dc(dc)!=0;
}
void *CreateCompatibleDC(void *dc)
{
    if(dc && !gdi_valid_dc(dc))return 0;
    for(unsigned i=0;i<GDI_MAX_DCS;++i)
        if(!gdi_dcs[i].handle) {
            u32 token=gdi_new_handle(GDI_MEMDC_TAG);
            if(!token)return 0;
            gdi_dcs[i].handle=token;
            gdi_dcs[i].bitmap=0u;
            return (void *)(uintptr_t)token;
        }
    return 0;
}
int DeleteDC(void *dc)
{
    GDI_MEMDC *d=gdi_dc(dc);
    if(!d)return 0;
    if(d->bitmap) {
        GDI_BITMAP *b=gdi_bitmap((void *)(uintptr_t)d->bitmap);
        if(b && b->selected_dc==d->handle)b->selected_dc=0u;
    }
    d->bitmap=0u;d->handle=0u;
    return 1;
}
void *CreateCompatibleBitmap(void *dc,int width,int height)
{
    if(!gdi_valid_dc(dc) || width<1 || height<1 ||
       width>320 || height>528)return 0;
    /* 320x528x2 is 337920 bytes; overflow impossible after bounds. */
    const u32 bytes=(u32)width*(u32)height*2u;
    GDI_BITMAP *entry=0;
    for(unsigned i=0;i<GDI_MAX_BITMAPS;++i)
        if(!gdi_bitmaps[i].handle){entry=&gdi_bitmaps[i];break;}
    if(!entry)return 0;
    u16 *pixels=(u16 *)coredll_malloc(bytes);
    if(!pixels)return 0;
    u32 token=gdi_new_handle(GDI_BITMAP_TAG);
    if(!token) {coredll_free(pixels);return 0;}
    for(u32 i=0;i<bytes/2u;++i)pixels[i]=0u;
    entry->handle=token;
    entry->selected_dc=0u;
    entry->w=width;entry->h=height;entry->pixels=pixels;
    return (void *)(uintptr_t)token;
}
static void *gdi_select_bitmap(void *dc,void *object)
{
    GDI_MEMDC *d=gdi_dc(dc);
    if(!d)return 0;
    GDI_BITMAP *new_bitmap=0;
    if(object!=GDI_STOCK_BITMAP) {
        new_bitmap=gdi_bitmap(object);
        if(!new_bitmap ||
           (new_bitmap->selected_dc &&
            new_bitmap->selected_dc!=d->handle))return 0;
    }
    const u32 old=d->bitmap;
    if(old) {
        GDI_BITMAP *prior=gdi_bitmap((void *)(uintptr_t)old);
        if(prior && prior->selected_dc==d->handle)prior->selected_dc=0u;
    }
    d->bitmap=new_bitmap?new_bitmap->handle:0u;
    if(new_bitmap)new_bitmap->selected_dc=d->handle;
    return old ? (void *)(uintptr_t)old:GDI_STOCK_BITMAP;
}
static int gdi_delete_bitmap(void *object)
{
    GDI_BITMAP *b=gdi_bitmap(object);
    if(!b || b->selected_dc)return 0;
    coredll_free(b->pixels);
    b->pixels=0;b->w=0;b->h=0;b->handle=0;b->selected_dc=0;
    return 1;
}
u32 GetPixel(void *dc,int x,int y)
{
    GDI_BITMAP *b=gdi_selected(dc);
    if(!b || x<0 || y<0 || x>=b->w || y>=b->h)
        return GDI_INVALID_COLOR;
    return gdi_from565(b->pixels[(u32)y*(u32)b->w+(u32)x]);
}
static int gdi_fill_bitmap(void *dc,const NT_RECT *rect,u32 color)
{
    GDI_BITMAP *b=gdi_selected(dc);
    if(!b || !rect || rect->right<=rect->left ||
       rect->bottom<=rect->top)return 0;
    const int l=rect->left<0?0:rect->left;
    const int t=rect->top<0?0:rect->top;
    const int r=rect->right>b->w?b->w:rect->right;
    const int bottom=rect->bottom>b->h?b->h:rect->bottom;
    if(r<=l || bottom<=t)return 1; /* a valid clipped no-op */
    for(int y=t;y<bottom;++y)
        for(int x=l;x<r;++x)
            b->pixels[(u32)y*(u32)b->w+(u32)x]=(u16)color;
    return 1;
}
int BitBlt(void *destination,int x,int y,int width,int height,
           void *source,int srcx,int srcy,u32 rop)
{
    GDI_BITMAP *sb=gdi_selected(source);
    GDI_BITMAP *db=gdi_selected(destination);
    const int screen=gdi_device_dc(destination);
    const int dw=screen?320:(db?db->w:0);
    const int dh=screen?528:(db?db->h:0);
    if(!sb || (!db && !screen) || rop!=GDI_SRCCOPY ||
       width<1 || height<1 || width>320 || height>528 ||
       x< -width || y< -height ||
       srcx< -width || srcy< -height ||
       x>dw || y>dh || srcx>sb->w || srcy>sb->h)
        return 0;
    const void **api=ntapi();
    if(screen && (!api || !api[7]))return 0;
    /* If a bitmap is copied onto itself, choose memmove-like direction.
     * No staging buffer or stack-sized framebuffer is ever required. */
    const int same=db && db==sb;
    const int yback=same && y>srcy;
    const int xback=same && y==srcy && x>srcx;
    for(int row=0;row<height;++row) {
        int j=yback?height-1-row:row;
        int sy=srcy+j,dy=y+j;
        if(sy<0 || sy>=sb->h || dy<0 || dy>=dh)continue;
        for(int col=0;col<width;++col) {
            int i=xback?width-1-col:col;
            int sx=srcx+i,dx=x+i;
            if(sx<0 || sx>=sb->w || dx<0 || dx>=dw)continue;
            u16 value=sb->pixels[(u32)sy*(u32)sb->w+(u32)sx];
            if(screen)
                ((void (*)(u32,u32,u32))api[7])((u32)dx,(u32)dy,(u32)value);
            else db->pixels[(u32)dy*(u32)db->w+(u32)dx]=value;
        }
    }
    return 1;
}

/* Integer nearest-neighbour StretchBlt for WinCE GDI, intentionally
 * no bilinear filtering or antialiasing. Uses DDA integer accumulators
 * rather than compiler software division helpers. Only SRCCOPY from
 * a selected memory bitmap is implemented. Different-size self-blit
 * is denied because no staging buffer exists for aliasing.
 */
int StretchBlt(void *dst,int dx,int dy,int dw,int dh,
               void *src,int sx,int sy,int sw,int sh,u32 rop)
{
    GDI_BITMAP *source=gdi_selected(src);
    GDI_BITMAP *target=gdi_selected(dst);
    const int screen=gdi_device_dc(dst);
    const int width=screen?320:(target?target->w:0);
    const int height=screen?528:(target?target->h:0);
    if(!source || (!target && !screen) || rop!=GDI_SRCCOPY ||
       dw<1 || dh<1 || sw<1 || sh<1 ||
       dw>320 || dh>528 || sw>320 || sh>528 ||
       dx< -dw || dy< -dh || sx< -sw || sy< -sh ||
       dx>width || dy>height ||
       sx>source->w || sy>source->h)return 0;
    if(target==source) {
        if(dw!=sw || dh!=sh)return 0;
        return BitBlt(dst,dx,dy,dw,dh,src,sx,sy,rop);
    }
    const void **api=ntapi();
    if(screen && (!api || !api[7]))return 0;
    unsigned row_in_source=0u,remaining_y=0u;
    for(int j=0;j<dh;++j) {
        int to_y=dy+j,from_y=sy+(int)row_in_source;
        unsigned col_in_source=0u,remaining_x=0u;
        for(int k=0;k<dw;++k) {
            int to_x=dx+k,from_x=sx+(int)col_in_source;
            if(to_y>=0 && to_y<height && to_x>=0 && to_x<width &&
               from_y>=0 && from_y<source->h &&
               from_x>=0 && from_x<source->w) {
                u16 pixel=source->pixels[(u32)from_y*(u32)source->w+
                                         (u32)from_x];
                if(screen)
                    ((void (*)(u32,u32,u32))api[7])
                        ((u32)to_x,(u32)to_y,(u32)pixel);
                else
                    target->pixels[(u32)to_y*(u32)target->w+
                                   (u32)to_x]=pixel;
            }
            remaining_x+=(u32)sw;
            while(remaining_x>=(u32)dw) {
                remaining_x-=(u32)dw;++col_in_source;
            }
        }
        remaining_y+=(u32)sh;
        while(remaining_y>=(u32)dh) {
            remaining_y-=(u32)dh;++row_in_source;
        }
    }
    return 1;
}

/* The actual loader calls COREDLL DllMain(DLL_PROCESS_DETACH) before
 * unmapping its P0 image and before disabling the MMU. Do not let an
 * application which forgot DeleteObject leak P1 SDK allocations.
 * This is NOT an IRQ/VBR operation; no MMIO or filesystem is used.
 */
static void gdi_release_all(void)
{
    for(unsigned d=0;d<GDI_MAX_DCS;++d) {
        gdi_dcs[d].bitmap=0u;
        gdi_dcs[d].handle=0u;
    }
    for(unsigned k=0;k<GDI_MAX_BITMAPS;++k) {
        GDI_BITMAP *bmp=&gdi_bitmaps[k];
        if(bmp->pixels)coredll_free(bmp->pixels);
        bmp->pixels=0;bmp->w=0;bmp->h=0;
        bmp->selected_dc=0u;bmp->handle=0u;
    }
}
