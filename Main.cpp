#include "FoxScript.hpp"

#include <assert.h>

#include <cstdio>
#include <string>

int main()
{
    FoxConfigScript script;
    script.LoadFile("Main.fox");

    script.DefineExternalVar("playerid", "emd22", FoxValue(FoxValue::INT, 1020));

    FoxVM vm;

    script.Execute(vm);

    std::string command = "";

    printf("\nFoxtrot Script\nVersion %d.%d.%d\n", FX_SCRIPT_VERSION_MAJOR, FX_SCRIPT_VERSION_MINOR, FX_SCRIPT_VERSION_PATCH);

    // while (true) {
    // 	printf(" -> ");
    // 	fflush(stdout);

    // 	std::getline(std::cin, command);

    // 	if (command == "quit") {
    // 		break;
    // 	}

    // 	if (!command.ends_with(';')) {
    // 		command += ';';
    // 	}

    // 	//printf("Executing command {%s}\n", command.c_str());

    // 	script.ExecuteUserCommand(command.c_str(), interpreter);
    // }

#ifdef _WIN32
    system("pause");
#endif

    return 0;
}
