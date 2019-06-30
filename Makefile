default: main

all: main main.html

%: %.cpp
	g++ -O -Wall -I/usr/local/include/SDL2 -std=c++11 -lSDL2 -lSDL2_image -lSDL2_ttf $< -o $@

%.html: %.cpp
	emcc $< -std=c++11 -s USE_SDL=2 -s USE_SDL_TTF=2 -s USE_SDL_IMAGE=2 -s SDL2_IMAGE_FORMATS='["png"]' -o $@ --preload-file data

clean:
	rm -f main main.html main.data main.wasm main.js
