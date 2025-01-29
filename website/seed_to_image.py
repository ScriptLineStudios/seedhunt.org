import pybiomes

from pybiomes.versions import MC_1_21_WD, MC_1_17_1
from pybiomes.dimensions import DIM_OVERWORLD
from pybiomes.biomes import *
from pybiomes.structures import *

import pygame

import math

colors = {
    ocean:                            0x000070,
    plains:                           0x8db360,
    desert:                           0xfa9418,
    windswept_hills:                  0x606060,
    forest:                           0x056621,
    taiga:                            0x0b6a5f,
    swamp:                            0x07f9b2,
    river:                            0x0000ff,
    nether_wastes:                    0x572526,
    the_end:                          0x8080ff,
    frozen_ocean:                     0x7070d6,
    frozen_river:                     0xa0a0ff,
    snowy_plains:                     0xffffff,
    snowy_mountains:                  0xa0a0a0,
    mushroom_fields:                  0xff00ff,
    mushroom_field_shore:             0xa000ff,
    beach:                            0xfade55,
    desert_hills:                     0xd25f12,
    wooded_hills:                     0x22551c,
    taiga_hills:                      0x163933,
    mountain_edge:                    0x72789a,
    jungle:                           0x507b0a, 
    jungle_hills:                     0x2c4205,
    sparse_jungle:                    0x60930f, 
    deep_ocean:                       0x000030,
    stony_shore:                      0xa2a284,
    snowy_beach:                      0xfaf0c0,
    birch_forest:                     0x307444,
    birch_forest_hills:               0x1f5f32,
    dark_forest:                      0x40511a,
    snowy_taiga:                      0x31554a,
    snowy_taiga_hills:                0x243f36,
    old_growth_pine_taiga:            0x596651,
    giant_tree_taiga_hills:           0x454f3e,
    windswept_forest:                 0x5b7352, 
    savanna:                          0xbdb25f,
    savanna_plateau:                  0xa79d64,
    badlands:                         0xd94515,
    wooded_badlands:                  0xb09765,
    badlands_plateau:                 0xca8c65,
    small_end_islands:                0x4b4bab, 
    end_midlands:                     0xc9c959, 
    end_highlands:                    0xb5b536,
    end_barrens:                      0x7070cc, 
    warm_ocean:                       0x0000ac,
    lukewarm_ocean:                   0x000090,
    cold_ocean:                       0x202070,
    deep_warm_ocean:                  0x000050,
    deep_lukewarm_ocean:              0x000040,
    deep_cold_ocean:                  0x202038,
    deep_frozen_ocean:                0x404090,
    seasonal_forest:                  0x2f560f,
    rainforest:                       0x47840e, 
    shrubland:                        0x789e31, 
    the_void:                         0x000000,
    sunflower_plains:                 0xb5db88,
    desert_lakes:                     0xffbc40,
    windswept_gravelly_hills:         0x888888,
    flower_forest:                    0x2d8e49,
    taiga_mountains:                  0x339287, 
    swamp_hills:                      0x2fffda,
    ice_spikes:                       0xb4dcdc,
    modified_jungle:                  0x78a332,
    modified_jungle_edge:             0x88bb37, 
    old_growth_birch_forest:          0x589c6c,
    tall_birch_hills:                 0x47875a,
    dark_forest_hills:                0x687942,
    snowy_taiga_mountains:            0x597d72,
    old_growth_spruce_taiga:          0x818e79,
    giant_spruce_taiga_hills:         0x6d7766,
    modified_gravelly_mountains:      0x839b7a, 
    windswept_savanna:                0xe5da87,
    shattered_savanna_plateau:        0xcfc58c,
    eroded_badlands:                  0xff6d3d,
    modified_wooded_badlands_plateau: 0xd8bf8d,
    modified_badlands_plateau:        0xf2b48d,
    bamboo_jungle:                    0x849500, 
    bamboo_jungle_hills:              0x5c6c04, 
    soul_sand_valley:                 0x4d3a2e, 
    crimson_forest:                   0x981a11, 
    warped_forest:                    0x49907b,
    basalt_deltas:                    0x645f63, 
    dripstone_caves:                  0x4e3012, 
    lush_caves:                       0x283c00, 
    meadow:                           0x60a445, 
    grove:                            0x47726c, 
    snowy_slopes:                     0xc4c4c4, 
    jagged_peaks:                     0xdcdcc8, 
    frozen_peaks:                     0xb0b3ce, 
    stony_peaks:                      0x7b8f74, 
    deep_dark:                        0x031f29,
    mangrove_swamp:                   0x2ccc8e,
    cherry_grove:                     0xff91c8, 
    pale_garden:                      0x696d95,
}

RADIUS = 1024

def count_structures(seed, structure, generator):
    finder = pybiomes.Finder(MC_1_21_WD)

    sconfig = finder.get_structure_config(structure)
    reg_max = math.floor(RADIUS / 16.0 / sconfig["regionSize"])
    reg_min = -reg_max

    count = 0
    positions = []
    for rx in range(reg_min, reg_max+1):
        for rz in range(reg_min, reg_max+1):
            pos = finder.get_structure_pos(structure, seed, rx, rz)
            if not pos:
                continue
            
            if generator.is_viable_structure_pos(structure, pos.x, pos.z, 0):
                count += 1
                positions.append([pos.x, pos.z])

    return count, positions

def get_data_for_seed(seed):
    structures = [Mansion, Village, Trial_Chambers, Outpost, Ancient_City, Igloo, Swamp_Hut, Desert_Pyramid, Jungle_Pyramid]
    
    structure_names = {
        Mansion: "Mansion", Village: "Village", Trial_Chambers: "Trial Chambers", Outpost: "Outpost", Ancient_City: "Ancient City", Igloo: "Igloo", Swamp_Hut: "Swamp Hut", Desert_Pyramid: "Desert Pyramid", Jungle_Pyramid: "Jungle Pyramid"
    }

    structure_counts = {}
    structure_positions = {}

    generator = pybiomes.Generator(MC_1_21_WD, 0)
    generator.apply_seed(seed, DIM_OVERWORLD)

    r = pybiomes.Range(scale=1, x=-1024, y=256, z=-1024, sx=2048, sy=0, sz=2048)
    biomes = generator.gen_biomes(r)
    biomes = set(biomes)
    biomes = [generator.biome_to_string(b) for b in biomes]

    for structure in structures:
        structure_counts[structure_names[structure]], structure_positions[structure_names[structure]] = count_structures(seed, structure, generator)
    
    return {
        "structure_counts": structure_counts,
        "structure_positions": structure_positions,
        "biomes": biomes,
    }

def get_image_for_seed(seed):
    generator = pybiomes.Generator(MC_1_21_WD, 0)
    generator.apply_seed(seed, DIM_OVERWORLD)

    r = pybiomes.Range(scale=1, x=-1024, y=256, z=-1024, sx=2048, sy=0, sz=2048)
    biomes = generator.gen_biomes(r)

    surface = pygame.Surface((2048, 2048))

    for x in range(2048):
        for z in range(2048):
            bid = biomes[0 * r.sx * r.sz + z * r.sx + x]
            surface.set_at((x, z), colors[bid])

    filename = f"static/seeds/{seed}.png"
    pygame.image.save(surface, filename)
    return f"/{filename}"

def get_seed_data(seed):
    data = get_data_for_seed(seed)
    return {
        "image": get_image_for_seed(seed),
        "structure_counts": data.get("structure_counts"), 
        "structure_positions": data.get("structure_positions"),
        "biomes": data.get("biomes")
    }