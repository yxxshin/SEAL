// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include <array>
#include <iterator>
#include <memory>
#include "seal/util/defines.h"
#include "seal/util/common.h"
#include "seal/util/aes.h"
#include "seal/intarray.h"
#include "seal/memorymanager.h"

namespace seal
{
    /**
    Returns a random 64-bit integer.
    */
    SEAL_NODISCARD std::uint64_t random_uint64();

    /**
    Provides the base class for a seeded uniform random number generator. Instances
    of this class are meant to be created by an instance of the factory class
    UniformRandomGeneratorFactory. This class is meant for users to sub-class to
    implement their own random number generators. The library will never use one
    instance of the UniformRandomGenerator class concurrently, so calls to functions
    such as generate() do not need to be thread-safe.

    @see UniformRandomGeneratorFactory for the base class of a factory class that
    generates UniformRandomGenerator instances.
    */
    class UniformRandomGenerator
    {
    public:
        /**
        Creates a new UniformRandomGenerator instance initialized with the given seed.

        @param[in] seed The seed for the random number generator
        */
        UniformRandomGenerator(std::array<std::uint64_t, 2> seed) :
            seed_(seed)
        {
        }

        /**
        Fills a given buffer with a given number of bytes of randomness. Note that
        the implementation does not need to be thread-safe.
        */
        void generate(std::size_t byte_count, SEAL_BYTE *destination);

        /**
        Generates a new unsigned 32-bit random number. Note that the implementation
        does not need to be thread-safe.
        */
        SEAL_NODISCARD inline std::uint32_t generate()
        {
            std::uint32_t result;
            generate(sizeof(result), reinterpret_cast<SEAL_BYTE*>(&result));
            return result;
        }

        /**
        Discards the contents of the current randomness buffer and refills it
        with fresh randomness.
        */
        inline void refresh()
        {
            refill_buffer();
            buffer_head_ = buffer_begin_;
        }

        /**
        Returns the seed for the random number generator.
        */
        SEAL_NODISCARD inline std::array<std::uint64_t, 2> seed() noexcept
        {
            return seed_;
        }

        /**
        Destroys the random number generator.
        */
        virtual ~UniformRandomGenerator() = default;

    protected:
        static constexpr std::size_t buffer_uint64_count_ = 1024;

        static constexpr std::size_t buffer_byte_count_ =
            buffer_uint64_count_ * util::bytes_per_uint64;

        const std::array<std::uint64_t, 2> seed_;

        virtual void refill_buffer() = 0;

    private:
        IntArray<SEAL_BYTE> buffer_{
            buffer_byte_count_,
            MemoryManager::GetPool(mm_prof_opt::FORCE_NEW, true) };

    protected:
        decltype(buffer_)::T *const buffer_begin_{ buffer_.begin() };

        decltype(buffer_)::T *const buffer_end_{ buffer_.end() };

        decltype(buffer_)::T *buffer_head_{ buffer_end_ };
    };

    /**
    Provides the base class for a factory instance that creates instances of
    UniformRandomGenerator. This class is meant for users to sub-class to implement
    their own random number generators.

    @see UniformRandomGenerator for details relating to the random number generator
    instances.
    @see StandardRandomAdapterFactory for an implementation of
    UniformRandomGeneratorFactory that supports the standard C++ library's
    random number generators.
    */
    class UniformRandomGeneratorFactory
    {
    public:
        /**
        Creates a new UniformRandomGeneratorFactory. The seed will be sampled
        randomly for each UniformRandomGenerator instance created by the factory
        instance, which is desirable in most normal use-cases.
        */
        UniformRandomGeneratorFactory() :
            use_random_seed_(true)
        {
        }

        /**
        Creates a new UniformRandomGeneratorFactory and sets the default seed to
        the given value. For debugging purposes it may sometimes be convenient to
        have the same randomness be used deterministically and repeatedly. Such
        randomness sampling is naturally insecure and must be strictly restricted
        to debugging situations. Thus, most users should never have a reason to
        use this constructor.

        @param[in] default_seed The default value for a seed to be used by all
        created instances of UniformRandomGenerator
        */
        UniformRandomGeneratorFactory(
            std::array<std::uint64_t, 2> default_seed) :
            default_seed_(default_seed),
            use_random_seed_(false)
        {
        }

        /**
        Creates a new uniform random number generator.
        */
        SEAL_NODISCARD auto create() -> std::shared_ptr<UniformRandomGenerator>
        {
            return use_random_seed_ ?
                create_impl({ random_uint64(), random_uint64() }) :
                create_impl(default_seed_);
        }

        /**
        Creates a new uniform random number generator seeded with the given seed,
        overriding the default seed for this factory instance.

        @param[in] seed The seed to be used for the created random number generator
        */
        SEAL_NODISCARD auto create(std::array<std::uint64_t, 2> seed)
            -> std::shared_ptr<UniformRandomGenerator>
        {
            return create_impl(seed);
        }

        /**
        Destroys the random number generator factory.
        */
        virtual ~UniformRandomGeneratorFactory() = default;

        /**
        Returns the default random number generator factory. This instance should
        not be destroyed.
        */
        static auto DefaultFactory()
            -> const std::shared_ptr<UniformRandomGeneratorFactory>;

    protected:
        SEAL_NODISCARD virtual auto create_impl(std::array<std::uint64_t, 2> seed)
            -> std::shared_ptr<UniformRandomGenerator> = 0;

    private:
        std::array<std::uint64_t, 2> default_seed_;

        bool use_random_seed_ = false;
    };
#ifdef SEAL_USE_AES_NI_PRNG
    /**
    Provides an implementation of UniformRandomGenerator for using very fast
    AES-NI randomness with given 128-bit seed.
    */
    class FastPRNG : public UniformRandomGenerator
    {
    public:
        /**
        Creates a new FastPRNG instance initialized with the given seed.
        */
        FastPRNG(std::array<std::uint64_t, 2> seed) :
            UniformRandomGenerator(seed),
            aes_enc_{ seed_[0], seed_[1] }
        {
        }

        /**
        Destroys the random number generator.
        */
        virtual ~FastPRNG() override = default;

    protected:
        virtual void refill_buffer() override;

    private:
        AESEncryptor aes_enc_;

        static constexpr std::size_t aes_block_byte_count_ = 16;

        static constexpr std::size_t buffer_block_count_ =
            buffer_byte_count_ / aes_block_byte_count_;

        std::size_t counter_ = 0;
    };

    class FastPRNGFactory : public UniformRandomGeneratorFactory
    {
    public:
        /**
        Creates a new FastPRNGFactory. The seed will be sampled randomly for each
        FastPRNG instance created by the factory instance, which is desirable in
        most normal use-cases.
        */
        FastPRNGFactory() : UniformRandomGeneratorFactory()
        {
        }

        /**
        Creates a new FastPRNGFactory and sets the default seed to the given value.
        For debugging purposes it may sometimes be convenient to have the same
        randomness be used deterministically and repeatedly. Such randomness
        sampling is naturally insecure and must be strictly restricted to debugging
        situations. Thus, most users should never have a reason to use this
        constructor.

        @param[in] default_seed The default value for a seed to be used by all
        created instances of FastPRNG
        */
        FastPRNGFactory(std::array<std::uint64_t, 2> default_seed) :
            UniformRandomGeneratorFactory(default_seed)
        {
        }

        /**
        Destroys the random number generator factory.
        */
        virtual ~FastPRNGFactory() = default;

    protected:
        SEAL_NODISCARD virtual auto create_impl(std::array<std::uint64_t, 2> seed)
            -> std::shared_ptr<UniformRandomGenerator> override
        {
            return std::make_shared<FastPRNG>(seed);
        }

    private:
    };
#endif //SEAL_USE_AES_NI_PRNG
    /**
    Provides an implementation of UniformRandomGenerator for using Blake2xb for
    generating randomness with given 128-bit seed.
    */
    class BlakePRNG : public UniformRandomGenerator
    {
    public:
        /**
        Creates a new BlakePRNG instance initialized with the given seed.
        */
        BlakePRNG(std::array<std::uint64_t, 2> seed) :
            UniformRandomGenerator(seed)
        {
        }

        /**
        Destroys the random number generator.
        */
        virtual ~BlakePRNG() override = default;

    protected:
        virtual void refill_buffer() override;

    private:
        std::size_t counter_ = 0;
    };

    class BlakePRNGFactory : public UniformRandomGeneratorFactory
    {
    public:
        /**
        Creates a new BlakePRNGFactory. The seed will be sampled randomly for each
        BlakePRNG instance created by the factory instance, which is desirable in
        most normal use-cases.
        */
        BlakePRNGFactory() : UniformRandomGeneratorFactory()
        {
        }

        /**
        Creates a new BlakePRNGFactory and sets the default seed to the given value.
        For debugging purposes it may sometimes be convenient to have the same
        randomness be used deterministically and repeatedly. Such randomness
        sampling is naturally insecure and must be strictly restricted to debugging
        situations. Thus, most users should never have a reason to use this
        constructor.

        @param[in] default_seed The default value for a seed to be used by all
        created instances of BlakePRNG
        */
        BlakePRNGFactory(std::array<std::uint64_t, 2> default_seed) :
            UniformRandomGeneratorFactory(default_seed)
        {
        }

        /**
        Destroys the random number generator factory.
        */
        virtual ~BlakePRNGFactory() = default;

    protected:
        SEAL_NODISCARD virtual auto create_impl(std::array<std::uint64_t, 2> seed)
            -> std::shared_ptr<UniformRandomGenerator> override
        {
            return std::make_shared<BlakePRNG>(seed);
        }

    private:
    };
}
