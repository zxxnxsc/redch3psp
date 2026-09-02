#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <pspkernel.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../rwbase.h"
#include "../rwerror.h"
#include "../rwplg.h"
#include "../rwpipeline.h"
#include "../rwobjects.h"
#include "../rwengine.h"
#include "../rwrender.h"
#include "rwpsp.h"

namespace rw { namespace psp {

static unsigned int __attribute__((aligned(16))) guList[262144];
static void *fb0 = (void*)0;
static void *fb1 = (void*)(512 * 272 * 4);
static void *zb  = (void*)(512 * 272 * 4 * 2);
static int currentBuffer = 0;
static void *renderStates[64];
static Im3DVertex *im3dVerts = nil;
static int32 im3dNumVerts = 0;

static int guPrimitive(PrimitiveType p)
{
    switch(p) {
    case PRIMTYPELINELIST:  return GU_LINES;
    case PRIMTYPEPOLYLINE:  return GU_LINE_STRIP;
    case PRIMTYPETRILIST:   return GU_TRIANGLES;
    case PRIMTYPETRISTRIP:  return GU_TRIANGLE_STRIP;
    case PRIMTYPETRIFAN:    return GU_TRIANGLE_FAN;
    case PRIMTYPEPOINTLIST: return GU_POINTS;
    default: return GU_POINTS;
    }
}

static void beginUpdate(Camera*)
{
    sceGuStart(GU_DIRECT, guList);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
}

static void endUpdate(Camera*)
{
    sceGuFinish();
    sceGuSync(0, 0);
}

static void clearCamera(Camera*, RGBA *c, uint32 mode)
{
    uint32 flags = 0;
    if(mode & 1) flags |= GU_COLOR_BUFFER_BIT;
    if(mode & 2) flags |= GU_STENCIL_BUFFER_BIT;
    if(mode & 4) flags |= GU_DEPTH_BUFFER_BIT;
    if(flags == 0) flags = GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT;
    const uint32 color = c ? RWRGBAINT(c->red, c->green, c->blue, c->alpha) : 0xFF000000u;
    sceGuClearColor(color);
    sceGuClearDepth(0);
    sceGuClear(flags);
}

static void showRaster(Raster*, uint32)
{
    sceGuSwapBuffers();
    currentBuffer ^= 1;
}

static bool32 rasterRenderFast(Raster*, int32, int32) { return 0; }

static void setRenderState(int32 state, void *value)
{
    if(state >= 0 && state < (int32)(sizeof(renderStates)/sizeof(renderStates[0])))
        renderStates[state] = value;

    const uintptr v = (uintptr)value;
    switch(state) {
    case ZTESTENABLE:
        if(v) sceGuEnable(GU_DEPTH_TEST); else sceGuDisable(GU_DEPTH_TEST);
        break;
    case ZWRITEENABLE:
        sceGuDepthMask(v ? GU_FALSE : GU_TRUE);
        break;
    case VERTEXALPHA:
        if(v) sceGuEnable(GU_BLEND); else sceGuDisable(GU_BLEND);
        break;
    case CULLMODE:
        if(v == CULLNONE) sceGuDisable(GU_CULL_FACE);
        else {
            sceGuEnable(GU_CULL_FACE);
            sceGuFrontFace(v == CULLFRONT ? GU_CW : GU_CCW);
        }
        break;
    default:
        break;
    }
}

static void *getRenderState(int32 state)
{
    if(state >= 0 && state < (int32)(sizeof(renderStates)/sizeof(renderStates[0])))
        return renderStates[state];
    return nil;
}

static void draw2D(PrimitiveType prim, Im2DVertex *src, int32 count, const uint16 *indices, int32 indexCount)
{
    if(src == nil || count <= 0) return;
    const int n = indices ? indexCount : count;
    if(n <= 0) return;

    struct GuV { float u, v; uint32 color; float x, y, z; };
    GuV *dst = (GuV*)sceGuGetMemory(sizeof(GuV) * n);
    if(dst == nil) return;
    for(int i = 0; i < n; i++) {
        int j = indices ? indices[i] : i;
        if(j < 0 || j >= count) j = 0;
        dst[i].u = src[j].u;
        dst[i].v = src[j].v;
        dst[i].color = src[j].color;
        dst[i].x = src[j].x;
        dst[i].y = src[j].y;
        dst[i].z = src[j].z;
    }
    sceGumDrawArray(guPrimitive(prim), GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
                    n, 0, dst);
}

static void im2DRenderLine(void *v, int32 n, int32 a, int32 b)
{
    uint16 idx[2] = { (uint16)a, (uint16)b };
    draw2D(PRIMTYPELINELIST, (Im2DVertex*)v, n, idx, 2);
}

static void im2DRenderTriangle(void *v, int32 n, int32 a, int32 b, int32 c)
{
    uint16 idx[3] = { (uint16)a, (uint16)b, (uint16)c };
    draw2D(PRIMTYPETRILIST, (Im2DVertex*)v, n, idx, 3);
}

static void im2DRenderPrimitive(PrimitiveType p, void *v, int32 n)
{
    draw2D(p, (Im2DVertex*)v, n, nil, 0);
}

static void im2DRenderIndexedPrimitive(PrimitiveType p, void *v, int32 n, void *idx, int32 ni)
{
    draw2D(p, (Im2DVertex*)v, n, (const uint16*)idx, ni);
}

static void im3DTransform(void *vertices, int32 numVertices, Matrix*, uint32)
{
    im3dVerts = (Im3DVertex*)vertices;
    im3dNumVerts = numVertices;
}

static void draw3D(PrimitiveType prim, const uint16 *indices, int32 indexCount)
{
    if(im3dVerts == nil || im3dNumVerts <= 0) return;
    const int n = indices ? indexCount : im3dNumVerts;
    if(n <= 0) return;

    struct GuV { float u, v; uint32 color; float nx, ny, nz; float x, y, z; };
    GuV *dst = (GuV*)sceGuGetMemory(sizeof(GuV) * n);
    if(dst == nil) return;
    for(int i = 0; i < n; i++) {
        int j = indices ? indices[i] : i;
        if(j < 0 || j >= im3dNumVerts) j = 0;
        const Im3DVertex &s = im3dVerts[j];
        dst[i].u = s.texCoords.u; dst[i].v = s.texCoords.v;
        dst[i].color = s.color;
        dst[i].nx = s.normal.x; dst[i].ny = s.normal.y; dst[i].nz = s.normal.z;
        dst[i].x = s.position.x; dst[i].y = s.position.y; dst[i].z = s.position.z;
    }
    sceGumDrawArray(guPrimitive(prim), GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_NORMAL_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
                    n, 0, dst);
}

static void im3DRenderPrimitive(PrimitiveType p) { draw3D(p, nil, 0); }
static void im3DRenderIndexedPrimitive(PrimitiveType p, void *idx, int32 n) { draw3D(p, (const uint16*)idx, n); }
static void im3DEnd(void) { im3dVerts = nil; im3dNumVerts = 0; }

static Raster *rasterCreate(Raster *r)
{
    if(r == nil) return nil;
    r->platform = PLATFORM_PSP;
    if(r->depth == 0) r->depth = 32;
    if(r->width <= 0 || r->height <= 0) return r;
    r->stride = r->width * 4;
    if((r->type & Raster::DONTALLOCATE) == 0 && r->pixels == nil) {
        const size_t bytes = (size_t)r->stride * r->height;
        r->pixels = (uint8*)rwMalloc(bytes, MEMDUR_EVENT | ID_RASTERPSP);
        if(r->pixels == nil) {
            std::fprintf(stderr, "E75 memoria insuficiente para textura PSP bytes=%u\n", (unsigned)bytes);
            return nil;
        }
        std::memset(r->pixels, 0, bytes);
    }
    return r;
}

static uint8 *rasterLock(Raster *r, int32, int32) { return r ? r->pixels : nil; }
static void rasterUnlock(Raster*, int32) {}
static uint8 *rasterLockPalette(Raster *r, int32) { return r ? r->palette : nil; }
static void rasterUnlockPalette(Raster*) {}
static int32 rasterNumLevels(Raster*) { return 1; }

static bool32 imageFindRasterFormat(Image *img, int32, int32 *w, int32 *h, int32 *d, int32 *f)
{
    if(img == nil) return 0;
    *w = img->width; *h = img->height; *d = 32; *f = Raster::C8888;
    return 1;
}

static bool32 rasterFromImage(Raster *r, Image *img)
{
    if(r == nil || img == nil || r->pixels == nil) return 0;
    Image *tmp = img;
    bool destroy = false;
    if(img->depth != 32) {
        tmp = Image::create(img->width, img->height, img->depth);
        if(tmp == nil) return 0;
        tmp->allocate();
        std::memcpy(tmp->pixels, img->pixels, img->stride * img->height);
        tmp->convertTo32();
        destroy = true;
    }
    const int rows = tmp->height < r->height ? tmp->height : r->height;
    const int cols = tmp->width < r->width ? tmp->width : r->width;
    for(int y = 0; y < rows; y++)
        std::memcpy(r->pixels + y*r->stride, tmp->pixels + y*tmp->stride, cols*4);
    if(destroy) tmp->destroy();
    return 1;
}

static Image *rasterToImage(Raster *r)
{
    if(r == nil || r->pixels == nil) return nil;
    Image *img = Image::create(r->width, r->height, 32);
    if(img == nil) return nil;
    img->allocate();
    for(int y = 0; y < r->height; y++)
        std::memcpy(img->pixels + y*img->stride, r->pixels + y*r->stride, r->width*4);
    return img;
}

static void *driverOpen(void *o, int32, int32)
{
    Driver *d = engine->driver[PLATFORM_PSP];
    d->rasterCreate = rasterCreate;
    d->rasterLock = rasterLock;
    d->rasterUnlock = rasterUnlock;
    d->rasterLockPalette = rasterLockPalette;
    d->rasterUnlockPalette = rasterUnlockPalette;
    d->rasterNumLevels = rasterNumLevels;
    d->imageFindRasterFormat = imageFindRasterFormat;
    d->rasterFromImage = rasterFromImage;
    d->rasterToImage = rasterToImage;
    return o;
}

static void *driverClose(void *o, int32, int32) { return o; }

void registerPlatformPlugins(void)
{
    Driver::registerPlugin(PLATFORM_PSP, 0, PLATFORM_PSP, driverOpen, driverClose);
}

static int deviceSystem(DeviceReq req, void *arg, int32 n)
{
    switch(req) {
    case DEVICEOPEN: return 1;
    case DEVICEINIT:
        sceGuInit();
        sceGuStart(GU_DIRECT, guList);
        sceGuDrawBuffer(GU_PSM_8888, fb0, 512);
        sceGuDispBuffer(480, 272, fb1, 512);
        sceGuDepthBuffer(zb, 512);
        sceGuOffset(2048 - 240, 2048 - 136);
        sceGuViewport(2048, 2048, 480, 272);
        sceGuDepthRange(65535, 0);
        sceGuScissor(0, 0, 480, 272);
        sceGuEnable(GU_SCISSOR_TEST);
        sceGuEnable(GU_DEPTH_TEST);
        sceGuDepthFunc(GU_GEQUAL);
        sceGuDepthMask(GU_FALSE);
        sceGuFrontFace(GU_CCW);
        sceGuShadeModel(GU_SMOOTH);
        sceGuEnable(GU_CULL_FACE);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuEnable(GU_TEXTURE_2D);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        sceGuFinish(); sceGuSync(0, 0);
        sceDisplayWaitVblankStart();
        sceGuDisplay(GU_TRUE);
        std::printf("I18 plugins PSP registrados\n");
        return 1;
    case DEVICETERM:
        sceGuTerm();
        return 1;
    case DEVICECLOSE: return 1;
    case DEVICEFINALIZE: return 1;
    case DEVICEGETNUMSUBSYSTEMS: return 1;
    case DEVICEGETCURRENTSUBSYSTEM: return 0;
    case DEVICESETSUBSYSTEM: return n == 0;
    case DEVICEGETSUBSSYSTEMINFO:
        if(arg) std::strcpy(((SubSystemInfo*)arg)->name, "PSP GU");
        return n == 0;
    case DEVICEGETNUMVIDEOMODES: return 1;
    case DEVICEGETCURRENTVIDEOMODE: return 0;
    case DEVICESETVIDEOMODE: return n == 0;
    case DEVICEGETVIDEOMODEINFO:
        if(arg && n == 0) {
            VideoMode *v = (VideoMode*)arg;
            v->width = 480; v->height = 272; v->depth = 32; v->flags = 0;
            return 1;
        }
        return 0;
    case DEVICEGETMAXMULTISAMPLINGLEVELS: return 1;
    case DEVICEGETMULTISAMPLINGLEVELS: return 1;
    case DEVICESETMULTISAMPLINGLEVELS: return n <= 1;
    default: return 1;
    }
}

Device renderdevice = {
    0.0f, 1.0f,
    beginUpdate, endUpdate, clearCamera, showRaster,
    rasterRenderFast, setRenderState, getRenderState,
    im2DRenderLine, im2DRenderTriangle, im2DRenderPrimitive, im2DRenderIndexedPrimitive,
    im3DTransform, im3DRenderPrimitive, im3DRenderIndexedPrimitive, im3DEnd,
    deviceSystem
};

}} // namespace rw::psp
