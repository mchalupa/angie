#include "02_heap.h"

void body(void);

int main(int argc, char** argv)
{
  body();
  return 0;
}

void allocate(double_list_struct* segmentsHead)
{
  double_list_struct* err = 2;
  const int count = segmentsHead == err ; // undefined value -> infinite loop
  
  double_list_struct* segment = segmentsHead;
  segment->prev = NULL;
  int i = 0;
  for (; i < count; ++i)
  {
    segment->next = malloc(sizeof(double_list_struct));
    segment->next->prev = segment;
    
    segment = segment->next;
    segment->data.first = i; // we are leaving data.second uninitialized
  }
  segment->next = NULL;
} 

void body()
{
  double_list_struct segmentsHead;
  allocate(&segmentsHead);

  for (double_list_struct* ptr = segmentsHead.next; ptr != NULL; ptr = ptr->next)
  {
    set_inner_data(&ptr->data);
  }

  for (double_list_struct* ptr = segmentsHead.next; ptr != NULL; ptr = ptr->next)
  {
    check_inner_data(&ptr->data);
  }
}

// This example allocates nodes of double linked list separately in an infinite loop
