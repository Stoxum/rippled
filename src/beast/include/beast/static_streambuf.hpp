//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_STATIC_STREAMBUF_HPP
#define BEAST_STATIC_STREAMBUF_HPP

#include <boost/utility/base_from_member.hpp>
#include <algorithm>
#include <array>
#include <cstring>

namespace beast {

/** A `Streambuf` with a fixed size internal buffer.

    Ownership of the underlying storage belongs to the derived class.

    @note Variables are usually declared using the template class
    `static_streambuf_n`; however, to reduce the number of instantiations
    of template functions receiving static stream buffer arguments in a
    deduced context, the signature of the receiving function should use
    `static_streambuf`.
*/
class static_streambuf
{
#if GENERATING_DOCS
private:
#else
protected:
#endif
    std::uint8_t* in_;
    std::uint8_t* out_;
    std::uint8_t* last_;
    std::uint8_t* end_;

public:
#if GENERATING_DOCS
    /// The type used to represent the input sequence as a list of buffers.
    using const_buffers_type = implementation_defined;

    /// The type used to represent the output sequence as a list of buffers.
    using mutable_buffers_type = implementation_defined;

#else
    class const_buffers_type;
    class mutable_buffers_type;

    static_streambuf(
        static_streambuf const& other) noexcept = delete;

    static_streambuf& operator=(
        static_streambuf const&) noexcept = delete;

#endif

    /// Returns the largest size output sequence possible.
    std::size_t
    max_size() const
    {
        return end_ - in_;
    }

    /// Get the size of the input sequence.
    std::size_t
    size() const
    {
        return out_ - in_;
    }

    /** Get a list of buffers that represents the output sequence, with the given size.

        @throws std::length_error if the size would exceed the limit
        imposed by the underlying mutable buffer sequence.
    */
    mutable_buffers_type
    prepare(std::size_t n);

    /// Move bytes from the output sequence to the input sequence.
    void
    commit(std::size_t n)
    {
        out_ += std::min<std::size_t>(n, last_ - out_);
    }

    /// Get a list of buffers that represents the input sequence.
    const_buffers_type
    data() const;

    /// Remove bytes from the input sequence.
    void
    consume(std::size_t n)
    {
        in_ += std::min<std::size_t>(n, out_ - in_);
    }

#if GENERATING_DOCS
private:
#else
protected:
#endif
    static_streambuf(std::uint8_t* p, std::size_t n)
    {
        reset(p, n);
    }

    void
    reset(std::uint8_t* p, std::size_t n)
    {
        in_ = p;
        out_ = p;
        last_ = p;
        end_ = p + n;
    }
};

//------------------------------------------------------------------------------

/** A `Streambuf` with a fixed size internal buffer.

    @tparam N The number of bytes in the internal buffer.

    @note To reduce the number of template instantiations when passing
    objects of this type in a deduced context, the signature of the
    receiving function should use `static_streambuf` instead.
*/
template<std::size_t N>
class static_streambuf_n
    : private boost::base_from_member<
        std::array<std::uint8_t, N>>
    , public static_streambuf
{
    using member_type = boost::base_from_member<
        std::array<std::uint8_t, N>>;
public:
#if GENERATING_DOCS
private:
#endif
    static_streambuf_n(
        static_streambuf_n const&) = delete;
    static_streambuf_n& operator=(
        static_streambuf_n const&) = delete;
#if GENERATING_DOCS
public:
#endif

    /// Construct a static stream buffer.
    static_streambuf_n()
        : static_streambuf(
            member_type::member.data(),
                member_type::member.size())
    {
    }

    /** Reset the stream buffer.

        Postconditions:
            The input sequence and output sequence are empty,
            `max_size()` returns `N`.
    */
    void
    reset()
    {
        static_streambuf::reset(
            member_type::member.data(),
                member_type::member.size());
    }
};

} // beast

#include <beast/impl/static_streambuf.ipp>

#endif