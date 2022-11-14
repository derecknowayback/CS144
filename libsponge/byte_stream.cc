#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}
using namespace std;

//:capacity(_capacity),hasRead(0),hasWritten(0),isEof(false),buffer("")
ByteStream::ByteStream(const size_t capacity):_capacity(capacity),_hasRead(0),_hasWritten(0),_isEof(false),_queue(){ 
 }

/**这里只是简单地拼接，如果空间不够了，尽可能多的写*/
size_t ByteStream::write(const string &data) {
  if(_isEof) return 0;
  size_t useful = _capacity - _queue.size(), len = data.length();
  size_t res = len > useful ? useful : len;
  for (size_t i = 0; i < res; i++){
    _queue.push_back(data.at(i));
  }
  _hasWritten += res;  
  return res;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t pop_size = min(len, _queue.size());
    return string(_queue.begin(), _queue.begin() + pop_size);
}


//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_size = min(len, _queue.size());
    for (size_t i = 0; i < pop_size; i++)
        _queue.pop_front();
    _hasRead += pop_size;    
 }

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    string res = peek_output(len);
    pop_output(len);
    return res;
}

void ByteStream::end_input() {
    _isEof = true;
}

bool ByteStream::input_ended() const { return _isEof; }

size_t ByteStream::buffer_size() const { return _queue.size(); }

bool ByteStream::buffer_empty() const { return _queue.size() == 0;}

bool ByteStream::eof() const { return _isEof && buffer_empty(); }

size_t ByteStream::bytes_written() const { return _hasWritten; }

size_t ByteStream::bytes_read() const { return _hasRead; }

size_t ByteStream::remaining_capacity() const { return _capacity - _queue.size(); }
