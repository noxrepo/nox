#ifndef NETWORK_IARCHIVE_HH
#define NETWORK_IARCHIVE_HH

#include <vector>
#include <boost/asio/streambuf.hpp>
#include <boost/type_traits.hpp>
#include <boost/archive/archive_exception.hpp>
#include <boost/archive/detail/common_iarchive.hpp>
#include <boost/archive/detail/iserializer.hpp>
#include <boost/archive/detail/polymorphic_iarchive_route.hpp>
#include <boost/archive/detail/register_archive.hpp>
#include <boost/detail/endian.hpp>
#include <boost/mpl/bool.hpp>
#include <boost/serialization/array.hpp>
#include <boost/serialization/is_bitwise_serializable.hpp>

#include "network_archive.hh"

namespace vigil
{
class network_iarchive
    //: public boost::archive::detail::common_iarchive<network_iarchive>
{
public:
    typedef boost::mpl::bool_<true> is_loading;
    typedef boost::mpl::bool_<false> is_saving;
    //typedef boost::archive::detail::common_iarchive<network_iarchive> archive_base_t;
    //friend class boost::archive::detail::common_iarchive<network_iarchive>;
    //friend class boost::archive::detail::interface_iarchive<network_iarchive>;
    friend class boost::archive::load_access;

    struct use_array_optimization
    {
        template <class T> struct apply :
            public boost::serialization::is_bitwise_serializable<T> {};
        //public boost::is_same<T, uint8_t> {};
    };
    template <class ValueType>
    void load_array(boost::serialization::array<ValueType>& a, unsigned int)
    {
        load_binary(a.address(), a.count()*sizeof(ValueType));
    }
    void load_binary(void* address, std::size_t count)
    {
        m_sb.sgetn(static_cast<char*>(address), count);
        //__builtin_memcpy((char*)address, boost::asio::buffer_cast<const char*>(m_sb.data()), count);
        //m_sb.consume(count);
    }
    // default fall through for any types not specified here
    template<class T>
    void load(T& t)
    {
        BOOST_STATIC_ASSERT(sizeof(T) == 0);
        char* cptr = reinterpret_cast<char*>(& t);
        load_binary(cptr, sizeof(T));
#ifdef BOOST_LITTLE_ENDIAN
        if (sizeof(T) != 1)
        {
            reverse_bytes(sizeof(T), cptr);
        }
#endif
    }
    template<class T>
    void load_override(T& t, int)
    {
        //this->archive_base_t::load_override(t, 0);
        t.serialize(*this, 0);
        //boost::serialization::serialize_adl(
        //    *this, t, 0);
        //BOOST_STATIC_ASSERT(sizeof(T) == 0);
        //this->archive_base_t::load_override(t, 0);

    }
    // TODO: only for is_wrapper
    template<class T>
    void load_override(const T & t, int)
    {
        const_cast<T&>(t).serialize(*this, 0);
    }    
    void load_override(uint8_t& t, int)
    {
        load_binary(&t, 1);
    }
    void load_override(uint16_t& t, int)
    {
        load_binary(&t, 2);
#ifdef BOOST_LITTLE_ENDIAN
        t = bswap_16(t);
#endif
    }
    void load_override(uint32_t& t, int)
    {
        load_binary(&t, 4);
#ifdef BOOST_LITTLE_ENDIAN
        t = bswap_32(t);
#endif
    }
    void load_override(uint64_t& t, int)
    {
        load_binary(&t, 8);
#ifdef BOOST_LITTLE_ENDIAN
        t = bswap_64(t);
#endif
    }

    void load_override(boost::archive::object_id_type&, int) {}
    void load_override(boost::archive::object_reference_type&, int) {}
    void load_override(boost::archive::version_type&, int) {}
    void load_override(boost::archive::class_id_type&, int) {}
    void load_override(boost::archive::class_id_optional_type&, int) {}
    void load_override(boost::archive::class_id_reference_type&, int) {}
    void load_override(boost::archive::class_name_type&, int) {}
    void load_override(boost::archive::tracking_type&, int) {}

    template<class T>
    network_iarchive& operator>>(T & t) {
        load_override(t, 0);
        return *this;
    }

    template<class T>
    network_iarchive& operator&(T & t) {
        return *this >> t;
    }

public:
    network_iarchive(boost::asio::streambuf& sbuf)
        //: archive_base_t(boost::archive::no_header | boost::archive::no_codecvt | endian_big),
        : m_sb(sbuf)
    {
    }

private:
    //std::vector<char> & v;
    boost::asio::streambuf& m_sb;
};

typedef boost::archive::detail::polymorphic_iarchive_route<network_iarchive> polymorphic_network_iarchive;
} // namespace vigil

//BOOST_SERIALIZATION_REGISTER_ARCHIVE(vigil::network_iarchive)
//BOOST_SERIALIZATION_REGISTER_ARCHIVE(vigil::polymorphic_network_iarchive)

#endif // NETWORK_IARCHIVE_HH
