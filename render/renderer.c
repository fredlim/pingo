#include <string.h>
#include <stdio.h>

#include "renderer.h"
#include "sprite.h"
#include "pixel.h"
#include "backend.h"
#include "scene.h"
#include "rasterizer.h"
#include "object.h"


int renderFrame(Renderer *r, Renderable ren) {
    Texture * f = ren.impl;
    return rasterizer_draw_pixel_perfect((Vec2i){0,0},r,f);
};

int renderSprite(Mat4 transform, Renderer *r, Renderable ren) {
    Sprite * s = ren.impl;
    Mat4 backUp = s->t;

    //Apply parent transform to the local transform
    s->t = mat4MultiplyM( &s->t, &transform);

    //Apply camera translation
    Mat4 newMat = mat4Translate((Vec3f){-r->camera.x,-r->camera.y,0});
    s->t = mat4MultiplyM(&s->t, &newMat);
/*
    if (mat4IsOnlyTranslation(&s->t)) {
        Vec2i off = {s->t.elements[2], s->t.elements[5]};
        rasterizer_draw_pixel_perfect(off,r, &s->frame);
        s->t = backUp;
        return 0;
    }

    if (mat4IsOnlyTranslationDoubled(&s->t)) {
        Vec2i off = {s->t.elements[2], s->t.elements[5]};
        rasterizer_draw_pixel_perfect_doubled(off,r, &s->frame);
        s->t = backUp;
        return 0;
    }*/

    rasterizer_draw_transformed(s->t,r,&s->frame);
    s->t = backUp;
    return 0;
};



void renderRenderable(Mat4 transform, Renderer *r, Renderable ren) {
    renderingFunctions[ren.renderableType](transform, r, ren);
};

int renderScene(Mat4 transform, Renderer *r, Renderable ren) {
    Scene * s = ren.impl;
    if (!s->visible)
        return 0;

    //Apply hierarchy transfom
    Mat4 newTransform = mat4MultiplyM(&s->transform,&transform);
    for (int i = 0; i < s->numberOfRenderables; i++) {
        renderRenderable(newTransform, r, s->renderables[i]);
    }
    return 0;
};

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))


int renderObject(Mat4 object_transform, Renderer *r, Renderable ren) {
    Object * o = ren.impl;
    //Apply model transform to hierarchy transform
    Mat4 model = mat4MultiplyM(&o->transform,&object_transform);
    Mat4 modelview = mat4MultiplyM(  &model, &r->camera_transform);
    //Apply camera transform

    for (int i = 0; i < o->mesh.vertices_count; i++) {
        Vec3f trans_vert = mat4MultiplyVec3(&o->mesh.vertices[i], &modelview);

        //Then clamp max/min values to destination buffer
        trans_vert.x = MIN(r->frameBuffer.size.x, MAX(trans_vert.x, 0));
        trans_vert.y = MIN(r->frameBuffer.size.y, MAX(trans_vert.y, 0));

        texture_draw(&r->frameBuffer, (Vec2i){trans_vert.x,trans_vert.y}, pixelFromUInt8(200));
    }

    return 0;
};

int rendererInit(Renderer * r, Vec2i size, BackEnd * backEnd) {
    renderingFunctions[RENDERABLE_SPRITE] = &renderSprite;
    renderingFunctions[RENDERABLE_SCENE] = &renderScene;
    renderingFunctions[RENDERABLE_OBJECT] = &renderObject;

    r->scene = 0;
    r->clear = 1;
    r->clearColor = PIXELBLACK;
    r->backEnd = backEnd;

    r->backEnd->init(r, r->backEnd, (Vec4i){0,0,0,0});

    int e = 0;
    e = texture_init(&(r->frameBuffer), size, backEnd->getFrameBuffer(r, backEnd));
    if (e) return e;


    return 0;
}

/**
 * @brief Clear the whole framebuffer slowly. This prevents underrun in the FPGA draw buffer and prevents desynchronization
 * of the video signal
 */
void clearBufferSlowly (Texture f)
{
    int length = f.size.x*sizeof (Pixel);
    for (int y = 0; y < f.size.y; y++) {
        uint16_t offset = f.size.x*sizeof (Pixel);
        memset(f.frameBuffer+offset*y,0,length);
    }
}


int rendererRender(Renderer * r)
{
    r->backEnd->beforeRender(r, r->backEnd);

    //get current framebuffe from backend
    r->frameBuffer.frameBuffer= r->backEnd->getFrameBuffer(r, r->backEnd);

    //Clear draw buffer before rendering
    if (r->clear) {
        clearBufferSlowly(r->frameBuffer);
    }

    renderScene(mat4Identity(), r, sceneAsRenderable(r->scene));

    r->backEnd->afterRender(r, r->backEnd);

    return 0;
}

int rendererSetScene(Renderer *r, Scene *s)
{
    if (s == 0)
        return 1; //nullptr scene

    r->scene = s;
    return 0;
}

int rendererSetCamera(Renderer *r, Vec4i rect)
{
    r->camera = rect;
    r->backEnd->init(r, r->backEnd, rect);
    r->frameBuffer.size = (Vec2i){rect.z, rect.w};
    return 0;
}
