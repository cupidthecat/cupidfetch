#include "../../cupidfetch.h"

void get_linux_kernel() {
#ifdef _WIN32
    typedef LONG(WINAPI *rtl_get_version_fn)(PRTL_OSVERSIONINFOW);
    OSVERSIONINFOEXW osvi;
    memset(&osvi, 0, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);

    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        rtl_get_version_fn rtl_get_version = (rtl_get_version_fn)GetProcAddress(ntdll, "RtlGetVersion");
        if (rtl_get_version && rtl_get_version((PRTL_OSVERSIONINFOW)&osvi) == 0) {
            print_info("Linux Kernel", "Windows NT %lu.%lu (build %lu)", 20, 30,
                (unsigned long)osvi.dwMajorVersion,
                (unsigned long)osvi.dwMinorVersion,
                (unsigned long)osvi.dwBuildNumber);
            return;
        }
    }
    print_info("Linux Kernel", "Windows NT", 20, 30);
#else
    struct utsname uname_data;

    if (uname(&uname_data) != 0)
        cupid_log(LogType_ERROR, "couldn't get uname data");
    else
        print_info("Linux Kernel", uname_data.release, 20, 30);
#endif
}
