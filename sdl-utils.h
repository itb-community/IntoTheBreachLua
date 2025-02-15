#ifndef __SDL_UTILS__
#define __SDL_UTILS__

#include <windows.h>
#include <SDL.h>
#include <string>
#include <memory>
#include <map>
#include <vector>
#include "Gdiplus.h"

#include "blob.h"
#include "lua.h"

#include "glew/glew.h"
#include <GL/GL.h>
#include <GL/GLU.h>

namespace SDL {

GLuint glTexture(unsigned char *pixelData, int w, int h);

struct Color :public SDL_Color {
	static Color White;
	static Color Black;
	static Color Transparent;
	Color();
	Color(int r, int g, int b, int a);
	Color(int r, int g, int b);
};

struct Rect :public SDL_Rect {
	Rect(int x, int y, int w, int h);
	bool contains(int x, int y);
	bool intersects(Rect*);
	Rect getIntersect(Rect*);
	Rect getUnion(Rect*);
};

struct TextSettings {
	Color color;
	bool antialias;

	Color outlineColor;
	int outlineWidth;

	TextSettings() {
		antialias = true;
		outlineWidth = 0;
	}
};

struct Font {
	Gdiplus::Font *font;
	Gdiplus::FontFamily *family;

	void setFont(Gdiplus::Font *f);

	float ascent;
	float descent;

	Font();
	~Font();
	Font(const std::string &name, double size);

	void defaults();

	operator const Gdiplus::Font *() const {
		return font;
	}
};

struct FileFont :public Font {
	Gdiplus::PrivateFontCollection privateFontCollection;

	FileFont(const std::string &filename, double size);
	FileFont(const Blob *blob, double size);

	void init(double size);
};

// allows adding more single parameter constructors without worry about too many overloads
enum SurfaceTransform { GRAYSCALE };

struct Surface {
	unsigned char *pixelData;
	GLuint textureId;
	unsigned long long hash;
	int width, height;
	double x, y;
	int padl, padr;

	void setBitmap(Gdiplus::Bitmap *bitmap);
	void setBitmap(HBITMAP hbitmap, int x, int y, int w, int h);
	void setBitmap(void *data, int x, int y, int w, int h, int stride);
	void createSurfaceFromPixelData(int w, int h);

	void init();
	Surface();
	Surface(const std::string &filename);
	Surface(Surface *parent, int levels, Color *color);
	Surface(const Font *font, const TextSettings *settings, const std::string &text);
	Surface(int scaling, Surface *parent);
	Surface(Blob *blob);
	Surface(Surface *parent, std::vector<Color *> colormap);
	Surface(Surface* parent, Color* mask);
	Surface(Surface* parent, SurfaceTransform type);

	int w() {
		return width;
	}

	int h() {
		return height;
	}

	int leftPadding() {
		return padl;
	}

	int rightPadding() {
		return padr;
	}

	bool wasDrawn();

	GLint texture() {
		if(textureId == 0 && isValid()) {
			textureId = glTexture(pixelData, width, height);
		}

		return textureId;
	}

	~Surface();

	bool isValid();

protected:
	void addOutline(int levels, const Color *color);
};
static std::vector<Color *> testMap() {
	std::vector<Color *> list;
	list.push_back(new Color(136, 126, 68));
	list.push_back(new Color(255, 0, 0));
	return list;
}
struct SurfaceScreenshot :public Surface {
	SurfaceScreenshot();
};

struct Screen {
	SDL_Window* window;
	std::vector<Rect> clippingRects;
	std::vector<Rect> maskRects;

	Screen();

	int w() {
		int w, h;

		SDL_GL_GetDrawableSize(window, &w, &h);
		return w;
	}

	int h() {
		int w, h;

		SDL_GL_GetDrawableSize(window, &w, &h);
		return h;
	}

	void begin();
	void finishWithoutSwapping();
	void finish();

	void blitRect(Surface *src, Rect *srcRect, Rect *destRect, Color *color);
	void blit(Surface *src, Rect *srcRect, int destx, int desty);
	void drawrect(Color *color, Rect *rect);
	void clip(Rect *rect);
	void unclip();
	void mask(Rect *rect);
	void unmask(size_t count);
	void clearmask();
	Rect* getClipRect();

protected:

	void applyClipping();
};

struct DrawHook {
	DrawHook();
	~DrawHook();
	virtual void draw(Screen & screen) = 0;
};

struct Event {
	SDL_Event event;
	
	int type();

	int mousebutton();
	int x();
	int y();

	int wheely();
	int keycode();
	int textinput(lua_State* L);
};

struct EventHook {
	EventHook();
	~EventHook();
	virtual bool handle(Event & evt) = 0; // return true if event has been handled and should not be sent to game
};

struct EventLoop :public Event{
	bool next();
};

struct Timer {
	Timer();

	int startTime;

	int elapsed();
	void reset();
};

int mousex();
int mousey();

void setClipboardData(std::string);
std::string getClipboardData();

struct Coord { GLdouble x; GLdouble y; Coord() { x = y = 0;  } };

extern std::vector< DrawHook * > hookListDraw;
extern std::vector< EventHook * > hookListEvents;
extern std::map< GLuint, unsigned long long > texturesMap;
extern std::map< unsigned long long, Coord > lastFrameMap;

}

#endif
