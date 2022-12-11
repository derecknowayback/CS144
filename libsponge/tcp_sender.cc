#include "tcp_sender.hh"

#include "tcp_config.hh"
#include <iostream>

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    ,_timer(retx_timeout){}



/**
 * How many sequence numbers are occupied by segments sent but not yet acknowledged?
 *  SYN 和 FIN 也算
 */
uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_flight;
 }



void TCPSender::fill_window() {
    
    if(_has_sent_FIN) return;
    // !The test "Immediate writes respect the window" failed
    // 发送的时候要比对窗口
    size_t window = _right_max - _next_seqno + 1;

    while(window > 0){
        size_t length = TCPConfig::MAX_PAYLOAD_SIZE > window
                            ? window : TCPConfig::MAX_PAYLOAD_SIZE;

        TCPSegment segment;

         
        // 处理SYN，如果没有发送过SYN,那么这一段铁定是会有SYN 
        if(!_has_sent_SYN){
            // 看看窗口够不够大，够的话不用-1，容得下SYN，不够的话要减1,使得 SYN+payload = 窗口大小
           if(window == length)
                length -= 1;
            segment.header().syn = true;
            _has_sent_SYN = true;
        }


        // 处理FIN,我要判断这一段是否包含了FIN
        // 如果 length可发送的长度 > 字节流中的字节数 ,那么我们就发送
        // 而且这个发完之后,就没有数据了
        if(_stream.input_ended() && length >= _stream.buffer_size() && !_has_sent_FIN){
                bool b1 = _stream.buffer_size() == 0 && window == length;
                bool b2 = window > _stream.buffer_size();
                if(b1 || b2){
                    segment.header().fin = true;
                    _has_sent_FIN = true;
                }    
        }
        string data = _stream.read(length);


        // 只有 有payload 或者 syn要发送 才会push到 输出队列里 
        if(data.length() > 0 || segment.header().syn || segment.header().fin){
            if(data.length()) segment.payload() = Buffer(move(data)); // 设置载荷
            segment.header().seqno = wrap(_next_seqno,_isn);
            _segments_out.push(segment); // 发送

            pair <size_t,TCPSegment> newcome(_next_seqno,segment);
            _outstanding.push_back(newcome); // 加报文段加入到 "待确认" 队列中
        

            size_t no_ack = segment.length_in_sequence_space();
            _bytes_in_flight += no_ack; // 更新未ack的字节数
            _next_seqno += no_ack; 
            window = _right_max - _next_seqno + 1;
            // 如果计时器没有启动的话，启动计时器
            if(_timer.is_stop())
                _timer.start();
        }else{
            break;
        }
    }
}




//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 先拆箱获得_next_seqno数据,使用unwrap(我觉得checkpoint选择next_seqno是正确的,
    // 如果一个窗口有两个值出现,那就是出现了经典的“窗口过小”错误)
    size_t new_ack = unwrap(ackno,_isn,_next_seqno);

    // "Impossible ackno (beyond next seqno) is ignored" 
    // (那过早的ack也应该忽视,比如我已经 ack-10 了，你再 ack-3 ，毫无意义，因为根据累计确认，ack-10一定是在ack-3后)
    if(new_ack > _next_seqno || new_ack < _pre_ack) return;


    // 更新窗口的大小
    if(window_size == 0){
        _right_max = new_ack;
        // 窗口为0的时候，右边界不变，但是这时候我们不应该加倍rto
        _should_backoff_rto = false;
    } 
    else{
        _right_max = new_ack + window_size - 1;
        _should_backoff_rto = true;
    }
        
    // 接下来做两件事
    // 1. 清除已经ack的数据
    // 2. 发送需要的数据(这里倒不一定是重发)

    bool ack_has_changed = false;

    // 如果next_seqno变了，说明有新的数据被ack了
    if(new_ack != _pre_ack){
        _pre_ack = new_ack;
        // 重置 rto ，但是先不启动
        _timer.set_rto(_initial_retransmission_timeout);
        // 重置 "重传次数"为0 
        _consecutive_retransmissions = 0;
        ack_has_changed = true;
    }

    
    // 确认-清除
    // 确认的标准:  fully acknowledged—all of the sequence numbers it occupies are less than the ackno.
    while(!_outstanding.empty()){
        pair<size_t,TCPSegment> temp = _outstanding.at(0);
        TCPSegment segment = temp.second;
        size_t seqno = temp.first, len = segment.length_in_sequence_space();
        // segment全部被ack了
        if(seqno + len - 1 < new_ack){
            _outstanding.pop_front();
            _bytes_in_flight -= len;
        }else{
            break;
        }
    }
    

    // 发送需要的数据
    // 判断是重发还是发送新的
    // 这里 "==" 不是很严谨，但是我们还是这么做了
    // ack_has_changed: 只有 新的待发段落 才会启动timer
    if(!_outstanding.empty() && ack_has_changed){
        // 启动timer
        _timer.start();
        // 这里不需要基于ack的快速重传，不用重传，只需要启动timer
       // 不应该pop,只有ack来了才能pop 
    }
    // 如果不是重发，那么就是发新的，和fill_window逻辑一样，直接调用
    else if(_outstanding.empty()){
        _timer.stop();
    }
    // 小心，fill_window并没有重启timer???
    // fill_window();
    // 有一个问题，我们在这里需不需要更新window呢? 我觉得是不用的
}




//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {

    
    // 如果时钟已经关闭了，啥都不做???
    if(_timer.is_stop()) return;
    _timer.time_pass(ms_since_last_tick);
    
    // 如果已经过期了，那么我们要
    // 1. 出队已经确认的报文段(这件事不要在这里做啊，这个功能应该在ack_received中做)
    // 2. 重传最老的
    //  2.1 更新重传次数 d
    //  2.2 加倍RTO(重传的时间间隔) d
    //  2.3 重置timer，开始新一轮计时 d
       
    if(_timer.isExpired()){

        // 重传最老的未确认segment(无条件): 
        // If the window size is nonzero: 重传的时候看上去不需要考虑窗口的大小
        // 为什么上面Push的时候不需要考虑window，push完之后需要考虑
        if(!_outstanding.empty())
            _segments_out.push(_outstanding.at(0).second);


        // 重传次数 ++
        _consecutive_retransmissions++;

        // 翻倍 RTO，并启动
        // 要判断是不是需要 double_rto
        if(_should_backoff_rto)
            _timer.double_rto();
        // 这里重新使用 reset,而不是 start
        _timer.reset();
    }
 }




/**记录连续重传的次数*/
unsigned int TCPSender::consecutive_retransmissions() const { 
    return _consecutive_retransmissions; 
}



void TCPSender::send_empty_segment() {
    TCPSegment segment;
    // uint64_t right_no = _next_seqno ; 
    // 这里要发送的应该是 _next_seqno，但是_next_seqno是后面的还是前面的???
    // 从lab4的文档知道，这里要发送的不是
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}