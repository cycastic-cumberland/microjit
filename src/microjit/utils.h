//
// Created by cycastic on 8/17/23.
//

#ifndef MICROJIT_UTILS_H
#define MICROJIT_UTILS_H

#include <exception>
#include <string>

#define MJ_RAISE(msg) throw MicroJITException(std::string(__FILE__) + " at (" + __FUNCTION__  + " : " + std::to_string(__LINE__) + "): " + (msg))

namespace microjit {
class MicroJITException : public std::exception {
private:
    const std::string msg;
public:
    explicit MicroJITException(const char* p_msg = "") : msg(p_msg) {}
    explicit MicroJITException(std::string p_msg) : msg(std::move(p_msg)) {}

    const char* what() const noexcept override { return msg.c_str(); }
};
}

#endif //MICROJIT_UTILS_H
