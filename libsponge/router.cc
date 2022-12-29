#include "router.hh"

#include <iostream>

using namespace std;

// Dummy implementation of an IP router

// Given an incoming Internet datagram, the router decides
// (1) which interface to send it out on, and
// (2) what next hop address to send it to.

// For Lab 6, please replace with a real implementation that passes the
// automated checks run by `make check_lab6`.

// You will need to add private members to the class declaration in `router.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

//! \param[in] route_prefix The "up-to-32-bit" IPv4 address prefix to match the datagram's destination address against
//! \param[in] prefix_length For this route to be applicable, how many high-order (most-significant) bits of the route_prefix will need to match the corresponding bits of the datagram's destination address?
//! \param[in] next_hop The IP address of the next hop. Will be empty if the network is directly attached to the router (in which case, the next hop address should be the datagram's final destination).
//! \param[in] interface_num The index of the interface to send the datagram out on.
void Router::add_route(const uint32_t route_prefix,
                       const uint8_t prefix_length,
                       const optional<Address> next_hop,
                       const size_t interface_num) {
    cerr << "DEBUG: adding route " << Address::from_ipv4_numeric(route_prefix).ip() << "/" << int(prefix_length)
         << " => " << (next_hop.has_value() ? next_hop->ip() : "(direct)") << " on interface " << interface_num << "\n";
    // Your code here.
    _route_entries.push_back({route_prefix,prefix_length,next_hop,interface_num});
}

//! \param[in] dgram The datagram to be routed
void Router::route_one_datagram(InternetDatagram &dgram) {
    // Your code here.
    // The Router searches the routing table to find the routes that match the datagram’s
    // destination address. By “match,” we mean the most-significant prefix length bits of
    // the destination address are identical to the most-significant prefix length bits of the
    // route prefix.
    // • Among the matching routes, the router chooses the route with the biggest value of
    // prefix length. This is the longest-prefix-match route.
    // • If no routes matched, the router drops the datagram.
    // • The router decrements the datagram’s TTL (time to live). If the TTL was zero already,
    // or hits zero after the decrement, the router should drop the datagram.
    // • Otherwise, the router sends the modified datagram on the appropriate interface
    // ( interface(interface num).send datagram() ) to the appropriate next hop.

    // 这里 interface是 unsigned,但是我们还是使用 -1去作为不可能的值
    auto max_matched_entry = _route_entries.end();
    const uint32_t next_hop = dgram.header().dst;
    for(auto iter = _route_entries.begin(); iter != _route_entries.end(); iter++){ 
        //! \bug 这里要特殊判断 prefix == 0, arp广播的情况
        if((iter->route_prefix ^ next_hop) >> (32 - iter->prefix_length) == 0 || iter->prefix_length == 0){
            if(max_matched_entry == _route_entries.end() || max_matched_entry->prefix_length < iter->prefix_length){
                max_matched_entry = iter;
            }
        }
    }
    if(max_matched_entry == _route_entries.end()) return;
    if(dgram.header().ttl <= 1) return;
    dgram.header().ttl--;
    if(max_matched_entry->next_hop.has_value()) interface(max_matched_entry->interface_idx ).send_datagram(dgram,max_matched_entry->next_hop.value());
    else interface(max_matched_entry->interface_idx ).send_datagram(dgram,Address::from_ipv4_numeric(next_hop));
}

void Router::route() {
    // Go through all the interfaces, and route every incoming datagram to its proper outgoing interface.
    for (auto &interface : _interfaces) {
        auto &queue = interface.datagrams_out();
        while (not queue.empty()) {
            route_one_datagram(queue.front());
            queue.pop();
        }
    }
}