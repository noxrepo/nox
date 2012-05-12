#include "network_oarchive.hh"

#include <boost/archive/impl/archive_serializer_map.ipp>

namespace boost
{
namespace archive
{
namespace detail
{
template class archive_serializer_map<vigil::network_oarchive>;
template class archive_serializer_map<vigil::polymorphic_network_oarchive>;
} // namespace detail
} // namespace archive
} // namespace boost
