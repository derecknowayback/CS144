#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();
    EthernetFrame frame;
    EthernetHeader header;
    // 先判断 mapping 里面有没有对应 next_hop_ip 物理地址: 
    // 如果mapping有
    if(_arp_table.find(next_hop_ip) != _arp_table.end()){
        header.type = EthernetHeader::TYPE_IPv4;
        // If the destination Ethernet address is already known, send it right away. Create
        // an Ethernet frame (with type = EthernetHeader::TYPE IPv4), set the payload to
        // be the serialized datagram, and set the source and destination addresses
        frame.payload() = dgram.serialize();
        header.type = EthernetHeader::TYPE_IPv4;
        header.dst = _arp_table.find(next_hop_ip)->second.eth_addr;
    }
    // 如果mapping没有
    else{
        // If the destination Ethernet address is unknown, broadcast an ARP request for the
        // next hop’s Ethernet address, and queue the IP datagram so it can be sent after
        // the ARP reply is received.

        // You don’t want to flood the network with ARP requests. If the network
        // interface already sent an ARP request about the same IP address in the last
        // five seconds, don’t send a second request—just wait for a reply to the first one.

        //! \attention 这里还要注意: and queue the IP datagram so it can be sent after the ARP reply is received.
        pair<Address, InternetDatagram> k(next_hop,dgram);
        _waiting_arp_internet_datagrams.push_back(k);

        if(_waiting_arp_response_ip_addr.find(next_hop_ip) == _waiting_arp_response_ip_addr.end()){
            header.type = EthernetHeader::TYPE_ARP;
            header.dst = ETHERNET_BROADCAST;
            ARPMessage arp_message;
            arp_message.opcode = ARPMessage::OPCODE_REQUEST;
            //! \bug 这里有bug: 我还不清楚src是写自己的还是写dgram里面的, 但是我觉得泛洪这里应该是写自己的
            arp_message.sender_ip_address = _ip_address.ipv4_numeric();
            arp_message.sender_ethernet_address = _ethernet_address;
            arp_message.target_ip_address = next_hop_ip;
            frame.payload() = BufferList(arp_message.serialize());
            //! \bug 这里要加入_waiting_arp 
            _waiting_arp_response_ip_addr[next_hop_ip] = _arp_request_default_ttl; // 加入_waiting_arp 
        }else{
            return;
        }       
    }
    // 我还不清楚src是写自己的还是写dgram里面的 -> 应该是写自己的
    header.src = _ethernet_address; 
    frame.header() = header;
    _frames_out.push(frame);
}

//! \param[in] frame the incoming Ethernet frame
//! \note Receives an Ethernet frame and responds appropriately.
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    const EthernetHeader header = frame.header();
    BufferList bufferlist = frame.payload(); // 第一个是链路层的头，第二个是链路层的payload 也就是ip层的整个报文
    //! \bug 好像不需要？？？
    // bufferlist.remove_prefix(EthernetHeader::LENGTH); // 移除了链路层的头 
    
    // 过滤掉不是发往当前位置的包
    if (frame.header().dst != _ethernet_address && frame.header().dst != ETHERNET_BROADCAST)
        return nullopt;

    // If type is IPv4, returns the datagram. 
    if(header.type == EthernetHeader::TYPE_IPv4){
        // 剩下来的是 IPV4 的数据报
        InternetDatagram res;
        ParseResult parseresult = res.parse(bufferlist);
        if(parseresult != ParseResult::NoError){
           return {};
        }
        return res; // 这里不需要判断ip地址，现在是链路层协议，我们不管ip
    }
    else{
        ARPMessage arp_msg;
        if (arp_msg.parse(frame.payload()) != ParseResult::NoError)
            return nullopt;        
        
        const uint32_t &src_ip_addr = arp_msg.sender_ip_address;
        const uint32_t &dst_ip_addr = arp_msg.target_ip_address;
        const EthernetAddress &src_eth_addr = arp_msg.sender_ethernet_address;
        const EthernetAddress &dst_eth_addr = arp_msg.target_ethernet_address;
        // 如果是一个发给自己的 ARP 请求
        bool is_valid_arp_request =
            arp_msg.opcode == ARPMessage::OPCODE_REQUEST && dst_ip_addr == _ip_address.ipv4_numeric();
        bool is_valid_arp_response = arp_msg.opcode == ARPMessage::OPCODE_REPLY && dst_eth_addr == _ethernet_address;
        if (is_valid_arp_request) {
            ARPMessage arp_reply;
            arp_reply.opcode = ARPMessage::OPCODE_REPLY;
            arp_reply.sender_ethernet_address = _ethernet_address;
            arp_reply.sender_ip_address = _ip_address.ipv4_numeric();
            arp_reply.target_ethernet_address = src_eth_addr;
            arp_reply.target_ip_address = src_ip_addr;

            EthernetFrame eth_frame;
            eth_frame.header() = {/* dst  */ src_eth_addr,
                                  /* src  */ _ethernet_address,
                                  /* type */ EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_reply.serialize();
            _frames_out.push(eth_frame);
        }
        // 否则是一个 ARP 响应包
        //! NOTE: 我们可以同时从 ARP 请求和响应包中获取到新的 ARP 表项
        if (is_valid_arp_request || is_valid_arp_response) {
            _arp_table[src_ip_addr] = {_arp_entry_default_ttl,src_eth_addr};
            // 将对应数据从原先等待队列里删除
            for (auto iter = _waiting_arp_internet_datagrams.begin(); iter != _waiting_arp_internet_datagrams.end();
                 /* nop */) {
                if (iter->first.ipv4_numeric() == src_ip_addr) {
                    // cerr << "src_ip_addr: " <<  src_ip_addr << endl << endl;
                    send_datagram(iter->second, iter->first);
                    iter = _waiting_arp_internet_datagrams.erase(iter);
                } else
                    ++iter;
            }
            _waiting_arp_response_ip_addr.erase(src_ip_addr);
        }
        //! \bug 这里有点奇怪，为什么只会有这两种情况： 出发地和目的地，不会有中转站的情况吗？如果是中转站不是要继续向下一跳发送ARP？
    }
    return {};
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) {
    // 清除过期的 ARP Request
    for (auto iter = _waiting_arp_response_ip_addr.begin(); iter != _waiting_arp_response_ip_addr.end() ; /*nop*/){
        size_t remain = iter->second;
        if(ms_since_last_tick >= remain){
            //! \bug 这里还是要重新发送ARP请求
            // 重新发送 ARP 请求
            ARPMessage arp_request;
            arp_request.opcode = ARPMessage::OPCODE_REQUEST;
            arp_request.sender_ethernet_address = _ethernet_address;
            arp_request.sender_ip_address = _ip_address.ipv4_numeric();
            arp_request.target_ethernet_address = {/* 这里应该置为空*/};
            arp_request.target_ip_address = iter->first;

            EthernetFrame eth_frame;
            eth_frame.header() = {/* dst  */ ETHERNET_BROADCAST,
                                  /* src  */ _ethernet_address,
                                  /* type */ EthernetHeader::TYPE_ARP};
            eth_frame.payload() = arp_request.serialize();
            _frames_out.push(eth_frame);
            iter->second = _arp_request_default_ttl;
        }else{
            iter->second -= ms_since_last_tick;
            iter++;
        }
    }
    
    // 清除过期的 ARP Entry
    for (auto iter = _arp_table.begin(); iter != _arp_table.end(); /*nop*/){
        size_t remain = iter->second.ttl;
        if(ms_since_last_tick >= remain){
            iter = _arp_table.erase(iter);
        }else{
            iter->second.ttl -= ms_since_last_tick;
            iter++;
        }
    }
}
