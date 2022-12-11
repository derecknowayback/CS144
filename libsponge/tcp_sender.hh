#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>
#include <deque>

class RetryTimer{
  private:
    size_t time_to_expire{0};
    size_t rto;
    bool _is_stop{true};

  public:
    RetryTimer(size_t retx_timeout):rto(retx_timeout){}

    //! 启动时间
    void start(){
      time_to_expire = rto;
      _is_stop = false;
    }

    //! 设置rto超时时间
    void set_rto(size_t init){rto = init;}

    //! 两倍延长RTO
    void double_rto(){
      if(rto > UINT64_MAX / 2) rto = UINT64_MAX;
      else rto = rto * 2; 
    }

    //! 重置
    void reset(){time_to_expire = rto;}

    //! 减少时间，这里时间过大了不知道会发生什么
    void time_pass(const size_t ms_since_last_tick){
      // 如果钟还没有开启，就不开
      if(_is_stop) return;
      if(ms_since_last_tick > time_to_expire){
        time_to_expire = 0;
        return;
      } 
      time_to_expire -= ms_since_last_tick;
    }

    //! 判断是否过期
    bool isExpired(){
      return time_to_expire == 0;
    }

    // stop ??? 什么叫stop
    void stop(){
      _is_stop = true;
    }

    bool is_stop(){
      return _is_stop;
    }


};




//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 _isn;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> _segments_out{};

    //! retransmission timer for the connection
    unsigned int _initial_retransmission_timeout;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream _stream;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t _next_seqno{0};

    //! 字节流已经确认到哪里了
    uint64_t _pre_ack{0};

    //! 维护接收窗口的右边界，控制发送窗口发送的频率
    uint64_t _right_max{0};

    //! 因为只有最早的片段会重传，所以只需要一个数来记录就好了
    unsigned int _consecutive_retransmissions{0};

    // 计时器
    RetryTimer _timer;

    // 支持随机访问的双端队列，适合我们使用
    std::deque<std::pair<size_t,TCPSegment>> _outstanding{};

    // 看是否发送过了SYN
    bool _has_sent_SYN{false};

    // 看是否发送了FIN
    bool _has_sent_FIN{false};

    // 计算未被ack的sequence数量
    uint64_t _bytes_in_flight{0};

    // 要不要增加rto  (专门应对 window == 0 的情况)
    bool _should_backoff_rto{true};


  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return _stream; }
    const ByteStream &stream_in() const { return _stream; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    size_t segment_size(){return _segments_out.size();}

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return _segments_out; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return _next_seqno; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(_next_seqno, _isn); }
    //!@}
};


#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
