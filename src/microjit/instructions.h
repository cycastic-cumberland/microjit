//
// Created by cycastic on 8/15/23.
//

#ifndef MICROJIT_INSTRUCTIONS_H
#define MICROJIT_INSTRUCTIONS_H

#include <typeinfo>
#include <typeindex>
#include <utility>
#include <vector>
#include <functional>
#include <list>
#include <unordered_map>
#include <string>
#include <cstring>

#include "helper.h"
#include "virtual_stack.h"
#include "utils.h"
#include "primitive_conversion_map.gen.h"

namespace microjit {
    class ObjectTools {
    public:
        template<class T>
        static void ctor(T* p_obj) { new (p_obj) T(); }
        template<class T>
        static void copy_ctor(T* p_obj, const T* p_copy_target) { new (p_obj) T(*p_copy_target); }
        template<class T>
        static void dtor(T* p_obj) { p_obj->~T(); }
        template<class T>
        static void assign(T* p_obj, const T* copy_target) { *p_obj = *copy_target; }
        template<typename From, typename To>
        static void convert(const From* p_from, To* p_to){ *p_to = To(*p_from); }
        template<typename From, typename To>
        static void coerce(const From* p_from, To* p_to){ *p_to = *(To*)p_from; }

        static void empty_ctor(void*) {  }
        static void empty_copy_ctor(void*, void*) {  }
        static void empty_dtor(void*) {  }

        template<typename T>
        static void add(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left + *p_right; }
        template<typename T>
        static void sub(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left - *p_right; }
        template<typename T>
        static void mul(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left * *p_right; }
        template<typename T>
        static void div(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left / *p_right; }
        template<typename T>
        static void mod(T* p_re, const T* p_left, const T* p_right) { *p_re = *p_left % *p_right; }
    };
    struct Type {
        const std::type_info *type_info;
        const size_t size;
        const void* copy_constructor;
        const void* destructor;
        const bool is_primitive;
    private:
        TYPE_CONSTEXPR Type(const std::type_info* p_info,
                            const size_t& p_size,
                            const void* p_cc,
                            const void* p_dtor,
                            const bool& p_fundamental)
                       : type_info(p_info), size(p_size),
                         copy_constructor(p_cc), destructor(p_dtor), is_primitive(p_fundamental) {}

        static TYPE_CONSTEXPR Type create_internal(void (*)()){
            TYPE_CONSTEXPR auto copy_ctor = (const void*)ObjectTools::empty_copy_ctor;
            TYPE_CONSTEXPR auto dtor = (const void*)ObjectTools::empty_dtor;
            return { &typeid(void), 0, copy_ctor, dtor, true };
        }

        template<typename T>
        static TYPE_CONSTEXPR Type create_internal(T (*)()){
            TYPE_CONSTEXPR auto is_trivially_destructible  = std::is_trivially_destructible_v<T>;

            TYPE_CONSTEXPR auto copy_ctor = (const void*)ObjectTools::copy_ctor<T>;
            TYPE_CONSTEXPR auto dtor = (is_trivially_destructible ?
                                        (const void*)ObjectTools::empty_dtor :
                                        (const void*)ObjectTools::dtor<T>);
            TYPE_CONSTEXPR auto fundamental = std::is_fundamental_v<T>;
            return { &typeid(T), sizeof(T), copy_ctor, dtor, fundamental };
        }

    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ TYPE_CONSTEXPR bool operator==(const Type& p_right) const {
            return type_info == p_right.type_info;
        }
        _NO_DISCARD_ _ALWAYS_INLINE_ TYPE_CONSTEXPR bool operator!=(const Type& p_right) const {
            return type_info != p_right.type_info;
        }

        template<typename T>
        static TYPE_CONSTEXPR Type create(){
            // A little cheat for void type
            TYPE_CONSTEXPR T (*f)() = nullptr;
            return create_internal(f);
        }
        static bool is_floating_point(Type p_type) {
            TYPE_CONSTEXPR auto float_type = Type::create<float>();
            TYPE_CONSTEXPR auto double_type = Type::create<double>();
            return (p_type == float_type || p_type == double_type);
        }
        static bool is_signed_integer(Type p_type) {
            TYPE_CONSTEXPR auto i8 = Type::create<int8_t>();
            TYPE_CONSTEXPR auto i16 = Type::create<int16_t>();
            TYPE_CONSTEXPR auto i32 = Type::create<int32_t>();
            TYPE_CONSTEXPR auto i64 = Type::create<int64_t>();
            return (p_type == i8) ||
                   (p_type == i16) ||
                   (p_type == i32) ||
                   (p_type == i64);
        }
    };
    template <typename R, typename ...Args> class Function;
    template <typename R, typename ...Args> class Scope;

    class RectifiedScope;
    class RectifiedFunction;

    class Instruction : public ThreadUnsafeObject{
    public:
        enum InstructionType {
            IT_NONE,
            IT_DECLARE_VARIABLE,
            IT_CONSTRUCT,
            IT_COPY_CONSTRUCT,
            IT_ASSIGN,
            IT_RETURN,
            IT_SCOPE_CREATE,
            IT_CONVERT,
            IT_PRIMITIVE_CONVERT,
            IT_INVOKE_JIT,
            IT_INVOKE_NATIVE,
            IT_BRANCH,
            IT_BREAK,
        };
    private:
        const InstructionType type;
        
        uint32_t scope_offset{};
        friend class RectifiedScope;
    protected:
        explicit TYPE_CONSTEXPR Instruction(InstructionType p_type) : type(p_type) {}
    public:
        _ALWAYS_INLINE_ InstructionType get_instruction_type() const { return type; }
        _ALWAYS_INLINE_ uint32_t get_scope_offset() const { return scope_offset; }
    };

    class Value : public ThreadUnsafeObject {
    public:
        enum ValueType {
            VAL_IMMEDIATE,
            VAL_ARGUMENT,
            VAL_VARIABLE,
            VAL_EXPRESSION,
        };
    private:
        const ValueType type;
    protected:
        explicit Value(const ValueType& p_type) : type(p_type) {}
    public:
        ValueType get_value_type() const { return type; }
    };

    class ArgumentsDeclaration : public ThreadUnsafeObject {
    private:
        const std::vector<Type> arg_types;
        const std::vector<size_t> offsets;
    private:
        ArgumentsDeclaration(std::vector<Type>& p_types, std::vector<size_t>& p_offsets)
            : arg_types(std::move(p_types)), offsets(std::move(p_offsets)) {}
        template<typename T>
        static void add_arg(std::vector<Type>* p_vec) {
            static TYPE_CONSTEXPR auto void_type = Type::create<void>();
            static TYPE_CONSTEXPR auto curr_type = Type::create<T>();
            TYPE_ASSERT(void_type != curr_type);
            p_vec->push_back(curr_type);
        }
        static void create_indexes(const std::vector<Type>* p_types, std::vector<size_t>* p_offsets){
            p_offsets->resize(p_types->size());
            size_t current_offset = 0;
            // The stack growth downward
            for (int64_t i = int64_t(p_offsets->size()) - 1; i >= 0; i--){
                p_offsets->operator[](i) = current_offset;
                current_offset += p_types->operator[](i).size;
            }
        }
    public:
        _NO_DISCARD_ _ALWAYS_INLINE_ const std::vector<Type>& argument_types() const { return arg_types; }
        _NO_DISCARD_ _ALWAYS_INLINE_ const std::vector<size_t>& argument_offsets() const { return offsets; }

        template<typename ...Args>
        static Ref<ArgumentsDeclaration> create(){
            std::vector<Type> p_types;
            std::vector<size_t> p_offsets;
            p_types.reserve(sizeof...(Args));
            p_offsets.reserve(sizeof...(Args));
            (add_arg<Args>(&p_types), ...);
            create_indexes(&p_types, &p_offsets);
            auto args = new ArgumentsDeclaration(p_types, p_offsets);
            return Ref<ArgumentsDeclaration>::from_uninitialized_object(args);
        }
    };

    class VariableValue;

    class VariableInstruction : public Instruction {
    public:
        enum Property : uint32_t {
            NONE = 0,
            CONST = 1,
        };
        const RectifiedScope* parent_scope;
        const Type type;
        const uint32_t properties;
    private:
        TYPE_CONSTEXPR VariableInstruction(const RectifiedScope* p_scope, Type p_type, const uint32_t& p_props)
            : Instruction(IT_DECLARE_VARIABLE),
              parent_scope(p_scope), type(p_type), properties(p_props) {}
    public:
        template<class T>
        static Ref<VariableInstruction> create(const RectifiedScope* p_scope, const uint32_t& p_props){
            return Ref<VariableInstruction>::from_uninitialized_object(
                    new VariableInstruction(p_scope, Type::create<T>(), p_props));
        }
        Ref<VariableValue> value_reference() const;

    };

    class ImmediateValue : public Value {
    public:
        void* data;
        const Type imm_type;
    private:
        ImmediateValue(void* p_data, Type&& p_type) : Value(VAL_IMMEDIATE), data(p_data), imm_type(p_type) {}
    public:
        ~ImmediateValue() override {
            auto dtor = (void (*)(void*))imm_type.destructor;
            dtor(data);
            free(data);
        }
        template<typename T>
        static Ref<ImmediateValue> create(const T& p_value){
            auto type = Type::create<T>();
            auto data = malloc(type.size);
            auto cc = (void (*)(void*, const void*))type.copy_constructor;
            cc(data, &p_value);
            auto ins = new ImmediateValue(data, std::move(type));
            return Ref<ImmediateValue>::from_uninitialized_object(ins);
        }
    };

    class ArgumentValue : public Value {
    public:
        const uint32_t& argument_index;
    private:
        explicit ArgumentValue(const uint32_t& p_idx) : Value(VAL_ARGUMENT), argument_index(p_idx) {}
    public:
        static Ref<ArgumentValue> create(const uint32_t& p_idx);
    };

    class VariableValue : public Value {
    public:
        const Ref<VariableInstruction> variable;
    private:
        explicit VariableValue(const Ref<VariableInstruction>& p_var) : Value(VAL_VARIABLE), variable(p_var) {}
    public:
        static Ref<VariableValue> create(const Ref<VariableInstruction>& p_var);
    };

    class ConstructInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const void* ctor;
    private:
        ConstructInstruction(const Ref<VariableInstruction>& p_target, const void* p_ctor)
            : Instruction(IT_CONSTRUCT), target_variable(p_target), ctor(p_ctor){}
    public:
        template<typename T>
        static Ref<ConstructInstruction> create_unsafe(const Ref<VariableInstruction>& p_target){
            // Does not check for type, just need to have a default constructor
            auto ins = new ConstructInstruction(p_target, (const void*)ObjectTools::ctor<T>);
            return Ref<ConstructInstruction>::from_uninitialized_object(ins);
        }
        template<typename T>
        static Ref<ConstructInstruction> create(const Ref<VariableInstruction>& p_target) {
            static TYPE_CONSTEXPR auto type = Type::create<T>();
            if (type != p_target->type) MJ_RAISE("Mismatched type");
            return create_unsafe<T>(p_target);
        }
    };

    class ReturnInstruction : public Instruction {
    public:
        // null for void functions
        const Ref<VariableInstruction> return_var;
    private:
        explicit ReturnInstruction(const Ref<VariableInstruction>& p_ret)
            : Instruction(IT_RETURN), return_var(p_ret) {}
    public:
        static Ref<ReturnInstruction> create(const Ref<VariableInstruction>& p_ret){
            auto re = new ReturnInstruction(p_ret);
            return Ref<ReturnInstruction>::from_uninitialized_object(re);
        }
    };

    class ScopeCreateInstruction : public Instruction {
    public:
        const Ref<RectifiedScope> scope;
        explicit ScopeCreateInstruction(const Ref<RectifiedScope>& p_scope)
            : Instruction(IT_SCOPE_CREATE), scope(p_scope) {}
    };
    class ConvertInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> from_var;
        const Ref<VariableInstruction> to_var;
        const void* converter;
    private:
        ConvertInstruction(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to, const void* p_conv)
            : Instruction(IT_CONVERT), from_var(p_from), to_var(p_to), converter(p_conv) {}
    public:
        template<typename From, typename To>
        static Ref<ConvertInstruction> create(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to, bool p_coerce){
            auto converter = p_coerce ?  ObjectTools::coerce<From, To> : ObjectTools::convert<From, To>;
            auto ins = new ConvertInstruction(p_from, p_to, (const void*)converter);
            return Ref<ConvertInstruction>::from_uninitialized_object(ins);
        }
    };
    class PrimitiveConvertInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> from_var;
        const Ref<VariableInstruction> to_var;
        // This will only act as key
        const void* converter;
    private:
        PrimitiveConvertInstruction(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to, const void* p_conv)
                : Instruction(IT_PRIMITIVE_CONVERT), from_var(p_from), to_var(p_to), converter(p_conv) {}
    public:
        template<typename From, typename To>
        static Ref<PrimitiveConvertInstruction> create(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to){
            void (*converter)(const From*, To*);
            PrimitiveConversionHelper::convert(&converter);
            auto ins = new PrimitiveConvertInstruction(p_from, p_to, (const void*)converter);
            return Ref<PrimitiveConvertInstruction>::from_uninitialized_object(ins);
        }
    };
    class ArgumentsVector : public ThreadUnsafeObject {
    public:
        std::vector<Ref<Value>> values;
        ArgumentsVector() : values() {  }
    };
    class InvokeJitInstruction : public Instruction {
    public:
        const Ref<RectifiedFunction> target_function;
        const Ref<VariableInstruction> return_variable;
        const Ref<ArgumentsVector> passed_arguments;
        const size_t arguments_total_size{};
    private:
        InvokeJitInstruction(const Ref<RectifiedFunction>& p_func, const Ref<ArgumentsVector>& p_args,
                             const Ref<VariableInstruction>& p_ret_var, const size_t& p_total_size);
    public:
        static Ref<InvokeJitInstruction> create(const Ref<ArgumentsDeclaration>& p_parent_args,
                                                const Ref<RectifiedFunction>& p_func,
                                                const Ref<ArgumentsVector>& p_args,
                                                const Ref<VariableInstruction>& p_ret_var);
    };

    class ExpressionValue;

    class AbstractOperation : public Value {
    public:
        enum OperationType {
            BINARY_ADD,
            BINARY_SUB,
            BINARY_MUL,
            BINARY_DIV,
            BINARY_MOD,

            BINARY_EQUAL,
            BINARY_NOT_EQUAL,
            BINARY_GREATER,
            BINARY_GREATER_OR_EQUAL,
            BINARY_LESSER,
            BINARY_LESSER_OR_EQUAL,

            BINARY_END,

            UNARY_COMPOUND_ADD,
            UNARY_COMPOUND_SUB,
            UNARY_COMPOUND_MUL,
            UNARY_COMPOUND_DIV,
            UNARY_COMPOUND_MOD,

            OP_END,
        };
        enum OperationMask : uint32_t {
            RET_SAME_TYPE = 1,
            RET_BOOLEAN = 2,
            RET_NONE = 4,
            READ_ONLY = 8,
        };
        static constexpr uint32_t operation_masks[OP_END] {
                RET_SAME_TYPE | READ_ONLY,
                RET_SAME_TYPE | READ_ONLY,
                RET_SAME_TYPE | READ_ONLY,
                RET_SAME_TYPE | READ_ONLY,
                RET_SAME_TYPE | READ_ONLY,

                RET_BOOLEAN | READ_ONLY,
                RET_BOOLEAN | READ_ONLY,
                RET_BOOLEAN | READ_ONLY,
                RET_BOOLEAN | READ_ONLY,
                RET_BOOLEAN | READ_ONLY,
                RET_BOOLEAN | READ_ONLY,

                RET_NONE, // BINARY_END

                RET_NONE,
                RET_NONE,
                RET_NONE,
                RET_NONE,
                RET_NONE,
        };
        const OperationType operation_type;
    protected:
        explicit AbstractOperation(OperationType p_type)
            : Value(VAL_EXPRESSION), operation_type(p_type) {}
    public:
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool is_binary(OperationType p_op) {
            return p_op < BINARY_END;
        }
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool is_unary(OperationType p_op) {
            return p_op > BINARY_END && p_op < OP_END;
        }
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool is_operation_read_only(OperationType p_op) {
            return  (operation_masks[p_op] & READ_ONLY);
        }
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool operation_does_return(OperationType p_op) {
            return !(operation_masks[p_op] & RET_NONE);
        }
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool operation_return_same(OperationType p_op) {
            return  (operation_masks[p_op] & RET_SAME_TYPE);
        }
        _NO_DISCARD_ static _ALWAYS_INLINE_ bool operation_return_bool(OperationType p_op) {
            return  (operation_masks[p_op] & RET_BOOLEAN);
        }
    };

    class BinaryOperation : public AbstractOperation {
    public:
        const Ref<Value> left_operand;
        const Ref<Value> right_operand;
        const bool is_primitive;
    protected:
        BinaryOperation(OperationType p_op,
                        const Ref<Value>& p_left,
                        const Ref<Value>& p_right,
                        const bool& p_primitive)
                        : AbstractOperation(p_op),
                          left_operand(p_left),
                          right_operand(p_right),
                          is_primitive(p_primitive) {}
    };

    class PrimitiveBinaryOperation : public BinaryOperation {
    private:
        PrimitiveBinaryOperation(OperationType p_op,
                                 const Ref<Value>& p_left,
                                 const Ref<Value>& p_right)
                                 : BinaryOperation(p_op, p_left, p_right, true) {}
    public:
        static Ref<PrimitiveBinaryOperation> create(OperationType p_op,
                                                    const Ref<Value>& p_left,
                                                    const Ref<Value>& p_right);
        static _ALWAYS_INLINE_ Ref<PrimitiveBinaryOperation> add(const Ref<Value>& p_left, const Ref<Value>& p_right){
            return create(BINARY_ADD, p_left, p_right);
        }
    };


    class AtomicBinaryExpressionParser : public ThreadUnsafeObject {
    protected:
        const RectifiedScope* host_scope;
        explicit AtomicBinaryExpressionParser(const RectifiedScope* p_host) : host_scope(p_host) {}
    public:
        struct ParseResult {
            const Ref<AbstractOperation> expression;
            const Type return_type;
            const RectifiedScope* host;

            friend class AtomicBinaryExpressionParser;
        private:
            ParseResult(const Ref<AbstractOperation>& p_expr, Type p_type, const RectifiedScope* p_host)
                : expression(p_expr), return_type(p_type), host(p_host) {}
        };
    protected:
        _NO_DISCARD_ _ALWAYS_INLINE_ ParseResult create_result(const Ref<AbstractOperation>& p_expr, Type p_type){
            return { p_expr, p_type, host_scope };
        }
    public:

        // This is to make sure no one tries to sneak in an expression as value
        // Yes, I could have let all of them passed as Ref<Value>, and use dynamic_cast to check,
        // but that would not be as performance
        virtual ParseResult parse(AbstractOperation::OperationType p_op, const Ref<ImmediateValue>& p_left, const Ref<ImmediateValue>& p_right) = 0;
        virtual ParseResult parse(AbstractOperation::OperationType p_op, const Ref<ImmediateValue>& p_left, const Ref<VariableValue>& p_right) = 0;
        virtual ParseResult parse(AbstractOperation::OperationType p_op, const Ref<VariableValue>& p_left, const Ref<ImmediateValue>& p_right) = 0;
        virtual ParseResult parse(AbstractOperation::OperationType p_op, const Ref<VariableValue>& p_left, const Ref<VariableValue>& p_right) = 0;
    };
    class PrimitiveAtomicBinaryExpressionParser : public AtomicBinaryExpressionParser {
    private:
        explicit PrimitiveAtomicBinaryExpressionParser(const RectifiedScope* p_host) : AtomicBinaryExpressionParser(p_host) {}
        friend class RectifiedScope;
    public:

        ParseResult parse(AbstractOperation::OperationType p_op, const Ref<ImmediateValue>& p_left, const Ref<ImmediateValue>& p_right) override;
        ParseResult parse(AbstractOperation::OperationType p_op, const Ref<ImmediateValue>& p_left, const Ref<VariableValue>& p_right) override;
        ParseResult parse(AbstractOperation::OperationType p_op, const Ref<VariableValue>& p_left, const Ref<ImmediateValue>& p_right) override;
        ParseResult parse(AbstractOperation::OperationType p_op, const Ref<VariableValue>& p_left, const Ref<VariableValue>& p_right) override;
    };

    class CopyConstructInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const Ref<Value> value_reference;
        void const* ctor; // void (*)(void*, const void*)
    private:
        CopyConstructInstruction(const Ref<VariableInstruction>& p_target, const Ref<Value>& p_val, void const* p_ctor)
                : Instruction(IT_COPY_CONSTRUCT), target_variable(p_target), value_reference(p_val), ctor(p_ctor){}
    public:
        // This method don't have an unsafe equivalent as it's constructor is saved in its Type data
        template<typename T>
        static Ref<CopyConstructInstruction> create_imm(const Ref<VariableInstruction>& p_target, const T& p_value){
            auto imm = ImmediateValue::create(p_value);
            if (imm->imm_type != p_target->type) MJ_RAISE("Mismatched type");
            auto as_value = imm.template c_style_cast<Value>();
            auto ins = new CopyConstructInstruction(p_target, as_value, (const void*)p_target->type.copy_constructor);
            return Ref<CopyConstructInstruction>::from_uninitialized_object(ins);
        }
        static Ref<CopyConstructInstruction> create_arg(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_arg_idx);
        static Ref<CopyConstructInstruction> create_var(const Ref<VariableInstruction> &p_target, const Ref<VariableInstruction> &p_assign_target);
        static Ref<CopyConstructInstruction> create_expr_primitive(const Ref<VariableInstruction> &p_target, const AtomicBinaryExpressionParser::ParseResult& p_parse_result);
    };
    class AssignInstruction : public Instruction {
    public:
        const Ref<VariableInstruction> target_variable;
        const Ref<Value> value_reference;
        void const* ctor; // void (*)(void*, const void*)
    private:
        AssignInstruction(const Ref<VariableInstruction>& p_target, const Ref<Value>& p_val, void const* p_ctor)
                : Instruction(IT_ASSIGN), target_variable(p_target), value_reference(p_val), ctor(p_ctor){}
    public:
        template<typename T>
        static Ref<AssignInstruction> create_imm_unsafe(const Ref<VariableInstruction>& p_target, const T& p_value) {
            auto imm = ImmediateValue::create(p_value).template c_style_cast<Value>();
            auto ins = new AssignInstruction(p_target, imm, (const void*)ObjectTools::assign<T>);
            return Ref<AssignInstruction>::from_uninitialized_object(ins);
        }
        template<typename T>
        static Ref<AssignInstruction> create_imm(const Ref<VariableInstruction>& p_target, const T& p_value) {
            static TYPE_CONSTEXPR auto type = Type::create<T>();
            if (type != p_target->type) MJ_RAISE("Mismatched type");
            return create_imm_unsafe(p_target, p_value);
        }
        template<typename T>
        static Ref<AssignInstruction> create_arg_unsafe(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_idx);
        template<typename T>
        static Ref<AssignInstruction> create_arg(const RectifiedScope* p_scope, const Ref<VariableInstruction>& p_target, const uint32_t& p_idx){
            static TYPE_CONSTEXPR auto type = Type::create<T>();
            if (type != p_target->type) MJ_RAISE("Mismatched type");
            return create_arg_unsafe<T>(p_scope, p_target, p_idx);
        }
        template<typename T>
        static Ref<AssignInstruction> create_var_unsafe(const Ref<VariableInstruction> &p_target, const Ref<VariableInstruction> &p_assign_target){
            if (p_target->type != p_assign_target->type) MJ_RAISE("Mismatched type");
            auto var = VariableValue::create(p_assign_target).c_style_cast<Value>();
            auto ins = new AssignInstruction(p_target, var, (const void*)ObjectTools::assign<T>);
            return Ref<AssignInstruction>::from_uninitialized_object(ins);
        }
        template<typename T>
        static Ref<AssignInstruction> create_var(const Ref<VariableInstruction> &p_target, const Ref<VariableInstruction> &p_assign_target){
            static TYPE_CONSTEXPR auto type = Type::create<T>();
            if (type != p_target->type) MJ_RAISE("Mismatched type");
            return create_var_unsafe<T>(p_target, p_assign_target);
        }
        static Ref<AssignInstruction> create_expr_primitive(const Ref<VariableInstruction> &p_target, const AtomicBinaryExpressionParser::ParseResult& p_parse_result);
    };

    class BranchInstruction : public Instruction {
    public:
        enum BranchType {
            BRANCH_IF,
            BRANCH_ELSE,
            BRANCH_WHILE,
        };
        const BranchType branch_type;
        const Ref<RectifiedScope> sub_scope;
    private:
    protected:
        explicit BranchInstruction(BranchType p_type, const Ref<RectifiedScope>& p_sub_scope);
    };

    class IfInstruction : public BranchInstruction {
    public:
        const Ref<AbstractOperation> condition;
    private:
        IfInstruction(const Ref<AbstractOperation>& p_condition, const Ref<RectifiedScope>& p_scope)
            : BranchInstruction(BRANCH_IF, p_scope), condition(p_condition) {}
    public:
        static Ref<IfInstruction> create(const Ref<AbstractOperation>& p_condition, const Ref<RectifiedScope>& p_scope);
    };

    class ElseInstruction : public BranchInstruction {
    private:
        explicit ElseInstruction(const Ref<RectifiedScope>& p_scope)
                : BranchInstruction(BRANCH_ELSE, p_scope) {}
    public:
        static Ref<ElseInstruction> create(const Ref<RectifiedScope>& p_scope);
    };

    class WhileInstruction : public BranchInstruction {
    public:
        const Ref<AbstractOperation> condition;
    private:
        WhileInstruction(const Ref<AbstractOperation>& p_condition, const Ref<RectifiedScope>& p_scope)
                : BranchInstruction(BRANCH_WHILE, p_scope), condition(p_condition) {}
    public:
        static Ref<WhileInstruction> create(const Ref<AbstractOperation>& p_condition, const Ref<RectifiedScope>& p_scope);
    };

    class BreakInstruction : public Instruction {
    private:
        BreakInstruction() : Instruction(IT_BREAK) {}
    public:
        static _ALWAYS_INLINE_ Ref<BreakInstruction> create(){
            return Ref<BreakInstruction>::from_uninitialized_object(new BreakInstruction());
        }
    };

    class RectifiedScope : public ThreadUnsafeObject {
    private:
        const RectifiedScope* parent_scope;
        const Ref<ArgumentsDeclaration> arguments;
        std::vector<Ref<RectifiedScope>> directly_owned_scopes{};
        std::vector<Ref<VariableInstruction>> variables{};
        std::vector<Ref<BranchInstruction>> branches{};
        std::vector<Ref<Instruction>> instructions{};
        uint32_t current_scope_offset{};

        void push_instruction(Ref<Instruction> p_ins);
    public:
        explicit RectifiedScope(const Ref<ArgumentsDeclaration>& p_args, const RectifiedScope* p_parent = nullptr);
        _NO_DISCARD_ _ALWAYS_INLINE_ const auto& get_arguments() const { return arguments; }
        _NO_DISCARD_ _ALWAYS_INLINE_ bool has_variable(const Ref<VariableInstruction>& p_var) const {
            return (p_var.is_valid() && p_var->parent_scope == this && p_var->scope_offset <= current_scope_offset);
        }
        _NO_DISCARD_ bool has_variable_in_all_scope(const Ref<VariableInstruction>& p_var) const;
        template<typename T>
        Ref<VariableInstruction> create_variable(const uint32_t& p_props = VariableInstruction::NONE){
            static TYPE_CONSTEXPR auto void_type = Type::create<void>();
            static TYPE_CONSTEXPR auto curr_type = Type::create<T>();
            TYPE_ASSERT(void_type != curr_type);
            // Create an anonymous ins
            auto ins = VariableInstruction::create<T>(this, p_props);
            variables.push_back(ins);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct_unsafe(const Ref<VariableInstruction>& p_var){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = ConstructInstruction::create_unsafe<T>(p_var);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct(const Ref<VariableInstruction>& p_var){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = ConstructInstruction::create<T>(p_var);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<CopyConstructInstruction> construct_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable(p_var)) MJ_RAISE("Does not own variable");
            auto ins = CopyConstructInstruction::create_imm(p_var, p_imm);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        Ref<CopyConstructInstruction> construct_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx);
        Ref<CopyConstructInstruction> construct_from_variable(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target);
        Ref<CopyConstructInstruction> construct_from_primitive_atomic_expression(const Ref<VariableInstruction> &p_var, const AtomicBinaryExpressionParser::ParseResult& p_parse_result);
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate_unsafe(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_imm_unsafe(p_var, p_imm);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_imm(p_var, p_imm);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument_unsafe(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_arg_unsafe<T>(this, p_var, p_idx);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own variable");
            auto ins = AssignInstruction::create_arg<T>(this, p_var, p_idx);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_variable_unsafe(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own assignment target");
            if (!has_variable_in_all_scope(p_copy_target)) MJ_RAISE("Does not own copy target");
            if (p_var == p_copy_target) MJ_RAISE("Assign and copy target must not be the same");
            auto ins = AssignInstruction::create_var_unsafe<T>(p_var, p_copy_target);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_variable(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target){
            if (!has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own assignment target");
            if (!has_variable_in_all_scope(p_copy_target)) MJ_RAISE("Does not own copy target");
            if (p_var == p_copy_target) MJ_RAISE("Assign and copy target must not be the same");
            auto ins = AssignInstruction::create_var<T>(p_var, p_copy_target);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        Ref<AssignInstruction> assign_from_primitive_atomic_expression(const Ref<VariableInstruction> &p_var, const AtomicBinaryExpressionParser::ParseResult& p_parse_result);
        template<typename From, typename To>
        Ref<ConvertInstruction> convert(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to, bool p_coerce){
            static TYPE_CONSTEXPR auto f_type = Type::create<From>();
            static TYPE_CONSTEXPR auto t_type = Type::create<To>();
            if (!has_variable_in_all_scope(p_from) || !has_variable_in_all_scope(p_to)) MJ_RAISE("Does not own target");
            if (p_from->type != f_type) MJ_RAISE("Mismatched 'from' type");
            if (p_to->type != t_type) MJ_RAISE("Mismatched 'to' type");
            auto ins = ConvertInstruction::create<From, To>(p_from, p_to, p_coerce);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        template<typename From, typename To>
        Ref<PrimitiveConvertInstruction> primitive_convert(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to){
            static TYPE_CONSTEXPR auto f_type = Type::create<From>();
            static TYPE_CONSTEXPR auto t_type = Type::create<To>();
            if (!has_variable_in_all_scope(p_from) || !has_variable_in_all_scope(p_to)) MJ_RAISE("Does not own target");
            if (p_from->type != f_type) MJ_RAISE("Mismatched 'from' type");
            if (p_to->type != t_type) MJ_RAISE("Mismatched 'to' type");
            auto ins = PrimitiveConvertInstruction::create<From, To>(p_from, p_to);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        Ref<InvokeJitInstruction> invoke_jit(const Ref<RectifiedFunction>& p_func, const Ref<ArgumentsVector>& p_args,
                                             const Ref<VariableInstruction>& p_ret_var);
        template<typename R>
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            // Does not own this variable;
            if (p_var.is_valid() && !has_variable_in_all_scope(p_var)) MJ_RAISE("Does not own variable");
            // Does not match the return type
            static const auto return_type = Type::create<R>();
            // If return type is void, then p_var must be null
            if (return_type.size != 0) {
                if (p_var->type != return_type)
                    MJ_RAISE("Return type mismatched");
            } else if (p_var.is_valid()) MJ_RAISE("p_var must be invalid for functions that return 'void'");
            auto ins = ReturnInstruction::create(p_var);
            push_instruction(ins.template c_style_cast<Instruction>());
            return ins;
        }
        Ref<ScopeCreateInstruction> create_scope(const Ref<RectifiedScope>& p_child);
        Ref<IfInstruction> if_branch(const AtomicBinaryExpressionParser::ParseResult& p_parse_result,
                                     const microjit::Ref<microjit::RectifiedScope> &p_child);
        Ref<ElseInstruction> else_branch(const microjit::Ref<microjit::RectifiedScope> &p_child);
        Ref<WhileInstruction> while_branch(const AtomicBinaryExpressionParser::ParseResult& p_parse_result,
                                           const microjit::Ref<microjit::RectifiedScope> &p_child);
        Ref<BreakInstruction> break_loop();
        Ref<PrimitiveAtomicBinaryExpressionParser> create_primitive_binary_expression_parser() const;

        _NO_DISCARD_ const auto& get_instructions() const { return instructions; }
        _NO_DISCARD_ const auto& get_variables() const { return variables; }
        _NO_DISCARD_ const auto& get_branches() const { return branches; }
        _NO_DISCARD_ const auto& get_scopes() const { return directly_owned_scopes; }
    };

    template <typename R, typename ...Args>
    class Scope : public ThreadUnsafeObject {
    private:
        Function<R, Args...>* parent_function;
        const Scope<R, Args...>* parent_scope;
        Ref<RectifiedScope> rectified_scope;
    public:
        explicit Scope(Function<R, Args...>* p_parent, const Scope<R, Args...>* p_parent_scope = nullptr)
                : parent_function(p_parent), parent_scope(p_parent_scope),
                  rectified_scope(Ref<RectifiedScope>::make_ref(p_parent->get_arguments_data(),
                                                                parent_scope ? parent_scope->rectified_scope.ptr() : nullptr)) {}

        bool has_variable(const Ref<VariableInstruction>& p_var) const {
            return rectified_scope->has_variable(p_var);
        }
        bool has_variable_in_all_scope(const Ref<VariableInstruction>& p_var) const {
            return rectified_scope->has_variable_in_all_scope(p_var);
        }
        template<typename T>
        Ref<VariableInstruction> create_variable(const uint32_t& p_props = VariableInstruction::NONE){
            return rectified_scope->template create_variable<T>(p_props);
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct_unsafe(const Ref<VariableInstruction>& p_var){
            return rectified_scope->default_construct_unsafe<T>(p_var);
        }
        template<typename T>
        Ref<ConstructInstruction> default_construct(const Ref<VariableInstruction>& p_var){
            return rectified_scope->default_construct<T>(p_var);
        }
        template<typename T>
        Ref<CopyConstructInstruction> construct_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->construct_from_immediate<T>(p_var, p_imm);
        }
        Ref<CopyConstructInstruction> construct_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->construct_from_argument(p_var, p_idx);
        }
        Ref<CopyConstructInstruction> construct_from_variable(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target){
            return rectified_scope->construct_from_variable(p_var, p_copy_target);
        }
        Ref<CopyConstructInstruction> construct_from_primitive_atomic_expression(const Ref<VariableInstruction> &p_var, const AtomicBinaryExpressionParser::ParseResult& p_parse_result){
            return rectified_scope->construct_from_primitive_atomic_expression(p_var, p_parse_result);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate_unsafe(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->assign_from_immediate_unsafe<T>(p_var, p_imm);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_immediate(const Ref<VariableInstruction>& p_var, const T& p_imm){
            return rectified_scope->assign_from_immediate<T>(p_var, p_imm);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument_unsafe(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->assign_from_argument_unsafe<T>(p_var, p_idx);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_argument(const Ref<VariableInstruction>& p_var, const uint32_t& p_idx){
            return rectified_scope->assign_from_argument<T>(p_var, p_idx);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_variable_unsafe(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target){
            return rectified_scope->assign_from_variable_unsafe<T>(p_var, p_copy_target);
        }
        template<typename T>
        Ref<AssignInstruction> assign_from_variable(const Ref<VariableInstruction>& p_var, const Ref<VariableInstruction>& p_copy_target){
            return rectified_scope->assign_from_variable<T>(p_var, p_copy_target);
        }
        Ref<AssignInstruction> assign_from_primitive_atomic_expression(const Ref<VariableInstruction> &p_var, const AtomicBinaryExpressionParser::ParseResult& p_parse_result){
            return rectified_scope->assign_from_primitive_atomic_expression(p_var, p_parse_result);
        }
        template<typename From, typename To>
        Ref<ConvertInstruction> convert(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to, bool p_coerce = false){
            TYPE_ASSERT(Type::create<From>() != Type::create<To>());
            return rectified_scope->convert<From, To>(p_from, p_to, p_coerce);
        }
        template<typename From, typename To>
        [[deprecated("When Virtual Stack stop being supported, this method will straight up cause SIGSEGV")]]
        Ref<PrimitiveConvertInstruction> primitive_convert(const Ref<VariableInstruction>& p_from, const Ref<VariableInstruction>& p_to){
            TYPE_ASSERT(Type::create<From>() != Type::create<To>());
            return rectified_scope->primitive_convert<From, To>(p_from, p_to);
        }

        template<typename R2, typename ...Args2>
        Ref<InvokeJitInstruction> invoke_jit(const Ref<Function<R2, Args2...>>& p_func,
                                             const Ref<ArgumentsVector>& p_args = Ref<ArgumentsVector>::null(),
                                             const Ref<VariableInstruction>& p_ret_var = Ref<VariableInstruction>::null());
        Ref<ReturnInstruction> function_return(const Ref<VariableInstruction>& p_var = Ref<VariableInstruction>::null()){
            return rectified_scope->function_return<R>(p_var);
        }
        Ref<Scope<R, Args...>> create_scope() {
            auto new_scope = Ref<Scope<R, Args...>>::make_ref(parent_function, this);
            rectified_scope->create_scope(new_scope->rectified_scope);
            return new_scope;
        }
        Ref<Scope<R, Args...>> if_branch(const AtomicBinaryExpressionParser::ParseResult& p_parse_result){
            // This could be optimized...
            auto new_scope = Ref<Scope<R, Args...>>::make_ref(parent_function, this);
            rectified_scope->if_branch(p_parse_result, new_scope->rectified_scope);
            return new_scope;
        }
        Ref<Scope<R, Args...>> else_branch(){
            auto new_scope = Ref<Scope<R, Args...>>::make_ref(parent_function, this);
            rectified_scope->else_branch(new_scope->rectified_scope);
            return new_scope;
        }
        Ref<Scope<R, Args...>> while_branch(const AtomicBinaryExpressionParser::ParseResult& p_parse_result){
            auto new_scope = Ref<Scope<R, Args...>>::make_ref(parent_function, this);
            rectified_scope->while_branch(p_parse_result, new_scope->rectified_scope);
            return new_scope;
        }
        Ref<BreakInstruction> break_loop(){
            return rectified_scope->break_loop();
        }
        Ref<PrimitiveAtomicBinaryExpressionParser> create_primitive_binary_expression_parser() const {
            return rectified_scope->create_primitive_binary_expression_parser();
        }
        _NO_DISCARD_ Ref<RectifiedScope> get_rectified_scope() const { return rectified_scope; }
    };

    class RectifiedFunction : public ThreadUnsafeObject {
    public:
        const void* host;
        const Ref<ArgumentsDeclaration> arguments;
        const std::function<void(VirtualStack*)>* trampoline;
        const Ref<RectifiedScope> main_scope;
        const Type return_type;
    private:
        RectifiedFunction(const void* p_host,
                          const Ref<ArgumentsDeclaration>& p_args,
                          std::function<void(VirtualStack*)>* p_trampoline,
                          const Ref<RectifiedScope>& p_main_scope,
                          Type p_ret)
                          : host(p_host), arguments(p_args), trampoline(p_trampoline),
                            main_scope(p_main_scope), return_type(p_ret) {}
    public:
        template <typename R, typename ...Args>
        static Ref<RectifiedFunction> create(const Function<R, Args...>* p_host,
                                             const Ref<ArgumentsDeclaration>& p_args,
                                             std::function<void(VirtualStack*)>* p_trampoline,
                                             const Ref<RectifiedScope>& p_main_scope,
                                             Type p_ret){
            auto rectified = new RectifiedFunction(p_host, p_args, p_trampoline, p_main_scope, p_ret);
            return Ref<RectifiedFunction>::from_uninitialized_object(rectified);
        }
    };

    template <typename R, typename ...Args>
    class Function : public ThreadUnsafeObject {
    private:
        const Ref<ArgumentsDeclaration> arguments;
        Ref<Scope<R, Args...>> main_scope;
        mutable std::function<void(VirtualStack*)> trampoline_function{};
    public:
        Function() : arguments(ArgumentsDeclaration::create<Args...>()),
                     main_scope(Ref<Scope<R, Args...>>::make_ref(this)) {}

        Ref<Scope<R, Args...>> get_main_scope() { return main_scope; }
        const Ref<ArgumentsDeclaration>& get_arguments_data() const { return arguments; }
        Ref<RectifiedFunction> rectify() const {
            return RectifiedFunction::create(this, arguments, &trampoline_function,
                                             main_scope->get_rectified_scope(), Type::create<R>());
        }
        std::function<void(VirtualStack*)>& get_trampoline() const { return trampoline_function; }
    };
    template<typename T>
    Ref<AssignInstruction>
    AssignInstruction::create_arg_unsafe(const RectifiedScope *p_scope, const Ref<VariableInstruction> &p_target,
                                         const uint32_t &p_idx) {
        const auto& types = p_scope->get_arguments()->argument_types();
        if (types.size() <= p_idx) MJ_RAISE("Invalid argument index");
        if (types[p_idx] != p_target->type) MJ_RAISE("Mismatched type");
        auto arg = ArgumentValue::create(p_idx).c_style_cast<Value>();
        auto ins = new AssignInstruction(p_target, arg, (const void*)ObjectTools::assign<T>);
        return Ref<AssignInstruction>::from_uninitialized_object(ins);
    }
    template<typename R, typename... Args>
    template<typename R2, typename... Args2>
    Ref<InvokeJitInstruction>
    Scope<R, Args...>::invoke_jit(const Ref<Function<R2, Args2...>> &p_func, const Ref<ArgumentsVector> &p_args,
                                  const Ref<VariableInstruction> &p_ret_var) {
        return rectified_scope->invoke_jit(p_func->rectify(), p_args, p_ret_var);
    }
}

#endif //MICROJIT_INSTRUCTIONS_H
