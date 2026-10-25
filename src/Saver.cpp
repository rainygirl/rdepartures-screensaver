/*
 * R Departures as a screen saver: the same boards as the app (the
 * rdepartures submodule), drawn by the screen_blanker instead of a window.
 *
 * The app picks its airport or station from the gear in the corner; a
 * screen saver cannot be clicked, so here it is picked in ScreenSaver
 * preferences and kept in the saver's settings.
 *
 * Written so that gcc2 builds it: on an x86_gcc2 hybrid the screen_blanker
 * only loads add-ons of the primary architecture. No C++11.
 */

#include "Board.h"
#include "Station.h"

#include <Bitmap.h>
#include <LayoutBuilder.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Message.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <ScreenSaver.h>
#include <StringView.h>
#include <View.h>

#include <math.h>
#include <string.h>
#include <strings.h>


// What Board.cpp expects of the program around it. The app saves the board
// picked from its gear; the saver has no gear, and saves in SaveState().
int64 gTimeOffset = 0;

void
save_airport_setting(const char* code)
{
}


static const bigtime_t kTick = 1000000 / 30;
// The preview in ScreenSaver preferences is a few dozen pixels wide: the
// board is laid out at this width and scaled down into it.
static const float kPreviewWidth = 960;

enum {
	MSG_BOARD = 'rbrd'
};


static const char*
board_code(int index)
{
	if (index < kAirportCount)
		return kAirports[index].code;
	return kStations[index - kAirportCount].code;
}


static int
board_index(const char* code)
{
	for (int i = 0; i < kAirportCount + kStationCount; i++) {
		if (strcasecmp(board_code(i), code) == 0)
			return i;
	}
	return 0;
}


class DeparturesSaver : public BScreenSaver {
public:
							DeparturesSaver(BMessage* archive, image_id id);
	virtual					~DeparturesSaver();

	virtual	status_t		StartSaver(BView* view, bool preview);
	virtual	void			StopSaver();
	virtual	void			Draw(BView* view, int32 frame);
	virtual	void			StartConfig(BView* view);
	virtual	status_t		SaveState(BMessage* into) const;

			int32			fBoardIndex;
			// Bumped by the config view: the next Draw() starts the new
			// board.
			volatile int32	fGeneration;

private:
			BoardView*		fBoard;
			bool			fPreview;
			int32			fBoardGeneration;
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
	fBoardIndex(0),
	fGeneration(0),
	fBoard(NULL),
	fPreview(false),
	fBoardGeneration(-1)
{
	const char* code;
	if (archive != NULL && archive->FindString("board", &code) == B_OK)
		fBoardIndex = board_index(code);
	SetTickSize(kTick);
}


DeparturesSaver::~DeparturesSaver()
{
	delete fBoard;
}


status_t
DeparturesSaver::SaveState(BMessage* into) const
{
	into->AddString("board", board_code(fBoardIndex));
	return B_OK;
}


status_t
DeparturesSaver::StartSaver(BView* view, bool preview)
{
	fPreview = preview;
	fBoardGeneration = -1;
	return B_OK;
}


void
DeparturesSaver::StopSaver()
{
	delete fBoard;
	fBoard = NULL;
}


void
DeparturesSaver::Draw(BView* view, int32 frame)
{
	BRect bounds = view->Bounds();
	if (fBoard == NULL || fBoardGeneration != fGeneration) {
		fBoardGeneration = fGeneration;
		delete fBoard;
		BRect frame = bounds;
		if (fPreview) {
			frame.Set(0, 0, kPreviewWidth - 1, floorf(kPreviewWidth
				* (bounds.Height() + 1) / (bounds.Width() + 1)) - 1);
		}
		fBoard = new BoardView(frame, fBoardIndex);
		fBoard->SetShowGear(false);
		fBoard->LayoutBoard(frame);
	}

	fBoard->Tick();
	BRegion dirty;
	fBoard->TakeDirty(dirty);
	if (dirty.CountRects() == 0)
		return;
	BBitmap* bitmap = fBoard->Bitmap();
	if (fPreview) {
		view->DrawBitmap(bitmap, bitmap->Bounds(), bounds,
			B_FILTER_BITMAP_BILINEAR);
		return;
	}
	for (int32 i = 0; i < dirty.CountRects(); i++) {
		BRect r = dirty.RectAt(i);
		view->DrawBitmap(bitmap, r, r);
	}
}


// #pragma mark - configuration


void
DeparturesSaver::StartConfig(BView* view)
{
	BLayoutBuilder::Group<>(view, B_VERTICAL)
		.Add(new ConfigView(this));
}


// Laid out with the layout API: the view the preferences hand to
// StartConfig() has not been sized yet, so frames computed from it come out
// empty.
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

	// The airports, a line, the stations: the app's gear menu.
	fMenu = new BPopUpMenu("board");
	for (int i = 0; i < kAirportCount + kStationCount; i++) {
		if (i == kAirportCount)
			fMenu->AddSeparatorItem();
		BMessage* message = new BMessage(MSG_BOARD);
		message->AddInt32("board", i);
		BMenuItem* item = new BMenuItem(board_name(i), message);
		item->SetMarked(i == saver->fBoardIndex);
		fMenu->AddItem(item);
	}
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
	int32 index;
	if (message->what == MSG_BOARD
		&& message->FindInt32("board", &index) == B_OK) {
		fSaver->fBoardIndex = index;
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
