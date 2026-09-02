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

extern void pspTraceBoot(const char *message);
extern void pspTraceMemory(const char *stage);

void operator delete(void *ptr) noexcept { std::free(ptr); }
void operator delete[](void *ptr) noexcept { std::free(ptr); }

namespace rw {
namespace psp {

namespace {

constexpr int kBufferWidth = 512;
constexpr int kScreenWidth = 480;
constexpr int kScreenHeight = 272;
alignas(16) unsigned int displayList[262144];
ObjPipeline *defaultPipeline;
uintptr renderStates[32];
int32 nativeRasterOffset = -1;

static Raster *rasterCreate(Raster *raster);
static uint8 *rasterLock(Raster *raster, int32 level, int32 lockMode);
static void rasterUnlock(Raster *raster, int32 level);
static uint8 *rasterLockPalette(Raster *raster, int32 lockMode);
static void rasterUnlockPalette(Raster *raster);
static int32 rasterNumLevels(Raster *raster);
static bool32 imageFindRasterFormat(Image *image, int32 type, int32 *w, int32 *h, int32 *depth, int32 *format);
static bool32 rasterFromImage(Raster *raster, Image *image);
static Image *rasterToImage(Raster *raster);

struct PspRaster {
    uint8 *allocation;
};

struct GuVertex {
    float u, v;
    uint32 color;
    float x, y, z;
};

struct PspMeshInstance {
    GuVertex *vertexAllocation;
    GuVertex *vertices;
    uint16 *indexAllocation;
    uint16 *indices;
    uint32 numVertices;
    uint32 numIndices;
    Material *material;
};

struct PspInstanceData : InstanceDataHeader {
    uint16 serialNumber;
    uint16 numMeshes;
    PspMeshInstance *meshes;
};

static void destroyInstanceData(Geometry *geometry) {
    if(!geometry || !geometry->instData || geometry->instData->platform != PLATFORM_PSP)
        return;
    PspInstanceData *header = static_cast<PspInstanceData*>(geometry->instData);
    if(header->meshes) {
        for(uint32 i = 0; i < header->numMeshes; i++) {
            if(header->meshes[i].vertexAllocation)
                rwFree(header->meshes[i].vertexAllocation);
            if(header->meshes[i].indexAllocation)
                rwFree(header->meshes[i].indexAllocation);
        }
        rwFree(header->meshes);
    }
    rwFree(header);
    geometry->instData = nil;
}

static void *geometryDtor(void *object, int32, int32) {
    destroyInstanceData(static_cast<Geometry*>(object));
    return object;
}

static PspRaster *getPspRaster(Raster *raster) {
    return PLUGINOFFSET(PspRaster, raster, nativeRasterOffset);
}

static void *rasterCtor(void *object, int32 offset, int32) {
    PLUGINOFFSET(PspRaster, object, offset)->allocation = nil;
    return object;
}

static void *rasterDtor(void *object, int32 offset, int32) {
    PspRaster *native = PLUGINOFFSET(PspRaster, object, offset);
    if(native->allocation) {
        rwFree(native->allocation);
        native->allocation = nil;
    }
    return object;
}

static uint32 packColor(const RGBA &c) {
    return RWRGBAINT(c.red, c.green, c.blue, c.alpha);
}

static void loadRwMatrix(const Matrix *src) {
    ScePspFMatrix4 dst = {
        { src->right.x, src->right.y, src->right.z, 0.0f },
        { src->up.x,    src->up.y,    src->up.z,    0.0f },
        { src->at.x,    src->at.y,    src->at.z,    0.0f },
        { src->pos.x,   src->pos.y,   src->pos.z,   1.0f }
    };
    sceGumLoadMatrix(&dst);
}

static void pipelineInstance(ObjPipeline*, Atomic *atomic) {
    Geometry *geometry = atomic->geometry;
    if(!geometry || !geometry->meshHeader)
        return;
    if(geometry->instData && geometry->instData->platform == PLATFORM_PSP) {
        PspInstanceData *existing = static_cast<PspInstanceData*>(geometry->instData);
        if(existing->serialNumber == geometry->meshHeader->serialNum)
            return;
    }
    destroyInstanceData(geometry);

    PspInstanceData *header = rwNewT(PspInstanceData, 1, MEMDUR_EVENT);
    if(header == nil)
        return;
    std::memset(header, 0, sizeof(*header));
    header->platform = PLATFORM_PSP;
    header->serialNumber = geometry->meshHeader->serialNum;
    header->numMeshes = geometry->meshHeader->numMeshes;
    header->meshes = rwNewT(PspMeshInstance, header->numMeshes, MEMDUR_EVENT);
    if(header->meshes == nil) {
        rwFree(header);
        return;
    }
    memset(header->meshes, 0, sizeof(PspMeshInstance) * header->numMeshes);

    Mesh *sourceMeshes = geometry->meshHeader->getMeshes();
    V3d *positions = geometry->morphTargets[0].vertices;
    const bool triangleStrip = (geometry->meshHeader->flags & MeshHeader::TRISTRIP) != 0;
    for(uint32 m = 0; m < header->numMeshes; m++) {
        PspMeshInstance &instance = header->meshes[m];
        Mesh &source = sourceMeshes[m];
        instance.material = source.material;

        // Convert strips to a compact indexed triangle list. The previous PSP
        // path duplicated a 24-byte vertex for every triangle corner, so the
        // real heap use could be several times larger than the streaming
        // budget. Keeping one vertex per mesh index plus 16-bit indices cuts
        // both memory traffic and transform work without dropping geometry.
        const uint32 maxIndices = triangleStrip && source.numIndices >= 3
            ? (source.numIndices - 2) * 3
            : (source.numIndices / 3) * 3;
        const uint32 geometryVertices = (uint32)geometry->numVertices;
        const uint32 maxVertices = geometryVertices < source.numIndices ?
            geometryVertices : source.numIndices;
        if(maxIndices == 0 || maxVertices == 0 || source.indices == nil ||
           positions == nil || source.material == nil)
            continue;
        int32 *remap = rwNewT(int32, geometry->numVertices, MEMDUR_EVENT);
        instance.vertexAllocation = rwNewT(GuVertex, maxVertices + 1, MEMDUR_EVENT);
        instance.indexAllocation = rwNewT(uint16, maxIndices + 8, MEMDUR_EVENT);
        if(remap == nil || instance.vertexAllocation == nil || instance.indexAllocation == nil) {
            if(remap) rwFree(remap);
            if(instance.vertexAllocation) rwFree(instance.vertexAllocation);
            if(instance.indexAllocation) rwFree(instance.indexAllocation);
            instance.vertexAllocation = nil;
            instance.indexAllocation = nil;
            continue;
        }
        for(int32 i = 0; i < geometry->numVertices; i++)
            remap[i] = -1;
        instance.vertices = reinterpret_cast<GuVertex*>((
            reinterpret_cast<uintptr>(instance.vertexAllocation) + 15) & ~static_cast<uintptr>(15));
        instance.indices = reinterpret_cast<uint16*>((
            reinterpret_cast<uintptr>(instance.indexAllocation) + 15) & ~static_cast<uintptr>(15));
        const uint32 materialColor = packColor(source.material->color);

        auto mappedIndex = [&](uint16 index, uint16 &mapped) {
            if(index >= static_cast<uint32>(geometry->numVertices))
                return false;
            int32 compact = remap[index];
            if(compact >= 0) {
                mapped = (uint16)compact;
                return true;
            }
            if(instance.numVertices >= maxVertices || instance.numVertices >= 65535)
                return false;
            compact = (int32)instance.numVertices++;
            remap[index] = compact;
            mapped = (uint16)compact;
            GuVertex &vertex = instance.vertices[compact];
            vertex.u = geometry->texCoords[0] ? float(geometry->texCoords[0][index].u) : 0.0f;
            vertex.v = geometry->texCoords[0] ? float(geometry->texCoords[0][index].v) : 0.0f;
            vertex.color = geometry->colors ? packColor(geometry->colors[index]) : materialColor;
            vertex.x = positions[index].x;
            vertex.y = positions[index].y;
            vertex.z = positions[index].z;
            return true;
        };
        auto appendTriangle = [&](uint16 a, uint16 b, uint16 c) {
            if(a == b || b == c || a == c || instance.numIndices + 3 > maxIndices)
                return;
            uint16 ca, cb, cc;
            if(!mappedIndex(a, ca) || !mappedIndex(b, cb) || !mappedIndex(c, cc))
                return;
            instance.indices[instance.numIndices++] = ca;
            instance.indices[instance.numIndices++] = cb;
            instance.indices[instance.numIndices++] = cc;
        };

        if(triangleStrip) {
            for(uint32 i = 0; i + 2 < source.numIndices; i++) {
                const uint16 a = source.indices[i];
                const uint16 b = source.indices[i + 1 + (i & 1)];
                const uint16 c = source.indices[i + 2 - (i & 1)];
                appendTriangle(a, b, c);
            }
        } else {
            for(uint32 i = 0; i + 2 < source.numIndices; i += 3)
                appendTriangle(source.indices[i], source.indices[i + 1], source.indices[i + 2]);
        }
        rwFree(remap);
        sceKernelDcacheWritebackRange(instance.vertices, sizeof(GuVertex) * instance.numVertices);
        sceKernelDcacheWritebackRange(instance.indices, sizeof(uint16) * instance.numIndices);
    }
    geometry->instData = header;
}

static void pipelineUninstance(ObjPipeline*, Atomic *atomic) {
    destroyInstanceData(atomic ? atomic->geometry : nil);
}

static void pipelineRender(ObjPipeline*, Atomic *atomic) {
    Geometry *geometry = atomic->geometry;
    if(geometry == nil || geometry->meshHeader == nil)
        return;
    pipelineInstance(nil, atomic);
    PspInstanceData *header = static_cast<PspInstanceData*>(geometry->instData);
    if(!header || header->platform != PLATFORM_PSP)
        return;

    sceGumMatrixMode(GU_MODEL);
    loadRwMatrix(atomic->getFrame()->getLTM());

    for(uint32 m = 0; m < header->numMeshes; m++) {
        PspMeshInstance &mesh = header->meshes[m];
        if(mesh.numVertices == 0 || mesh.numIndices == 0 || mesh.material == nil)
            continue;
        Texture *texture = mesh.material->texture;
        Raster *raster = texture ? texture->raster : nil;
        const bool textured = raster && raster->pixels && geometry->texCoords[0];
        const bool transparent = mesh.material->color.alpha < 255;
        const bool textureHasAlpha = textured && raster->format == Raster::C4444;
        if(transparent) {
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
            sceGuEnable(GU_ALPHA_TEST);
            sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
            sceGuDepthMask(GU_TRUE);
        } else {
            sceGuDisable(GU_BLEND);
            // Every PSP texture is RGBA4444. Even opaque materials can contain
            // cut-out texels (fences, leaves, windows). Without alpha testing
            // those zero-alpha areas became large flat polygons on screen.
            if(textureHasAlpha) {
                sceGuEnable(GU_ALPHA_TEST);
                sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
            } else {
                sceGuDisable(GU_ALPHA_TEST);
            }
            sceGuDepthMask(GU_FALSE);
        }
        if(textured) {
            const int psm = raster->format == Raster::C4444 ? GU_PSM_4444 : GU_PSM_8888;
            const int bytesPerPixel = raster->depth / 8;
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(psm, 0, 0, GU_FALSE);
            sceGuTexImage(0, raster->width, raster->height,
                raster->stride / bytesPerPixel, raster->pixels);
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
            const bool linear = texture->getFilter() != Texture::NEAREST;
            sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
            sceGuTexWrap(GU_REPEAT, GU_REPEAT);
            // The GU 3D path consumes RenderWare's normalized float UVs
            // directly. Scaling by raster dimensions (10B) sampled outside
            // the intended area and erased the textures.
            sceGuTexScale(1.0f, 1.0f);
            sceGuTexOffset(0.0f, 0.0f);
        } else {
            sceGuDisable(GU_TEXTURE_2D);
        }
        // GuVertex siempre contiene UV antes de color y XYZ. La GU calcula el
        // stride y los offsets exclusivamente a partir de estas banderas; si
        // omitiamos GU_TEXTURE_32BITF en un material sin textura, interpretaba
        // u/v como color/posicion y la malla terminaba fuera de pantalla. Se
        // mantienen los UV en el formato aunque GU_TEXTURE_2D este desactivado.
        sceGumDrawArray(GU_TRIANGLES,
            GU_INDEX_16BIT | GU_TEXTURE_32BITF | GU_COLOR_8888 |
            GU_VERTEX_32BITF | GU_TRANSFORM_3D,
            mesh.numIndices, mesh.indices, mesh.vertices);
    }
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDepthMask(GU_FALSE);
}

static ObjPipeline *makeDefaultPipeline(void) {
    ObjPipeline *pipe = ObjPipeline::create();
    pipe->init(PLATFORM_PSP);
    pipe->impl.instance = pipelineInstance;
    pipe->impl.uninstance = pipelineUninstance;
    pipe->impl.render = pipelineRender;
    return pipe;
}

static void *driverOpen(void *object, int32, int32) {
    defaultPipeline = makeDefaultPipeline();
    Driver *driver = engine->driver[PLATFORM_PSP];
    driver->defaultPipeline = defaultPipeline;
    driver->rasterCreate = rasterCreate;
    driver->rasterLock = rasterLock;
    driver->rasterUnlock = rasterUnlock;
    driver->rasterLockPalette = rasterLockPalette;
    driver->rasterUnlockPalette = rasterUnlockPalette;
    driver->rasterNumLevels = rasterNumLevels;
    driver->imageFindRasterFormat = imageFindRasterFormat;
    driver->rasterFromImage = rasterFromImage;
    driver->rasterToImage = rasterToImage;
    return object;
}

static void *driverClose(void *object, int32, int32) {
    if(defaultPipeline) {
        defaultPipeline->destroy();
        defaultPipeline = nil;
    }
    return object;
}

static Raster *rasterCreate(Raster *raster) {
	// Camera/depth rasters are initially requested as 0x0. The platform driver
	// must supply its active video mode instead of leaving invalid metadata.
	if((raster->type == Raster::CAMERA || raster->type == Raster::ZBUFFER) &&
	   (raster->width <= 0 || raster->height <= 0)) {
		raster->width = kScreenWidth;
		raster->height = kScreenHeight;
	}
    if(raster->depth == 0)
        raster->depth = raster->format == Raster::C4444 ? 16 : 32;
    raster->stride = raster->width * ((raster->depth + 7) / 8);
    if(raster->type == Raster::TEXTURE && !(raster->flags & Raster::DONTALLOCATE) &&
       raster->width > 0 && raster->height > 0) {
        const uint32 size = raster->stride * raster->height;
        PspRaster *native = getPspRaster(raster);
        native->allocation = rwNewT(uint8, size + 15, MEMDUR_EVENT);
		if(native->allocation == nil) {
			raster->pixels = nil;
			pspTraceBoot("E75 memoria insuficiente para textura PSP");
			pspTraceMemory("fallo textura");
			return raster;
		}
        raster->pixels = reinterpret_cast<uint8*>(
            (reinterpret_cast<uintptr>(native->allocation) + 15) & ~static_cast<uintptr>(15));
        memset(raster->pixels, 0, size);
    }
    return raster;
}

static uint8 *rasterLock(Raster *raster, int32, int32) { return raster->pixels; }
static void rasterUnlock(Raster *raster, int32) {
    if(raster->pixels)
        sceKernelDcacheWritebackRange(raster->pixels, raster->stride * raster->height);
}
static uint8 *rasterLockPalette(Raster *raster, int32) { return raster->palette; }
static void rasterUnlockPalette(Raster*) { }
static int32 rasterNumLevels(Raster*) { return 1; }
static bool32 imageFindRasterFormat(Image *image, int32 type, int32 *w, int32 *h, int32 *depth, int32 *format) {
    if(image) {
        *w = image->width;
        *h = image->height;
    }
    // PSP textures use RGBA4444: half the memory of RGBA8888 while preserving
    // alpha. Camera color remains 32-bit and depth remains 16-bit.
    *depth = type == Raster::ZBUFFER || type == Raster::TEXTURE ? 16 : 32;
    *format = type | (type == Raster::ZBUFFER ? Raster::D16 :
                      type == Raster::TEXTURE ? Raster::C4444 : Raster::C8888);
    return 1;
}
static bool32 rasterFromImage(Raster *raster, Image *image) {
    if(raster == nil || image == nil || raster->pixels == nil)
        return 0;

    // Native GTA III TXDs are decoded to an RGBA Image first, so this is the
    // single upload path for paletted, 16-bit, 24-bit and compressed sources.
    if(image->depth <= 8)
        image->unpalettize(true);
    else if(image->depth != 32)
        image->convertTo32();

    if(image->width != raster->width || image->height != raster->height ||
       image->pixels == nil)
        return 0;

    if(raster->format == Raster::C4444){
        for(int32 y = 0; y < raster->height; y++){
            uint16 *dst = reinterpret_cast<uint16*>(raster->pixels + y*raster->stride);
            const uint8 *src = image->pixels + y*image->stride;
            for(int32 x = 0; x < raster->width; x++, src += 4)
                // GU_PSM_4444 reads a little-endian pixel as low-to-high
                // nibbles R,G,B,A.  Writing the visual string 0xRGBA here
                // swapped red with alpha: frontend art became opaque red and
                // glyph atlases collapsed into black rectangles.
                dst[x] = (src[0] >> 4) | ((src[1] >> 4) << 4) |
                         ((src[2] >> 4) << 8) | ((src[3] >> 4) << 12);
        }
    }else{
        for(int32 y = 0; y < raster->height; y++)
            memcpy(raster->pixels + y*raster->stride,
                   image->pixels + y*image->stride,
                   raster->width*4);
    }
    sceKernelDcacheWritebackRange(raster->pixels, raster->stride*raster->height);
    return 1;
}

static Image *rasterToImage(Raster *raster) {
    if(raster == nil || raster->pixels == nil || raster->width <= 0 || raster->height <= 0)
        return nil;
    Image *image = Image::create(raster->width, raster->height, 32);
    if(image == nil)
        return nil;
    image->allocate();
    if(image->pixels == nil) {
        image->destroy();
        return nil;
    }
    if(raster->format == Raster::C4444) {
        for(int32 y = 0; y < raster->height; y++) {
            const uint16 *src = reinterpret_cast<const uint16*>(
                raster->pixels + y*raster->stride);
            uint8 *dst = image->pixels + y*image->stride;
            for(int32 x = 0; x < raster->width; x++, dst += 4) {
                const uint16 pixel = src[x];
                dst[0] = uint8((pixel & 0x000F) * 17);
                dst[1] = uint8(((pixel >> 4) & 0x000F) * 17);
                dst[2] = uint8(((pixel >> 8) & 0x000F) * 17);
                dst[3] = uint8(((pixel >> 12) & 0x000F) * 17);
            }
        }
    } else {
        for(int32 y = 0; y < raster->height; y++)
            memcpy(image->pixels + y*image->stride,
                   raster->pixels + y*raster->stride,
                   raster->width*4);
    }
    return image;
}

static void beginUpdate(Camera *camera) {
    sceGuStart(GU_DIRECT, displayList);
    const float aspect = static_cast<float>(kScreenWidth) / kScreenHeight;
    const float fov = 2.0f * atanf(camera->viewWindow.y) * 180.0f / 3.14159265358979323846f;
    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumPerspective(fov, aspect, camera->nearPlane, camera->farPlane);
    sceGumMatrixMode(GU_VIEW);
    Matrix view;
    Matrix::invert(&view, camera->getFrame()->getLTM());
    // RenderWare mira hacia +Z; la GU de PSP usa -Z delante de la camara.
    view.right.z = -view.right.z;
    view.up.z = -view.up.z;
    view.at.z = -view.at.z;
    view.pos.z = -view.pos.z;
    loadRwMatrix(&view);
}

static void endUpdate(Camera*) {
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

static void clearCamera(Camera*, RGBA *color, uint32 mode) {
    uint32 flags = 0;
    if(mode & Camera::CLEARIMAGE) {
        sceGuClearColor(packColor(*color));
        flags |= GU_COLOR_BUFFER_BIT;
    }
    if(mode & Camera::CLEARZ) {
        sceGuClearDepth(0);
        flags |= GU_DEPTH_BUFFER_BIT;
    }
    sceGuClear(flags);
}

static void showRaster(Raster*, uint32 flags) {
    if(flags & Raster::FLIPWAITVSYNCH)
        sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

static bool32 rasterRenderFast(Raster*, int32, int32) { return 0; }
static void setRenderState(int32 state, void *value) {
    if(state >= 0 && state < static_cast<int32>(sizeof(renderStates)/sizeof(renderStates[0])))
        renderStates[state] = reinterpret_cast<uintptr>(value);
    const bool enabled = reinterpret_cast<uintptr>(value) != 0;
    switch(state) {
    case VERTEXALPHA:
        if(enabled) {
            sceGuEnable(GU_BLEND);
            sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
        } else {
            sceGuDisable(GU_BLEND);
        }
        break;
    case ZTESTENABLE:
        if(enabled) sceGuEnable(GU_DEPTH_TEST); else sceGuDisable(GU_DEPTH_TEST);
        break;
    case ZWRITEENABLE:
        sceGuDepthMask(enabled ? GU_FALSE : GU_TRUE);
        break;
    case CULLMODE: {
        const int mode = static_cast<int>(reinterpret_cast<uintptr>(value));
        if(mode == CULLNONE) {
            sceGuDisable(GU_CULL_FACE);
        } else {
            sceGuEnable(GU_CULL_FACE);
            sceGuFrontFace(mode == CULLFRONT ? GU_CCW : GU_CW);
        }
        break;
    }
    default:
        break;
    }
}
static void *getRenderState(int32 state) {
    return state >= 0 && state < static_cast<int32>(sizeof(renderStates)/sizeof(renderStates[0]))
        ? reinterpret_cast<void*>(renderStates[state]) : nil;
}

struct Gu2DVertex {
    float u, v;
    uint32 color;
    float x, y, z;
};

static int guPrimitive(PrimitiveType type) {
    switch(type) {
    case PRIMTYPELINELIST: return GU_LINES;
    case PRIMTYPEPOLYLINE: return GU_LINE_STRIP;
    case PRIMTYPETRILIST: return GU_TRIANGLES;
    case PRIMTYPETRISTRIP: return GU_TRIANGLE_STRIP;
    case PRIMTYPETRIFAN: return GU_TRIANGLE_FAN;
    case PRIMTYPEPOINTLIST: return GU_POINTS;
    default: return GU_TRIANGLES;
    }
}

static void bind2DTexture() {
    Raster *raster = reinterpret_cast<Raster*>(renderStates[TEXTURERASTER]);
    if(!raster || !raster->pixels) {
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDisable(GU_ALPHA_TEST);
        return;
    }
    const int bytesPerPixel = raster->depth / 8;
    const int psm = raster->format == Raster::C4444 ? GU_PSM_4444 : GU_PSM_8888;
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(psm, 0, 0, GU_FALSE);
    sceGuTexImage(0, raster->width, raster->height,
                  raster->stride / bytesPerPixel, raster->pixels);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    // TexScale is ignored by GU_TRANSFORM_2D. UV conversion is done while
    // copying the Im2D vertices below.
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    if(raster->format == Raster::C4444) {
        sceGuEnable(GU_ALPHA_TEST);
        sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
    } else {
        sceGuDisable(GU_ALPHA_TEST);
    }
}

static Gu2DVertex *copy2DVertices(void *data, int32 count) {
    auto *source = static_cast<Im2DVertex*>(data);
    auto *dest = static_cast<Gu2DVertex*>(sceGuGetMemory(sizeof(Gu2DVertex) * count));
    Raster *raster = reinterpret_cast<Raster*>(renderStates[TEXTURERASTER]);
    const float scaleU = raster && raster->pixels ? (float)raster->width : 1.0f;
    const float scaleV = raster && raster->pixels ? (float)raster->height : 1.0f;
    for(int32 i = 0; i < count; ++i) {
        dest[i].u = source[i].u * scaleU;
        dest[i].v = source[i].v * scaleV;
        dest[i].color = RWRGBAINT(source[i].r, source[i].g, source[i].b, source[i].a);
        dest[i].x = source[i].x;
        dest[i].y = source[i].y;
        dest[i].z = source[i].z;
    }
    return dest;
}

static void render2DPrimitive(PrimitiveType type, void *vertices, int32 count) {
    if(!vertices || count <= 0) return;
    bind2DTexture();
    Gu2DVertex *converted = copy2DVertices(vertices, count);
    sceGuDrawArray(guPrimitive(type), GU_TEXTURE_32BITF | GU_COLOR_8888 |
        GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, nullptr, converted);
}

static void render2DIndexed(PrimitiveType type, void *vertices, int32 numVertices,
                            void *indices, int32 numIndices) {
    if(!vertices || !indices || numVertices <= 0 || numIndices <= 0) return;
    bind2DTexture();
    Gu2DVertex *converted = copy2DVertices(vertices, numVertices);
    auto *indexCopy = static_cast<uint16*>(sceGuGetMemory(sizeof(uint16) * numIndices));
    std::memcpy(indexCopy, indices, sizeof(uint16) * numIndices);
    sceGuDrawArray(guPrimitive(type), GU_INDEX_16BIT | GU_TEXTURE_32BITF |
        GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        numIndices, indexCopy, converted);
}

static void render2DLine(void *vertices, int32 numVertices, int32 a, int32 b) {
    uint16 indices[2] = {uint16(a), uint16(b)};
    render2DIndexed(PRIMTYPELINELIST, vertices, numVertices, indices, 2);
}
static void render2DTriangle(void *vertices, int32 numVertices, int32 a, int32 b, int32 c) {
    uint16 indices[3] = {uint16(a), uint16(b), uint16(c)};
    render2DIndexed(PRIMTYPETRILIST, vertices, numVertices, indices, 3);
}
static void noop3DTransform(void*, int32, Matrix*, uint32) { }
static void noop3DPrimitive(PrimitiveType) { }
static void noop3DIndexed(PrimitiveType, void*, int32) { }
static void noop3DEnd(void) { }

static int system(DeviceReq req, void *arg, int32 n) {
    (void)n;
    if(req == DEVICEINIT) {
        void *frame0 = reinterpret_cast<void*>(0);
        void *frame1 = reinterpret_cast<void*>(kBufferWidth * kScreenHeight * 4);
        void *depth = reinterpret_cast<void*>(kBufferWidth * kScreenHeight * 8);
        sceGuInit();
        sceGuStart(GU_DIRECT, displayList);
        sceGuDrawBuffer(GU_PSM_8888, frame0, kBufferWidth);
        sceGuDispBuffer(kScreenWidth, kScreenHeight, frame1, kBufferWidth);
        sceGuDepthBuffer(depth, kBufferWidth);
        sceGuOffset(2048 - kScreenWidth / 2, 2048 - kScreenHeight / 2);
        sceGuViewport(2048, 2048, kScreenWidth, kScreenHeight);
        sceGuDepthRange(65535, 0);
        sceGuScissor(0, 0, kScreenWidth, kScreenHeight);
        sceGuEnable(GU_SCISSOR_TEST);
        sceGuDepthFunc(GU_GEQUAL);
        sceGuEnable(GU_DEPTH_TEST);
        sceGuFrontFace(GU_CW);
        sceGuShadeModel(GU_SMOOTH);
        sceGuEnable(GU_CULL_FACE);
        sceGuEnable(GU_CLIP_PLANES);
        sceGuFinish();
        sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
        sceDisplayWaitVblankStart();
        sceGuDisplay(GU_TRUE);
    } else if(req == DEVICETERM) {
        sceGuTerm();
    } else if(req == DEVICEGETNUMSUBSYSTEMS || req == DEVICEGETNUMVIDEOMODES) {
        // RenderWare asks these queries with arg == nil and expects the count
        // as the return value. Writing through arg made the PSP implementation
        // incompatible with the engine's Device contract.
        return 1;
    } else if(req == DEVICEGETCURRENTSUBSYSTEM || req == DEVICEGETCURRENTVIDEOMODE) {
        return 0;
    } else if(req == DEVICEGETSUBSSYSTEMINFO) {
        SubSystemInfo *info = static_cast<SubSystemInfo*>(arg);
        std::strncpy(info->name, "PSP GU", sizeof(info->name) - 1);
    } else if(req == DEVICEGETVIDEOMODEINFO) {
        VideoMode *mode = static_cast<VideoMode*>(arg);
        mode->width = kScreenWidth;
        mode->height = kScreenHeight;
        mode->depth = 32;
        mode->flags = VIDEOMODEEXCLUSIVE;
    } else if(req == DEVICESETSUBSYSTEM || req == DEVICESETVIDEOMODE) {
        return n == 0;
    }
    return 1;
}

} // namespace

Device renderdevice = {
    0.0f, 1.0f,
    beginUpdate, endUpdate, clearCamera, showRaster, rasterRenderFast,
    setRenderState, getRenderState,
    render2DLine, render2DTriangle, render2DPrimitive, render2DIndexed,
    noop3DTransform, noop3DPrimitive, noop3DIndexed, noop3DEnd,
    system
};

void registerPlatformPlugins(void) {
    Driver::registerPlugin(PLATFORM_PSP, 0, PLATFORM_PSP, driverOpen, driverClose);
    nativeRasterOffset = Raster::registerPlugin(
        sizeof(PspRaster), ID_RASTERPSP, rasterCtor, rasterDtor, nil);
    Geometry::registerPlugin(0, MAKEPLUGINID(VEND_DRIVER, PLATFORM_PSP),
        nil, geometryDtor, nil);
}

} // namespace psp
} // namespace rw
