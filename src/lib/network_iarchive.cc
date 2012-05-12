#include "network_iarchive.hh"

#include <boost/archive/impl/archive_serializer_map.ipp>

namespace boost
{
namespace archive
{
namespace detail
{
template class archive_serializer_map<vigil::network_iarchive>;
template class archive_serializer_map<vigil::polymorphic_network_iarchive>;
} // namespace detail
} // namespace archive
} // namespace boost
