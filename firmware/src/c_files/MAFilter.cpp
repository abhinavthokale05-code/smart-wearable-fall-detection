#include "MAFilter.h"

MAFilter::MAFilter(int size) {
  _size = size;
  _buffer = new int[_size];
  for (int i = 0; i < _size; i++) _buffer[i] = 0;
  _index = 0;
  _sum = 0;
}

int MAFilter::filter(int input) {
  _sum -= _buffer[_index];
  _buffer[_index] = input;
  _sum += input;
  _index++;
  if (_index >= _size) _index = 0;
  return (int)(_sum / _size);
}
