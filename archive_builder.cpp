#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENGL 1

#ifdef DEBUG
    #undef DEBUG
#endif

#define DEBUG 0

#include "../cOMS/system/FileUtils.cpp"
#include "../cOMS/ui/UITheme.cpp"
#include "../cOMS/utils/StringUtils.h"
#include "../cOMS/asset/AssetArchive.cpp"
#include "../cOMS/audio/Audio.cpp"
#include "../cOMS/audio/Qoa.h"
#include "../cOMS/image/Image.cpp"
#include "../cOMS/image/Qoi.h"
#include "../cOMS/object/Mesh.cpp"
#include "../cOMS/object/TextureAtlas.cpp"
#include "../cOMS/localization/Language.cpp"
#include "../cOMS/gpuapi/opengl/ShaderUtils.h"
#include "../cOMS/gpuapi/direct3d/ShaderUtils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../cOMS/image/stb_image.h"

void create_base_path(const char* path, char* rel_path)
{
    if (*path == '.') {
        relative_to_absolute(path, rel_path);
    } else {
        strcpy(rel_path, path);
    }

    char* dir = strrchr(rel_path, '/');
    if (!dir) {
        dir = strrchr(rel_path, '\\');
    }

    if (dir) {
        *dir = '\0';
    }
}

void enum_name_from_path(const char* path, char* enum_name) {
    // Extract the last directory and file name
    char* last_dir = strrchr(path, '/');
    if (!last_dir) {
        last_dir = strrchr(path, '\\');
    }

    if (!last_dir) {
        // Skip if no '/' is found
        return;
    }

    char* file_name = last_dir + 1;

    // Remove file extension
    char* dot = strrchr(file_name, '.');
    if (dot != NULL) {
        *dot = '_';
    }

    // If the file contains multiple . as file extension we ignore the last one
    // The reason for this is that we sometimes need platform/api specific file names but in our enum we need the same name between all enums (e.g. compiled vulkan shaders)
    char* dot2 = strrchr(file_name, '.');
    if (dot2 != NULL && last_dir < dot2) {
        *dot2 = '_';
        *dot = '\0';
    }

    // Transform to enum format
    int32 j = 0;
    for (int32 c = 0; file_name[c] != '\0'; ++c) {
        if (isalnum(file_name[c])) {
            enum_name[j++] = (char) toupper(file_name[c]);
        } else {
            enum_name[j++] = '_';
        }
    }
    enum_name[j] = '\0';
};

void build_enum(RingMemory* memory_volatile, const char* enum_char, int32 asset_count, char* output_path, int32 toc_id)
{
    char* last_dir = strrchr(output_path, '/');
    if (!last_dir) {
        last_dir = strrchr(output_path, '\\');
    }

    ++last_dir;

    char output_name[64];
    char namespace_name[64];

    strcpy(output_name, last_dir);
    output_name[0] = (char) toupper(*output_name);

    last_dir = output_name;
    while(*last_dir) {
        if (*last_dir == '.') {
            *last_dir = '\0';
            break;
        }

        ++last_dir;
    }

    strcpy(namespace_name, output_name);
    str_toupper(namespace_name);

    byte* enum_file_data = ring_memory_get(memory_volatile, 1 * MEGABYTE, sizeof(size_t));

    sprintf((char *) enum_file_data,
        "/**\n"
        " * Jingga\n"
        " *\n"
        " * @copyright Jingga\n"
        " * @license    License 2.0\n"
        " * @version   1.0.0\n"
        " * @link      https://jingga.app\n"
        " */\n"
        "#ifndef TOS_ASSET_ARCHIVE_IDS_%s\n"
        "#define TOS_ASSET_ARCHIVE_IDS_%s\n"
        "\n"
        "enum AssetArchiveIds%s {\n",
        namespace_name,
        namespace_name,
        output_name
    );

    for (int32 i = 0; i < asset_count; ++i) {
        // Add the entry to the enum file content
        sprintf(
            (char *) enum_file_data + strlen((char *) enum_file_data),
            "    AA_ID_%s = %d | (%d << 24),\n",
            &enum_char[64 * i], i, toc_id
        );
    }

    sprintf((char *) enum_file_data + strlen((char *) enum_file_data),
        "};\n\n"
        "#endif\n"
    );

    FileBody enum_file = {0};
    enum_file.content = enum_file_data;
    enum_file.size = strlen((char *) enum_file_data);

    char* dot = strrchr(output_path, '.');
    if (dot != NULL) {
        ++dot;
        *dot++ = 'h';
        *dot = '\0';
    }

    file_write(output_path, &enum_file);
}

build_asset(
    RingMemory* memory_volatile,
    const char* rel_path,
    const char* input_path,
    const char** archive_body, const char* body_start,
    char* enum_char,
    AssetArchiveHeader* header,
    int32 archive_id
) {
    if (!file_exists(input_path)) {
        printf("Couldn't find %s\n", input_path);
        return;
    }

    enum_name_from_path(input_path, &enum_char[64 * header->asset_count]);

    // Get the file extension of the asset (we handle file types differently)
    const char* extension = (input_path + 1);
    while (*extension != '.') {
        --extension;
    }

    byte* element_start = *archive_body;

    AssetArchiveElement* element = &header->asset_element[header->asset_count];
    element->type = ASSET_TYPE_GENERAL;
    element->start = (uint32) (archive_body - body_start);

    ++header->asset_count;

    if (strncmp(extension, ".wav", sizeof("wav") - 1) == 0) {
        element->type = ASSET_TYPE_AUDIO;

        // Read file
        Audio audio;
        audio.data = ring_memory_get(memory_volatile, file_size(input_path) + sizeof(Audio), 4);
        audio_from_file(&audio, input_path, memory_volatile);

        // Create output data
        element->uncompressed = audio_data_size(&audio);

        *archive_body += audio_header_to_data(&audio, *archive_body);
        *archive_body += qoa_encode(&audio, *archive_body);

        element->length = *archive_body - element_start;
    } else if (strncmp(extension, ".objtxt", sizeof("objtxt") - 1) == 0) {
        element->type = ASSET_TYPE_OBJ;

        // Read file
        Mesh mesh;
        mesh.data = ring_memory_get(memory_volatile, file_size(input_path) * 2 + sizeof(Mesh), 4);
        mesh_from_file_txt(&mesh, input_path, memory_volatile);

        // Create output data
        element->uncompressed = mesh_to_data(&mesh, *archive_body);
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;
    } else if (strncmp(extension, ".langtxt", sizeof("langtxt") - 1) == 0) {
        element->type = ASSET_TYPE_LANGUAGE;

        // Read file
        Language lang;
        lang.data = ring_memory_get(memory_volatile, file_size(input_path) + 1024, 4);
        language_from_file_txt(&lang, input_path, memory_volatile);

        // Create output data
        element->uncompressed = language_to_data(&lang, *archive_body);
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;
    } else if (strncmp(extension, ".atlastxt", sizeof("atlastxt") - 1) == 0) {
        element->type = ASSET_TYPE_TEXTURE_ATLAS;

        // Read file
        TextureAtlas atlas = {0};
        atlas_from_file_txt(&atlas, input_path, memory_volatile);

        // Create output data
        element->uncompressed = atlas_to_data(&atlas, *archive_body);
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;

        bool is_external = str_is_num(atlas.texture_name);
        int32 texture_id;

        if (is_external) {
            texture_id = str_to_int(atlas.texture_name);
        } else {
            wchar_t dependency_input_path[MAX_PATH];
            int32 i = 0;

            if (atlas.texture_name[0] != '.') {
                memcpy(dependency_input_path, L"./", sizeof(L"./"));
                i = 2;
            }

            for (const char* pos = atlas.texture_name; *pos; ++pos) {
                dependency_input_path[i++] = (wchar_t) *pos;
            }

            texture_id = (header->asset_count) | (archive_id << 24);
            build_asset(
                memory_volatile,
                rel_path,
                dependency_input_path,
                archive_body, body_start,
                enum_char,
                header,
                archive_id
            );
        }
        element->dependency_start = header->asset_dependency_count;
        header->asset_dependencies[element->dependency_count++] = texture_id;
        ++header->asset_dependency_count;
    } else if (strncmp(extension, ".fonttxt", sizeof("fonttxt") - 1) == 0) {
        element->type = ASSET_TYPE_FONT;

        // Read file
        Font font = {0};
        font.glyphs = (Glyph *) ring_memory_get(memory_volatile, file_size(input_path) + sizeof(Glyph), 4);
        font_from_file_txt(&font, input_path, memory_volatile);

        // Create output data
        element->uncompressed = font_to_data(&font, *archive_body);
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;

        bool is_external = str_is_num(font.texture_name);
        int32 texture_id;

        if (is_external) {
            texture_id = str_to_int(font.texture_name);
        } else {
            wchar_t dependency_input_path[MAX_PATH];
            int32 i = 0;

            if (font.texture_name[0] != '.') {
                memcpy(dependency_input_path, L"./", sizeof(L"./"));
                i = 2;
            }

            for (const char* pos = font.texture_name; *pos; ++pos) {
                dependency_input_path[i++] = (wchar_t) *pos;
            }

            texture_id = (header->asset_count) | (archive_id << 24);
            build_asset(
                memory_volatile,
                rel_path,
                dependency_input_path,
                archive_body, body_start,
                enum_char,
                header,
                archive_id
            );
        }
        element->dependency_start = header->asset_dependency_count;
        header->asset_dependencies[element->dependency_count++] = texture_id;
        ++header->asset_dependency_count;
    } else if (strncmp(extension, ".themetxt", sizeof("themetxt") - 1) == 0) {
        element->type = ASSET_TYPE_THEME;

        // Read file
        UIThemeStyle theme;
        theme.data = ring_memory_get(memory_volatile, file_size(input_path) * 2 + sizeof(UIThemeStyle), 4);
        theme_from_file_txt(&theme, input_path, memory_volatile);

        // Create output data
        element->uncompressed = theme_to_data(&theme, *archive_body);
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;
    } else if (strncmp(extension, ".png", sizeof("png") - 1) == 0
        || strncmp(extension, ".bmp", sizeof("bmp") - 1) == 0
        || strncmp(extension, ".tga", sizeof("tga") - 1) == 0
    ) {
        element->type = ASSET_TYPE_IMAGE;

        Image image = {0};

        if (strncmp(extension, ".png", sizeof("png") - 1) == 0) {
            // @todo Once we have implemented our own png just use the image_from_file function below
            int32 w2,h2;
            byte* data2 = stbi_load(input_path, &w2, &h2, 0, 4);

            image.width = w2;
            image.height = h2;
            image.pixel_count = w2 * h2;
            image.pixels = data2;
            image.image_settings |= 4;
        } else {
            image_from_file(&image, input_path, memory_volatile);
        }

        // @performance The way how we load assets (see overhead usage) we could maybe use only the pixel data as size instead of also including the header size here
        // The same could be said for all assets actually
        element->uncompressed = image_data_size(&image);

        *archive_body += image_header_to_data(&image, *archive_body);
        *archive_body += qoi_encode(&image, *archive_body);

        if (strncmp(extension, ".png", sizeof("png") - 1) == 0) {
            free(image.pixels);
        }

        element->length = *archive_body - element_start;
    } else if (strncmp(extension, ".fs", sizeof("fs") - 1) == 0
        || strncmp(extension, ".vs", sizeof("vs") - 1) == 0
        || strncmp(extension, ".hlsl", sizeof("hlsl") - 1) == 0
    ) {
        element->type = ASSET_TYPE_GENERAL;

        FileBody file = {0};
        file_read(input_path, &file, memory_volatile);

        char* optimized = (char *) ring_memory_get(memory_volatile, file.size, 4);

        const int32 opt_size = strncmp(extension, ".hlsl", sizeof("hlsl") - 1)
            ? directx_program_optimize((char *) file.content, optimized)
            : opengl_program_optimize((char *) file.content, optimized);

        // @todo we should compress this file here
        memcpy(*archive_body, optimized, opt_size);
        element->uncompressed = opt_size;
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;
    } else {
        element->type = ASSET_TYPE_GENERAL;

        FileBody file = {0};
        file_read(input_path, &file, memory_volatile);

        // @todo we should compress this file here
        memcpy(*archive_body, file.content, file.size);
        element->uncompressed = (uint32) file.size;
        *archive_body += element->uncompressed;

        element->length = *archive_body - element_start;
    }
}

int32 build_asset_archive(
    RingMemory* memory_volatile,
    char* argv[],
    const char* rel_path,
    char* enum_char,
    FileBody* toc,
    int32 archive_id
)
{
    BufferMemory memory_output = {0};
    memory_output.memory = (byte *) malloc(sizeof(byte) * GIGABYTE * 4);
    memory_output.head = memory_output.memory;
    memory_output.size = sizeof(byte) * GIGABYTE * 4;
    memory_output.end = memory_output.memory + memory_output.size;

    AssetArchiveHeader header = {0};
    header->version = ASSET_ARCHIVE_VERSION;
    header->asset_element = (AssetArchiveElement *) malloc(sizeof(AssetArchiveElement) * 10000);
    header->asset_dependencies = (uint32 *) malloc(sizeof(uint32) * 10000);

    // We don't know how large our header actually needs to be
    // We just pick a generous offset.
    // Don't worry we will not store empty space in our archive file
    FileBody output_body = {0};
    output_body.content = buffer_memory_get(&memory_output, 4 * GIGABYTE, sizeof(size_t));

    // We need some space in the beginning for the header
    byte* body_start = output_body.content + 100 * MEGABYTE;
    byte* archive_body = body_start;

    // Create archive file data by loading the assets from the table of contents (toc)
    // and then loading this asset data into our archive file
    // While "copying" the asset data over to the asset archive we do some transformation & compression
    // as needed and used in our game engine
    byte* pos = toc->content;
    while (*pos != '\0') {
        // Get the file path for the asset (stored in the toc)
        char* file_path = (char *) pos;
        str_move_to((const char **) &pos, '\n');
        if (*pos == '\n') {
            if (*(pos - 1) == '\r') {
                *(pos - 1) = '\0';
            }

            *pos++ = '\0';
        }

        // Make path absolute based on input file
        char input_path[PATH_MAX_LENGTH];
        if (*file_path == '.') {
            memcpy(input_path, rel_path, PATH_MAX_LENGTH);
            strcpy(input_path + strlen(input_path), file_path + 1);
        } else {
            strcpy(input_path, file_path);
        }

        build_asset(
            memory_volatile,
            rel_path,
            input_path,
            &archive_body, body_start,
            enum_char,
            &header,
            archive_id
        );
    }

    byte* output = output_body.content;

    output = write_le(output, header.version);
    output = write_le(output, header.asset_count);
    output = write_le(output, header.asset_dependency_count);

    const int32 header_offset = sizeof(archive->header.version)
        + sizeof(archive->header.asset_count)
        + sizeof(archive->header.asset_dependency_count)
        + header->asst_count * sizeof(AssetArchiveElement)
        + header->asset_dependency_count * sizeof(int32);

    for (int i = 0; i < header.asset_count; ++i) {
        header.asset_element->start += header_offset;
    }

    memcpy(output, header.asset_element, header.asset_count * sizeof(AssetArchiveElement));
    SWAP_ENDIAN_LITTLE_SIMD(
        (int32 *) output,
        (int32 *) output,
        header->asset_count * sizeof(AssetArchiveElement) / 4, // everything is 4 bytes -> easy to swap
        1
    );
    output += header.asset_count * sizeof(AssetArchiveElement);

    // Now write dependency array
    if (header->asset_dependency_count > 0) {
        ASSERT_TRUE(output % alignof(uint32));
        memcpy(output, header->asset_dependencies, header->asset_dependency_count * sizeof(uint32));
        SWAP_ENDIAN_LITTLE_SIMD(
            (int32 *) output,
            (int32 *) output,
            header->asset_dependency_count, // everything is 4 bytes -> easy to swap
            1
        );
        output += header->asset_dependency_count * sizeof(uint32);
    }

    memcpy(output, body_start, archive_body - body_start);
    output += archive_body - body_start;

    output_body.size = output - output_body.content;

    // We now write the data to the file
    file_write(argv[2], &output_body);
}

int32 main(int32 argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Generating archive...\n");

    char rel_path[PATH_MAX_LENGTH];
    create_base_path(argv[1], rel_path);

    // Table of contents used to create the archive file
    FileBody toc = {0};
    toc.content = (byte *) malloc(sizeof(byte) * MEGABYTE * 4);
    file_read(argv[1], &toc);

    // parsing the asset file id
    int32 toc_id = atoi((char *) toc.content);
    while (*toc.content != '\n') {
        ++toc.content;
        --toc.size;
    }

    ++toc.content;
    --toc.size;

    RingMemory memory_volatile = {0};
    memory_volatile.memory = (byte *) malloc(sizeof(byte) * GIGABYTE * 1);
    memory_volatile.head = memory_volatile.memory;
    memory_volatile.size = sizeof(byte) * GIGABYTE * 1;
    memory_volatile.end = memory_volatile.memory + memory_volatile.size;

    // This holds all the enum names
    char* enum_char = malloc(sizeof(char) * 64 * 1024);

    // Careful, argv and toc both get modified.
    int32 asset_count = build_asset_archive(&memory_volatile, argv, rel_path, enum_char, &toc, toc_id);
    build_enum(&memory_volatile, enum_char, asset_count, argv[2], toc_id);

    printf("Archive generated\n");
}
