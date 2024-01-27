#ifndef SCENE_DATA_H
#define SCENE_DATA_H

#include "utility.h"
#include "cube_mesh.h"

/*
    A minimal "asset database" for this demo.
    The only purpose is to remove some clutter from the main file.
    Using this system for a larger application is probably not recommended.
*/

#define TEXTURE_COUNT   3
#define MATERIAL_COUNT  5
#define MESH_COUNT      1
#define OBJECT_COUNT    10
#define LIGHT_COUNT     2


/* Textures */
static const char *texture_files[] = {
    "rom:/texture0.ci4.sprite",
    "rom:/texture1.ci4.sprite",
    "rom:/texture2.ci4.sprite",
};


/* Materials */
static const uint32_t material_texture_indices[] = {
    0, 0, 0, 1, 2
};
static const uint32_t material_diffuse_colors[] = {
    0xffffffff,
    0x5a81e6ff,
    0x3b5c34ff,
    0xffffffff,
    0xffffffff,
};


/* Meshes */
static const vertex *mesh_vertices[] = {
    cube_vertices
};
static const uint32_t mesh_vertex_counts[] = {
    ARRAY_SIZE(cube_vertices)
};
static const uint16_t *mesh_indices[] = {
    cube_indices
};
static const uint32_t mesh_index_counts[] = {
    ARRAY_SIZE(cube_indices)
};


/* Objects */
static const uint32_t object_material_ids[] = {
    0, 0, 1, 2, 2, 2, 3, 3, 4, 4
};
static const uint32_t object_mesh_ids[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};
static const float object_positions[][3] = {
    { 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 2.0f },
    { 0.0f, 0.0f, 4.0f },
    { 2.0f, 0.0f, 5.0f },
    { 4.0f, 1.0f, 0.0f },
    { 4.0f, 0.0f, 4.0f },
    { -3.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, -5.0f },
    { -2.0f, 0.0f, 2.0f },
    { -4.0f, 0.0f, -3.0f },
};


/* Lights */
static const uint32_t light_colors[] = {
    0xffffffff,
    0xdbbc72ff
};
static const float light_positions[][4] = {
    { 0.507093f, -0.845154f, -0.169031f, 0.0f },
    { 0.0f, 3.0f, 0.0f, 1.0f }
};
static const float light_radii[] = {
    0.0f,
    30.0f
};
static const uint32_t ambient_light_color = 0x101010ff;

/* Camera */
static const float camera_fov = 65.0f;
static const float camera_near_plane = 1.0f;
static const float camera_far_plane = 100.0f;
static const float camera_starting_position[3] = { 0.0f, 0.0f, 0.0f };

#endif