# Builds the R Departures screen saver add-on on Haiku. On an x86_gcc2 hybrid
# the default g++ is gcc2, which is what the screen_blanker there loads.
#
# The boards are the app's: git submodule update --init
NAME     = RDeparturesSaver
CXX     ?= g++
CXXFLAGS = -O2 -Wall -Wno-multichar -fPIC -Irdepartures/src
LIBS     = -lbe -lscreensaver
SRCS     = src/Saver.cpp rdepartures/src/Board.cpp rdepartures/src/Data.cpp \
	rdepartures/src/Station.cpp

ADDONS = $(HOME)/config/non-packaged/add-ons/Screen Savers
FONTS  = $(HOME)/config/non-packaged/data/fonts/otfonts

$(NAME): $(SRCS)
	$(CXX) $(CXXFLAGS) -shared -o $(NAME) $(SRCS) $(LIBS)

# Then pick R Departures in ScreenSaver preferences.
install: $(NAME)
	mkdir -p "$(ADDONS)" "$(FONTS)"
	cp $(NAME) "$(ADDONS)/R Departures"
	cp rdepartures/fonts/texgyreheroscn-bold.otf \
		"$(FONTS)/RDepartures-texgyreheroscn-bold.otf"

# Runs the add-on into a PNG file (test/render.cpp), for checking over ssh.
render: test/render.cpp
	$(CXX) -O2 -Wno-multichar -o render test/render.cpp -lbe -ltranslation -lscreensaver

clean:
	rm -f $(NAME) render

.PHONY: install clean
