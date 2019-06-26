all: main main.html

%: %.cpp
	g++ -O -Wall -I/usr/local/include/SDL2 -std=c++11 -lSDL2 -lSDL2_image -lSDL2_ttf $< -o $@

%.html: %.cpp
	emcc $< -std=c++11 -s USE_SDL=2 -s USE_SDL_TTF=2 -o $@ --preload-file data

clean:
	rm -f main main.html
