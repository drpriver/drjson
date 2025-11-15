#ifndef CMD_PARSE_C
#define CMD_PARSE_C
#include "cmd_parse.h"
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

CMD_PARSE_WARN_UNUSED
static
int
cmd_param_parse_signature(StringView sig, CmdParams* params){
    params->count = 0;
    _Bool optional = 0;
    _Bool in_string = 0;
    const char* p = sig.text;
    const char* end = sig.text + sig.length;
    const char* current = NULL;
    // Skip leading command
    for(;p!=end;p++){
        if(*p == ' ')
            break;
    }
    for(;p!=end;){
        // skip leading whitespace
        if(*p == ' '){
            p++;
            continue;
        }
        if(*p == '['){
            if(optional) return 1; // TODO: better error reporting
            optional = 1;
            p++;
            continue;
        }
        if(*p == '<'){
            if(in_string) return 1;
            in_string = 1;
            p++;
            current = p;
            continue;
        }
        if(*p == ']'){
            if(!optional) return 1;
            optional = 0;
            p++;
            if(current){
                // TODO
                current = NULL;
            }
            continue;
        }
        if(*p == '>'){
            if(!in_string) return 1;
            if(!current) return 1;
            in_string = 0;
            // TODO
            current = NULL;
            p++;
            continue;
        }
        if(*p == '|'){
            // TODO
            current = NULL;
            p++;
            continue;
        }
        if(!current){
            current = p;
        }
        p++;
        continue;
    }
    return 0;
}

CMD_PARSE_WARN_UNUSED
static
int
cmd_param_parse_args(StringView cmd_line, const CmdParams* params, CmdArgs* args){
    args->count = 0;
    // TODO
    return 1;
    __builtin_debugtrap();
    (void)cmd_line;
    (void)params;
    return 0;
}

CMD_PARSE_WARN_UNUSED
static
int
cmd_get_arg_bool(CmdArgs* args, StringView name, _Bool* out){
    for(size_t i = 0; i < args->count; i++){
        CmdArg* arg = &args->args[i];
        const CmdParam* param = arg->param;
        if(arg->consumed) continue;
        if(SV_equals(param->names[0], name) || SV_equals(param->names[1], name)){
            if(param->kind != CMD_PARAM_FLAG)
                return CMD_ARG_ERROR_TYPE_ERROR;
            if(!arg->present){
                if(param->optional)
                    return CMD_ARG_ERROR_MISSING_BUT_OPTIONAL;
                return CMD_ARG_ERROR_MISSING;
            }
            _Bool match = SV_equals(arg->content, name);
            if(match) arg->consumed = 1;
            *out = match;
            return CMD_ARG_ERROR_NONE;
        }
    }
    return CMD_ARG_ERROR_MISSING_PARAM;
}

CMD_PARSE_WARN_UNUSED
static
int
cmd_get_arg_string(CmdArgs* args, StringView name, StringView* out){
    for(size_t i = 0; i < args->count; i++){
        CmdArg* arg = &args->args[i];
        const CmdParam* param = arg->param;
        if(arg->consumed) continue;
        if(SV_equals(param->names[0], name) || SV_equals(param->names[1], name)){
            if(param->kind != CMD_PARAM_PATH && param->kind != CMD_PARAM_STRING)
                return CMD_ARG_ERROR_TYPE_ERROR;
            if(!arg->present){
                if(param->optional)
                    return CMD_ARG_ERROR_MISSING_BUT_OPTIONAL;
                return CMD_ARG_ERROR_MISSING;
            }
            *out = arg->content;
            arg->consumed = 1;
            return CMD_ARG_ERROR_NONE;
        }
    }
    return CMD_ARG_ERROR_MISSING_PARAM;
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
