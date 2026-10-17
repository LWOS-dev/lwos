#include "abi.h"
#include "resman.h"

PVOID *lw_abi_base;

DWORD vi_h, vi_w, vi_pitch;
PDWORD vi_fb;

#define VK_ESC      0x1b

static void putpixel(int x, int y, DWORD color) {
    if (x>=(int)vi_w || y>=(int)vi_h || x<0 || y<0)return;
    vi_fb[x+y*(vi_pitch/4)]=color&0xffffff;
}
static inline float cheapFloor(float x) {
    int n = (int)x;
    if ((float)n > x) n -= 1;
    return (float)n;
}

static inline float cheapCosPi(float x) {
    int n = (int)cheapFloor(x);
    float f = x - (float)n;
    float y = 1.0f + f * f * (4.0f * f - 6.0f);
    return (n & 1) ? -y : y;
}

static inline float cheapSinPi(float x) {
    int n = (int)cheapFloor(x);
    float f = x - (float)n;
    float t = f * (1.0f - f);
    float y = t * (3.14159265f + 3.40185714f * t);
    return (n & 1) ? -y : y;
}

static inline float cheapSqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    union { float f; unsigned int i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float r = u.f;
    r = r * (1.5f - 0.5f * x * r * r);
    r = r * (1.5f - 0.5f * x * r * r);
    return x * r;
}

static inline float cheapAtanf(float x) {
    int neg = x < 0.0f;
    if (neg) x = -x;
    int inv = 0;
    if (x > 1.0f) { x = 1.0f / x; inv = 1; }
    float x2 = x * x;
    float y = x * (0.9998660f + x2 * (-0.3302995f + x2 * (0.1801410f + x2 * (-0.0851330f))));
    if (inv) y = 1.5707963f - y;
    if (neg) y = -y;
    return y;
}

static inline float cheapAtan2f(float y, float x) {
    if (x > 0.0f) return cheapAtanf(y / x);
    if (x < 0.0f) {
        if (y >= 0.0f) return cheapAtanf(y / x) + 3.14159265f;
        else           return cheapAtanf(y / x) - 3.14159265f;
    }
    if (y > 0.0f) return 1.5707963f;
    if (y < 0.0f) return -1.5707963f;
    return 0.0f;
}

static DWORD render(int x, int y) {
    float cx = vi_w * 0.5f;
    float cy = vi_h * 0.5f;
    float px = (x - cx) / cx;
    float py = (y - cy) / cy;

    float d = cheapSqrtf(px*px + py*py);
    float a = cheapAtan2f(py, px);
    float t = a / 6.2831853f + 0.5f;

    float ring = 0.5f + 0.5f * cheapSinPi(d * 12.732395f);

    float tp = t * 2.0f;
    float r = 0.5f + 0.5f * cheapCosPi(tp);
    float g = 0.5f + 0.5f * cheapCosPi(tp - 0.6666667f);
    float b = 0.5f + 0.5f * cheapCosPi(tp - 1.3333333f);

    float vig = 1.0f - d * 0.7f;
    if (vig < 0.0f) vig = 0.0f;

    r = r * ring * vig;
    g = g * ring * vig;
    b = b * ring * vig;

    if (d < 0.03f) { r = g = b = 1.0f; }

    int R = (int)(r * 255.0f);
    int G = (int)(g * 255.0f);
    int B = (int)(b * 255.0f);
    if (R > 255) R = 255; 
    if (R < 0) R = 0;
    if (G > 255) G = 255; 
    if (G < 0) G = 0;
    if (B > 255) B = 255; 
    if (B < 0) B = 0;

    return ((DWORD)R << 16) | ((DWORD)G << 8) | (DWORD)B;
}
__attribute__((section(".text.start")))
int lwp_main(const LW_ENV *env) {
    if (!env || env->version != LW_ENV_VERSION ||
        env->size < sizeof(*env) || !env->abi)
        return -1;
    lw_abi_base = env->abi;
    if (!LW_ABI_VALID() || !lw_abi_base[LW_SLOT_TRAMP_CALL] ||
        !lw_abi_base[LW_SLOT_CONSOLE_GETK] ||
        !lw_abi_base[LW_SLOT_IO_VIDEO_EXIT])
        return -2;

    lw_gfx_enter();
    vi_h=lw_get_fb_h();
    vi_w=lw_get_fb_w();
    vi_pitch=lw_get_fb_pitch();
    vi_fb=(PVOID)lw_get_fb();

    DWORD t=0;
    // 暂时没有写动态内存分配，先凑合直接写显存吧

    while (t!=VK_ESC) {
        for (int y=0; y<(int)vi_h; y++) {
            for (int x=0; x<(int)vi_w; x++) {
                putpixel(x,y,render(x,y));
            }
        }
        t=lw_getk();
    }

    lw_gfx_exit();
    return 0;
}
