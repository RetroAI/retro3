/*
 * Copyright (C) 2024 retro.ai
 * This file is part of retro3 - https://github.com/retroai/retro3
 *
 * This file is derived from Sunodo under the Apache 2.0 License
 * Copyright (C) 2023-2024 Sunodo (https://docs.sunodo.io)
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later AND Apache-2.0
 * See the file LICENSE.txt for more information.
 */

#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

// The rollup.h header file specifies how the userspace interacts with the
// Cartesi Rollup device driver. This header is provided by the Cartesi version
// of the Linux kernel and it is bundled with the Cartesi toolchain. Hence, it
// is recommended to use the Cartesi Toolchain Docker image to build this
// example. The latest version of this header is available in the Cartesi Linux
// repository:
//
//   https://github.com/cartesi/linux/blob/linux-5.5.19-ctsi-y/include/uapi/linux/cartesi/rollup.h
//
extern "C"
{
#include <linux/cartesi/rollup.h>
}

// Path to the Cartesi Rollup device driver inside the Cartesi Machine.
#define ROLLUP_DEVICE_NAME "/dev/rollup"

// To interact with the Cartesi Rollup, it is necessary to open the respective
// device driver. Currently, only one system process can open the Rollup driver
// at a time.
static int open_rollup_device()
{
  int fd = open(ROLLUP_DEVICE_NAME, O_RDWR);
  if (fd < 0)
  {
    throw std::system_error(errno, std::generic_category(), "unable to open rollup device");
  }
  return fd;
}

static std::string get_ioctl_name(int request)
{
  switch (request)
  {
    case IOCTL_ROLLUP_WRITE_VOUCHER:
      return {"write voucher"};
    case IOCTL_ROLLUP_WRITE_NOTICE:
      return {"write notice"};
    case IOCTL_ROLLUP_WRITE_REPORT:
      return {"write report"};
    case IOCTL_ROLLUP_FINISH:
      return {"finish"};
    case IOCTL_ROLLUP_READ_ADVANCE_STATE:
      return {"advance state"};
    case IOCTL_ROLLUP_READ_INSPECT_STATE:
      return {"inspect state"};
    case IOCTL_ROLLUP_THROW_EXCEPTION:
      return {"throw exception"};
    default:
      return {"unknown"};
  }
}

// Call the ioctl system call to interact with the Cartesi Rollup device driver.
// Currently, that is the only way to interact with the driver.
static void rollup_ioctl(int fd, unsigned long request, void* data)
{
  if (ioctl(fd, request, data) < 0)
  {
    throw std::system_error(errno, std::generic_category(), "unable to " + get_ioctl_name(request));
  }
}

static std::string hex(const uint8_t* data, uint64_t length)
{
  std::stringstream ss;
  ss << "0x";
  for (auto b : std::string_view{reinterpret_cast<const char*>(data), length})
  {
    ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<unsigned>(b);
  }
  return ss.str();
}

void handle_advance(int fd, rollup_bytes payload_buffer)
{
  struct rollup_advance_state request
  {
  };
  request.payload = payload_buffer;
  rollup_ioctl(fd, IOCTL_ROLLUP_READ_ADVANCE_STATE, &request);
  auto data =
      std::string_view{reinterpret_cast<const char*>(request.payload.data), request.payload.length};
  std::cout << "[DApp] Received advance request data " << data << std::endl;
  // TODO: add application logic here
}

void handle_inspect(int fd, rollup_bytes payload_buffer)
{
  struct rollup_inspect_state request
  {
  };
  request.payload = payload_buffer;
  rollup_ioctl(fd, IOCTL_ROLLUP_READ_INSPECT_STATE, &request);
  auto data =
      std::string_view{reinterpret_cast<const char*>(request.payload.data), request.payload.length};
  std::cout << "[DApp] Received inspect request data " << data << std::endl;
  // TODO: add application logic here
}

// Below, the DApp performs a system call finishing the previous Rollup request
// and asking for the next one. The Cartesi Machine is paused until the next
// request is available. The finish call returns the type of the next request
// and the number of bytes of the payload so the code can allocate a buffer.
// After the finish call, it is necessary to perform either the read advance or
// the read inspect call to obtain the actual request payload. After receiving
// the payload, the DApp can send notices, vouchers, reports or exceptions to
// the driver. Finally, when the DApp finishes to process the request, it must
// call finish again and restart the cycle.
int main(int argc, char** argv)
try
{
  int fd = open_rollup_device();
  struct rollup_finish finish_request
  {
  };
  finish_request.accept_previous_request = true;
  std::vector<uint8_t> payload_buffer;
  while (true)
  {
    std::cout << "[DApp] Sending finish" << std::endl;
    rollup_ioctl(fd, IOCTL_ROLLUP_FINISH, &finish_request);
    auto len = static_cast<uint64_t>(finish_request.next_request_payload_length);
    std::cout << "[DApp] Received finish with payload length " << len << std::endl;
    payload_buffer.resize(len);
    if (finish_request.next_request_type == CARTESI_ROLLUP_ADVANCE_STATE)
    {
      handle_advance(fd, {payload_buffer.data(), len});
    }
    else if (finish_request.next_request_type == CARTESI_ROLLUP_INSPECT_STATE)
    {
      handle_inspect(fd, {payload_buffer.data(), len});
    }
  }
  close(fd);
  return 0;
}
catch (std::exception& e)
{
  std::cerr << "Caught exception: " << e.what() << '\n';
  return 1;
}
catch (...)
{
  std::cerr << "Caught unknown exception\n";
  return 1;
}
