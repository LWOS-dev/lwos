/* Host-only libm oracle; production xtal has no libc/libm dependency. */
#include <stdio.h>
#include <math.h>
#define XTAL_HOST_TEST
#include "../external/xtal.c"

int main(int argc, char **argv) {
    float sin_error=0.0f, sqrt_error=0.0f, pow_error=0.0f;
    const int powers[]={2,3,5,48,52,76,96,128};
    for (int i=0;i<100000;i++) {
        float x=(float)i/99999.0f, angle=x*600.0f-300.0f;
        sin_error=maxf(sin_error,fabsf(sin_lut(angle)-sinf(angle)));
        sin_error=maxf(sin_error,fabsf(cos_lut(angle)-cosf(angle)));
        float z=exp2f(x*100.0f-50.0f);
        sqrt_error=maxf(sqrt_error,fabsf(sqrt_lut(z)/sqrtf(z)-1.0f));
        for (int j=0;j<8;j++)
            pow_error=maxf(pow_error,fabsf(power_lut(x,powers[j])-powf(x,(float)powers[j])));
    }
    if (sqrt_lut(0.0f) != 0.0f ||
        fabsf(sqrt_lut(1.0e-40f)/sqrtf(1.0e-40f)-1.0f) > 0.000003f) return 1;
    printf("LUT max errors: sin/cos=%g sqrt-relative=%g power=%g\n",
           (double)sin_error,(double)sqrt_error,(double)pow_error);
    if (sin_error > 0.00005f || sqrt_error > 0.000003f || pow_error > 0.002f) return 2;
    scene_init();
    const int w=240, h=160;
    FILE *out=argc > 1 ? fopen(argv[1],"wb") : NULL;
    if (argc > 1 && !out) return 3;
    if (out) fprintf(out,"P6\n%d %d\n255\n",w,h);
    for (int frame=0;frame<2;frame++) {
        int visible=0;
        for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
            V3 c=render_pixel(x,y,w,h,frame*0.7f,frame*0.2f);
            if (!isfinite(c.x) || !isfinite(c.y) || !isfinite(c.z) ||
                c.x < 0.0f || c.x > 1.0f || c.y < 0.0f || c.y > 1.0f ||
                c.z < 0.0f || c.z > 1.0f) return 4;
            unsigned char p[3]={(unsigned char)(c.x*255.0f),
                                (unsigned char)(c.y*255.0f),(unsigned char)(c.z*255.0f)};
            if (p[0] || p[1] || p[2]) visible++;
            if (out && frame == 0 && fwrite(p,1,3,out) != 3) return 5;
        }
        printf("Frame %d: %d visible pixels, finite RGB within [0,1]\n",frame,visible);
        if (visible < 100 || visible >= w*h) return 6;
    }
    if (out && fclose(out)) return 7;
    return 0;
}
