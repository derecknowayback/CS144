#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    // return _sender.stream_in().buffer_size(); 
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }


void TCPConnection::transport(){
    // cerr << endl << "transport-ing " << endl;
    size_t win = _receiver.window_size();
    optional<WrappingInt32> ack = _receiver.ackno();
     // 如果已经决定abandon了，那么pop掉前面所有的段落
    while(!_sender.segments_out().empty()){
        auto temp = _sender.segments_out().front();
        _sender.segments_out().pop();
        if(ack.has_value()){
            temp.header().ack = true;
            temp.header().ackno = ack.value();
        }
        // windows 不应该和 ack捆绑
        size_t max_win = numeric_limits<uint16_t>().max();
        temp.header().win = min(win, max_win);
        _segments_out.push(temp);
    }
}

void TCPConnection::connect() {
    // cerr << endl << "connect called " << endl;
    //! 如果是 closed,那么就建立连接
    // if(_sender.next_seqno_absolute() == 0){
        _sender.fill_window();
        transport();
    // }
}

void TCPConnection::segment_received(const TCPSegment &seg) {
if (!active()) return;

     // 别忘了 重置时间
    _time_since_last_segment_received = 0;

    TCPHeader header = seg.header();

    bool is_listen = _sender.next_seqno_absolute() == 0 && !_receiver.ackno().has_value();

    //! in LISTEN, send ACK ->  any ACK should result in a RST 
    if (header.rst || (is_listen && header.ack)) {
        //! \brief all RSTs should be ignored in LISTEN 这是 严格&relaxed 测试的条件
        //! \bug any ACK should result in a RST  这是 严格测试 的条件
        // 但是这里却没有，很奇怪（应该是只测试relaxed）
        if(!is_listen){
            _linger_after_streams_finish = false;
            _has_rcvsd_RST = true;
            //! \bug 怎么设置inputstream的error啊,利用ByteStream的api
            _receiver.stream_out().set_error();
            _sender.stream_in().set_error();
            // good ACK with RST should result in a RESET but no RST segment sent
            if(!(_sender.next_seqno() == header.ackno))
                set_rst(true);
        } 
    }else if(_receiver.ackno().has_value() and (seg.length_in_sequence_space() == 0) and seg.header().seqno == _receiver.ackno().value() - 1){
        if(header.ack){
            _sender.ack_received(header.ackno, header.win);
            //! 如果这里没有ack,那么我们就不会告知下层sender windows的大小
        }
        _sender.send_empty_segment();
        transport();
    }else{
        // 给receiver
        _receiver.segment_received(seg);

        // 如果该报文段有 ack
        if(header.ack){
            _sender.ack_received(header.ackno, header.win);
            //! 如果这里没有ack,那么我们就不会告知下层sender windows的大小
        }
        // 因为更新了窗口，所以尽力fill_window()
        _sender.fill_window();
        transport(); 

        // 如果这个segment有占据 "序列号"，那么就要 “确保”发送一个segment，返回ack和window
        if(seg.length_in_sequence_space() != 0){
            uint16_t win = _receiver.window_size();
            optional<WrappingInt32> ack = _receiver.ackno();
            //! \bug 这里可能会出错，到底是确保第一个是最新的ack和win，
            //! 还是直接new 一个segment压到队列中呢
            if(ack.has_value()){
                // 如果输出队列为空，就压入一个segment    
                if (_sender.segment_size() == 0 && _segments_out.empty()) {
                    _sender.send_empty_segment();
                }
                transport();
                // 如果输出队列不为空，那么直接修改第一个就好了
                _segments_out.front().header().ack = true;
                _segments_out.front().header().ackno = ack.value();
                _segments_out.front().header().win = win;
            }
        }
        
        //  If the inbound stream ends before the TCPConnection has reached EOF on its outbound stream, 
        //  _linger_after_streams_finish needs to be set to false.
        if(_receiver.stream_out().input_ended() && !_sender.stream_in().eof())
            _linger_after_streams_finish = false;
    }
}

void TCPConnection::set_rst(bool need_rst){
    _has_rcvsd_RST = true;
    // 清空可能要发送的数据包
    while(!_sender.segments_out().empty()) _sender.segments_out().pop();
    while(!_segments_out.empty()) _segments_out.pop();
    // 发送RST
    if (need_rst) {
        TCPSegment rst_seg;
        rst_seg.header().rst = true;
        _segments_out.push(rst_seg);
    }
    _receiver.stream_out().set_error();
    _sender.stream_in().set_error();
    _linger_after_streams_finish = false;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // 增加时间
    _time_since_last_segment_received += ms_since_last_tick;
    _sender.tick(ms_since_last_tick);
    if(_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS){
        set_rst(true);
        return;
    }
    transport();
    bool fin_recv = _receiver.stream_out().input_ended();
    bool fin_acked = _sender.stream_in().eof() and _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 and _sender.bytes_in_flight() == 0;
    bool clean = fin_recv && fin_acked;
    // 只有发完了，超时才会终结 _linnger_after_streams_finish
    if(clean && _time_since_last_segment_received >= _linger_time){
        _linger_after_streams_finish = false;
    }    
}

size_t TCPConnection::write(const string &data) {
    size_t res = _sender.stream_in().write(data);
    _sender.fill_window();
    transport();
    return res;
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    // 结束流完之后要立马通知sender发
    _sender.fill_window();
    transport();
}

bool TCPConnection::active() const {
    // 1. 输入流重组完成并且结束
    // 2. 输出流结束并且完整发送
    // 3. 输出流已经全部被ack了
    bool fin_recv = _receiver.stream_out().input_ended();
    bool fin_acked = _sender.stream_in().eof() and _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 and _sender.bytes_in_flight() == 0;
    bool clean = fin_recv && fin_acked;
    return !(_has_rcvsd_RST || (clean && !_linger_after_streams_finish)); 
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            set_rst(false);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}