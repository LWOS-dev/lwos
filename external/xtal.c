/* 本 C 转换文件由 AI 生成。
 * CPU port of 04.crystal cluster.glsl, scene2: 72 dispersive crystals.
 * All arithmetic is float. sin/cos, sqrt and the shader's powers use LUTs.
 * Geometry/planes are built once, rather than once per ray as in GLSL.
 */
#ifndef XTAL_HOST_TEST
#include "abi.h"
#include "resman.h"
PVOID *lw_abi_base;
#endif
#include "xtal_lut.h"

#define XTAL_COUNT 72
#define XTAL_PLANES 13
#define XTAL_EPS 0.001f
#define XTAL_TAU 6.28318530718f
#define XTAL_IOR 1.52f
#define XTAL_DISPERSION 0.15f

typedef struct { float x, y, z; } V3;
typedef struct { V3 n; float offset, inv_nn; } Plane;
typedef struct {
    V3 pos, axis, color;
    float height, radius2;
    Plane planes[XTAL_PLANES];
} Crystal;
typedef struct { float t; V3 p, reflected, color; } Hit;
typedef struct { V3 origin, direction; float value, body, inner; int crossed; } Channel;
static Crystal crystals[XTAL_COUNT];
static V3 light1, light2;

static inline float minf(float a, float b) { return a < b ? a : b; }
static inline float maxf(float a, float b) { return a > b ? a : b; }
static inline float absf(float x) { return x < 0.0f ? -x : x; }
static inline float clampf(float x, float a, float b) { return minf(maxf(x, a), b); }
static inline float mixf(float a, float b, float t) { return a + (b-a)*t; }
static inline float floorf_local(float x) {
    int i = (int)x;
    return (float)(i - ((float)i > x));
}
static inline float fractf(float x) { return x - floorf_local(x); }
static float lut_sample(const float *table, float index, int last) {
    int i = (int)index;
    if (i <= 0 && index <= 0.0f) return table[0];
    if (i >= last) return table[last];
    return mixf(table[i], table[i+1], index-(float)i);
}
static float sin_lut(float x) {
    return lut_sample(xtal_sin_lut, fractf(x/XTAL_TAU)*1024.0f, 1024);
}
static float cos_lut(float x) { return sin_lut(x + XTAL_TAU*0.25f); }
static float sqrt_lut(float x) {
    if (x <= 0.0f) return 0.0f;
    /* x = m * 2^(2e), m in [1,4). Avoid poor resolution near zero. */
    union { float f; unsigned int u; } bits = { .f = x };
    int exponent = (int)((bits.u >> 23) & 255u) - 127;
    float scale = 1.0f;
    if (exponent == -127) {
        bits.f = x * 16777216.0f;
        exponent = (int)((bits.u >> 23) & 255u) - 127;
        scale = 0.000244140625f;
    }
    int odd = exponent & 1;
    bits.u = (bits.u & 0x7fffffu) | ((unsigned int)(127+odd) << 23);
    float mantissa = bits.f;
    int half = (exponent-odd)/2;
    bits.u = (unsigned int)(half+127) << 23;
    return lut_sample(xtal_sqrt_lut, (mantissa-1.0f)*256.0f, 768)*bits.f*scale;
}
static float power_lut(float x, int n) {
    const float *table;
    switch (n) {
        case 2: table=xtal_pow2_lut; break;
        case 3: table=xtal_pow3_lut; break;
        case 5: table=xtal_pow5_lut; break;
        case 48: table=xtal_pow48_lut; break;
        case 52: table=xtal_pow52_lut; break;
        case 76: table=xtal_pow76_lut; break;
        case 96: table=xtal_pow96_lut; break;
        default: table=xtal_pow128_lut; break;
    }
    return lut_sample(table, clampf(x,0.0f,1.0f)*1024.0f, 1024);
}
static inline V3 vec(float x, float y, float z) { return (V3){x,y,z}; }
static inline V3 add(V3 a, V3 b) { return vec(a.x+b.x,a.y+b.y,a.z+b.z); }
static inline V3 sub(V3 a, V3 b) { return vec(a.x-b.x,a.y-b.y,a.z-b.z); }
static inline V3 mul(V3 a, float s) { return vec(a.x*s,a.y*s,a.z*s); }
static inline float dot(V3 a, V3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
static V3 cross(V3 a, V3 b) {
    return vec(a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x);
}
static V3 normalize(V3 a) {
    float d = dot(a,a);
    return d > 0.0f ? mul(a,1.0f/sqrt_lut(d)) : vec(0.0f,0.0f,0.0f);
}
static V3 mixv(V3 a, V3 b, float t) { return add(mul(a,1.0f-t),mul(b,t)); }
static V3 clampv(V3 a) {
    return vec(clampf(a.x,0.0f,1.0f),clampf(a.y,0.0f,1.0f),clampf(a.z,0.0f,1.0f));
}
static V3 refractv(V3 d, V3 n, float eta) {
    float nd=dot(n,d), k=1.0f-eta*eta*(1.0f-nd*nd);
    if (k < 0.0f) return vec(0.0f,0.0f,0.0f);
    return sub(mul(d,eta),mul(n,eta*nd+sqrt_lut(k)));
}
static void set_plane(Crystal *c, int i, V3 a, V3 b, V3 v) {
    V3 n=cross(sub(b,a),sub(v,a));
    float nn=dot(n,n);
    c->planes[i]=(Plane){n,dot(a,n),nn >= 1.0e-12f ? 1.0f/nn : 0.0f};
}
static void build_crystal(Crystal *c, V3 pos, V3 up, float rot,
                          float sz, float len, V3 color) {
    V3 z=normalize(up), x, y;
    float co=cos_lut(rot), si=sin_lut(rot), m=sz*0.577350269189626f;
    if (z.z > -0.999999f) {
        float k=1.0f/(1.0f+z.z), a=z.x*co+z.y*si, b=z.y*co-z.x*si;
        x=mul(vec(co-z.x*k*a,si-z.y*k*a,-a),sz);
        y=mul(vec(-si-z.x*k*b,co-z.y*k*b,-b),m);
    } else {
        x=mul(vec(co,-si,0.0f),sz);
        y=mul(vec(-si,-co,0.0f),m);
    }
    V3 p=add(pos,mul(z,len));
    V3 d=add(p,mul(z,sz*0.211722519f));
    V3 e=add(p,mul(z,sz*1.058612594f));
    V3 offsets[6]={sub(x,y),add(x,y),mul(y,2.0f),sub(y,x),mul(add(x,y),-1.0f),mul(y,-2.0f)};
    V3 b[6], v[6];
    for (int i=0;i<6;i++) { b[i]=add(pos,offsets[i]); v[i]=add(p,offsets[i]); }
    V3 d0=add(d,add(mul(x,5.0f/6.0f),mul(y,7.0f/6.0f)));
    V3 d1=add(d,add(mul(x,1.0f/6.0f),mul(y,11.0f/6.0f)));
    V3 d2=add(d,add(mul(x,-1.0f),mul(y,2.0f/3.0f)));
    V3 d3=sub(sub(d,x),mul(y,2.0f/3.0f));
    V3 d4=add(d,sub(mul(x,1.0f/6.0f),mul(y,11.0f/6.0f)));
    V3 d5=add(d,sub(mul(x,5.0f/6.0f),mul(y,7.0f/6.0f)));
    V3 e0=add(e,add(mul(x,1.0f/6.0f),mul(y,0.5f)));
    V3 e1=sub(e,mul(x,1.0f/3.0f));
    V3 e2=add(e,sub(mul(x,1.0f/6.0f),mul(y,0.5f)));
    set_plane(c,0,b[0],b[1],v[1]); set_plane(c,1,v[0],v[1],d0);
    set_plane(c,2,b[1],b[2],v[2]); set_plane(c,3,d0,d1,e0);
    set_plane(c,4,b[2],b[3],v[3]); set_plane(c,5,v[2],v[3],d2);
    set_plane(c,6,b[3],b[4],v[4]); set_plane(c,7,d2,d3,e1);
    set_plane(c,8,b[4],b[5],v[5]); set_plane(c,9,v[4],v[5],d4);
    set_plane(c,10,b[5],b[0],v[0]); set_plane(c,11,d4,d5,e2);
    set_plane(c,12,b[0],b[4],b[2]);
    c->pos=pos; c->axis=z; c->color=color;
    c->height=len+1.270335113f*sz;
    c->radius2=1.333333333f*sz*sz;
}
static float crystal_rand(float x) {
    x=fractf(x*0.1031f);
    x*=x+33.33f;
    x*=x+x;
    return fractf(x);
}
static void scene_init(void) {
    for (int i=0;i<XTAL_COUNT;i++) {
        float fi=(float)i, seed=fi+19.37f;
        float angle=XTAL_TAU*(fi+0.5f)*0.618034f+(crystal_rand(seed+1.0f)-0.5f)*0.75f;
        float radius=(0.12f+0.78f*sqrt_lut((fi+0.5f)/(float)XTAL_COUNT))
                    *(0.82f+0.36f*crystal_rand(seed+2.0f));
        float co=cos_lut(angle), si=sin_lut(angle), size=crystal_rand(seed+3.0f);
        float len=0.20f+1.35f*size*size*size;
        float width=0.045f+0.095f*sqrt_lut(len/1.55f);
        float sign=crystal_rand(seed+5.0f) >= 0.78f ? -1.0f : 1.0f;
        float lean=0.04f+0.34f*crystal_rand(seed+6.0f);
        V3 up=vec(lean*(0.72f*co-0.45f*si*sign),1.0f,lean*(0.72f*si+0.45f*co*sign));
        V3 color=mixv(vec(0.42f,0.70f,0.94f),vec(0.76f,0.88f,1.0f),crystal_rand(seed+8.0f));
        build_crystal(&crystals[i],vec(co*radius*1.22f,-0.90f,si*radius*1.42f),
                      up,XTAL_TAU*crystal_rand(seed+7.0f),width,len,color);
    }
    light1=normalize(vec(0.4f,0.8f,0.5f));
    light2=normalize(vec(-0.7f,0.3f,0.6f));
}
static int bound_hit(const Crystal *c, V3 o, V3 rd, float nearest) {
    V3 v=sub(o,c->pos), z=c->axis;
    float a=dot(v,z), b=dot(rd,z), t0=0.0f, t1=1.0e30f;
    if (b == 0.0f) {
        if (a < 0.0f || a > c->height) return 0;
    } else {
        float u=-a/b, w=(c->height-a)/b;
        t0=maxf(0.0f,minf(u,w)); t1=maxf(u,w);
        if (t0 > t1 || t0 >= nearest) return 0;
    }
    V3 p=sub(v,mul(z,a)), d=sub(rd,mul(z,b));
    float dd=dot(d,d), t=dd > 0.0f ? clampf(-dot(p,d)/dd,t0,t1) : t0;
    p=add(p,mul(d,t));
    /* Small conservative padding accounts for normalized-axis LUT error. */
    return dot(p,p) <= c->radius2*1.0001f;
}
static int maptest(V3 o, V3 rd, Hit *hit) {
    float nearest=9999999.0f;
    int found=0;
    for (int i=0;i<XTAL_COUNT;i++) {
        const Crystal *c=&crystals[i];
        if (!bound_hit(c,o,rd,nearest)) continue;
        float t0=-1.0e30f, t1=1.0e30f;
        int enter=-1, exit=-1, valid=1;
        for (int j=0;j<XTAL_PLANES;j++) {
            const Plane *p=&c->planes[j];
            if (p->inv_nn == 0.0f) continue;
            float d=dot(rd,p->n), s=dot(o,p->n)-p->offset;
            if (d == 0.0f) {
                if (s > 0.0f) { valid=0; break; }
                continue;
            }
            float t=-s/d;
            if (d < 0.0f) { if (t > t0) { t0=t; enter=j; } }
            else if (t < t1) { t1=t; exit=j; }
            if (t0 > t1 || t1 <= 1.0e-4f || t0 >= nearest) { valid=0; break; }
        }
        if (!valid) continue;
        int inside=t0 <= 1.0e-4f, plane=inside ? exit : enter;
        float t=inside ? t1 : t0;
        if (plane < 0 || t <= 1.0e-4f || t >= nearest) continue;
        const Plane *p=&c->planes[plane];
        nearest=t; found=1;
        hit->t=t; hit->p=add(o,mul(rd,t));
        hit->reflected=sub(rd,mul(p->n,2.0f*dot(rd,p->n)*p->inv_nn));
        hit->color=c->color;
    }
    return found;
}
static float component(V3 a, int ch) { return ch == 0 ? a.x : ch == 1 ? a.y : a.z; }
static Channel dispersion(V3 p, V3 d, V3 n, V3 glass, int ch, float ior) {
    float g=component(glass,ch);
    Channel result={p,d,mixf(1.0f,g,0.6f)*0.02f,0.02f,0.0f,0};
    V3 in=refractv(d,n,1.0f/ior);
    if (dot(in,in) < 0.000001f) return result;
    Hit h;
    if (!maptest(add(p,mul(in,XTAL_EPS)),in,&h)) return result;
    result.crossed=1;
    V3 ni=normalize(sub(h.reflected,in)), out=refractv(in,ni,ior);
    if (dot(out,out) < 0.000001f) out=in;
    result.direction=normalize(out);
    result.origin=add(h.p,mul(result.direction,XTAL_EPS));
    float distance=maxf(h.t,0.0f), depth=distance/(distance+2.0f);
    result.body=0.025f+0.17f*depth;
    float tint=mixf(g,component(clampv(h.color),ch),0.4f);
    float body_color=mixf(1.0f,tint,0.6f);
    float fold=power_lut(1.0f-absf(dot(n,ni)),2);
    V3 r=normalize(h.reflected);
    float b1=power_lut(maxf(dot(r,light1),0.0f),52);
    float b2=power_lut(maxf(dot(r,light2),0.0f),76);
    result.inner=clampf(0.18f*fold+0.65f*b1+0.45f*b2,0.0f,0.75f);
    result.value=mixf(1.0f,tint,0.3f)*result.inner+
                 (1.0f-result.inner)*body_color*result.body;
    return result;
}
static V3 maprender(V3 o, V3 direction) {
    V3 rd=normalize(direction), ro=o, rgb=vec(0.0f,0.0f,0.0f);
    float f0=(XTAL_IOR-1.0f)/(XTAL_IOR+1.0f), mirror=0.0f;
    f0*=f0;
    for (int j=0;j<2;j++) {
        Hit h;
        if (!maptest(ro,rd,&h)) break;
        V3 r=normalize(h.reflected), n=normalize(sub(r,rd));
        V3 reflect_o=add(h.p,mul(r,XTAL_EPS));
        V3 pass_o=ro, pass_d=rd, local=vec(0.0f,0.0f,0.0f);
        float remain=1.0f;
        for (int k=0;k<6;k++) {
            Hit layer_hit;
            if (k == 0) layer_hit=h;
            else if (!maptest(pass_o,pass_d,&layer_hit)) break;
            V3 nk=normalize(sub(layer_hit.reflected,pass_d));
            V3 rk=normalize(layer_hit.reflected), glass=clampv(layer_hit.color);
            float nv=clampf(-dot(pass_d,nk),0.0f,1.0f);
            float fresnel=f0+(1.0f-f0)*power_lut(1.0f-nv,5);
            float rim=power_lut(1.0f-nv,3);
            float s1=power_lut(maxf(dot(rk,light1),0.0f),96);
            float s2=power_lut(maxf(dot(rk,light2),0.0f),128);
            float shine=clampf(s1+s2*0.7f,0.0f,1.0f);
            float coat_a=clampf(0.025f+0.38f*fresnel+0.22f*rim+0.85f*shine,0.0f,0.98f);
            V3 coat=mixv(vec(0.9f,0.95f,1.0f),glass,0.16f);
            Hit reflected;
            if (maptest(add(layer_hit.p,mul(rk,XTAL_EPS)),rk,&reflected))
                coat=mixv(coat,clampv(reflected.color),0.35f);
            Channel red=dispersion(layer_hit.p,pass_d,nk,glass,0,XTAL_IOR-XTAL_DISPERSION);
            Channel green=dispersion(layer_hit.p,pass_d,nk,glass,1,XTAL_IOR);
            Channel blue=dispersion(layer_hit.p,pass_d,nk,glass,2,XTAL_IOR+XTAL_DISPERSION);
            float body_a=(red.body+green.body+blue.body)/3.0f;
            float inner_a=(red.inner+green.inner+blue.inner)/3.0f;
            float a=coat_a+(1.0f-coat_a)*(inner_a+(1.0f-inner_a)*body_a);
            V3 layer=add(mul(coat,coat_a),mul(vec(red.value,green.value,blue.value),1.0f-coat_a));
            local=add(local,mul(layer,remain));
            remain*=1.0f-a;
            if (!green.crossed || remain < 0.01f) break;
            pass_o=green.origin; pass_d=green.direction;
        }
        if (j == 0) {
            rgb=local;
            float nv=clampf(-dot(rd,n),0.0f,1.0f);
            mirror=clampf(0.12f+0.88f*power_lut(1.0f-nv,2),0.0f,1.0f);
        } else {
            float facet=clampf(dot(rd,r),0.0f,1.0f);
            V3 surface=mixv(mul(clampv(h.color),0.125f),vec(1.0f,1.0f,1.0f),facet);
            float s1=power_lut(maxf(dot(r,light1),0.0f),48);
            float s2=power_lut(maxf(dot(r,light2),0.0f),96);
            V3 highlight=add(mul(vec(1.6f,1.45f,1.25f),s1),mul(vec(0.7f,0.9f,1.3f),s2));
            rgb=add(rgb,mul(add(add(local,mul(surface,0.75f)),highlight),mirror/(float)(j+1)));
        }
        ro=reflect_o; rd=r;
    }
    /* GLSL MAP_USE_ALPHA_BLEND=false; write RGB directly against black. */
    return clampv(rgb);
}
static V3 camera_rotate(V3 p, float sy, float cy, float sx, float cx) {
    V3 a=vec(p.x,cx*p.y-sx*p.z,sx*p.y+cx*p.z);
    return vec(cy*a.x+sy*a.z,a.y,-sy*a.x+cy*a.z);
}
static V3 render_pixel(int x, int y, int w, int h, float yaw, float tilt) {
    /* gl_FragCoord has bottom-left origin; the framebuffer has top-left. */
    float u=((float)x+0.5f-(float)w*0.5f)/(float)h;
    float v=((float)h*0.5f-(float)y-0.5f)/(float)h;
    /* tan(100 degrees/2) is constant, so no runtime tan is needed. */
    V3 ray=vec(2.383507185f*u,2.383507185f*v,1.0f);
    float sy=sin_lut(yaw), cy=cos_lut(yaw), sx=sin_lut(tilt), cx=cos_lut(tilt);
    V3 o=camera_rotate(vec(0.0f,0.0f,-2.0f),sy,cy,sx,cx);
    V3 d=camera_rotate(normalize(ray),sy,cy,sx,cx);
    return maprender(o,d);
}

#ifndef XTAL_HOST_TEST
static DWORD rgb24(V3 c) {
    return ((DWORD)(c.x*255.0f)<<16) | ((DWORD)(c.y*255.0f)<<8) | (DWORD)(c.z*255.0f);
}
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION || env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base=env->abi;
    if (!LW_ABI_VALID() || !lw_abi_base[LW_SLOT_IO_VIDEO_ENTER] ||
        !lw_abi_base[LW_SLOT_IO_VIDEO_EXIT] || !lw_abi_base[LW_SLOT_CONSOLE_GETK] ||
        !lw_abi_base[LW_SLOT_CONSOLE_GETP] || !lw_abi_base[LW_SLOT_INFO_FB] ||
        !lw_abi_base[LW_SLOT_INFO_FB_W] || !lw_abi_base[LW_SLOT_INFO_FB_H] ||
        !lw_abi_base[LW_SLOT_INFO_FB_BPP] || !lw_abi_base[LW_SLOT_INFO_FB_PITCH]) return -2;
    if (lw_gfx_enter() != 0) { lw_gfx_exit(); return -3; }
    DWORD w=lw_get_fb_w(), h=lw_get_fb_h(), pitch=lw_get_fb_pitch(), bpp=lw_get_fb_bpp();
    volatile BYTE *fb=(volatile BYTE *)lw_get_fb();
    if (!fb || !w || !h || w > 1920 || h > 1200 ||
        (bpp != 24 && bpp != 32) || pitch < w*(bpp/8)) {
        lw_gfx_exit(); return -4;
    }
    /* Same BGR byte layout used by shader.c; framebuffer format ABI does
     * not yet expose RGB mask positions. Support both 24 and 32 bpp. */
    scene_init();
    float yaw=0.0f, tilt=0.0f;
    int quit=0;
    while (!quit) {
        /* CPU ray tracing: cap samples at ~320x200 and fill blocks. All
         * 72 crystals, 2 reflections and 6 transmission layers remain. */
        //int step=(int)((w+319)/320);
        //int sh=(int)((h+199)/200);
        //if (sh > step) step=sh;
        int step=1;
        for (int y=0;y<(int)h && !quit;y+=step) {
            for (int x=0;x<(int)w;x+=step) {
                int px=x+step/2, py=y+step/2;
                if (px >= (int)w) px=(int)w-1;
                if (py >= (int)h) py=(int)h-1;
                DWORD color=rgb24(render_pixel(px,py,(int)w,(int)h,yaw,tilt));
                for (int yy=y;yy<y+step && yy<(int)h;yy++) {
                    volatile BYTE *row=fb+(DWORD)yy*pitch;
                    for (int xx=x;xx<x+step && xx<(int)w;xx++) {
                        volatile BYTE *p=row+(DWORD)xx*(bpp/8);
                        p[0]=(BYTE)color; p[1]=(BYTE)(color>>8); p[2]=(BYTE)(color>>16);
                        if (bpp == 32) p[3]=0;
                    }
                }
            }
            if (lw_getp() == 0x1b) quit=1;
        }
        if (quit) break;
        int key=lw_getk();
        if (key == 0x1b) break;
        if (key == 'a' || key == 'A') yaw-=0.15f;
        else if (key == 'w' || key == 'W') tilt-=0.10f;
        else if (key == 's' || key == 'S') tilt+=0.10f;
        else if (key == 'r' || key == 'R') { yaw=0.0f; tilt=0.0f; }
        else yaw+=0.15f; /* D/other key: advance GLSL's time-driven rotation. */
        yaw=fractf(yaw/XTAL_TAU)*XTAL_TAU;
        tilt=clampf(tilt,-1.3f,1.3f);
    }
    lw_gfx_exit();
    return 0;
}
#endif
