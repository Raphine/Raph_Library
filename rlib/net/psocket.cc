/*
 *
 * Copyright (c) 2016 Project Raphine
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
 * Author: Levelfour
 * 
 */

#ifndef __KERNEL__

#include <net/psocket.h>
#include <mem/uvirtmem.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int32_t PoolingSocket::Open() {
  if ((_tcp_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    perror("");
    return _tcp_socket;
  }

  if ((_udp_socket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("");
    return _udp_socket;
  }
  
  // initialize
  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    _tcp_client[i] = -1; // fd not in use
  }
  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    _udp_client[i].enabled = false;
  }

  // turn on non-blocking mode
  int flag = fcntl(_tcp_socket, F_GETFL);
  fcntl(_tcp_socket, F_SETFL, flag | O_NONBLOCK);

  flag = fcntl(_udp_socket, F_GETFL);
  fcntl(_udp_socket, F_SETFL, flag | O_NONBLOCK);

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(_port);
  bind(_tcp_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));
  bind(_udp_socket, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr));

  FD_ZERO(&_fds);

  _timeout.tv_sec = 0;
  _timeout.tv_usec = 0;

  InitPacketBuffer();
  SetupPollingHandler();

  return 0;
}

void PoolingSocket::InitPacketBuffer() {
  while (!_tx_reserved.IsFull()) {
    Packet *packet = reinterpret_cast<Packet *>(virtmem_ctrl->Alloc(sizeof(Packet)));
    packet->adr = -1; // negative value is invalid address
    packet->len = 0;
    kassert(_tx_reserved.Push(packet));
  }

  while (!_rx_reserved.IsFull()) {
    Packet *packet = reinterpret_cast<Packet *>(virtmem_ctrl->Alloc(sizeof(Packet)));
    packet->adr = -1; // negative value is invalid address
    packet->len = 0;
    kassert(_rx_reserved.Push(packet));
  }
}

void PoolingSocket::Poll(void *arg) {
  int32_t capacity = Capacity();
  if (capacity > 0) {
    listen(_tcp_socket, capacity);

    int32_t index = GetAvailableTcpClientIndex();

    // at this point, _tcp_client[index] is not used

    if ((_tcp_client[index] = accept(_tcp_socket, nullptr, nullptr)) > 0) {
      FD_SET(_tcp_client[index], &_fds);
    }
  }

  {
    // TCP receive sequence
    Packet *packet;

    fd_set tmp_fds;
    memcpy(&tmp_fds, &_fds, sizeof(_fds));

    // receive packet (non-blocking)
    if (select(GetNfds(), &tmp_fds, 0, 0, &_timeout) >= 0) {
      for (int32_t i = 0; i < kMaxClientNumber; i++) {
        if (_tcp_client[i] == -1) {
          // this socket is not used now
          continue;
        }

        if (FD_ISSET(_tcp_client[i], &tmp_fds)) {
          if (GetRxPacket(packet)) {
            int32_t rval = read(_tcp_client[i], packet->buf, kMaxPacketLength);
            if (rval > 0) {
              packet->adr = i;
              packet->len = rval;
              _rx_buffered.Push(packet);
            } else {
              if (rval == 0) {
                // socket may be closed by foreign host
                close(_tcp_client[i]);
                FD_CLR(_tcp_client[i], &_fds);
                _tcp_client[i] = -1;
              }

              ReuseRxBuffer(packet);
            }
          }
        }
      }
    }
  }

  {
    // UDP receive sequence
    Packet *packet;
    int32_t index = GetAvailableUdpClientIndex();

    if (index != -1 && GetRxPacket(packet)) {
      socklen_t sin_size;
      int32_t rval = recvfrom(_udp_socket, packet->buf, kMaxPacketLength, 0, reinterpret_cast<struct sockaddr *>(&_udp_client[index]), &sin_size);
      if (rval > 0) {
        packet->adr = kUDPAddressOffset + index;
        packet->len = rval;
        _rx_buffered.Push(packet);
      } else {
        ReuseRxBuffer(packet);
      }
    }
  }

  {
    // transmit sequence
    Packet *packet;

    // transmit packet (if non-sent packet remains in buffer)
    if (_tx_buffered.Pop(packet)) {
      int32_t adr = packet->adr;
      int32_t uadr = adr - kUDPAddressOffset;

      if (0 <= uadr && uadr < kMaxClientNumber && _udp_client[uadr].enabled) {
        // the address number is valid UDP address
        sendto(_udp_socket, packet->buf, packet->len, 0, reinterpret_cast<struct sockaddr *>(&_udp_client[uadr]), sizeof(_udp_client[uadr]));
        ReuseTxBuffer(packet);
      } else if (0 <= adr && adr < kMaxClientNumber && _tcp_client[adr] != -1) {
        // the address number is valid TCP address
        int32_t rval, residue = packet->len;
        uint8_t *ptr = packet->buf;

        while (residue > 0) {
          // send until EOF (rval == 0)
          rval = write(_tcp_client[adr], ptr, residue);
          residue -= rval;
          ptr += rval;
        }

        ReuseTxBuffer(packet);
      }
    }
  }
}

void PoolingSocket::SetupPollingHandler() {
  ClassFunction<PoolingSocket> polling_func;
  polling_func.Init(this, &PoolingSocket::Poll, nullptr);
  _polling.Init(polling_func);
  _polling.Register(0);
}

int32_t PoolingSocket::Capacity() {
  // count the number of unused fd
  int32_t capacity = 0;

  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    if (_tcp_client[i] == -1) {
      capacity++;
    }
  }

  return capacity;
}

int32_t PoolingSocket::GetAvailableTcpClientIndex() {
  // return index of the first unused TCP client
  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    if (_tcp_client[i] == -1) {
      return i;
    }
  }

  return -1;
}

int32_t PoolingSocket::GetAvailableUdpClientIndex() {
  // return index of the first unused UDP client
  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    if (!_udp_client[i].enabled) {
      return i;
    }
  }

  return -1;
}

int32_t PoolingSocket::GetNfds() {
  // this function is used for 1st-arg of select syscall
  int32_t max_fd = -1;

  for (int32_t i = 0; i < kMaxClientNumber; i++) {
    if (_tcp_client[i] > max_fd) {
      max_fd = _tcp_client[i];
    }
  }

  return max_fd + 1;
}

#endif // !__KERNEL__