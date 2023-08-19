
#                         type  bit_count
#                          ^     ^
PRIMITIVE_LIST: list[tuple[int, int]] = [
    (0, 8),
    (0, 16),
    (0, 32),
    (0, 64),
    (1, 8),
    (1, 16),
    (1, 32),
    (1, 64),
    (2, 32),
    (2, 64)
]

INDENT = 4

TEMPLATE_TEXT = '''
// This file is automatically generated

#ifndef MICROJIT_X86_64_PRIMITIVE_CONVERTER_H
#define MICROJIT_X86_64_PRIMITIVE_CONVERTER_H

#include "jit.h"
#include "primitive_conversion_map.gen.h"

namespace microjit {{
    class x86_64PrimitiveConverter {{
    private:
        typedef MicroJITCompiler::Assembly Assembly;
        typedef Box<asmjit::x86::Assembler> Assembler;

        typedef void (*conversion_handler)(Assembler& p_assembler, const int64_t& p_vstack_loc, const int64_t& p_from_vstack_offset, const int64_t& p_to_vstack_offset);

        // Converters
{}
        // Converter getters
{}
    public:
        template <typename From, typename To>
        static conversion_handler get_converter() {{
            static constexpr void (*f)(const From*, To*) = nullptr;
            return get_converter(f);
        }}
    }};
}}


#endif //MICROJIT_X86_64_PRIMITIVE_CONVERTER_H
'''

CONVERSION_HANDLER_NAME = "convert_{}_to_{}"

def get_type_name(raw: tuple[int, int]) -> str:
    (type, size) = raw
    if type == 0:
        return f"uint{size}_t"
    if type == 1:
        return f"int{size}_t"
    if size == 32:
        return "float"
    if size == 64:
        return "double"

def generate_getter(from_: tuple[int, int], to_: tuple[int, int]):
    (f_name, t_name) = (get_type_name(from_), get_type_name(to_))
    return f"static conversion_handler get_converter(void (*)(const {f_name}*, {t_name}*)) {{ return {CONVERSION_HANDLER_NAME.format(f_name, t_name)}; }}"

def select_register(type: int, size: int, dis: int):
    reg = ""
    if type == 0 or type == 1:
        id = ""
        if dis == 0:
            id = "a"
        elif dis == 1:
            id = "b"
        else:
            id = "c"
        if size == 8:
            reg = f"{id}l"
        elif size == 16:
            reg = f"{id}x"
        elif size == 32:
            reg = f"e{id}x"
        elif size == 64:
            reg = f"r{id}x"
    else:
        if dis == 0:
            reg = "xmm0"
        elif dis == 1:
            reg = "xmm1"
        else:
            reg = "xmm2"
        pass
    return f"asmjit::x86::{reg}"

def get_ptr(size: int) -> str:
    match size:
        case 8:
            return "asmjit::x86::byte_ptr"
        case 16:
            return "asmjit::x86::word_ptr"
        case 32:
            return "asmjit::x86::dword_ptr"
        case 64:
            return "asmjit::x86::qword_ptr"

def handle_convert(from_type: tuple[int, int], to_type: tuple[int, int]):
    scope = 3
    re = ""

    (f_type, f_size) = from_type
    (t_type, t_size) = to_type

    from_reg = select_register(f_type, f_size, 0)
    to_reg = select_register(t_type, t_size, 1)

    vstack = f"asmjit::x86::qword_ptr(asmjit::x86::rbp, int32_t(p_vstack_loc))"
    re += (" " * INDENT * scope) + "//Load from address into rdi\n"
    re += (" " * INDENT * scope) + f"p_assembler->mov(asmjit::x86::rdi, {vstack});\n"
    re += (" " * INDENT * scope) + f"p_assembler->mov(asmjit::x86::rsi, asmjit::x86::rdi);\n"
    re += (" " * INDENT * scope) + f"p_assembler->sub(asmjit::x86::rdi, std::abs(p_from_vstack_offset));\n"
    re += (" " * INDENT * scope) + "//Load to address into rsi\n"
    re += (" " * INDENT * scope) + f"p_assembler->sub(asmjit::x86::rsi, std::abs(p_to_vstack_offset));\n"
    f_ptr_type = get_ptr(f_size)
    re += (" " * INDENT * scope) + "//Move from value into a register\n"
    if f_type == 0 or f_type == 1:
        re += (" " * INDENT * scope) + f"p_assembler->mov({from_reg}, {f_ptr_type}(asmjit::x86::rdi));\n"
    else:
        if f_size == 32:
            re += (" " * INDENT * scope) + f"p_assembler->movss({from_reg}, {f_ptr_type}(asmjit::x86::rdi));\n"
        else:
            re += (" " * INDENT * scope) + f"p_assembler->movsd({from_reg}, {f_ptr_type}(asmjit::x86::rdi));\n"


    t_ptr_type = get_ptr(t_size)
    # Buffer space, with type of from and size of to
    buffer_reg = select_register(f_type, t_size, 2)
    # If from is integer
    if f_type == 0 or f_type == 1:
        if t_type == 0 or t_type == 1:
            re += (" " * INDENT * scope) + f"p_assembler->mov({to_reg}, {from_reg});\n"
            re += (" " * INDENT * scope) + f"p_assembler->mov({t_ptr_type}(asmjit::x86::rsi), {to_reg});\n"
        # This is currently too much for me to handle
        else:
            from_name = get_type_name(from_type)
            to_name = get_type_name(to_type)
            re += (" " * INDENT * scope) + f"void (*f)(const {from_name}*, {to_name}*);\n"
            re += (" " * INDENT * scope) + f"f = PrimitiveConversionHelper::conversion_candidate;\n"
            re += (" " * INDENT * scope) + f"p_assembler->call(f);\n"
    # If from is floating point
    else:
        if t_type == 2:
            # Convert from float to double
            if t_size > f_size:
                re += (" " * INDENT * scope) + f"p_assembler->cvtss2sd({from_reg}, {from_reg});\n"
                re += (" " * INDENT * scope) + f"p_assembler->movsd({to_reg}, {from_reg});\n"
                re += (" " * INDENT * scope) + f"p_assembler->movsd({t_ptr_type}(asmjit::x86::rsi), {to_reg});\n"
            # Convert from double to float
            else:
                re += (" " * INDENT * scope) + f"p_assembler->cvtsd2ss({from_reg}, {from_reg});\n"
                re += (" " * INDENT * scope) + f"p_assembler->movss({to_reg}, {from_reg});\n"
                re += (" " * INDENT * scope) + f"p_assembler->movss({t_ptr_type}(asmjit::x86::rsi), {to_reg});\n"
        else:
            from_name = get_type_name(from_type)
            to_name = get_type_name(to_type)
            re += (" " * INDENT * scope) + f"void (*f)(const {from_name}*, {to_name}*) = PrimitiveConversionHelper::conversion_candidate;\n"
            re += (" " * INDENT * scope) + f"p_assembler->call(f);\n"

    re += "\n"
    return re

def populate_converters() -> str:
    n = len(PRIMITIVE_LIST)
    re = ""
    scope = 2
    for i in range(n):
        for j in range(n):
            if i == j: continue
            re += ((" " * INDENT * scope) + "static void " + CONVERSION_HANDLER_NAME.format(get_type_name(PRIMITIVE_LIST[i]), get_type_name(PRIMITIVE_LIST[j]))
                   + "(Assembler& p_assembler, const int64_t& p_vstack_loc, const int64_t& p_from_vstack_offset, const int64_t& p_to_vstack_offset) "+ "{\n")
            re += handle_convert(PRIMITIVE_LIST[i], PRIMITIVE_LIST[j])
            re += (" " * INDENT * scope) + "}\n"
    return re

def populate_getters() -> str:
    n = len(PRIMITIVE_LIST)
    re = ""
    scope = 2
    for i in range(n):
        for j in range(n):
            if i == j: continue
            re += (" " * INDENT * scope) + generate_getter(PRIMITIVE_LIST[i], PRIMITIVE_LIST[j]) + "\n"
    return re

TARGET_FILE = "primitive_converter.gen.h"

import sys
import os

(dirname, filename) = os.path.split(sys.argv[0])

full_target_path = os.path.join(dirname, TARGET_FILE)

with open(full_target_path, "wt") as f:
    f.write(TEMPLATE_TEXT.format(populate_converters(), populate_getters()))