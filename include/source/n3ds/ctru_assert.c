#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// clang-format off
#include <3ds/types.h>
#include <3ds/services/cfgu.h>
#include <3ds/applets/error.h>
#include <3ds/errf.h>
// clang-format on

errorConf ctru_error;
char ctru_err_buf[1024];
int ctru_is_aborting = 0;

void __assert_func(const char* file, int line, const char* func,
                   const char* failedexpr) {
    sniprintf(ctru_err_buf, sizeof(ctru_err_buf),
              "assertion \"%s\" failed: file \"%s\", line %d%s%s\n", failedexpr,
              file, line, func ? ", function: " : "", func ? func : "");
    fputs(ctru_err_buf, stderr);

    {
        errorInit(&ctru_error, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
        errorText(&ctru_error, ctru_err_buf);
        errorDisp(&ctru_error);
    }

    ctru_is_aborting = 1;
    abort();
}

void __assert(const char* file, int line, const char* failedexpr) {
    __assert_func(file, line, NULL, failedexpr);
}

void abort(void) {
    if (!ctru_is_aborting) {
        errorInit(&ctru_error, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
        errorText(&ctru_error, "abort() called");
        errorDisp(&ctru_error);
    }

    // After showing errorDisp it seems sometimes the process is unable to
    // return to Homebrew Launcher properly. Terminate using ERRF instead.
    ERRF_ThrowResultWithMessage(-1, "abort() called");

    while (1) {
        _exit(1);
    }
}
