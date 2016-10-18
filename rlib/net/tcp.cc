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
 * Author: levelfour
 * 
 */


#include <stdlib.h>
#include <net/eth.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <dev/eth.h>


int TcpSocket::Open() {
  NetDevCtrl::NetDevInfo *devinfo = netdev_ctrl->GetDeviceInfo(_ifname);
  if (devinfo == nullptr) {
    return kErrorNoDevice;
  }
  DevEthernet *device = static_cast<DevEthernet *>(devinfo->device);
  ProtocolStack *pstack = devinfo->ptcl_stack;

  uint8_t eth_addr[6];
  device->GetEthAddr(eth_addr);

  // stack construction
  // BaseLayer > EthernetLayer > Ipv4Layer > TcpLayer > TcpSocket
  ProtocolStackBaseLayer *base_layer_addr = reinterpret_cast<ProtocolStackBaseLayer *>(virtmem_ctrl->Alloc(sizeof(ProtocolStackBaseLayer)));
  if (base_layer_addr == nullptr) {
    return kErrorAllocFailure;
  }

  ProtocolStackBaseLayer *base_layer = new(base_layer_addr) ProtocolStackBaseLayer();
  base_layer->Setup(nullptr);
  if (!pstack->SetBaseLayer(base_layer)) {
    base_layer->Destroy();
    return kErrorNoDeviceSpace;
  }

  EthernetLayer *eth_layer_addr = reinterpret_cast<EthernetLayer *>(virtmem_ctrl->Alloc(sizeof(EthernetLayer)));
  if (eth_layer_addr == nullptr) {
    base_layer->Destroy();
    return kErrorAllocFailure;
  }

  EthernetLayer *eth_layer = new(eth_layer_addr) EthernetLayer();
  assert(eth_layer->Setup(base_layer));
  eth_layer->SetAddress(eth_addr);
  eth_layer->SetUpperProtocolType(EthernetLayer::kProtocolIpv4);

  Ipv4Layer *ip_layer_addr = reinterpret_cast<Ipv4Layer *>(virtmem_ctrl->Alloc(sizeof(Ipv4Layer)));
  if (ip_layer_addr == nullptr) {
    eth_layer->Destroy();
    return kErrorAllocFailure;
  }

  Ipv4Layer *ip_layer = new(ip_layer_addr) Ipv4Layer();
  assert(ip_layer->Setup(eth_layer));
  ip_layer->SetAddress(_ipv4_addr);
  ip_layer->SetPeerAddress(_peer_addr);
  ip_layer->SetProtocol(Ipv4Layer::kProtocolUdp);

  TcpLayer *tcp_layer_addr = reinterpret_cast<TcpLayer *>(virtmem_ctrl->Alloc(sizeof(TcpLayer)));
  if (tcp_layer_addr == nullptr) {
    ip_layer->Destroy();
    return kErrorAllocFailure;
  }

  TcpLayer *tcp_layer = new(tcp_layer_addr) TcpLayer();
  assert(tcp_layer->Setup(ip_layer));
  tcp_layer->SetAddress(_ipv4_addr);
  tcp_layer->SetPeerAddress(_peer_addr);
  tcp_layer->SetPort(_port);
  tcp_layer->SetPeerPort(_peer_port);

  return this->Setup(tcp_layer) ? kReturnSuccess : kErrorUnknown;
}


int TcpSocket::Close() {
  this->Destroy();
  return 0;
}


int TcpSocket::Read(uint8_t *buf, int len) {
  NetDev::Packet *packet = nullptr;
  if (this->ReceivePacket(packet)) {
    int ret = static_cast<int>(packet->len) <= len ? packet->len : len;

    memcpy(buf, packet->buf, ret);
    this->ReuseRxBuffer(packet);

    return ret;
  } else {
    return kErrorNoRxPacket;
  }
}


int TcpSocket::Write(const uint8_t *buf, int len) {
  NetDev::Packet *packet = nullptr;
  if (this->FetchTxBuffer(packet) < 0) {
    return kErrorOutOfBuffer;
  } else {
    memcpy(packet->buf, buf, len);
    packet->len = len;

    if (this->TransmitPacket(packet) == true) {
      return len;
    } else {
      return Socket::kErrorTxFailure;
    }
  }
}


int TcpLayer::Listen() {
  if (_state != TcpLayer::State::Closed && _state != TcpLayer::State::Listen &&
      _state != TcpLayer::State::SynReceived && _state != TcpLayer::State::SynSent) {
    // connection already established
    return Socket::kReturnAlreadyEstablished;
  }

  if (_state == TcpLayer::State::Closed) {
    // try to receive SYN packet
    _session_type = kFlagSYN;

    NetDev::Packet *synpkt = nullptr;
    if (this->ReceivePacket(synpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(synpkt->buf);
      _ack = header->seq_number + 1;
      _state = TcpLayer::State::Listen;
      this->ReuseRxBuffer(synpkt);
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  if (_state == TcpLayer::State::Listen) {
    // try to transmit SYN+ACK packet
    _session_type = kFlagSYN | kFlagACK;
    _seq = _seq == 0 ? rand() : _seq;

    NetDev::Packet *ackpkt = nullptr;
    if (this->FetchTxBuffer(ackpkt) < 0) {
      return Socket::kErrorOutOfBuffer;
    }

    if (this->TransmitPacket(ackpkt) == true) {
      _state = TcpLayer::State::SynSent;
    } else {
      // failure
      return Socket::kErrorTxFailure;
    }
  }

  if (_state == TcpLayer::State::SynSent) {
    // try to receive ACK packet
    _session_type = kFlagACK;

    NetDev::Packet *ackpkt = nullptr;
    if (this->ReceivePacket(ackpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(ackpkt->buf);
      uint32_t seq = header->seq_number;
      uint32_t ack = header->ack_number;

      this->ReuseRxBuffer(ackpkt);

      // check sequence number
      if (seq != _ack) {
        return Socket::kErrorAckFailure;
      }

      // check acknowledgement number
      if (ack != _seq + 1) {
        return Socket::kErrorAckFailure;
      }

      // connection established
      _seq = _ack;
      _ack = seq + 1;
      _state = TcpLayer::State::Established;
      return Socket::kReturnSuccess;
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  return Socket::kErrorUnknown;
}


int TcpLayer::Connect() {
  if (_state != TcpLayer::State::Closed && _state != TcpLayer::State::Listen &&
      _state != TcpLayer::State::SynReceived && _state != TcpLayer::State::SynSent) {
    // connection already established
    return Socket::kReturnAlreadyEstablished;
  }

  if (_state == TcpLayer::State::Closed) {
    // try to transmit SYN packet
    NetDev::Packet *synpkt = nullptr;
    if (this->FetchTxBuffer(synpkt) < 0) {
      return Socket::kErrorOutOfBuffer;
    }

    _session_type = kFlagSYN;
    _seq = rand();
    _ack = 0;

    if (this->TransmitPacket(synpkt) == true) {
      _state = TcpLayer::State::SynSent;
    } else {
      // failure
      return Socket::kErrorTxFailure;
    }
  }

  if (_state == TcpLayer::State::SynSent) {
    // try to receive SYN+ACK packet
    NetDev::Packet *synpkt = nullptr;

    _session_type = kFlagSYN | kFlagACK;

    if (this->ReceivePacket(synpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(synpkt->buf);
      uint32_t seq = header->seq_number;
      uint32_t ack = header->ack_number;

      this->ReuseRxBuffer(synpkt);

      // check acknowledgement number
      if (ack != _seq + 1) {
        return Socket::kErrorAckFailure;
      }

      // try to transmit ACK packet
      NetDev::Packet *ackpkt = nullptr;
      if (this->FetchTxBuffer(ackpkt) < 0) {
        return Socket::kErrorOutOfBuffer;
      }

      _session_type = kFlagACK;
      _seq = _seq + 1;
      _ack = seq + 1;

      if (this->TransmitPacket(ackpkt) == true) {
        _state = TcpLayer::State::Established;

        // connection established
        _ack = seq + 1;

        return Socket::kReturnSuccess;
      } else {
        // failure
        return Socket::kErrorTxFailure;
      }
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  return Socket::kErrorUnknown;
}


int TcpLayer::Shutup() {
  if (_state == TcpLayer::State::SynSent || _state == TcpLayer::State::Listen) {
    _state = TcpLayer::State::Closed;
    return Socket::kReturnSuccess;
  }

  if (_state == TcpLayer::State::Established) {
    // try to transmit FIN+ACK packet
    NetDev::Packet *finpkt = nullptr;
    if (this->FetchTxBuffer(finpkt) < 0) {
      return Socket::kErrorOutOfBuffer;
    }

    _session_type = kFlagFIN | kFlagACK;

    if (this->TransmitPacket(finpkt) == true) {
      _state = TcpLayer::State::FinWait1;
    } else {
      // failure
      return Socket::kErrorTxFailure;
    }
  }

  if (_state == TcpLayer::State::FinWait1) {
    // try to receive ACK packet
    NetDev::Packet *ackpkt = nullptr;

    _session_type = kFlagACK;

    if (this->ReceivePacket(ackpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(ackpkt->buf);
      uint32_t seq = header->seq_number;
      uint32_t ack = header->ack_number;

      this->ReuseRxBuffer(ackpkt);

      // check sequence number
      if (seq != _ack) {
        return Socket::kErrorAckFailure;
      }

      // check acknowledgement number
      if (ack != _seq + 1) {
        return Socket::kErrorAckFailure;
      }

      _state = TcpLayer::State::FinWait2;
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  if (_state == TcpLayer::State::FinWait2) {
    // try to receive FIN+ACK packet
    NetDev::Packet *finpkt = nullptr;

    _session_type = kFlagFIN | kFlagACK;

    if (this->ReceivePacket(finpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(finpkt->buf);
      uint32_t seq = header->seq_number;
      uint32_t ack = header->ack_number;

      this->ReuseRxBuffer(finpkt);

      // check sequence number
      if (seq != _ack) {
        return Socket::kErrorAckFailure;
      }

      // check acknowledgement number
      if (ack != _seq + 1) {
        return Socket::kErrorAckFailure;
      }

      // try to transmit ACK packet
      NetDev::Packet *ackpkt = nullptr;
      if (this->FetchTxBuffer(ackpkt) < 0) {
        return Socket::kErrorOutOfBuffer;
      }

      _session_type = kFlagACK;
      _seq = _seq + 1;
      _ack = _ack + 1;

      if (this->TransmitPacket(ackpkt) == true) {
        _state = TcpLayer::State::Closed;
        _seq = 0;
        _ack = 0;
        return Socket::kReturnSuccess;
      } else {
        // failure
        return Socket::kErrorTxFailure;
      }
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  return Socket::kErrorUnknown;
}


bool TcpLayer::FilterPacket(NetDev::Packet *packet) {
  TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(packet->buf);

  if (_peer_port != TcpSocket::kPortAny && ntohs(header->sport) != _peer_port) {
    return false;
  }

  if (ntohs(header->dport) != _port) {
    return false;
  }

  // received session type (partially ignored)
  uint8_t sess =
      (header->fin ? kFlagFIN : 0)
    | (header->syn ? kFlagSYN : 0)
    | (header->rst ? kFlagRST : 0)
    | (header->ack ? kFlagACK : 0);

  if ((sess & kFlagFIN) == 0 && sess != _session_type) {
    return false;
  }

  return true;
}


bool TcpLayer::PreparePacket(NetDev::Packet *packet) {
  if (_peer_port == TcpSocket::kPortAny) {
    return false;
  }

  // TODO: set options

  TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(packet->buf);

  uint16_t header_length = GetProtocolHeaderLength();

  header->sport = htons(_port);
  header->dport = htons(_peer_port);
  header->seq_number = htonl(_seq);
  header->ack_number = htonl(_ack);
  header->reserved = 0;
  header->header_len = (header_length >> 2);
  header->window_size = 0xffff;
  header->checksum = 0;
  header->urgent_pointer = 0;

  header->checksum = this->Ipv4Checksum(packet->buf, packet->len, _ipv4_addr, _peer_addr);

  return true;
}


int TcpLayer::ReceiveSub(NetDev::Packet *&packet) {
  // TODO: check when to use ReuseRxBuffer on packet?
  if (_state == TcpLayer::State::CloseWait || _state == TcpLayer::State::LastAck) {
    // continue closing connection
    int rval = CloseAck();

    if (rval >= 0) {
      return Socket::kReturnConnectionClosed;
    } else {
      // failure
      return rval;
    }
  }

  if (_state == TcpLayer::State::Established) {
    if (_session_type & kFlagACK) {
      // try to receive TCP acknowledgement
      if (this->TransmitPacket(packet) == true) {
        TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(packet->buf);

        // TODO: check if ack number is duplicated

        if (header->fin) {
          _seq = header->ack_number;
          _ack = header->seq_number + 1;
          int rval = CloseAck();

          if (rval >= 0) {
            return Socket::kReturnConnectionClosed;
          } else {
            return rval;
          }
        } else if (_ack == header->seq_number || (_seq == header->seq_number && _ack == header->ack_number)) {
          // correct sequence: acknowledge number = the expected next sequence number
          // or right after 3-way handshake

          // set next expected sequence / acknowledge number
          _seq = header->ack_number;
          _ack = header->seq_number + packet->len;

          if (_state == TcpLayer::State::Established) {
            // send acknowledment completion
            NetDev::Packet *ackpkt = nullptr;

            if (this->FetchTxBuffer(ackpkt) < 0) {
              return Socket::kErrorOutOfBuffer;
            }

            ackpkt->len = 0;
            if (this->TransmitPacket(ackpkt) == true) {
              return ackpkt->len;
            } else {
              return Socket::kErrorTxFailure;
            }
          }
        } else {
          return Socket::kErrorAckFailure;
        }
      }
    } else {
      return this->ReceivePacket(packet);
    }
  }

  return Socket::kErrorUnknown;
}


int TcpLayer::TransmitSub(NetDev::Packet *packet) {
  if (_state != TcpLayer::State::AckWait) {
    // try to send a packet
    if(this->TransmitPacket(packet) == false) {
      return Socket::kErrorTxFailure;
    }

    // check transmission mode
    if (_session_type & kFlagACK && _state != TcpLayer::State::Closed) {
      _packet_length = packet->len;
      _state = TcpLayer::State::AckWait;
    } else {
      // paranoid
      assert(false);
    }
  }

  // after transmission of an initial packet, go on to wait for ACK
  // NOTE: this if-statement must not be concatenated with the above one!
  if (_state == TcpLayer::State::AckWait) {
    // try to receive ACK
    NetDev::Packet *rpacket = nullptr;
    if (this->ReceivePacket(rpacket) == false) {
      // no available packet
      return Socket::kErrorNoRxPacket;
    } else {
      // check TCP acknowldgement conditions
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(rpacket->buf);
      if (header->ack && header->seq_number == _ack &&
          header->ack_number == _seq + _packet_length) {

        // acknowledgement completed
        _seq = _seq + _packet_length;
        _state = TcpLayer::State::Established;
        this->ReuseRxBuffer(packet);

        return _packet_length;
      } else {
        // could not receive the corresponding ACK packet
        // try another calling to receive ACK
        this->ReuseRxBuffer(packet);
        return Socket::kErrorNoAck;
      }
    }
  }

  return Socket::kErrorUnexpected;
}


int TcpLayer::CloseAck() {
  if (_state == TcpLayer::State::Established) {
    // transmit ACK packet
    _session_type = kFlagACK;

    NetDev::Packet *ackpkt = nullptr;
    if (this->FetchTxBuffer(ackpkt) < 0) {
      return Socket::kErrorOutOfBuffer;
    }

    if (this->TransmitPacket(ackpkt) == true) {
      _state = TcpLayer::State::CloseWait;
    } else {
      // failure
      return Socket::kErrorTxFailure;
    }
  }

  if (_state == TcpLayer::State::CloseWait) {
    // transmit FIN+ACK packet
    _session_type = kFlagFIN | kFlagACK;

    NetDev::Packet *ackpkt = nullptr;
    if (this->FetchTxBuffer(ackpkt) < 0) {
      return Socket::kErrorOutOfBuffer;
    }

    if (this->TransmitPacket(ackpkt) == true) {
      _state = TcpLayer::State::LastAck;
    } else {
      // failure
      return Socket::kErrorTxFailure;
    }
  }

  if (_state == TcpLayer::State::LastAck) {
    // receive ACK packet
    _session_type = kFlagACK;

    NetDev::Packet *ackpkt = nullptr;

    if (this->ReceivePacket(ackpkt) == true) {
      TcpLayer::Header *header = reinterpret_cast<TcpLayer::Header *>(ackpkt->buf);
      uint32_t seq = header->seq_number;
      uint32_t ack = header->ack_number;

      this->ReuseRxBuffer(ackpkt);

      // check sequence number
      if (seq != _ack) {
        return Socket::kErrorAckFailure;
      }

      // check acknowledgement number
      if (ack != _seq + 1) {
        return Socket::kErrorAckFailure;
      }

      _state = TcpLayer::State::Closed;
      _seq = 0;
      _ack = 0;

      return Socket::kReturnSuccess;
    } else {
      // failure
      return Socket::kErrorNoRxPacket;
    }
  }

  return Socket::kErrorUnknown;
}


uint16_t TcpLayer::Ipv4Checksum(uint8_t *buf, uint32_t size, uint32_t saddr, uint32_t daddr) {
  uint64_t sum = 0;

  // pseudo header
  sum += ntohs((saddr >> 16) & 0xffff);
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);
  sum += ntohs((saddr >> 0) & 0xffff);
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);
  sum += ntohs((daddr >> 16) & 0xffff);
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);
  sum += ntohs((daddr >> 0) & 0xffff);
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);
  sum += ntohs(Ipv4Layer::kProtocolTcp);
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);
  sum += ntohs(static_cast<uint16_t>(size));
  if (sum & 0x80000000) sum = (sum & 0xffff) + (sum >> 16);

  // (true) TCP header and body
  while (size > 1) {
    sum += *reinterpret_cast<uint16_t*>(buf);
    buf += 2;
    if(sum & 0x80000000) {
      // if high order bit set, fold
      sum = (sum & 0xffff) + (sum >> 16);
    }
    size -= 2;
  }

  if (size) {
    // take care of left over byte
    sum += static_cast<uint16_t>(*buf);
  }
 
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return ~sum;
}
