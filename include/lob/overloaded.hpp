#pragma once

template <typename... Lambdas> struct Overloaded : Lambdas... {
    using Lambdas::operator()...;
};