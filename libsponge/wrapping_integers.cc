#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t _isn = isn.raw_value();
    uint64_t mod = 1ull << 32;
    uint32_t raw_value = (n + _isn) % mod;    
    return WrappingInt32(raw_value);
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    // here seems buggy ! INT32_MAX or INT32_MAX + 1
    uint64_t k = checkpoint / (1ull << 32); // 向下取整
    uint64_t var1 =  (1ull << 32) * k , var2 = (1ull << 32) * (k + 1) , var3 = k == 0 ? 0 : (1ull << 32) *( k - 1);
    uint64_t _isn = isn.raw_value(), _n = n.raw_value();
    var1 += (_n + (1ull << 32) - _isn) % (1ull << 32);
    var2 += (_n + (1ull << 32) - _isn) % (1ull << 32);
    var3 += (_n + (1ull << 32) - _isn) % (1ull << 32);
    uint64_t  res = abs(static_cast<long long >(var1 - checkpoint)) > abs(static_cast<long long >(var2 - checkpoint))  ? var2 : var1;
    return abs(static_cast<long long >(res - checkpoint)) > abs(static_cast<long long >(var3 - checkpoint)) ? var3 : res;
}
