all: main

%: %.cpp
	g++ -O -Wall -I/usr/local/include/SDL2 -std=c++11 -lSDL2 -lSDL2_image -lSDL2_ttf $< -o $@

clean:
	rm main
