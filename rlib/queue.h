/*
 *
 * Copyright (c) 2016 Raphine Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Author: Liva
 * 
 */

#ifndef __RAPH_KERNEL_RAPHQUEUE_H__
#define __RAPH_KERNEL_RAPHQUEUE_H__

#include <functional.h>
#include <spinlock.h>

class Queue {
 public:
  Queue() {
    _last = &_first;
    _first.data = nullptr;
    _first.next = nullptr;
  }
  virtual ~Queue() {
  }
  void Push(void *data);
  // 空の時はfalseが帰る
  bool Pop(void *&data);
  bool IsEmpty() {
    return &_first == _last;
  }
 private:
  struct Container {
    void *data;
    Container *next;
  };
  Container _first;
  Container *_last;
  SpinLock _lock;
};

class FunctionalQueue final : public Functional {
 public:
  FunctionalQueue() {
  }
  ~FunctionalQueue() {
  }
  void Push(void *data) {
    _queue.Push(data);
    WakeupFunction();
  }
  bool Pop(void *&data) {
    return _queue.Pop(data);
  }
  bool IsEmpty() {
    return _queue.IsEmpty();
  }
 private:
  virtual bool ShouldFunc() override {
    return !_queue.IsEmpty();
  }
  Queue _queue;
};

// TODO replace Queue to Queue2.
template<class T>
class Queue2 {
 public:
  Queue2() {
    _last = &_first;
    _first.data = nullptr;
    _first.next = nullptr;
  }
  virtual ~Queue2() {
  }
  void Push(T *data);
  // 空の時はfalseが帰る
  bool Pop(T *&data);
  bool IsEmpty() {
    return &_first == _last;
  }
 private:
  struct Container {
    T *data;
    Container *next;
  };
  Container _first;
  Container *_last;
  SpinLock _lock;
};

template <class T>
class FunctionalQueue2 final : public Functional {
 public:
  FunctionalQueue2() {
  }
  ~FunctionalQueue2() {
  }
  void Push(T *data) {
    _queue.Push(data);
    WakeupFunction();
  }
  bool Pop(T *&data) {
    return _queue.Pop(data);
  }
  bool IsEmpty() {
    return _queue.IsEmpty();
  }
 private:
  virtual bool ShouldFunc() override {
    return !_queue.IsEmpty();
  }
  Queue2<T> _queue;
};

template <class T>
void Queue2<T>::Push(T *data) {
  Container *c = new Container;
  c->data = data;
  c->next = nullptr;
  Locker locker(_lock);
  kassert(_last->next == nullptr);
  _last->next = c;
  _last = c;
}

template<class T>
bool Queue2<T>::Pop(T *&data) {
  Container *c;
  {
    Locker locker(_lock);
    if (IsEmpty()) {
      return false;
    }
    c = _first.next;
    kassert(c != nullptr);
    _first.next = c->next;
    if (_last == c) {
      _last = &_first;
    }
  }
  data = c->data;
  delete c;
  return true;
}

template<class T>
class IntQueue {
 public:
  class Container {
  private:
    friend IntQueue<T>;
    Container *next;
    T *data;
  };
  IntQueue() {
    _last = &_first;
    _first.next = nullptr;
  }
  virtual ~IntQueue() {
  }
  void Push(T *data);
  // 空の時はfalseが帰る
  bool Pop(T *&data);
  bool IsEmpty() {
    return &_first == _last;
  }
 private:
  Container _first;
  Container *_last;
  IntSpinLock _lock;
};

template <class T>
class FunctionalIntQueue final : public Functional {
 public:
  FunctionalIntQueue() {
  }
  ~FunctionalIntQueue() {
  }
  void Push(T *data) {
    _queue.Push(data);
    WakeupFunction();
  }
  bool Pop(T *&data) {
    return _queue.Pop(data);
  }
  bool IsEmpty() {
    return _queue.IsEmpty();
  }
 private:
  virtual bool ShouldFunc() override {
    return !_queue.IsEmpty();
  }
  IntQueue<T> _queue;
};

template <class T>
void IntQueue<T>::Push(T *data) {
  Container *c = data;
  c->next = nullptr;
  c->data = data;
  Locker locker(_lock);
  kassert(_last->next == nullptr);
  _last->next = c;
  _last = c;
}

template<class T>
bool IntQueue<T>::Pop(T *&data) {
  Container *c;
  {
    Locker locker(_lock);
    if (IsEmpty()) {
      return false;
    }
    c = _first.next;
    kassert(c != nullptr);
    _first.next = c->next;
    if (_last == c) {
      _last = &_first;
    }
  }
  data = c->data;
  return true;
}

#endif // __RAPH_KERNEL_RAPHQUEUE_H__
