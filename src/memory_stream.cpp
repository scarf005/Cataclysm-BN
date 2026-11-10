#include "memory_stream.h"

#include <utility>

memory_streambuf::memory_streambuf( std::string contents ) : data_( std::move( contents ) )
{
    setg( data_.data(), data_.data(), data_.data() + data_.size() );
}

auto memory_streambuf::seekoff( const off_type off, const std::ios_base::seekdir dir,
                                const std::ios_base::openmode which ) -> pos_type
{
    if( !( which & std::ios_base::in ) ) {
        return pos_type( off_type( -1 ) );
    }
    const auto base = [&]() -> off_type {
        switch( dir )
        {
            case std::ios_base::beg:
                return 0;
            case std::ios_base::cur:
                return gptr() - eback();
            case std::ios_base::end:
                return egptr() - eback();
            default:
                return -1;
        }
    }();
    const auto target = base + off;
    if( base < 0 || target < 0 || target > egptr() - eback() ) {
        return pos_type( off_type( -1 ) );
    }
    setg( eback(), eback() + target, egptr() );
    return pos_type( target );
}

auto memory_streambuf::seekpos( const pos_type pos,
                                const std::ios_base::openmode which ) -> pos_type
{
    return seekoff( off_type( pos ), std::ios_base::beg, which );
}

memory_istream::memory_istream( std::string contents ) : std::istream( nullptr ),
    buffer_( std::move( contents ) )
{
    rdbuf( &buffer_ );
}
