#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include <cxxabi.h>

extern "C" {
// clang-format off
#include <3ds/types.h>
#include <3ds/services/cfgu.h>
#include <3ds/applets/error.h>
// clang-format on
}

extern "C" {
extern errorConf ctru_error;
extern char ctru_err_buf[1024];
extern int ctru_is_aborting;
}

static void mystrcat_s(char* dst, size_t sz, const char* src) {
    char* start = dst + strnlen(dst, sz);
    char* end = dst + sz;
    if (start >= end) {
        return;
    }
    size_t srclen = strlen(src);
    if (start + srclen < end) {
        strcpy(start, src);
    } else {
        strncpy(start, src, end - start - 1);
        *(end - 1) = '\0';
    }
}

static void terminate_handler() noexcept;

static std::terminate_handler prev_terminate_handler =
    std::set_terminate(terminate_handler);

static void terminate_handler() noexcept {
    static bool terminating = false;
    if (terminating) {
        mystrcat_s(ctru_err_buf, sizeof(ctru_err_buf),
                   "Terminate called recursively\n");
        fputs(ctru_err_buf, stderr);
        {
            errorInit(&ctru_error, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
            errorText(&ctru_error, ctru_err_buf);
            errorDisp(&ctru_error);
        }

        ctru_is_aborting = 1;
        abort();
    }
    terminating = true;

    std::type_info* t = abi::__cxa_current_exception_type();
    if (t) {
        const char* name_mangled = t->name();
        int status;
        char* demangled =
            abi::__cxa_demangle(name_mangled, nullptr, nullptr, &status);
        const char* name = name_mangled;
        if (demangled && status == 0) {
            name = demangled;
        }
        sniprintf(ctru_err_buf, sizeof(ctru_err_buf),
                  "Terminate called after throwing an instance of '%s'\n",
                  name);
        if (demangled && status == 0) {
            free(demangled);
            demangled = nullptr;
        }

#ifdef __cpp_exceptions
        try {
            std::rethrow_exception(std::current_exception());
        } catch (const std::exception& ex) {
            mystrcat_s(ctru_err_buf, sizeof(ctru_err_buf), "  what():  ");
            mystrcat_s(ctru_err_buf, sizeof(ctru_err_buf), ex.what());
            mystrcat_s(ctru_err_buf, sizeof(ctru_err_buf), "\n");
        } catch (...) {
        }
#else
#warning cannot print exception details with exception handling disabled, use -fexceptions to enable
#endif
    } else {
        mystrcat_s(ctru_err_buf, sizeof(ctru_err_buf),
                   "Terminate called without an active exception\n");
    }

    fputs(ctru_err_buf, stderr);
    {
        errorInit(&ctru_error, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
        errorText(&ctru_error, ctru_err_buf);
        errorDisp(&ctru_error);
    }

    ctru_is_aborting = 1;
    if (prev_terminate_handler) {
        prev_terminate_handler();
    }

    abort();
}
