#ifndef NETWORK_OARCHIVE_HH
#define NETWORK_OARCHIVE_HH

#include <vector>
#include <boost/asio/streambuf.hpp>
#include <boost/type_traits.hpp>
#include <boost/archive/archive_exception.hpp>
#include <boost/archive/detail/common_oarchive.hpp>
#include <boost/archive/detail/polymorphic_oarchive_route.hpp>
#include <boost/archive/detail/register_archive.hpp>
#include <boost/detail/endian.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>

#include "network_archive.hh"

namespace vigil
{
class network_oarchive
    : public boost::archive::detail::common_oarchive<network_oarchive>
{
public:
    typedef boost::archive::detail::common_oarchive<network_oarchive> archive_base_t;
    friend class boost::archive::detail::common_oarchive<network_oarchive>;
    friend class boost::archive::detail::interface_oarchive<network_oarchive>;
    friend class boost::archive::save_access;

    struct use_array_optimization
    {
        template <class T> struct apply :
            public boost::serialization::is_bitwise_serializable<T> {};
        //public boost::is_same<T, uint8_t> {};
    };
    template <class ValueType>
    void save_array(boost::serialization::array<ValueType> const& a, unsigned int)
    {
        save_binary(a.address(), a.count()*sizeof(ValueType));
    }
    void save_binary(const void* address, std::size_t count)
    {
        m_sb.sputn(static_cast<const char*>(address), count);
        //__builtin_memcpy(boost::asio::buffer_cast<char*>(m_sb.prepare(count)), (char*)address, count);
        //m_sb.commit(count);
    }
    // default fall through for any types not specified here
    template<class T>
    void save(const T& t)
    {
        BOOST_STATIC_ASSERT(sizeof(T) == 0);
        boost::intmax_t l = t;
        char* cptr = reinterpret_cast<char*>(&l);
#ifdef BOOST_LITTLE_ENDIAN
        if (sizeof(T) != 1)
        {
            reverse_bytes(sizeof(T), cptr);
        }
#endif
        save_binary(cptr, sizeof(T));
    }

    template<class T>
    void save_override(const T& t, int)
    {
        this->archive_base_t::save_override(t, 0);
    }
    void save_override(const uint8_t& t, int)
    {
        save_binary(&t, 1);
    }
    void save_override(const uint16_t& t, int)
    {
#ifdef BOOST_LITTLE_ENDIAN
        uint16_t y = bswap_16(t);
        save_binary(&y, 2);
#else
        save_binary(&t, 2);
#endif
    }
    void save_override(const uint32_t& t, int)
    {
#ifdef BOOST_LITTLE_ENDIAN
        uint32_t y = bswap_32(t);
        save_binary(&y, 4);
#else
        save_binary(&t, 4);
#endif
    }
    void save_override(const uint64_t& t, int)
    {
#ifdef BOOST_LITTLE_ENDIAN
        uint64_t y = bswap_64(t);
        save_binary(&y, 8);
#else
        save_binary(&t, 8);
#endif
    }


    void save_override(const boost::archive::object_id_type&, int) {}
    void save_override(const boost::archive::object_reference_type&, int) {}
    void save_override(const boost::archive::version_type&, int) {}
    void save_override(const boost::archive::class_id_type&, int) {}
    void save_override(const boost::archive::class_id_optional_type&, int) {}
    void save_override(const boost::archive::class_id_reference_type&, int) {}
    void save_override(const boost::archive::class_name_type&, int) {}
    void save_override(const boost::archive::tracking_type&, int) {}

public:
    network_oarchive(boost::asio::streambuf& sbuf)
        : archive_base_t(boost::archive::no_header | boost::archive::no_codecvt | endian_big),
          m_sb(sbuf)
    {
    }

private:
    //std::vector<char> & v;
    boost::asio::streambuf& m_sb;
};

typedef boost::archive::detail::polymorphic_oarchive_route <
network_oarchive > polymorphic_network_oarchive;
} // namespace vigil

BOOST_SERIALIZATION_REGISTER_ARCHIVE(vigil::network_oarchive)
BOOST_SERIALIZATION_REGISTER_ARCHIVE(vigil::polymorphic_network_oarchive)

#endif // NETWORK_OARCHIVE_HH
