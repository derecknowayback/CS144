#include "stream_reassembler.hh"
#include <queue>
#include <utility>
#include <iostream>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : 
_output(capacity), 
_capacity(capacity),
_unassembled_bytes(0),
_last_bytes(-1), // 无限大
_expected_bytes(0),
_eof(false),
_buffer(){
}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {


   size_t expected_bytes = _output.bytes_written();
   
   size_t new_index = index;
   // 先别急着写
   // 找上下界，过滤

   // 先找到最后一个小于等于的index
   auto pre_it = _buffer.upper_bound(index);
   if(pre_it != _buffer.begin()){
      pre_it--;
   }
   // 现在有几种情况：
   // 1. 如果buffer里的元素全部比index来的大，那么 pre_it.first > index
   // 2. 如果buffer里的元素全部比index来的小等于，那么 pre_it 指向 end()-1
   // 3. pre_it == end() 什么数据也没有，buffer为空 [这个真的没有想到]

   // 如果重叠了
   if(pre_it != _buffer.end() && index > pre_it->first){
      size_t pre_index = pre_it->first;
      if(pre_index + pre_it->second.length() > index){
         new_index = pre_index + pre_it->second.length(); // 这里要小心，前面的字符串有可能一下子把新的字符串覆盖了，所以下面会有溢出
      }
      // Q: 为什么不用担心一个字符串和前面多个字符串重合，只验证前面最后字符串呢？
      // A: 因为我们是一次次进行过滤、重排的，所以 “前面的字符串之间肯定不会重叠” ，
      //    前面所有字符串能到达 “最远的index” 一定是最后一个字符串所能到达的index。 (这是不是有点像数学归纳法)
   }
   // 如果buffer里面没有重叠的（即上面的1/3两种情况），我们还要考虑和已经写入字符流的数据有没有重合
   else if(index < expected_bytes){
      new_index = expected_bytes;
   }
   // 现在的new_index不会和前面的字符串有重合了


   // 计算长度的式子，当新字符串被 “老字符” 全部覆盖了的时候, new_length就会是负数
   // 因为 new_index - index 是 unsigned类型, 所以写入到 ssize_t 有可能会溢出
   ssize_t new_length = data.length() - (new_index - index); 
   
   // 拿到下一个字符串
   pre_it = _buffer.lower_bound(new_index);

   // 开始过滤后面的字符串
   while (pre_it != _buffer.end()){
      size_t end_index = new_index + new_length;
      // 如果有重叠
      if(pre_it -> first < end_index){
         // 部分重叠
         if(pre_it -> first + pre_it -> second.length() > end_index){
            // 改变新字符串，不改变老字符串
            new_length = pre_it -> first - new_index;
            break; // 已经达到了目标
         }
         // 全部重叠，从map中删除，并且更新 unassembled
         else{
            _unassembled_bytes -= pre_it -> second.length();
            pre_it = _buffer.erase(pre_it);
         }
      }
      // 没有重叠就什么都不做
      else
         break;
   }
   
   // 现在要开始检查 容量 和 最后的字符index
   size_t unacceptable_index = min(expected_bytes + _capacity - _output.buffer_size(),_last_bytes);
   // 如果在允许的字符范围后
   if(new_index >= unacceptable_index) return;
   // 如果超出了范围,截取new_length
   if(new_index + new_length > unacceptable_index){
      new_length = unacceptable_index - new_index;
   }

   // 到这里，正式开始新的字符串
   if(new_length > 0){
      string copy = data.substr(new_index - index,new_length);
      // 如果正好是期待的bytes，那么我们就直接写入
      if(new_index == expected_bytes){
         size_t has_writte = _output.write(copy);
         expected_bytes += has_writte;
         // 不一定都写进去了,如果有剩下的字符，我们就要存下来;
         if(has_writte < copy.length()){
            string remain = data.substr(has_writte,data.length()-has_writte);
            size_t remain_index = new_index + has_writte;
            _unassembled_bytes += remain.length();
            _buffer.insert(make_pair(remain_index,remain));
         } // 这边其实没有检查，碎片的段会不会溢出capacity
      }
      else{
         _unassembled_bytes += copy.length();
         _buffer.insert(make_pair(new_index,copy));
      }
   }



   // 检查map中所有的
   for (auto x = _buffer.begin(); x != _buffer.end(); /* nop 什么也不做 */){
      size_t x_index = x -> first;
      string x_data = x -> second;
      if(x_index <= expected_bytes){
         // 全部被覆盖的直接丢弃
         if(x_index + x_data.length() - 1 < expected_bytes){
            x = _buffer.erase(x);
            continue;
         }
         // 去除掉 overlap的字符 
         size_t overlap = expected_bytes - x_index;
         x_data.erase(0,overlap);
         x_index += overlap;

         // 开始写
         size_t has_writte = _output.write(x_data);
         expected_bytes += has_writte; // 不要忘记了
         _unassembled_bytes -= has_writte;
         // 没有写全，那就不应该继续写了
         if(has_writte < x_data.length()){
            size_t new_x_index = x_index + has_writte;
            string new_x_data = x_data.substr(expected_bytes,x_data.length() - has_writte);
            _buffer.insert(make_pair(new_x_index,new_x_data));
            _buffer.erase(x);
            break;
         }
         x = _buffer.erase(x);
      }
      else{
         break;
      }
   }

   _expected_bytes = expected_bytes;

   if(eof){
       _eof = true;
      _last_bytes = index + data.length();
   }


   if(_eof && expected_bytes >= _last_bytes){
      _output.end_input();
   }
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

/**暂且先这么做吧，我也不知道怎么实现这个好*/
bool StreamReassembler::empty() const { return _output.buffer_empty(); }
