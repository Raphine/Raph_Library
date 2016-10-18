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

#ifndef __RAPH_KERNEL_NET_TCP_H__
#define __RAPH_KERNEL_NET_TCP_H__


#include <net/pstack.h>
#include <net/socket.h>
#include <net/arp.h>


class TcpLayer : public ProtocolStackLayer {
public:

  /** session flags */
  static const uint8_t kFlagFIN = 1 << 0;
  static const uint8_t kFlagSYN = 1 << 1;
  static const uint8_t kFlagRST = 1 << 2;
  static const uint8_t kFlagPSH = 1 << 3;
  static const uint8_t kFlagACK = 1 << 4;
  static const uint8_t kFlagURG = 1 << 5;
  static const uint8_t kFlagECE = 1 << 6;
  static const uint8_t kFlagCWR = 1 << 7;

  /** maximum segment size */
  static const uint32_t kMss = 1460;

  TcpLayer() {}

  /**
   * TCP header
   */
  struct Header {
    uint16_t sport;           /** source port */
    uint16_t dport;           /** destination port */
    uint32_t seq_number;      /** sequence number */
    uint32_t ack_number;      /** acknowledge number */
    uint8_t reserved: 4;      /** reserved */
    uint8_t header_len: 4;    /** TCP header length */
    uint8_t fin: 1,           /** session: finish */
            syn: 1,           /** session: sync */
            rst: 1,           /** session: reset */
            psh: 1,           /** session: push */
            ack: 1,           /** session: acknowledge */
            urg: 1,           /** session: urgent */
            ece: 1,           /** session: ECN-echo (RFC 3168) */
            cwr: 1;           /** session: congestion window reduced (RFC 3168) */
    uint8_t flag: 6;          /** session flag */
    uint16_t window_size;     /** window size */
    uint16_t checksum;        /** checksum */
    uint16_t urgent_pointer;  /** urgent pointer */
  } __attribute__((packed));

  /**
   * Set my IPv4 address.
   *
   * @param ipv4_addr
   */
  void SetAddress(uint32_t ipv4_addr) {
    _ipv4_addr = ipv4_addr;
  }

  /**
   * Set peer IPv4 address.
   *
   * @param ipv4_addr
   */
  void SetPeerAddress(uint32_t ipv4_addr) {
    _peer_addr = ipv4_addr;
  }

  /**
   * Set my TCP port.
   *
   * @param port 
   */
  void SetPort(uint16_t port) {
    _port = port;
  }

  /**
   * Set peer TCP port.
   *
   * @param port 
   */
  void SetPeerPort(uint16_t port) {
    _peer_port = port;
  }

  /**
   * A wrapper of TransmitPacket, handling acknowledgement.
   * (e.g. check sequence/acknowledement number, receive ACK packets, etc.)
   *
   * @param packet
   * @return return code.
   */
  int TransmitSub(NetDev::Packet *packet);

  /**
   * A wrapper of ReceivePacket, handling acknowledgement.
   * (e.g. check sequence/acknowledement number, send ACK packets, etc.)
   *
   * @param packet
   * @return return code.
   */
  int ReceiveSub(NetDev::Packet *&packet);

  /**
   * Wait for a client connection (3-way handshake).
   * This is used by TCP server sockets.
   *
   * @return return code.
   */
  int Listen();

  /**
   * Connect to a server (with 3-way handshake).
   * This is used by TCP client sockets.
   *
   * @return return code.
   */
  int Connect();

  /**
   * Close TCP connection (with 3-way handshake).
   * This can be used both by TCP server and client sockets.
   *
   * @return return code.
   */
  int Shutup();

protected:
  /**
   * Return TCP header size.
   * 
   * @return protocol header length.
   */
  virtual int GetProtocolHeaderLength() {
    return sizeof(TcpLayer::Header);
  }

  /**
   * Filter the received packet by its header content.
   *
   * @param packet
   * @return if the packet is to be received or not.
   */
  virtual bool FilterPacket(NetDev::Packet *packet);

  /**
   * Make contents of the header before transmitting the packet.
   *
   * @param packet
   * @return if succeeds.
   */
  virtual bool PreparePacket(NetDev::Packet *packet);

private:
  /** TCP states (extended) (cf. RFC 793 p.26) */
  enum class State {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
    AckWait,
  };

  /** IPv4 address assigned to the network device under this layer */
  uint32_t _ipv4_addr = 0;

  /** IPv4 address of the peer */
  uint32_t _peer_addr = 0;

  /** port number of this connection */
  uint16_t _port;

  /** port number of the peer */
  uint16_t _peer_port;

  /** current TCP state */
  TcpLayer::State _state = TcpLayer::State::Closed;

  /** length of packets sent last time */
  int _packet_length = 0;

  /** session type */
  uint8_t _session_type = kFlagRST;

  /** sequence number */
  uint32_t _seq = 0;

  /** acknowledege number */
  uint32_t _ack = 0;

  /** maximum segment size */
  uint16_t _mss = kMss;

  /** window scale */
  uint8_t _ws = 1;

  /**
   * Calculate TCP (over IPv4) checksum.
   *
   * @param buf TCP packet.
   * @param size the length of (TCP header + TCP header option + packet body).
   * @param saddr source IPv4 address, used for pseudo-header.
   * @param daddr destination IPv4 address, used for pseudo-header.
   * @return checksum value.
   */
  uint16_t Ipv4Checksum(uint8_t *buf, uint32_t size, uint32_t saddr, uint32_t daddr);

  /**
   * Responce to FIN session with FIN+ACK.
   *
   * @return return code.
   */
  int CloseAck();
};


class TcpSocket : public Socket {
public:

  /** do not specify peer port */
  static const uint16_t kPortAny = 0xffff;

  TcpSocket() {}

  virtual ~TcpSocket() {
    this->Close();
  }

  /**
   * Reserve the connection on protocol stack and construct the stack.
   *
   * @return 0 if succeeds.
   */
  virtual int Open() override;

  /**
   * Release resources reserved in Socket::Open().
   * This function is tolerant of "double-calling".
   *
   * @return 0 if succeeeds.
   */
  virtual int Close() override;

  /**
   * Bind port to this socket.
   *
   * @param port
   */
  void BindPort(uint16_t port) {
    _port = port;
  }

  /**
   * Bind peer address info to this socket.
   *
   * @param ipv4_addr
   * @param port
   */
  void BindPeer(uint32_t ipv4_addr, uint16_t port) {
    _peer_addr = ipv4_addr;
    _peer_port = port;

    // prefetch by ARP request
    ArpSocket arp;
    if (arp.Open() >= 0) {
      arp.Request(_peer_addr);
    }
  }

  /**
   * Receive a TCP packet.
   *
   * @param buf buffer to be stored a packet.
   * @param len packet length.
   * @return received length.
   */
  int Read(uint8_t *buf, int len);

  /**
   * Transmit a TCP packet.
   *
   * @param buf content to be sent.
   * @param len content length.
   * @return transmitted length.
   */
  int Write(const uint8_t *buf, int len);

  /**
   * Wait for a client connection (3-way handshake).
   * This is used by TCP server sockets.
   *
   * @return return code.
   */
  int Listen() {
    TcpLayer *tcp = reinterpret_cast<TcpLayer *>(_prev_layer);
    return tcp->Listen();
  }

  /**
   * Connect to a server (with 3-way handshake).
   * This is used by TCP client sockets.
   *
   * @return return code.
   */
  int Connect() {
    TcpLayer *tcp = reinterpret_cast<TcpLayer *>(_prev_layer);
    return tcp->Connect();
  }

  /**
   * Close TCP connection (with 3-way handshake).
   * This can be used both by TCP server and client sockets.
   *
   * @return return code.
   */
  int Shutup() {
    TcpLayer *tcp = reinterpret_cast<TcpLayer *>(_prev_layer);
    return tcp->Shutup();
  }

  /**
   * Update information of the interface.
   */
  virtual void Update() override {
    assert(GetIpv4Address(_ipv4_addr));
  }

  /**
   * Receive a packet from the parent layer.
   *
   * @param packet MUST be freed by ReuseRxBuffer once it's no longer necessary.
   * @return if succeeeds.
   */
  bool ReceivePacket(NetDev::Packet *&packet) override {
    // call the wrapper function of TcpLayer::TransmitPacket
    TcpLayer *tcp_layer = reinterpret_cast<TcpLayer *>(_prev_layer);
    return tcp_layer->ReceiveSub(packet);
  }

  /**
   * Transmit a packet to the parent layer.
   *
   * @param packet MUST be fetched by FetchTxBuffer in advance.
   * @return if succeeeds.
   */
  bool TransmitPacket(NetDev::Packet *packet) override {
    // call the wrapper function of TcpLayer::TransmitPacket
    TcpLayer *tcp_layer = reinterpret_cast<TcpLayer *>(_prev_layer);
    return tcp_layer->TransmitSub(packet);
  }

private:
  /** my IPv4 address */
  uint32_t _ipv4_addr;

  /** my port */
  uint16_t _port;

  /** target IPv4 address */
  uint32_t _peer_addr;

  /** target port */
  uint16_t _peer_port;
};


#endif // __RAPH_KERNEL_NET_TCP_H__
