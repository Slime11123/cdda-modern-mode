#pragma once
#ifndef CATA_SRC_SCOPED_RNG_H
#define CATA_SRC_SCOPED_RNG_H
#include <random>
#include "rng.h"

// Preview queries also use normal_roll's cached spare sample.
class scoped_rng_restore
{
        cata_default_random_engine engine;
        std::normal_distribution<double> normal;
    public:
        scoped_rng_restore();
        ~scoped_rng_restore();
        scoped_rng_restore( const scoped_rng_restore & ) = delete;
        scoped_rng_restore &operator=( const scoped_rng_restore & ) = delete;
};
#endif
