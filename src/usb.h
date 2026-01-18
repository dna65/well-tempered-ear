#pragma once

#include <tb/tb.h>

#include <memory>
#include <thread>
#include <vector>

#include <libusb-1.0/libusb.h>

namespace usb
{

enum class AudioSubclass : uint8_t
{
    SUBCLASS_UNDEFINED = 0, AUDIOCONTROL = 1, AUDIOSTREAMING = 2, MIDISTREAMING = 3
};

enum class LangID : uint16_t
{
    ENGLISH_IRELAND = 0x1809
};

struct Error
{
    int error_code;

    auto What() const -> std::string_view
    {
        return libusb_strerror(error_code);
    }
};

struct DeviceHandle;

struct DeviceEntry
{
    std::string product_name, manufacturer;
    libusb_device* device_;
    int interface_index, altsetting_index;
    uint16_t endpoint_in_packet_size;
    uint8_t endpoint_in_addr, endpoint_out_addr;

    auto Open(void* event_context_user_data) const -> tb::result<DeviceHandle, Error>;
};

using UHandle = std::unique_ptr<libusb_device_handle, tb::deleter<libusb_close>>;
using UCFGDesc = std::unique_ptr<libusb_config_descriptor,
    tb::deleter<libusb_free_config_descriptor>>;
using UTransfer = std::unique_ptr<libusb_transfer, tb::deleter<libusb_free_transfer>>;
using UDataBuffer = std::unique_ptr<uint8_t, tb::deleter<free>>;

struct DeviceHandle
{
    DeviceEntry entry;
    UHandle     dev_handle;
    UCFGDesc    cfg_desc;
    UTransfer   transfer;
    UDataBuffer packet_buffer;
    void*       user_data = nullptr;
    libusb_transfer_cb_fn event_callback = nullptr;

    void ReceiveBulkPackets(libusb_transfer_cb_fn cb);
    void Close();
};

inline void free_dev_list(libusb_device** ptr)
{
    libusb_free_device_list(ptr, true);
}

using DeviceList = std::unique_ptr<libusb_device*, tb::deleter<free_dev_list>>;

struct PollingContext
{
    std::thread thread_;
    std::atomic<bool> done_ = false;

    PollingContext();
    ~PollingContext();
};

auto Init() -> tb::error<Error>;
auto IndexDevices() -> tb::result<DeviceList, Error>;
auto SearchMIDIDevices(const DeviceList& list)
-> tb::result<std::vector<DeviceEntry>, Error>;
void Exit();

}
