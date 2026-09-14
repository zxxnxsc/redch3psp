#pragma once

namespace rw {

#ifdef RW_PSP
struct EngineOpenParams
{
    void *display;
    EngineOpenParams(void *display = nil) : display(display) {}
};
#endif

namespace psp {

struct Im2DVertex
{
    float32 x, y, z, recipz;
    uint32 color;
    float32 u, v;

    void setScreenX(float32 v) { x = v; }
    void setScreenY(float32 v) { y = v; }
    void setScreenZ(float32 v) { z = v; }
    void setCameraZ(float32) {}
    void setRecipCameraZ(float32 v) { recipz = v; }
    void setColor(uint8 r, uint8 g, uint8 b, uint8 a) { color = RWRGBAINT(r, g, b, a); }
    void setU(float32 v, float32) { u = v; }
    void setV(float32 v_, float32) { v = v_; }

    float32 getScreenX(void) { return x; }
    float32 getScreenY(void) { return y; }
    float32 getScreenZ(void) { return z; }
    float32 getCameraZ(void) { return 1.0f; }
    float32 getRecipCameraZ(void) { return recipz; }
    RGBA getColor(void) {
        return makeRGBA(color & 0xFF, (color >> 8) & 0xFF,
                        (color >> 16) & 0xFF, (color >> 24) & 0xFF);
    }
    float32 getU(void) { return u; }
    float32 getV(void) { return v; }
};

struct Im3DVertex
{
    V3d position;
    V3d normal;
    uint32 color;
    TexCoords texCoords;

    void setPos(float32 x, float32 y, float32 z) { position.set(x, y, z); }
    void setNormal(float32 x, float32 y, float32 z) { normal.set(x, y, z); }
    void setColor(uint8 r, uint8 g, uint8 b, uint8 a) { color = RWRGBAINT(r, g, b, a); }
    void setU(float32 u) { texCoords.u = u; }
    void setV(float32 v) { texCoords.v = v; }

    V3d *getPos(void) { return &position; }
    V3d *getNormal(void) { return &normal; }
    RGBA getColor(void) {
        return makeRGBA(color & 0xFF, (color >> 8) & 0xFF,
                        (color >> 16) & 0xFF, (color >> 24) & 0xFF);
    }
    float32 getU(void) { return texCoords.u; }
    float32 getV(void) { return texCoords.v; }
};

void registerPlatformPlugins(void);
extern Device renderdevice;

}
}
