#ifndef CMD_PARSE_C
#define CMD_PARSE_C
#include <string.h>
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
    _Bool in_angle = 0;
    const char* p = sig.text;
    const char* end = sig.text + sig.length;
    const char* token_start = NULL;
    _Bool expecting_alt = 0;  // We just saw | and are expecting an alternative name

    // Skip command name (first token before space)
    for(; p != end && *p != ' '; p++);
    // Skip spaces after command
    for(; p != end && *p == ' '; p++);

    enum {MAX_PARAMS = sizeof params->params / sizeof params->params[0]};

    while(p < end){
        char c = *p;

        if(c == ' '){
            if(token_start && !in_angle){
                // Complete the current token as a flag
                size_t len = p - token_start;
                if(params->count >= MAX_PARAMS) return 1;

                if(expecting_alt && params->count > 0){
                    // This is the second name for the previous param
                    params->params[params->count - 1].names[1] =
                        (StringView){.length = len, .text = token_start};
                    expecting_alt = 0;
                }
                else {
                    CmdParam* param = &params->params[params->count++];
                    param->names[0] = (StringView){.length = len, .text = token_start};
                    param->names[1] = (StringView){0, NULL};
                    param->kind = CMD_PARAM_FLAG;
                    param->optional = optional;
                }
                token_start = NULL;
            }
            p++;
            continue;
        }

        if(c == '['){
            if(optional || in_angle) return 1;
            optional = 1;
            p++;
            continue;
        }

        if(c == ']'){
            if(!optional) return 1;

            if(token_start && !in_angle){
                // Complete pending token
                size_t len = p - token_start;
                if(params->count >= MAX_PARAMS) return 1;

                if(expecting_alt && params->count > 0){
                    params->params[params->count - 1].names[1] =
                        (StringView){.length = len, .text = token_start};
                    expecting_alt = 0;
                }
                else {
                    CmdParam* param = &params->params[params->count++];
                    param->names[0] = (StringView){.length = len, .text = token_start};
                    param->names[1] = (StringView){0, NULL};
                    param->kind = CMD_PARAM_FLAG;
                    param->optional = optional;
                }
                token_start = NULL;
            }

            optional = 0;
            p++;
            continue;
        }

        if(c == '<'){
            if(in_angle) return 1;
            in_angle = 1;
            p++;
            token_start = p;
            continue;
        }

        if(c == '>'){
            if(!in_angle || !token_start) return 1;

            size_t len = p - token_start;
            if(params->count >= MAX_PARAMS) return 1;

            CmdParam* param = &params->params[params->count++];
            param->names[0] = (StringView){.length = len, .text = token_start};
            param->names[1] = (StringView){0, NULL};

            // Determine kind based on name (exact match for "file" or "dir")
            StringView name = param->names[0];
            if(SV_equals(name, SV("file")) || SV_equals(name, SV("dir")))
                param->kind = CMD_PARAM_PATH;
            else
                param->kind = CMD_PARAM_STRING;
            param->optional = optional;

            in_angle = 0;
            token_start = NULL;
            p++;
            continue;
        }

        if(c == '|'){
            if(!token_start || in_angle) return 1;

            // Complete first name and expect alternative
            size_t len = p - token_start;
            if(params->count >= MAX_PARAMS) return 1;

            CmdParam* param = &params->params[params->count++];
            param->names[0] = (StringView){.length = len, .text = token_start};
            param->names[1] = (StringView){0, NULL};
            param->kind = CMD_PARAM_FLAG;
            param->optional = optional;

            expecting_alt = 1;
            token_start = NULL;
            p++;
            // Next non-space char starts the alternative
            if(p < end && *p != ' '){
                token_start = p;
            }
            continue;
        }

        if(!token_start && c != ' '){
            token_start = p;
        }
        p++;
    }

    // Handle trailing token
    if(token_start && !in_angle){
        size_t len = end - token_start;
        if(params->count >= MAX_PARAMS) return 1;

        if(expecting_alt && params->count > 0){
            params->params[params->count - 1].names[1] =
                (StringView){.length = len, .text = token_start};
        }
        else {
            CmdParam* param = &params->params[params->count++];
            param->names[0] = (StringView){.length = len, .text = token_start};
            param->names[1] = (StringView){0, NULL};
            param->kind = CMD_PARAM_FLAG;
            param->optional = optional;
        }
    }

    // Check for unterminated constructs
    if(optional || in_angle) return 1;

    return 0;
}

CMD_PARSE_WARN_UNUSED
static
int
cmd_param_parse_args(StringView cmd_line, const CmdParams* params, CmdArgs* args){
    // Initialize args from params
    for(size_t i = 0; i < params->count; i++){
        args->args[i].param = &params->params[i];
        args->args[i].present = 0;
        args->args[i].consumed = 0;
        args->args[i].content = (StringView){0, NULL};
    }
    args->count = params->count;

    enum {MAX_NON_FLAGS=16};
    // Collect non-flag tokens for string/path args
    const char* non_flag_tokens[MAX_NON_FLAGS];
    size_t non_flag_lengths[MAX_NON_FLAGS];
    size_t non_flag_count = 0;

    // Tokenize and match
    const char* p = cmd_line.text;
    const char* end = cmd_line.text + cmd_line.length;

    while(p < end){
        // Skip whitespace
        while(p < end && *p == ' ') p++;
        if(p >= end) break;

        const char* token_start = p;
        size_t token_len = 0;

        // Scan token - track quote state and bracket depth
        // Break on space only when outside quotes and brackets
        int bracket_depth = 0;
        int brace_depth = 0;
        int backslash = 0;
        char in_quote = 0;  // 0 = not in quote, '"' or '\'' = in that quote type

        for(;p < end;p++){
            char c = *p;

            if(in_quote){
                if(c == '\\'){
                    backslash++;
                    continue;
                }
                // Inside a quote - only the matching quote ends it
                if(c == in_quote && !(backslash & 1)){
                    in_quote = 0;
                }
                backslash = 0;
                continue;
            }
            if(c == '"' || c == '\''){
                // Start of quoted section
                in_quote = c;
                continue;
            }
            if(c == '{'){
                brace_depth++;
                continue;
            }
            if(c == '}'){
                if(brace_depth)
                    brace_depth--;
                continue;
            }
            if(c == '['){
                bracket_depth++;
                continue;
            }
            if(c == ']'){
                if(bracket_depth)
                    bracket_depth--;
                continue;
            }
            if(c == ' ' && bracket_depth == 0 && brace_depth == 0){
                // Space outside of brackets/quotes - end of token
                break;
            }
        }
        token_len = p - token_start;

        StringView token = {.length = token_len, .text = token_start};

        // Check if this token matches any flag param
        _Bool matched_flag = 0;
        for(size_t i = 0; i < params->count; i++){
            const CmdParam* param = &params->params[i];
            if(param->kind != CMD_PARAM_FLAG) continue;

            if(SV_equals(token, param->names[0]) ||
               (param->names[1].text && SV_equals(token, param->names[1]))){
                // Matched this flag
                args->args[i].present = 1;
                args->args[i].content = token;
                matched_flag = 1;
                break;
            }
        }
        if(matched_flag && non_flag_count){
            const char* first = non_flag_tokens[0];
            const char* last = non_flag_tokens[non_flag_count - 1];
            size_t total_len = (last + non_flag_lengths[non_flag_count - 1]) - first;
            StringView concatenated = {.length = total_len, .text = first};

            // Assign to string/path param(s)
            for(size_t i = 0; i < params->count; i++){
                if(args->args[i].present) continue;
                const CmdParam* param = &params->params[i];
                if(param->kind == CMD_PARAM_STRING || param->kind == CMD_PARAM_PATH){
                    args->args[i].present = 1;
                    args->args[i].content = concatenated;
                    break;
                }
            }
            non_flag_count = 0;
        }

        if(!matched_flag){
            // This is a non-flag token, save it for string/path args
            if(non_flag_count < MAX_NON_FLAGS){
                non_flag_tokens[non_flag_count] = token_start;
                non_flag_lengths[non_flag_count] = token_len;
                non_flag_count++;
            }
        }
    }

    // Build string/path arg content by concatenating non-flag tokens
    // Preserve spaces between tokens by using the range from first to last
    if(non_flag_count){
        const char* first = non_flag_tokens[0];
        const char* last = non_flag_tokens[non_flag_count - 1];
        size_t total_len = (last + non_flag_lengths[non_flag_count - 1]) - first;
        StringView concatenated = {.length = total_len, .text = first};
        _Bool used = 0;

        // Assign to string/path param(s)
        for(size_t i = 0; i < params->count; i++){
            if(args->args[i].present) continue;
            const CmdParam* param = &params->params[i];
            if(param->kind == CMD_PARAM_STRING || param->kind == CMD_PARAM_PATH){
                args->args[i].present = 1;
                args->args[i].content = concatenated;
                used = 1;
                break;
            }
        }
        if(!used)
            return 1; // unused string args
    }

    // Check for missing mandatory args
    for(size_t i = 0; i < params->count; i++){
        const CmdParam* param = &params->params[i];
        if(!param->optional && !args->args[i].present){
            return 1;  // Missing mandatory argument
        }
    }

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

CMD_PARSE_WARN_UNUSED
static
int
cmd_get_completion_params(StringView cmd_line, const CmdParams* restrict params, CmdParams* restrict out, StringView* completing_token){
    CmdArgs args_ = {0};
    CmdArgs* args = &args_;
    out->count = 0;
    // Copy-pasted and altered from cmd_param_parse_args

    // Initialize args from params
    for(size_t i = 0; i < params->count; i++){
        args->args[i].param = &params->params[i];
        args->args[i].present = 0;
        args->args[i].consumed = 0;
        args->args[i].content = (StringView){0, NULL};
    }
    args->count = params->count;

    enum {MAX_NON_FLAGS=16};
    // Collect non-flag tokens for string/path args
    StringView non_flag_tokens[MAX_NON_FLAGS];
    size_t non_flag_count = 0;

    // Tokenize and match
    const char* p = cmd_line.text;
    const char* end = cmd_line.text + cmd_line.length;

    // Skip command name (first token before space)
    for(; p != end && *p != ' '; p++);
    // Skip spaces after command
    for(; p != end && *p == ' '; p++);

    while(p < end){
        // Skip whitespace
        while(p < end && *p == ' ') p++;
        if(p >= end) break;

        const char* token_start = p;
        size_t token_len = 0;

        // Scan token - track quote state and bracket depth
        // Break on space only when outside quotes and brackets
        int bracket_depth = 0;
        int brace_depth = 0;
        int backslash = 0;
        char in_quote = 0;  // 0 = not in quote, '"' or '\'' = in that quote type

        for(;p < end;p++){
            char c = *p;

            if(in_quote){
                if(c == '\\'){
                    backslash++;
                    continue;
                }
                // Inside a quote - only the matching quote ends it
                if(c == in_quote && !(backslash & 1)){
                    in_quote = 0;
                }
                backslash = 0;
                continue;
            }
            if(c == '"' || c == '\''){
                // Start of quoted section
                in_quote = c;
                continue;
            }
            if(c == '{'){
                brace_depth++;
                continue;
            }
            if(c == '}'){
                if(brace_depth)
                    brace_depth--;
                continue;
            }
            if(c == '['){
                bracket_depth++;
                continue;
            }
            if(c == ']'){
                if(bracket_depth)
                    bracket_depth--;
                continue;
            }
            if(c == ' ' && bracket_depth == 0 && brace_depth == 0){
                // Space outside of brackets/quotes - end of token
                break;
            }
        }
        token_len = p - token_start;

        StringView token = {.length = token_len, .text = token_start};

        // Check if this token matches any flag param
        _Bool matched_flag = 0;
        for(size_t i = 0; i < params->count; i++){
            const CmdParam* param = &params->params[i];
            if(param->kind != CMD_PARAM_FLAG) continue;

            if(SV_equals(token, param->names[0]) ||
               (param->names[1].text && SV_equals(token, param->names[1]))){
                // Matched this flag
                args->args[i].present = 1;
                args->args[i].content = token;
                matched_flag = 1;
                break;
            }
        }
        if(matched_flag && non_flag_count){
            StringView first = non_flag_tokens[0];
            StringView last = non_flag_tokens[non_flag_count-1];
            size_t total_len = (last.text + last.length) - first.text;
            StringView concatenated = {total_len, first.text};

            // Assign to string/path param(s)
            for(size_t i = 0; i < params->count; i++){
                if(args->args[i].present) continue;
                const CmdParam* param = &params->params[i];
                if(param->kind == CMD_PARAM_STRING || param->kind == CMD_PARAM_PATH){
                    args->args[i].present = 1;
                    args->args[i].content = concatenated;
                    break;
                }
            }
            non_flag_count = 0;
        }

        if(!matched_flag && non_flag_count < MAX_NON_FLAGS){
            // This is a non-flag token, save it for string/path args
            non_flag_tokens[non_flag_count++] = (StringView){token_len, token_start};
        }
    }

    if(non_flag_count){
        // Check the last token to see if it partially matches a flag
        StringView last = non_flag_tokens[non_flag_count-1];
        if(last.text + last.length != end){
            // Trailing space, consume a string/path param if possible
            // and then as all consumed.
            for(size_t i = 0; i < args->count; i++){
                CmdArg* a = &args->args[i];
                if(a->present) continue;
                const CmdParam* p = a->param;
                if(p->kind != CMD_PARAM_STRING && p->kind != CMD_PARAM_PATH) continue;
                a->present = 1;
                break;
            }
            goto all_consumed;
        }
        _Bool matched = 0;
        for(size_t i = 0; i < args->count; i++){
            CmdArg* a = &args->args[i];
            if(a->present) continue;
            const CmdParam* p = a->param;
            if(p->kind != CMD_PARAM_FLAG) continue;
            if(SV_starts_with(p->names[0], last) || SV_starts_with(p->names[1], last)){
                out->params[out->count++] = *p;
                matched = 1;
            }
        }
        if(matched){
            *completing_token = last;
            return 0;
        }
        // didn't partial match a flag, but we have tokens so we can only match strings.
        StringView first = non_flag_tokens[0];
        size_t total_len = (last.text + last.length) - first.text;
        StringView concatenated = {total_len, first.text};
        for(size_t i = 0; i < args->count; i++){
            CmdArg* a = &args->args[i];
            if(a->present) continue;
            const CmdParam* p = a->param;
            if(p->kind != CMD_PARAM_STRING && p->kind != CMD_PARAM_PATH) continue;
            out->params[out->count++] = *p;
        }
        *completing_token = concatenated;
        return 0;
    }
    all_consumed:;
    // All our tokens got consumed, so any param not used is valid.
    for(size_t i = 0; i < args->count; i++){
        CmdArg* a = &args->args[i];
        if(a->present) continue;
        const CmdParam* p = a->param;
        out->params[out->count++] = *p;
    }
    *completing_token = (StringView){0, end};
    return 0;
}
#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
