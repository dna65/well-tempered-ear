#include "usb.h"

#include <fmt/core.h>

namespace usb
{

auto DeviceEntry::Open(void* event_context_user_data) const
-> tb::result<DeviceHandle, Error>
{
    libusb_device_handle* dev_handle;
    int err = libusb_open(device_, &dev_handle);
    if (err != LIBUSB_SUCCESS)
        return Error { err };

    DeviceHandle result {
        .entry { *this },
        .dev_handle { dev_handle },
        .user_data = event_context_user_data
    };

    libusb_set_auto_detach_kernel_driver(dev_handle, 1);

    if ((err = libusb_claim_interface(dev_handle, interface_index))
        != LIBUSB_SUCCESS)
        return Error { err };

    libusb_config_descriptor* cfg_desc;
    if ((err = libusb_get_active_config_descriptor(device_, &cfg_desc))
        != LIBUSB_SUCCESS)
        return Error { err };

    result.cfg_desc.reset(cfg_desc);

    libusb_transfer* transfer = libusb_alloc_transfer(0);
    if (transfer == nullptr)
        return Error { err };

    result.transfer.reset(transfer);

    result.packet_buffer.reset(static_cast<uint8_t*>(malloc(endpoint_in_packet_size)));
    if (result.packet_buffer == nullptr)
        return Error { LIBUSB_ERROR_NO_MEM };

    return result;
}

void TransferCallback(libusb_transfer* transfer)
{
    auto* handle = static_cast<DeviceHandle*>(transfer->user_data);

    handle->event_callback(transfer);

    // TODO: Allow timeout specification
    libusb_fill_bulk_transfer(transfer, handle->dev_handle.get(),
        handle->entry.endpoint_in_addr,
        handle->packet_buffer.get(), handle->entry.endpoint_in_packet_size,
        transfer->callback, handle,
        transfer->timeout);

    if (int err = libusb_submit_transfer(transfer); err != LIBUSB_SUCCESS) {
        fmt::print("Error submitting transfer: {}\n", libusb_strerror(err));
    }
}

void DeviceHandle::ReceiveBulkPackets(libusb_transfer_cb_fn cb)
{
    event_callback = cb;
    libusb_fill_bulk_transfer(transfer.get(), dev_handle.get(),
        entry.endpoint_in_addr,
        packet_buffer.get(), entry.endpoint_in_packet_size,
        TransferCallback, this,
        1000);

    if (int err = libusb_submit_transfer(transfer.get()); err != LIBUSB_SUCCESS) {
        fmt::print("Error submitting transfer: {}\n", libusb_strerror(err));
    }
}

void DeviceHandle::Close()
{
    if (dev_handle)
        libusb_release_interface(dev_handle.get(), entry.interface_index);
    dev_handle.reset();
    cfg_desc.reset();
    transfer.reset();
}

PollingContext::PollingContext()
{
    thread_ = std::thread([this] {
        while (libusb_handle_events_completed(nullptr, nullptr) == LIBUSB_SUCCESS
            && !done_) {}
    });
}

PollingContext::~PollingContext()
{
    done_ = true;
    libusb_interrupt_event_handler(nullptr);
    thread_.join();
}

auto Init() -> tb::error<Error>
{
    int err = libusb_init_context(NULL, NULL, 0);
    if (err != LIBUSB_SUCCESS)
        return Error { err };
    return tb::ok;
}

auto IndexDevices() -> tb::result<DeviceList, Error>
{
    libusb_device** devices;
    ssize_t count = libusb_get_device_list(NULL, &devices);
    if (count < 0)
        return Error { static_cast<int>(count) };

    DeviceList list(devices);
    return list;
}

auto SearchMIDIDevices(const DeviceList& list)
-> tb::result<std::vector<DeviceEntry>, Error>
{
    std::vector<DeviceEntry> result;
    int ret;
    for (libusb_device** dev = list.get(); *dev; ++dev) {
        DeviceEntry device {
            .device_ = *dev
        };

        libusb_device_descriptor dev_desc;
        if ((ret = libusb_get_device_descriptor(device.device_, &dev_desc))
            != LIBUSB_SUCCESS)
            return Error { ret };

        libusb_device_handle* dev_handle;
        if ((ret = libusb_open(device.device_, &dev_handle)) != LIBUSB_SUCCESS)
            return Error { ret };

        tb::scoped_guard close_guard = [&dev_handle] { libusb_close(dev_handle); };

        unsigned char string_buffer[64];
        int len = libusb_get_string_descriptor(dev_handle, dev_desc.iProduct,
            static_cast<uint16_t>(LangID::ENGLISH_IRELAND), string_buffer,
            sizeof(string_buffer));

        if (len < 0)
            return Error { len };

        device.product_name = std::string(reinterpret_cast<char*>(string_buffer), len);

        len = libusb_get_string_descriptor(dev_handle, dev_desc.iManufacturer,
            static_cast<uint16_t>(LangID::ENGLISH_IRELAND), string_buffer,
            sizeof(string_buffer));

        if (len < 0)
            return Error { len };

        device.manufacturer = std::string(reinterpret_cast<char*>(string_buffer), len);

        libusb_set_auto_detach_kernel_driver(dev_handle, true);

        libusb_config_descriptor* cfg_desc;
        if ((ret = libusb_get_active_config_descriptor(*dev, &cfg_desc))
            != LIBUSB_SUCCESS)
            return Error { ret };

        tb::scoped_guard cfg_guard
            = [&cfg_desc] { libusb_free_config_descriptor(cfg_desc); };

        if (dev_desc.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE
            && dev_desc.bDeviceClass != LIBUSB_CLASS_AUDIO)
            continue;

        for (int i = 0; i < cfg_desc->bNumInterfaces; ++i) {
            const libusb_interface* interface = &(cfg_desc->interface[i]);

            for (int j = 0; j < interface->num_altsetting; ++j) {
                const libusb_interface_descriptor* desc = &(interface->altsetting[j]);

                if (desc->bInterfaceClass != LIBUSB_CLASS_AUDIO
                    || static_cast<AudioSubclass>(desc->bInterfaceSubClass)
                        != AudioSubclass::MIDISTREAMING)
                    continue;

                uint16_t midistreaming_version;
                memcpy(&midistreaming_version, &desc->extra[3], 2);

                // TODO: Implement support for USB MIDI 2.0
                if (midistreaming_version != 0x100)
                    continue;

                bool found_input = false, found_output = false;
                for (int k = 0; k < desc->bNumEndpoints; ++k) {
                    const libusb_endpoint_descriptor* endpoint = &(desc->endpoint[k]);
                    device.endpoint_in_packet_size = endpoint->wMaxPacketSize;
                    if ((endpoint->bEndpointAddress & 0x80) == LIBUSB_ENDPOINT_IN) {
                        device.endpoint_in_addr = endpoint->bEndpointAddress;
                        found_input = true;
                    } else {
                        device.endpoint_out_addr = endpoint->bEndpointAddress;
                        found_output = true;
                    }
                }

                if (found_input && found_output) {
                    device.interface_index = i;
                    device.altsetting_index = j;
                    result.emplace_back(device);
                }
            }
        }
    }

    return result;
}

void Exit()
{
    libusb_exit(NULL);
}

}
