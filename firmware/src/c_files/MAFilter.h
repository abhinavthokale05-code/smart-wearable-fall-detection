#ifndef MAFILTER_H
#define MAFILTER_H

class MAFilter {
public:
  MAFilter(int size = 5);
  int filter(int input);

private:
  int *_buffer;
  int _size;
  int _index;
  long _sum;
};

#endif
