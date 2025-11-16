#ifndef CMD_PARSE_H
#define CMD_PARSE_H
#include <stddef.h>
#include "../long_string.h"
#ifdef __clang__
#pragma clang assume_nonnull begin
#endif

#ifndef CMD_PARSE_WARN_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define CMD_PARSE_WARN_UNUSED __attribute__((warn_unused_result))
#elif defined(_MSC_VER)
#if __cplusplus >= 201703L || (defined(_HAS_CXX17) && _HAS_CXX17)
#define CMD_PARSE_WARN_UNUSED [[nodiscard]]
#else
#define CMD_PARSE_WARN_UNUSED __checkReturn
#endif
#else
#define CMD_PARSE_WARN_UNUSED
#endif

#endif

enum CmdParamKind {
    CMD_PARAM_FLAG,
    CMD_PARAM_PATH,
    CMD_PARAM_STRING,
};

typedef struct CmdParam CmdParam;
struct CmdParam {
    StringView names[2];
    enum CmdParamKind kind;
    _Bool optional;
};

typedef struct CmdParams CmdParams;
struct CmdParams {
    CmdParam params[8];
    size_t count;
};

//
// int cmd_param_parse_signature(StringView sig, CmdParams* params)
// ----------------------------------------------------------------
// Parses the example string into a structured param.
//
// Returns 0 on success. Non-zero on error.
//
// In general:
//  The command name (first token) is skipped.
//  Strings and paths are surrounded with <>.
//  String vs path is decided by if the name is "file" or "dir".
//  | indicates an alternative name. We currently support two. This is used for mutually exclusive options.
//  If it's not a string or path, it's a flag.
//  [] indicates the param is optional.
//
// Examples:
//    This string:
//      :open [--braceless] <file>
//    will parse into:
//      CmdParams params = {
//          .params = {
//              {
//                  .names = {SV("--braceless")},
//                  .kind  = CMD_PARAM_FLAG,
//                  .optional = 1,
//              },
//              {
//                  .names = {SV("file")},
//                  .kind = CMD_PARAM_PATH,
//                  .optional = 0,
//              },
//          },
//          .count = 2,
//      };
//
//    This string:
//      :sort [<query>] [keys|values] [asc|desc]
//    will parse into:
//      CmdParams params = {
//          .params = {
//              {
//                  .names = {SV("query")},
//                  .kind  = CMD_PARAM_STRING,
//                  .optional = 1,
//              },
//              {
//                  .names = {SV("keys"), SV("values")},
//                  .kind  = CMD_PARAM_FLAG,
//                  .optional = 1,
//              },
//              {
//                  .names = {SV("asc"), SV("desc")},
//                  .kind  = CMD_PARAM_FLAG,
//                  .optional = 1,
//              },
//          },
//          .count = 3,
//      };

CMD_PARSE_WARN_UNUSED
static
int
cmd_param_parse_signature(StringView sig, CmdParams* params);

typedef struct CmdArg CmdArg;
struct CmdArg {
    const CmdParam* param;
    _Bool present; // Whether this arg was present in the commandline.
    _Bool consumed; // For arg retrieval, whether we've already consumed this arg.
    StringView content; // The string that matched this arg.
};

typedef struct CmdArgs CmdArgs;
struct CmdArgs {
    CmdArg args[8];
    size_t count;
};

//
// int cmd_param_parse_args(StringView cmd_line, const CmdParams* params, CmdArgs* args);
// -------------------------------------------------------------------------------------
// Parses a command line into args
//
// Returns 0 on success, nonzero on error.
//
CMD_PARSE_WARN_UNUSED
static
int
cmd_param_parse_args(StringView cmd_line, const CmdParams* params, CmdArgs* args);

enum {
    CMD_ARG_ERROR_NONE = 0,
    CMD_ARG_ERROR_MISSING_BUT_OPTIONAL = 1,

    // These shouldn't happen if signature and cmd retrieval agree, but just in case.
    //
    // Mandatory argument is missing.
    CMD_ARG_ERROR_MISSING = 2,
    // Argument was retrieved with the wrong type.
    CMD_ARG_ERROR_TYPE_ERROR = 3,
    // Argument doesn't match any params
    CMD_ARG_ERROR_MISSING_PARAM = 4,
};

//
// Retrieves a flag of the given name.
// Returns one of the CMD_ARG_ERROR_* error codes.
//
CMD_PARSE_WARN_UNUSED
static
int
cmd_get_arg_bool(CmdArgs* args, StringView name, _Bool* out);

//
// Retrieves a string or path of the given name.
// Returns one of the CMD_ARG_ERROR_* error codes.
//
CMD_PARSE_WARN_UNUSED
static
int
cmd_get_arg_string(CmdArgs* args, StringView name, StringView* out);


//
// Parses the command line and and outputs the params we could be completing.
// cmd_line should include the command.
//
// Returns 0 on success and nonzero on error.
CMD_PARSE_WARN_UNUSED
static
int
cmd_get_completion_params(StringView cmd_line, const CmdParams* restrict params, CmdParams* restrict out, StringView* completing_token);



#ifdef __clang__
#pragma clang assume_nonnull end
#endif
#endif
