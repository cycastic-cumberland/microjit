
// This file is automatically generated

#ifndef MICROJIT_PRIMITIVE_CONVERSION_MAP_H
#define MICROJIT_PRIMITIVE_CONVERSION_MAP_H

#include <cstdint>

namespace microjit {
    // The actual conversion won't be done here as
    // primitive type conversion should always be done inline, for performance reason.
    // These functions only act as keys for the actual conversion map,
    // which will be implemented by the compilers independently
    // Update: apparently converting from integer to floating point is too much of a hassle, so no more inlining for them
    class PrimitiveConversionHelper {
    public:
        static void conversion_candidate(const uint8_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint8_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint16_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint32_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const uint64_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int8_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int16_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int32_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, float* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const int64_t* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const float* p_from, double* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, uint8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, uint16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, uint32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, uint64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, int8_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, int16_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, int32_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, int64_t* p_to) { *p_to = *p_from; }
        static void conversion_candidate(const double* p_from, float* p_to) { *p_to = *p_from; }

        template<typename To, typename From>
        static void convert(void (**f)(const From*, To*)){
            *f = conversion_candidate;
        }
    };
}

#endif //MICROJIT_PRIMITIVE_CONVERSION_MAP_H
