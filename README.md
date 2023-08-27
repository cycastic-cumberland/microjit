# MicroJIT


MicroJIT is a lightweight Dynamic Compilation Library designed to enable developers
with efficient runtime code generation.

**This project is in the early stage of its lifetime so most features currently do not work, correctly or at all**

## Requirement

MicroJIT makes extensive use of variadic template and attributes, which require C++ 17 or above.
It has been tested with clang 15.0.7.

## Getting Started

To use MicroJIT, start by cloning this repo to your CMake project:

```shell
git clone --recurse-submodules https://github.com/cycastic-cumberland/microjit.git
```

Add these lines to your CMakeLists.txt:

```cmake
set(MICROJIT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/microjit)
set(ASMJIT_SOURCE_DIR ${MICROJIT_SOURCE_DIR}/asmjit)

add_subdirectory(${MICROJIT_SOURCE_DIR})
add_subdirectory(${ASMJIT_SOURCE_DIR})

include_directories(${ASMJIT_SOURCE_DIR}/src)
include_directories(${MICROJIT_SOURCE_DIR}/src)

target_link_libraries(your_project_name asmjit microjit)
```


## Motivation

The creation of MicroJIT stems from the need for compiler for my own programming language.
Due to its nature, the language must either parse or compile its bytecode at runtime, so I decided to look
into JIT compilers. Despite how hard I look, I could only find two libraries that fits my criteria:
NativeJIT and AsmJit. NativeJIT is made by the team behind Bing, but it has gone inactive for years now,
and I find its documentary lacking. AsmJit is an active project, is well documented and has many contributors,
but its APIs are not user-friendly, without the integration of a lot of C++ features.
That's why decided to create a library on top of AsmJit, which (will) make it easier for C++ developers
to dynamically generate codes. 

## Usage

Start by including `microjit/orchestrator.h`

The `MicroJITOrchestrator` class is the heart of MicroJIT, which compile and function_cache `FunctionInstance` objects. By default, it is atomically reference-counted, which allows it to be used concurrently and can be cleaned-up automatically, as long as there's no nested reference.

All other major components of MicroJIT are also reference counted (albeit not atomically), so no further memory management is needed.

A `FunctionInstance` is a utility class which manage and compile your functions. `FunctionInstance` support lazy compilation, the first time it is invoked, it will forward the compilation request to the Orchestrator, which will in turn return the compiled callback, provided by AsmJit. Any subsequence invocation will use the compiled callback.

The Orchestrator actually return an `InstanceWrapper` instead of the `FunctionInstance` itself, but you could use it by dereferencing the `InstanceWrapper`.

```c++
auto re1 = instance(12);  // This will compile the function and call it
auto re2 = instance(122); // This will call the compiled code immediately
```

To edit your function, first call `get_function()` from the `FunctionInstance` object.

```c++
auto function = instance->get_function();
```

The return value is of type `Ref<Function<R, Args...>>`.
This generic object store the argument list and (generic) main scope.
All function generic scopes are just wrapper for `RectifiedScope`,
which does not use template (at least at class level),
allowing IntelliSense to work without all template arguments filled.
Despite this, type checking still work thanks to the serialized type data provided by `Type`.

```c++
template<typename T>
static constexpr Type get_type_info(){
    return Type::create<T>();
}
```

All instructions are issued through `Scope<R, Args...>`. All variables can only be constructed inside the scope that owns it.

## Feature checklist

### Basic features

- [x] Lazy compilation
- [x] x86_64 support
- [x] Generic functions
- [x] Variables
- [x] Literals
- [x] Arguments
- [x] Return value
- [x] Orchestrator's basic functionalities
- [x] Construction/Copy construction/Destruction
- [x] Multiple scopes
- [x] Type casting/Coercion (Non-inline)
- [x] Expressions
- [x] Primitive Operations
- [x] Branches (if/else/while)
- [x] JIT compiled function call
- [x] Compile-and-go for JIT compiled function call
- [ ] Native function call
- [ ] Documentation

### Advanced features

- [ ] More optimizations
- [ ] (Overloaded) Operations
- [x] Asynchronous compilation
- [ ] Function dependencies analysis
- [ ] x86 and Windows support
- TBA...

## Targets

This library has been tested on the following target(s):

|        | gcc (Linux) | clang (Linux) | MSVC (Windows) | MinGW (Windows) |
|--------|:-----------:|:-------------:|:--------------:|-----------------|
| x86    |             |               |                |                 |
| x86_64 |   &#215;    |   &#10004;    |       *        |                 |

*: The example worked on Windows using MSVC, but I've yet to run the whole test suite

## License

See LICENSE.txt

## Acknowledgements

This project use the AsmJit library (See [LICENSE](https://github.com/asmjit/asmjit/blob/master/LICENSE.md))

## Example

```c++
#include <microjit/orchestrator.h>
#include <iostream>

int main(){
    auto orchestrator = microjit::orchestrator();
    auto instance = orchestrator->create_instance<int, int>();
    // Use this if you have a function as mold
    // std::function<int(int)> mold;
    // auto instance = orchestrator->create_instance_from_model(mold);
    auto main_scope = instance->get_function()->get_main_scope();
    // Create stack space for a var1
    auto var1 = main_scope->create_variable<int>();
    // Assign var1 as the value of the first argument
    main_scope->construct_from_argument(var1, 0);
    // Return var1
    main_scope->function_return(var1);
    // Compile and run the function
    auto re = instance(12);
    if (re == 12)
        std::cout << "It worked!\n";
    // instance is now compiled and any subsequence invocation will use the compiled function
    // To recompile:
    // instance->recompile();
    return 0;
}
```
