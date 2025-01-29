#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <omp.h>

#include "cubiomes/finders.h"
#include "cubiomes/generator.h"
#include "cubiomes/rng.h"

#include "cJSON/cJSON.h"
#include "naett/naett.h"
#include "CThreads/cthreads.h"

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
    for (int rx = reg_min; rx <= reg_max; rx++) {
        for (int rz = reg_min; rz <= reg_max; rz++) {
            if (!getStructurePos(structure, VERSION, seed, rx, rz, &p)) {
                continue;
            }

            if (isViableStructurePos(structure, g, p.x, p.z, 0)) {
                // if (structure == Desert_Pyramid) {
                    // printf("\nDesert Pyramid: %d %d Seed: %" PRIu64 "\n", p.x, p.z, seed);
                // }
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

    StrongholdIter sh;
    Pos p = initFirstStronghold(&sh, VERSION, seed);
    if (p.x > RADIUS || p.x < -RADIUS || p.z > RADIUS || p.z < -RADIUS) {
        early_exits++;
        return false;
    }
    // this is a nice quick invalidation we can do:
    StructureConfig sconf;
    getStructureConfig(Outpost, VERSION, &sconf);

    int outpost_region = floor(RADIUS / 16.0 / sconf.regionSize);

    for (int rx = -outpost_region; rx <= outpost_region; rx++) {
        for (int rz = -outpost_region; rz <= outpost_region; rz++) {
            uint64_t s = seed;
            Pos p = getFeaturePos(sconf, seed, rx, rz);
            _setAttemptSeed(&s, (p.x) >> 4, (p.z) >> 4);
            // can_place[index] = nextInt(&seed, 5) == 0;
            // index++;
            bool possible_to_place = nextInt(&s, 5) == 0;
            if (possible_to_place) {
                goto complete;
            }
        }
    }
    early_exits++;
    return false;

complete:

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
    if (!radius_has_structure(g, Ancient_City, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Igloo, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Swamp_Hut, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Desert_Pyramid, seed)) {
        return false;
    }
    if (!radius_has_structure(g, Jungle_Pyramid, seed)) {
        return false;
    }

    int mask = 0b0000000;
    for (int cx = -64; cx < 64; cx+=8) {
        for (int cz= -64; cz < 64; cz+=8) {
            int biomeID = getBiomeAt(g, 16, cx, 256, cz);
            switch (biomeID) {
                case forest:
                    mask |= 0b1000000;
                    break;
                case cherry_grove:
                    mask |= 0b0100000;
                    break;
                case savanna:
                    mask |= 0b0010000;
                    break;
                case mangrove_swamp:
                    mask |= 0b0001000;
                    break;
                case jungle:
                    mask |= 0b0000100;
                    break;
                case taiga:
                    mask |= 0b0000010;
                    break;
                case pale_garden:
                    mask |= 0b0000001;
                    break;
                default:
                    break;
            }
        }
    }

    if (mask != 0b1111111) {
        return false;
    }

    return true;
    // return count >= 4;
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

int sign_in(const char *email, const char *password, int client_device_id) {
    printf("[INFO]: Signing in\n");    
    
    cJSON *signin_info = cJSON_CreateObject();
    cJSON *j_email = cJSON_CreateString(email);
    cJSON_AddItemToObject(signin_info, "email", j_email);
    cJSON *j_password = cJSON_CreateString(password);
    cJSON_AddItemToObject(signin_info, "password", j_password);
    cJSON *j_client_device_id = cJSON_CreateNumber(client_device_id);
    cJSON_AddItemToObject(signin_info, "client_device_id", j_client_device_id);

    Response r;
    make_request("http://127.0.0.1:5000/device_signin", cJSON_Print(signin_info), &r);    
    printf("%s\n", r.buffer);
    cJSON *json = cJSON_Parse(r.buffer);
    cJSON *json_message = cJSON_GetObjectItemCaseSensitive(json, "message");
    cJSON *json_device_id = cJSON_GetObjectItemCaseSensitive(json, "device_id");

    char *message = json_message->valuestring;
    int device_id = json_device_id->valueint;

    if (device_id < 0) {
        printf("[INFO]: Signin failed!\n");
        printf("%s\n", message);
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

int report_sps(int device_id, int sps) {
    cJSON *data = cJSON_CreateObject();
    cJSON *j_sps = cJSON_CreateNumber(sps);
    cJSON_AddItemToObject(data, "sps", j_sps);
    cJSON *j_device_id = cJSON_CreateNumber(device_id);
    cJSON_AddItemToObject(data, "device_id", j_device_id);

    Response r;
    make_request("http://127.0.0.1:5000/device_report_sps", cJSON_Print(data), &r); 
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

    if (r.code == 401) {
        printf("[INFO]: Client already has work!\n");  
        exit(1);
    }

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

WorkResults do_work(Work *work, int device_id) {
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
            // printf("\r[INFO]: Currently searching %f seeds/second", total_seeds / time_taken);
            // fflush(stdout);
        }
        if ((total_seeds + 1) % 100000000 == 0) {
            gettimeofday(&current_time, 0);
            double time_taken = current_time.tv_sec + current_time.tv_usec / 1e6 - start_time.tv_sec - start_time.tv_usec / 1e6; // in seconds
            double sps = total_seeds / time_taken;
            // report_sps(device_id, (int)sps);
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

int main(int argc, char **argv) {
    if (argc <= 1) {
        printf("USAGE: seedfinder <device num> <email> <password> <num threads>\n");
        exit(1);
    }

    naettInit(NULL);

    const char *email = argv[2];
    const char *password = argv[3];

    int device_id = sign_in(email, password, atoi(argv[1]));
    // printf("%d\n", device_id);
    // Work work = get_work(device_id);
    // printf("%" PRIu64 " %" PRIu64 "\n", work.start_seed, work.end_seed);
    // printf("%d\n", device_id);
    // sign_out(device_id);
    // exit(0);

    int num_threads = atoi(argv[4]);

    omp_set_num_threads(num_threads);
    for (int i = 0; ; i++) {
        Work work = get_work(device_id);
        printf("GOT WORK: %" PRIu64 " %" PRIu64 "\n", work.start_seed, work.end_seed);
        WorkResults thread_results[64];

        uint64_t seeds_per_thread = (work.end_seed - work.start_seed) / num_threads;

        clock_t start_time = clock();
        #pragma omp parallel 
        {
            uint64_t thread_start_seed = work.start_seed + (seeds_per_thread * omp_get_thread_num());
            uint64_t thread_end_seed = work.start_seed + (seeds_per_thread * omp_get_thread_num()) + seeds_per_thread;
            printf("%d %" PRIu64 " %" PRIu64 "\n", omp_get_thread_num(), thread_start_seed, thread_end_seed);
            Work thread_work = (Work){.start_seed=thread_start_seed, .end_seed=thread_end_seed};
            WorkResults thread_result = do_work(&thread_work, device_id);
            int id = omp_get_thread_num();
            thread_results[id] = thread_result;
        }
        clock_t end_time = clock();
        double seconds = ((double)(end_time - start_time) / CLOCKS_PER_SEC) / num_threads;
        printf("Time Taken: %f\n", seconds); 
        double sps = (work.end_seed - work.start_seed) / seconds;
        printf("SPS: %f\n", sps);
        // exit(1);
        report_sps(device_id, (int)sps);

        WorkResults final_results;
        final_results.size = 0;
        for (int i = 0; i < num_threads; i++) {
            WorkResults tr = thread_results[i];
            for (int j = 0; j < tr.size; j++) {
                final_results.seeds[final_results.size] = tr.seeds[j];
                final_results.size++;
            }
        }

        submit_work(&final_results, device_id);
    }

    sign_out(device_id);

    // sign_out(device_id);

    return 0;
}
