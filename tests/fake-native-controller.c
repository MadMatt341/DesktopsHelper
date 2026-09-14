#include <windows.h>
// Deliberately stuck launcher, used only inside an isolated test directory.
// The helper must terminate this child through its deadline/owner job.
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE previous,PWSTR command,int show) {
    (void)instance;(void)previous;(void)command;(void)show;
    Sleep(60000);
    return 0;
}
