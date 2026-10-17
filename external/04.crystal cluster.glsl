// Shader Editor disabled this shader because it caused a crash while loading.
// Fix the shader and remove the #if 0 wrapper to re-enable it.

#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif

uniform vec2 resolution;
uniform float time;
uniform vec2 mouse;
uniform sampler2D backbuffer;

mat3 rotx(float w){return mat3(1.,0.,0.,0.,cos(w),sin(w),0.,-sin(w),cos(w));}
mat3 roty(float w){return mat3(cos(w),0.,-sin(w),0.,1.,0.,sin(w),0.,cos(w));}
mat3 rotz(float w){return mat3(cos(w),sin(w),0.,-sin(w),cos(w),0.,0.,0.,1.);}

vec3 r0,rd;//test ray
float nt;//near
vec3 np,nr;
vec4 gcol,ncol;

float ct0,ct1;
vec3 cn0,cn1;
vec4 cc0,cc1;
bool cv;
void startconvex(){
    ct0=-1e30;ct1=1e30;cv=true;
}
void addtri(vec3 a,vec3 b,vec3 c){
    if(!cv)return;
    vec3 n=cross(b-a,c-a);
    float nn=dot(n,n);
    if(nn<1e-12)return;
    float d=dot(rd,n),s=dot(r0-a,n);
    if(d==0.){if(s>0.)cv=false;return;}
    float t=-s/d;
    if(d<0.){if(t>ct0){ct0=t;cn0=n;cc0=gcol;}}
    else if(t<ct1){ct1=t;cn1=n;cc1=gcol;}
    if(ct0>ct1||ct1<=1e-4||ct0>=nt)cv=false;
}
void endconvex(){
    if(!cv)return;
    bool inside=ct0<=1e-4;
    float t=inside?ct1:ct0;
    if(t<=1e-4||t>=nt)return;
    vec3 n=inside?cn1:cn0;
    nt=t;np=r0+t*rd;nr=rd-2.*dot(rd,n)/dot(n,n)*n;
    ncol=inside?cc1:cc0;
}

bool crystalhittest(vec3 r0,vec3 rd,vec3 pos,vec3 up,float sz,float len){
    vec3 z=normalize(up),v=r0-pos;
    float h=len+1.270335113*sz,a=dot(v,z),b=dot(rd,z),t0=0.,t1=1e30;
    if(b==0.){if(a<0.||a>h)return false;}
    else{float k=1./b,u=-a*k,w=(h-a)*k;t0=max(0.,min(u,w));t1=max(u,w);if(t0>t1)return false;}
    vec3 p=v-z*a,d=rd-z*b;
    float dd=dot(d,d),t=dd>0.?clamp(-dot(p,d)/dd,t0,t1):t0;
    p+=d*t;
    return dot(p,p)<=1.333333333*sz*sz;
}
void addcrystal(vec3 pos,vec3 up,float rot,float sz,float len){
    if(!crystalhittest(r0,rd,pos,up,sz,len))return;
    vec3 Z=normalize(up),X,Y;
    float c=cos(rot),s=sin(rot),m=sz*.577350269189626;
    if(Z.z>-.999999){
        float k=1./(1.+Z.z),a=Z.x*c+Z.y*s,b=Z.y*c-Z.x*s;
        X=sz*vec3(c-Z.x*k*a,s-Z.y*k*a,-a);
        Y=m*vec3(-s-Z.x*k*b,c-Z.y*k*b,-b);
    }else{
        X=sz*vec3(c,-s,0.);
        Y=m*vec3(-s,-c,0.);
    }
    vec3 P=pos+Z*len,D=P+Z*(sz*.211722519),E=P+Z*(sz*1.058612594);
    vec3 o0=X-Y,o1=X+Y,o2=2.*Y,o3=Y-X,o4=-X-Y,o5=-2.*Y;
    vec3 b0=pos+o0,b1=pos+o1,b2=pos+o2,b3=pos+o3,b4=pos+o4,b5=pos+o5;
    vec3 c0=P+o0,c1=P+o1,c2=P+o2,c3=P+o3,c4=P+o4,c5=P+o5;
    vec3 d0=D+X*(5./6.)+Y*(7./6.),d1=D+X*(1./6.)+Y*(11./6.);
    vec3 d2=D-X+Y*(2./3.),d3=D-X-Y*(2./3.);
    vec3 d4=D+X*(1./6.)-Y*(11./6.),d5=D+X*(5./6.)-Y*(7./6.);
    vec3 e0=E+X*(1./6.)+Y*.5,e1=E-X/3.,e2=E+X*(1./6.)-Y*.5;
    startconvex();
    addtri(b0,b1,c1);
    addtri(c0,c1,d0);
    addtri(b1,b2,c2);
    addtri(d0,d1,e0);
    addtri(b2,b3,c3);
    addtri(c2,c3,d2);
    addtri(b3,b4,c4);
    addtri(d2,d3,e1);
    addtri(b4,b5,c5);
    addtri(c4,c5,d4);
    addtri(b5,b0,c0);
    addtri(d4,d5,e2);
    addtri(b0,b4,b2);
    endconvex();
}
void scene1()
{
	gcol=vec4(.5,1.,1.,1.);
    addcrystal(vec3(0.,-.8,0.),vec3(0.,1.,0.),0.,.25,1.6);
    gcol=vec4(.1,.5,1.,1.);
    addcrystal(vec3(.3,-.8,.1),vec3(.3,1.,.15),.4,.18,1.1);
    gcol=vec4(.5,.1,1.,1.);
    addcrystal(vec3(-.3,-.8,0.),vec3(-.4,1.,0.),.7,.2,1.2);
    gcol=vec4(1.,1.,.5,1.);
    addcrystal(vec3(0.,-.8,-.3),vec3(.1,1.,-.4),1.2,.15,.9);
}
float crystalRand(float x)
{
    x = fract(x * 0.1031);
    x *= x + 33.33;
    x *= x + x;
    return fract(x);
}
void scene2()
{
    const float TAU = 6.2831853;
    const int COUNT = 72;
    for (int i = 0; i < COUNT; ++i)
    {
        float fi = float(i);
        float seed = fi + 19.37;
        float rAngle = crystalRand(seed + 1.0);
        float rRadius = crystalRand(seed + 2.0);
        float rSize = crystalRand(seed + 3.0);
        float rCross = crystalRand(seed + 5.0);
        float rLean = crystalRand(seed + 6.0);
        float rRotation = crystalRand(seed + 7.0);
        float rTint = crystalRand(seed + 8.0);
        float angle = TAU * (fi + 0.5) * 0.618034
                    + (rAngle - 0.5) * 0.75;
        float radius = (0.12 + 0.78 * sqrt((fi + 0.5) / float(COUNT)))
                     * (0.82 + 0.36 * rRadius);
        // 同一组 sin/cos 同时用于位置和生长方向。
        float ca = cos(angle);
        float sa = sin(angle);
        float px = ca * radius * 1.22;
        float pz = sa * radius * 1.42;
        // x^3 与 pow(x, 3.0) 等价，省去通用幂函数。
        float sizeCubed = rSize * rSize * rSize;
        float len = 0.20 + 1.35 * sizeCubed;
        float width = 0.045 + 0.095 * sqrt(len / 1.55);
        // 用径向和切向组合倾斜方向，省去第二组 sin/cos。
        float tangentSign = mix(1.0, -1.0, step(0.78, rCross));
        float lean = 0.04 + 0.34 * rLean;
        vec3 up = vec3(
            lean * (0.72 * ca - 0.45 * sa * tangentSign),
            1.0,
            lean * (0.72 * sa + 0.45 * ca * tangentSign)
        );
        float rotation = TAU * rRotation;
        gcol = vec4(mix(vec3(.42,.70,.94),
                        vec3(.76,.88,1.00), rTint), 1.0);
        addcrystal(vec3(px, -.90, pz), up, rotation, width, len);
    }
}
bool maptest(vec3 o,vec3 d,out float t,out vec3 p,out vec3 r,out vec4 c)
{
	r0=o;rd=d;
	nt=9999999.;
	//scene start
	//addtri(vec3(0.,0.,0.),vec3(1.,1.,0.),vec3(0.,1.,0.));
	scene2();
	//scene end
	if(nt<9999999.)
	{
		t=nt;
		p=np;
		r=nr;
		c=ncol;
		return true;
	}
	return false;
}

const float MAP_IOR=1.52,MAP_DISPERSION=0.15,MAP_EPS=0.001;
const int MAP_REFLECT_STEPS=2,MAP_LAYERS=6;
const bool MAP_USE_ALPHA_BLEND=false;

vec3 mapDispChannel(vec3 p,vec3 d,vec3 N,vec3 glass,vec3 mask,float ior,vec3 L1,vec3 L2,out vec3 nextO,out vec3 nextD,out bool crossed){
    float g=dot(glass,mask);
    vec3 result=vec3(mix(1.,g,0.6)*0.02,0.02,0.);
    nextO=p;nextD=d;crossed=false;
    vec3 inD=refract(d,N,1./ior);
    if(dot(inD,inD)<0.000001)return result;
    float ti;
    vec3 pi,ri;
    vec4 ci;
    if(!maptest(p+inD*MAP_EPS,inD,ti,pi,ri,ci))return result;
    crossed=true;
    vec3 Ni=normalize(ri-inD);
    vec3 outD=refract(inD,Ni,ior);
    if(dot(outD,outD)<0.000001)outD=inD;
    nextD=normalize(outD);
    nextO=pi+nextD*MAP_EPS;
    float h=max(ti,0.)/(max(ti,0.)+2.);
    float bodyA=0.025+0.17*h;
    float tint=mix(g,dot(clamp(ci.rgb,0.,1.),mask),0.4);
    float bodyColor=mix(1.,tint,0.6);
    float fold=pow(1.-abs(dot(N,Ni)),2.);
    float b1=pow(max(dot(normalize(ri),L1),0.),52.);
    float b2=pow(max(dot(normalize(ri),L2),0.),76.);
    float innerA=clamp(0.18*fold+0.65*b1+0.45*b2,0.,0.75);
    float innerColor=mix(1.,tint,0.3);
    return vec3(innerColor*innerA+(1.-innerA)*bodyColor*bodyA,bodyA,innerA);
}

vec4 maprender(vec3 o,vec3 d){
    d=normalize(d);
    vec3 L1=normalize(vec3(0.4,0.8,0.5));
    vec3 L2=normalize(vec3(-0.7,0.3,0.6));
    float f0=(MAP_IOR-1.)/(MAP_IOR+1.);
    f0*=f0;
    vec3 rgb=vec3(0.);
    float alpha=0.,mirror=0.;
    vec3 ro=o,rd=d;
    for(int j=0;j<MAP_REFLECT_STEPS;j++){
        float t;
        vec3 p,r;
        vec4 c;
        if(!maptest(ro,rd,t,p,r,c))break;
        vec3 R=normalize(r),N=normalize(R-rd);
        vec3 reflectO=p+R*MAP_EPS,reflectD=R;
        vec3 passO=ro,passD=rd;
        vec3 local=vec3(0.);
        float remain=1.;
        for(int k=0;k<MAP_LAYERS;k++){
            float tk;
            vec3 pk,rk;
            vec4 ck;
            if(k==0){
                pk=p;rk=r;ck=c;
            }else{
                if(!maptest(passO,passD,tk,pk,rk,ck))break;
            }
            vec3 nk=normalize(rk-passD),Rk=normalize(rk);
            vec3 glass=clamp(ck.rgb,0.,1.);
            float nv=clamp(-dot(passD,nk),0.,1.);
            float F=f0+(1.-f0)*pow(1.-nv,5.);
            float rim=pow(1.-nv,3.);
            float s1=pow(max(dot(Rk,L1),0.),96.);
            float s2=pow(max(dot(Rk,L2),0.),128.);
            float shine=clamp(s1+s2*0.7,0.,1.);
            float coatA=clamp(0.025+0.38*F+0.22*rim+0.85*shine,0.,0.98);
            vec3 coat=mix(vec3(0.9,0.95,1.),glass,0.16);
            float tr;
            vec3 pr,rr;
            vec4 cr;
            if(maptest(pk+Rk*MAP_EPS,Rk,tr,pr,rr,cr))coat=mix(coat,clamp(cr.rgb,0.,1.),0.35);
            vec3 nextOR,nextDR,nextOG,nextDG,nextOB,nextDB;
            bool crossR,crossG,crossB;
            vec3 aR=mapDispChannel(pk,passD,nk,glass,vec3(1.,0.,0.),MAP_IOR-MAP_DISPERSION,L1,L2,nextOR,nextDR,crossR);
            vec3 aG=mapDispChannel(pk,passD,nk,glass,vec3(0.,1.,0.),MAP_IOR,L1,L2,nextOG,nextDG,crossG);
            vec3 aB=mapDispChannel(pk,passD,nk,glass,vec3(0.,0.,1.),MAP_IOR+MAP_DISPERSION,L1,L2,nextOB,nextDB,crossB);
            float bodyA=(aR.y+aG.y+aB.y)/3.;
            float innerA=(aR.z+aG.z+aB.z)/3.;
            float a=coatA+(1.-coatA)*(innerA+(1.-innerA)*bodyA);
            vec3 layer=coat*coatA+(1.-coatA)*vec3(aR.x,aG.x,aB.x);
            local+=remain*layer;
            remain*=1.-a;
            if(!crossG||remain<0.01)break;
            passO=nextOG;
            passD=nextDG;
        }
        if(j==0){
            rgb=local;
            alpha=clamp(1.-remain,0.,1.);
            float nv=clamp(-dot(rd,N),0.,1.);
            mirror=clamp(0.12+0.88*pow(1.-nv,2.),0.,1.);
        }else{
            float facet=clamp(dot(rd,R),0.,1.);
            vec3 surface=mix(clamp(c.rgb,0.,1.)*0.125,vec3(1.),facet);
            float spec1=pow(max(dot(R,L1),0.),48.);
            float spec2=pow(max(dot(R,L2),0.),96.);
            vec3 highlight=vec3(1.6,1.45,1.25)*spec1+vec3(0.7,0.9,1.3)*spec2;
            rgb+=mirror*(local+surface*0.75+highlight)/float(j+1);
        }
        ro=reflectO;
        rd=reflectD;
    }
    return vec4(clamp(MAP_USE_ALPHA_BLEND?rgb/max(alpha,0.0001):rgb,0.,1.),alpha);
}
void main(void) {
    vec2 uv=gl_FragCoord.xy/resolution;
    vec2 m=mouse;
    vec2 bbuv=uv;
    uv-=vec2(.5);
    m-=vec2(.5);
    uv.x*=resolution.x/resolution.y;
    m.x*=resolution.x/resolution.y;
    const float fov=100.;
    const vec3 rup=vec3(0.,1.,0.);
    const vec3 forward=vec3(0.,0.,1.);
    const vec3 right=vec3(1.,0.,0.);
    const vec3 ray0=vec3(0.,0.,-2.);
    vec3 rayd=normalize(forward+2.*tan(radians(fov)*.5)*(uv.x*right+uv.y*rup));

    float w=time*.05;
    mat3 rm=roty(w+m.x*16.)*rotx(-m.y*8.);
    vec3 o=rm*ray0,d=rm*rayd;
    gl_FragColor=maprender(o,d);
}
