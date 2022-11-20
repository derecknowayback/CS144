#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // todo 初始化isn
    // 先检查 syn fin (可能会出现 syn、fin、payload出现在一个segment中的情况)
    TCPHeader header = seg.header();
    WrappingInt32 seqno = header.seqno;
    bool syn = header.syn;
    bool fin = header.fin;

    if(syn) {
        _has_received_syn = true;
        _syn = seqno.raw_value();
    }
   
    if(_has_received_syn){
        uint64_t checkpoint = _reassembler.get_expected_index(); // 这里不用担心 0 或者 要不要-1 的问题
        uint64_t abs_index = unwrap(seqno,WrappingInt32(_syn),checkpoint); // 拿到在absoluteseq
    
        // 现在开始计算在流中的位置，如果payload和syn一起来的，那index就是1，否则就要 -1 (不用担心溢出，溢出是正确的)
        uint64_t index = seqno.raw_value() == _syn ? 0 : abs_index - 1;

        // 真卑鄙的case，数据流会出错？要一个特判?
        /**
         * 
            Failure message:
	        The TCPReceiver stream reported `1` bytes written, but there was expected to be `0` bytes written (in total)
            List of steps that executed successfully:
	        Initialized with (capacity=4)
	        Action:      segment arrives Header(flags=S,seqno=23452,ack=0,win=0)
	        Action:      segment arrives Header(flags=,seqno=23452,ack=0,win=0) with data "a"
            The TCPReceiver stream reported `1` bytes written, but there was expected to be `0` bytes written (in total)
        */
        if(seqno.raw_value() == _syn && syn == false) return;


        // 把数据放到 重组器里， 不要写 syn 和 fin
        _reassembler.push_substring(seg.payload().copy(),index,fin);
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
     if(_has_received_syn){
        // 如果到流的终点了
        if(_reassembler.get_expected_index() == _reassembler.get_last_bytes())
            return wrap(_reassembler.get_expected_index() + 2,WrappingInt32(_syn));
        return wrap(_reassembler.get_expected_index() + 1,WrappingInt32(_syn));
     }else{
        return {}; 
     }
}

size_t TCPReceiver::window_size() const {
    // 如果还没有收到 syn，那么就返回_capacity
    if(!_has_received_syn) return _capacity;
    size_t first_unassembled = _reassembler.get_expected_index();
    size_t first_unacceptable = _reassembler.get_unacceptable_index();
    /** 
     * Returns the distance between the “first unassembled” index (the index corresponding to the ackno) 
     * and the “first unacceptable” index.
     * 没有看懂啊，这样子是不需要计算 乱序缓存的字符串 吗？ 感觉不合理，是不是要返回 重组器的 useful?
     */
    return first_unacceptable - first_unassembled;
}
