#include "sdl-utils.h"
#include "utils.h"
#include "xxhash.h"
#include <algorithm>

#include "SDL_syswm.h"
#include "Gdiplus.h"
#pragma comment(lib,"Gdiplus.lib")

#include "glew/glew.h"
#include <GL/GL.h>
#include <GL/GLU.h>
#include "glext.h"
#pragma comment(lib,"opengl32.lib")

struct GdiPlusHelper {
	GdiPlusHelper() {
		Gdiplus::GdiplusStartupInput gdiplusStartupInput;
		ULONG_PTR gdiplusToken;
		Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
	}
} gdiPlusHelper;

extern SDL_Window *globalWindow;

namespace SDL {

std::vector< DrawHook * > hookListDraw;
std::vector< EventHook * > hookListEvents;
std::map< GLuint, unsigned long long > texturesMap;
std::map< unsigned long long, Coord > lastFrameMap;

GLuint glTexture(unsigned char *pixelData, int w, int h) {
	GLuint texture = 0;

	GLenum texture_format, internal_format, tex_type;
	texture_format = GL_RGBA;
	tex_type = GL_UNSIGNED_INT_8_8_8_8_REV;
	internal_format = GL_RGBA8;

	int pitch = w * 4;
	int alignment = 8;
	while(pitch%alignment) alignment >>= 1; // x%1==0 for any x
	glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);

	int expected_pitch = (w*4 + alignment - 1) / alignment*alignment;
	if(pitch - expected_pitch >= alignment) // Alignment alone wont't solve it now
		glPixelStorei(GL_UNPACK_ROW_LENGTH, pitch / 4);
	else glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexImage2D(GL_TEXTURE_2D, 0, internal_format, w, h, 0, texture_format, tex_type, pixelData);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

	return texture;
}

Color::Color() {
	r = 255;
	g = 255;
	b = 255;
	a = 255;
}

Color::Color(int r, int g, int b, int a) {
	this->r = r;
	this->g = g;
	this->b = b;
	this->a = a;
}

Color::Color(int r, int g, int b) {
	this->r = r;
	this->g = g;
	this->b = b;
	this->a = 0xff;
}

Color Color::White = Color();
Color Color::Black = Color(0, 0, 0);
Color Color::Transparent = Color(0, 0, 0, 0);

Rect::Rect(int x, int y, int w, int h) {
	this->x = x;
	this->y = y;
	this->w = w;
	this->h = h;
}

bool Rect::contains(int x, int y) {
	SDL_Point p = { x, y };
	return SDL_PointInRect(&p, this);
}

bool Rect::intersects(Rect* other) {
	return SDL_HasIntersection(this, other);
}

Rect Rect::getIntersect(Rect* other) {
	Rect* result;
	if (SDL_IntersectRect(this, other, result)) {
		return *result;
	}

	return Rect(0, 0, 0, 0);
}

Rect Rect::getUnion(Rect* other) {
	Rect* result;
	SDL_UnionRect(this, other, result);

	return *result;
}

Font::Font() {
	defaults();
}
Font::~Font() {
	// this is a leak
	// i have no idea how to handle font->GetFamily(family) without gdi crashing

	//delete family;

	delete font;
}
void Font::defaults() {
	setFont(new Gdiplus::Font(L"Arial", 12));
}

Font::Font(const std::string &name, double size) {
	setFont(new Gdiplus::Font(s2ws(name).c_str(), (Gdiplus::REAL) size, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint));
}

FileFont::FileFont(const Blob *blob, double size) {
	privateFontCollection.AddMemoryFont(blob->data, blob->length);

	init(size);
}

FileFont::FileFont(const std::string &filename, double size) {
	privateFontCollection.AddFontFile(s2ws(filename).c_str());

	init(size);
}

void FileFont::init(double size) {
	int count = privateFontCollection.GetFamilyCount();

	int found = 0;
	Gdiplus::FontFamily family;
	privateFontCollection.GetFamilies(1, &family, &found);
	if(found < 1) {
		defaults();
		return;
	}

	WCHAR familyName[LF_FACESIZE];
	family.GetFamilyName(familyName);

	int styles[] = { Gdiplus::FontStyleRegular, Gdiplus::FontStyleBold, Gdiplus::FontStyleItalic, Gdiplus::FontStyleBoldItalic };
	for(int i = 0; i < sizeof(styles)/sizeof(styles[0]); i++) {
		if(family.IsStyleAvailable(styles[i])) {
			Gdiplus::Font* newfont = new Gdiplus::Font(
				familyName, (Gdiplus::REAL) size, Gdiplus::FontStyleBold, Gdiplus::UnitPoint, &privateFontCollection
				);
			setFont(newfont);
			return;
		}
	}

	defaults();
}

void Font::setFont(Gdiplus::Font *f) {
	font = f;

	family = new Gdiplus::FontFamily;
	f->GetFamily(family);
	
	HWND hDesktopWnd = GetDesktopWindow();
	HDC hDesktopDC = GetDC(hDesktopWnd);
	HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);
	
	Gdiplus::Graphics * graphics = new Gdiplus::Graphics(hCaptureDC);

	float ascentPoints = f->GetSize() / family->GetEmHeight(f->GetStyle())*family->GetCellAscent(f->GetStyle());
	float descentPoints = f->GetSize() / family->GetEmHeight(f->GetStyle())*family->GetCellDescent(f->GetStyle());
	ascent = graphics->GetDpiY() / 72.0f * ascentPoints;
	descent = graphics->GetDpiY() / 72.0f * descentPoints;

	delete graphics;

	ReleaseDC(hDesktopWnd, hDesktopDC);
	DeleteDC(hCaptureDC);
}

void Surface::init() {
	pixelData = NULL;
	textureId = 0;
	hash = 0;
	width = 0;
	height = 0;
	padl = 0;
	padr = 0;
}

Surface::~Surface() {
	if(pixelData != NULL)
		delete[] pixelData;
	if(textureId != 0)
		glDeleteTextures(1, &textureId);
}

void Surface::setBitmap(HBITMAP hCaptureBitmap, int sx, int sy, int w, int h) {
	BITMAP bm;

	GetObject(hCaptureBitmap, sizeof(BITMAP), &bm);

	int nScreenWidth = bm.bmWidth;
	int nScreenHeight = bm.bmHeight;

	BITMAPINFO bmpInfo;
	bmpInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmpInfo.bmiHeader.biWidth = nScreenWidth;
	bmpInfo.bmiHeader.biHeight = -nScreenHeight;
	bmpInfo.bmiHeader.biPlanes = 1;
	bmpInfo.bmiHeader.biBitCount = 32;
	bmpInfo.bmiHeader.biCompression = BI_RGB;
	bmpInfo.bmiHeader.biSizeImage = 0;

	HDC hCaptureDC = CreateCompatibleDC(NULL);
	unsigned char *pixels = new unsigned char[nScreenWidth * nScreenHeight * 4];
	::GetDIBits(hCaptureDC, hCaptureBitmap, 0, nScreenHeight, pixels, &bmpInfo, DIB_RGB_COLORS);

	setBitmap(pixels, sx, sy, w, h, nScreenWidth * 4);

	delete[] pixels;

	DeleteDC(hCaptureDC);
}

void Surface::setBitmap(void *data, int sx, int sy, int w, int h, int stride) {
	unsigned char *pixels = (unsigned char *) data;

	pixelData = new unsigned char[w * h * 4];
	int initial = 0;
	if(stride < 0) {
		initial = (sy + h - 1) * -stride;
	}

	for(int y = 0; y < h; y++) {
		unsigned char *dst = pixelData + (y * w * 4);
		unsigned char *src = pixels + (initial + sx * 4 + (sy + y) * stride);

		for(int x = 0; x < w; x++) {
			dst[0] = src[2];
			dst[1] = src[1];
			dst[2] = src[0];
			dst[3] = src[3];

			dst += 4;
			src += 4;
		}
	}

	createSurfaceFromPixelData(w, h);
}

void Surface::createSurfaceFromPixelData(int w, int h){
	hash = XXH64(pixelData, w * h * 4, 0);

	width = w;
	height = h;
}

void Surface::setBitmap(Gdiplus::Bitmap *bitmap) {
	Gdiplus::BitmapData* bitmapData = new Gdiplus::BitmapData;
	Gdiplus::Rect rect(0, 0, bitmap->GetWidth(), bitmap->GetHeight());

	bitmap->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, bitmapData);
	UINT* pixels = (UINT*) bitmapData->Scan0;

	setBitmap(pixels, 0, 0, rect.Width, rect.Height, bitmapData->Stride);

	bitmap->UnlockBits(bitmapData);

	delete bitmapData;
}

Surface::Surface(const std::string &filename) {
	init();

	std::wstring ws = s2ws(filename);
	Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromFile(ws.c_str(), false);
	if(bitmap == NULL) {
		::log("couldn't open picture: %s\n", filename.c_str());
		exit(1);
	}

	setBitmap(bitmap);

	delete bitmap;
}

Surface::Surface(Blob *blob) {
	init();

	Gdiplus::Bitmap *bitmap = Gdiplus::Bitmap::FromStream(blob, false);
	if(bitmap == NULL) {
		::log("couldn't open picture from blob\n", blob->source.c_str());
		exit(1);
	}

	setBitmap(bitmap);

	delete bitmap;

	blob->reset();
}

UINT findLeftPadding(Gdiplus::BitmapData* bitmapData, UINT width, UINT height) {
	UINT *rawBitmapData = (UINT*)bitmapData->Scan0;
	UINT padding = 0;
	for (UINT x = 0; x < width; x++) {
		for (UINT y = 0; y < height; y++) {
			UINT color = rawBitmapData[y * bitmapData->Stride / 4 + x];
			int red = (color & 0xff0000) >> 16;
			if (red != NULL) {
				return padding;
			}
		}
		padding++;
	}
	return padding;
}

int findRightPadding(Gdiplus::BitmapData* bitmapData, UINT width, UINT height) {
	UINT *rawBitmapData = (UINT*)bitmapData->Scan0;
	int padding = 0;
	for (int x = width - 1; x >= 0; x--) {
		for (int y = 0; y < height; y++) {
			UINT color = rawBitmapData[y * bitmapData->Stride / 4 + x];
			int red = (color & 0xff0000) >> 16;
			if (red != NULL) {
				return padding;
			}
		}
		padding++;
	}
	return padding;
}

Surface::Surface(const Font * font, const TextSettings *settings, const std::string &text) {
	init();
	int outline = settings->outlineWidth;
	bool antialias = (settings == NULL || settings->antialias) && outline==0;

	HWND hDesktopWnd = GetDesktopWindow();
	HDC hDesktopDC = GetDC(hDesktopWnd);
	HDC hCaptureDC = CreateCompatibleDC(hDesktopDC);

	Gdiplus::Graphics * graphics = new Gdiplus::Graphics(hCaptureDC);
	std::wstring s = s2ws(text);
	Gdiplus::RectF layoutRect(0, 0, 2560, 1600);
	Gdiplus::RectF boundRect;

	graphics->MeasureString(s.c_str(), -1, *font, layoutRect, &boundRect);

	Gdiplus::Color color = settings == NULL ?
		Gdiplus::Color::White :
		Gdiplus::Color::MakeARGB(settings->color.a, settings->color.r, settings->color.g, settings->color.b);

	boundRect.Height = font->ascent + font->descent + outline * 2;
	boundRect.Width += outline * 2;

	delete graphics;

	Gdiplus::Bitmap bitmap((int) ceil(boundRect.Width), (int) ceil(boundRect.Height), PixelFormat32bppARGB);
	Gdiplus::Graphics *g = Gdiplus::Graphics::FromImage(&bitmap);

	g->SetTextRenderingHint(antialias ?
		Gdiplus::TextRenderingHintAntiAlias :
		Gdiplus::TextRenderingHintSingleBitPerPixelGridFit
	);

	Gdiplus::SolidBrush redbrush(Gdiplus::Color::Red);
	g->DrawString(s.c_str(), -1, *font, Gdiplus::PointF(0, 0), &redbrush);

	UINT oldWidth = bitmap.GetWidth();
	UINT oldHeight = bitmap.GetHeight();

	Gdiplus::BitmapData* bitmapData = new Gdiplus::BitmapData;
	Gdiplus::Rect rect(0, 0, oldWidth, oldHeight);
	bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, bitmapData);

	padl = findLeftPadding(bitmapData, oldWidth, oldHeight);
	padr = findRightPadding(bitmapData, oldWidth, oldHeight);

	if (padl == oldWidth && padr == oldWidth) {
		padl = 0;
		padr = 0;
	}

	bitmap.UnlockBits(bitmapData);
	delete bitmapData;
	delete g;

	Gdiplus::Bitmap target((int)oldWidth + 2 * outline - padl - padr, (int)oldHeight, PixelFormat32bppARGB);
	g = Gdiplus::Graphics::FromImage(&target);

	g->SetTextRenderingHint(antialias ?
		Gdiplus::TextRenderingHintAntiAlias :
		Gdiplus::TextRenderingHintSingleBitPerPixelGridFit
	);

	//Gdiplus::Pen pen(Gdiplus::Color(255, 255, 0, 0), 4.0f);
	//g->DrawRectangle(&pen, 0, 0, (int)oldWidth + 2 * outline - padl - padr, (int)oldHeight);

	Gdiplus::SolidBrush brush(color);
	Gdiplus::PointF origin((Gdiplus::REAL) (outline - padl), (Gdiplus::REAL) outline);
	g->DrawString(s.c_str(), -1, *font, origin, &brush);
	delete g;

	setBitmap(&target);

	ReleaseDC(hDesktopWnd, hDesktopDC);
	DeleteDC(hCaptureDC);

	addOutline(outline, &settings->outlineColor);
}

void Surface::addOutline(int levels, const Color *color) {
	if(levels == 0) return;

	int colorValue = (0xff << 24) | (color->b << 16) | (color->g << 8) | (color->r);

	int w = width;
	int h = height;

	unsigned char *data = new unsigned char[w * h * 4];
	unsigned char *data2 = new unsigned char[w * h * 4];

	memcpy(data, pixelData, 4 * w * h);

	for(int i = 0; i < levels; i++) {
		memcpy(data2, data, 4 * w * h);

		for(int y = 0; y < h; y++) {
			for(int x = 0; x < w; x++) {
				unsigned char *p = &data[(x + y * w) * 4];
				unsigned char *p2 = &data2[(x + y * w) * 4];
				if(p[3] > 128) {
					if(x > 0 && p[-1] < 128)    ((int *) p2)[-1] = colorValue;
					if(x + 1 < w && p[7] < 128) ((int *) p2)[+1] = colorValue;
				}
			}
		}

		memcpy(data, data2, 4 * w * h);

		for(int y = 0; y < h; y++) {
			for(int x = 0; x < w; x++) {
				unsigned char *p = &data[(x + y * w) * 4];
				unsigned char *p2 = &data2[(x + y * w) * 4];
				if(p[3] > 128) {
					if(y > 0 && p[-w * 4 + 3] < 128)    ((int *) p2)[-w] = colorValue;
					if(y + 1 < h && p[w * 4 + 3] < 128) ((int *) p2)[+w] = colorValue;
				}
			}
		}

		unsigned char *tmp = data;
		data = data2;
		data2 = tmp;
	}

	memcpy(pixelData, data, 4 * w * h);

	delete[] data;
	delete[] data2;
}

bool Surface::isValid() {
	if(this == NULL) return false;
	if(pixelData == NULL) return false;

	return true;
}


Surface::Surface(Surface *parent, int levels, Color *color) {
	init();

	if(!parent->isValid()) return;

	int w = parent->w();
	int h = parent->h();

	unsigned char *data = new unsigned char[w * h * 4];

	memcpy(data, parent->pixelData, 4 * w * h);

	pixelData = (unsigned char *) data;

	width = w;
	height = h;

	addOutline(levels, color);

	createSurfaceFromPixelData(w, h);
}

Surface::Surface(int scaling, Surface *parent) {
	init();
	if(! parent->isValid()) return;

	int w = parent->w();
	int h = parent->h();

	int neww = w * scaling;
	int newh = h * scaling;

	Uint32 *data = new Uint32[neww * newh];
	Uint32 *pixels = (Uint32 *) parent->pixelData;

	for(int x = 0; x < w; x++) {
		for(int y = 0; y < h; y++) {
			Uint32 pixel = pixels[x + y * w];
			for(int x1 = 0; x1 < scaling; x1++) {
				for(int y1 = 0; y1 < scaling; y1++) {
					data[(x * scaling + x1) + (y * scaling + y1) * neww] = pixel;
				}
			}
		}
	}

	pixelData = (unsigned char *) data;
	createSurfaceFromPixelData(neww, newh);
}

Surface::Surface(Surface *parent, std::vector<Color *> colormap) {
	init();
	if(!parent->isValid()) return;

	std::map<Uint32, Uint32> map;
	for(unsigned int i = 0; i + 1 < colormap.size(); i += 2) {
		Color *from = colormap[i];
		Color *to = colormap[i+1];

		Uint32 fromPx = from->r | (from->g << 8) | (from->b << 16);
		Uint32 toPx = to->r | (to->g << 8) | (to->b << 16);

		map[fromPx] = toPx;
	}

	int w = parent->w();
	int h = parent->h();

	Uint32 *data = new Uint32[w * h];
	Uint32 *pixels = (Uint32 *) parent->pixelData;

	for(int x = 0; x < w; x++) {
		for(int y = 0; y < h; y++) {
			Uint32 pixel = pixels[x + y * w];

			Uint32 color = pixel & 0x00ffffff;
			auto iter = map.find(color);
			if(iter != map.end()) {
				Uint32 alpha = pixel & 0xff000000;
				data[x + y * w] = iter->second | alpha;
			} else {
				data[x + y * w] = pixel;
			}
		}
	}

	pixelData = (unsigned char *) data;
	createSurfaceFromPixelData(w, h);
}

Surface::Surface(Surface *parent, Color *color) {
	init();
	if(!parent->isValid()) return;

	int w = parent->w();
	int h = parent->h();

	Uint32 *data = new Uint32[w * h];
	Uint32 *pixels = (Uint32 *) parent->pixelData;

	Uint32 rScale = 0x000000ff;
	Uint32 gScale = 0x0000ff00;
	Uint32 bScale = 0x00ff0000;
	for(int x = 0; x < w; x++) {
		for(int y = 0; y < h; y++) {
			Uint32 pixel = pixels[x + y * w];
			Uint32 r = ((pixel & 0x000000ff) * color->r / 0xff) & 0x000000ff;
			Uint32 g = ((pixel & 0x0000ff00) * color->g / 0xff) & 0x0000ff00;
			Uint32 b = ((pixel & 0x00ff0000) * color->b / 0xff) & 0x00ff0000;
			Uint32 a = (((pixel >> 24) & 0x000000ff) * color->a / 0xff) << 24;
			data[x + y * w] = a | r | g | b;
		}
	}

	pixelData = (unsigned char *) data;
	createSurfaceFromPixelData(w, h);
}

// type is currently unused, but provided to make this easier to extend in the future
Surface::Surface(Surface *parent, SurfaceTransform type) {
	init();
	if(!parent->isValid()) return;

	int w = parent->w();
	int h = parent->h();

	Uint32 *data = new Uint32[w * h];
	Uint32 *pixels = (Uint32 *) parent->pixelData;

	for(int x = 0; x < w; x++) {
		for(int y = 0; y < h; y++) {
			Uint32 pixel = pixels[x + y * w];
			Uint32 r = pixel & 0x000000ff;
			Uint32 g = (pixel >> 8) & 0x000000ff;
			Uint32 b = (pixel >> 16) & 0x000000ff;
			Uint32 gray = (21 * r + 72 * g + 7 * b) / 100;
			data[x + y * w] = pixel & 0xff000000 | gray | (gray << 8) | (gray << 16);
		}
	}

	pixelData = (unsigned char *) data;
	createSurfaceFromPixelData(w, h);
}

Surface::Surface() {
	init();
}

bool Surface::wasDrawn() {
	auto iter = lastFrameMap.find(hash);

	if(iter == lastFrameMap.end())
		return false;

/*	GLint mat[4];
	glGetIntegerv(GL_VIEWPORT, mat);

	x = iter->second.x * mat[2];
	y = iter->second.y * mat[3];*/

	x = iter->second.x;
	y = iter->second.y;

	return true;
}

SurfaceScreenshot::SurfaceScreenshot() {
	SDL_Window *window = SDL_GL_GetCurrentWindow();
	if(window == NULL) window = globalWindow;

	int w, h;
	SDL_GL_GetDrawableSize(window, &w, &h);

	unsigned char* pixels = new unsigned char[4 * w * h];
	glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels);

	setBitmap(pixels, 0, 0, w, h, -w * 4);

	delete pixels;
};

Screen::Screen() {
	window = SDL_GL_GetCurrentWindow();
	if(window == NULL) window = globalWindow;
}

void Screen::begin() {
	int w, h;
	SDL_GL_GetDrawableSize(window, &w, &h);

	glViewport(0, 0, w, h);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0.0, w, h, 0.0, -1.0, 1.0);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();

	glLoadIdentity();
	glDisable(GL_LIGHTING);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Screen::finishWithoutSwapping() {
	glDisable(GL_TEXTURE_2D);
	glPopMatrix();

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();

	glMatrixMode(GL_MODELVIEW);
}

void Screen::finish() {
	finishWithoutSwapping();
	SDL_GL_SwapWindow(window);
}

void Screen::blitRect(Surface *src, Rect *srcRect, Rect *destRect, Color *color) {
	if(!src->isValid()) return;

	int x1 = destRect->x;
	int y1 = destRect->y;
	int x2 = destRect->x + destRect->w;
	int y2 = destRect->y + destRect->h;

	glColor4f( (float)color->r / 0xFF,
			   (float)color->g / 0xFF,
			   (float)color->b / 0xFF,
			   (float)color->a / 0xFF);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, src->texture());

	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex3i(x1, y1, 0);
	glTexCoord2f(0, 1); glVertex3i(x1, y2, 0);
	glTexCoord2f(1, 1); glVertex3i(x2, y2, 0);
	glTexCoord2f(1, 0); glVertex3i(x2, y1, 0);
	glEnd();
}

void Screen::blit(Surface *src, Rect *srcRect, int destx, int desty) {
	if(!src->isValid()) return;

	Rect destRect = { destx, desty, src->w(), src->h() };

	blitRect(src, srcRect, &destRect, &Color::White);
}
void Screen::drawrect(Color *color, Rect *rect) {
	glColor4f(color->r/255.0f, color->g / 255.0f, color->b / 255.0f, color->a / 255.0f);
	glDisable(GL_TEXTURE_2D);

	int x1, y1, x2, y2;
	if(rect == NULL) {
		int w, h;
		SDL_GL_GetDrawableSize(window, &w, &h);

		x1 = 0;
		y1 = 0;
		x2 = w;
		y2 = h;
	} else {
		x1 = rect->x;
		y1 = rect->y;
		x2 = rect->x + rect->w;
		y2 = rect->y + rect->h;
	}


	glBegin(GL_QUADS);
	glVertex3i(x1, y1, 0);
	glVertex3i(x1, y2, 0);
	glVertex3i(x2, y2, 0);
	glVertex3i(x2, y1, 0);
	glEnd();
}

void Screen::clip(Rect *rect) {
	clippingRects.push_back(*rect);

	applyClipping();
}

void Screen::unclip() {
	if(!clippingRects.empty())
		clippingRects.pop_back();
	
	applyClipping();
}

void Screen::mask(Rect* rect) {
	glEnable(GL_STENCIL_TEST);

	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilMask(0xFF);

	maskRects.push_back(*rect);
	drawrect(&Color::Transparent, rect);

	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_EQUAL, 0, 0xFF);
	glStencilMask(0x00);
}

void Screen::unmask(size_t count) {
	glEnable(GL_STENCIL_TEST);

	glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
	glStencilFunc(GL_ALWAYS, 1, 0xFF);
	glStencilMask(0xFF);

	while (count-- > 0 && !maskRects.empty()) {
		auto rect = maskRects.back();

		drawrect(&Color::Transparent, &rect);

		maskRects.pop_back();
	}

	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_EQUAL, 0, 0xFF);
	glStencilMask(0x00);

	if (maskRects.empty())
		glDisable(GL_STENCIL_TEST);
}

void Screen::clearmask() {
	glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_ALWAYS, 0, 0xFF);
	glStencilMask(0xFF);

	maskRects.clear();

	glClear(GL_STENCIL_BUFFER_BIT);
	glDisable(GL_STENCIL_TEST);
}

void Screen::applyClipping() {
	if(clippingRects.empty()) {
		glDisable(GL_SCISSOR_TEST);
	} else {
		int w, h;
		SDL_GL_GetDrawableSize(window, &w, &h);

		Rect *rect = &clippingRects.at(clippingRects.size() - 1);
		glScissor(rect->x, h - rect->y - rect->h, rect->w, rect->h);
		glEnable(GL_SCISSOR_TEST);
	}
}

Rect* Screen::getClipRect() {
	if (clippingRects.empty())
		return NULL;

	return &clippingRects.at(clippingRects.size() - 1);
}

DrawHook::DrawHook() {
	hookListDraw.push_back(this);
}
DrawHook::~DrawHook() {
	hookListDraw.erase(std::remove(hookListDraw.begin(), hookListDraw.end(), this), hookListDraw.end());
}

EventHook::EventHook() {
	hookListEvents.push_back(this);
}
EventHook::~EventHook() {
	hookListEvents.erase(std::remove(hookListEvents.begin(), hookListEvents.end(), this), hookListEvents.end());
}

bool EventLoop::next() {
	if(SDL_PollEvent(&event) == 0)
		return false;

	return true;
}

int Event::type() {
	return event.type;
}

int Event::mousebutton() {
	return event.button.button;
}

int Event::x() {
	return event.motion.x;
}

int Event::y() {
	return event.motion.y;
}

int Event::wheely() {
	return event.wheel.y;
}
int Event::keycode() {
	return event.key.keysym.sym;
}
int Event::textinput(lua_State* L) {
	lua_pushstring(L, event.text.text);
	return 1;
}






int mousex() {
	int x, y;
	SDL_GetMouseState(&x, &y);

	return x;
}

int mousey() {
	int x, y;
	SDL_GetMouseState(&x, &y);

	return y;
}

Timer::Timer() {
	reset();
}

int Timer::elapsed() {
	return SDL_GetTicks() - startTime;
}

void Timer::reset() {
	startTime = SDL_GetTicks();
}

HWND currentWindowHandle = NULL;
HWND getCurrentWindowHandle() {
	if (currentWindowHandle == NULL)
		currentWindowHandle = FindWindowA(NULL, "Into the Breach");

	return currentWindowHandle;
}

void setClipboardData(std::string text) {
	HWND currentWindow = getCurrentWindowHandle();

	if (currentWindow == NULL)
		return;
	if (!OpenClipboard(currentWindow))
		return;

	const size_t length = text.length();
	HANDLE hMem = GlobalAlloc(GMEM_MOVEABLE, length + 1);
	if (hMem == nullptr) {
		CloseClipboard();
		return;
	}

	char* c_str = static_cast<char*>(GlobalLock(hMem));
	memcpy(c_str, text.c_str(), length);
	c_str[length] = NULL;
	GlobalUnlock(hMem);

	EmptyClipboard();
	SetClipboardData(CF_TEXT, hMem);
	CloseClipboard();
}

std::string getClipboardData() {
	HWND currentWindow = getCurrentWindowHandle();

	if (currentWindow == NULL)
		return "";
	if (!IsClipboardFormatAvailable(CF_TEXT))
		return "";
	if (!OpenClipboard(currentWindow))
		return "";

	HANDLE hMem = GetClipboardData(CF_TEXT);
	if (hMem == nullptr) {
		CloseClipboard();
		return "";
	}

	char* c_str = static_cast<char*>(GlobalLock(hMem));
	if (c_str == nullptr) {
		CloseClipboard();
		return "";
	}

	std::string text(c_str);
	GlobalUnlock(hMem);
	CloseClipboard();

	return text;
}

}

