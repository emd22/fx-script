#include <cstdio>

#include <string>
#include <assert.h>

#include "FxScript.hpp"


int main()
{
	FxConfigScript script;
	script.LoadFile("Main.fxS");

	script.DefineExternalVar("playerid", "emd22", FxScriptValue(FxScriptValue::INT, 1020));

	FxScriptVM vm ;

	script.Execute(vm);

	std::string command = "";

	printf(
		"\nFoxtrot Script\nVersion %d.%d.%d\n",
		FX_SCRIPT_VERSION_MAJOR,
		FX_SCRIPT_VERSION_MINOR,
		FX_SCRIPT_VERSION_PATCH
	);


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

	return 0;
}
