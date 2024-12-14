#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OPENGL 1

#ifdef DEBUG
    #undef DEBUG
#endif

#define DEBUG 0

#include "debug.h"
#include "../GameEngine/log/Debug.cpp"

#if _WIN32
    #include <windows.h>
    #include "../GameEngine/platform/win32/FileUtils.cpp"
#else
    #include "../GameEngine/platform/linux/FileUtils.cpp"
#endif

#include "../GameEngine/ui/UITheme.h"
#include "../GameEngine/utils/StringUtils.h"
#include "../GameEngine/asset/AssetArchive.h"
#include "../GameEngine/audio/Audio.cpp"
#include "../GameEngine/object/Mesh.h"
#include "../GameEngine/localization/Language.h"
#include "../GameEngine/gpuapi/opengl/ShaderUtils.h"

#define STB_IMAGE_IMPLEMENTATION
#include "../GameEngine/image/stb_image.h"

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

void build_enum(RingMemory* memory_volatile, FileBody* toc, char* output_path)
{
    byte* enum_file_data = ring_get_memory(memory_volatile, 1 * MEGABYTE, 4);

    sprintf((char *) enum_file_data,
        "/**\n"
        " * Jingga\n"
        " *\n"
        " * @copyright Jingga\n"
        " * @license    License 2.0\n"
        " * @version   1.0.0\n"
        " * @link      https://jingga.app\n"
        " */\n"
        "#ifndef TOS_ASSET_ARCHIVE_IDS\n"
        "#define TOS_ASSET_ARCHIVE_IDS\n"
        "\n"
        "enum AssetArchiveIds {\n"
    );

    for (int32 i = 0; i < toc->size; ++i) {
        while (toc->content[i] == '\0') {
            ++i;
        }

        if (i >= toc->size) {
            break;
        }

        char* line = (char *) (toc->content + i);
        char buffer[512];
        char enum_name[512];

        // Extract the last directory and file name
        char* last_dir = strrchr(line, '/');
        if (!last_dir) {
            last_dir = strrchr(line, '\\');
        }

        if (!last_dir) {
            // Skip if no '/' is found
            continue;
        }

        const char* file_name = last_dir + 1;

        // Extract the second-last directory
        const char* second_last_dir = NULL;
        for (const char* p = line; p < last_dir; ++p) {
            if (*p == '/' || *p == '\\') {
                second_last_dir = p;
            }
        }

        if (second_last_dir == NULL) {
            continue;
        }

        ++second_last_dir; // Move past '/'
        *last_dir = '\0'; // Make second_last_dir string stop at last dir

        // Build the enum entry name
        snprintf(buffer, sizeof(buffer), "%s_%s", second_last_dir, file_name);

        // Remove file extension
        char* dot = strrchr(buffer, '.');
        if (dot != NULL) {
            *dot = '_';
        }

        // Transform to enum format
        int32 j = 0;
        for (int32 c = 0; buffer[c] != '\0'; ++c) {
            if (isalnum(buffer[c])) {
                enum_name[j++] = toupper_ascii(buffer[c]);
            } else {
                enum_name[j++] = '_';
            }
        }
        enum_name[j] = '\0';

        // Add the entry to the enum file content
        sprintf((char *) enum_file_data + strlen((char *) enum_file_data), "    AA_ID_%s,\n", enum_name);

        while (toc->content[i] != '\0') {
            ++i;
        }
    }

    sprintf((char *) enum_file_data + strlen((char *) enum_file_data),
        "};\n\n"
        "#endif\n"
    );

    FileBody enum_file;
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

void build_asset_archive(RingMemory* memory_volatile, char* argv[], const char* rel_path, FileBody* toc)
{
    BufferMemory memory_output = {};
    memory_output.memory = (byte *) malloc(sizeof(byte) * GIGABYTE * 4);
    memory_output.head = memory_output.memory;
    memory_output.size = sizeof(byte) * GIGABYTE * 4;
    memory_output.end = memory_output.memory + memory_output.size;

    // Output memory ranges
    byte* archive_header = buffer_get_memory(&memory_output, 4 * MEGABYTE, 4, true);
    FileBody output_header;
    output_header.content = archive_header;

    byte* archive_body = archive_header + 4 * MEGABYTE;
    FileBody output_body;
    output_body.content = archive_body;

    *((int32 *) archive_header) = SWAP_ENDIAN_LITTLE(ASSET_ARCHIVE_VERSION);
    archive_header += sizeof(int32);

    *((uint32 *) archive_header) = 0;
    uint32* asset_count = (uint32 *) archive_header;
    uint32 temp_asset_count = 0;
    archive_header += sizeof(uint32);

    *((uint32 *) archive_header) = 0;
    uint32* asset_dependency_count = (uint32 *) archive_header;
    archive_header += sizeof(uint32);

    int64 index = 0;

    // Create archive file data
    byte* pos = toc->content;
    while (*pos != '\0') {
        // Get file path
        char* file_path = (char *) pos;
        str_move_to((char **) &pos, '\n');
        if (*pos == '\n') {
            if (*(pos - 1) == '\r') {
                *(pos - 1) = '\0';
            }

            *pos++ = '\0';
        }

        // Make path absolute based on input file
        char input_path[MAX_PATH];
        if (*file_path == '.') {
            memcpy(input_path, rel_path, MAX_PATH);
            strcpy(input_path + strlen(input_path), file_path + 1);
        } else {
            strcpy(input_path, file_path);
        }

        // Get extension
        char* extension = (char *) (pos - 1);
        while (*extension != '.') {
            --extension;
        }

        ++temp_asset_count;

        byte* element_start = archive_body;

        uint32 element_type;
        if (strncmp(extension, ".wav", sizeof("wav") - 1) == 0) {
            element_type = ASSET_TYPE_AUDIO;

            // Read file
            Audio audio;
            audio.data = ring_get_memory(memory_volatile, file_size(input_path) + sizeof(Audio), 4);
            audio_from_file(&audio, input_path, memory_volatile);

            // Create output data
            archive_body += audio_to_data(&audio, archive_body);
        } else if (strncmp(extension, ".objtxt", sizeof("objtxt") - 1) == 0) {
            element_type = ASSET_TYPE_OBJ;

            // Read file
            Mesh mesh;
            mesh.data = ring_get_memory(memory_volatile, file_size(input_path) * 2 + sizeof(Mesh), 4);
            mesh_from_file_txt(&mesh, input_path, memory_volatile);

            // Create output data
            archive_body += mesh_to_data(&mesh, archive_body);
        } else if (strncmp(extension, ".langtxt", sizeof("langtxt") - 1) == 0) {
            element_type = ASSET_TYPE_LANGUAGE;

            // Read file
            Language lang;
            lang.data = ring_get_memory(memory_volatile, file_size(input_path) + 1024, 4);
            language_from_file_txt(&lang, input_path, memory_volatile);

            // Create output data
            archive_body += language_to_data(&lang, archive_body);
        } else if (strncmp(extension, ".fonttxt", sizeof("fonttxt") - 1) == 0) {
            element_type = ASSET_TYPE_FONT;

            // Read file
            Font font;
            font.glyphs = (Glyph *) ring_get_memory(memory_volatile, file_size(input_path) + sizeof(Glyph), 4);
            font_from_file_txt(&font, input_path, memory_volatile);

            // Create output data
            archive_body += font_to_data(&font, archive_body);
        } else if (strncmp(extension, ".themetxt", sizeof("themetxt") - 1) == 0) {
            element_type = ASSET_TYPE_THEME;

            // Read file
            UIThemeStyle theme;
            theme.data = ring_get_memory(memory_volatile, file_size(input_path) * 2 + sizeof(UIThemeStyle), 4);
            theme_from_file_txt(&theme, input_path, memory_volatile);

            // Create output data
            archive_body += theme_to_data(&theme, archive_body);
        } else if (strncmp(extension, ".png", sizeof("png") - 1) == 0
            || strncmp(extension, ".bmp", sizeof("bmp") - 1) == 0
            || strncmp(extension, ".tga", sizeof("tga") - 1) == 0
        ) {
            element_type = ASSET_TYPE_IMAGE;

            Image image = {};

            if (strncmp(extension, ".png", sizeof("png") - 1) == 0) {
                // @todo Once we have implemented our own png just use the image_from_file function below
                int32 w2,h2;
                byte* data2 = stbi_load(input_path, &w2, &h2, 0, 4);

                image.width = w2;
                image.height = h2;
                image.pixel_count = w2 * h2;
                image.pixel_type = PIXEL_TYPE_RGBA;
                image.pixels = data2;
            } else {
                image_from_file(&image, input_path, memory_volatile);
            }

            archive_body += image_to_data(&image, archive_body);

            if (strncmp(extension, ".png", sizeof("png") - 1) == 0) {
                free(image.pixels);
            }
        } else {
            element_type = ASSET_TYPE_GENERAL;

            FileBody file;
            file_read(file_path, &file, memory_volatile);

            if (strncmp(extension, ".fs", sizeof("fs") - 1) == 0
                || strncmp(extension, ".vs", sizeof("vs") - 1) == 0
            ) {
                char* optimized = (char *) ring_get_memory(memory_volatile, file.size, 4);
                int32 opt_size = shader_program_optimize((char *) file.content, optimized);

                memcpy(archive_body, optimized, opt_size);
                archive_body += opt_size;
            } else {
                memcpy(archive_body, file.content, file.size);
                archive_body += file.size;
            }
        }

        // WARNING: the offsets need to be adjusted in the future by the header size

        // Type
        *((uint32 *) archive_header) = SWAP_ENDIAN_LITTLE(element_type);
        archive_header += sizeof(uint32);

        // Start
        *((uint32 *) archive_header) = (uint32) (element_start - output_body.content);
        archive_header += sizeof(uint32);

        // Length
        *((uint32 *) archive_header) = SWAP_ENDIAN_LITTLE((uint32) (archive_body - element_start));
        archive_header += sizeof(uint32);

        // Dependency Start
        *((uint32 *) archive_header) = 0;
        archive_header += sizeof(uint32);

        // Dependency Count
        *((uint32 *) archive_header) = 0;
        archive_header += sizeof(uint32);
    }

    *asset_count = SWAP_ENDIAN_LITTLE(temp_asset_count);
    *asset_dependency_count = SWAP_ENDIAN_LITTLE(*asset_dependency_count);

    // Calculate header size
    output_header.size = archive_header - output_header.content;

    // Go to first asset_element (after version, asset_count, asset_dependency_count)
    archive_header = output_header.content + sizeof(int32) * 3;
    for (uint32 i = 0; i < temp_asset_count; ++i) {
        AssetArchiveElement* element = (AssetArchiveElement *) archive_header;
        element->start = SWAP_ENDIAN_LITTLE((uint32) (element->start + output_header.size));
        element->dependency_start = SWAP_ENDIAN_LITTLE((uint32) (element->dependency_start + output_header.size));

        archive_header += sizeof(AssetArchiveElement);
    }

    // Output archive file
    file_write(argv[2], &output_header);

    output_body.size = archive_body - output_body.content;
    file_append(argv[2], &output_body);
}

int32 main(int32 argc, char* argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <input> <output>\n", argv[0]);
        return EXIT_FAILURE;
    }

    printf("Generating archive...\n");

    char rel_path[MAX_PATH];
    create_base_path(argv[1], rel_path);

    // Table of contents used to create the archive file
    FileBody toc;
    toc.content = (byte *) malloc(sizeof(byte) * MEGABYTE * 4);
    file_read(argv[1], &toc);

    RingMemory memory_volatile = {};
    memory_volatile.memory = (byte *) malloc(sizeof(byte) * GIGABYTE * 1);
    memory_volatile.head = memory_volatile.memory;
    memory_volatile.size = sizeof(byte) * GIGABYTE * 1;
    memory_volatile.end = memory_volatile.memory + memory_volatile.size;

    // Careful, argv and toc both get modified.
    build_asset_archive(&memory_volatile, argv, rel_path, &toc);
    build_enum(&memory_volatile, &toc, argv[2]);

    printf("Archive generated\n");
}
