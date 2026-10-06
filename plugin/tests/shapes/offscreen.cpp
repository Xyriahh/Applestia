// Offline shader regression: software-only surfaceless EGL, no compositor.
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <array>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "../../src/GlassShapeLens.hpp"
constexpr int W=96,H=64;
std::string shaderDirectory;
GLuint program(const char* name) {
    std::ifstream file(shaderDirectory+"/"+name);
    assert(file.good());
    std::stringstream s;s<<file.rdbuf(); std::string fragment=s.str();
    const char* vertex="#version 300 es\nlayout(location=0) in vec2 pos; out vec2 v_texcoord; uniform vec4 box; void main(){v_texcoord=pos;gl_Position=vec4((box.xy+pos*box.zw)*2.0-1.0,0,1);}";
    auto compile=[](GLenum type,const char* src){GLuint id=glCreateShader(type);glShaderSource(id,1,&src,nullptr);glCompileShader(id);GLint ok;glGetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok){char log[4096];glGetShaderInfoLog(id,4096,nullptr,log);std::cerr<<log;abort();}return id;};
    auto v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragment.c_str());
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);
    GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);glDeleteShader(v);glDeleteShader(f);return p;
}
void uni(GLuint p,const char* n,float x){glUniform1f(glGetUniformLocation(p,n),x);}
void uni2(GLuint p,const char* n,float x,float y){glUniform2f(glGetUniformLocation(p,n),x,y);}
void uni4(GLuint p,const char* n,float x,float y,float z,float w){glUniform4f(glGetUniformLocation(p,n),x,y,z,w);}
struct FB {GLuint tex,fb; int w,h; FB(int width=W,int height=H):w(width),h(height){glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,w,h);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);} };
using Pixel=std::array<unsigned char,4>;
Pixel pixel(const FB& f,int x,int y){glBindFramebuffer(GL_FRAMEBUFFER,f.fb);Pixel p;glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
void clear(const FB& f,float r,float g,float b,float a=1){glBindFramebuffer(GL_FRAMEBUFFER,f.fb);glDisable(GL_SCISSOR_TEST);glClearColor(r,g,b,a);glClear(GL_COLOR_BUFFER_BIT);}
void copy(const FB& from,const FB& to){glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,to.fb);glBlitFramebuffer(0,0,W,H,0,0,W,H,GL_COLOR_BUFFER_BIT,GL_NEAREST);}
void drawShape(GLuint p,const FB& target,const FB& source,int debug=0,float opacity=1,float tintR=0,float tintA=0) {
    glBindFramebuffer(GL_FRAMEBUFFER,target.fb);glViewport(0,0,W,H);glUseProgram(p);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,source.tex);glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(p,"tex"),0);glUniform1i(glGetUniformLocation(p,"softTex"),1);
    glUniform1i(glGetUniformLocation(p,"debugView"),debug);
    glUniform1i(glGetUniformLocation(p,"inlineFrost"),0);
    uni4(p,"box",16.f/W,8.f/H,64.f/W,48.f/H);uni2(p,"shapeSize",64,48);
    uni4(p,"radii",16,8,0,24);uni2(p,"sampleOffset",16,8);uni2(p,"sampleSize",W,H);uni2(p,"storageSize",source.w,source.h);
    const auto physical=GlassShapeLens::geometry(64,48,{16,8,0,24},1);
    uni(p,"bezel",physical.bezelPx);glUniform4f(glGetUniformLocation(p,"bezelSides"),physical.bezelPx,physical.bezelPx,physical.bezelPx,physical.bezelPx);glUniform3f(glGetUniformLocation(p,"lensPhysical"),physical.bezelPx>0?physical.displacementPx/physical.bezelPx:0.f,physical.chromaticPx,physical.lipPx);
    uni(p,"scale",1);uni(p,"opacity",opacity);uni(p,"frostMix",0);uni(p,"specular",0);
    uni4(p,"tint",tintR,0,0,tintA);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
}

std::vector<Pixel> pixels(const FB& f) {
    glBindFramebuffer(GL_FRAMEBUFFER,f.fb);
    std::vector<Pixel> result(f.w*f.h);
    glReadPixels(0,0,f.w,f.h,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
    return result;
}

void performanceRegression(GLuint lens,GLuint frost) {
    constexpr int width=544,height=384;
    FB source(width,height),soft(width,height),optimized(width,height),reference(width,height),unusedSoft(width,height);
    clear(unusedSoft,1,0,1); // Fused material must never consume stale/unused m_frost.
    std::vector<Pixel> backdrop(width*height),blurred(width*height);
    for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
        backdrop[y*width+x]=Pixel{static_cast<unsigned char>((x*13+y*17)%256),
                                 static_cast<unsigned char>((x*11+y*7)%256),
                                 static_cast<unsigned char>((x+y*3)%256),255};
        blurred[y*width+x]=Pixel{static_cast<unsigned char>((x*3+y)%256),
                                static_cast<unsigned char>((x+y*2)%256),
                                static_cast<unsigned char>((x*5+y*3)%256),255};
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,backdrop.data());
    glBindTexture(GL_TEXTURE_2D,soft.tex);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,blurred.data());
    auto oldLens=program("liquidshape-reference.frag"),oldFrost=program("shapefrost-reference.frag");
    auto draw=[&](GLuint shader,const FB& target,int debug,float scale,float frostMix,float opacity,float fusedRadius=0,bool small=false,bool clip=false,int boundary=0) {
        clear(target,.1,.2,.3);
        glViewport(0,0,width,height);glUseProgram(shader);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,fusedRadius>0 ? unusedSoft.tex : soft.tex);glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(shader,"tex"),0);glUniform1i(glGetUniformLocation(shader,"softTex"),1);
        glUniform1i(glGetUniformLocation(shader,"debugView"),debug);
        glUniform1i(glGetUniformLocation(shader,"inlineFrost"),fusedRadius>0 ? 1 : 0);
        uni(shader,"inlineRadius",fusedRadius);
        const float shapeWidth=small ? 120.f : 512.f, shapeHeight=small ? 40.f : 320.f;
        const float x=boundary<0 ? -12.3f : boundary>0 ? width-shapeWidth+20.3f : scale==1.25f ? 16.13f : 16.f;
        const float y=boundary<0 ? -6.7f : boundary>0 ? height-shapeHeight+10.7f : scale==1.25f ? 32.27f : 32.f;
        uni4(shader,"box",x/width,y/height,shapeWidth/width,shapeHeight/height);
        uni2(shader,"shapeSize",shapeWidth,shapeHeight);uni2(shader,"sampleOffset",x,y);
        uni2(shader,"sampleSize",width,height);uni2(shader,"storageSize",width,height);
        uni4(shader,"radii",32*scale,8*scale,0,24*scale);
        const auto physical=GlassShapeLens::geometry(shapeWidth/scale,shapeHeight/scale,{32,8,0,24},scale);
        uni(shader,"bezel",physical.bezelPx);glUniform4f(glGetUniformLocation(shader,"bezelSides"),physical.bezelPx,physical.bezelPx,physical.bezelPx,physical.bezelPx);glUniform3f(glGetUniformLocation(shader,"lensPhysical"),physical.bezelPx>0?physical.displacementPx/physical.bezelPx:0.f,physical.chromaticPx,physical.lipPx);
        uni(shader,"scale",scale);uni(shader,"opacity",opacity);
        uni(shader,"frostMix",frostMix);uni(shader,"specular",.38);
        uni4(shader,"tint",.27,.49,.73,.17);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(clip) {glEnable(GL_SCISSOR_TEST);glScissor(int(x)+19,int(y)+3,int(shapeWidth)-28,int(shapeHeight)-7);}
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisable(GL_SCISSOR_TEST);
    };
    for (int debug=0;debug<=3;++debug) for (float scale : {.5f,1.f,1.25f,2.f})
        for (float mix : {0.f,.2f,1.f}) for (float opacity : {.25f,1.f}) {
            draw(lens,optimized,debug,scale,mix,opacity);
            draw(oldLens,reference,debug,scale,mix,opacity);
            assert(pixels(optimized)==pixels(reference)); // Full image incl exact circular/chromatic edges.
        }
    int maxFrostError=0;
    for (float radius : {.1f,.5f,.9f,1.f,1.25f,2.5f}) {
        auto blur=[&](GLuint shader,const FB& target) {
            clear(target,1,0,1);glViewport(0,0,width,height);glDisable(GL_BLEND);glUseProgram(shader);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);
            uni4(shader,"box",0,0,1,1);uni2(shader,"sampleSize",width,height);uni2(shader,"storageSize",width,height);
            uni(shader,"radius",radius);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        };
        blur(frost,optimized);blur(oldFrost,reference);
        const auto a=pixels(optimized),b=pixels(reference);
        for(size_t i=0;i<a.size();++i) for(int c=0;c<4;++c) {
            const int error=std::abs(int(a[i][c])-int(b[i][c]));
            if(radius>1.f) assert(error==0);
            maxFrostError=std::max(maxFrostError,error);
            assert(error<=1); // Equivalent ideal kernel; GL_LINEAR interpolation quantizes fractions.
        }
    }
    std::cout<<"PASS optimized shared lens byte-exact vs full-model oracle (96 full-image cases); frost error <= "<<maxFrostError<<" UNORM8 LSB\n";
    // Fuse the SAME center-blur then bilinear soft sampling, not a different
    // four-tap kernel at arbitrary refracted coordinates. The random texture
    // deliberately makes that naive approximation fail by far more than 1 LSB.
    int inlineError=0;
    for(float mix : {.05f,.2f,.25f}) for(float scale : {.5f,1.f,1.25f}) {
        const float radius=(.5f+2.f*mix)*scale;
        if(radius>1.f) continue;
        clear(soft,1,0,1);glViewport(0,0,width,height);glDisable(GL_BLEND);glUseProgram(frost);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);
        uni4(frost,"box",0,0,1,1);uni2(frost,"sampleSize",width,height);uni2(frost,"storageSize",width,height);
        uni(frost,"radius",radius);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
        for(bool small : {false,true}) for(bool clip : {false,true}) for(float opacity : {.25f,1.f}) for(int boundary : {-1,0,1}) {
            draw(lens,optimized,0,scale,mix,opacity,radius,small,clip,boundary);
            draw(oldLens,reference,0,scale,mix,opacity,0,small,clip,boundary);
            const auto a=pixels(optimized),b=pixels(reference);
            for(size_t i=0;i<a.size();++i) for(int c=0;c<4;++c) {
                const int error=std::abs(int(a[i][c])-int(b[i][c]));
                inlineError=std::max(inlineError,error);
                assert(error<=1);
                if(c==3) assert(error==0); // Coverage/opacity and clips never approximate.
            }
        }
    }
    std::cout<<"PASS fused low-frost vs separate pass incl chromatic edges, fractional positions, small pills, clips/tint/opacity: <= "<<inlineError<<" UNORM8 LSB, alpha exact\n";
}

void ownerLensRegression(GLuint shader) {
    constexpr int width=512,height=320,x=32,y=32;
    FB snapshot(width,height),bent(width,height),old(width,height),flat(width,height);
    std::vector<Pixel> pattern(width*height);
    for(int row=0;row<height;++row) for(int col=0;col<width;++col)
        pattern[row*width+col]=Pixel{static_cast<unsigned char>(std::round(255.f*(row+col)/(width+height-2))),
                                    static_cast<unsigned char>(std::round(255.f*row/(height-1))),
                                    static_cast<unsigned char>(((row/8+col/8)&1) ? 240 : 16),255};
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,snapshot.tex);
    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pattern.data());
    auto draw=[&](const FB& target,float w,float h,float r,const GlassShapeLens::SGeometry& lens,int view) {
        glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,snapshot.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,target.fb);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,target.fb);glViewport(0,0,width,height);glUseProgram(shader);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,snapshot.tex);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,snapshot.tex);glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(shader,"tex"),0);glUniform1i(glGetUniformLocation(shader,"softTex"),1);
        glUniform1i(glGetUniformLocation(shader,"inlineFrost"),0);glUniform1i(glGetUniformLocation(shader,"debugView"),view);
        uni4(shader,"box",float(x)/width,float(y)/height,w/width,h/height);uni2(shader,"shapeSize",w,h);
        uni2(shader,"sampleOffset",x,y);uni2(shader,"sampleSize",width,height);uni2(shader,"storageSize",width,height);
        uni4(shader,"radii",r,r,r,r);uni4(shader,"tint",0,0,0,0);
        uni(shader,"bezel",lens.bezelPx);uni(shader,"scale",1);uni(shader,"frostMix",.2);uni(shader,"specular",.4);uni(shader,"opacity",1);
        glUniform4f(glGetUniformLocation(shader,"bezelSides"),lens.bezelPx,lens.bezelPx,lens.bezelPx,lens.bezelPx);glUniform3f(glGetUniformLocation(shader,"lensPhysical"),lens.bezelPx>0?lens.displacementPx/lens.bezelPx:0.f,lens.chromaticPx,lens.lipPx);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    };
    for(auto shape : {std::array<float,3>{430,190,16}, {160,48,24}, {120,32,16}}) {
        const float w=shape[0],h=shape[1],r=shape[2];
        const auto lens=GlassShapeLens::geometry(w,h,{r,r,r,r},1);
        const float oldBezel=std::clamp(.5f*std::min(r,.5f*std::min(w,h)),4.f,16.f);
        draw(old,w,h,r,{oldBezel,.6f*oldBezel,.25f,1.5f},5);
        draw(bent,w,h,r,lens,5);draw(flat,w,h,r,lens,4);
        const auto before=pixels(snapshot),a=pixels(bent),b=pixels(old),f=pixels(flat);
        for(size_t i=0;i<f.size();++i) for(int c=0;c<4;++c)
            assert(std::abs(int(f[i][c])-int(before[i][c]))<=1); // AA source-over can round once; no painted rim.
        const int edge=(y+1)*width+x+int(w/2),centre=(y+int(h/2))*width+x+int(w/2);
        const float oldPull=(int(b[edge][1])-int(before[edge][1]))*(height-1)/255.f;
        const float newPull=(int(a[edge][1])-int(before[edge][1]))*(height-1)/255.f;
        assert(newPull>oldPull+1.5f && newPull>1.5f*oldPull);
        assert(a[centre]==before[centre]); // No dome magnification or frozen centre.
        int checkerChanges=0;
        for(int row=0;row<height;++row) for(int col=0;col<width;++col) {
            const int i=row*width+col;
            if(col<x || row<y || col>=x+w || row>=y+h) assert(a[i]==before[i]);
            assert(std::abs(int(a[i][1])-int(before[i][1]))*(height-1)/255.f <= lens.displacementPx+1.5f);
            if(std::abs(int(a[i][2])-int(b[i][2]))>32) ++checkerChanges;
        }
        assert(checkerChanges>100);
        // Fully outside the TL rounded corner must remain untouched too.
        assert(a[y*width+x]==before[y*width+x]);
        std::cout<<"PASS owner lens "<<int(w)<<"x"<<int(h)<<" r"<<r<<": diagonal edge pull "<<oldPull<<" -> "<<newPull
                 <<" px, "<<checkerChanges<<" visibly changed checker pixels, centre/outside/bounds exact\n";
    }
}

void fourEdgeRegression(GLuint shader) {
    constexpr int width=512,height=320;
    FB scene(width,height),output(width,height),normal(width,height),field(width,height),bezelView(width,height),
       parent(width,height),stack(width,height),roi(640,384);
    struct Rect {float x,y,w,h;};
    auto reset=[&](const FB& from,const FB& to) {
        glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,to.fb);
        glBlitFramebuffer(0,0,width,height,0,0,width,height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    };
    auto draw=[&](const FB& target,const FB& prefix,Rect box,std::array<float,4> radii,float scale,int view,float clipTop=0) {
        const auto lens=GlassShapeLens::geometry(box.w/scale,box.h/scale,
            {radii[0]/scale,radii[1]/scale,radii[2]/scale,radii[3]/scale},scale);
        const int pad=int(GlassShapeLens::samplePaddingPx(lens,.2f,scale));
        const int x0=std::max(0,int(std::floor(box.x))-pad),y0=std::max(0,int(std::floor(box.y+clipTop))-pad);
        const int x1=std::min(width,int(std::ceil(box.x+box.w))+pad),y1=std::min(height,int(std::ceil(box.y+box.h))+pad);
        clear(roi,1,0,1); // Grow/reused capacity OUTSIDE initialized footprint must never bleed into any edge.
        glBindFramebuffer(GL_READ_FRAMEBUFFER,prefix.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,roi.fb);
        glBlitFramebuffer(x0,y0,x1,y1,0,0,x1-x0,y1-y0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,target.fb);glViewport(0,0,width,height);glUseProgram(shader);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,roi.tex);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,roi.tex);glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(shader,"tex"),0);glUniform1i(glGetUniformLocation(shader,"softTex"),1);
        glUniform1i(glGetUniformLocation(shader,"inlineFrost"),1);glUniform1i(glGetUniformLocation(shader,"debugView"),view);
        uni(shader,"inlineRadius",GlassShapeLens::frostRadiusPx(.2f,scale));
        uni4(shader,"box",box.x/width,box.y/height,box.w/width,box.h/height);uni2(shader,"shapeSize",box.w,box.h);
        uni2(shader,"sampleOffset",box.x-x0,box.y-y0);uni2(shader,"sampleSize",x1-x0,y1-y0);uni2(shader,"storageSize",roi.w,roi.h);
        uni4(shader,"radii",radii[0],radii[1],radii[2],radii[3]);uni4(shader,"tint",0,0,0,0);
        uni(shader,"bezel",lens.bezelPx);uni(shader,"frostMix",.2f);uni(shader,"specular",.4f);uni(shader,"opacity",1);
        glUniform4f(glGetUniformLocation(shader,"bezelSides"),lens.bezelPx,lens.bezelPx,lens.bezelPx,lens.bezelPx);glUniform3f(glGetUniformLocation(shader,"lensPhysical"),lens.bezelPx>0?lens.displacementPx/lens.bezelPx:0.f,lens.chromaticPx,lens.lipPx);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
        if(clipTop>0) {glEnable(GL_SCISSOR_TEST);glScissor(int(std::ceil(box.x)),int(std::ceil(box.y+clipTop)),int(box.w),int(box.h-clipTop));}
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisable(GL_SCISSOR_TEST);
    };
    auto sdf=[](float x,float y,Rect box,std::array<float,4> r) {
        x-=box.x+box.w*.5f;y-=box.y+box.h*.5f;
        const float radius=x<0 ? (y<0 ? r[0] : r[3]) : (y<0 ? r[1] : r[2]);
        const float qx=std::abs(x)-box.w*.5f+radius,qy=std::abs(y)-box.h*.5f+radius;
        return std::min(std::max(qx,qy),0.f)+std::hypot(std::max(qx,0.f),std::max(qy,0.f))-radius;
    };
    auto circular=[](float d,float b) {
        const float x=1-std::clamp(d/b,0.f,1.f),eps=std::clamp(.5f/b,.0001f,.5f),top=std::sqrt(1+eps);
        return (top-std::sqrt(std::max(1-x*x,0.f)+eps))/(top-std::sqrt(eps));
    };
    std::vector<Pixel> pattern(width*height);
    for(int frequency : {3,7,13,23}) {
        for(int row=0;row<height;++row) for(int col=0;col<width;++col)
            pattern[row*width+col]=Pixel{static_cast<unsigned char>(std::round(255.f*col/(width-1))),
                                        static_cast<unsigned char>(std::round(255.f*row/(height-1))),
                                        static_cast<unsigned char>(((row/frequency+col/frequency)&1) ? 240 : 16),255};
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene.tex);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pattern.data());
        for(float scale : {.75f,1.f,1.25f}) for(int kind=0;kind<3;++kind) for(bool fractional : {false,true}) {
            const Rect box{fractional ? 197.13f : 197.f,fractional ? 99.27f : 99.f,(kind==2 ? 40.f : 120.f)*scale,40.f*scale};
            const std::array<float,4> r=kind==0 ? std::array<float,4>{20*scale,20*scale,20*scale,20*scale} :
                kind==1 ? std::array<float,4>{20*scale,4*scale,4*scale,20*scale} : std::array<float,4>{4*scale,20*scale,20*scale,4*scale};
            const auto physical=GlassShapeLens::geometry(box.w/scale,box.h/scale,{r[0]/scale,r[1]/scale,r[2]/scale,r[3]/scale},scale);
            reset(scene,output);draw(output,scene,box,r,scale,5);
            reset(scene,normal);draw(normal,scene,box,r,scale,2);
            reset(scene,field);draw(field,scene,box,r,scale,1);
            reset(scene,bezelView);draw(bezelView,scene,box,r,scale,3);
            const auto a=pixels(output),n=pixels(normal),s=pixels(field),b=pixels(bezelView);
            const std::array<std::array<int,2>,4> probes{{{int(box.x+box.w/2),int(box.y+1)},
                {int(box.x+box.w/2),int(std::ceil(box.y+box.h))-2},{int(box.x+1),int(box.y+box.h/2)},
                {int(std::ceil(box.x+box.w))-2,int(box.y+box.h/2)}}};
            std::array<float,4> pulls{};
            for(int face=0;face<4;++face) {
                const auto p=probes[face];const float x=p[0]+.5f,y=p[1]+.5f;
                const int i=p[1]*width+p[0];
                const float distance=-sdf(x,y,box,r);
                float nx=-(sdf(x+.5f,y,box,r)-sdf(x-.5f,y,box,r));
                float ny=-(sdf(x,y+.5f,box,r)-sdf(x,y-.5f,box,r));
                const float magnitude=std::hypot(nx,ny);assert(magnitude>.95f);nx/=magnitude;ny/=magnitude;
                assert(std::abs((n[i][0]/255.f-.5f)*2-nx)<.015f);
                assert(std::abs((n[i][1]/255.f-.5f)*2-ny)<.015f);
                assert(std::abs(s[i][0]/255.f-std::clamp(.5f+distance/(2*physical.bezelPx),0.f,1.f))<.006f);
                const float profile=.25f*(circular(distance-.375f,physical.bezelPx)+circular(distance-.125f,physical.bezelPx)
                                           +circular(distance+.125f,physical.bezelPx)+circular(distance+.375f,physical.bezelPx));
                assert(profile>.25f);assert(std::abs(b[i][0]/255.f-profile)<.006f);
                const float dx=(int(a[i][0])-int(pattern[i][0]))*(width-1)/255.f;
                const float dy=(int(a[i][1])-int(pattern[i][1]))*(height-1)/255.f;
                pulls[face]=dx*nx+dy*ny;
                assert(pulls[face]>2.f && std::abs(pulls[face]-profile*physical.displacementPx)<2.1f);
            }
            if(!fractional && scale==1) {
                assert(std::abs(pulls[0]-pulls[1])<1.6f);
                assert(std::abs(pulls[2]-pulls[3])<2.1f); // Different gradient quantization axis, <=1 LSB.
            }
            if(frequency==7 && scale==1 && !fractional)
                std::cout<<"PASS padded-ROI "<<(kind==0 ? "round pill" : kind==1 ? "split left" : "split right")
                         <<" TOP/BOTTOM/LEFT/RIGHT inward pull = "<<pulls[0]<<"/"<<pulls[1]<<"/"<<pulls[2]<<"/"<<pulls[3]<<" px, SDF/profile/normals verified\n";
        }
        // Actual staged parent->left->right sequence, with a gap. Nonoverlap
        // sibling draws cannot erase the left top edge, even when ROI padding overlaps.
        reset(scene,parent);draw(parent,parent,{20,20,472,270},{16,16,16,16},1,5);
        reset(parent,stack);const Rect left{302,68,120,40},right{426,68,40,40};
        draw(stack,stack,left,{20,4,4,20},1,5);const auto beforeRight=pixels(stack);
        draw(stack,stack,right,{4,20,20,4},1,5);const auto afterRight=pixels(stack);
        for(int row=68;row<108;++row) for(int col=302;col<422;++col)
            assert(afterRight[row*width+col]==beforeRight[row*width+col]);
        // A real top clip cuts output only: never invent a new lens/normal at
        // the clipping cut to disguise metadata that removed an optical edge.
        reset(scene,output);draw(output,scene,left,{20,4,4,20},1,5,5);
        const auto clipped=pixels(output);
        for(int row=68;row<73;++row) for(int col=302;col<422;++col)
            assert(clipped[row*width+col]==pattern[row*width+col]);
    }
    // The composed material can be optically neutral at a particular texture
    // feature despite a nonzero child sample-address displacement. Scan a
    // control near the parent's bezel using an independent monotonic gradient,
    // not a checker frequency whose period happens to match the excursion.
    float smallestChange=1000;int neutralInset=-1;
    for(int inset=1;inset<=24;++inset) {
        const Rect child{302,float(20+inset),120,40};
        reset(parent,stack);draw(stack,stack,child,{20,4,4,20},1,5);
        const auto a=pixels(stack),p=pixels(parent);
        const int probe=(int(child.y)+1)*width+int(child.x+child.w/2);
        const float change=std::abs(int(a[probe][1])-int(p[probe][1]))*(height-1)/255.f;
        if(change<smallestChange) {smallestChange=change;neutralInset=inset;}
    }
    assert(smallestChange<=1.3f);
    std::cout<<"OPTICAL STACK REPRO: nonzero split top lens (~7px sample bend) yields "<<smallestChange
             <<" px visible gradient change vs parent at parent-bezel inset "<<neutralInset
             <<" px. This is content/parent remapping cancellation, NOT a missing edge or a fix.\n";
    std::cout<<"PASS all four edges at four checker frequencies/three fractional scales, parent+split sibling order, poisoned initialized ROI and flat clipping\n";
}
int main(int argc,char** argv){
    assert(argc==2);shaderDirectory=argv[1];
    EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;assert(eglInitialize(d,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(d,attrs,&config,1,&count)&&count);
    EGLint pb[]={EGL_WIDTH,W,EGL_HEIGHT,H,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    auto surface=eglCreatePbufferSurface(d,config,pb),context=eglCreateContext(d,config,EGL_NO_CONTEXT,ca);assert(eglMakeCurrent(d,surface,surface,context));
    std::cout<<"Renderer: "<<glGetString(GL_RENDERER)<<"\n";
    GLuint vao,vbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);float quad[]={0,0,1,0,0,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_STATIC_DRAW);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);glEnableVertexAttribArray(0);
    auto lens=program("liquidshape.frag"),frost=program("shapefrost.frag"),foreground=program("shapeforeground.frag"),panel=program("liquidglass.frag");
    FB target,source,soft(128,128),label,poison(128,128);
    clear(target,.2,.4,.6);copy(target,source);
    // A control's center samples the CURRENT target, including parent glass.
    drawShape(lens,target,source,0,1,1,.5);auto first=pixel(target,48,32);
    copy(target,source);drawShape(lens,target,source);auto second=pixel(target,48,32);
    assert(first==second);assert(first[0]>=152&&first[0]<=154&&first[1]>=50&&first[1]<=52);
    // Rounded shape corner: no glass outside exact TL radius; square BR stays.
    clear(target,0,0,0);clear(source,1,1,1);drawShape(lens,target,source);
    assert(pixel(target,16,8)[0]==0);assert(pixel(target,79,55)[0]>230);
    // Analytical normal/debug at the top and right are consistent, finite.
    clear(target,0,0,0);drawShape(lens,target,source,2);
    auto top=pixel(target,48,8),right=pixel(target,79,32);
    assert(top[0]>=126&&top[0]<=129&&top[1]==255);assert(right[0]==0&&right[1]>=126&&right[1]<=129);
    // Flat clipping cut doesn't create a rounded edge or new lens normal.
    clear(target,0,0,0);glEnable(GL_SCISSOR_TEST);glScissor(48,0,W-48,H);drawShape(lens,target,source);
    assert(pixel(target,47,32)[0]==0);assert(pixel(target,48,32)[0]==255);glDisable(GL_SCISSOR_TEST);
    // Opacity affects the WHOLE contribution, not just tint/reflections.
    clear(target,0,0,0);drawShape(lens,target,source,0,.25);
    auto faded=pixel(target,48,32);assert(faded[0]>=63&&faded[0]<=65);
    // Frost respects initialized subregion; retained capacity is magenta poison.
    clear(soft,1,0,1);clear(source,.2,.4,.6);
    clear(poison,1,0,1);glEnable(GL_SCISSOR_TEST);glScissor(0,0,W,H);glClearColor(.2,.4,.6,1);glClear(GL_COLOR_BUFFER_BIT);glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER,soft.fb);glViewport(0,0,W,H);glDisable(GL_BLEND);glUseProgram(frost);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,poison.tex);
    uni4(frost,"box",0,0,1,1);uni2(frost,"sampleSize",W,H);uni2(frost,"storageSize",128,128);uni(frost,"radius",2.5);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    assert(pixel(soft,0,0)==pixel(source,0,0));assert(pixel(soft,W-1,H-1)==pixel(source,W-1,H-1));assert(pixel(soft,110,110)[0]==255);
    // Labels/fills are untouched premultiplied-over, after all glass passes.
    clear(target,.2,.4,.6);clear(label,.4,.1,0,.5);
    glBindFramebuffer(GL_FRAMEBUFFER,target.fb);glViewport(0,0,W,H);glUseProgram(foreground);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,label.tex);
    uni4(foreground,"box",0,0,1,1);uni2(foreground,"uvOffset",0,0);uni2(foreground,"uvScale",1,1);glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    auto result=pixel(target,48,32);assert(std::abs(int(result[0])-128)<=1&&std::abs(int(result[1])-77)<=1&&std::abs(int(result[2])-77)<=1&&result[3]==255);
    // Panel split must sample glass independently of foreground opacity. With
    // an opaque red client, native glass-only is still blue BEFORE foreground;
    // legacy panelOnly=0 remains the original opaque red one-pass composite.
    clear(source,.2,.4,.6);clear(label,1,0,0);clear(poison,1,1,1);clear(target,0,0,0);
    glBindFramebuffer(GL_FRAMEBUFFER,target.fb);glViewport(0,0,W,H);glUseProgram(panel);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,source.tex);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,label.tex);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,poison.tex);glActiveTexture(GL_TEXTURE0);
    auto integer=[&](const char* n,int v){glUniform1i(glGetUniformLocation(panel,n),v);};
    integer("tex",0);integer("maskTex",1);integer("sdfTex",2);integer("useMask",1);integer("useSilhouette",1);
    integer("maskMode",1);integer("regionRectCount",1);integer("panelOnly",1);
    uni4(panel,"regionRects[0]",0,0,W,H);uni4(panel,"box",0,0,1,1);
    uni2(panel,"fullSize",W,H);uni2(panel,"invFullSize",1.f/W,1.f/H);uni2(panel,"maskUVScale",1,1);uni2(panel,"sampleUVScale",1,1);
    uni(panel,"silThreshold",.05);uni(panel,"sdfPxScale",100);uni(panel,"glassOpacity",1);uni(panel,"invBezelWidthPx",.125);
    uni(panel,"brightness",1);uni(panel,"contrast",1);uni(panel,"saturation",1);uni(panel,"monitorScale",1);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);assert(pixel(target,48,32)==pixel(source,48,32));
    glBindFramebuffer(GL_FRAMEBUFFER,target.fb);integer("panelOnly",0);glDrawArrays(GL_TRIANGLE_STRIP,0,4);assert(pixel(target,48,32)==pixel(label,48,32));
    performanceRegression(lens,frost);
    ownerLensRegression(lens);
    fourEdgeRegression(lens);
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS current-target parent sampling, asymmetric corners, normals, flat clips, opacity, bounded frost, premultiplied foreground, panel-only/legacy algebra\n";
    eglMakeCurrent(d,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(d,context);eglDestroySurface(d,surface);eglTerminate(d);
}
