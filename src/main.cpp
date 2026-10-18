#include "application.h"
#include <shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int count{};
    LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments)
        return 1;
    AppOptions options;
    for (int i = 1; i < count; ++i) {
        if (wcscmp(arguments[i], L"--background") == 0)
            options.background = true;
        else if (wcscmp(arguments[i], L"--preview-menu") == 0)
            options.preview = true;
        else if (wcscmp(arguments[i], L"--dry-run") == 0)
            options.dryRun = true;
        else if (wcscmp(arguments[i], L"--self-test") == 0)
            options.selfTest = true;
    }
    LocalFree(arguments);
    Application app(options);
    return app.Run(instance);
}
