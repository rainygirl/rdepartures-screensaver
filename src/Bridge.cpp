/*
 * The screen saver add-on for an x86_gcc2 hybrid.
 *
 * The screen_blanker there is a gcc2 program and loads only gcc2 add-ons,
 * while the boards are built with the hybrid's x86 compiler. So this add-on
 * draws nothing itself: it starts src/Renderer.cpp, which draws the board
 * into an area the two share, and puts on the screen what the renderer says
 * has changed. Only this file is gcc2 code; no C++11.
 *
 * The renderer is in the package's bin/x86, found from this add-on's path.
 */

#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScreenSaver.h>
#include <String.h>
#include <StringView.h>
#include <View.h>

#include <image.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>


static const bigtime_t kTick = 1000000 / 30;
// The preview in ScreenSaver preferences is a few dozen pixels wide: the
// board is laid out at this width and scaled down into it.
static const float kPreviewWidth = 960;
static const int32 kDirtyMessage = 'rdrt';
static const int32 kMaxRects = 64;

enum {
	MSG_BOARD = 'rbrd'
};


class DeparturesSaver : public BScreenSaver {
public:
							DeparturesSaver(BMessage* archive, image_id id);
	virtual					~DeparturesSaver();

	virtual	status_t		StartSaver(BView* view, bool preview);
	virtual	void			StopSaver();
	virtual	void			Draw(BView* view, int32 frame);
	virtual	void			StartConfig(BView* view);
	virtual	status_t		SaveState(BMessage* into) const;

			BString			RendererPath() const;

			BString			fBoard;
			// Bumped by the config view: the next Draw() starts the new
			// board.
			volatile int32	fGeneration;

private:
			bool			_Start(BRect bounds);
			void			_Stop();

			BString			fRendererPath;
			bool			fPreview;
			int32			fRunningGeneration;
			BBitmap*		fBitmap;
			area_id			fArea;
			uint8*			fFrame;
			port_id			fPort;
			team_id			fRenderer;
			bool			fFailed;
};


class ConfigView : public BView {
public:
							ConfigView(DeparturesSaver* saver);
	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

private:
			DeparturesSaver* fSaver;
			BPopUpMenu*		fMenu;
};


// #pragma mark - DeparturesSaver


DeparturesSaver::DeparturesSaver(BMessage* archive, image_id id)
	:
	BScreenSaver(archive, id),
	fGeneration(0),
	fPreview(false),
	fRunningGeneration(-1),
	fBitmap(NULL),
	fArea(-1),
	fFrame(NULL),
	fPort(-1),
	fRenderer(-1),
	fFailed(false)
{
	const char* code;
	if (archive != NULL && archive->FindString("board", &code) == B_OK)
		fBoard = code;

	// <prefix>/add-ons/Screen Savers/R Departures -> <prefix>/bin/x86/...
	image_info info;
	if (get_image_info(id, &info) == B_OK) {
		BPath path(info.name);
		BPath prefix;
		if (path.GetParent(&prefix) == B_OK && prefix.GetParent(&prefix) == B_OK
			&& prefix.GetParent(&prefix) == B_OK) {
			prefix.Append("bin/x86/rdepartures-saver-renderer");
			fRendererPath = prefix.Path();
		}
	}
	SetTickSize(kTick);
}


DeparturesSaver::~DeparturesSaver()
{
	_Stop();
}


BString
DeparturesSaver::RendererPath() const
{
	return fRendererPath;
}


status_t
DeparturesSaver::SaveState(BMessage* into) const
{
	into->AddString("board", fBoard.String());
	return B_OK;
}


status_t
DeparturesSaver::StartSaver(BView* view, bool preview)
{
	fPreview = preview;
	fRunningGeneration = -1;
	fFailed = false;
	return B_OK;
}


void
DeparturesSaver::StopSaver()
{
	_Stop();
}


bool
DeparturesSaver::_Start(BRect bounds)
{
	_Stop();
	BRect frame = bounds.OffsetToCopy(B_ORIGIN);
	if (fPreview) {
		frame.Set(0, 0, kPreviewWidth - 1, floorf(kPreviewWidth
			* (bounds.Height() + 1) / (bounds.Width() + 1)) - 1);
	}
	int32 width = frame.IntegerWidth() + 1;
	int32 height = frame.IntegerHeight() + 1;

	size_t size = ((size_t)width * height * 4 + B_PAGE_SIZE - 1)
		& ~(size_t)(B_PAGE_SIZE - 1);
	fArea = create_area("rdepartures saver frame", (void**)&fFrame,
		B_ANY_ADDRESS, size, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA
			| B_CLONEABLE_AREA);
	fPort = create_port(kMaxRects, "rdepartures saver dirty");
	fBitmap = new BBitmap(frame, B_RGB32);
	if (fArea < 0 || fPort < 0 || fBitmap->InitCheck() != B_OK)
		return false;
	memset(fFrame, 0, size);

	BString board = fBoard.Length() > 0 ? fBoard : BString("ICN");
	char w[16], h[16], a[16], p[16];
	sprintf(w, "%ld", (long)width);
	sprintf(h, "%ld", (long)height);
	sprintf(a, "%ld", (long)fArea);
	sprintf(p, "%ld", (long)fPort);
	const char* args[] = { fRendererPath.String(), board.String(), w, h, a, p,
		NULL };
	extern char** environ;
	thread_id thread = load_image(6, args, (const char**)environ);
	if (thread < 0)
		return false;
	fRenderer = thread;
	resume_thread(thread);
	return true;
}


void
DeparturesSaver::_Stop()
{
	// Deleting the port is what tells the renderer to quit; the kill is for
	// one that is stuck.
	if (fPort >= 0)
		delete_port(fPort);
	if (fRenderer >= 0) {
		kill(fRenderer, SIGKILL);
		status_t result;
		wait_for_thread(fRenderer, &result);
	}
	if (fArea >= 0)
		delete_area(fArea);
	delete fBitmap;
	fBitmap = NULL;
	fArea = -1;
	fFrame = NULL;
	fPort = -1;
	fRenderer = -1;
}


void
DeparturesSaver::Draw(BView* view, int32 frame)
{
	BRect bounds = view->Bounds();
	if (fRunningGeneration != fGeneration) {
		fRunningGeneration = fGeneration;
		fFailed = !_Start(bounds);
		if (fFailed)
			_Stop();
	}
	if (fFailed || fBitmap == NULL)
		return;

	BRegion dirty;
	int32 rects[kMaxRects * 4];
	int32 code;
	ssize_t bytes;
	while ((bytes = read_port_etc(fPort, &code, rects, sizeof(rects),
			B_RELATIVE_TIMEOUT, 0)) >= 0) {
		for (int32 i = 0; i + 3 < bytes / (int32)sizeof(int32); i += 4)
			dirty.Include(BRect(rects[i], rects[i + 1], rects[i + 2],
				rects[i + 3]));
	}
	if (dirty.CountRects() == 0)
		return;

	uint8* bits = (uint8*)fBitmap->Bits();
	int32 bpr = fBitmap->BytesPerRow();
	int32 width = fBitmap->Bounds().IntegerWidth() + 1;
	for (int32 i = 0; i < dirty.CountRects(); i++) {
		clipping_rect r = dirty.RectAtInt(i);
		int32 rowBytes = (r.right - r.left + 1) * 4;
		for (int32 y = r.top; y <= r.bottom; y++) {
			memcpy(bits + y * bpr + r.left * 4,
				fFrame + (y * width + r.left) * 4, rowBytes);
		}
	}

	if (fPreview) {
		view->DrawBitmap(fBitmap, fBitmap->Bounds(), bounds,
			B_FILTER_BITMAP_BILINEAR);
		return;
	}
	for (int32 i = 0; i < dirty.CountRects(); i++) {
		BRect r = dirty.RectAt(i);
		view->DrawBitmap(fBitmap, r, r);
	}
}


// #pragma mark - configuration


void
DeparturesSaver::StartConfig(BView* view)
{
	BLayoutBuilder::Group<>(view, B_VERTICAL)
		.Add(new ConfigView(this));
}


// The boards come from the renderer, which has them built in. Laid out
// with the layout API: the view the preferences hand to StartConfig() has
// not been sized yet, so frames computed from it come out empty.
ConfigView::ConfigView(DeparturesSaver* saver)
	:
	BView("rdepartures config", B_WILL_DRAW),
	fSaver(saver)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	BStringView* title = new BStringView("title", "R Departures");
	title->SetFont(be_bold_font);
	BStringView* about = new BStringView("about",
		"A split-flap departures board, for RenkuOS.");

	fMenu = new BPopUpMenu("board");
	BString command;
	command << "'" << saver->RendererPath() << "' --list";
	FILE* list = popen(command.String(), "r");
	char line[256];
	bool first = true;
	while (list != NULL && fgets(line, sizeof(line), list) != NULL) {
		line[strcspn(line, "\r\n")] = '\0';
		if (strcmp(line, "-") == 0) {
			fMenu->AddSeparatorItem();
			continue;
		}
		char* tab = strchr(line, '\t');
		if (tab == NULL)
			continue;
		*tab = '\0';
		BMessage* message = new BMessage(MSG_BOARD);
		message->AddString("board", line);
		BMenuItem* item = new BMenuItem(tab + 1, message);
		item->SetMarked(saver->fBoard.ICompare(line) == 0
			|| (first && saver->fBoard.Length() == 0));
		fMenu->AddItem(item);
		first = false;
	}
	if (list != NULL)
		pclose(list);
	BMenuField* field = new BMenuField("board", "Board:", fMenu);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_DEFAULT_SPACING)
		.Add(title)
		.Add(about)
		.AddStrut(be_plain_font->Size())
		.Add(field)
		.AddGlue();
}


void
ConfigView::AttachedToWindow()
{
	fMenu->SetTargetForItems(this);
}


void
ConfigView::MessageReceived(BMessage* message)
{
	const char* code;
	if (message->what == MSG_BOARD
		&& message->FindString("board", &code) == B_OK) {
		fSaver->fBoard = code;
		fSaver->fGeneration++;
		return;
	}
	BView::MessageReceived(message);
}


// #pragma mark - entry point


extern "C" _EXPORT BScreenSaver*
instantiate_screen_saver(BMessage* message, image_id image)
{
	return new DeparturesSaver(message, image);
}
