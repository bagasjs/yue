#define NOB_IMPLEMENTATION
#include "nob.h"

const char *resolve_include_file_path(String_View include_file, const char *input_file_path)
{
    const char *dir = nob_temp_dir_name(input_file_path);
    return nob_temp_sprintf("%s"SV_Fmt, dir, SV_Arg(include_file));
}

NOBDEF Nob_String_View nob_sv_chop_by_sv(Nob_String_View *sv, Nob_String_View delim)
{
    size_t i = 0;
    while (i < sv->count) {
        bool should_break = true;
        for(int j = 0; j < delim.count; ++j) {
            if(sv->items[i + j] != delim.items[j]) {
                should_break = false;
            }
        }
        if(should_break) break;
        i += 1;
    }

    Nob_String_View result = nob_sv_from_parts(sv->data, i);

    if (i < sv->count) {
        sv->count -= i + delim.count;
        sv->data  += i + delim.count;
    } else {
        sv->count -= i;
        sv->data  += i;
    }

    return result;
}

bool preprocess_nob_in_file(const char *input_file_path, Nob_String_Builder *output)
{
    Nob_String_Builder input  = {0};
    if(!read_entire_file(input_file_path, &input)) return false;
    String_View sv = nob_sb_to_sv(input);
    int i = 0;
    static const String_View include_sv = SVLIT_STATIC("#include");
    while(sv.count > 0) {
        String_View line = nob_sv_chop_by_delim(&sv, '\n');
        if(sv_chop_prefix(&line, include_sv)) {
            String_View file = sv_trim(nob_sv_chop_by_sv(&line, SVLIT("//")));
            file.items += 1;
            file.count -= 2;
            if(line.count > 0) {
                String_View command = sv_trim(line);
                line = sv_chop_by_delim(&command, '=');
                command = sv_trim(command);
                if(sv_eq(command, SVLIT("inline_include"))) {
                    const char *include_file = resolve_include_file_path(file, input_file_path);
                    sb_appendf(output, "/// --- [BEGIN] Amalgamation of \""SV_Fmt"\"\n", SV_Arg(file));
                    if(!preprocess_nob_in_file(include_file, output)) return false;
                    sb_appendf(output, "/// --- [END]   Amalgamation of \""SV_Fmt"\"\n", SV_Arg(file));
                } else if(sv_eq(command, SVLIT("skip_line"))) {
                } else {
                    sb_appendf(output, "#include \""SV_Fmt"\"\n", SV_Arg(file));
                }
            } else {
                sb_appendf(output, "#include \""SV_Fmt"\"\n", SV_Arg(file));
            }
        } else {
            sb_appendf(output, SV_Fmt"\n", SV_Arg(line));
        }
        i += 1;
    }

    sb_free(input);
    return true;
}

int main(int argc, char *argv[])
{
#ifdef _WIN32
    const char *output_file = "build/yue.exe";
#else
    const char *output_file = "build/yue";
#endif

    GO_REBUILD_URSELF(argc, argv);
    const char *source_files[] = {
        "./src/yue.c",
        "./src/main.c",
    };
    Nob_String_Builder output = {0};
    if(!preprocess_nob_in_file("./src/yue.c", &output)) {
        return false;
    }
    write_entire_file("./build/yue_amalgamated.c", output.items, output.count);

    if(nob_needs_rebuild(output_file, source_files, ARRAY_LEN(source_files))) {
        Nob_Cmd cmd = {0};
        nob_cc(&cmd);
        nob_cc_flags(&cmd);
        cmd_append(&cmd, "-D_CRT_SECURE_NO_WARNINGS");
        nob_cc_output(&cmd, output_file);
        for(int i = 0; i < ARRAY_LEN(source_files); ++i) {
            nob_cc_inputs(&cmd, ARRAY_GET(source_files, i));
        }
        cmd_run(&cmd);
    }
    return 0;
}
