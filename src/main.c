#if defined(__STDC_LIB_EXT1__)
  #if (__STDC_LIB_EXT1__ >= 201112L)
    #define USE_EXT1 1
    #define __STDC_WANT_LIB_EXT1__ 1 /* Want the ext1 functions */
  #endif
#endif

#define  _BSD_SOURCE

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <sys/types.h>
#include <dirent.h>

#include <jansson.h>

typedef char path_t[512];

#define STEP_EXECUTABLE_FLAG 0x0001
#define STEP_GENERIC_PROJECT 0x1000

#ifdef __STDC_LIB_EXT1__
#define LOG_ERRNO() do { \
    size_t errmsglen = strerrorlen_s(errno) + 1; \
    char errmsg[errmsglen]; \
    strerror_s(errmsg, errmsglen, errno); \
    } while (0)
#else
#define LOG_ERRNO() do { \
    const char *errmsg = strerror(errno); \
    fprintf(stderr, "ERROR %s\n", errmsg); \
    } while (0)
#endif

typedef struct {
    size_t  size;
    char    filename[];
} source_t;

typedef struct {
    uint32_t flags;
    char name[128];
    void *sources;
    size_t sources_size;
    size_t sources_count;
} step_t;

step_t *all_steps;
size_t all_steps_num;

static void
print_usage(FILE *output, const char *app) {
    fprintf(output, "Usage:\n \t%s [-o output path] <source file>\n", app);
}

static int
parse_source(const char *path) {
    fprintf(stdout, "Parsing... %s\n", path);

    json_error_t error;
    json_t *root = json_load_file(path, 0, &error);
    if (!root) {
        fprintf(stderr, "ERROR line(%d): %s\n", error.line, error.text);
        exit(EXIT_FAILURE);
    }

    json_t *steps = json_object_get(root, "steps");
    if (!json_is_array(steps)) {
        fprintf(stderr, "ERROR: steps is not an array\n");
        json_decref(root);
        return -1;
    }

    fprintf(stdout, "steps %zu\n", json_array_size(steps));

    all_steps_num = json_array_size(steps);
    all_steps = malloc(all_steps_num * sizeof (step_t));
    memset(all_steps, 0, all_steps_num * sizeof (step_t));

    for(size_t i = 0; i < json_array_size(steps); i++) {
        json_t *step = json_array_get(steps, i);
        if (!json_is_object(step)) {
            fprintf(stderr, "ERROR: step %d is not an object\n", (int)(i + 1));
            json_decref(root);
            return 1;
        }

        json_t *name = json_object_get(step, "name");
        if (name)
            strcpy(all_steps[i].name, json_string_value(name));

        json_t *executable = json_object_get(step, "executable");
        if (executable)
            if (json_boolean_value(executable)) {
                fprintf(stdout, "\t executable\n");

                all_steps[i].flags |= STEP_EXECUTABLE_FLAG;
            }

        json_t *sources = json_object_get(step, "sources");
        if (json_is_string(sources)) {
            size_t name_len = strlen(json_string_value(sources));
            all_steps[i].sources_size = name_len + sizeof (size_t);
            all_steps[i].sources_count = 1;
            all_steps[i].sources = malloc(sizeof (source_t) * name_len + 1);
            memset(all_steps[i].sources, 0, sizeof (source_t) * name_len + 1);
            *((size_t*)all_steps[i].sources) = name_len;
            strncpy((char*)all_steps[i].sources + sizeof (size_t), json_string_value(sources), name_len);
        }

        json_t *pro = json_object_get(step, "project");
        if (json_is_string(pro)) {
            if (strcasecmp("generic", json_string_value(pro)) == 0)
                all_steps[i].flags |= STEP_GENERIC_PROJECT;
        }
    }

    json_decref(root);

    return 0;
}

// TODO: without make templates
static char*
get_sources(const char *path, const char *ext, const char *delimer) {
    DIR *dp = opendir(path);
    if (!dp)
        perror("Couldn't open the directory");

    size_t source_size = 0;
    char *sources = NULL;

    const size_t delimer_size = strlen(delimer);

    struct dirent *ep;
    while ((ep = readdir(dp)) != NULL) {
        // TODO: dt_type DT_REG
        const char *file_ext = strrchr(ep->d_name, '.');
        if (file_ext && strcmp(file_ext, ext) == 0)
            if (ep->d_type == DT_REG) {
                bool first = false;
                if (source_size == 0)
                    first = true;

                source_size += strlen(ep->d_name) + delimer_size;
                sources = realloc(sources, source_size);

                if (!first)
                    strcat(sources, delimer);

                strcat(sources, ep->d_name);
            }
    }

    closedir(dp);

    return sources;
}

#define DEF_CLEAN_EXTS "*~ *.o *.d Makefile"
#define GENERIC_PRO_EXTS " *.files *.includes *.config *.creator *.user"

static int
generate_makefiles(step_t *steps, size_t size, const char *path) {
    fprintf(stdout, "Generating make files... in %s\n", path);

    path_t filepath = {0};
    strncpy(filepath, path, sizeof filepath);
    strcat(filepath, "Makefile");

    errno = 0;
    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        LOG_ERRNO();
        return errno;
    }

    // TODO: Alloc memory for this
    char executable[4096] = {0};
    char sources[4096] = {0};
    char objs[20] = {0};
    char clean_exts[128] = {0};

    for (size_t j = 0; j < size; j++) {
        fprintf(stdout, "Step \'%s\'\n", steps[j].name);

        strcpy(clean_exts, DEF_CLEAN_EXTS);

        if (steps[j].flags & STEP_EXECUTABLE_FLAG)
            strncat(executable, steps[j].name, sizeof executable);

        if (steps[j].flags & STEP_GENERIC_PROJECT)
            strncat(clean_exts, GENERIC_PRO_EXTS, sizeof clean_exts);

        size_t offset = sizeof (size_t);
        for (size_t i = 0; i < steps[i].sources_count; i++) {
            char *source = (char*)all_steps[i].sources + offset;
            if (strchr(source, '*')) {
                char s[128];
                snprintf(s, sizeof s, "$(wildcard $(addsuffix /%s, $(SOURCES_DIRS)))", source);

                const char *ext = strrchr(source, '.');
                if (ext)
                    strcpy(objs, ext);

                if (ext)
                    puts(ext);

                strcat(sources, s);
            }

            printf("sources %s\n", source);
            offset += sizeof (size_t) + *(size_t*)all_steps[i].sources;
        }
    }

    fputs("# WARNING: Do not modify this file it is auto generated\n\n", fp);

    // Variables
    char line[4096]; // TODO: Calculate size
    snprintf(line, sizeof line, "CC := %s\n", "gcc");
    fputs(line, fp);

    snprintf(line, sizeof line, "CXX := %s\n", "g++");
    fputs(line, fp);

    snprintf(line, sizeof line, "CFLAGS := %s\n", "");
    fputs(line, fp);

    snprintf(line, sizeof line, "CXXFLAGS := %s\n", "");
    fputs(line, fp);

    snprintf(line, sizeof line, "LDFLAGS := %s\n", "");
    fputs(line, fp);

    snprintf(line, sizeof line, "LDLIBS := %s\n", "");
    fputs(line, fp);

    snprintf(line, sizeof line, "SOURCES_DIRS := %s\n", ".");
    fputs(line, fp);

    //char *sources_list = get_sources(path, ".c", " ");
    //snprintf(line, sizeof line, "SOURCES := %s\n", "/*.c");
    snprintf(line, sizeof line, "SOURCES := %s\n", sources);
    fputs(line, fp);
    //free(sources_list);

    snprintf(line, sizeof line, "OBJECTS := $(OBJECTS:%s=.o)\n", objs);
    fputs("OBJECTS := $(notdir $(SOURCES))\n", fp);
    fputs(line, fp);

    snprintf(line, sizeof line, "EXECUTABLE := %s\n", executable);
    fputs(line, fp);

    // Targets
    fputs("\n", fp);
    fputs("all: $(SOURCES) $(EXECUTABLE)\n\n", fp);
    fputs("$(EXECUTABLE): $(OBJECTS)\n\t$(CC) -o $@ $(OBJECTS) $(LDFLAGS) -pipe\n\n", fp);
    fputs("%.o: %.c\n\t$(CC) $< $(CFLAGS) -c -MD -pipe\n\n", fp);
    fputs("%.o: %.cpp\n\t$(CXX) $< $(CXXFLAGS) -c -MD -pipe\n\n", fp);
    fprintf(fp, "clean:\n\trm -rf %s $(EXECUTABLE)\n\n", clean_exts);
    fputs(".PHONY: all clean install uninstall", fp);

    fclose(fp);

    return 0;
}

static int
generate_projects(step_t *steps, size_t size, const char *path) {
    fprintf(stdout, "Generating project files... in %s\n", path);

    char files[4096] = {0};

    for (size_t j = 0; j < size; j++) {
        if (!(steps[j].flags & STEP_GENERIC_PROJECT))
            continue;

        fprintf(stdout, "Step \'%s\'\n", steps[j].name);

        size_t offset = 0;
        for (size_t i = 0; i < steps[j].sources_count; i++) {
            char *source = (char*)all_steps[j].sources + sizeof (size_t) + offset;
            if (strchr(source, '*')) {
                char *sources_list = get_sources(path, ".c", "\n");

                if (sources_list)
                    strcat(files, sources_list);

                free(sources_list);
            }

            printf("sources %s\n", source);
            offset += *(size_t*)all_steps[j].sources;
        }

        // .files
        path_t filepath = {0};
        strncpy(filepath, path, sizeof filepath);
        strcat(filepath, all_steps[j].name);
        strcat(filepath, ".files");

        errno = 0;
        FILE *fp = fopen(filepath, "w");
        if (!fp) {
            LOG_ERRNO();
            return errno;
        }

        if (files[0] != 0)
            fputs(files, fp);

        fclose(fp);

        // .includes
        strncpy(filepath, path, sizeof filepath);
        strcat(filepath, all_steps[j].name);
        strcat(filepath, ".includes");

        errno = 0;
        fp = fopen(filepath, "w");
        if (!fp) {
            LOG_ERRNO();
            return errno;
        }

        fclose(fp);

        // .config
        strncpy(filepath, path, sizeof filepath);
        strcat(filepath, all_steps[j].name);
        strcat(filepath, ".config");

        errno = 0;
        fp = fopen(filepath, "w");
        if (!fp) {
            LOG_ERRNO();
            return errno;
        }

        fclose(fp);

        // .creator
        strncpy(filepath, path, sizeof filepath);
        strcat(filepath, all_steps[j].name);
        strcat(filepath, ".creator");

        errno = 0;
        fp = fopen(filepath, "w");
        if (!fp) {
            LOG_ERRNO();
            return errno;
        }

        fputs("[General]\n", fp);

        fclose(fp);
    }

    return 0;
}

extern int
main(int argc, char* argv[]) {
    path_t source_path = {0};
    path_t out_path;

    if (argc == 1) {
        print_usage(stdout, argv[0]);
        exit(EXIT_SUCCESS);
    }

    // parse options
    int opt;
    for (opt = 1; (opt < argc) && (argv[opt][0] == '-'); opt++) {
        if (argv[opt][1] == 'o') {
            if (opt + 1 < argc)
                strncpy(out_path, argv[opt + 1], sizeof out_path);

            continue;
        }

        print_usage(stderr, argv[0]);
        exit(EXIT_FAILURE);
    }

    // get source path
    if (argc >= opt + 1) {
        if (opt == 1)
            opt = 0;

        strncpy(source_path, argv[opt + 1], sizeof source_path);
    }

    if (source_path[0] != 0)
        parse_source(source_path);

    if (out_path[0] != 0) {
        generate_makefiles(all_steps, all_steps_num, out_path);
        generate_projects(all_steps, all_steps_num, out_path);
    }

    if (all_steps)
        free(all_steps);

    return EXIT_SUCCESS;
}
