#pragma once

#include "FoxAst.hpp"
#include "FoxMPPagedArray.hpp"
#include "FoxTokenizer.hpp"
#include "FoxVar.hpp"

#include <vector>

#define FX_SCRIPT_VERSION_MAJOR 0
#define FX_SCRIPT_VERSION_MINOR 3
#define FX_SCRIPT_VERSION_PATCH 2


// class FoxVM;


// struct FoxExternalFunc
// {
//     // using FuncType = void (*)(FoxInterpreter& interpreter, std::vector<FoxValue>& params, FoxValue* return_value);

//     // using FuncType = void (*)(FoxVM* vm, std::vector<FoxValue>& params, FoxValue* return_value);

//     FoxHash HashedName = 0;
//     // FuncType Function = nullptr;

//     std::vector<FoxValue::ValueType> ParameterTypes;
//     bool IsVariadic = false;
// };
