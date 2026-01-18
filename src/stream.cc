#include "stream.h"

#include <tb/tb.h>

auto Stream::Skip(size_t bytes) -> tb::error<StreamError>
{
    if (fseek(file_, bytes, SEEK_CUR) == -1)
        return StreamError::FILE_ERROR;

    return tb::ok;
}

auto Stream::Position() -> tb::result<size_t, StreamError>
{
    long result = ftell(file_);
    if (result < 0)
        return StreamError::FILE_ERROR;

    return static_cast<size_t>(result);
}
