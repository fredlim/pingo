#include <string.h>

#include <stdio.h>

#include "renderer.h"

#include "sprite.h"

#include "pixel.h"

#include "backend.h"

#include "scene.h"

#include "rasterizer.h"

#include "object.h"


int renderFrame(Renderer * r, Renderable ren) {
    Texture * f = ren.impl;
    return rasterizer_draw_pixel_perfect((Vec2i) {
                                             0,
                                             0
                                         }, r, f);
};

int renderSprite(Mat4 transform, Renderer * r, Renderable ren) {
    Sprite * s = ren.impl;
    Mat4 backUp = s -> t;

    //Apply parent transform to the local transform
    s -> t = mat4MultiplyM( & s -> t, & transform);

    //Apply camera translation
    Mat4 newMat = mat4Translate((Vec3f) {
                                    -r -> camera.x, -r -> camera.y, 0
                                });
    s -> t = mat4MultiplyM( & s -> t, & newMat);
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

    rasterizer_draw_transformed(s -> t, r, & s -> frame);
    s -> t = backUp;
    return 0;
};

void renderRenderable(Mat4 transform, Renderer * r, Renderable ren) {
    renderingFunctions[ren.renderableType](transform, r, ren);
};

int renderScene(Mat4 transform, Renderer * r, Renderable ren) {
    Scene * s = ren.impl;
    if (!s -> visible)
        return 0;

    //Apply hierarchy transfom
    Mat4 newTransform = mat4MultiplyM( & s -> transform, & transform);
    for (int i = 0; i < s -> numberOfRenderables; i++) {
        renderRenderable(newTransform, r, s -> renderables[i]);
    }
    return 0;
};

#define MIN(a, b)(((a) < (b)) ? (a) : (b))
#define MAX(a, b)(((a) > (b)) ? (a) : (b))

float edgeFunction(const Vec2f * a, const Vec2f * b, const Vec2f * c) {
    return (c -> x - a -> x) * (b -> y - a -> y) - (c -> y - a -> y) * (b -> x - a -> x);
}

float isClockWise(float x1, float y1, float x2, float y2, float x3, float y3) {
    return (y2 - y1) * (x3 - x2) - (y3 - y2) * (x2 - x1);
}

int renderObject(Mat4 object_transform, Renderer * r, Renderable ren) {
    Object * o = ren.impl;
    //MODEL MATRIX
    Mat4 m = mat4MultiplyM( &o->transform, &object_transform  );

    //VIEW MATRIX
    Mat4 v = r->camera_view;

    Mat4 p = r -> camera_projection;

    Mat4 t = mat4MultiplyM( & m, &v);
    Mat4 mvp = mat4MultiplyM( & t, &p);

    for (int i = 0; i < o->mesh->indexes_count; i += 3) {
        Vec3f * ver1 = &o->mesh->positions[o->mesh->indexes[i+0]];
        Vec3f * ver2 = &o->mesh-> positions[o->mesh->indexes[i+1]];
        Vec3f * ver3 = &o->mesh->positions[o->mesh->indexes[i+2]];

        Vec4f a =  { ver1->x, ver1->y, ver1->z, 1 };
        Vec4f b =  { ver2->x, ver2->y, ver2->z, 1 };
        Vec4f c =  { ver3->x, ver3->y, ver3->z, 1 };

        a = mat4MultiplyVec4( &a, &mvp);
        b = mat4MultiplyVec4( &b, &mvp);
        c = mat4MultiplyVec4( &c, &mvp);

        // convert to device coordinates by perspective division
        a.w = 1.0 / a.w;
        b.w = 1.0 / b.w;
        c.w = 1.0 / c.w;
        a.x *= a.w; a.y *= a.w; a.z *= a.w;
        b.x *= b.w; b.y *= b.w; b.z *= b.w;
        c.x *= c.w; c.y *= c.w; c.z *= c.w;

        if (isClockWise(a.x, a.y, b.x, b.y, c.x, c.y) > 0)
            continue;

        //COMPUTE SCREEN COORDS
        Vec3f a_s = {(a.x+1.0) * (1366/2),(a.y+1.0) * (768/2),a.z};
        Vec3f b_s = {(b.x+1.0) * (1366/2),(b.y+1.0) * (768/2),b.z};
        Vec3f c_s = {(c.x+1.0) * (1366/2),(c.y+1.0) * (768/2),c.z};

        float minX = MIN(MIN(a_s.x, b_s.x), c_s.x);
        float minY = MIN(MIN(a_s.y, b_s.y), c_s.y);
        float maxX = MAX(MAX(a_s.x, b_s.x), c_s.x);
        float maxY = MAX(MAX(a_s.y, b_s.y), c_s.y);

        minX = MIN(MAX(minX, 0), r -> frameBuffer.size.x);
        maxX = MIN(MAX(maxX, 0), r -> frameBuffer.size.x);
        minY = MIN(MAX(minY, 0), r -> frameBuffer.size.y);
        maxY = MIN(MAX(maxY, 0), r -> frameBuffer.size.y);

        for (int y = minY; y < maxY; y++) {
            for (int x = minX; x < maxX; x++) {
                //Transform the coordinate back to sprite space
                Vec2f desPosF = { x , y };

                // area of the triangle multiplied by 2
                float area = edgeFunction( (Vec2f*)&a_s, (Vec2f*)&b_s, (Vec2f*)&c_s);

                //Area of sub triangles
                float ba = edgeFunction( (Vec2f*)&b_s, (Vec2f*)&c_s, (Vec2f*)&desPosF) / area;
                float bc = edgeFunction( (Vec2f*)&a_s, (Vec2f*)&b_s, (Vec2f*)&desPosF) / area;
                float bb = 1.0 - bc - ba; //edgeFunction(&c, &a, &desPosF) / area;

                // If all the areas are positive then point is inside triangle
                if (ba < 0 || bb < 0 || bc < 0)
                    continue;

                float depth = 10000000 * ((ba * a.z * a.w + bb * b.z * b.w + bc * c.z * c.w) / (ba * a.w + bb * b.w + bc * c.w));

                if (zbuffer[x][y] > depth)
                    continue;

                //texture_draw( & r -> frameBuffer, desPos, pixelFromRGBA(depth*255, depth*255, depth*255, 255));
                texture_draw(&r->frameBuffer, vecFtoI(desPosF), pixelFromRGBA(ba*255,bb*255,bc*255,255));
                //texture_draw(&r->frameBuffer, desPos, pixelFromRGBA(255,255,255,255));
                zbuffer[x][y] = depth;
            }
        }

    }

    return 0;
};

int rendererInit(Renderer * r, Vec2i size, BackEnd * backEnd) {
    renderingFunctions[RENDERABLE_SPRITE] = & renderSprite;
    renderingFunctions[RENDERABLE_SCENE] = & renderScene;
    renderingFunctions[RENDERABLE_OBJECT] = & renderObject;

    r -> scene = 0;
    r -> clear = 1;
    r -> clearColor = PIXELBLACK;
    r -> backEnd = backEnd;

    r -> backEnd -> init(r, r -> backEnd, (Vec4i) {
                             0,
                             0,
                             0,
                             0
                         });

    int e = 0;
    e = texture_init( & (r -> frameBuffer), size, backEnd -> getFrameBuffer(r, backEnd));
    if (e) return e;

    return 0;
}

/**
 * @brief Clear the whole framebuffer slowly. This prevents underrun in the FPGA draw buffer and prevents desynchronization
 * of the video signal
 */
void clearBufferSlowly(Texture f) {
    int length = f.size.x * 1;

    for (int y = 0; y < f.size.y; y++) {
        memset(f.frameBuffer + length * y, 0, length * 4);
    }
}

int rendererRender(Renderer * r) {
    for (int i = 1366 * 768; i > 0; --i) {
        zbuffer[0][i] = -2000000000;
    }

    r -> backEnd -> beforeRender(r, r -> backEnd);

    //get current framebuffe from backend
    r -> frameBuffer.frameBuffer = r -> backEnd -> getFrameBuffer(r, r -> backEnd);

    //Clear draw buffer before rendering
    if (r -> clear) {
        clearBufferSlowly(r -> frameBuffer);
    }

    renderScene(mat4Identity(), r, sceneAsRenderable(r -> scene));

    r -> backEnd -> afterRender(r, r -> backEnd);

    return 0;
}

int rendererSetScene(Renderer * r, Scene * s) {
    if (s == 0)
        return 1; //nullptr scene

    r -> scene = s;
    return 0;
}

int rendererSetCamera(Renderer * r, Vec4i rect) {
    r -> camera = rect;
    r -> backEnd -> init(r, r -> backEnd, rect);
    r -> frameBuffer.size = (Vec2i) {
            rect.z, rect.w
};
    return 0;
}
