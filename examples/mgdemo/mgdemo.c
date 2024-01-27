#include <libdragon.h>

#include "matrix.h"
#include "quat.h"
#include "utility.h"
#include "vertex.h"
#include "cube_mesh.h"
#include "scene_data.h"

#define FB_COUNT    3

typedef struct
{
    mgfx_modes_t modes;
    mgfx_fog_t fog;
    mgfx_lighting_t lighting;
} scene_raw_data;

typedef struct
{
    mg_resource_set_t *resource_set;
    sprite_t *texture;
    rdpq_texparms_t rdpq_tex_parms;
} material_data;

typedef struct
{
    mg_buffer_t *vertex_buffer;
    mg_buffer_t *index_buffer;
    uint32_t index_count;
} mesh_data;

typedef struct
{
    mat4x4_t mvp_matrix;
    mat4x4_t mv_matrix;
    mat4x4_t n_matrix;
    quat_t rotation;
    float position[3];
    uint32_t material_id;
    uint32_t mesh_id;
} object_data;

void init();
void render();
void create_scene_resources();
void material_create(material_data *mat, mgfx_material_parms_t *mat_parms, sprite_t *texture);
void mesh_create(mesh_data *mesh, const vertex *vertices, uint32_t vertex_count, const uint16_t *indices, uint32_t index_count);
void update_object_matrices(object_data *object);

static surface_t zbuffer;

static mg_shader_t *vertex_shader;
static mg_pipeline_t *pipeline;
static mg_vertex_loader_t *vertex_loader;
static mg_buffer_t *scene_resource_buffer;
static mg_resource_set_t *scene_resource_set;

static sprite_t *textures[TEXTURE_COUNT];
static material_data materials[MATERIAL_COUNT];
static mesh_data meshes[MESH_COUNT];
static object_data objects[OBJECT_COUNT];

static mat4x4_t projection_matrix;
static mat4x4_t view_matrix;
static mat4x4_t vp_matrix;
static float camera_position[3];
static quat_t camera_rotation;

int main()
{
    init();

    while (true) {
        render();
    }
}

void init()
{
    const resolution_t resolution = RESOLUTION_320x240;

    // Initialize the required subsystems
	debug_init(DEBUG_FEATURE_LOG_ISVIEWER | DEBUG_FEATURE_LOG_USB);
    dfs_init(DFS_DEFAULT_LOCATION);
    display_init(resolution, DEPTH_16_BPP, FB_COUNT, GAMMA_NONE, FILTERS_RESAMPLE_ANTIALIAS_DEDITHER);
    rdpq_init();
    mg_init();

    // Create depth buffer
    zbuffer = surface_alloc(FMT_RGBA16, resolution.width, resolution.height);

    // Create the vertex shader. This function returns the vertex shader of the fixed function pipeline provided by libdragon.
    vertex_shader = mgfx_create_vertex_shader();

    // Create the graphics pipeline. We need to attach the vertex shader and setup some initial state.
    // Culling flags and viewport can be changed later (which we won't be doing in this demo). See mg_set_culling and mg_set_viewport.
    pipeline = mg_pipeline_create(&(mg_pipeline_parms_t){
        .vertex_shader = vertex_shader,
        .culling.cull_flags = MG_CULL_FLAGS_BACK,
        .viewport = (mg_viewport_t) {
            .width = resolution.width,
            .height = resolution.height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        }
    });

    // Create the vertex loader. We need to create one of these per vertex layout we intend on using.
    // After creation, a vertex loader contains a little piece of ucode that can load vertices of the specified layout optimally.
    vertex_loader = mg_vertex_loader_create(&(mg_vertex_loader_parms_t) {
        .attribute_descriptor_count = ARRAY_SIZE(vertex_attributes),
        .attribute_descriptors = vertex_attributes,
        .stride = sizeof(vertex)
    });

    create_scene_resources();

    // Load textures
    for (size_t i = 0; i < TEXTURE_COUNT; i++)
    {
        textures[i] = sprite_load(texture_files[i]);
    }

    // Create materials
    for (size_t i = 0; i < MATERIAL_COUNT; i++)
    {
        material_create(&materials[i], &(mgfx_material_parms_t) {
            .diffuse_color = color_from_packed32(material_diffuse_colors[i]),
        }, textures[material_texture_indices[i]]);
    }

    // Create meshes
    for (size_t i = 0; i < MESH_COUNT; i++)
    {
        mesh_create(&meshes[i], mesh_vertices[i], mesh_vertex_counts[i], mesh_indices[i], mesh_index_counts[i]);
    }

    // Initialize objects
    for (size_t i = 0; i < OBJECT_COUNT; i++)
    {
        objects[i] = (object_data) {
            .material_id = object_material_ids[i],
            .mesh_id = object_mesh_ids[i],
        };
        memcpy(objects[i].position, object_positions[i], sizeof(objects[i].position));
        quat_make_identity(&objects[i].rotation);
    }

    // Initialize camera properties
    mat4x4_make_projection(&projection_matrix, camera_fov, (float)resolution.width / (float)resolution.height, camera_near_plane, camera_far_plane);
    memcpy(camera_position, camera_starting_position, sizeof(camera_position));
    quat_make_identity(&camera_rotation);
}

void create_scene_resources()
{
    // These are resources that are expected to stay constant during the entire scene.
    // These will be provided to the shader by writing the data to a uniform buffer, and attaching that buffer
    // to a resource set. By using a resource set, uploading the data to DMEM during rendering will be 
    // automatically optimized for us. Using uniform buffers also allows us to modify the buffer contents dynamically,
    // for example to update lighting (which we aren't doing for now in this demo).

    // 1. Create the uniform buffer. It's important that the buffer contents need to be in a format that is optimized for
    //    access by the RSP. This is what the scene_raw_data struct is, which itself contains the predefined structs
    //    provided by the fixed function pipeline.
    scene_resource_buffer = mg_buffer_create(&(mg_buffer_parms_t) {
        .size = sizeof(scene_raw_data),
        .flags = MG_BUFFER_FLAGS_USAGE_UNIFORM
    });

    // Lighting parameters
    mgfx_light_t lights[LIGHT_COUNT];
    for (size_t i = 0; i < LIGHT_COUNT; i++)
    {
        lights[i].diffuse_color = color_from_packed32(light_colors[i]);
        lights[i].radius = light_radii[i];
        memcpy(lights[i].position, light_positions[i], sizeof(lights[i].position));
    }

    // 2. Map the buffer for writing access and write the uniform data into it. It's important to always unmap the buffer once done.
    scene_raw_data *raw_data = mg_buffer_map(scene_resource_buffer, 0, sizeof(raw_data), MG_BUFFER_MAP_FLAGS_WRITE);
        // These mgfx_get_* functions will take the parameters in a convenient format and convert them into the RSP-optimized format that the buffer is supposed to contain.
        mgfx_get_modes(&raw_data->modes, &(mgfx_modes_parms_t) {
            .flags = MGFX_MODES_FLAGS_LIGHTING_ENABLED | MGFX_MODES_FLAGS_NORMALIZATION_ENABLED | MGFX_MODES_FLAGS_TEXTURING_ENABLED | MGFX_MODES_FLAGS_ZBUFFER_ENABLED
        });
        mgfx_get_fog(&raw_data->fog, &(mgfx_fog_parms_t) {0});
        mgfx_get_lighting(&raw_data->lighting, &(mgfx_lighting_parms_t) {
            .ambient_color = color_from_packed32(ambient_light_color),
            .light_count = ARRAY_SIZE(lights),
            .lights = lights
        });
    mg_buffer_unmap(scene_resource_buffer);

    // 3. Create the resource set. A resource set contains a number of resource bindings.
    //    Each resource binding describes the type of resource, where to copy it from, and which binding location to copy it to.
    //    Binding locations are defined by the vertex shader. Therefore we use predefined binding locations that are provided by
    //    the fixed function pipeline.
    mg_resource_binding_t scene_bindings[] = {
        {
            .binding = MGFX_BINDING_MODES,
            .type = MG_RESOURCE_TYPE_UNIFORM_BUFFER,
            .buffer = scene_resource_buffer,
            .offset = offsetof(scene_raw_data, modes)
        },
        {
            .binding = MGFX_BINDING_FOG,
            .type = MG_RESOURCE_TYPE_UNIFORM_BUFFER,
            .buffer = scene_resource_buffer,
            .offset = offsetof(scene_raw_data, fog)
        },
        {
            .binding = MGFX_BINDING_LIGHTING,
            .type = MG_RESOURCE_TYPE_UNIFORM_BUFFER,
            .buffer = scene_resource_buffer,
            .offset = offsetof(scene_raw_data, lighting)
        },
    };

    // By bundling multiple resource bindings in a resource set, magma can automatically optimize the operation
    // (for example by detecting that some bindings can be coalesced into a contiguous DMA). During rendering, 
    // the set can be "bound" with a single function call, which will load all attached resources into DMEM at once.
    scene_resource_set = mg_resource_set_create(&(mg_resource_set_parms_t) {
        .pipeline = pipeline,
        .binding_count = ARRAY_SIZE(scene_bindings),
        .bindings = scene_bindings
    });
}

void material_create(material_data *material, mgfx_material_parms_t *mat_parms, sprite_t *texture)
{
    // Similarly to the scene resources, we will provide materials to the shader via resource sets.
    // We separate the material from scene resources, because they are expected to change during the scene.
    // Not all objects will have the same material after all. To show off all features of magma in this demo,
    // we will make the assumption that the materials themselves will stay constant. Therefore we won't store
    // this data inside buffers, but attach it to the resource set directly via so called "inline uniforms".

    // 1. Initialize the raw material data by using the functions provided by the fixed function pipeline.
    //    Just as with uniform buffers, the shader expects the data in a format that is optimized for the RSP.
    mgfx_material_t mat;
    mgfx_texturing_t tex;
    mgfx_get_material(&mat, mat_parms);
    mgfx_get_texturing(&tex, &(mgfx_texturing_parms_t) {
        .scale[0] = texture->width,
        .scale[1] = texture->height,
    });

    // 2. Create the resource set. This time, we use the resource type "inline uniform" and set
    //    the "inline_data" pointer to pass in the data we initialized above.
    mg_resource_binding_t bindings[] = {
        {
            .binding = MGFX_BINDING_MATERIAL,
            .type = MG_RESOURCE_TYPE_INLINE_UNIFORM,
            .inline_data = &mat
        },
        {
            .binding = MGFX_BINDING_TEXTURING,
            .type = MG_RESOURCE_TYPE_INLINE_UNIFORM,
            .inline_data = &tex
        },
    };

    // When this call returns, the "inline_data" has been consumed and a copy embedded inside the resource set itself.
    // This effectively does the same as a uniform buffer, but there is one less object to manage. The drawback is that 
    // once the resource set has been created, there is no way to modify the embedded data. But due to the assumption we made,
    // that won't be required anyway.
    material->resource_set = mg_resource_set_create(&(mg_resource_set_parms_t) {
        .pipeline = pipeline,
        .binding_count = ARRAY_SIZE(bindings),
        .bindings = bindings,
    });

    // Additionally prepare texture data for rdpq, but this is completely unrelated to magma.
    material->texture = texture;
    material->rdpq_tex_parms = (rdpq_texparms_t) {
        .s.repeats = REPEAT_INFINITE,
        .t.repeats = REPEAT_INFINITE,
    };
}

void mesh_create(mesh_data *mesh, const vertex *vertices, uint32_t vertex_count, const uint16_t *indices, uint32_t index_count)
{
    // Preparing mesh data is relatively straightforward. Simply load vertex and index data into buffers.
    // By setting "initial_data", the buffer will already contain this data after creation, so we don't 
    // need to map it and manually copy the data in.
    // Additionally, if the "MG_BUFFER_FLAGS_LAZY_ALLOC" flag is set, magma will use the pointer in "initial_data"
    // as the buffer's actual backing memory for as long as possible, which avoids an extra memory allocation.
    // Extra care must be taken when using this flag, as the passed pointer will be accessed by magma internally, so
    // it must stay valid for as long as the buffer is used, and the user is responsible of handling data cache.
    // However, the flag doesn't guarantee that no extra memory will ever be allocated. When and if it happens is up to the implementation.

    mesh->vertex_buffer = mg_buffer_create(&(mg_buffer_parms_t) {
        .size = sizeof(vertex) * vertex_count,
        .initial_data = vertices,
        .flags = MG_BUFFER_FLAGS_USAGE_VERTEX | MG_BUFFER_FLAGS_LAZY_ALLOC
    });

    mesh->index_buffer = mg_buffer_create(&(mg_buffer_parms_t) {
        .size = sizeof(uint16_t) * index_count,
        .initial_data = indices,
        .flags = MG_BUFFER_FLAGS_USAGE_INDEX | MG_BUFFER_FLAGS_LAZY_ALLOC
    });

    mesh->index_count = index_count;
}

void render()
{
    // Update camera matrices. Note that the camera position and rotation need to be inverted for this to work.
    mat4x4_make_translation_rotation(&view_matrix, camera_position, camera_rotation.v);
    mat4x4_mult(&vp_matrix, &projection_matrix, &view_matrix);

    // Get framebuffer
    surface_t *disp = display_get();
    rdpq_attach_clear(disp, &zbuffer);

    // Set up render modes with rdpq. This could be set per material, but for simplicity's sake we use the same render mode for all objects in this demo.
    rdpq_mode_begin();
        rdpq_set_mode_standard();
        rdpq_mode_zbuf(true, true);
        rdpq_mode_antialias(AA_STANDARD);
        rdpq_mode_persp(true);
        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
    rdpq_mode_end();

    // All our materials use the same pipeline in this demo, so bind it once for the entire scene
    mg_bind_pipeline(pipeline);

    // Because all our meshes have the same vertex format in this demo, we can just bind the vertex loader once for the entire scene
    mg_bind_vertex_loader(vertex_loader);

    // Bind resources that stay constant for the entire scene (for example lighting)
    mg_bind_resource_set(scene_resource_set);

    uint32_t current_material_id = -1;
    uint32_t current_mesh_id = -1;

    material_data *current_material = NULL;
    mesh_data *current_mesh = NULL;
    object_data *object = NULL;

    // Iterate over all objects
    for (size_t i = 0; i < OBJECT_COUNT; i++)
    {
        // Recalculate object matrices.
        object = &objects[i];
        update_object_matrices(object);

        // Swap out the current material resources. This will automatically upload all uniform data to DMEM that is bound to the set.
        // To avoid redundant uploads, we keep track of the current material and only make this call when it actually changes.
        // This will be most optimal if the list of objects has been sorted by material.
        if (object->material_id != current_material_id) {
            current_material_id = object->material_id;
            current_material = &materials[current_material_id];
            mg_bind_resource_set(current_material->resource_set);

            // Additionally, upload the material's texture via rdpq, which can be done completely independently from magma.
            if (current_material->texture) {
                rdpq_sprite_upload(TILE0, current_material->texture, &current_material->rdpq_tex_parms);
            }
        }

        // Swap out the currently bound vertex/index buffers. Similar to materials, we only do this when it changes.
        // Since swapping buffers is very quick, objects should be sorted by material first, and then by mesh.
        if (object->mesh_id != current_mesh_id) {
            current_mesh_id = object->mesh_id;
            current_mesh = &meshes[current_mesh_id];
            mg_bind_vertex_buffer(current_mesh->vertex_buffer, 0);
            mg_bind_index_buffer(current_mesh->index_buffer, 0);
        }

        // Because the matrices are expected to change every frame and for every object, we upload them "inline", which embeds their data within the command stream itself.
        // After the call returns, the matrix data has been consumed entirely and we won't need to worry about keeping it in memory.
        // If we were to use resource sets for matrices as well, we would have to manually synchronize updating them on the CPU.
        // TODO: an example how to do manual synchronization
        // This function uses "mg_push_constants" internally with a predefined offset and size, and automatically converts the data to a RSP-native format.
        mgfx_set_matrices_inline(&(mgfx_matrices_parms_t) {
            .model_view_projection = object->mvp_matrix.m[0],
            .model_view = object->mv_matrix.m[0],
            .normal = object->n_matrix.m[0],
        });

        assert(current_mesh != NULL);

        // Perform the draw call. This will assemble the triangles from the currently bound vertex/index buffers, process them with the
        // currently bound pipeline (using the attached vertex shader), applying all currently bound resources such as matrices, lighting and material parameters etc.
        mg_draw_indexed(&(mg_input_assembly_parms_t) {
            .primitive_topology = MG_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
        }, current_mesh->index_count, 0, 0);
    }

    // Done. Detach from the framebuffer and present it.
    rdpq_detach_show();
}

void update_object_matrices(object_data *object)
{
    // TODO: Do (parts of) this on RSP instead
    mat4x4_t model_matrix;
    mat4x4_make_rotation_translation(&model_matrix, object->position, object->rotation.v);
    mat4x4_mult(&object->mvp_matrix, &vp_matrix, &model_matrix);
    mat4x4_mult(&object->mv_matrix, &view_matrix, &model_matrix);
    mat4x4_transpose_inverse(&object->n_matrix, &object->mv_matrix);
}