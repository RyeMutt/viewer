/**
 * @file llrand.cpp
 * @brief Global random generator.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llrand.h"
#include "lluuid.h"

#include <boost/random/lagged_fibonacci.hpp>
#include <boost/random/mersenne_twister.hpp>

/**
 * Use the boost random number generators if you want a stateful
 * random numbers. If you want more random numbers, use the
 * c-functions since they will generate faster/better randomness
 * across the process.
 *
 * I tested some of the boost random engines, and picked a good double
 * generator and a good integer generator. I also took some timings
 * for them on linux using gcc 3.3.5. The harness also did some other
 * fairly trivial operations to try to limit compiler optimizations,
 * so these numbers are only good for relative comparisons.
 *
 * usec/inter       algorithm
 * 0.21             boost::minstd_rand0
 * 0.039            boost:lagged_fibonacci19937
 * 0.036            boost:lagged_fibonacci607
 * 0.44             boost::hellekalek1995
 * 0.44             boost::ecuyer1988
 * 0.042            boost::rand48
 * 0.043            boost::mt11213b
 * 0.028            stdlib random()
 * 0.05             stdlib lrand48()
 * 0.034            stdlib rand()
 * 0.020            the old & lame LLRand
 */

/**
 * @brief typedefs for good boost lagged fibonacci.
 * @see boost::lagged_fibonacci
 *
 * These generators will quickly generate doubles. Note the memory
 * requirements, because they are somewhat high. I chose the smallest
 * one, and one comparable in speed but higher periodicity without
 * outrageous memory requirements.
 * To use:
 *  LLRandLagFib607 foo((U32)time(NULL));
 *  double bar = foo();
 */

typedef boost::lagged_fibonacci607 LLRandLagFib607;
/**<
 * lengh of cycle: 2^32,000
 * memory: 607*sizeof(double) (about 5K)
 */

typedef boost::lagged_fibonacci2281 LLRandLagFib2281;
/**<
 * lengh of cycle: 2^120,000
 * memory: 2281*sizeof(double) (about 17K)
 */

/**
 * @breif typedefs for a good boost mersenne twister implementation.
 * @see boost::mersenne_twister
 *
 * This fairly quickly generates U32 values
 * To use:
 *  LLRandMT19937 foo((U32)time(NULL));
 *  U32 bar = foo();
 *
 * lengh of cycle: 2^19,937-1
 * memory: about 2496 bytes
 */
typedef boost::mt11213b LLRandMT19937;

/**
 * Through analysis, we have decided that we want to take values which
 * are close enough to 1.0 to map back to 0.0.  We came to this
 * conclusion from noting that:
 *
 * [0.0, 1.0)
 *
 * when scaled to the integer set:
 *
 * [0, 4)
 *
 * there is some value close enough to 1.0 that when multiplying by 4,
 * gets truncated to 4. Therefore:
 *
 * [0,1-eps] => 0
 * [1,2-eps] => 1
 * [2,3-eps] => 2
 * [3,4-eps] => 3
 *
 * So 0 gets uneven distribution if we simply clamp. The actual
 * clamp utilized in this file is to map values out of range back
 * to 0 to restore uniform distribution.
 *
 * Also, for clamping floats when asking for a distribution from
 * [0.0,g) we have determined that for values of g < 0.5, then
 * rand*g=g, which is not the desired result. As above, we clamp to 0
 * to restore uniform distribution.
 */

// pRandomGenerator is a stateful static object, which is therefore not
// inherently thread-safe.
//We use a pointer to not construct a huge object in the TLS space, sadly this is necessary
// due to libcef.so on Linux being compiled with TLS model initial-exec (resulting in
// FLAG STATIC_TLS, see readelf libcef.so). CEFs own TLS objects + LLRandLagFib2281 then will exhaust the
// available TLS space, causing media failure.

static thread_local std::unique_ptr< LLRandLagFib2281 > pRandomGenerator = nullptr;

namespace {
    F64 ll_internal_get_rand()
    {
        if( !pRandomGenerator )
        {
            pRandomGenerator = std::make_unique<LLRandLagFib2281>(LLUUID::getRandomSeed());
        }

        return(*pRandomGenerator)();
    }
}

// no default implementation, only specific F64 and F32 specializations
template <typename REAL>
inline REAL ll_internal_random();

template <>
inline F64 ll_internal_random<F64>()
{
    // *HACK: Through experimentation, we have found that dual core
    // CPUs (or at least multi-threaded processes) seem to
    // occasionally give an obviously incorrect random number -- like
    // 5^15 or something. Sooooo, clamp it as described above.
    F64 rv{ ll_internal_get_rand() };
    if(!((rv >= 0.0) && (rv < 1.0))) return fmod(rv, 1.0);
    return rv;
}

template <>
inline F32 ll_internal_random<F32>()
{
    // *HACK: clamp the result as described above.
    // Per Monty, it's important to clamp using the correct fmodf() rather
    // than expanding to F64 for fmod() and then truncating back to F32. Prior
    // to this change, we were getting sporadic ll_frand() == 1.0 results.
    F32 rv{ narrow<F64>(ll_internal_get_rand()) };
    if(!((rv >= 0.0f) && (rv < 1.0f))) return fmodf(rv, 1.0f);
    return rv;
}

/*------------------------------ F64 aliases -------------------------------*/
inline F64 ll_internal_random_double()
{
    return ll_internal_random<F64>();
}

F64 ll_drand()
{
    return ll_internal_random_double();
}

/*------------------------------ F32 aliases -------------------------------*/
inline F32 ll_internal_random_float()
{
    return ll_internal_random<F32>();
}

F32 ll_frand()
{
    return ll_internal_random_float();
}

/*-------------------------- clamped random range --------------------------*/
S32 ll_rand()
{
    return ll_rand(RAND_MAX);
}

S32 ll_rand(S32 val)
{
    // The clamping rules are described above.
    S32 rv = (S32)(ll_internal_random_double() * val);
    if(rv == val) return 0;
    return rv;
}

template <typename REAL>
REAL ll_grand(REAL val)
{
    // The clamping rules are described above.
    REAL rv = ll_internal_random<REAL>() * val;
    if(val > 0)
    {
        if(rv >= val) return REAL();
    }
    else
    {
        if(rv <= val) return REAL();
    }
    return rv;
}

F32 ll_frand(F32 val)
{
    return ll_grand<F32>(val);
}

F64 ll_drand(F64 val)
{
    return ll_grand<F64>(val);
}
