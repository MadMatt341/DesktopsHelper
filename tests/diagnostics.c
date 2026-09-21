#include <windows.h>
#include <string.h>
#include "../src/diagnostics.h"
int main(int argc,char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    diagnostics_start();
    if(argc>1 && !strcmp(argv[1],"duplicate"))return diagnostics_finish("already-running",0);
    diagnostics_owner();
    if(argc>1 && !strcmp(argv[1],"kill"))TerminateProcess(GetCurrentProcess(),42);
    if(argc>1 && !strcmp(argv[1],"crash"))RaiseException(0xe0424242,EXCEPTION_NONCONTINUABLE,0,NULL);
    return diagnostics_finish("test-normal",0);
}
