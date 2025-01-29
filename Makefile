all: seed_finder_linux seed_finder_windows.exe

seed_finder_linux: seed_finder.c
	gcc -g -o seed_finder_linux seed_finder.c naett/naett.c cubiomes/libcubiomes.a cJSON/cJSON.c -lm -lcurl -O3 -fopenmp

seed_finder_windows.exe: seed_finder.c
	x86_64-w64-mingw32-gcc -static -g -o seed_finder_windows.exe seed_finder.c naett/naett.c cubiomes/*.c cJSON/cJSON.c -lm -lwinhttp -O3 -fopenmp