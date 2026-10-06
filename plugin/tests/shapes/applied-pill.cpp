// Reuse the existing private EGL helpers, not a second compositor implementation.
#define main shape_baseline_main
#include "offscreen.cpp"
#undef main
#include "../../src/GlassShapes.hpp"
#include <algorithm>
#include <numeric>
struct STracedShape {int index; GlassShapes::SGlassShape shape;};
#include "applied-fixture.hpp"

int main(int argc,char** argv) {
    assert(argc==2);shaderDirectory=argv[1];
    EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);EGLint major,minor;
    assert(eglInitialize(display,&major,&minor));assert(eglBindAPI(EGL_OPENGL_ES_API));
    EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
    EGLConfig config;EGLint count;assert(eglChooseConfig(display,attributes,&config,1,&count)&&count);
    EGLint pb[]={EGL_WIDTH,1600,EGL_HEIGHT,1000,EGL_NONE},ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    auto surface=eglCreatePbufferSurface(display,config,pb),context=eglCreateContext(display,config,EGL_NO_CONTEXT,ca);
    assert(eglMakeCurrent(display,surface,surface,context));
    std::cout<<"APPLIED generation "<<TRACE_GENERATION<<", all 28 exact submitted shapes; renderer "<<glGetString(GL_RENDERER)<<"\n"
             <<"INPUT LIMIT: synthetic raw Canvas pattern, NOT captured pre-shape panel/foreground textures\n";
    GLuint vao,vbo;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    float quad[]={0,0,1,0,0,1,1,1};glBufferData(GL_ARRAY_BUFFER,sizeof quad,quad,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);glEnableVertexAttribArray(0);
    const auto lens=program("liquidshape.frag"),flat=program("matched-material-flat.frag");
    constexpr int sw=1600,sh=1000;
    FB scene(sw,sh),target(sw,sh),parentOnly(sw,sh),prefix(sw,sh),afterLeft(sw,sh),afterRight(sw,sh),
       comparison(sw,sh),probe(sw,sh),roi(1664,1024);
    auto copyAll=[&](const FB& from,const FB& to) {
        glDisable(GL_SCISSOR_TEST);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,to.fb);
        glBlitFramebuffer(0,0,sw,sh,0,0,sw,sh,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    };
    auto draw=[&](const FB& to,const FB& from,const GlassShapes::SGlassShape& shape,GLuint shader,int view=0) {
        const auto physical=GlassShapeLens::geometry(shape.width,shape.height,shape.radii,1);
        const int pad=int(GlassShapeLens::samplePaddingPx(physical,.2f,1));
        float vx0=std::max(0.f,shape.x),vy0=std::max(0.f,shape.y),vx1=std::min(float(sw),shape.x+shape.width),vy1=std::min(float(sh),shape.y+shape.height);
        if(shape.clip) {const auto c=*shape.clip;vx0=std::max(vx0,c[0]);vy0=std::max(vy0,c[1]);vx1=std::min(vx1,c[0]+c[2]);vy1=std::min(vy1,c[1]+c[3]);}
        if(vx1<=vx0 || vy1<=vy0 || shape.opacity<=0) return;
        const int x0=std::max(0,int(std::floor(vx0))-pad),y0=std::max(0,int(std::floor(vy0))-pad);
        const int x1=std::min(sw,int(std::ceil(vx1))+pad),y1=std::min(sh,int(std::ceil(vy1))+pad);
        clear(roi,1,0,1);glBindFramebuffer(GL_READ_FRAMEBUFFER,from.fb);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,roi.fb);
        glBlitFramebuffer(x0,y0,x1,y1,0,0,x1-x0,y1-y0,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER,to.fb);glViewport(0,0,sw,sh);glUseProgram(shader);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,roi.tex);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,roi.tex);glActiveTexture(GL_TEXTURE0);
        glUniform1i(glGetUniformLocation(shader,"tex"),0);glUniform1i(glGetUniformLocation(shader,"softTex"),1);
        glUniform1i(glGetUniformLocation(shader,"inlineFrost"),1);glUniform1i(glGetUniformLocation(shader,"debugView"),view);
        uni(shader,"inlineRadius",.9f);uni(shader,"frostMix",.2f);uni(shader,"specular",.65f);uni(shader,"opacity",shape.opacity);
        uni4(shader,"box",shape.x/sw,shape.y/sh,shape.width/sw,shape.height/sh);uni2(shader,"shapeSize",shape.width,shape.height);
        uni2(shader,"sampleOffset",shape.x-x0,shape.y-y0);uni2(shader,"sampleSize",x1-x0,y1-y0);uni2(shader,"storageSize",roi.w,roi.h);
        uni4(shader,"radii",shape.radii[0],shape.radii[1],shape.radii[2],shape.radii[3]);
        uni(shader,"bezel",physical.bezelPx);glUniform3f(glGetUniformLocation(shader,"lensPhysical"),physical.displacementPx,physical.chromaticPx,physical.lipPx);
        uni4(shader,"tint",((shape.tint>>24)&255)/255.f,((shape.tint>>16)&255)/255.f,((shape.tint>>8)&255)/255.f,(shape.tint&255)/255.f);
        glEnable(GL_BLEND);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glEnable(GL_SCISSOR_TEST);
        glScissor(int(vx0),int(vy0),int(std::ceil(vx1))-int(vx0),int(std::ceil(vy1))-int(vy0));
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);glDisable(GL_SCISSOR_TEST);
    };
    auto records=TRACE;
    std::stable_sort(records.begin(),records.end(),[](const auto& a,const auto& b){return a.shape.depth<b.shape.depth;});
    std::cout<<"SORT:";for(const auto& r:records)std::cout<<' '<<r.index;std::cout<<"\n";
    const auto& card=TRACE.at(22).shape;const auto& left=TRACE.at(23).shape;const auto& right=TRACE.at(24).shape;
    assert(card.x==1176 && card.y==535 && card.width==408 && card.height==209);
    assert(left.x==1412 && left.y==558 && left.width==114 && left.height==40 && left.radii==std::array<float,4>{20,6,6,20});
    assert(right.x==1528 && right.y==558 && right.width==40 && right.height==40 && right.radii==std::array<float,4>{6,20,20,6});
    std::vector<Pixel> background(sw*sh);
    for(int y=0;y<sh;++y)for(int x=0;x<sw;++x) {
        background[y*sw+x]=((x/12+y/12)&1) ? Pixel{0x72,0x79,0x85,255} : Pixel{0x15,0x1b,0x24,255};
        // Same line centres/width/colors as the staged Canvas; raster AA is a
        // software approximation, explicitly not the compositor's actual prefix.
        const float phase=std::remainder((x+.5f)-(y+.5f)+1000.f,120.f);
        if(std::abs(phase)/std::sqrt(2.f)<=1.5f)background[y*sw+x]=Pixel{0xd8,0x44,0x68,255};
    }
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,scene.tex);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,sw,sh,GL_RGBA,GL_UNSIGNED_BYTE,background.data());
    copyAll(scene,target);
    for(const auto& r:records) {
        if(r.index==23)copyAll(target,prefix);
        draw(target,target,r.shape,lens);
        if(r.index==22)copyAll(target,parentOnly);
        if(r.index==23)copyAll(target,afterLeft);
        if(r.index==24)copyAll(target,afterRight);
    }
    const auto final=pixels(target),p=pixels(parentOnly),pre=pixels(prefix),l=pixels(afterLeft),r=pixels(afterRight);
    for(int y=558;y<598;++y)for(int x=1412;x<1568;++x) {
        const int i=y*sw+x;assert(p[i]==pre[i]); // Other depth-1 elements did not touch recorder prefix.
        if(x<1526)assert(final[i]==l[i]);
        if(x>=1528)assert(final[i]==r[i]); // No later native overdraw of either half.
    }
    copyAll(prefix,comparison);draw(comparison,comparison,left,flat);draw(comparison,comparison,right,flat);
    const auto noLens=pixels(comparison);
    for(const auto index:{23,24}) {
        const auto& s=TRACE[index].shape;
        const std::array<std::array<int,4>,4> bands{{{int(s.x+20),int(s.y),int(s.x+s.width-6),int(s.y+12)},
            {int(s.x+20),int(s.y+s.height-12),int(s.x+s.width-6),int(s.y+s.height)},
            {int(s.x),int(s.y+16),int(s.x+12),int(s.y+s.height-16)},
            {int(s.x+s.width-12),int(s.y+16),int(s.x+s.width),int(s.y+s.height-16)}}};
        for(int face=0;face<4;++face) {
            const auto b=bands[face];int count=0,changed=0,maximum=0;double error=0;
            for(int y=b[1];y<b[3];++y)for(int x=b[0];x<b[2];++x) {
                const int i=y*sw+x;int delta=0;for(int c=0;c<3;++c)delta=std::max(delta,std::abs(int(final[i][c])-int(noLens[i][c])));
                ++count;changed+=delta>2;maximum=std::max(maximum,delta);error+=delta;
            }
            assert(count>0 && changed>0);
            std::cout<<"SHAPE "<<index<<' '<<std::array<const char*,4>{"TOP","BOTTOM","LEFT","RIGHT"}[face]
                     <<" vs identical paint/flat optics: "<<changed<<'/'<<count<<" pixels changed >2LSB; mean="<<error/count<<" max="<<maximum<<" LSB\n";
        }
        const auto physical=GlassShapeLens::geometry(s.width,s.height,s.radii,1);
        copyAll(prefix,probe);draw(probe,prefix,s,lens,2);const auto normals=pixels(probe);
        copyAll(prefix,probe);draw(probe,prefix,s,lens,3);const auto profiles=pixels(probe);
        const int x=int(s.x+s.width/2),y=int(s.y+1),i=y*sw+x;
        assert(normals[i][1]>250 && profiles[i][0]>100);
        std::cout<<"SHAPE "<<index<<" TOP sample address: inwardY="<<(normals[i][1]/255.f-.5f)*2
                 <<" profile="<<profiles[i][0]/255.f<<" displacementY~="<<(normals[i][1]/255.f-.5f)*2*profiles[i][0]/255.f*physical.displacementPx<<"px\n";
    }
    assert(glGetError()==GL_NO_ERROR);
    std::cout<<"PASS exact APPLIED list: parent22 precedes23/24, full clip, all edges nonzero, no native overwrite\n"
             <<"REMAINING INPUTS: actual post-panel texture and final client foreground alpha/color not in metadata; real screenshot cause not declared fixed\n";
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}
