#pragma once

namespace rw {

// Selected per TXD by the GTA loader. Large world dictionaries use 32px;
// smaller UI/object dictionaries retain 64px.
extern int32 pspTextureMaxDimension;

#ifdef RW_PSP
struct EngineOpenParams {
    void *displayID;
};
#endif

namespace psp {

struct Im3DVertex
{
    V3d position;
    uint8 r, g, b, a;
    float32 u, v;

    void setX(float32 value) { position.x = value; }
    void setY(float32 value) { position.y = value; }
    void setZ(float32 value) { position.z = value; }
    void setColor(uint8 red, uint8 green, uint8 blue, uint8 alpha) {
        r = red; g = green; b = blue; a = alpha;
    }
    void setU(float32 value) { u = value; }
    void setV(float32 value) { v = value; }
    float getX(void) { return position.x; }
    float getY(void) { return position.y; }
    float getZ(void) { return position.z; }
    RGBA getColor(void) { return makeRGBA(r, g, b, a); }
    float getU(void) { return u; }
    float getV(void) { return v; }
};

struct Im2DVertex {
    float32 x, y, z, w;
    uint8 r, g, b, a;
    float32 u, v;

    void setScreenX(float32 value) { x = value; }
    void setScreenY(float32 value) { y = value; }
    void setScreenZ(float32 value) { z = value; }
    void setCameraZ(float32 value) { w = value; }
    void setRecipCameraZ(float32 value) { w = value == 0.0f ? 1.0f : 1.0f/value; }
    void setColor(uint8 red, uint8 green, uint8 blue, uint8 alpha) {
        r = red; g = green; b = blue; a = alpha;
    }
    void setU(float32 value, float32) { u = value; }
    void setV(float32 value, float32) { v = value; }
    float getScreenX(void) { return x; }
    float getScreenY(void) { return y; }
    float getScreenZ(void) { return z; }
    float getCameraZ(void) { return w; }
    float getRecipCameraZ(void) { return w == 0.0f ? 1.0f : 1.0f/w; }
    RGBA getColor(void) { return makeRGBA(r, g, b, a); }
    float getU(void) { return u; }
    float getV(void) { return v; }
};

void registerPlatformPlugins(void);
extern Device renderdevice;

} // namespace psp
} // namespace rw
