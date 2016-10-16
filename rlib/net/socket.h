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

#ifndef __RAPH_KERNEL_NET_SOCKET_H__
#define __RAPH_KERNEL_NET_SOCKET_H__


#include <net/pstack.h>


/**
 * Socket class which is the top layer of protocol stack, and serves as
 * the interface for users.
 * Socket constructs one stack connection in initial sequence.
 */
class Socket : public ProtocolStackLayer {
public:

  // return codes

  /** success */
  static const int kReturnSuccess             = 0;

  /** unknown error */
  static const int kErrorUnknown              = -0x1;
  /** no device specified by interface name */
  static const int kErrorNoDevice             = -0x100;
  /** no enough space for connections in the device */
  static const int kErrorNoDeviceSpace        = -0x101;
  /** memory allocation failure */
  static const int kErrorAllocFailure         = -0x102;
  /** out of reserved buffer for tx/rx */
  static const int kErrorOutOfBuffer          = -0x103;
  /** TCP: did not receive ACK */
  static const int kErrorNoAck                = -0x1000;
  /** unexpected error (e.g. reached the point not expected to be reached) */
  static const int kErrorUnexpected           = -0x10000;

  Socket() {
    // TODO: find an available interface name
    strcpy(_ifname, "en0");
    this->Update();
  }

  /**
   * Reserve the connection on protocol stack and construct the stack.
   */
  virtual int Open() = 0;

  /**
   * Release resources reserved in Socket::Open().
   * This function should be implemented to be tolerant of "double-calling".
   */
  virtual int Close() = 0;

  virtual bool SetupSubclass() {
    // prevent adding sublayer to Socket class
    _next_layer = this;

    return true;
  }

  /**
   * Closing sequence of subclasses.
   */
  virtual void DestroySubclass() override {
    // do nothing
  }

  /**
   * Assign network device, specified by interface name.
   * Network device fetching is done during Socket::Open.
   *
   * @param ifname interface name.
   */
  void AssignNetworkDevice(const char *ifname) {
    strncpy(_ifname, ifname, kIfNameLength);
    this->Update();
  }

  /**
   * Update information of the interface, e.g., IP address.
   */
  virtual void Update() {}

  /**
   * Receive a packet from the parent layer.
   *
   * @param packet MUST be freed by ReuseRxBuffer once it's no longer necessary.
   * @return if succeeeds.
   */
  virtual bool ReceivePacket(NetDev::Packet *&packet) override {
    // omit header manipulation since Socket does not have header
    return _prev_layer->ReceivePacket(packet);
  }

  /**
   * Transmit a packet to the parent layer.
   *
   * @param packet MUST be fetched by FetchTxBuffer in advance.
   * @return if succeeds.
   */
  virtual bool TransmitPacket(NetDev::Packet *packet) override {
    // omit header manipulation since Socket does not have header
    return _prev_layer->TransmitPacket(packet);
  }

protected:
  // same constants exists in NetDevCtrl, should we merge?
  static const int kIfNameLength = 16;

  /** network interface name */
  char _ifname[kIfNameLength];

  /**
   * Get IPv4 address of the interface.
   *
   * @param addr buffer to return address.
   * @return if the interface supports IPv4 or not.
   */
  bool GetIpv4Address(uint32_t &addr);
};


#endif // __RAPH_KERNEL_NET_SOCKET_H__
