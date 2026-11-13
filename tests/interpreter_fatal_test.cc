// A script error raised while no script is running (for example a missing
// .int file during map setup) must not crash. It used to longjmp into a null
// `Program`, because the compiler dropped the null check in
// programFatalError().

#include "interpreter.h"

#include <stdio.h>

using namespace fallout;

int main()
{
    Program* program = programCreateByPath("scripts/this_script_does_not_exist.int");
    if (program != nullptr) {
        fprintf(stderr, "expected nullptr for a missing script\n");
        return 1;
    }

    puts("ok");
    return 0;
}
