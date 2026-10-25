/*
 * Renders the screen saver add-on into a PNG file without the
 * screen_blanker, for checking it over ssh:
 *
 *   render <add-on> <width> <height> <out.png> [board] [seconds] [preview]
 *
 * Runs the saver for that many seconds (default 5) at 30 frames a second,
 * as the screen_blanker would, then saves what is on its view. board is an
 * airport or station code, as in the settings; "preview" lays it out the
 * way the ScreenSaver preferences' preview does.
 */

#include <Application.h>
#include <Bitmap.h>
#include <BitmapStream.h>
#include <File.h>
#include <Message.h>
#include <ScreenSaver.h>
#include <TranslatorRoster.h>
#include <View.h>

#include <image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef BScreenSaver* (*instantiate_func)(BMessage*, image_id);


int
main(int argc, char** argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: render <add-on> <w> <h> <out.png> [board]"
			" [seconds] [preview]\n");
		return 1;
	}
	BApplication app("application/x-vnd.rainygirl-RDepartures-render");
	image_id image = load_add_on(argv[1]);
	instantiate_func instantiate;
	if (image < 0 || get_image_symbol(image, "instantiate_screen_saver",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiate) != B_OK) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}

	BMessage settings;
	if (argc > 5)
		settings.AddString("board", argv[5]);
	float seconds = argc > 6 ? atof(argv[6]) : 5;
	bool preview = argc > 7 && strcmp(argv[7], "preview") == 0;

	BRect r(0, 0, atoi(argv[2]) - 1, atoi(argv[3]) - 1);
	BBitmap bitmap(r, B_RGB32, true);
	BView* view = new BView(r, "screen", B_FOLLOW_NONE, B_WILL_DRAW);
	bitmap.AddChild(view);

	BScreenSaver* saver = instantiate(&settings, image);
	saver->StartSaver(view, preview);
	bigtime_t end = system_time() + (bigtime_t)(seconds * 1000000);
	int32 frame = 0;
	bigtime_t worst = 0;
	while (system_time() < end) {
		bigtime_t start = system_time();
		bitmap.Lock();
		saver->Draw(view, frame++);
		view->Sync();
		bitmap.Unlock();
		bigtime_t took = system_time() - start;
		if (frame > 1 && took > worst)
			worst = took;
		if (took < 33333)
			snooze(33333 - took);
	}
	saver->StopSaver();

	BMessage state;
	saver->SaveState(&state);
	const char* code = "";
	state.FindString("board", &code);
	printf("%d frames, slowest %d ms, board %s\n", (int)frame,
		(int)(worst / 1000), code);

	BBitmap* copy = new BBitmap(&bitmap);
	BBitmapStream stream(copy);
	BFile file(argv[4], B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	status_t status = BTranslatorRoster::Default()->Translate(&stream, NULL,
		NULL, &file, B_PNG_FORMAT);
	printf("%s: %s\n", argv[4], strerror(status));
	delete saver;
	return status == B_OK ? 0 : 1;
}
