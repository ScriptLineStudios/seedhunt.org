seed_finder: seed_finder.c
	nvcc -o seed_finder seed_finder.c naett/naett.c cubiomes/libcubiomes.a cJSON/cJSON.c -lm -lcurl -O3