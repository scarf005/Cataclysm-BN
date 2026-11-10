#pragma once

#include <cstddef>
#include <istream>
#include <streambuf>
#include <string>
#include <string_view>
#include <unordered_map>

/// Where a JSON object or array that was already skipped ends; see JsonIn::skip_value.
struct skipped_json_value {
    int end = 0;
    bool ate_separator = false;
};

/// Read-only stream buffer over an owned string.
/// Exposes the unread bytes so parsers can scan runs of characters at once.
class memory_streambuf : public std::streambuf
{
    public:
        explicit memory_streambuf( std::string contents );
        memory_streambuf( const memory_streambuf & ) = delete;
        auto operator=( const memory_streambuf & ) -> memory_streambuf & = delete; // *NOPAD*

        auto contents() const -> const std::string & { // *NOPAD*
            return data_;
        }
        auto unread() const -> std::string_view {
            return { gptr(), static_cast<size_t>( egptr() - gptr() ) };
        }
        /// Consumes `count` bytes, which must not exceed `unread().size()`.
        auto advance( const size_t count ) -> void {
            setg( eback(), gptr() + count, egptr() );
        }
        /// JSON values skipped so far by start offset, shared by every JsonIn reading this buffer.
        auto skipped_json_values() -> std::unordered_map<int, skipped_json_value> & { // *NOPAD*
            return skipped_json_values_;
        }

    protected:
        auto seekoff( off_type off, std::ios_base::seekdir dir,
                      std::ios_base::openmode which ) -> pos_type override;
        auto seekpos( pos_type pos, std::ios_base::openmode which ) -> pos_type override;

    private:
        std::string data_;
        std::unordered_map<int, skipped_json_value> skipped_json_values_;
};

/// Input stream reading from an owned string through memory_streambuf.
class memory_istream : public std::istream
{
    public:
        explicit memory_istream( std::string contents );

        auto contents() const -> const std::string & { // *NOPAD*
            return buffer_.contents();
        }

    private:
        memory_streambuf buffer_;
};
