#include "FoxScript.hpp"

#include "FoxLog.hpp"
#include "FoxScriptUtil.hpp"

#include <stdio.h>

using Token = FoxTokenizer::Token;
using TT = FoxTokenizer::TokenType;

FoxValue FoxValue::None {};
