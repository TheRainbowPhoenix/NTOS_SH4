#include "../src/nt/gwes_compositor.hpp"
#include <cassert>
#include <cstdint>
#include <cstring>

using nt::GwesCompositor;
struct Canvas {
    uint16_t pixel[528][320]{};
    unsigned rows=0,pixels=0;
    int min_y=528,max_y=0;
};
static void writer(void *context,int x,int y,const uint16_t *p,unsigned n) {
    auto *c=static_cast<Canvas *>(context);
    assert(x>=0 && y>=24 && x+n<=320u && y<528);
    ++c->rows;c->pixels+=n;
    if(y<c->min_y)c->min_y=y;
    if(y>c->max_y)c->max_y=y;
    for(unsigned k=0;k<n;++k)c->pixel[y][x+k]=p[k];
}
int main() {
    GwesCompositor<> g;
    static uint16_t a[528][320],b[528][320],c[528][320];
    for(unsigned y=0;y<528;++y)for(unsigned x=0;x<320;++x) {
        a[y][x]=0xf800u;b[y][x]=0x001fu;c[y][x]=0x07e0u;
    }
    assert(!g.create(0,22,&a[0][0],320,528,320));
    assert(!g.create(11,0,&a[0][0],320,528,320));
    assert(!g.create(11,22,nullptr,320,528,320));
    assert(!g.create(11,22,&a[0][0],321,528,321));
    auto h1=g.create(11,22,&a[0][0],320,528,320);
    auto h2=g.create(12,23,&b[0][0],320,528,320);
    assert(h1 && h2 && h1!=h2);
    assert(!g.create(13,24,&c[0][0],320,528,320));
    assert(g.owns(11,h1) && !g.owns(12,h1));
    assert(!g.publish(12,h1,&c[0][0],320,528,320));
    assert(!g.close(12,h1));
    assert(!g.focus(11,h2));
    assert(g.focus(12,h2));
    Canvas screen{};
    assert(g.compose(writer,&screen)==504u);
    assert(screen.pixel[24][0]==0x001fu);
    assert(screen.pixel[527][319]==0x001fu);
    assert(g.compose(writer,&screen)==0);
    assert(g.set_layout(1u));
    assert(g.rect(h1,nullptr)==false);
    nt::GwesArea one{},two{};
    assert(g.rect(h1,&one) && g.rect(h2,&two));
    assert(one.top==24 && one.bottom==276 && two.top==276);
    assert(g.hit(10,24)==h1 && g.hit(10,275)==h1 &&
           g.hit(10,276)==h2 && g.hit(10,528)==0);
    assert(g.compose(writer,&screen)==504u);
    assert(screen.pixel[30][10]==0xf800u);
    assert(screen.pixel[300][10]==0x001fu);
    g.swap();
    assert(g.hit(10,275)==h2 && g.hit(10,276)==h1);
    assert(g.compose(writer,&screen)==504u);
    assert(screen.pixel[30][10]==0x001fu);
    assert(screen.pixel[300][10]==0xf800u);
    assert(g.set_layout(2u));
    assert(g.compose(writer,&screen)==1008u);
    assert(screen.pixel[100][0]==0x001fu);
    assert(screen.pixel[100][319]==0xf800u);
    g.keyboard(true);
    assert(g.bottom()==271);
    assert(g.hit(0,300)==0u);
    assert(g.compose(writer,&screen)==494u);
    assert(screen.pixel[270][0]==0x001fu);
    assert(!g.publish(12,h2,&b[0][0],320,528,319));
    assert(g.publish(12,h2,&c[0][0],320,528,320));
    assert(g.compose(writer,&screen)==494u);
    assert(screen.pixel[100][0]==0x07e0u);
    assert(g.close(12,h2));
    assert(!g.owns(12,h2) && !g.find(h2) && !g.set_layout(2u));
    assert(g.count()==1u);
    assert(g.hit(10,100)==h1);
    assert(g.compose(writer,&screen)==247u);
    assert(screen.pixel[100][10]==0xf800u);
    g.close_pid(11);
    assert(!g.count());
    assert(g.compose(writer,&screen)==0);
    auto reused=g.create(77,88,&c[0][0],320,528,320);
    assert(reused!=h1 && reused!=h2);
    assert(!g.focus(11,reused));
    assert(g.owns(77,reused));
    return 0;
}
