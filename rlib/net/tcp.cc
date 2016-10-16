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
  }

  return 0;
}


int TcpSocket::Close() {
  this->Destroy();
  return 0;
}


int TcpSocket::Read(uint8_t *buf, int len) {
  return 0;
}


int TcpSocket::Write(const uint8_t *buf, int len) {
  NetDev::Packet *packet = nullptr;
  if (this->FetchTxBuffer(packet) < 0) {
    return kErrorOutOfBuffer;
  } else {
    memcpy(packet->buf, buf, len);
    packet->len = len;

    int rval = this->TransmitPacket(packet);
    if (rval >= 0) {
      return len;
    } else {
      return rval;
    }
  }
}


int TcpSocket::Listen() {
  return 0;
}


int TcpSocket::Connect() {
  return 0;
}


int TcpSocket::Shutup() {
  return 0;
}


int TcpSocket::CloseAck() {
  return 0;
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

//  header->checksum = this->Checksum(header, header_length/* + length */, saddr, daddr);

  return true;
}


int TcpLayer::ReceiveSub(NetDev::Packet *&packet) {
}


int TcpLayer::TransmitSub(NetDev::Packet *packet) {
  if (_state != TcpLayer::State::AckWait) {
    // try to send a packet
    int rval = this->TransmitPacket(packet);

    // check transmission mode
    if (_session_type & kFlagACK) {
      if (rval >= 0 && _state != TcpLayer::State::Closed) {
        _packet_length = rval;
        _state = TcpLayer::State::AckWait;
      } else if (rval < 0) {
        // failure
        return rval;
      }
    }
  }

  // after transmission of an initial packet, go on to wait for ACK
  // NOTE: this if-statement must not be concatenated with the above one!
  if (_state == TcpLayer::State::AckWait) {
    // try to receive ACK
    NetDev::Packet *rpacket = nullptr;
    int rval = this->ReceivePacket(rpacket);

    if (rval < 0) {
      // no available packet
      return rval;
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
