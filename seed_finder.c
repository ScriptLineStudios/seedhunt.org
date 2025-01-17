#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#include "cubiomes/finders.h"
#include "cubiomes/generator.h"
#include "cubiomes/rng.h"

#include "cJSON/cJSON.h"
#include "naett/naett.h"

/*
Goal: Find seeds that match the following criteria within 1500 of (0, 0) in each direction

1. All wood types
2. All major biomes
3. All major structures

All major structures:
    - Village
    - Mineshaft
    - Buried treasure
    - Trail ruins
    - Trial chambers
    - Desert pyramid
    - Igloo
    - Jungle pyramid
    - Pillager outpost
    - Swamp hut
    - Woodland mansion
    - Shipwreck
    - Ancient cities
*/

#define RADIUS 1024
#define UNUSED(x) ((void)x)
#define VERSION MC_1_21
#define EXIT() exit(0)

StructureConfig rp_sconf;

bool radius_has_ruined_portal(uint64_t seed) {
    int reg_max = floor(RADIUS / 16.0 / rp_sconf.regionSize);
    int reg_min = -floor(RADIUS / 16.0 / rp_sconf.regionSize);

    Pos p;
    for (int rx = -reg_min; rx <= reg_max; rx++) {
        for (int rz = -reg_min; rz <= reg_max; rz++) {
            if (getStructurePos(Ruined_Portal, VERSION, seed, rx, rz, &p)) {
                return true;
            }
        }
    }
    return false;
}

bool radius_has_structure(Generator *g, int structure, uint64_t seed) {
    StructureConfig sconf;
    getStructureConfig(structure, VERSION, &sconf);

    int reg_max = floor(RADIUS / 16.0 / sconf.regionSize);
    int reg_min = -floor(RADIUS / 16.0 / sconf.regionSize);

    Pos p;
    for (int rx = -reg_min; rx <= reg_max; rx++) {
        for (int rz = -reg_min; rz <= reg_max; rz++) {
            if (!getStructurePos(structure, VERSION, seed, rx, rz, &p)) {
                continue;
            }

            if (isViableStructurePos(structure, g, p.x, p.z, 0)) {
                return true;
            }
        }
    }
    return false;
}

void _setAttemptSeed(uint64_t *s, int cx, int cz) {
    *s ^= (uint64_t)(cx >> 4) ^ ( (uint64_t)(cz >> 4) << 4 );
    setSeed(s, *s);
    next(s, 31);
}

int early_exits = 0;

bool check_seed(Generator *g, uint64_t seed) {
    // this is where the magic happens...

    // this is a nice quick invalidation we can do:
    // StructureConfig sconf;
    // getStructureConfig(Outpost, VERSION, &sconf);

    // int outpost_region = floor(RADIUS / 16.0 / sconf.regionSize);

    // for (int rx = -outpost_region; rx <= outpost_region; rx++) {
    //     for (int rz = -outpost_region; rz <= outpost_region; rz++) {
    //         uint64_t s = seed;
    //         Pos p = getFeaturePos(sconf, seed, rx, rz);
    //         _setAttemptSeed(&s, (p.x) >> 4, (p.z) >> 4);
    //         // can_place[index] = nextInt(&seed, 5) == 0;
    //         // index++;
    //         bool possible_to_place = nextInt(&s, 5) == 0;
    //         if (possible_to_place) {
    //             goto complete;
    //         }
    //     }
    // }
    // early_exits++;
    // return false;

complete:
    StrongholdIter sh;
    Pos p = initFirstStronghold(&sh, VERSION, seed);
    if (p.x > RADIUS || p.x < -RADIUS || p.z > RADIUS || p.z < -RADIUS) {
        early_exits++;
        return false;
    }

    applySeed(g, DIM_OVERWORLD, seed);
    if (!radius_has_structure(g, Mansion, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Village, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Trial_Chambers, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Outpost, seed)) {
        return false;
    }
    // if (!radius_has_structure(g, Igloo, seed)) {
    //     return false;
    // }
    // if (!radius_has_structure(g, Swamp_Hut, seed)) {
    //     return false;
    // }
    // if (!radius_has_structure(g, Desert_Pyramid, seed)) {
    //     return false;
    // }
    // if (!radius_has_structure(g, Jungle_Pyramid, seed)) {
    //     return false;
    // }
    // if (!radius_has_structure(g, Ancient_City, seed)) {
    //     return false;
    // }

    return true;
}

typedef struct {
    char buffer[1024];
    int code;
} Response; 

typedef struct {
    char parts[32][1024];
    size_t size;
} ParsedResponse;

void parse_response_buffer(char *buffer, ParsedResponse *parsed_response) {
    int index = 0;
    int length = 0;
    parsed_response->size++;

    for (size_t i = 0; i < strlen(buffer); i++) {
        if (buffer[i] == ';') {
            parsed_response->parts[index][length] = '\0';
            parsed_response->size++;
            index++;
            length = 0;
        }
        parsed_response->parts[index][length] = buffer[i];
        length++;
    }
}

const char *make_request(const char *url, const char *data, Response *r) {

    naettReq* req = naettRequest(url, naettBody(data, strlen(data)), naettMethod("POST"), naettHeader("accept", "*/*"));
    naettRes* res = naettMake(req);
    
    while (!naettComplete(res)) {
        usleep(100 * 1000);
    }

    int status = naettGetStatus(res);

    int bodyLength = 0;
    const char* body = naettGetBody(res, &bodyLength);
    int code = naettGetStatus(res);

    strcpy(r->buffer, body);
    r->code = code;

    naettClose(res);
    naettFree(req);
}

int sign_in(const char *email, const char *password) {
    printf("[INFO]: Signing in\n");    
    
    cJSON *signin_info = cJSON_CreateObject();
    cJSON *j_email = cJSON_CreateString(email);
    cJSON_AddItemToObject(signin_info, "email", j_email);
    cJSON *j_password = cJSON_CreateString(password);
    cJSON_AddItemToObject(signin_info, "password", j_password);
    
    Response r;
    make_request("http://127.0.0.1:5000/device_signin", cJSON_Print(signin_info), &r);    
    cJSON *json = cJSON_Parse(r.buffer);
    cJSON *json_message = cJSON_GetObjectItemCaseSensitive(json, "message");
    cJSON *json_device_id = cJSON_GetObjectItemCaseSensitive(json, "device_id");

    char *message = json_message->valuestring;
    int device_id = json_device_id->valueint;

    if (device_id < 0) {
        printf("[INFO]: Signin failed!\n");
        exit(1);
    }

    printf("[INFO]: Signin success!\n");
    return device_id;
}

int sign_out(int device_id) {
    printf("[INFO]: Signing out\n");    

    cJSON *data = cJSON_CreateObject();
    cJSON *j_device_id = cJSON_CreateNumber(device_id);
    cJSON_AddItemToObject(data, "device_id", j_device_id);

    Response r;
    make_request("http://127.0.0.1:5000/device_sign_out", cJSON_Print(data), &r);    
    
    exit(0);
}

typedef struct {
    uint64_t start_seed;
    uint64_t end_seed;
} Work;

Work get_work(int device_id) {
    printf("[INFO]: Getting work\n");

    cJSON *data = cJSON_CreateObject();
    cJSON *j_device_id = cJSON_CreateNumber(device_id);
    cJSON_AddItemToObject(data, "device_id", j_device_id);
    
    Response r;
    make_request("http://127.0.0.1:5000/device_get_work", cJSON_Print(data), &r);    

    cJSON *json = cJSON_Parse(r.buffer);
    cJSON *json_end_seed = cJSON_GetObjectItemCaseSensitive(json, "end_seed");
    cJSON *json_start_seed = cJSON_GetObjectItemCaseSensitive(json, "start_seed");

    uint64_t start_seed = (uint64_t)strtoull(json_start_seed->valuestring, NULL, 10);
    uint64_t end_seed = (uint64_t)strtoull(json_end_seed->valuestring, NULL, 10);

    return (Work){
        .start_seed = start_seed,
        .end_seed = end_seed
    };
}

typedef struct {
    size_t size;
    uint64_t seeds[64];
} WorkResults;

WorkResults do_work(Work *work) {
    getStructureConfig(Ruined_Portal, VERSION, &rp_sconf);

    WorkResults results;
    results.size = 0;

    Generator g;
    setupGenerator(&g, VERSION, 0);

    int valid_seeds = 0;
    long total_seeds = 0;

    printf("[INFO]: Starting search\n");

    struct timeval start_time;
    struct timeval current_time;

    gettimeofday(&start_time, 0);

    for (uint64_t world_seed = work->start_seed; world_seed < work->end_seed; world_seed++) {
        if (check_seed(&g, world_seed)) {
            printf("\nFound a viable seed! %" PRIu64 "\n", world_seed);
            results.seeds[results.size] = world_seed;
            results.size++;
            valid_seeds++;
        }        
        if (total_seeds % 10000000 == 0) {
            gettimeofday(&current_time, 0);
            double time_taken = current_time.tv_sec + current_time.tv_usec / 1e6 - start_time.tv_sec - start_time.tv_usec / 1e6; // in seconds
            printf("\r[INFO]: Currently searching %f seeds/second", total_seeds / time_taken);
            fflush(stdout);
        }
        total_seeds++;
    }
    printf("[INFO]: Search complete!\n");
    printf("[INFO]: Valid seeds: %d\n", valid_seeds);
    printf("[INFO]: Total seeds: %ld\n", total_seeds);
    printf("[INFO]: Early exits: %d\n", early_exits);

    return results;
}

void submit_work(WorkResults *results, int device_id) {
    printf("[INFO]: Submitting Work\n");

    cJSON *data = cJSON_CreateObject();
    cJSON *result_array = cJSON_CreateArray();

    for (size_t i = 0; i < results->size; i++) {
        printf("seed: %" PRIu64 "\n", results->seeds[i]);
        cJSON *result = cJSON_CreateNumber(results->seeds[i]);
        cJSON_AddItemToArray(result_array, result);
    }
    cJSON_AddItemToObject(data, "results", result_array);

    cJSON *j_device_id = cJSON_CreateNumber(device_id);
    cJSON_AddItemToObject(data, "device_id", j_device_id);

    Response r;
    make_request("http://127.0.0.1:5000/device_submit_work", cJSON_Print(data), &r);    
}

int main(void) {
    naettInit(NULL);

    int device_id = sign_in("scriptlinestudios@protonmail.com", "MissyMolly@2021");
    // exit(0);

    for (int i = 0; i < 20; i++) {
        Work work = get_work(device_id);
        WorkResults results = do_work(&work);
        submit_work(&results, device_id);
    }

    sign_out(device_id);

    // sign_out(device_id);

    return 0;
}