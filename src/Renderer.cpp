/*
 * The boards for the x86_gcc2 build of the screen saver, in a process of
 * their own.
 *
 * On an x86_gcc2 hybrid the screen_blanker is a gcc2 program and loads only
 * gcc2 add-ons, but the boards are built with the hybrid's x86 compiler. So
 * the add-on there is src/Bridge.cpp, which starts this program, and the two
 * share a frame buffer:
 *
 *   rdepartures-saver-renderer <board> <width> <height> <area> <port>
 *
 * draws <board> (an airport or station code, as in the settings) at that
 * size into the area, B_RGB32 rows of width * 4 bytes, and after each frame
 * writes the rectangles that changed to the port, four int32s each: left,
 * top, right, bottom. It exits when the port goes away, which it does when
 * the screen saver stops or the screen_blanker dies.
 *
 *   rdepartures-saver-renderer --list
 *
 * prints the boards for the preferences' menu, "code<TAB>name" per line,
 * with a line "-" between the airports and the stations.
 */

#include "Board.h"
#include "Station.h"

#include <Application.h>
#include <Bitmap.h>
#include <OS.h>
#include <Region.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>


// What Board.cpp expects of the program around it.
int64 gTimeOffset = 0;

void
save_airport_setting(const char* code)
{
}


static const bigtime_t kTick = 1000000 / 30;
static const int32 kDirtyMessage = 'rdrt';
// Rectangles per message; a frame with more is sent as its bounds.
static const int32 kMaxRects = 64;


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


static int
list_boards()
{
	for (int i = 0; i < kAirportCount + kStationCount; i++) {
		if (i == kAirportCount)
			printf("-\n");
		printf("%s\t%s\n", board_code(i), board_name(i));
	}
	return 0;
}


static void
copy_rect(const BBitmap* bitmap, uint8* frame, int32 width, BRect r)
{
	const uint8* bits = (const uint8*)bitmap->Bits();
	int32 bpr = bitmap->BytesPerRow();
	int32 left = (int32)r.left;
	int32 bytes = ((int32)r.right - left + 1) * 4;
	for (int32 y = (int32)r.top; y <= (int32)r.bottom; y++) {
		memcpy(frame + (y * width + left) * 4, bits + y * bpr + left * 4,
			bytes);
	}
}


int
main(int argc, char** argv)
{
	if (argc == 2 && strcmp(argv[1], "--list") == 0)
		return list_boards();
	if (argc != 6) {
		fprintf(stderr, "usage: %s <board> <width> <height> <area> <port>\n"
			"       %s --list\n", argv[0], argv[0]);
		return 1;
	}

	int32 width = atoi(argv[2]);
	int32 height = atoi(argv[3]);
	port_id port = atoi(argv[5]);
	uint8* frame;
	area_id area = clone_area("rdepartures saver frame", (void**)&frame,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, atoi(argv[4]));
	if (width <= 0 || height <= 0 || area < 0)
		return 1;
	area_info info;
	if (get_area_info(area, &info) != B_OK
		|| info.size < (size_t)width * height * 4) {
		return 1;
	}

	// Not run: the board draws into its own bitmap, which needs only the
	// connection to the app_server that constructing the application makes.
	BApplication app("application/x-vnd.rainygirl-RDeparturesSaverRenderer");

	BRect bounds(0, 0, width - 1, height - 1);
	BoardView board(bounds, board_index(argv[1]));
	board.SetShowGear(false);
	board.LayoutBoard(bounds);

	// What changed but has not been announced: the port is full when the
	// screen_blanker falls behind, and the pixels stay in the area anyway.
	BRegion pending;
	int32 rects[kMaxRects * 4];
	bigtime_t next = system_time();
	while (true) {
		board.Tick();
		BRegion dirty;
		board.TakeDirty(dirty);
		BBitmap* bitmap = board.Bitmap();
		if (dirty.CountRects() > 0 && bitmap->Lock()) {
			for (int32 i = 0; i < dirty.CountRects(); i++)
				copy_rect(bitmap, frame, width, dirty.RectAt(i));
			bitmap->Unlock();
			pending.Include(&dirty);
		}

		if (pending.CountRects() > 0) {
			int32 count = 0;
			if (pending.CountRects() > kMaxRects) {
				BRect r = pending.Frame();
				rects[0] = (int32)r.left;
				rects[1] = (int32)r.top;
				rects[2] = (int32)r.right;
				rects[3] = (int32)r.bottom;
				count = 1;
			} else {
				for (; count < pending.CountRects(); count++) {
					BRect r = pending.RectAt(count);
					rects[count * 4] = (int32)r.left;
					rects[count * 4 + 1] = (int32)r.top;
					rects[count * 4 + 2] = (int32)r.right;
					rects[count * 4 + 3] = (int32)r.bottom;
				}
			}
			status_t status = write_port_etc(port, kDirtyMessage, rects,
				count * 4 * sizeof(int32), B_RELATIVE_TIMEOUT, 0);
			if (status == B_OK)
				pending.MakeEmpty();
			else if (status != B_WOULD_BLOCK && status != B_TIMED_OUT)
				break;
		} else {
			// Nothing to say this frame; still notice a deleted port.
			port_info portInfo;
			if (get_port_info(port, &portInfo) != B_OK)
				break;
		}

		next += kTick;
		bigtime_t now = system_time();
		if (next < now)
			next = now;
		snooze_until(next, B_SYSTEM_TIMEBASE);
	}
	return 0;
}
