#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define WITH_D3D
#include "rwbase.h"
#include "rwerror.h"
#include "rwplg.h"
#include "rwpipeline.h"
#include "rwobjects.h"
#include "rwengine.h"
#include "ps2-x/rwps2.h"
// #include "d3d-x/rwd3d.h"
// #include "d3d/rwxbox.h"
#include "d3d-x/rwd3d8.h"
// #include "d3d/rwd3d9.h"
// #include "d3d/rwd3dimpl.h"
// #include "gl/rwgl3.h"
#ifdef RW_PSP
#include "psp/rwpsp.h"
#else
#include "dc/rwdc.h"
#endif

#define PLUGIN_ID 0

namespace rw {

int32 Texture::numAllocated;
int32 TexDictionary::numAllocated;
#ifdef RW_PSP
int32 pspTextureMaxDimension = 64;
#endif

PluginList TexDictionary::s_plglist(sizeof(TexDictionary));
PluginList Texture::s_plglist(sizeof(Texture));
PluginList Raster::s_plglist(sizeof(Raster));

struct TextureGlobals
{
	TexDictionary *initialTexDict;
	TexDictionary *currentTexDict;
	// load textures from files
	bool32 loadTextures;
	// create dummy textures to store just names
	bool32 makeDummies;
	bool32 mipmapping;
	bool32 autoMipmapping;
	LinkList texDicts;

	LinkList textures;
};
int32 textureModuleOffset;

#define TEXTUREGLOBAL(v) (PLUGINOFFSET(TextureGlobals, engine, textureModuleOffset)->v)

static void*
textureOpen(void *object, int32 offset, int32 size)
{
	TexDictionary *texdict;
	textureModuleOffset = offset;
	TEXTUREGLOBAL(texDicts).init();
	TEXTUREGLOBAL(textures).init();
	texdict = TexDictionary::create();
	TEXTUREGLOBAL(initialTexDict) = texdict;
	TexDictionary::setCurrent(texdict);
	TEXTUREGLOBAL(loadTextures) = 1;
	TEXTUREGLOBAL(makeDummies) = 0;
	TEXTUREGLOBAL(mipmapping) = 0;
	TEXTUREGLOBAL(autoMipmapping) = 0;
	return object;
}
static void*
textureClose(void *object, int32 offset, int32 size)
{
	FORLIST(lnk, TEXTUREGLOBAL(texDicts))
		TexDictionary::fromLink(lnk)->destroy();
	TEXTUREGLOBAL(initialTexDict) = nil;
	TEXTUREGLOBAL(currentTexDict) = nil;

	FORLIST(lnk, TEXTUREGLOBAL(textures)){
		Texture *tex = LLLinkGetData(lnk, Texture, inGlobalList);
		printf("Tex still allocated: %d %s %s\n", tex->refCount, tex->name, tex->mask);
		assert(tex->dict == nil);
		tex->destroy();
	}
	return object;
}

void
Texture::registerModule(void)
{
	Engine::registerPlugin(sizeof(TextureGlobals), ID_TEXTUREMODULE, textureOpen, textureClose);
}

void
Texture::setLoadTextures(bool32 b)
{
	TEXTUREGLOBAL(loadTextures) = b;
}

void
Texture::setCreateDummies(bool32 b)
{
	TEXTUREGLOBAL(makeDummies) = b;
}

void Texture::setMipmapping(bool32 b) { TEXTUREGLOBAL(mipmapping) = b; }
void Texture::setAutoMipmapping(bool32 b) { TEXTUREGLOBAL(autoMipmapping) = b; }
bool32 Texture::getMipmapping(void) { return TEXTUREGLOBAL(mipmapping); }
bool32 Texture::getAutoMipmapping(void) { return TEXTUREGLOBAL(autoMipmapping); }

//
// TexDictionary
//

TexDictionary*
TexDictionary::create(void)
{
	TexDictionary *dict = (TexDictionary*)rwMalloc(s_plglist.size, MEMDUR_EVENT | ID_TEXDICTIONARY);
	if(dict == nil){
		RWERROR((ERR_ALLOC, s_plglist.size));
		return nil;
	}
	numAllocated++;
	dict->object.init(TexDictionary::ID, 0);
	dict->textures.init();
	TEXTUREGLOBAL(texDicts).add(&dict->inGlobalList);
	s_plglist.construct(dict);
	return dict;
}

void
TexDictionary::destroy(void)
{
	if(TEXTUREGLOBAL(currentTexDict) == this)
		TEXTUREGLOBAL(currentTexDict) = nil;
	FORLIST(lnk, this->textures){
		Texture *tex = Texture::fromDict(lnk);
		this->remove(tex);
		tex->destroy();
	}
	s_plglist.destruct(this);
	this->inGlobalList.remove();
	rwFree(this);
	numAllocated--;
}

void
TexDictionary::add(Texture *t)
{
	if(t->dict)
		t->inDict.remove();
	t->dict = this;
	this->textures.append(&t->inDict);
}

void
TexDictionary::remove(Texture *t)
{
	assert(t->dict == this);
	t->inDict.remove();
	t->dict = nil;
}

void
TexDictionary::addFront(Texture *t)
{
	if(t->dict)
		t->inDict.remove();
	t->dict = this;
	this->textures.add(&t->inDict);
}

Texture*
TexDictionary::find(const char *name)
{
	FORLIST(lnk, this->textures){
		Texture *tex = Texture::fromDict(lnk);
		if(strncmp_ci(tex->name, name, 32) == 0)
			return tex;
	}
	return nil;
}

TexDictionary*
TexDictionary::streamRead(Stream *stream)
{
	if(!findChunk(stream, ID_STRUCT, nil, nil)){
		RWERROR((ERR_CHUNK, "STRUCT"));
		return nil;
	}
	int32 numTex = stream->readI16();
	stream->readI16(); // device id (0 = unknown, 1 = d3d8, 2 = d3d9,
	                   // 3 = gcn, 4 = null, 5 = opengl,
	                   // 6 = ps2, 7 = softras, 8 = xbox, 9 = psp)
	TexDictionary *txd = TexDictionary::create();
	if(txd == nil)
		return nil;
	Texture *tex;
	for(int32 i = 0; i < numTex; i++){
		if(!findChunk(stream, ID_TEXTURENATIVE, nil, nil)){
			RWERROR((ERR_CHUNK, "TEXTURENATIVE"));
			goto fail;
		}
		tex = Texture::streamReadNative(stream);
		if(tex == nil)
			goto fail;
		Texture::s_plglist.streamRead(stream, tex);
		txd->add(tex);
	}
	if(s_plglist.streamRead(stream, txd))
		return txd;
fail:
	txd->destroy();
	return nil;
}

void
TexDictionary::streamWrite(Stream *stream)
{
	writeChunkHeader(stream, ID_TEXDICTIONARY, this->streamGetSize());
	writeChunkHeader(stream, ID_STRUCT, 4);
	int32 numTex = this->count();
	stream->writeI16(numTex);
	stream->writeI16(0);
	FORLIST(lnk, this->textures){
		Texture *tex = Texture::fromDict(lnk);
		uint32 sz = tex->streamGetSizeNative();
		sz += 12 + Texture::s_plglist.streamGetSize(tex);
		writeChunkHeader(stream, ID_TEXTURENATIVE, sz);
		tex->streamWriteNative(stream);
		Texture::s_plglist.streamWrite(stream, tex);
	}
	s_plglist.streamWrite(stream, this);
}

uint32
TexDictionary::streamGetSize(void)
{
	uint32 size = 12 + 4;
	FORLIST(lnk, this->textures){
		Texture *tex = Texture::fromDict(lnk);
		size += 12 + tex->streamGetSizeNative();
		size += 12 + Texture::s_plglist.streamGetSize(tex);
	}
	size += 12 + s_plglist.streamGetSize(this);
	return size;
}

void
TexDictionary::setCurrent(TexDictionary *txd)
{
	PLUGINOFFSET(TextureGlobals, engine, textureModuleOffset)->currentTexDict = txd;
}

TexDictionary*
TexDictionary::getCurrent(void)
{
	return PLUGINOFFSET(TextureGlobals, engine, textureModuleOffset)->currentTexDict;
}

//
// Texture
//

static Texture *defaultFindCB(const char *name);
static Texture *defaultReadCB(const char *name, const char *mask);

Texture *(*Texture::findCB)(const char *name) = defaultFindCB;
Texture *(*Texture::readCB)(const char *name, const char *mask) = defaultReadCB;

Texture*
Texture::create(Raster *raster)
{
	Texture *tex = (Texture*)rwMalloc(s_plglist.size, MEMDUR_EVENT | ID_TEXTURE);
	if(tex == nil){
		RWERROR((ERR_ALLOC, s_plglist.size));
		return nil;
	}
	numAllocated++;
	tex->dict = nil;
	tex->inDict.init();
	memset(tex->name, 0, 32);
	memset(tex->mask, 0, 32);
	tex->filterAddressing = (WRAP << 12) | (WRAP << 8) | NEAREST;
	tex->raster = raster;
	tex->refCount = 1;
	TEXTUREGLOBAL(textures).add(&tex->inGlobalList);
	s_plglist.construct(tex);
	return tex;
}

void
Texture::destroy(void)
{
	this->refCount--;
	if(this->refCount <= 0){
		s_plglist.destruct(this);
		if(this->dict)
			this->inDict.remove();
		if(this->raster)
			this->raster->destroy();
		this->inGlobalList.remove();
		rwFree(this);
		numAllocated--;
	}
}

static Texture*
defaultFindCB(const char *name)
{
	if(TEXTUREGLOBAL(currentTexDict))
		return TEXTUREGLOBAL(currentTexDict)->find(name);
	// TODO: RW searches *all* TXDs otherwise
	return nil;
}


static Texture*
defaultReadCB(const char *name, const char *mask)
{
	Texture *tex;
	Image *img;

	img = Image::readMasked(name, mask);
	if(img){
		tex = Texture::create(Raster::createFromImage(img));
		strncpy(tex->name, name, 32);
		if(mask)
			strncpy(tex->mask, mask, 32);
		img->destroy();
		return tex;
	}else
		return nil;
}

Texture*
Texture::read(const char *name, const char *mask)
{
	(void)mask;
	Raster *raster = nil;
	Texture *tex;

	if(tex = Texture::findCB(name), tex){
		tex->addRef();
		return tex;
	}
	if(TEXTUREGLOBAL(loadTextures)){
		tex = Texture::readCB(name, mask);
		if(tex == nil)
			goto dummytex;
	}else dummytex: if(TEXTUREGLOBAL(makeDummies)){
//printf("missing texture %s %s\n", name ? name : "", mask ? mask : "");
		tex = Texture::create(nil);
		if(tex == nil)
			return nil;
		strncpy(tex->name, name, 32);
		if(mask)
			strncpy(tex->mask, mask, 32);
		raster = Raster::create(0, 0, 0, Raster::DONTALLOCATE);
		tex->raster = raster;
	}
	if(tex && TEXTUREGLOBAL(currentTexDict)){
		if(tex->dict)
			tex->inDict.remove();
		TEXTUREGLOBAL(currentTexDict)->add(tex);
	}
	return tex;
}

Texture*
Texture::streamRead(Stream *stream)
{
	uint32 length;
	char name[128], mask[128];
	if(!findChunk(stream, ID_STRUCT, nil, nil)){
		RWERROR((ERR_CHUNK, "STRUCT"));
		return nil;
	}
	uint32 filterAddressing = stream->readU32();
	// if V addressing is 0, copy U
	if((filterAddressing & 0xF000) == 0)
		filterAddressing |= (filterAddressing&0xF00) << 4;

	// if using mipmap filter mode, set automipmapping,
	// if 0x10000 is set, set mipmapping

	if(!findChunk(stream, ID_STRING, &length, nil)){
		RWERROR((ERR_CHUNK, "STRING"));
		return nil;
	}
	stream->read8(name, length);

	if(!findChunk(stream, ID_STRING, &length, nil)){
		RWERROR((ERR_CHUNK, "STRING"));
		return nil;
	}
	stream->read8(mask, length);

	bool32 mipState = getMipmapping();
	bool32 autoMipState = getAutoMipmapping();
	int32 filter = filterAddressing&0xFF;
	if(filter == MIPNEAREST || filter == MIPLINEAR ||
	   filter == LINEARMIPNEAREST || filter == LINEARMIPLINEAR){
		setMipmapping(1);
		setAutoMipmapping((filterAddressing&0x10000) == 0);
	}else{
		setMipmapping(0);
		setAutoMipmapping(0);
	}

	Texture *tex = Texture::read(name, mask);

	setMipmapping(mipState);
	setAutoMipmapping(autoMipState);

	if(tex == nil){
		s_plglist.streamSkip(stream);
		return nil;
	}
	if(tex->refCount == 1)
		tex->filterAddressing = filterAddressing&0xFFFF;

	if(s_plglist.streamRead(stream, tex))
		return tex;

	tex->destroy();
	return nil;
}

bool
Texture::streamWrite(Stream *stream)
{
	int size;
	char buf[36];
	writeChunkHeader(stream, ID_TEXTURE, this->streamGetSize());
	writeChunkHeader(stream, ID_STRUCT, 4);
	uint32 filterAddressing = this->filterAddressing;
	if(this->raster && (raster->format & Raster::AUTOMIPMAP) == 0)
		filterAddressing |= 0x10000;
	stream->writeU32(filterAddressing);

	memset(buf, 0, 36);
	strncpy(buf, this->name, 32);
	size = strlen(buf)+4 & ~3;
	writeChunkHeader(stream, ID_STRING, size);
	stream->write8(buf, size);

	memset(buf, 0, 36);
	strncpy(buf, this->mask, 32);
	size = strlen(buf)+4 & ~3;
	writeChunkHeader(stream, ID_STRING, size);
	stream->write8(buf, size);

	s_plglist.streamWrite(stream, this);
	return true;
}

uint32
Texture::streamGetSize(void)
{
	uint32 size = 0;
	size += 12 + 4;
	size += 12 + 12;
	size += strlen(this->name)+4 & ~3;
	size += strlen(this->mask)+4 & ~3;
	size += 12 + s_plglist.streamGetSize(this);
	return size;
}

Texture*
Texture::streamReadNative(Stream *stream)
{
#ifdef RW_PSP
	// GTA III PC assets contain D3D8 native textures. The PSP must decode
	// those bytes on the CPU and upload RGBA8888; registering the desktop
	// D3D8 driver here would leave function pointers/device objects invalid.
	uint32 nativeStructSize = 0;
	if(!findChunk(stream, ID_STRUCT, &nativeStructSize, nil))
		return nil;
	const uint32 nativeStructBegin = stream->tell();
	const uint32 nativeStructEnd = nativeStructBegin + nativeStructSize;
	// D3D8 native header through compression is 88 bytes. Reject truncated or
	// implausible assets before any dimensions become allocation sizes.
	if(nativeStructSize < 88)
		return nil;
	uint32 platform = stream->readU32();
	if(platform != PLATFORM_D3D8)
		return nil;

	Texture *tex = Texture::create(nil);
	if(tex == nil)
		return nil;
	tex->filterAddressing = stream->readU32();
	stream->read8(tex->name, 32);
	stream->read8(tex->mask, 32);

	uint32 format = stream->readU32();
	bool32 hasAlpha = stream->readI32();
	int32 width = stream->readU16();
	int32 height = stream->readU16();
	stream->readU8();                         // source depth
	int32 numLevels = stream->readU8();
	stream->readU8();                         // source raster type
	int32 compression = stream->readU8();
	if(width <= 0 || height <= 0 || width > 2048 || height > 2048 ||
	   numLevels <= 0 || numLevels > 16) {
		tex->destroy();
		return nil;
	}

	uint8 palette[256*4];
	memset(palette, 0, sizeof(palette));
	int32 paletteEntries = 0;
	if(format & Raster::PAL4) {
		paletteEntries = 16;
		// RenderWare D3D8 writes 32 entries for PAL4.
		if(stream->tell() + 32*4 > nativeStructEnd ||
		   stream->read8(palette, 32*4) != 32*4) {
			tex->destroy();
			return nil;
		}
	} else if(format & Raster::PAL8) {
		paletteEntries = 256;
		if(stream->tell() + 256*4 > nativeStructEnd ||
		   stream->read8(palette, 256*4) != 256*4) {
			tex->destroy();
			return nil;
		}
	}
	if(!hasAlpha)
		for(int32 i = 0; i < paletteEntries; i++) palette[i*4+3] = 0xFF;

	Image *image = Image::create(width < 4 ? 4 : width,
	                            height < 4 ? 4 : height, 32);
	if(image == nil) {
		tex->destroy();
		return nil;
	}
	image->allocate();
	if(image->pixels == nil) {
		image->destroy();
		tex->destroy();
		return nil;
	}

	for(int32 level = 0; level < numLevels; level++) {
		if(stream->tell() + 4 > nativeStructEnd) {
			image->destroy();
			tex->destroy();
			return nil;
		}
		uint32 size = stream->readU32();
		if(size > nativeStructEnd - stream->tell()) {
			image->destroy();
			tex->destroy();
			return nil;
		}
		if(level != 0) {
			stream->seek(size);
			continue;
		}
		uint32 minimumSize = 0;
		if(compression >= 1 && compression <= 5) {
			const uint32 blockBytes = (compression == 1 ? 8 : 16);
			minimumSize = ((width + 3)/4) * ((height + 3)/4) * blockBytes;
		} else if(paletteEntries) {
			minimumSize = width * height;
		} else {
			switch(format & 0xF00) {
			case Raster::C8888: minimumSize = width*height*4; break;
			case Raster::C888:  minimumSize = width*height*3; break;
			case Raster::C565:
			case Raster::C1555:
			case Raster::C555:
			case Raster::C4444: minimumSize = width*height*2; break;
			default: break;
			}
		}
		if(minimumSize == 0 || size < minimumSize) {
			image->destroy();
			tex->destroy();
			return nil;
		}
		uint8 *data = rwNewT(uint8, size, MEMDUR_FUNCTION | ID_IMAGE);
		if(data == nil) {
			image->destroy();
			tex->destroy();
			return nil;
		}
		if(stream->read8(data, size) != size) {
			rwFree(data);
			image->destroy();
			tex->destroy();
			return nil;
		}

		if(compression == 1 || compression == 2 || compression == 3 ||
		   compression == 4 || compression == 5) {
			image->setPixelsDXT(compression == 2 ? 3 : compression == 4 ? 5 : compression, data);
		} else {
			uint8 *dst = image->pixels;
			const int32 count = width*height;
			for(int32 i = 0; i < count; i++) {
				uint8 r = 255, g = 255, b = 255, a = 255;
				if(paletteEntries) {
					uint32 idx;
					// RW's D3D8 native PAL4 path stores one byte per index too;
					// only the palette cardinality is four bit.
					idx = data[i];
					if(idx >= static_cast<uint32>(paletteEntries)) idx = 0;
					r = palette[idx*4+0]; g = palette[idx*4+1];
					b = palette[idx*4+2]; a = palette[idx*4+3];
				} else switch(format & 0xF00) {
				case Raster::C8888: {
					const uint8 *p = data + i*4;
					r = p[2]; g = p[1]; b = p[0]; a = p[3]; break;
				}
				case Raster::C888: {
					const uint8 *p = data + i*3;
					r = p[2]; g = p[1]; b = p[0]; break;
				}
				case Raster::C565: {
					uint16 p = reinterpret_cast<uint16*>(data)[i];
					r = ((p >> 11) & 31)*255/31; g = ((p >> 5) & 63)*255/63;
					b = (p & 31)*255/31; break;
				}
				case Raster::C1555:
				case Raster::C555: {
					uint16 p = reinterpret_cast<uint16*>(data)[i];
					a = (format & 0xF00) == Raster::C1555 ? ((p & 0x8000) ? 255 : 0) : 255;
					r = ((p >> 10) & 31)*255/31; g = ((p >> 5) & 31)*255/31;
					b = (p & 31)*255/31; break;
				}
				case Raster::C4444: {
					uint16 p = reinterpret_cast<uint16*>(data)[i];
					a = ((p >> 12) & 15)*17; r = ((p >> 8) & 15)*17;
					g = ((p >> 4) & 15)*17; b = (p & 15)*17; break;
				}
				default: break;
				}
				dst[i*4+0] = r; dst[i*4+1] = g; dst[i*4+2] = b; dst[i*4+3] = a;
			}
		}
		rwFree(data);
	}

	image->width = width;
	image->height = height;
	image->stride = (width < 4 ? 4 : width)*4;
	// Preserve aspect ratio. The old clamp independently forced both axes to
	// 64 and visibly stretched wide/narrow GTA textures. Large dictionaries are
	// capped at 32 by TexRead; smaller ones retain 64.
	const int32 maxDimension = pspTextureMaxDimension < 16 ? 16 : pspTextureMaxDimension;
	if(image->width > maxDimension || image->height > maxDimension) {
		int32 scaledWidth, scaledHeight;
		if(image->width >= image->height){
			scaledWidth = maxDimension;
			const int32 candidateHeight = image->height * maxDimension / image->width;
			scaledHeight = candidateHeight < 4 ? 4 : candidateHeight;
		}else{
			scaledHeight = maxDimension;
			const int32 candidateWidth = image->width * maxDimension / image->height;
			scaledWidth = candidateWidth < 4 ? 4 : candidateWidth;
		}
		Image *scaled = Image::create(scaledWidth, scaledHeight, 32);
		if(scaled == nil) {
			image->destroy();
			tex->destroy();
			return nil;
		}
		scaled->allocate();
		if(scaled->pixels == nil) {
			scaled->destroy();
			image->destroy();
			tex->destroy();
			return nil;
		}
		for(int32 y = 0; y < scaledHeight; y++) {
			const int32 sourceY = y*image->height/scaledHeight;
			for(int32 x = 0; x < scaledWidth; x++) {
				const int32 sourceX = x*image->width/scaledWidth;
				memcpy(scaled->pixels + y*scaled->stride + x*4,
				       image->pixels + sourceY*image->stride + sourceX*4, 4);
			}
		}
		image->destroy();
		image = scaled;
	}
	tex->raster = Raster::createFromImage(image, PLATFORM_PSP);
	image->destroy();
	if(tex->raster == nil) {
		tex->destroy();
		return nil;
	}
	return tex;
#else
	if(!findChunk(stream, ID_STRUCT, nil, nil)){
		RWERROR((ERR_CHUNK, "STRUCT"));
		return nil;
	}
	uint32 platform = stream->readU32();
	stream->seek(-16);
	#if !defined(RW_DC)
	if(platform == PLATFORM_D3D8)
		return d3d8::readNativeTexture(stream);
	if(platform == PLATFORM_D3D9)
		return d3d9::readNativeTexture(stream);
	if(platform == PLATFORM_XBOX)
		return xbox::readNativeTexture(stream);
	if(platform == PLATFORM_GL3)
		return gl3::readNativeTexture(stream);
	#else
	#if defined(DC_TEXCONV)
	if(platform == FOURCC_PS2)
		return ps2::readNativeTexture(stream);
	if(platform == PLATFORM_D3D8)
		return d3d8::readNativeTexture(stream);
	#endif
	if(platform == PLATFORM_DC)
		return dc::readNativeTexture(stream);
	printf("Implement this: %s %d %u\n", __func__, __LINE__, platform);
	#endif
	assert(false && "Shouldn't reach here");
	return nil;
#endif
}

void
Texture::streamWriteNative(Stream *stream)
{
	#if defined(RW_PSP)
		(void)stream;
	#elif !defined(RW_DC)
		if(this->raster->platform == PLATFORM_PS2)
			ps2::writeNativeTexture(this, stream);
		else if(this->raster->platform == PLATFORM_D3D8)
			d3d8::writeNativeTexture(this, stream);
		else if(this->raster->platform == PLATFORM_D3D9)
			d3d9::writeNativeTexture(this, stream);
		else if(this->raster->platform == PLATFORM_XBOX)
			xbox::writeNativeTexture(this, stream);
		else if(this->raster->platform == PLATFORM_GL3)
			gl3::writeNativeTexture(this, stream);
		else
			assert(0 && "unsupported platform");	
	#elif defined(DC_TEXCONV)
		if (this->raster->platform == PLATFORM_DC)
			dc::writeNativeTexture(this, stream);
		else
			assert(0 && "unsupported platform");
	#else
		assert(0 && "streamWriteNative during dreamcast runtime?!");
	#endif
}

uint32
Texture::streamGetSizeNative(void)
{
	// if(this->raster->platform == PLATFORM_PS2)
	// 	return ps2::getSizeNativeTexture(this);
	// if(this->raster->platform == PLATFORM_D3D8)
	// 	return d3d8::getSizeNativeTexture(this);
	// if(this->raster->platform == PLATFORM_D3D9)
	// 	return d3d9::getSizeNativeTexture(this);
	// if(this->raster->platform == PLATFORM_XBOX)
	// 	return xbox::getSizeNativeTexture(this);
	// if(this->raster->platform == PLATFORM_GL3)
	// 	return gl3::getSizeNativeTexture(this);
	printf("Implement this: %s\n", __func__);
	assert(0 && "unsupported platform");
	return 0;
}



int32 anisotOffset;

static void*
createAnisot(void *object, int32 offset, int32)
{
	*GETANISOTROPYEXT(object) = 1;
	return object;
}

static void*
copyAnisot(void *dst, void *src, int32 offset, int32)
{
	*GETANISOTROPYEXT(dst) = *GETANISOTROPYEXT(src);
	return dst;
}

static Stream*
readAnisot(Stream *stream, int32, void *object, int32 offset, int32)
{
	*GETANISOTROPYEXT(object) = stream->readI32();
	return stream;
}

static Stream*
writeAnisot(Stream *stream, int32, void *object, int32 offset, int32)
{
	stream->writeI32(*GETANISOTROPYEXT(object));
	return stream;
}

static int32
getSizeAnisot(void *object, int32 offset, int32)
{
	if(*GETANISOTROPYEXT(object) == 1)
		return 0;
	return sizeof(int32);
}

void
registerAnisotropyPlugin(void)
{
	anisotOffset = Texture::registerPlugin(sizeof(int32), ID_ANISOT, createAnisot, nil, copyAnisot);
	Texture::registerPluginStream(ID_ANISOT, readAnisot, writeAnisot, getSizeAnisot);
}

void
Texture::setMaxAnisotropy(int32 maxaniso)
{
	if(anisotOffset > 0)
		*GETANISOTROPYEXT(this) = maxaniso;
}

int32
Texture::getMaxAnisotropy(void)
{
	if(anisotOffset > 0)
		return *GETANISOTROPYEXT(this);
	return 1;
}

int32
getMaxSupportedMaxAnisotropy(void)
{
#ifdef RW_D3D9
	return d3d::d3d9Globals.caps.MaxAnisotropy;
#endif
#ifdef RW_GL3
	return (int32)gl3::gl3Caps.maxAnisotropy;
#endif
	return 1;
}

}
